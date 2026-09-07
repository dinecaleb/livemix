#pragma once
#include <cmath>
#include <algorithm>
#include "Constants.h"

namespace livemix
{

inline float dbToGain (float db) noexcept
{
    return db <= kSilenceDb ? 0.0f : std::pow (10.0f, db * 0.05f);
}

inline float gainToDb (float gain) noexcept
{
    constexpr float minGain = 1.0e-6f; // -120 dB
    return gain <= minGain ? kSilenceDb : 20.0f * std::log10 (gain);
}

template <typename T>
inline T clamp (T value, T lo, T hi) noexcept
{
    return std::min (std::max (value, lo), hi);
}

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Flush tiny values to zero. Used in feedback paths as a belt-and-braces
// measure in addition to the FTZ/DAZ hardware mode.
inline float flushDenormal (float x) noexcept
{
    return std::fabs (x) < 1.0e-20f ? 0.0f : x;
}

} // namespace livemix
