#pragma once
#include <map>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "Mix/MixParameters.h"
#include "Mix/MixSession.h"
#include "Mix/MixMacros.h"
#include "Tune/TuneTypes.h"

namespace livemix
{

// One channel's tuning history: every time a tune landed on a strip, or its chain was edited
// by hand, remembered as what the strip was and what it became, with the sentence that says
// what did it. The engineer reads what changed to the lead vocal over the morning and puts
// any earlier setting back. Mute, solo and the link are not part of a setting: they are keys,
// not tuning, and a "put back" never touches them.
//
// This is a record, not a mechanism: the kept mix is still the one truth, UNDO / REDO still
// work on the whole mix, and a put-back is an ordinary mix change that is remembered here too.
struct StripTuneRecord
{
    int strip = -1;
    std::string what;        // "TUNE MIX", "TUNE CHANNEL", "TUNE LIVE MIX", "Mix Buddy: ...", "Inspector edit", "Put back: ..."
    int tune = 0;            // the TUNE count it belongs to (0 for a hand edit made before any tune)
    long long whenMs = 0;    // wall clock, milliseconds since the epoch; 0 when unknown
    StripParameters before;  // the setting it replaced
    StripParameters after;   // the setting it made - what "put back" restores
};

// The part of a strip that a tune decides: the chain, the digital preamp, the level, the
// pan and the sends. True when any of it differs enough to hear.
inline bool stripTuneDiffers (const StripParameters& a, const StripParameters& b) noexcept
{
    if (std::fabs (a.inputGainDb - b.inputGainDb) >= 0.05f) return true;
    if (std::fabs (a.faderDb - b.faderDb) >= 0.05f) return true;
    if (std::fabs (a.pan - b.pan) >= 0.005f) return true;
    for (size_t f = 0; f < a.sendDb.size(); ++f)
        if (std::fabs (a.sendDb[f] - b.sendDb[f]) >= 0.05f) return true;
    return ! diffParameters (a.channel, b.channel).empty();
}

// ---------------------------------------------------------------------------
// WHAT A MIX SOUNDED LIKE, measured
//
// A mix is a set of fader positions and a set of chains, and neither of those is what
// somebody means when they say they liked it. What they liked is where things *landed*: the
// lead over the band, the backing under the lead, the kit against the bass, how loud the
// master was and how much of it was peaks. Those are relationships, and a relationship is
// measured from the listen - never from a fader, because a fader at -6 means nothing without
// knowing what arrived at it.
//
// So a favourite mix carries this beside its parameters: the RelationshipEngine's own metrics
// under their own stable names, where each group landed against the master, and what the
// master itself measured. It is what makes a favourite something a later mix can be aimed at
// rather than only something that can be recalled.
struct MixFingerprint
{
    static constexpr int kVersion = 1;
    int version = kVersion;
    bool valid = false;

    // The RelationshipEngine's measurements, keyed on its own stable metric names
    // ("sub_overlap_db", "lead_over_band_db", ...). A name it does not know is ignored, so an
    // older favourite opens in a later build without pretending to know more than it does.
    std::vector<std::pair<std::string, float>> metrics;

    // Where each group landed relative to the master, dB - measured at the master's input,
    // not read off a fader.
    std::array<float, int (MixBus::Count)> busBelowMasterDb {};
    std::array<bool, int (MixBus::Count)> busMeasured {};

    // The master itself: how loud it was, how much of it was peaks, how wide it was, and its
    // tonal balance - the same numbers a ReferenceProfile carries, so a favourite can be
    // aimed at exactly the way a record is.
    float masterLufs = -120.0f;
    float masterCrestDb = 0.0f;
    float masterTruePeakDb = -120.0f;
    float masterCorrelation = 1.0f;
    std::array<float, int (Band::Count)> masterBandDb {};

    float metric (const std::string& name, float fallback = 0.0f) const noexcept
    {
        for (const auto& m : metrics) if (m.first == name) return m.second;
        return fallback;
    }
};

// A scene: the whole mix as it was kept for one part of the service - the band, the pastor,
// the choir - recalled in one press. It holds the kept mix and the macros, and the names of
// the inputs it was kept with, so it is recalled onto the same console and refused, with a
// sentence, onto a different one. Four slots with plain names; saved with the session.
//
// A FAVOURITE IS A SCENE THAT WAS ALSO MEASURED (2026-09-28). Rather than a third store beside
// the scenes and the reference, the scene list simply grows: slots 0..kMixScenes-1 are the
// four named parts of a service that LIVE's pads recall, and everything after them is a
// favourite - named by the engineer, carrying a `MixFingerprint` of what it sounded like, and
// aimable at through the same ReferenceMix machinery a record goes through.
struct MixScene
{
    std::string name;
    bool kept = false;
    MixParameters mix;
    MixMacroValues macros;
    std::vector<std::string> inputs;     // the session's input names when it was kept
    bool favourite = false;              // one of the list, rather than one of the four slots
    long long whenMs = 0;                // wall clock when it was marked; 0 when unknown
    MixFingerprint sound;                // what it actually sounded like, measured
};
inline constexpr int kMixScenes = 4;

// ---------------------------------------------------------------------------
// THE SETLIST: the service in order (v4's prototype). A cue is a moment in it - a song, the
// welcome, the sermon - and what is happening in it: who is on, and for each who is on,
// Softer, Normal or Up front. Everyone the cue switches off is muted when it starts. "Who"
// is the session's sources by kind (the lead singer, the backing singers, the drums, the
// keys...) and each speaking microphone by name (MixController::cueUnits). A cue may also
// start from a kept scene or favourite; without one it starts from the mix as it is. Going
// to a cue is one Mix history entry.
// ---------------------------------------------------------------------------
enum class CueKind : int { Band = 0, Speaking, QuietMoment, MusicPlayback, Count };
inline const char* cueKindName (CueKind k) noexcept
{
    switch (k) { case CueKind::Band: return "Band"; case CueKind::Speaking: return "Speaking";
                 case CueKind::QuietMoment: return "Quiet moment"; case CueKind::MusicPlayback: return "Music playback";
                 default: return "Band"; }
}
// Who is on, and how: off (muted), softer, normal or up front.
enum class CueLevel : int { Off = -2, Softer = -1, Normal = 0, UpFront = 1 };
struct Cue
{
    std::string name;
    CueKind kind = CueKind::Band;
    int scene = -1;                // start from one of the four scene slots; -1 = from the mix as it is
    std::string favourite;         // or from a kept favourite, by name
    std::map<std::string, int> who;   // unit key -> CueLevel; a unit not here is on, at Normal
    CueLevel levelOf (const std::string& unit) const
    {
        const auto it = who.find (unit);
        return it == who.end() ? CueLevel::Normal : CueLevel (it->second);
    }
    bool operator== (const Cue& o) const
    {
        return name == o.name && kind == o.kind && scene == o.scene && favourite == o.favourite && who == o.who;
    }
};
struct Setlist
{
    std::vector<Cue> cues;
    int current = -1;              // the cue that is on now; -1 before the first
    bool operator== (const Setlist& o) const { return cues == o.cues && current == o.current; }
    bool operator!= (const Setlist& o) const { return ! (*this == o); }
    int next() const noexcept { return current + 1 < int (cues.size()) ? current + 1 : -1; }
};
inline const char* defaultSceneName (int slot) noexcept
{
    switch (slot) { case 0: return "Band"; case 1: return "Speech"; case 2: return "Worship"; default: return "Custom"; }
}

// ---------------------------------------------------------------------------
// MIX HISTORY: the whole mix as it was at a moment worth coming back to.
//
// UNDO and REDO are for the last thing you did, they are fine-grained, and they live and die
// with the graph. This is the other kind of going back: it survives quitting, it is a list
// rather than a stack, and it is written in the words of what happened - "TUNE MIX",
// "Scene: Sermon", "Put back on Lead Vocal" - so an engineer on Monday can find the mix the
// service actually went out on.
//
// A checkpoint is taken at every milestone (MixController::mark), and on a slow beat while the
// mix is being worked on, so a long morning of small moves is not one undo step wide. Going
// back to one is itself a checkpoint, so it can be undone like anything else.
//
// It is bounded by what it costs rather than by how many there are: a 64-channel console's
// checkpoint is three times a 21-channel one's, and the session document has to stay a file
// that opens quickly. When the budget is passed the oldest goes - except that a checkpoint
// made by a tune outlives the hand edits around it, because that is the one people ask for.
// ---------------------------------------------------------------------------
struct MixCheckpoint
{
    long long whenMs = 0;              // wall clock, milliseconds since the epoch
    std::string what;                  // the sentence: "TUNE MIX", "Scene: Sermon", "Before reset"
    bool fromTune = false;             // kept longest when the list is pruned
    int tuneCount = 0;                 // which TUNE the mix was on
    MixParameters mix;                 // the kept mix, without macros
    MixMacroValues macros;
    std::vector<std::string> inputs;   // the session's input names when it was taken
};

// How much history a session carries, measured in strip snapshots rather than in entries: one
// checkpoint of a 21-input console costs 21, one of a 64-input console costs 64. Roughly two
// and a half thousand strips of history is a few megabytes of document, which still opens
// instantly and still autosaves inside its two seconds.
inline constexpr int kCheckpointStripBudget = 800;
inline constexpr int kMaxCheckpoints = 100;          // for a tiny session, where the budget never bites

// Drops the oldest until the list fits. A tune's checkpoint is passed over while any hand edit
// is still there to drop instead: "put it back to how TUNE MIX left it" is the request this
// list exists to answer.
inline void pruneCheckpoints (std::vector<MixCheckpoint>& list)
{
    auto cost = [&list]
    {
        int n = 0;
        for (const auto& c : list) n += std::max (1, c.mix.numStrips);
        return n;
    };
    while (list.size() > 1 && (int (list.size()) > kMaxCheckpoints || cost() > kCheckpointStripBudget))
    {
        auto oldest = list.end();
        for (auto it = list.begin(); it + 1 != list.end(); ++it)
            if (! it->fromTune) { oldest = it; break; }
        list.erase (oldest != list.end() ? oldest : list.begin());
    }
}

// The records carried onto a rebuilt session: each one follows its input by the identity
// matchInputs uses for everything else, and one whose input became a different source is
// dropped with the chain it described (carryMix drops that chain for the same reason).
inline std::vector<StripTuneRecord> carryStripHistory (const std::vector<StripTuneRecord>& from,
                                                       const MixSession& previous, const MixSession& next)
{
    std::vector<StripTuneRecord> out;
    // A record is kept against a strip, and the match is between inputs (see carryMix).
    const auto match = matchInputs (previous, next);
    const auto stripWas = stripsOfInputs (previous);
    const auto stripNow = stripsOfInputs (next);
    for (size_t n = 0; n < match.size(); ++n)
    {
        const int was = match[n];
        if (was < 0 || was >= int (previous.inputs.size())) continue;
        if (previous.inputs[size_t (was)].role != next.inputs[n].role) continue;
        if (stripWas[size_t (was)] < 0 || stripNow[n] < 0) continue;
        for (const auto& r : from)
            if (r.strip == stripWas[size_t (was)])
            {
                out.push_back (r);
                out.back().strip = stripNow[n];
            }
    }
    return out;
}

} // namespace livemix
