#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

namespace livemix
{

// The playhead. One position in project samples, moved by the audio thread while
// playing and read by everything else. Locating happens on the message thread.
// Nothing here allocates, locks or blocks.
class Transport
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate.store (newSampleRate > 0.0 ? newSampleRate : 48000.0, std::memory_order_relaxed);
    }
    double getSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }

    // ---- message thread ----
    void play() noexcept  { playing.store (true, std::memory_order_release); }
    void stop() noexcept  { playing.store (false, std::memory_order_release); recording.store (false, std::memory_order_release); }
    void setRecording (bool r) noexcept { recording.store (r, std::memory_order_release); }

    bool isPlaying() const noexcept   { return playing.load (std::memory_order_acquire); }
    bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }

    juce::int64 getPosition() const noexcept { return position.load (std::memory_order_acquire); }
    void setPosition (juce::int64 s) noexcept { position.store (juce::jmax ((juce::int64) 0, s), std::memory_order_release); }
    double getPositionSeconds() const noexcept { return double (getPosition()) / getSampleRate(); }
    void setPositionSeconds (double s) noexcept { setPosition ((juce::int64) (s * getSampleRate())); }

    void setLoop (bool on, juce::int64 start, juce::int64 end) noexcept
    {
        loopStart.store (juce::jmax ((juce::int64) 0, start), std::memory_order_relaxed);
        loopEnd.store (juce::jmax (start + 1, end), std::memory_order_relaxed);
        looping.store (on, std::memory_order_release);
    }
    bool isLooping() const noexcept    { return looping.load (std::memory_order_acquire); }
    juce::int64 getLoopStart() const noexcept { return loopStart.load (std::memory_order_relaxed); }
    juce::int64 getLoopEnd() const noexcept   { return loopEnd.load (std::memory_order_relaxed); }

    // Where the timeline stops on its own (the end of the recorded material). 0 = never.
    void setEnd (juce::int64 s) noexcept { endSample.store (s, std::memory_order_relaxed); }
    juce::int64 getEnd() const noexcept  { return endSample.load (std::memory_order_relaxed); }

    // ---- audio thread ----
    // Moves the playhead on by one block and returns where that block started. The loop
    // wraps to the sample, which is also how TimelinePlayer fills its stream, so what is
    // heard and what is shown never drift apart.
    juce::int64 advance (int numSamples) noexcept
    {
        const juce::int64 start = position.load (std::memory_order_relaxed);
        juce::int64 next = start + numSamples;
        if (looping.load (std::memory_order_relaxed))
        {
            const juce::int64 e = loopEnd.load (std::memory_order_relaxed);
            const juce::int64 s = loopStart.load (std::memory_order_relaxed);
            if (start >= e)          next = s + (next - start);
            else if (next > e)       next = s + (next - e);
        }
        position.store (next, std::memory_order_release);
        return start;
    }

    static juce::String formatTime (double seconds)
    {
        const bool negative = seconds < 0.0;
        if (negative) seconds = -seconds;
        const int total = (int) seconds;
        const int hours = total / 3600, minutes = (total / 60) % 60, secs = total % 60;
        const int millis = juce::jlimit (0, 999, (int) ((seconds - (double) total) * 1000.0));
        return (negative ? "-" : "") + juce::String (hours).paddedLeft ('0', 2) + ":"
             + juce::String (minutes).paddedLeft ('0', 2) + ":"
             + juce::String (secs).paddedLeft ('0', 2) + "." + juce::String (millis).paddedLeft ('0', 3);
    }

private:
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<juce::int64> position { 0 };
    std::atomic<juce::int64> loopStart { 0 }, loopEnd { 48000 }, endSample { 0 };
    std::atomic<bool> playing { false }, recording { false }, looping { false };
};

} // namespace livemix
