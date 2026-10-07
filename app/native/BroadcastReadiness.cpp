#include "BroadcastReadiness.h"
#include <algorithm>
#include <chrono>
#include <juce_core/juce_core.h>

namespace livemix
{

namespace
{
    long long nowMs() noexcept
    {
        using clock = std::chrono::system_clock;
        return std::chrono::duration_cast<std::chrono::milliseconds> (clock::now().time_since_epoch()).count();
    }

    // Short, plain, one job each: the label says what is true when it is done, the guidance
    // says how to find out. Nothing here is proved by DINE; the person listening decides.
    constexpr ReadinessItemDef kDefs[kReadinessItemCount] = {
        { ReadinessItemId::SourcesMapped,          ReadinessGroup::SignalAndRouting, "Every source is on the right channel",
          "Speak into each mic and play each instrument; the right strip moves.", false },
        { ReadinessItemId::InputLevels,            ReadinessGroup::SignalAndRouting, "Input levels are healthy",
          "Nothing clips, nothing is too quiet, and there is room for the loud moments.", false },
        { ReadinessItemId::BroadcastOutput,        ReadinessGroup::SignalAndRouting, "The stream receives DINE's mix",
          "Check the meter in the encoder or streaming app - DINE's own meter cannot see it.", false },

        { ReadinessItemId::SpeechClear,            ReadinessGroup::BroadcastMix, "Speech is clear",
          "Every word of the pastor or host is easy to follow, quiet and loud.", false },
        { ReadinessItemId::LeadVocals,             ReadinessGroup::BroadcastMix, "The lead vocal sits on top",
          "In a full-band song the lead is clear and nothing covers it.", false },
        { ReadinessItemId::InstrumentsBalanced,    ReadinessGroup::BroadcastMix, "The band is balanced",
          "No instrument or singer sticks out or disappears; the low end is not boomy.", false },
        { ReadinessItemId::DynamicsNatural,        ReadinessGroup::BroadcastMix, "Nothing is cut off or pumping",
          "Words and notes are not chopped, and quiet moments are still heard.", false },
        { ReadinessItemId::RoomSound,              ReadinessGroup::BroadcastMix, "The room mics help",
          "The congregation adds life without muddying speech or music.", true },
        { ReadinessItemId::OutputLevel,            ReadinessGroup::BroadcastMix, "Loudness and peaks are right",
          "Loud enough for the platform, with no clipping and no heavy limiting.", false },

        { ReadinessItemId::NoUnintendedSoloMute,   ReadinessGroup::FinalStreamCheck, "Nothing is muted or soloed by mistake",
          "Every source that should be in the stream is in it.", false },
        { ReadinessItemId::StreamListened,         ReadinessGroup::FinalStreamCheck, "Someone listened to the stream itself",
          "On headphones, from the stream or its preview - not from DINE.", false },
        { ReadinessItemId::AvSync,                 ReadinessGroup::FinalStreamCheck, "Sound and picture line up",
          "Clap or speak on camera and watch the stream: lips and sound together.", false },
        { ReadinessItemId::MusicSpeechTransitions, ReadinessGroup::FinalStreamCheck, "Talking over music works",
          "Speech stays clear when the band plays under it, with no level jumps.", false },
        { ReadinessItemId::RecoverySnapshot,       ReadinessGroup::FinalStreamCheck, "This mix is saved as a favourite",
          "Mix history > Mark this mix as a favourite, so it can be put back.", false },
        { ReadinessItemId::RecordingConfirmed,     ReadinessGroup::FinalStreamCheck, "The recording is running",
          "If it is recorded, the recorder is rolling and its meters move.", true },
    };

    // Which active checks to ask the operator to look at again after a change.
    void flagItems (ReadinessRecord& r, std::initializer_list<ReadinessItemId> ids)
    {
        for (auto id : ids)
        {
            auto& item = r.items[size_t (id)];
            if (item.status == ReadinessStatus::Pending || item.status == ReadinessStatus::NotNeeded)
                continue;
            item.needsReview = true;
        }
    }
}

const ReadinessItemDef& readinessItemDef (ReadinessItemId id) noexcept
{
    const int i = int (id);
    if (i < 0 || i >= kReadinessItemCount) return kDefs[0];
    return kDefs[i];
}

const char* readinessGroupName (ReadinessGroup g) noexcept
{
    switch (g)
    {
        case ReadinessGroup::SignalAndRouting: return "Signal and routing";
        case ReadinessGroup::BroadcastMix:      return "Broadcast mix";
        case ReadinessGroup::FinalStreamCheck:  return "Before going live";
    }
    return "Broadcast readiness";
}

const char* readinessStatusName (ReadinessStatus s) noexcept
{
    switch (s)
    {
        case ReadinessStatus::Pending:        return "To do";
        case ReadinessStatus::Checked:        return "Done";
        case ReadinessStatus::NeedsAttention: return "Problem";
        case ReadinessStatus::NotNeeded:      return "Skipped";
    }
    return "To do";
}

std::string ReadinessProgress::summaryLine() const
{
    // ASCII separators only here: this string is stored / shown from native code without Glyph.
    juce::String s;
    s << checked << " of " << applicable << " done";
    if (needsAttention > 0) s << ", " << needsAttention << (needsAttention == 1 ? " problem" : " problems");
    if (needsReview > 0) s << ", " << needsReview << " to check again";
    if (notNeeded > 0) s << ", " << notNeeded << " skipped";
    return s.toStdString();
}

ReadinessProgress ReadinessRecord::progress() const noexcept
{
    ReadinessProgress p;
    for (int i = 0; i < kReadinessItemCount; ++i)
    {
        const auto& it = items[size_t (i)];
        if (it.needsReview) ++p.needsReview;
        switch (it.status)
        {
            case ReadinessStatus::NotNeeded:
                ++p.notNeeded;
                break;
            case ReadinessStatus::Checked:
                ++p.checked;
                ++p.applicable;
                break;
            case ReadinessStatus::NeedsAttention:
                ++p.needsAttention;
                ++p.applicable;
                break;
            case ReadinessStatus::Pending:
                ++p.pending;
                ++p.applicable;
                break;
        }
    }
    return p;
}

ReadinessRecord makeFreshReadinessRecord (const std::string& name, const std::string& operatorName)
{
    ReadinessRecord r;
    r.id = juce::Uuid().toDashedString().toStdString();
    r.name = name.empty() ? "Broadcast" : name;
    r.startedMs = nowMs();
    r.operatorName = operatorName;
    r.finished = false;
    r.finishedMs = 0;
    r.items = {};
    return r;
}

void BroadcastReadiness::ensureActive()
{
    if (! active.id.empty()) return;
    active = makeFreshReadinessRecord();
}

ReadinessRecord BroadcastReadiness::newService (const std::string& name, const std::string& operatorName)
{
    ensureActive();
    // Only archive when there is something worth keeping: a blank checklist is not a service.
    if (active.finished || active.hasWork())
    {
        if (! active.finished)
        {
            active.finished = true;
            active.finishedMs = nowMs();
        }
        bool already = false;
        for (const auto& h : history)
            if (h.id == active.id) { already = true; break; }
        if (! already) history.insert (history.begin(), active);
    }
    active = makeFreshReadinessRecord (name.empty() ? "Broadcast" : name, operatorName);
    rememberOperator (operatorName);
    return active;
}

void BroadcastReadiness::finishActive()
{
    ensureActive();
    active.finished = true;
    active.finishedMs = nowMs();
    rememberOperator (active.operatorName);
    // Replace an existing history entry with the same id, else push newest-first.
    for (size_t i = 0; i < history.size(); ++i)
    {
        if (history[i].id == active.id)
        {
            history[i] = active;
            return;
        }
    }
    history.insert (history.begin(), active);
}

void BroadcastReadiness::resetActiveStatuses()
{
    ensureActive();
    for (auto& it : active.items)
    {
        it.status = ReadinessStatus::Pending;
        it.confirmedMs = 0;
        it.needsReview = false;
        // notes preserved
    }
    active.finished = false;
    active.finishedMs = 0;
}

bool ReadinessRecord::hasWork() const noexcept
{
    for (const auto& it : items)
        if (it.status != ReadinessStatus::Pending || ! it.note.empty()) return true;
    return false;
}

void setReadinessItem (ReadinessRecord& r, ReadinessItemId id, ReadinessStatus status)
{
    const int i = int (id);
    if (i < 0 || i >= kReadinessItemCount) return;
    auto& it = r.items[size_t (i)];
    if (! readinessItemDef (id).allowsNotNeeded && status == ReadinessStatus::NotNeeded)
        status = ReadinessStatus::Pending;
    it.status = status;
    it.confirmedMs = status == ReadinessStatus::Pending ? 0 : nowMs();
    it.needsReview = false;
}

void BroadcastReadiness::setItem (ReadinessItemId id, ReadinessStatus status, const std::string& note)
{
    ensureActive();
    if (active.finished) return;   // a finished service is read-only until it is reopened
    setReadinessItem (active, id, status);
    if (! note.empty()) active.items[size_t (id)].note = note;
}

void BroadcastReadiness::reopenActive()
{
    ensureActive();
    active.finished = false;       // the history copy stays until Finish replaces it
    active.finishedMs = 0;
}

void BroadcastReadiness::setItemNote (ReadinessItemId id, const std::string& note)
{
    ensureActive();
    if (active.finished) return;
    const int i = int (id);
    if (i < 0 || i >= kReadinessItemCount) return;
    active.items[size_t (i)].note = note;
}

void BroadcastReadiness::setActiveName (const std::string& name)
{
    ensureActive();
    if (! name.empty()) active.name = name;
}

void BroadcastReadiness::setActiveOperator (const std::string& name)
{
    ensureActive();
    active.operatorName = name;
    rememberOperator (name);
}

void BroadcastReadiness::flagForReview (ReadinessChange change)
{
    if (active.id.empty() || active.finished) return;
    switch (change)
    {
        case ReadinessChange::InputMapping:
            flagItems (active, { ReadinessItemId::SourcesMapped });
            break;
        case ReadinessChange::BroadcastRouting:
            flagItems (active, { ReadinessItemId::BroadcastOutput, ReadinessItemId::NoUnintendedSoloMute });
            break;
        case ReadinessChange::BroadcastDevice:
            flagItems (active, { ReadinessItemId::SourcesMapped, ReadinessItemId::BroadcastOutput });
            break;
    }
}

void BroadcastReadiness::rememberOperator (const std::string& name)
{
    if (name.empty()) return;
    for (const auto& o : knownOperators)
        if (o == name) return;
    knownOperators.push_back (name);
    if (knownOperators.size() > 24) knownOperators.erase (knownOperators.begin());
}

ReadinessRecord* BroadcastReadiness::historyMutable (const std::string& id)
{
    for (auto& r : history)
        if (r.id == id) return &r;
    if (active.id == id) return &active;
    return nullptr;
}

const ReadinessRecord* BroadcastReadiness::historyFind (const std::string& id) const noexcept
{
    for (const auto& r : history)
        if (r.id == id) return &r;
    if (active.id == id) return &active;
    return nullptr;
}

std::vector<const ReadinessRecord*> BroadcastReadiness::filterHistory (long long fromMs, long long toMs,
                                                                       const std::string& operatorFilter) const
{
    std::vector<const ReadinessRecord*> out;
    auto consider = [&] (const ReadinessRecord& r)
    {
        if (! r.finished) return;
        const long long when = r.finishedMs > 0 ? r.finishedMs : r.startedMs;
        if (fromMs > 0 && when < fromMs) return;
        if (toMs > 0 && when > toMs) return;
        if (! operatorFilter.empty() && r.operatorName != operatorFilter) return;
        out.push_back (&r);
    };
    for (const auto& r : history) consider (r);
    return out;
}

ReadinessSummary BroadcastReadiness::summarise (long long fromMs, long long toMs,
                                                const std::string& operatorFilter) const
{
    ReadinessSummary s;
    for (const auto* r : filterHistory (fromMs, toMs, operatorFilter))
    {
        ++s.broadcasts;
        const auto p = r->progress();
        s.checked += p.checked;
        s.applicable += p.applicable;
        for (int i = 0; i < kReadinessItemCount; ++i)
        {
            const auto st = r->items[size_t (i)].status;
            if (st == ReadinessStatus::Pending) ++s.pendingCount[size_t (i)];
            if (st == ReadinessStatus::NeedsAttention) ++s.attentionCount[size_t (i)];
            if (! r->items[size_t (i)].note.empty() && s.relatedNotes.size() < 12)
            {
                juce::String line;
                line << juce::String (r->name) << ": " << juce::String (r->items[size_t (i)].note);
                s.relatedNotes.push_back (line.toStdString());
            }
        }
    }
    return s;
}

bool readinessEquals (const BroadcastReadiness& a, const BroadcastReadiness& b) noexcept
{
    auto sameRecord = [] (const ReadinessRecord& x, const ReadinessRecord& y)
    {
        if (x.id != y.id || x.name != y.name || x.operatorName != y.operatorName) return false;
        if (x.finished != y.finished || x.startedMs != y.startedMs || x.finishedMs != y.finishedMs) return false;
        for (int i = 0; i < kReadinessItemCount; ++i)
        {
            if (x.items[size_t (i)].status != y.items[size_t (i)].status) return false;
            if (x.items[size_t (i)].note != y.items[size_t (i)].note) return false;
            if (x.items[size_t (i)].needsReview != y.items[size_t (i)].needsReview) return false;
        }
        return true;
    };
    if (! sameRecord (a.active, b.active)) return false;
    if (a.history.size() != b.history.size()) return false;
    for (size_t i = 0; i < a.history.size(); ++i)
        if (! sameRecord (a.history[i], b.history[i])) return false;
    return a.knownOperators == b.knownOperators;
}

} // namespace livemix
