#pragma once
#include <string>
#include "MixSession.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// THE MONITOR (SOLO) BUS
//
// A live console has two outputs that are not the same thing: what the room and the
// broadcast hear, and what the engineer hears. DLIVE keeps them apart. Pressing S on a
// channel puts that channel into the *monitor* bus - headphones, a pair of nearfields,
// whatever the monitor feed is routed to - and the master carries on untouched. That is
// the whole point: an engineer has to be able to find the buzz on channel 9 during the
// sermon without the congregation hearing them look for it.
//
// The one exception is deliberate, named and never the default: SoloMode::InPlace is the
// old destructive behaviour (mute-everything-else on the main mix), which is right when
// DLIVE is being used to mix a recording with nobody listening. The UI says which is on.
// ---------------------------------------------------------------------------

enum class SoloMode : int
{
    Monitor = 0,   // solo feeds the monitor bus; the live/broadcast output never changes
    InPlace,       // solo mutes everything else on the main mix (recording / rehearsal only)
    Count
};

inline constexpr const char* soloModeName (SoloMode m) noexcept
{
    return m == SoloMode::InPlace ? "SOLO IN PLACE" : "MONITOR SOLO";
}

// Where the monitor taps a soloed source.
//   PFL - pre-fader: the channel as it is, whatever its fader and mute are doing. This is
//         the troubleshooting listen ("is there signal at all, and what does it sound like").
//   AFL - after fader: the channel at its place in the mix, panned, and silent if it is
//         muted. This is the "how does it sit" listen.
enum class SoloPoint : int { AFL = 0, PFL, Count };

inline constexpr const char* soloPointName (SoloPoint p) noexcept { return p == SoloPoint::PFL ? "PFL" : "AFL"; }

inline constexpr const char* soloPointDescription (SoloPoint p) noexcept
{
    return p == SoloPoint::PFL
        ? "Pre-fader: the channel as it arrives, whatever its fader and mute are doing. For finding a problem."
        : "After fader: the channel at its place in the mix, panned, silent when it is muted. For judging balance.";
}

// The engineer's own listen. None of this is ever part of the mix, the plan, a macro or an
// export: it is monitoring, exactly like OutputFeeds, and it lives here for the same reason.
struct MonitorState
{
    SoloMode mode = SoloMode::Monitor;
    SoloPoint point = SoloPoint::AFL;
    float gainDb = 0.0f;               // monitor level, -60 .. +12
    bool mute = false;
    bool dim = false;                  // talk to someone without pulling the monitor level down
    float dimDb = -18.0f;
    // What the monitor carries when nothing is soloed: normally the finished mix, so the
    // headphones are useful before anybody presses S.
    MixBus source = MixBus::Master;

    float effectiveGainDb() const noexcept { return gainDb + (dim ? dimDb : 0.0f); }
};

} // namespace livemix
