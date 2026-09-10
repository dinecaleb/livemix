#pragma once
#include <array>
#include "MixSession.h"
#include "DSP/ChannelParameters.h"
#include "FX/FxParameters.h"
#include "Core/Constants.h"

namespace livemix
{

// One strip as the audio thread sees it: the channel chain plus its place in the mix.
struct StripParameters
{
    ChannelParameters channel;
    float inputGainDb = 0.0f;                           // digital preamp before analysis and the chain (the console gain DLIVE owns)
    float faderDb = 0.0f;
    float pan = 0.0f;                                   // -1 = left .. +1 = right (balance on stereo strips)
    bool mute = false;
    bool solo = false;
    std::array<float, int (FxSlot::Count)> sendDb {};   // post-fader send level; kSilenceDb = no send

    StripParameters() { sendDb.fill (kSilenceDb); }
};

struct BusParameters
{
    ChannelParameters channel;
    float faderDb = 0.0f;
    bool mute = false;
    bool solo = false;
};

struct FxSlotParameters
{
    FxParameters fx;          // mix is forced to 1 (wet only): a return, not an insert
    float returnDb = 0.0f;
    bool enabled = false;
};

// The complete state of the mix engine, fixed capacity so it can be copied on the
// audio thread without allocating. The message thread owns the master copy and
// publishes it whole (TripleBuffer); the audio thread applies it when it changes.
struct MixParameters
{
    int numStrips = 0;
    std::array<StripParameters, kMaxStrips> strips {};
    std::array<BusParameters, int (MixBus::Count)> buses {};
    std::array<FxSlotParameters, int (FxSlot::Count)> fx {};

    // What the band is playing, so a tempo-synced delay is in time with the song. A live console has no
    // host play head; TUNE MIX measures this from the listen. 120 is the engine default, used until a
    // listen finds a tempo.
    float tempoBpm = 120.0f;

    // BEFORE: pass every strip and bus through unprocessed (faders, pans and routing
    // stay), returns are silent. The master limiter keeps its delay so latency is constant.
    bool bypassProcessing = false;

    BusParameters& master() noexcept { return buses[size_t (MixBus::Master)]; }
    const BusParameters& master() const noexcept { return buses[size_t (MixBus::Master)]; }
};

} // namespace livemix
