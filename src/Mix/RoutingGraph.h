#pragma once
#include <array>
#include <string>
#include <vector>
#include "MixSession.h"
#include "MixParameters.h"
#include "FX/FxParameters.h"

namespace livemix
{

// One assigned input as a strip in the graph.
struct StripRoute
{
    int input = -1;                                     // index into MixSession::inputs
    std::string name;
    ChannelRole role = ChannelRole::KickIn;
    int inputA = -1, inputB = -1;                       // device channels
    MixBus bus = MixBus::Music;
    float pan = 0.0f;
    std::array<float, int (FxSlot::Count)> sendDb {};   // kSilenceDb = none

    int numChannels() const noexcept { return inputB >= 0 ? 2 : 1; }
};

// The routing DINELIVE builds from the assignments. Deterministic: the same
// session always gives the same graph. The user never edits this directly;
// Advanced mode may later expose the sends.
struct RoutingGraph
{
    std::vector<StripRoute> strips;                      // at most kMaxStrips
    std::array<bool, int (MixBus::Count)> busUsed {};    // Master is always used
    std::array<bool, int (FxSlot::Count)> fxUsed {};     // a return exists when at least one strip sends to it
    std::array<FxType, int (FxSlot::Count)> fxType {};

    static RoutingGraph build (const MixSession& session);

    int numStrips() const noexcept { return int (strips.size()); }
    int stripsOnBus (MixBus b) const noexcept;
    std::string describe() const;                        // "Kick -> DRUMS", "Lead Vocal -> VOCALS (Vocal Plate -10 dB, ...)"
};

// The mix before Tune: every strip on its role's profile baseline, buses and
// returns on theirs, faders at 0, routing as built. This is what BEFORE means
// and what Tune Mix starts from.
MixParameters startingPoint (const MixSession& session, const RoutingGraph& graph);

} // namespace livemix
