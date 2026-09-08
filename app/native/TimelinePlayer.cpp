#include "TimelinePlayer.h"

namespace livemix
{

namespace
{
    constexpr double kRingSeconds = 2.0;
    constexpr int kPollMs = 5;

    int nextPowerOfTwo (int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }
}

TimelinePlayer::TimelinePlayer() : juce::Thread ("DINELIVE timeline") {}

TimelinePlayer::~TimelinePlayer()
{
    release();
}

void TimelinePlayer::prepare (double sampleRate, int maxBlockSize, const std::vector<TrackClips>& clipTracks)
{
    release();

    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (32, maxBlockSize);
    fillChunk = maxBlock * 4;
    ringSize = nextPowerOfTwo (juce::jmax (int (rate * kRingSeconds), maxBlock * 8));
    ringMask = ringSize - 1;

    source.prepare (rate, fillChunk, clipTracks);

    tracks.resize (clipTracks.size());
    for (size_t i = 0; i < clipTracks.size(); ++i)
    {
        auto& t = tracks[i];
        t.channels = juce::jlimit (1, 2, clipTracks[i].channels);
        for (int ch = 0; ch < t.channels; ++ch)
        {
            t.ring[size_t (ch)].assign (size_t (ringSize), 0.0f);
            t.block[size_t (ch)].assign (size_t (maxBlock), 0.0f);
        }
    }

    streamRead.store (0);
    streamWrite.store (0);
    fillPosition = 0;
    seekHandled = seekRequest.load();
    underruns.store (0);
    prepared = ! tracks.empty();
    if (prepared) startThread (juce::Thread::Priority::normal);
}

void TimelinePlayer::release()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (2000);
    prepared = false;
    tracks.clear();
    source.release();
}

void TimelinePlayer::setLoop (bool on, juce::int64 start, juce::int64 end) noexcept
{
    loopStart.store (juce::jmax ((juce::int64) 0, start), std::memory_order_relaxed);
    loopEnd.store (juce::jmax (start + 1, end), std::memory_order_relaxed);
    looping.store (on, std::memory_order_release);
}

void TimelinePlayer::prime (juce::int64 position)
{
    if (! prepared) return;
    priming.store (true, std::memory_order_release);
    seekTo.store (juce::jmax ((juce::int64) 0, position), std::memory_order_release);
    seekRequest.fetch_add (1, std::memory_order_acq_rel);
    filled.reset();
    wake.signal();
    filled.wait (250);           // plenty for an SSD; a slow disk simply starts a fraction quieter
    priming.store (false, std::memory_order_release);
}

const float* TimelinePlayer::channel (int track, int ch) const noexcept
{
    if (track < 0 || track >= int (tracks.size())) return nullptr;
    const auto& t = tracks[size_t (track)];
    return (ch >= 0 && ch < t.channels) ? t.block[size_t (ch)].data() : nullptr;
}

void TimelinePlayer::read (int numSamples) noexcept
{
    if (! prepared || numSamples <= 0) return;
    const int n = juce::jmin (numSamples, maxBlock);

    if (priming.load (std::memory_order_acquire))
    {
        for (auto& t : tracks)
            for (int ch = 0; ch < t.channels; ++ch)
                juce::FloatVectorOperations::clear (t.block[size_t (ch)].data(), n);
        return;
    }

    const juce::int64 rp = streamRead.load (std::memory_order_relaxed);
    const juce::int64 available = streamWrite.load (std::memory_order_acquire) - rp;
    const int have = int (juce::jlimit ((juce::int64) 0, (juce::int64) n, available));
    if (have < n) underruns.fetch_add (1, std::memory_order_relaxed);

    const int index = int (rp & ringMask);
    const int first = juce::jmin (have, ringSize - index);
    const int second = have - first;

    for (auto& t : tracks)
    {
        for (int ch = 0; ch < t.channels; ++ch)
        {
            float* dest = t.block[size_t (ch)].data();
            const float* ring = t.ring[size_t (ch)].data();
            if (first > 0)  juce::FloatVectorOperations::copy (dest, ring + index, first);
            if (second > 0) juce::FloatVectorOperations::copy (dest + first, ring, second);
            if (have < n)   juce::FloatVectorOperations::clear (dest + have, n - have);
        }
    }
    // The playhead moves whether or not the disk kept up, so audio and picture stay together.
    streamRead.store (rp + n, std::memory_order_release);
}

void TimelinePlayer::run()
{
    while (! threadShouldExit())
    {
        const int request = seekRequest.load (std::memory_order_acquire);
        if (request != seekHandled)
        {
            seekHandled = request;
            fillPosition = seekTo.load (std::memory_order_acquire);
            streamRead.store (0, std::memory_order_release);
            streamWrite.store (0, std::memory_order_release);
        }
        topUp();
        if (streamWrite.load (std::memory_order_relaxed) - streamRead.load (std::memory_order_relaxed)
              >= juce::jmin (ringSize / 2, maxBlock * 4))
            filled.signal();
        wake.wait (kPollMs);
    }
}

void TimelinePlayer::topUp()
{
    const int target = ringSize - maxBlock * 2;
    for (int guard = 0; guard < 64 && ! threadShouldExit(); ++guard)
    {
        const juce::int64 wp = streamWrite.load (std::memory_order_relaxed);
        const juce::int64 rp = streamRead.load (std::memory_order_relaxed);
        const int used = int (juce::jlimit ((juce::int64) 0, (juce::int64) ringSize, wp - rp));
        if (used >= target) return;

        const int base = int (wp & ringMask);
        int count = juce::jmin (target - used, fillChunk, ringSize - base);
        if (count <= 0) return;

        // The loop wraps to the sample, exactly as Transport::advance does.
        if (looping.load (std::memory_order_relaxed))
        {
            const juce::int64 end = loopEnd.load (std::memory_order_relaxed);
            const juce::int64 start = loopStart.load (std::memory_order_relaxed);
            if (fillPosition >= end) { fillPosition = start; continue; }
            count = int (juce::jmin ((juce::int64) count, end - fillPosition));
            if (count <= 0) { fillPosition = start; continue; }
        }

        source.read (fillPosition, count);
        for (size_t t = 0; t < tracks.size(); ++t)
            for (int ch = 0; ch < tracks[t].channels; ++ch)
                if (const float* from = source.channel (int (t), ch))
                    juce::FloatVectorOperations::copy (tracks[t].ring[size_t (ch)].data() + base, from, count);

        fillPosition += count;
        streamWrite.store (wp + count, std::memory_order_release);
    }
}

} // namespace livemix
