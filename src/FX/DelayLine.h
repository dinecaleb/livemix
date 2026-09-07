#pragma once
#include <vector>
#include <cmath>
#include "Core/Constants.h"

namespace livemix
{

// Multichannel circular delay line. prepare() allocates once; write/read are
// allocation-free and branch-light (power-of-two mask). Foundation for the
// Dine FX reverbs (comb/allpass networks) and delays (fractional, modulated).
class DelayLine
{
public:
    void prepare (int maxDelaySamples, int numChannels)
    {
        channels = numChannels < 1 ? 1 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);
        size = 1;
        while (size < maxDelaySamples + 2) size <<= 1;
        mask = size - 1;
        buffer.assign (size_t (size * channels), 0.0f);
        writePos = 0;
        maxDelay = maxDelaySamples;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int getMaxDelay() const noexcept { return maxDelay; }

    // Write one sample for a channel at the current position (call advance() once per frame after all channels).
    void write (int channel, float x) noexcept { buffer[size_t (channel * size + writePos)] = x; }
    void advance() noexcept { writePos = (writePos + 1) & mask; }

    // Sample written `delay` frames ago (1 = the previous frame). delay in [1, maxDelay].
    float read (int channel, int delay) const noexcept
    {
        const int idx = (writePos - delay) & mask;
        return buffer[size_t (channel * size + idx)];
    }

    // Linear interpolation between neighbouring integer delays.
    float readFractional (int channel, float delay) const noexcept
    {
        if (delay < 1.0f) delay = 1.0f;
        if (delay > float (maxDelay)) delay = float (maxDelay);
        const int d0 = int (delay);
        const float frac = delay - float (d0);
        const float a = read (channel, d0);
        const float b = read (channel, d0 + 1 <= maxDelay ? d0 + 1 : d0);
        return a + frac * (b - a);
    }

    // First-order allpass interpolation: flat magnitude, better for modulated delays / reverb tanks.
    float readAllpass (int channel, float delay, float& state) const noexcept
    {
        if (delay < 1.0f) delay = 1.0f;
        if (delay > float (maxDelay) - 1.0f) delay = float (maxDelay) - 1.0f;
        int d0 = int (delay);
        float frac = delay - float (d0);
        if (frac < 0.1f) { frac += 1.0f; --d0; if (d0 < 1) { d0 = 1; frac = 0.1f; } } // keep the coefficient away from 1
        const float eta = (1.0f - frac) / (1.0f + frac);
        const float a = read (channel, d0);
        const float b = read (channel, d0 + 1);
        const float y = b + eta * (a - state);
        state = y;
        return y;
    }

private:
    std::vector<float> buffer;
    int channels = 1, size = 0, mask = 0, writePos = 0, maxDelay = 0;
};

} // namespace livemix
