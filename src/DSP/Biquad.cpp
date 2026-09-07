#include "Biquad.h"
#include <cmath>
#include <algorithm>

namespace livemix
{

BiquadCoefficients BiquadCoefficients::make (FilterType type, double sampleRate, float freqHz, float q, float gainDb) noexcept
{
    const double maxFreq = 0.49 * sampleRate;
    const double f = std::clamp (double (freqHz), 10.0, maxFreq);
    const double Q = std::clamp (double (q), 0.05, 40.0);
    const double A = std::pow (10.0, double (gainDb) / 40.0);
    const double w0 = 2.0 * M_PI * f / sampleRate;
    const double cs = std::cos (w0);
    const double sn = std::sin (w0);
    const double alpha = sn / (2.0 * Q);
    const double sqrtA2alpha = 2.0 * std::sqrt (A) * alpha;

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case FilterType::LowPass:
            b0 = (1 - cs) * 0.5; b1 = 1 - cs; b2 = (1 - cs) * 0.5;
            a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
            break;
        case FilterType::HighPass:
            b0 = (1 + cs) * 0.5; b1 = -(1 + cs); b2 = (1 + cs) * 0.5;
            a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
            break;
        case FilterType::Notch:
            b0 = 1; b1 = -2 * cs; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
            break;
        case FilterType::Peak:
            b0 = 1 + alpha * A; b1 = -2 * cs; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cs; a2 = 1 - alpha / A;
            break;
        case FilterType::LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cs + sqrtA2alpha);
            b1 = 2 * A * ((A - 1) - (A + 1) * cs);
            b2 = A * ((A + 1) - (A - 1) * cs - sqrtA2alpha);
            a0 = (A + 1) + (A - 1) * cs + sqrtA2alpha;
            a1 = -2 * ((A - 1) + (A + 1) * cs);
            a2 = (A + 1) + (A - 1) * cs - sqrtA2alpha;
            break;
        case FilterType::HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cs + sqrtA2alpha);
            b1 = -2 * A * ((A - 1) + (A + 1) * cs);
            b2 = A * ((A + 1) + (A - 1) * cs - sqrtA2alpha);
            a0 = (A + 1) - (A - 1) * cs + sqrtA2alpha;
            a1 = 2 * ((A - 1) - (A + 1) * cs);
            a2 = (A + 1) - (A - 1) * cs - sqrtA2alpha;
            break;
        default:
            break;
    }

    BiquadCoefficients c;
    const double inv = 1.0 / a0;
    c.b0 = float (b0 * inv);
    c.b1 = float (b1 * inv);
    c.b2 = float (b2 * inv);
    c.a1 = float (a1 * inv);
    c.a2 = float (a2 * inv);
    return c;
}

} // namespace livemix
