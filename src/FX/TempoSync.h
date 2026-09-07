#pragma once
#include <array>

namespace livemix
{

// Host-tempo note divisions for delays. Kept host-agnostic: the plugin passes
// the BPM it reads from the host play head.
enum class NoteDivision : int
{
    Whole = 0, Half, DottedQuarter, Quarter, DottedEighth, Eighth, TripletEighth, Sixteenth, Count
};

inline constexpr std::array<const char*, int (NoteDivision::Count)> kNoteDivisionNames {
    "1/1", "1/2", "1/4.", "1/4", "1/8.", "1/8", "1/8T", "1/16"
};

namespace TempoSync
{
    inline constexpr float beatsFor (NoteDivision d) noexcept
    {
        switch (d)
        {
            case NoteDivision::Whole:         return 4.0f;
            case NoteDivision::Half:          return 2.0f;
            case NoteDivision::DottedQuarter: return 1.5f;
            case NoteDivision::Quarter:       return 1.0f;
            case NoteDivision::DottedEighth:  return 0.75f;
            case NoteDivision::Eighth:        return 0.5f;
            case NoteDivision::TripletEighth: return 1.0f / 3.0f;
            case NoteDivision::Sixteenth:     return 0.25f;
            case NoteDivision::Count:
            default:                          return 1.0f;
        }
    }

    inline constexpr float delayMs (NoteDivision d, double bpm) noexcept
    {
        const double safeBpm = bpm < 20.0 ? 20.0 : (bpm > 300.0 ? 300.0 : bpm);
        return float (60000.0 / safeBpm * double (beatsFor (d)));
    }

    inline constexpr float delaySamples (NoteDivision d, double bpm, double sampleRate) noexcept
    {
        return float (double (delayMs (d, bpm)) * 0.001 * sampleRate);
    }
}

} // namespace livemix
