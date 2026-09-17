#pragma once
#include <array>
#include "MixSession.h"

namespace livemix
{

inline constexpr int kMaxOutputFeeds = 4;      // main, cue, and two more: enough for a service
inline constexpr int kMaxOutputs = 16;         // device output channels DLIVE will feed (8 stereo pairs)

// One stereo destination on the open output device: which pair of device channels it
// leaves by, what it carries, and how loud. A feed is *monitoring*, not mix: changing one
// never changes the mix, the plan or what an export contains.
//
// Two different devices (the interface and the built-in headphones, say) cannot be opened
// at once by CoreAudio. On macOS the answer is an Aggregate Device in Audio MIDI Setup:
// it appears here as one device with every channel, and these feeds then send the mix
// wherever you like on it.
struct OutputFeed
{
    int left = 0, right = 1;                   // device output channels; < 0 = not routed
    MixBus source = MixBus::Master;            // MASTER = the finished mix; a group bus = that group alone
    float gainDb = 0.0f;                       // -60 .. +12, monitoring only
    bool mute = false;
    bool mono = false;                         // sum to mono: a single fill speaker, a phone feed
    // The engineer's own listen instead of `source`: whatever is soloed, or the monitor's
    // own source when nothing is. This is what makes solo safe - it lands here and nowhere
    // near the broadcast. Last in the struct so older brace-initialised feeds still compile.
    bool monitor = false;

    bool routed() const noexcept { return left >= 0 || right >= 0; }
};

// What the audio thread reads. Feed 0 is the main output and always exists.
struct OutputFeeds
{
    int count = 1;
    std::array<OutputFeed, kMaxOutputFeeds> feeds {};

    static OutputFeeds mainOnly() noexcept { OutputFeeds f; f.count = 1; return f; }
};

inline const char* outputFeedSourceName (MixBus b) noexcept
{
    return b == MixBus::Master ? "Main mix" : mixBusName (b);
}

// What one feed carries, in the words the Outputs sheet prints. "My headphones" rather than
// "monitor bus": the person setting this up on a Sunday morning has headphones on their head
// and has never heard the word monitor used as a noun. The engineer's term appears once, as a
// subtitle, where an engineer looks for it.
inline const char* outputFeedSourceName (const OutputFeed& f) noexcept
{
    return f.monitor ? "My headphones" : outputFeedSourceName (f.source);
}

// The broadcast and the engineer's listen are always a real stereo pair.
//
// Not "usually" and not "unless something went wrong": a mix that reaches the stream summed to
// mono, or on one leg because a pair was half-chosen, is the kind of fault nobody notices until
// it is on the recording. So the shape is enforced at the one place feeds enter the system
// (MixController::setOutputFeeds) rather than trusted to whatever set them.
//
// The optional extra feeds keep their mono switch, because that is what it is for - a single
// fill speaker, a feed to a phone. Feed 0 and anything carrying solo do not get the choice.
inline void forceStereoPair (OutputFeed& f) noexcept
{
    if (! f.routed()) { f.left = f.right = -1; return; }
    // Half a pair means somebody chose one channel. Take the one that exists as the left of a
    // pair rather than quietly summing the mix onto it.
    if (f.left < 0) f.left = f.right - 1;
    if (f.left < 0) f.left = 0;
    f.right = f.left + 1;
    f.mono = false;
}

// Puts every feed into a shape the engine can trust. `availableChannels` = 0 means "no device
// open", in which case the pairs are fixed but nothing is dropped for being out of range - the
// routing belongs to the session and has to survive being opened without the interface present.
inline void normaliseOutputs (OutputFeeds& feeds, int availableChannels = 0) noexcept
{
    feeds.count = feeds.count < 1 ? 1 : (feeds.count > kMaxOutputFeeds ? kMaxOutputFeeds : feeds.count);
    for (int i = 0; i < feeds.count; ++i)
    {
        auto& f = feeds.feeds[size_t (i)];
        // Feed 0 is the broadcast and a monitor feed is what the engineer solos into: both are
        // always stereo. The rest may be mono on purpose.
        if (i == 0 || f.monitor) forceStereoPair (f);
        if (availableChannels > 0 && f.routed() && f.right >= availableChannels)
        {
            // The device is smaller than the routing expects. Say nothing here - the Outputs
            // sheet prints "not on this device" - but never write past the end of it.
            f.left = f.right = -1;
        }
    }
}

// Did only the engineer's own listen change between two routings? A monitor feed is not
// part of the broadcast, so moving it is always allowed - even mid-service, which is exactly
// when an engineer needs to plug headphones in somewhere else.
inline bool onlyMonitorChanged (const OutputFeeds& a, const OutputFeeds& b) noexcept
{
    auto same = [] (const OutputFeed& x, const OutputFeed& y)
    {
        return x.left == y.left && x.right == y.right && x.source == y.source
            && x.gainDb == y.gainDb && x.mute == y.mute && x.mono == y.mono && x.monitor == y.monitor;
    };
    auto clampCount = [] (int n) { return n < 1 ? 1 : (n > kMaxOutputFeeds ? kMaxOutputFeeds : n); };
    const int na = clampCount (a.count);
    const int nb = clampCount (b.count);
    const int shared = na < nb ? na : nb;
    for (int i = 0; i < shared; ++i)
    {
        const auto& x = a.feeds[size_t (i)];
        const auto& y = b.feeds[size_t (i)];
        if (same (x, y)) continue;
        if (! x.monitor || ! y.monitor) return false;      // a broadcast feed moved
    }
    // Adding or removing a feed is only a monitor change when the feed itself is one:
    // plugging headphones in somewhere mid-service is fine, adding a second broadcast is not.
    const OutputFeeds& longer = na > nb ? a : b;
    for (int i = shared; i < (na > nb ? na : nb); ++i)
        if (! longer.feeds[size_t (i)].monitor) return false;
    return true;
}

// Is the engineer's listen routed anywhere? Solo is only useful when it is, so the app
// says so rather than letting an S key do nothing audible.
inline bool hasMonitorFeed (const OutputFeeds& f) noexcept
{
    const int n = f.count < kMaxOutputFeeds ? f.count : kMaxOutputFeeds;
    for (int i = 0; i < n; ++i)
        if (f.feeds[size_t (i)].monitor && f.feeds[size_t (i)].routed() && ! f.feeds[size_t (i)].mute) return true;
    return false;
}

} // namespace livemix
