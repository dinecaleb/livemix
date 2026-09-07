#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include "Constants.h"

// Fast log2/pow2 approximations for per-sample dynamics detectors.
// Accuracy is roughly 1e-4 in log2 (~0.0006 dB), which is far below anything
// audible for gain computation, but they are NOT used for metering or analysis.
namespace livemix::fastmath
{

inline float log2 (float x) noexcept
{
    // Split into exponent and mantissa, then polynomial on the mantissa.
    uint32_t bits;
    std::memcpy (&bits, &x, sizeof (bits));
    const int exponent = int ((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x007FFFFF) | 0x3F800000; // mantissa in [1, 2)
    float m;
    std::memcpy (&m, &bits, sizeof (m));
    // Least-squares degree-5 polynomial for log2(m), m in [1,2); max error ~3e-5 (0.0002 dB)
    const float p = ((((0.043445441f * m - 0.404990949f) * m + 1.594268346f) * m - 3.493032793f) * m + 5.047267047f) * m - 2.786925312f;
    return float (exponent) + p;
}

inline float pow2 (float x) noexcept
{
    // Clamp to a safe range for the bit trick.
    if (x < -126.0f) x = -126.0f;
    if (x >  126.0f) x =  126.0f;
    const float fl = std::floor (x);
    const float frac = x - fl;
    // Least-squares degree-4 polynomial for 2^frac, frac in [0,1); max rel error ~7e-6
    const float p = (((0.013675339f * frac + 0.051669201f) * frac + 0.241708765f) * frac + 0.692931647f) * frac + 1.000007259f;
    uint32_t bits;
    std::memcpy (&bits, &p, sizeof (bits));
    bits += uint32_t (int32_t (fl)) << 23;
    float r;
    std::memcpy (&r, &bits, sizeof (r));
    return r;
}

inline float gainToDb (float gain) noexcept
{
    constexpr float minGain = 1.0e-6f;
    if (gain <= minGain) return kSilenceDb;
    return 6.02059991f * log2 (gain); // 20*log10(x) = 20/log2(10) * log2(x)
}

inline float dbToGain (float db) noexcept
{
    if (db <= kSilenceDb) return 0.0f;
    return pow2 (db * 0.166096405f); // 10^(db/20) = 2^(db * log2(10)/20)
}

} // namespace livemix::fastmath
