#pragma once
#include <cmath>

namespace livemix
{

// Allocation-free low-frequency oscillator for delay/reverb modulation.
class LFO
{
public:
    enum class Shape { Sine, Triangle };

    void prepare (double sampleRate) noexcept { sr = sampleRate; setRateHz (rateHz); }
    void setRateHz (float hz) noexcept { rateHz = hz; increment = float (double (hz) / sr); }
    void setShape (Shape s) noexcept { shape = s; }
    void setPhase (float p) noexcept { phase = p - std::floor (p); }
    void reset() noexcept { phase = 0.0f; }

    // Returns -1..1 and advances one sample.
    float next() noexcept
    {
        float v;
        if (shape == Shape::Sine) v = std::sin (phase * 6.28318530718f);
        else v = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        phase += increment;
        if (phase >= 1.0f) phase -= 1.0f;
        return v;
    }

private:
    double sr = 48000.0;
    float rateHz = 0.5f, increment = 0.0f, phase = 0.0f;
    Shape shape = Shape::Sine;
};

} // namespace livemix
