#pragma once
#include "Biquad.h"
#include "ChannelParameters.h"

namespace livemix
{

// Frequency-response helpers for display. Not used on the audio thread.
namespace EqResponse
{
    // |H(e^jw)| in dB of one biquad at frequency f.
    float biquadMagnitudeDb (const BiquadCoefficients& c, double sampleRate, float freqHz) noexcept;

    // Combined magnitude of every enabled linear stage in the channel chain
    // (HPF/LPF, corrective EQ, tone EQ) at frequency f. Ignores dynamics.
    float chainMagnitudeDb (const ChannelParameters& p, double sampleRate, float freqHz) noexcept;
}

} // namespace livemix
