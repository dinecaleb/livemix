#pragma once
#include <string>
#include <vector>

namespace livemix
{

enum class Confidence : int { Low = 0, Medium, High };

inline constexpr const char* confidenceName (Confidence c) noexcept
{
    switch (c)
    {
        case Confidence::High:   return "HIGH";
        case Confidence::Medium: return "MEDIUM";
        case Confidence::Low:
        default:                 return "LOW";
    }
}

struct ParameterChange
{
    std::string paramId;
    float value = 0.0f; // in the parameter's natural units (dB, Hz, ms, ratio, 0/1 for bools)
};

// Where an item appears in the Tune result. Derived from Kind unless a strategy
// says otherwise (an informational "compression left alone" note is Dynamics).
enum class TuneSection : int { Input = 0, Tone, Dynamics, Attack, Bleed, Mix, Notes, Count };

inline constexpr const char* tuneSectionName (TuneSection s) noexcept
{
    switch (s)
    {
        case TuneSection::Input:    return "Input";
        case TuneSection::Tone:     return "Tone";
        case TuneSection::Dynamics: return "Level";
        case TuneSection::Attack:   return "Attack";
        case TuneSection::Bleed:    return "Clean-up";
        case TuneSection::Mix:      return "Mix";
        case TuneSection::Notes:
        default:                    return "Notes";
    }
}

// The same section reads differently per product: drum "Attack" is a voice's or keys' "Clarity".
inline constexpr const char* tuneSectionNameFor (TuneSection s, bool drums) noexcept
{
    if (! drums && s == TuneSection::Attack) return "Clarity";
    return tuneSectionName (s);
}

struct Recommendation
{
    enum class Kind : int { CaptureGain = 0, MixGain, Gate, Compression, EQ, Transient, Filter, Info };

    static constexpr TuneSection sectionFor (Kind k) noexcept
    {
        switch (k)
        {
            case Kind::CaptureGain: return TuneSection::Input;
            case Kind::MixGain:     return TuneSection::Mix;
            case Kind::Gate:        return TuneSection::Bleed;
            case Kind::Compression: return TuneSection::Dynamics;
            case Kind::EQ:          return TuneSection::Tone;
            case Kind::Filter:      return TuneSection::Tone;
            case Kind::Transient:   return TuneSection::Attack;
            case Kind::Info:
            default:                return TuneSection::Notes;
        }
    }

    Kind kind = Kind::Info;
    TuneSection section = TuneSection::Notes;
    std::string what;     // "Increase input/preamp approximately +4 dB"
    std::string why;      // "Signal is healthy but lower than the recommended operating range."
    Confidence confidence = Confidence::Medium;

    // Plugin-side changes the user may apply. Empty for capture-gain and info
    // items, which describe physical/console actions.
    std::vector<ParameterChange> changes;
    bool safeToAutoApply = false;
};

struct RecommendationResult
{
    bool valid = false;
    std::string inputHealth;       // "Healthy", "Low", "Hot", "Clipping", "No signal"
    float measuredPeakDb = -120.0f;
    float measuredRmsDb = -120.0f;
    float suggestedCaptureGainDb = 0.0f;
    std::vector<Recommendation> items;
};

} // namespace livemix
