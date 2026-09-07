#include "EqResponse.h"
#include <complex>
#include <cmath>

namespace livemix
{

namespace EqResponse
{

float biquadMagnitudeDb (const BiquadCoefficients& c, double sampleRate, float freqHz) noexcept
{
    const double w = 2.0 * M_PI * double (freqHz) / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = double (c.b0) + double (c.b1) * z1 + double (c.b2) * z2;
    const std::complex<double> den = 1.0 + double (c.a1) * z1 + double (c.a2) * z2;
    const double mag = std::abs (num / den);
    return mag <= 1.0e-9 ? -180.0f : float (20.0 * std::log10 (mag));
}

float chainMagnitudeDb (const ChannelParameters& p, double sampleRate, float freqHz) noexcept
{
    float db = 0.0f;
    if (p.hpfEnabled)
    {
        if (p.hpfSlope == 1)
        {
            db += biquadMagnitudeDb (BiquadCoefficients::make (FilterType::HighPass, sampleRate, p.hpfHz, 0.5412f, 0.0f), sampleRate, freqHz);
            db += biquadMagnitudeDb (BiquadCoefficients::make (FilterType::HighPass, sampleRate, p.hpfHz, 1.3066f, 0.0f), sampleRate, freqHz);
        }
        else
            db += biquadMagnitudeDb (BiquadCoefficients::make (FilterType::HighPass, sampleRate, p.hpfHz, 0.7071f, 0.0f), sampleRate, freqHz);
    }
    if (p.lpfEnabled)
    {
        if (p.lpfSlope == 1)
        {
            db += biquadMagnitudeDb (BiquadCoefficients::make (FilterType::LowPass, sampleRate, p.lpfHz, 0.5412f, 0.0f), sampleRate, freqHz);
            db += biquadMagnitudeDb (BiquadCoefficients::make (FilterType::LowPass, sampleRate, p.lpfHz, 1.3066f, 0.0f), sampleRate, freqHz);
        }
        else
            db += biquadMagnitudeDb (BiquadCoefficients::make (FilterType::LowPass, sampleRate, p.lpfHz, 0.7071f, 0.0f), sampleRate, freqHz);
    }
    auto addBands = [&] (const auto& bands, bool enabled)
    {
        if (! enabled) return;
        for (const auto& b : bands)
            if (b.enabled)
                db += biquadMagnitudeDb (BiquadCoefficients::make (b.type, sampleRate, b.freqHz, b.q, b.gainDb), sampleRate, freqHz);
    };
    addBands (p.correctiveBands, p.correctiveEqEnabled);
    addBands (p.toneBands, p.toneEqEnabled);
    return db;
}

} // namespace EqResponse
} // namespace livemix
