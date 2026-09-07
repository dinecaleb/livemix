#pragma once
#include <atomic>
#include <vector>
#include <cstring>
#include "Core/Constants.h"
#include "Core/AudioBlockView.h"

namespace livemix
{

// Single-producer (audio thread) / single-consumer (analysis worker) ring of
// interleaved frames. push() never allocates or blocks; if the consumer falls
// behind, frames are dropped and counted.
class AnalysisFifo
{
public:
    void prepare (int capacityFrames, int numChannels)
    {
        channels = numChannels < kMaxChannels ? numChannels : kMaxChannels;
        capacity = capacityFrames;
        buffer.assign (size_t (capacity * channels), 0.0f);
        readPos.store (0);
        writePos.store (0);
        dropped.store (0);
    }

    int getNumChannels() const noexcept { return channels; }

    // Audio thread.
    void push (const AudioBlockView& block) noexcept
    {
        const int w = writePos.load (std::memory_order_relaxed);
        const int r = readPos.load (std::memory_order_acquire);
        int free = r - w - 1;
        if (free < 0) free += capacity;
        const int n = block.numSamples < free ? block.numSamples : free;
        if (n < block.numSamples)
            dropped.fetch_add (block.numSamples - n, std::memory_order_relaxed);

        int pos = w;
        for (int i = 0; i < n; ++i)
        {
            float* frame = &buffer[size_t (pos * channels)];
            for (int ch = 0; ch < channels; ++ch)
                frame[ch] = ch < block.numChannels ? block.channels[ch][i] : block.channels[0][i];
            if (++pos == capacity) pos = 0;
        }
        writePos.store (pos, std::memory_order_release);
    }

    // Worker thread. Returns number of frames copied into dest (interleaved).
    int pop (float* dest, int maxFrames) noexcept
    {
        const int r = readPos.load (std::memory_order_relaxed);
        const int w = writePos.load (std::memory_order_acquire);
        int available = w - r;
        if (available < 0) available += capacity;
        const int n = available < maxFrames ? available : maxFrames;

        int pos = r;
        for (int i = 0; i < n; ++i)
        {
            std::memcpy (dest + i * channels, &buffer[size_t (pos * channels)], sizeof (float) * size_t (channels));
            if (++pos == capacity) pos = 0;
        }
        readPos.store (pos, std::memory_order_release);
        return n;
    }

    void clear() noexcept
    {
        readPos.store (writePos.load());
    }

    int getDroppedFrames() const noexcept { return dropped.load (std::memory_order_relaxed); }
    void resetDropped() noexcept { dropped.store (0); }

private:
    std::vector<float> buffer;
    int capacity = 0;
    int channels = 1;
    std::atomic<int> readPos { 0 };
    std::atomic<int> writePos { 0 };
    std::atomic<int> dropped { 0 };
};

} // namespace livemix
