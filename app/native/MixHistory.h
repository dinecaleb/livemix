#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "Mix/MixParameters.h"
#include "Mix/MixSession.h"
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
