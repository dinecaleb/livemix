// Dine FX profile data. Numbers only: the reverb/delay baseline for every type
// under Modern Gospel, and the documented Modern Worship deltas.
//
// Every value is an engineering starting point for a live/broadcast gospel
// mix (vocal plates that sit behind the singer, halls that bloom without
// washing the band, throws that only speak between phrases). They are the
// first thing to adjust after listening tests on real stems.
#include "FxProfiles.h"
#include "Core/DbUtils.h"

namespace livemix
{

namespace
{
    FxParameters reverbBase()
    {
        FxParameters p;
        p.reverbEnabled = true;
        p.delayEnabled = false;
        p.mix = 1.0f; // aux/send use
        return p;
    }

    FxParameters delayBase (int mode, NoteDivision division, float feedback)
    {
        FxParameters p;
        p.reverbEnabled = false;
        p.delayEnabled = true;
        p.delayMode = mode;
        p.delaySync = true;
        p.delayDivision = int (division);
        p.delayFeedback = feedback;
        p.delayLowCutHz = 180.0f;
        p.delayHighCutHz = 5500.0f;
        p.delayWidth = 100.0f;
        p.mix = 1.0f;
        return p;
    }

    // ------------------------------------------------------------------
    // Modern Gospel
    // ------------------------------------------------------------------
    FxParameters gospel (FxType type)
    {
        switch (type)
        {
            case FxType::VocalPlate:
            {
                auto p = reverbBase();
                p.reverbDecayS = 1.9f; p.reverbPreDelayMs = 28.0f; p.reverbSize = 45.0f; p.reverbDamping = 45.0f; p.reverbDiffusion = 85.0f;
                p.reverbLowCutHz = 160.0f; p.reverbHighCutHz = 8500.0f; p.reverbModRateHz = 0.9f; p.reverbModDepth = 35.0f; p.reverbEarly = 25.0f;
                return p;
            }
            case FxType::VocalHall:
            {
                auto p = reverbBase();
                p.reverbDecayS = 2.6f; p.reverbPreDelayMs = 40.0f; p.reverbSize = 70.0f; p.reverbDamping = 50.0f; p.reverbDiffusion = 70.0f;
                p.reverbLowCutHz = 150.0f; p.reverbHighCutHz = 7500.0f; p.reverbModRateHz = 0.6f; p.reverbModDepth = 30.0f; p.reverbEarly = 40.0f;
                return p;
            }
            case FxType::WorshipHall:
            {
                auto p = reverbBase();
                p.reverbDecayS = 3.4f; p.reverbPreDelayMs = 55.0f; p.reverbSize = 85.0f; p.reverbDamping = 55.0f; p.reverbDiffusion = 65.0f;
                p.reverbLowCutHz = 140.0f; p.reverbHighCutHz = 7000.0f; p.reverbModRateHz = 0.5f; p.reverbModDepth = 35.0f; p.reverbEarly = 45.0f;
                return p;
            }
            case FxType::Room:
            {
                auto p = reverbBase();
                p.reverbDecayS = 0.8f; p.reverbPreDelayMs = 10.0f; p.reverbSize = 30.0f; p.reverbDamping = 40.0f; p.reverbDiffusion = 60.0f;
                p.reverbLowCutHz = 120.0f; p.reverbHighCutHz = 9000.0f; p.reverbModRateHz = 1.0f; p.reverbModDepth = 15.0f; p.reverbEarly = 70.0f;
                return p;
            }
            case FxType::DrumRoom:
            {
                auto p = reverbBase();
                p.reverbDecayS = 0.9f; p.reverbPreDelayMs = 8.0f; p.reverbSize = 35.0f; p.reverbDamping = 30.0f; p.reverbDiffusion = 55.0f;
                p.reverbLowCutHz = 90.0f; p.reverbHighCutHz = 10000.0f; p.reverbModRateHz = 1.2f; p.reverbModDepth = 10.0f; p.reverbEarly = 80.0f;
                return p;
            }
            case FxType::SnarePlate:
            {
                auto p = reverbBase();
                p.reverbDecayS = 1.4f; p.reverbPreDelayMs = 12.0f; p.reverbSize = 40.0f; p.reverbDamping = 30.0f; p.reverbDiffusion = 90.0f;
                p.reverbLowCutHz = 220.0f; p.reverbHighCutHz = 11000.0f; p.reverbModRateHz = 1.1f; p.reverbModDepth = 25.0f; p.reverbEarly = 20.0f;
                return p;
            }
            case FxType::LargeAmbient:
            {
                auto p = reverbBase();
                p.reverbDecayS = 6.5f; p.reverbPreDelayMs = 80.0f; p.reverbSize = 100.0f; p.reverbDamping = 65.0f; p.reverbDiffusion = 75.0f;
                p.reverbLowCutHz = 200.0f; p.reverbHighCutHz = 6000.0f; p.reverbModRateHz = 0.35f; p.reverbModDepth = 50.0f; p.reverbEarly = 15.0f;
                return p;
            }
            case FxType::SlapDelay:
            {
                auto p = delayBase (int (DelayMode::Mono), NoteDivision::Sixteenth, 0.0f);
                p.delaySync = false; p.delayTimeMs = 105.0f; p.delayLowCutHz = 200.0f; p.delayHighCutHz = 5000.0f; p.delayLevelDb = -3.0f;
                return p;
            }
            case FxType::QuarterDelay:
            {
                auto p = delayBase (int (DelayMode::Stereo), NoteDivision::Quarter, 32.0f);
                p.delayOffset = 0.0f; p.delayDuck = 25.0f; p.delayDuckReleaseMs = 500.0f;
                return p;
            }
            case FxType::EighthDelay:
            {
                auto p = delayBase (int (DelayMode::Stereo), NoteDivision::Eighth, 28.0f);
                p.delayDuck = 20.0f; p.delayDuckReleaseMs = 400.0f;
                return p;
            }
            case FxType::DottedEighthDelay:
            {
                auto p = delayBase (int (DelayMode::Stereo), NoteDivision::DottedEighth, 35.0f);
                p.delayDuck = 25.0f; p.delayDuckReleaseMs = 450.0f; p.delayModDepth = 10.0f; p.delayModRateHz = 0.4f;
                return p;
            }
            case FxType::StereoDelay:
            {
                auto p = delayBase (int (DelayMode::Stereo), NoteDivision::Quarter, 30.0f);
                p.delayOffset = -25.0f; p.delayWidth = 100.0f; p.delayDuck = 20.0f;
                return p;
            }
            case FxType::PingPongDelay:
            {
                auto p = delayBase (int (DelayMode::PingPong), NoteDivision::DottedEighth, 40.0f);
                p.delayWidth = 100.0f; p.delayDuck = 25.0f; p.delayDuckReleaseMs = 500.0f;
                return p;
            }
            case FxType::VocalThrow:
            {
                auto p = delayBase (int (DelayMode::PingPong), NoteDivision::Quarter, 45.0f);
                p.delayDuck = 70.0f; p.delayDuckReleaseMs = 600.0f; p.delayHighCutHz = 4500.0f; p.delayLowCutHz = 250.0f;
                p.reverbEnabled = true; p.delayToReverb = 40.0f;
                p.reverbDecayS = 2.2f; p.reverbPreDelayMs = 20.0f; p.reverbSize = 60.0f; p.reverbDamping = 55.0f; p.reverbDiffusion = 80.0f;
                p.reverbLowCutHz = 220.0f; p.reverbHighCutHz = 6500.0f; p.reverbModDepth = 35.0f; p.reverbEarly = 10.0f; p.reverbLevelDb = -6.0f;
                return p;
            }
            case FxType::Count:
            default:
                return reverbBase();
        }
    }

    // ------------------------------------------------------------------
    // Modern Worship = Modern Gospel with: longer, more distant tails (+15 % decay,
    // +5 ms pre-delay, slightly darker), delays that repeat a little more but duck harder.
    // ------------------------------------------------------------------
    FxParameters worship (FxType type)
    {
        FxParameters p = gospel (type);
        p.reverbDecayS = clamp (p.reverbDecayS * 1.15f, 0.2f, 20.0f);
        p.reverbPreDelayMs = clamp (p.reverbPreDelayMs + 5.0f, 0.0f, 250.0f);
        p.reverbHighCutHz = clamp (p.reverbHighCutHz * 0.9f, 1000.0f, 20000.0f);
        p.reverbModDepth = clamp (p.reverbModDepth + 5.0f, 0.0f, 100.0f);
        if (p.delayEnabled)
        {
            p.delayFeedback = clamp (p.delayFeedback + 5.0f, 0.0f, 95.0f);
            p.delayDuck = clamp (p.delayDuck + 10.0f, 0.0f, 100.0f);
        }
        return p;
    }
}

namespace FxProfiles
{

FxParameters baseline (StyleProfileId profile, FxType type)
{
    switch (profile)
    {
        case StyleProfileId::ModernWorship: return worship (type);
        case StyleProfileId::ModernGospel:
        case StyleProfileId::Count:
        default:                            return gospel (type);
    }
}

const char* intent (FxType type)
{
    switch (type)
    {
        case FxType::VocalPlate:        return "Dense, smooth and short enough to stay behind the singer";
        case FxType::VocalHall:         return "Natural bloom that supports the lead without wash";
        case FxType::WorshipHall:       return "Large, warm and slow; the sanctuary around the band";
        case FxType::Room:              return "Small and early; puts a source in a real space";
        case FxType::DrumRoom:          return "Bright, short and explosive; drum ambience without mud";
        case FxType::SnarePlate:        return "Bright plate crack for the snare, high-passed out of the low end";
        case FxType::LargeAmbient:      return "Very long, dark and modulated; pads and swells";
        case FxType::SlapDelay:         return "One short repeat for thickness, no feedback";
        case FxType::QuarterDelay:      return "Quarter-note repeats that duck under the vocal";
        case FxType::EighthDelay:       return "Eighth-note repeats for rhythm and lift";
        case FxType::DottedEighthDelay: return "The worship dotted-eighth: movement without clutter";
        case FxType::StereoDelay:       return "Offset left/right repeats for width";
        case FxType::PingPongDelay:     return "Repeats that alternate sides, ducked under the source";
        case FxType::VocalThrow:        return "Only speaks between phrases, then blooms into reverb";
        case FxType::Count:
        default:                        return "";
    }
}

const std::vector<UsePreset>& usePresets()
{
    static const std::vector<UsePreset> presets = [] {
        std::vector<UsePreset> v;
        v.push_back ({ "Modern Gospel Lead Vocal",  StyleProfileId::ModernGospel,  FxType::VocalPlate,   { 50.0f, 50.0f, 50.0f, 60.0f, 45.0f } });
        v.push_back ({ "Modern Gospel BGV",         StyleProfileId::ModernGospel,  FxType::VocalHall,    { 60.0f, 55.0f, 60.0f, 45.0f, 65.0f } });
        v.push_back ({ "Worship Lead Vocal",        StyleProfileId::ModernWorship, FxType::VocalHall,    { 55.0f, 55.0f, 55.0f, 60.0f, 50.0f } });
        v.push_back ({ "Large Worship Ambient",     StyleProfileId::ModernWorship, FxType::LargeAmbient, { 70.0f, 60.0f, 65.0f, 50.0f, 70.0f } });
        v.push_back ({ "Gospel Snare Plate",        StyleProfileId::ModernGospel,  FxType::SnarePlate,   { 45.0f, 50.0f, 35.0f, 55.0f, 45.0f } });
        v.push_back ({ "Live Recording Vocal",      StyleProfileId::ModernGospel,  FxType::VocalPlate,   { 45.0f, 40.0f, 50.0f, 70.0f, 40.0f } });
        return v;
    }();
    return presets;
}

} // namespace FxProfiles
} // namespace livemix
