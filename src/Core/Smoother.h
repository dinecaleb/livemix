#pragma once
#include <cmath>

namespace livemix
{

// Exponential one-pole parameter smoother. Allocation-free.
class Smoother
{
public:
    void prepare (double sampleRate, float timeMs) noexcept
    {
        sr = sampleRate;
        setTime (timeMs);
    }

    void setTime (float timeMs) noexcept
    {
        const double samples = timeMs * 0.001 * sr;
        coeff = samples <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / samples);
    }

    void setTarget (float t) noexcept { target = t; }
    void snapTo (float v) noexcept { current = target = v; }
    void snapToTarget() noexcept { current = target; }

    float getTarget() const noexcept { return float (target); }
    float getCurrent() const noexcept { return float (current); }

    bool isSmoothing() const noexcept { return std::fabs (current - target) > 1.0e-7; }

    // State is kept in double so tiny increments near large values (e.g. a gain of
    // 1.0 approached with a 50 ms time constant) never stall below float resolution.
    float next() noexcept
    {
        current += coeff * (target - current);
        if (std::fabs (current - target) < 1.0e-7)
            current = target;
        return float (current);
    }

private:
    double sr = 48000.0;
    double coeff = 1.0;
    double current = 0.0;
    double target = 0.0;
};

} // namespace livemix
