#pragma once
#include <cmath>

namespace livemix
{

// Peak-style envelope follower with independent attack and release, operating
// on a rectified (non-negative) input. Allocation-free, per-sample.
class EnvelopeFollower
{
public:
    void prepare (double sampleRate) noexcept { sr = sampleRate; }

    static float coefficientFor (float timeMs, double sampleRate) noexcept
    {
        const double samples = timeMs * 0.001 * sampleRate;
        return samples <= 0.0 ? 1.0f : float (1.0 - std::exp (-1.0 / samples));
    }

    void setAttackMs (float ms) noexcept  { attackCoeff  = coefficientFor (ms, sr); }
    void setReleaseMs (float ms) noexcept { releaseCoeff = coefficientFor (ms, sr); }

    float process (float rectified) noexcept
    {
        const float c = rectified > env ? attackCoeff : releaseCoeff;
        env += c * (rectified - env);
        if (env < 1.0e-12f) env = 0.0f; // denormal guard
        return env;
    }

    float getValue() const noexcept { return env; }
    void reset() noexcept { env = 0.0f; }

private:
    double sr = 48000.0;
    float attackCoeff = 1.0f;
    float releaseCoeff = 1.0f;
    float env = 0.0f;
};

} // namespace livemix
