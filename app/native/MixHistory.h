#pragma once
#include <algorithm>
#include <cmath>
#include <string>
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

// A scene: the whole mix as it was kept for one part of the service - the band, the pastor,
// the choir - recalled in one press. It holds the kept mix and the macros, and the names of
// the inputs it was kept with, so it is recalled onto the same console and refused, with a
// sentence, onto a different one. Four slots with plain names; saved with the session.
struct MixScene
{
    std::string name;
    bool kept = false;
    MixParameters mix;
    MixMacroValues macros;
    std::vector<std::string> inputs;     // the session's input names when it was kept
};
inline constexpr int kMixScenes = 4;
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
    const auto match = matchInputs (previous, next);
    for (size_t n = 0; n < match.size(); ++n)
    {
        const int was = match[n];
        if (was < 0 || was >= int (previous.inputs.size())) continue;
        if (previous.inputs[size_t (was)].role != next.inputs[n].role) continue;
        for (const auto& r : from)
            if (r.strip == was)
            {
                out.push_back (r);
                out.back().strip = int (n);
            }
    }
    return out;
}

} // namespace livemix
