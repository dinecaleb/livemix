#pragma once
#include <array>
#include <string>
#include <type_traits>
#include "ParametricEQ.h"
#include "State/ParameterIDs.h"

namespace livemix
{

// Plain-old-data snapshot of every DSP parameter for one channel. The plugin
// fills this from the host parameter system once per block; ChannelProcessor
// consumes it. Also used by profiles/presets and the macro mapping.
struct ChannelParameters
{
    // Input
    float inputTrimDb = 0.0f;
    bool polarityInvert = false;
    bool hpfEnabled = false;
    float hpfHz = 80.0f;
    int hpfSlope = 0;            // 0 = 12 dB/oct, 1 = 24 dB/oct
    bool lpfEnabled = false;
    float lpfHz = 18000.0f;
    int lpfSlope = 0;

    // Gate / expander
    bool gateEnabled = false;
    float gateThresholdDb = -40.0f;
    float gateRangeDb = 40.0f;
    float gateAttackMs = 0.5f;
    float gateHoldMs = 50.0f;
    float gateReleaseMs = 100.0f;
    float gateHysteresisDb = 3.0f;
    float gateRatio = 4.0f;
    float gateScHpfHz = 0.0f;   // detector (sidechain) high-pass; < 20 = off. Stops kick/rumble opening tom/snare gates.

    // Corrective EQ
    bool correctiveEqEnabled = true;
    std::array<EQBandParams, ParamID::kCorrectiveBands> correctiveBands {};

    // Compressor
    bool compEnabled = false;
    float compThresholdDb = -20.0f;
    float compRatio = 4.0f;
    float compAttackMs = 10.0f;
    float compReleaseMs = 100.0f;
    float compKneeDb = 6.0f;
    float compMakeupDb = 0.0f;
    float compMix = 1.0f;
    float compScHpfHz = 0.0f;   // detector (sidechain) high-pass; < 20 = off. Stops sub energy pumping kick/bus.

    // Transient
    bool transientEnabled = false;
    float transientAttack = 0.0f;   // -1..1
    float transientSustain = 0.0f;  // -1..1

    // Tone EQ
    bool toneEqEnabled = true;
    std::array<EQBandParams, ParamID::kToneBands> toneBands {};

    // Saturation
    bool satEnabled = false;
    float satDrive = 0.0f; // 0..1
    float satMix = 1.0f;

    // De-esser (vocals): the band above deEssHz is turned down when it exceeds the threshold.
    bool deEssEnabled = false;
    float deEssHz = 6500.0f;
    float deEssThresholdDb = -30.0f;
    float deEssRangeDb = 6.0f;   // maximum reduction

    // Stereo width (keys, master): mid/side scaling; side is removed below monoBelowHz.
    bool widthEnabled = false;
    float widthAmount = 1.0f;    // 0 = mono, 1 = as recorded, 2 = double
    float widthMonoBelowHz = 0.0f;

    // Limiter (master): lookahead brickwall at the ceiling.
    bool limiterEnabled = false;
    float limiterCeilingDb = -1.0f;
    float limiterReleaseMs = 120.0f;

    // Output
    float outputTrimDb = 0.0f;

    // Global
    bool bypassAll = false;
    bool abLoudnessMatch = true;

    ChannelParameters()
    {
        // Neutral tone-EQ layout: low shelf, two peaks, high shelf.
        toneBands[0] = { false, FilterType::LowShelf,  100.0f,  0.0f, 0.7f };
        toneBands[1] = { false, FilterType::Peak,      400.0f,  0.0f, 1.0f };
        toneBands[2] = { false, FilterType::Peak,     3000.0f,  0.0f, 1.0f };
        toneBands[3] = { false, FilterType::HighShelf, 8000.0f, 0.0f, 0.7f };
        correctiveBands[0] = { false, FilterType::Peak, 250.0f, 0.0f, 2.0f };
        correctiveBands[1] = { false, FilterType::Peak, 500.0f, 0.0f, 2.0f };
        correctiveBands[2] = { false, FilterType::Peak, 1000.0f, 0.0f, 2.0f };
    }
};

// Parameter id helper for EQ band fields: e.g. eqBandId("toneEq", 2, "Freq") -> "toneEq2Freq".
inline std::string eqBandId (const char* prefix, int bandIndex, const char* field)
{
    return std::string (prefix) + std::to_string (bandIndex + 1) + field;
}

// Visits every host-exposed DSP field in a fixed, stable order.
// forEachDspField: f(idFn, T& value) where idFn() lazily builds the id string
//   (used on the audio thread; never call idFn there).
// forEachDspParameter: f(const std::string& id, T& value) (message thread).
// T is float, bool, or int (choice index). One definition drives both the
// audio-thread reader and the message-thread writer so they can never disagree.
template <typename Params, typename F>
void forEachDspField (Params& p, F&& f)
{
    using namespace ParamID;
    auto id = [] (const char* s) { return [s] { return std::string (s); }; };
    auto bandId = [] (const char* prefix, int i, const char* field) { return [=] { return eqBandId (prefix, i, field); }; };

    f (id (inputTrim), p.inputTrimDb);
    f (id (polarity), p.polarityInvert);
    f (id (hpfOn), p.hpfEnabled);
    f (id (hpfFreq), p.hpfHz);
    f (id (hpfSlope), p.hpfSlope);
    f (id (lpfOn), p.lpfEnabled);
    f (id (lpfFreq), p.lpfHz);
    f (id (lpfSlope), p.lpfSlope);

    f (id (gateOn), p.gateEnabled);
    f (id (gateThreshold), p.gateThresholdDb);
    f (id (gateRange), p.gateRangeDb);
    f (id (gateAttack), p.gateAttackMs);
    f (id (gateHold), p.gateHoldMs);
    f (id (gateRelease), p.gateReleaseMs);
    f (id (gateHysteresis), p.gateHysteresisDb);
    f (id (gateRatio), p.gateRatio);
    f (id (gateScHpf), p.gateScHpfHz);

    f (id (corrEqOn), p.correctiveEqEnabled);
    for (int i = 0; i < kCorrectiveBands; ++i)
    {
        auto& b = p.correctiveBands[size_t (i)];
        f (bandId ("corrEq", i, "On"), b.enabled);
        { int t = int (b.type); f (bandId ("corrEq", i, "Type"), t); b.type = FilterType (t); }
        f (bandId ("corrEq", i, "Freq"), b.freqHz);
        f (bandId ("corrEq", i, "Gain"), b.gainDb);
        f (bandId ("corrEq", i, "Q"), b.q);
    }

    f (id (compOn), p.compEnabled);
    f (id (compThreshold), p.compThresholdDb);
    f (id (compRatio), p.compRatio);
    f (id (compAttack), p.compAttackMs);
    f (id (compRelease), p.compReleaseMs);
    f (id (compKnee), p.compKneeDb);
    f (id (compMakeup), p.compMakeupDb);
    f (id (compMix), p.compMix);
    f (id (compScHpf), p.compScHpfHz);

    f (id (transOn), p.transientEnabled);
    f (id (transAttack), p.transientAttack);
    f (id (transSustain), p.transientSustain);

    f (id (toneEqOn), p.toneEqEnabled);
    for (int i = 0; i < kToneBands; ++i)
    {
        auto& b = p.toneBands[size_t (i)];
        f (bandId ("toneEq", i, "On"), b.enabled);
        { int t = int (b.type); f (bandId ("toneEq", i, "Type"), t); b.type = FilterType (t); }
        f (bandId ("toneEq", i, "Freq"), b.freqHz);
        f (bandId ("toneEq", i, "Gain"), b.gainDb);
        f (bandId ("toneEq", i, "Q"), b.q);
    }

    f (id (satOn), p.satEnabled);
    f (id (satDrive), p.satDrive);
    f (id (satMix), p.satMix);

    f (id (deEssOn), p.deEssEnabled);
    f (id (deEssFreq), p.deEssHz);
    f (id (deEssThreshold), p.deEssThresholdDb);
    f (id (deEssRange), p.deEssRangeDb);

    f (id (widthOn), p.widthEnabled);
    f (id (widthAmount), p.widthAmount);
    f (id (widthMonoBelow), p.widthMonoBelowHz);

    f (id (limiterOn), p.limiterEnabled);
    f (id (limiterCeiling), p.limiterCeilingDb);
    f (id (limiterRelease), p.limiterReleaseMs);

    f (id (outputTrim), p.outputTrimDb);
    f (id (bypass), p.bypassAll);
    f (id (abMatch), p.abLoudnessMatch);
}

template <typename Params, typename F>
void forEachDspParameter (Params& p, F&& f)
{
    forEachDspField (p, [&] (auto idFn, auto& v) { f (idFn(), v); });
}

// Number of fields visited by forEachDspParameter.
inline int countDspParameters()
{
    ChannelParameters p;
    int n = 0;
    forEachDspParameter (p, [&] (const std::string&, auto&) { ++n; });
    return n;
}

} // namespace livemix
