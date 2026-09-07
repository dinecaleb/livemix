#pragma once
#include <array>
#include "Core/Constants.h"

namespace livemix
{

enum class FilterType : int
{
    Peak = 0,
    LowShelf,
    HighShelf,
    LowPass,
    HighPass,
    Notch,
    Count
};

inline constexpr std::array<const char*, int (FilterType::Count)> kFilterTypeNames {
    "Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass", "Notch"
};

struct BiquadCoefficients
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    // RBJ audio-EQ-cookbook coefficients. Frequency is clamped to a stable range.
    static BiquadCoefficients make (FilterType type, double sampleRate, float freqHz, float q, float gainDb) noexcept;
};

// Transposed direct form II biquad with per-channel state.
class Biquad
{
public:
    void setCoefficients (const BiquadCoefficients& c) noexcept { coeffs = c; }
    const BiquadCoefficients& getCoefficients() const noexcept { return coeffs; }

    inline float processSample (int channel, float x) noexcept
    {
        auto& s = state[size_t (channel)];
        const float y = coeffs.b0 * x + s.z1;
        s.z1 = coeffs.b1 * x - coeffs.a1 * y + s.z2;
        s.z2 = coeffs.b2 * x - coeffs.a2 * y;
        return y;
    }

    void processBlock (int channel, float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (channel, data[i]);
    }

    void reset() noexcept
    {
        for (auto& s : state) s = {};
    }

private:
    struct State { float z1 = 0.0f, z2 = 0.0f; };
    BiquadCoefficients coeffs;
    std::array<State, kMaxChannels> state {};
};

} // namespace livemix
