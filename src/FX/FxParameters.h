#pragma once
#include <array>
#include <string>
#include <type_traits>
#include "FxParameterIDs.h"
#include "State/ParameterIDs.h"
#include "TempoSync.h"

namespace livemix
{

// Effect types. A type is what the user picks ("VOCAL PLATE"); it loads the
// profile baseline for the reverb and delay engines, exactly as a drum role
// loads a channel baseline. Reverb types leave the delay off; delay types
// leave the reverb off (VOCAL THROW uses both).
enum class FxType : int
{
    VocalPlate = 0,
    VocalHall,
    WorshipHall,
    Room,
    DrumRoom,
    SnarePlate,
    LargeAmbient,
    SlapDelay,
    QuarterDelay,
    EighthDelay,
    DottedEighthDelay,
    StereoDelay,
    PingPongDelay,
    VocalThrow,
    Count
};

enum class FxFamily : int { Reverb = 0, Delay, Count };

inline constexpr std::array<const char*, int (FxType::Count)> kFxTypeNames {
    "Vocal Plate", "Vocal Hall", "Worship Hall", "Room", "Drum Room", "Snare Plate", "Large Ambient",
    "Slap Delay", "1/4 Delay", "1/8 Delay", "Dotted 1/8 Delay", "Stereo Delay", "Ping-Pong Delay", "Vocal Throw"
};

inline constexpr const char* fxTypeName (FxType t) noexcept
{
    const int i = int (t);
    return (i >= 0 && i < int (FxType::Count)) ? kFxTypeNames[size_t (i)] : "Unknown";
}

inline constexpr FxType fxTypeFromIndex (int index) noexcept
{
    return (index >= 0 && index < int (FxType::Count)) ? FxType (index) : FxType::VocalPlate;
}

inline constexpr FxFamily fxFamily (FxType t) noexcept
{
    return int (t) >= int (FxType::SlapDelay) ? FxFamily::Delay : FxFamily::Reverb;
}

enum class DelayMode : int { Mono = 0, Stereo, PingPong, Count };
inline constexpr std::array<const char*, int (DelayMode::Count)> kDelayModeNames { "Mono", "Stereo", "Ping-Pong" };

// Plain-old-data snapshot of every DSP parameter of one Dine FX instance.
// Defaults are neutral engineering values; the profile baselines (FxProfiles)
// give each type its sound.
struct FxParameters
{
    // Input
    float inputTrimDb = 0.0f;

    // Reverb engine
    bool reverbEnabled = true;
    float reverbDecayS = 2.0f;        // RT60
    float reverbPreDelayMs = 20.0f;
    float reverbSize = 50.0f;         // 0..100 %, 50 = the reference plate
    float reverbDamping = 40.0f;      // 0..100 %: high-frequency loss per pass
    float reverbDiffusion = 70.0f;    // 0..100 %
    float reverbLowCutHz = 120.0f;
    float reverbHighCutHz = 9000.0f;
    float reverbModRateHz = 0.8f;
    float reverbModDepth = 30.0f;     // 0..100 %
    float reverbEarly = 40.0f;        // early-reflection level, 0..100 %
    float reverbLevelDb = 0.0f;

    // Delay engine
    bool delayEnabled = false;
    int delayMode = int (DelayMode::Stereo);
    bool delaySync = true;
    float delayTimeMs = 375.0f;
    int delayDivision = int (NoteDivision::DottedEighth);
    float delayOffset = 0.0f;         // -50..50 %: right channel time relative to left
    float delayFeedback = 30.0f;      // 0..95 %
    float delayLowCutHz = 150.0f;
    float delayHighCutHz = 6000.0f;
    float delayWidth = 100.0f;        // 0 = mono, 100 = full stereo
    float delayDuck = 0.0f;           // 0..100 %
    float delayDuckReleaseMs = 400.0f;
    float delayModRateHz = 0.5f;
    float delayModDepth = 0.0f;       // 0..100 %
    float delayToReverb = 0.0f;       // 0..100 %
    float delayLevelDb = 0.0f;

    // Output
    float mix = 1.0f;                 // 0 = dry .. 1 = wet
    float outputTrimDb = 0.0f;

    // Global (shared with the shell)
    bool bypassAll = false;
    bool abLoudnessMatch = true;
};

// Visits every host-exposed DSP field in a fixed, stable order. Same contract
// as forEachDspField: f(idFn, T& value); idFn() builds the id string lazily
// so the audio-thread reader never touches strings.
template <typename Params, typename F>
void forEachFxField (Params& p, F&& f)
{
    using namespace FxParamID;
    auto id = [] (const char* s) { return [s] { return std::string (s); }; };

    f (id (inputTrim), p.inputTrimDb);

    f (id (rvOn), p.reverbEnabled);
    f (id (rvDecay), p.reverbDecayS);
    f (id (rvPreDelay), p.reverbPreDelayMs);
    f (id (rvSize), p.reverbSize);
    f (id (rvDamping), p.reverbDamping);
    f (id (rvDiffusion), p.reverbDiffusion);
    f (id (rvLowCut), p.reverbLowCutHz);
    f (id (rvHighCut), p.reverbHighCutHz);
    f (id (rvModRate), p.reverbModRateHz);
    f (id (rvModDepth), p.reverbModDepth);
    f (id (rvEarly), p.reverbEarly);
    f (id (rvLevel), p.reverbLevelDb);

    f (id (dlOn), p.delayEnabled);
    f (id (dlMode), p.delayMode);
    f (id (dlSync), p.delaySync);
    f (id (dlTime), p.delayTimeMs);
    f (id (dlDivision), p.delayDivision);
    f (id (dlOffset), p.delayOffset);
    f (id (dlFeedback), p.delayFeedback);
    f (id (dlLowCut), p.delayLowCutHz);
    f (id (dlHighCut), p.delayHighCutHz);
    f (id (dlWidth), p.delayWidth);
    f (id (dlDuck), p.delayDuck);
    f (id (dlDuckRelease), p.delayDuckReleaseMs);
    f (id (dlModRate), p.delayModRateHz);
    f (id (dlModDepth), p.delayModDepth);
    f (id (dlToReverb), p.delayToReverb);
    f (id (dlLevel), p.delayLevelDb);

    f (id (mix), p.mix);
    f (id (outputTrim), p.outputTrimDb);
    f (id (ParamID::bypass), p.bypassAll);
    f (id (ParamID::abMatch), p.abLoudnessMatch);
}

template <typename Params, typename F>
void forEachFxParameter (Params& p, F&& f)
{
    forEachFxField (p, [&] (auto idFn, auto& v) { f (idFn(), v); });
}

inline int countFxParameters()
{
    FxParameters p;
    int n = 0;
    forEachFxParameter (p, [&] (const std::string&, auto&) { ++n; });
    return n;
}

} // namespace livemix
