#include "MixController.h"
#include "Core/DbUtils.h"
#include "Profiles/MixProfileData.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace livemix
{

MixController::MixController() = default;
MixController::~MixController()
{
    engine.setTap (nullptr);
    capture.abort();
}

void MixController::setSession (const MixSession& s)
{
    session = s;
    prepared = false;
    stage = Stage::Setup;
    plan.reset();
}

void MixController::setInputName (int strip, const std::string& name)
{
    if (strip < 0 || strip >= int (session.inputs.size()) || name.empty()) return;
    session.inputs[size_t (strip)].name = name;
    engine.setStripName (strip, name);
}

void MixController::setInputIcon (int strip, const std::string& icon)
{
    if (strip < 0 || strip >= int (session.inputs.size())) return;
    session.inputs[size_t (strip)].icon = icon;
    engine.setStripIcon (strip, icon);
}

void MixController::setPurpose (MixPurpose p) { session.purpose = p; }
void MixController::setProfile (StyleProfileId p) { session.profile = p; }

void MixController::prepare (double sr, int maxBlockSize)
{
    capture.abort();
    engine.setTap (nullptr);
    sampleRate = sr;
    blockSize = maxBlockSize;
    engine.prepare (sr, maxBlockSize, session);
    capture.prepare (sr, engine.getGraph());
    engine.setTap (&capture);
    kept = startingPoint (session, engine.getGraph());
    atCapture = kept;
    plan.reset();
    compare = Compare::After;
    macros = MixMacroValues {};
    bypassed = false;
    tuneCount = 0;
    tuningStrip = -1;
    mixed = false;
    stage = engine.getNumStrips() > 0 ? Stage::Ready : Stage::Setup;
    prepared = true;
    engine.setOutputFeeds (outputs);        // routing survives a rebuild; it belongs to the device, not the mix
    publish();
}

MixParameters MixController::compose() const
{
    const MixParameters& base = (plan && stage == Stage::Preview) ? (compare == Compare::Before ? plan->before : plan->proposed) : kept;
    if (bypassed)
    {
        // The console feed: no processing, no fader moves, no returns. Only the listening
        // controls (mute / solo) survive, so soloing one source still works while comparing.
        auto raw = startingPoint (session, engine.getGraph());
        raw.bypassProcessing = true;
        for (int i = 0; i < raw.numStrips && i < base.numStrips; ++i)
        {
            raw.strips[size_t (i)].mute = base.strips[size_t (i)].mute;
            raw.strips[size_t (i)].solo = base.strips[size_t (i)].solo;
        }
        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            raw.buses[size_t (b)].mute = base.buses[size_t (b)].mute;
            raw.buses[size_t (b)].solo = base.buses[size_t (b)].solo;
        }
        return raw;
    }
    return MixMacros::apply (base, macros, engine.getGraph(), session.profile);
}

void MixController::setOutputFeeds (const OutputFeeds& f)
{
    outputs = f;
    outputs.count = std::max (1, std::min (int (kMaxOutputFeeds), outputs.count));
    if (prepared) engine.setOutputFeeds (outputs);
    if (onMixChanged) onMixChanged();        // the session remembers where the cue goes
}

void MixController::setBypass (bool on)
{
    if (bypassed == on) return;
    bypassed = on;
    publish();                       // the kept mix is not touched, so there is nothing to save
}

void MixController::publish()
{
    if (! prepared) return;
    running = compose();
    engine.setParameters (running);
}

// ---- TUNE MIX ----

void MixController::startTuneMix (const ListenSettings& s)
{
    startListening (s, -1);
}

// One source on its own. The listen is the same listen - every input is measured, so the
// channel is still decided in mix context - it just waits for this channel to play and
// keeps only this channel's part of the plan.
void MixController::startTuneChannel (int strip, const ListenSettings& s)
{
    if (strip < 0 || strip >= engine.getNumStrips()) return;
    startListening (s, strip);
}

void MixController::startListening (const ListenSettings& s, int strip)
{
    if (! prepared || stage == Stage::Listening || stage == Stage::Planning) return;
    if (stage == Stage::Preview) keepPlan();   // a new listen starts from what is audible now
    listen = s;
    tuningStrip = strip;
    atCapture = running;                        // the faders and gains the listen will run with
    MixCapture::Settings cs;
    cs.seconds = s.seconds;
    cs.triggerDb = s.triggerDb;
    cs.maxWaitSeconds = s.maxWaitSeconds;
    cs.triggerStrip = strip;
    capture.start (cs);
    stage = Stage::Listening;
}

std::string MixController::getTuningName() const
{
    if (tuningStrip < 0 || tuningStrip >= int (session.inputs.size())) return {};
    return session.inputs[size_t (tuningStrip)].name;
}

void MixController::abortTuneMix()
{
    if (stage != Stage::Listening && stage != Stage::Planning) return;
    capture.abort();
    tuningStrip = -1;
    stage = restingStage();
}

void MixController::poll()
{
    if (stage != Stage::Listening) return;
    const auto s = capture.getState();
    if (s == MixCapture::State::Complete)
    {
        stage = Stage::Planning;
        MixPlanContext ctx;
        ctx.session = session;
        ctx.graph = engine.getGraph();
        ctx.current = kept;
        ctx.atCapture = atCapture;
        ctx.capture = capture.getResult();
        // The listen ran with the macros applied; the plan is built on the macro-free mix, and the macros
        // stay where the user left them (50 = the plan).
        plan = MixPlanner::plan (ctx);
        // TUNE CHANNEL keeps only this channel's part of it; everything else is left exactly
        // where it is, so what is proposed is what the mix becomes when it is kept.
        const int channel = tuningStrip;
        if (channel >= 0) plan = MixPlanner::channelOnly (*plan, channel, session.profile);

        const bool heardIt = plan->valid && (channel < 0 ? plan->stripsHeard > 0
                                                         : channel < int (plan->strips.size()) && plan->strips[size_t (channel)].heard);
        if (heardIt)
        {
            ++tuneCount;
            stage = Stage::Preview;
            compare = Compare::After;
            if (onMessage) onMessage (plan->headline);
        }
        else if (channel >= 0 && plan->valid)
        {
            // The channel said nothing, but the listen still measured every input: the plan is
            // kept for what it knows (gain staging, mix health) and nothing is proposed.
            if (onMessage) onMessage (plan->headline);
            stage = restingStage();
        }
        else
        {
            if (onMessage) onMessage (plan ? plan->headline : "MIX: NO SIGNAL");
            plan.reset();
            tuningStrip = -1;
            stage = restingStage();
        }
        publish();
    }
    else if (s == MixCapture::State::Failed)
    {
        if (onMessage) onMessage ("The listen was too short to measure. Tune Mix again while the band plays.");
        stage = restingStage();
    }
}

bool MixController::busHeard (MixBus bus) const noexcept
{
    const auto& g = engine.getGraph();
    for (int i = 0; i < g.numStrips(); ++i)
        if (g.strips[size_t (i)].bus == bus && capture.stripHeard (i)) return true;
    return false;
}

std::string MixController::getStatusText() const
{
    if (bypassed) return "BYPASS: hearing the inputs as they arrive.";
    switch (stage)
    {
        case Stage::Setup:     return "Assign your inputs to begin.";
        case Stage::Ready:     return "Press TUNE MIX and have the band play normally.";
        case Stage::Listening:
            if (isWaitingForBand()) return isTuningChannel() ? "Waiting for " + getTuningName() + "..." : "Waiting for the band...";
            return "LISTENING... " + std::to_string (int (std::round (getListenProgress() * listen.seconds))) + " of " + std::to_string (int (listen.seconds)) + " s";
        case Stage::Planning:  return "Building the mix...";
        case Stage::Preview:   return plan ? plan->headline + (compare == Compare::Before ? "  (hearing BEFORE)" : "  (hearing AFTER)") : "";
        case Stage::Mixed:
        {
            const auto notes = getMixHealthNotes();
            if (notes.empty()) return "READY";
            std::string s;
            for (const auto& n : notes) s += (s.empty() ? "" : "  ") + n;
            return s;
        }
        default:               return "READY";
    }
}

// ---- Preview ----

void MixController::setCompare (Compare c)
{
    if (compare == c) return;
    compare = c;
    publish();
}

void MixController::keepPlan()
{
    if (! plan || stage != Stage::Preview) return;
    kept = plan->proposed;
    mixed = true;
    stage = Stage::Mixed;
    compare = Compare::After;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::revertPlan()
{
    if (! plan || stage != Stage::Preview) return;
    kept = plan->before;
    plan.reset();
    tuningStrip = -1;
    stage = restingStage();
    compare = Compare::After;
    publish();
    if (onMixChanged) onMixChanged();
}

// ---- Macros ----

void MixController::setMacro (MixMacro m, float value)
{
    macros.set (m, value);
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::resetMacros()
{
    macros = MixMacroValues {};
    publish();
    if (onMixChanged) onMixChanged();
}

// ---- Advanced edits ----

namespace
{
    bool validStrip (const MixParameters& p, int strip) { return strip >= 0 && strip < p.numStrips; }
}

void MixController::setStripFader (int strip, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].faderDb = clamp (db, -60.0f, 12.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].faderDb = kept.strips[size_t (strip)].faderDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripPan (int strip, float pan)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].pan = clamp (pan, -1.0f, 1.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].pan = kept.strips[size_t (strip)].pan;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripInputGain (int strip, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].inputGainDb = clamp (db, -24.0f, 24.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].inputGainDb = kept.strips[size_t (strip)].inputGainDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripMute (int strip, bool mute)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].mute = mute;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].mute = mute;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripSolo (int strip, bool solo)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].solo = solo;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].solo = solo;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripSend (int strip, FxSlot slot, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].sendDb[size_t (slot)] = db <= -60.0f ? kSilenceDb : clamp (db, -60.0f, 6.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].sendDb[size_t (slot)] = kept.strips[size_t (strip)].sendDb[size_t (slot)];
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusFader (MixBus bus, float db)
{
    kept.buses[size_t (bus)].faderDb = clamp (db, -60.0f, 12.0f);
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].faderDb = kept.buses[size_t (bus)].faderDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusMute (MixBus bus, bool mute)
{
    if (bus == MixBus::Count) return;
    kept.buses[size_t (bus)].mute = mute;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].mute = mute;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setFxReturn (float db)
{
    kept.fxReturnDb = clamp (db, -60.0f, 12.0f);
    if (plan && stage == Stage::Preview) plan->proposed.fxReturnDb = kept.fxReturnDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setFxMute (bool mute)
{
    kept.fxMute = mute;
    if (plan && stage == Stage::Preview) plan->proposed.fxMute = mute;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusSolo (MixBus bus, bool solo)
{
    if (bus == MixBus::Master || bus == MixBus::Count) return;
    kept.buses[size_t (bus)].solo = solo;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].solo = solo;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripChannel (int strip, const ChannelParameters& c)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].channel = c;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].channel = c;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusChannel (MixBus bus, const ChannelParameters& c)
{
    if (bus == MixBus::Count) return;
    kept.buses[size_t (bus)].channel = c;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].channel = c;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::clearSolos()
{
    bool changed = false;
    for (int i = 0; i < kept.numStrips; ++i)
        if (kept.strips[size_t (i)].solo) { kept.strips[size_t (i)].solo = false; changed = true; }
    for (int b = 0; b < int (MixBus::Count); ++b)
        if (kept.buses[size_t (b)].solo) { kept.buses[size_t (b)].solo = false; changed = true; }
    if (plan && stage == Stage::Preview)
    {
        for (int i = 0; i < plan->proposed.numStrips; ++i) plan->proposed.strips[size_t (i)].solo = false;
        for (int b = 0; b < int (MixBus::Count); ++b) plan->proposed.buses[size_t (b)].solo = false;
    }
    if (! changed) return;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setKept (const MixParameters& p)
{
    kept = p;
    kept.numStrips = std::min (kept.numStrips, engine.getNumStrips());
    mixed = true;
    if (stage == Stage::Ready) stage = Stage::Mixed;
    publish();
}

void MixController::restoreKept (const MixParameters& p, int tunes)
{
    setKept (p);
    tuneCount = std::max (tuneCount, tunes);
}

// ---- Gain staging ----

namespace
{
    std::string upperCase (std::string s)
    {
        for (auto& c : s) c = (char) std::toupper ((unsigned char) c);
        return s;
    }
}

MixController::InputAdvice MixController::getInputAdvice (int strip) const
{
    InputAdvice a;
    if (! plan || strip < 0 || strip >= int (plan->strips.size())) return a;
    const auto& sp = plan->strips[size_t (strip)];
    a.known = true;
    a.capturePeakDb = sp.capturePeakDb;
    a.digitalGainDb = sp.inputGainDb;

    auto sentence = [&] (Recommendation::Kind kind) -> std::string
    {
        for (const auto& r : sp.mixItems) if (r.kind == kind) return r.why;
        if (sp.tune.valid) for (const auto& r : sp.tune.report.items) if (r.kind == kind) return r.why;
        return {};
    };

    if (sp.faint)
    {
        a.level = InputAdvice::Level::Faint;
        a.headline = "CHECK THIS INPUT";
        a.detail = sentence (Recommendation::Kind::Info);
        if (a.detail.empty())
            a.detail = "Its loudest moment during the listen was too quiet to be a source that is really playing. "
                       "Check the microphone, the cable and the preamp, then TUNE MIX again.";
        return a;
    }
    if (sp.bleedOnly)
    {
        a.level = InputAdvice::Level::Bleed;
        a.headline = "HEARD AS SPILL";
        a.detail = sentence (Recommendation::Kind::Info);
        return a;
    }
    if (! sp.heard)
    {
        a.level = InputAdvice::Level::NotHeard;
        a.headline = "NOT HEARD";
        a.detail = "Nothing played on this input during the listen, so nothing was decided about it. RE-TUNE while it plays.";
        return a;
    }

    // Heard. The report is the one taken after DLIVE's own digital gain, so what is left
    // is the console preamp itself.
    const auto& report = sp.tune.report;
    a.consoleMoveDb = sp.tune.valid ? report.suggestedCaptureGainDb : 0.0f;
    const std::string health = sp.tune.valid ? report.inputHealth : std::string ("Healthy");
    auto move = [] (float db)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.0f", std::fabs (db));
        return std::string (buf);
    };

    if (health == "Clipping")
    {
        a.level = InputAdvice::Level::Clipping;
        a.headline = "CLIPPING - TURN THE PREAMP DOWN " + move (a.consoleMoveDb) + " dB";
    }
    else if (health == "Hot")
    {
        a.level = InputAdvice::Level::Hot;
        a.headline = "TURN THE PREAMP DOWN " + move (a.consoleMoveDb) + " dB";
    }
    else if (health == "Low")
    {
        a.level = InputAdvice::Level::Low;
        a.headline = "TURN THE PREAMP UP " + move (a.consoleMoveDb) + " dB";
    }
    else if (std::fabs (sp.inputGainDb) >= MixProfile::relationships (session.profile).digitalGainAdviceDb)
    {
        // The level works, but only because DLIVE moved it a long way digitally. Doing it at
        // the desk instead keeps the noise floor down, so the app says so rather than hiding it.
        const bool up = sp.inputGainDb > 0.0f;
        a.level = InputAdvice::Level::Digital;
        a.consoleMoveDb = sp.inputGainDb;
        a.headline = std::string ("PREAMP ") + (up ? "UP " : "DOWN ") + move (sp.inputGainDb) + " dB AT THE DESK";
        a.detail = std::string ("DLIVE is running ") + (up ? "+" : "-") + move (sp.inputGainDb)
                 + " dB of digital input gain to bring this source up to a level the processing can work with. "
                 + (up ? "That works, but it lifts the preamp's own noise with the source: set the gain on the desk instead, then RE-TUNE."
                       : "Set the gain on the desk instead and the converter keeps its headroom, then RE-TUNE.");
        return a;
    }
    else
    {
        a.level = InputAdvice::Level::Healthy;
        a.headline = "HEALTHY";
        a.consoleMoveDb = 0.0f;
        a.detail = std::fabs (sp.inputGainDb - sp.inputGainBeforeDb) >= 0.5f
                       ? sentence (Recommendation::Kind::CaptureGain)
                       : std::string ("This input arrives at a level the processing can work with. Nothing to do.");
        if (a.detail.empty()) a.detail = "This input arrives at a level the processing can work with. Nothing to do.";
        return a;
    }
    a.detail = sentence (Recommendation::Kind::CaptureGain);
    if (a.detail.empty())
        a.detail = "The level reaching DLIVE is outside the healthy range for this source. Set the preamp on the desk, "
                   "then TUNE MIX again: gain staging comes before anything else in the mix.";
    return a;
}

// ---- Health ----

namespace
{
    // The three things the last listen can say about an input.
    struct HealthCount { int assigned = 0, good = 0, faint = 0, silent = 0, preamp = 0; };

    HealthCount countHealth (const MixPlan& plan, float digitalGainAdviceDb)
    {
        HealthCount c;
        for (const auto& sp : plan.strips)
        {
            ++c.assigned;
            if (sp.faint) { ++c.faint; continue; }
            if (! sp.heard) { ++c.silent; continue; }
            // Heard. Healthy when the level reaching the chain is inside the profile's range AND DLIVE did not
            // have to move it a long way digitally to get there: gain staging belongs on the desk, so an input that
            // only works because of a big digital raise is not a healthy input, it is a preamp waiting to be set.
            const bool healthy = sp.bleedOnly || ! sp.tune.valid
                                 || (sp.tune.report.inputHealth == "Healthy"
                                     && std::fabs (sp.inputGainDb) < digitalGainAdviceDb);
            if (healthy) ++c.good; else ++c.preamp;
        }
        return c;
    }
}

int MixController::getMixHealthPercent() const
{
    if (! prepared || engine.getNumStrips() == 0 || ! plan) return 0;
    const HealthCount c = countHealth (*plan, MixProfile::relationships (session.profile).digitalGainAdviceDb);
    if (c.assigned == 0) return 0;
    return int (std::round (100.0f * float (c.good) / float (c.assigned)));
}

std::vector<std::string> MixController::getMixHealthNotes() const
{
    std::vector<std::string> notes;
    if (! prepared || engine.getNumStrips() == 0) return notes;
    if (! plan)
    {
        if (mixed) notes.push_back ("Mix restored from the last session. RE-TUNE when the band plays.");
        return notes;
    }
    const HealthCount c = countHealth (*plan, MixProfile::relationships (session.profile).digitalGainAdviceDb);
    auto plural = [] (int n, const char* one, const char* many) { return std::to_string (n) + " " + (n == 1 ? one : many); };
    if (c.faint > 0)  notes.push_back (plural (c.faint, "input barely reached DLIVE: check its mic and cable.", "inputs barely reached DLIVE: check their mics and cables."));
    if (c.silent > 0) notes.push_back (plural (c.silent, "input was not heard: RE-TUNE while it plays.", "inputs were not heard: RE-TUNE while they play."));
    if (c.preamp > 0)
    {
        // Gain staging is the first move, so the note names the inputs rather than counting them.
        std::string names;
        int listed = 0;
        for (int i = 0; i < int (plan->strips.size()); ++i)
        {
            const auto advice = getInputAdvice (i);
            if (advice.level != InputAdvice::Level::Low && advice.level != InputAdvice::Level::Hot
                && advice.level != InputAdvice::Level::Clipping && advice.level != InputAdvice::Level::Digital) continue;
            if (listed < 3) names += (listed == 0 ? "" : ", ") + upperCase (plan->strips[size_t (i)].name);
            ++listed;
        }
        if (listed > 3) names += " and " + std::to_string (listed - 3) + " more";
        notes.push_back ("Set the preamp for " + names + " at the console, then RE-TUNE: gain staging comes first.");
    }
    if (notes.empty() && c.good == c.assigned) notes.push_back ("Every input was heard at a healthy level.");
    return notes;
}

} // namespace livemix
