#pragma once
#include <array>

namespace livemix
{

// Sonic profiles. A profile is a target engineering philosophy (tonal, dynamic,
// transient and spatial targets plus safe ranges), not a static preset. Data
// lives in src/Profiles/ProfileData.cpp. Modern Gospel is the first and the
// default; Modern Worship is derived from it. More are added only when they
// can be tuned by listening, never as arbitrary variants.
enum class StyleProfileId : int
{
    ModernGospel = 0,
    ModernWorship,
    Count
};

inline constexpr std::array<const char*, int (StyleProfileId::Count)> kStyleProfileNames {
    "Modern Gospel", "Modern Worship"
};

inline constexpr const char* styleProfileName (StyleProfileId s) noexcept
{
    const int i = int (s);
    return (i >= 0 && i < int (StyleProfileId::Count)) ? kStyleProfileNames[size_t (i)] : "Unknown";
}

inline constexpr StyleProfileId styleProfileFromIndex (int index) noexcept
{
    return (index >= 0 && index < int (StyleProfileId::Count)) ? StyleProfileId (index) : StyleProfileId::ModernGospel;
}

} // namespace livemix
