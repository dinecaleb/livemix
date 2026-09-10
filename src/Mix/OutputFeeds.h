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

} // namespace livemix
