#pragma once
#include <array>
#include "MixSession.h"
#include "MonitorBus.h"
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
    // A return can be soloed like anything else - "what is that reverb actually doing" is a
    // question an engineer asks mid-service. It only ever reaches the monitor bus.
    bool solo = false;
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

    // The effects returns taken as one group, the way a console gives the returns their own
    // fader: an offset on every used return together, and a mute that takes the effects out of
    // the mix without disturbing the level TUNE MIX chose for each one. 0 dB and not muted is
    // "as tuned", so a session that never touched them behaves exactly as before.
    float fxReturnDb = 0.0f;
    bool fxMute = false;

    // The engineer's own listen: where solo goes, what the monitor carries, how loud it is.
    // Monitoring, never mix - nothing here changes the master, the plan or an export. It
    // rides in MixParameters rather than beside it only because solo is a per-strip flag and
    // the two have to be applied in the same breath.
    MonitorState monitor {};

    // BEFORE: pass every strip and bus through unprocessed (faders, pans and routing
    // stay), returns are silent. The master limiter keeps its delay so latency is constant.
    bool bypassProcessing = false;

    BusParameters& master() noexcept { return buses[size_t (MixBus::Master)]; }
    const BusParameters& master() const noexcept { return buses[size_t (MixBus::Master)]; }
};

// The kept mix, carried across a change to the assignments - an input reordered, dropped,
// added or re-linked. Every strip that survived keeps its chain, its input gain, its fader,
// its pan, its keys and its sends, found by the same identity the timeline uses to keep its
// clips under the right track (matchInputs). `baseline` is what the rebuilt session starts
// from, so an input that is new to it gets its role's proper starting chain rather than an
// empty one, and the buses, the master, the returns and the tempo - none of which belong to
// a single input - come across from `from` whole.
//
// Without this, moving one input on the timeline threw away a tuned mix, because a mix that
// is remembered by position cannot survive the positions changing.
inline MixParameters carryMix (const MixParameters& from, const MixSession& previous,
                               const MixParameters& baseline, const MixSession& next)
{
    MixParameters out = baseline;
    out.numStrips = next.numStrips();
    const auto match = matchInputs (previous, next);
    for (size_t n = 0; n < match.size() && int (n) < out.numStrips; ++n)
    {
        const int was = match[n];
        if (was < 0 || was >= from.numStrips) continue;
        // An input that became a different source does *not* keep its chain: a kick's gate and
        // its 60 Hz shelf are wrong on a voice, and silently carrying them across would be the
        // one case where following the input is worse than starting again. Everything else
        // about that input - and every other strip - survives.
        if (previous.inputs[size_t (was)].role != next.inputs[n].role) continue;
        out.strips[n] = from.strips[size_t (was)];
    }

    out.buses = from.buses;
    out.fx = from.fx;
    out.tempoBpm = from.tempoBpm;
    out.fxReturnDb = from.fxReturnDb;
    out.fxMute = from.fxMute;
    out.monitor = from.monitor;
    out.bypassProcessing = from.bypassProcessing;
    return out;
}

} // namespace livemix
