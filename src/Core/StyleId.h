#pragma once
#include <array>

namespace livemix
{

// Sonic profiles. A profile is a target engineering philosophy (tonal, dynamic,
// transient and spatial targets plus safe ranges), not a static preset. Data
// lives in src/Profiles/ProfileData.cpp. Modern Gospel is the first and the
// default; every other profile is derived from it as a documented delta, so the
// bounds, the sentences and the idempotency rule come along unchanged. The church
// pair came first; the four after them are the other rooms the same desk ends up in
// - a rock club, an R&B stage, a jazz set, a conference - each tuned by listening,
// never as an arbitrary variant. New profiles go at the end: the index is stored
// with sessions, plug-in presets and input maps.
enum class StyleProfileId : int
{
    ModernGospel = 0,
    ModernWorship,
    RockBand,
    RnbHipHop,
    JazzAcoustic,
    TalkPodcast,
    Count
};

inline constexpr std::array<const char*, int (StyleProfileId::Count)> kStyleProfileNames {
    "Modern Gospel", "Modern Worship", "Rock Band", "R&B and Hip-Hop", "Jazz and Acoustic", "Talk and Podcast"
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

// Is this one of the church profiles? The rest of the app never branches on it for the
// sound - only a sentence or two ("A documented delta") reads it.
inline constexpr bool isChurchProfile (StyleProfileId s) noexcept
{
    return s == StyleProfileId::ModernGospel || s == StyleProfileId::ModernWorship;
}

} // namespace livemix
