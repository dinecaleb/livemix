#pragma once
#include "DelayLine.h"

namespace livemix
{

// Schroeder allpass diffuser: unity magnitude at every frequency, smears time.
// Building block for reverb early diffusion and tank feedback paths.
class SchroederAllpass
{
public:
    void prepare (int delaySamples, int numChannels)
    {
        delay = delaySamples < 1 ? 1 : delaySamples;
        line.prepare (delay + 1, numChannels);
    }
    void reset() noexcept { line.reset(); }
    void setGain (float g) noexcept { gain = g; }

    // Call once per channel per frame, then advance().
    float process (int channel, float x) noexcept
    {
        const float delayed = line.read (channel, delay);
        const float v = x + gain * delayed;   // feedback into the line
        line.write (channel, v);
        return delayed - gain * v;            // feed-forward
    }
    void advance() noexcept { line.advance(); }

private:
    DelayLine line;
    int delay = 1;
    float gain = 0.5f;
};

// Feedback comb with a one-pole low-pass in the loop (damping). The classic
// late-reverb building block; several in parallel with different lengths.
class DampedComb
{
public:
    void prepare (int delaySamples, int numChannels)
    {
        delay = delaySamples < 1 ? 1 : delaySamples;
        line.prepare (delay + 1, numChannels);
        for (auto& s : lp) s = 0.0f;
    }
    void reset() noexcept { line.reset(); for (auto& s : lp) s = 0.0f; }
    void setFeedback (float f) noexcept { feedback = f < 0.0f ? 0.0f : (f > 0.999f ? 0.999f : f); }
    void setDamping (float d) noexcept { damp = d < 0.0f ? 0.0f : (d > 0.999f ? 0.999f : d); }

    float process (int channel, float x) noexcept
    {
        const float out = line.read (channel, delay);
        auto& s = lp[size_t (channel)];
        s = out * (1.0f - damp) + s * damp;
        line.write (channel, x + s * feedback);
        return out;
    }
    void advance() noexcept { line.advance(); }

private:
    DelayLine line;
    int delay = 1;
    float feedback = 0.8f, damp = 0.2f;
    float lp[kMaxChannels] {};
};

} // namespace livemix
