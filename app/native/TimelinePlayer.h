#pragma once
#include <array>
#include <atomic>
#include <vector>
#include <juce_core/juce_core.h>
#include "ClipSource.h"

namespace livemix
{

// Plays the recorded timeline back into the mix. One reader thread keeps a ring of audio
// ahead of the playhead for every track; the audio thread only copies out of that ring,
// so playback never touches a file, a lock or an allocation.
//
// The ring is addressed by a monotonic stream counter, so a loop wrap costs nothing: the
// reader produces one continuous stream and wraps at exactly the sample Transport::advance
// does, which is why the playhead and what is heard never drift apart.
class TimelinePlayer : private juce::Thread
{
public:
    using TrackClips = ClipSource::Track;

    TimelinePlayer();
    ~TimelinePlayer() override;

    // ---- message thread ----
    void prepare (double sampleRate, int maxBlockSize, const std::vector<TrackClips>& tracks);
    void release();
    bool isPrepared() const noexcept { return prepared; }

    void setLoop (bool on, juce::int64 start, juce::int64 end) noexcept;

    // Fill the ring from `position` and wait, briefly, for enough audio to start cleanly.
    void prime (juce::int64 position);

    // ---- audio thread ----
    // Consumes one block. Afterwards channel() holds `numSamples` of audio per track.
    void read (int numSamples) noexcept;
    const float* channel (int track, int ch) const noexcept;
    int numTracks() const noexcept { return int (tracks.size()); }
    int getUnderruns() const noexcept { return underruns.load (std::memory_order_relaxed); }

private:
    struct Track
    {
        int channels = 1;
        std::array<std::vector<float>, 2> ring;
        std::array<std::vector<float>, 2> block;           // what the audio thread hands out
    };

    void run() override;
    void topUp();

    ClipSource source;
    std::vector<Track> tracks;

    double rate = 48000.0;
    int maxBlock = 512;
    int fillChunk = 512;
    int ringSize = 0;                    // power of two, shared by every track
    int ringMask = 0;

    std::atomic<juce::int64> streamRead { 0 }, streamWrite { 0 };
    juce::int64 fillPosition = 0;                // the timeline sample the reader will render next
    std::atomic<juce::int64> seekTo { 0 };
    std::atomic<int> seekRequest { 0 };
    int seekHandled = 0;
    std::atomic<bool> priming { false };
    std::atomic<int> underruns { 0 };
    std::atomic<bool> looping { false };
    std::atomic<juce::int64> loopStart { 0 }, loopEnd { 0 };
    juce::WaitableEvent wake { false }, filled { false };
    bool prepared = false;
};

} // namespace livemix
