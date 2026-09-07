#pragma once
#include <array>
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"
#include "DSP/ChannelParameters.h"
#include "Analysis/AnalysisResult.h"

namespace livemix
{

// What a profile wants from one source family, and how far Tune may go to get
// there. This is data: no decision logic lives here. Strategies (src/Tune)
// read these targets and safe ranges, compare them with the measured source
// and choose bounded moves. Tuning a profile during beta means editing
// ProfileData.cpp, not a processor or a strategy.
struct SourceTargets
{
    const char* intent = "";           // one line of engineering intent, shown to the user

    // ---- Capture (at the converter, before the plugin) ----
    float capturePeakMinDb = -18.0f;   // healthy raw peak range
    float capturePeakMaxDb = -6.0f;
    float captureGainMaxStepDb = 10.0f; // never suggest more than this in one step

    // ---- Tone ----
    std::array<float, int (Band::Count)> bandTargetDb {};    // desired band energy re total (dB, all <= 0)
    std::array<float, int (Band::Count)> bandToleranceDb {}; // deviation inside this is "fine, leave it"
    float fundamentalMinHz = 40.0f;    // where this source's fundamental is expected
    float fundamentalMaxHz = 120.0f;
    float bodyHz = 100.0f;             // fallback body region when no fundamental is found
    float boxinessHz = 400.0f;         // fallback low-mid mud region
    float attackHz = 4000.0f;          // definition / attack region
    float harshnessMinHz = 2500.0f;    // where harshness is looked for
    float harshnessMaxHz = 8000.0f;
    float airHz = 10000.0f;
    float hpfMinHz = 20.0f;            // Tune may place the high-pass inside this range
    float hpfMaxHz = 60.0f;
    float maxEqCutDb = 6.0f;           // largest single corrective cut Tune will make
    float maxEqBoostDb = 3.0f;         // largest single tonal boost Tune will make
    float maxNotchCutDb = 6.0f;        // resonance notches
    float resonanceMinProminenceDb = 6.0f; // below this a peak is character, not a problem

    // ---- Dynamics ----
    float crestFactorMaxDb = 18.0f;    // above: the source needs control
    float crestFactorMinDb = 8.0f;     // below: already dense, do not add compression
    bool compressionAppropriate = true;
    float compTargetGrDb = 4.0f;       // gain reduction on a normal hit that the profile wants
    float compRatioMin = 2.0f, compRatioMax = 6.0f;
    float compAttackMinMs = 5.0f, compAttackMaxMs = 30.0f;
    float compReleaseMinMs = 60.0f, compReleaseMaxMs = 250.0f;
    float compDetectorHpfHz = 0.0f;    // 0 = full band

    // ---- Transients ----
    bool transientAppropriate = true;
    float transientMaxAttack = 0.5f;   // Tune never sets attack above this
    float transientMaxSustainCut = 0.3f;
    float transientRiseLowDb = 14.0f;  // mean rise below this reads as soft / undefined

    // ---- Bleed ----
    bool gateAppropriate = true;
    float bleedGateThreshold = 0.35f;  // bleed estimate above which a gate is worth it
    float gateMaxRangeDb = 30.0f;      // conservative: expansion, not a hard mute
    float gateDetectorHpfHz = 0.0f;

    // ---- Saturation ----
    bool saturationAppropriate = true;
    float satMaxDrive = 0.3f;

    // ---- Sibilance (voices) ----
    bool deEssAppropriate = false;
    float sibilanceMaxDb = -8.0f;      // 95th-percentile (5 kHz+ minus full-band) frame level above which S sounds need control
    float deEssMaxRangeDb = 8.0f;
    float deEssHz = 6500.0f;           // split frequency Tune uses

    // ---- Stereo (keys, master) ----
    bool widthAppropriate = false;
    float widthTarget = 1.0f;          // 1 = as recorded
    float widthMin = 0.7f, widthMax = 1.4f;
    float correlationMin = 0.2f;       // below this the image is too wide / out of phase for mono systems
    float monoBelowHz = 0.0f;          // side removed below this (0 = off)

    // ---- Loudness (master) ----
    bool loudnessTargetAppropriate = false;
    float targetLufs = -16.0f;         // integrated loudness the delivery wants
    float loudnessToleranceLu = 1.0f;
    float truePeakCeilingDb = -1.0f;

    // ---- Mix ----
    float mixPeakTargetDb = -12.0f;    // processed output peak that sits well in the mix
    float kitBalanceRelDb = 0.0f;      // level relative to the kick/snare reference in a balanced kit
};

using RoleTargets = SourceTargets; // older name used by the kit rules and tests

// A profile: intent, per-family targets and per-family processing baselines.
struct ProfileDefinition
{
    StyleProfileId id = StyleProfileId::ModernGospel;
    const char* name = "";
    const char* philosophy = "";
    std::array<SourceTargets, int (RoleFamily::Count)> targets {};
    std::array<ChannelParameters, int (RoleFamily::Count)> baselines {};
};

namespace Profiles
{
    const ProfileDefinition& definition (StyleProfileId id);
    const SourceTargets& targets (StyleProfileId id, RoleFamily family);
    // Family targets refined for the specific role (Master delivery loudness, synth pad width, ...).
    SourceTargets targets (StyleProfileId id, ChannelRole role);
    // Family baseline refined for the specific role (kick out, snare bottom, floor tom, ...).
    ChannelParameters baseline (StyleProfileId id, ChannelRole role);
}

} // namespace livemix
