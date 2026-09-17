// Sonic profile data. Numbers only: targets, tolerances, safe ranges and the
// processing baseline per source family. Decision logic lives in src/Tune.
//
// Every value here is an engineering starting point chosen for the Modern
// Gospel broadcast sound (deep controlled kick, full cracking snare, large
// clean toms, smooth detailed overheads, cohesive punchy bus). They are the
// first thing to adjust after listening tests on real captures.
#include "Profile.h"
#include "Core/DbUtils.h"
#include <algorithm>

namespace livemix
{

namespace
{
    using Bands = std::array<float, int (Band::Count)>;
    EQBandParams band (bool on, FilterType t, float f, float g, float q) { return { on, t, f, g, q }; }

    // ------------------------------------------------------------------
    // Modern Gospel: targets per family
    // ------------------------------------------------------------------
    SourceTargets gospelKickTargets()
    {
        SourceTargets t;
        t.intent = "Deep, punchy, controlled and defined; translates to small systems.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -6.0f;
        //                Sub    Low   LowMid  Mid   UpMid  Pres   Brill  Air
        t.bandTargetDb   = { -2.0f, -3.0f, -13.0f, -19.0f, -19.0f, -21.0f, -30.0f, -40.0f };
        t.bandToleranceDb = { 5.0f, 4.0f, 4.0f, 5.0f, 5.0f, 4.0f, 6.0f, 8.0f };
        t.fundamentalMinHz = 40.0f; t.fundamentalMaxHz = 110.0f; t.bodyHz = 60.0f;
        t.boxinessHz = 350.0f; t.attackHz = 4000.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 6000.0f;
        t.hpfMinHz = 20.0f; t.hpfMaxHz = 45.0f;
        t.maxEqCutDb = 6.0f; t.maxEqBoostDb = 3.0f; t.maxNotchCutDb = 6.0f;
        t.crestFactorMaxDb = 19.0f; t.crestFactorMinDb = 9.0f;
        t.compTargetGrDb = 4.5f; t.compRatioMin = 3.0f; t.compRatioMax = 6.0f;
        t.compAttackMinMs = 10.0f; t.compAttackMaxMs = 30.0f; t.compReleaseMinMs = 60.0f; t.compReleaseMaxMs = 180.0f;
        t.compDetectorHpfHz = 60.0f;
        t.transientMaxAttack = 0.5f; t.transientMaxSustainCut = 0.3f; t.transientRiseLowDb = 14.0f;
        t.bleedGateThreshold = 0.35f; t.gateMaxRangeDb = 30.0f; t.gateDetectorHpfHz = 30.0f;
        t.satMaxDrive = 0.25f;
        t.mixPeakTargetDb = -10.0f; t.kitBalanceRelDb = 0.0f;
        return t;
    }

    SourceTargets gospelSnareTargets()
    {
        SourceTargets t;
        t.intent = "Full, bright and cracking, powerful and controlled; never harsh.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -6.0f;
        t.bandTargetDb   = { -30.0f, -9.0f, -6.0f, -8.0f, -8.0f, -9.0f, -14.0f, -22.0f };
        t.bandToleranceDb = { 8.0f, 5.0f, 4.0f, 4.0f, 4.0f, 4.0f, 5.0f, 6.0f };
        t.fundamentalMinHz = 140.0f; t.fundamentalMaxHz = 280.0f; t.bodyHz = 190.0f;
        t.boxinessHz = 450.0f; t.attackHz = 4500.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 8000.0f;
        t.hpfMinHz = 60.0f; t.hpfMaxHz = 120.0f;
        t.maxEqCutDb = 5.0f; t.maxEqBoostDb = 3.0f; t.maxNotchCutDb = 6.0f;
        t.crestFactorMaxDb = 19.0f; t.crestFactorMinDb = 9.0f;
        t.compTargetGrDb = 4.0f; t.compRatioMin = 3.0f; t.compRatioMax = 6.0f;
        t.compAttackMinMs = 5.0f; t.compAttackMaxMs = 20.0f; t.compReleaseMinMs = 50.0f; t.compReleaseMaxMs = 150.0f;
        t.compDetectorHpfHz = 100.0f;
        t.transientMaxAttack = 0.5f; t.transientMaxSustainCut = 0.3f; t.transientRiseLowDb = 15.0f;
        t.bleedGateThreshold = 0.35f; t.gateMaxRangeDb = 25.0f; t.gateDetectorHpfHz = 150.0f;
        t.satMaxDrive = 0.25f;
        t.mixPeakTargetDb = -10.0f; t.kitBalanceRelDb = 0.0f;
        return t;
    }

    SourceTargets gospelHiHatTargets()
    {
        SourceTargets t;
        t.intent = "Crisp and clear without splash or harshness; sits under the overheads.";
        t.capturePeakMinDb = -24.0f; t.capturePeakMaxDb = -10.0f;
        t.bandTargetDb   = { -45.0f, -32.0f, -22.0f, -15.0f, -8.0f, -6.0f, -6.0f, -10.0f };
        t.bandToleranceDb = { 10.0f, 8.0f, 6.0f, 5.0f, 4.0f, 4.0f, 4.0f, 5.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 300.0f;
        t.boxinessHz = 500.0f; t.attackHz = 6000.0f; t.harshnessMinHz = 3000.0f; t.harshnessMaxHz = 9000.0f;
        t.hpfMinHz = 200.0f; t.hpfMaxHz = 450.0f;
        t.maxEqCutDb = 4.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 4.0f;
        t.crestFactorMaxDb = 22.0f; t.crestFactorMinDb = 8.0f;
        t.compTargetGrDb = 2.5f; t.compRatioMin = 2.0f; t.compRatioMax = 3.0f;
        t.compAttackMinMs = 3.0f; t.compAttackMaxMs = 10.0f; t.compReleaseMinMs = 80.0f; t.compReleaseMaxMs = 200.0f;
        t.transientAppropriate = false;
        t.gateAppropriate = false;
        t.saturationAppropriate = false;
        t.mixPeakTargetDb = -22.0f; t.kitBalanceRelDb = -12.0f;
        return t;
    }

    SourceTargets gospelTomTargets()
    {
        SourceTargets t;
        t.intent = "Large and clean with strong attack, controlled sustain and sensible bleed management.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -6.0f;
        t.bandTargetDb   = { -12.0f, -3.0f, -6.0f, -14.0f, -16.0f, -19.0f, -26.0f, -36.0f };
        t.bandToleranceDb = { 6.0f, 4.0f, 4.0f, 4.0f, 5.0f, 4.0f, 6.0f, 8.0f };
        t.fundamentalMinHz = 60.0f; t.fundamentalMaxHz = 220.0f; t.bodyHz = 100.0f;
        t.boxinessHz = 400.0f; t.attackHz = 3000.0f; t.harshnessMinHz = 2000.0f; t.harshnessMaxHz = 6000.0f;
        t.hpfMinHz = 30.0f; t.hpfMaxHz = 90.0f;
        t.maxEqCutDb = 6.0f; t.maxEqBoostDb = 3.0f; t.maxNotchCutDb = 6.0f;
        t.crestFactorMaxDb = 19.0f; t.crestFactorMinDb = 9.0f;
        t.compTargetGrDb = 3.5f; t.compRatioMin = 2.5f; t.compRatioMax = 4.0f;
        t.compAttackMinMs = 10.0f; t.compAttackMaxMs = 30.0f; t.compReleaseMinMs = 80.0f; t.compReleaseMaxMs = 250.0f;
        t.compDetectorHpfHz = 60.0f;
        t.transientMaxAttack = 0.45f; t.transientMaxSustainCut = 0.35f; t.transientRiseLowDb = 14.0f;
        t.bleedGateThreshold = 0.3f; t.gateMaxRangeDb = 30.0f; t.gateDetectorHpfHz = 80.0f;
        t.satMaxDrive = 0.2f;
        t.mixPeakTargetDb = -13.0f; t.kitBalanceRelDb = -3.0f;
        return t;
    }

    SourceTargets gospelOverheadTargets()
    {
        SourceTargets t;
        t.intent = "Detailed and smooth: cymbal harshness controlled, image balanced, kick spill kept out.";
        t.capturePeakMinDb = -24.0f; t.capturePeakMaxDb = -10.0f;
        t.bandTargetDb   = { -32.0f, -17.0f, -10.0f, -10.0f, -8.0f, -8.0f, -9.0f, -12.0f };
        t.bandToleranceDb = { 8.0f, 5.0f, 4.0f, 4.0f, 4.0f, 3.5f, 4.0f, 5.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 200.0f;
        t.boxinessHz = 400.0f; t.attackHz = 5000.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 8000.0f;
        t.airHz = 10000.0f;
        t.hpfMinHz = 120.0f; t.hpfMaxHz = 300.0f;
        t.maxEqCutDb = 4.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 4.0f;
        t.resonanceMinProminenceDb = 7.0f;
        t.crestFactorMaxDb = 22.0f; t.crestFactorMinDb = 8.0f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.compAttackMinMs = 15.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 150.0f; t.compReleaseMaxMs = 400.0f;
        t.transientAppropriate = false;
        t.gateAppropriate = false;
        t.saturationAppropriate = false;
        t.mixPeakTargetDb = -16.0f; t.kitBalanceRelDb = -6.0f;
        return t;
    }

    SourceTargets gospelRoomTargets()
    {
        SourceTargets t;
        t.intent = "Excitement and size under the close mics; controlled so it never washes out the kick and snare.";
        t.capturePeakMinDb = -24.0f; t.capturePeakMaxDb = -10.0f;
        t.bandTargetDb   = { -22.0f, -11.0f, -8.0f, -10.0f, -10.0f, -10.0f, -12.0f, -18.0f };
        t.bandToleranceDb = { 8.0f, 6.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 6.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 150.0f;
        t.boxinessHz = 500.0f; t.attackHz = 4000.0f;
        t.hpfMinHz = 60.0f; t.hpfMaxHz = 160.0f;
        t.maxEqCutDb = 4.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 5.0f;
        t.crestFactorMaxDb = 24.0f; t.crestFactorMinDb = 6.0f;
        t.compTargetGrDb = 6.0f; t.compRatioMin = 3.0f; t.compRatioMax = 6.0f;
        t.compAttackMinMs = 2.0f; t.compAttackMaxMs = 10.0f; t.compReleaseMinMs = 100.0f; t.compReleaseMaxMs = 300.0f;
        t.transientMaxAttack = 0.2f; t.transientMaxSustainCut = 0.0f;
        t.gateAppropriate = false;
        t.satMaxDrive = 0.3f;
        t.mixPeakTargetDb = -20.0f; t.kitBalanceRelDb = -10.0f;
        return t;
    }

    SourceTargets gospelBusTargets()
    {
        SourceTargets t;
        t.intent = "Cohesion, punch and controlled peaks: glue, not squash.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -3.0f;
        t.bandTargetDb   = { -10.0f, -6.0f, -8.0f, -10.0f, -10.0f, -12.0f, -14.0f, -20.0f };
        t.bandToleranceDb = { 5.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 5.0f, 6.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 80.0f;
        t.boxinessHz = 400.0f; t.attackHz = 4000.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 8000.0f;
        t.hpfMinHz = 20.0f; t.hpfMaxHz = 30.0f;
        t.maxEqCutDb = 3.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 4.0f;
        t.resonanceMinProminenceDb = 8.0f;
        t.crestFactorMaxDb = 17.0f; t.crestFactorMinDb = 7.0f;
        t.compTargetGrDb = 2.5f; t.compRatioMin = 2.0f; t.compRatioMax = 3.0f;
        t.compAttackMinMs = 20.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 100.0f; t.compReleaseMaxMs = 250.0f;
        t.compDetectorHpfHz = 80.0f;
        t.transientAppropriate = false;
        t.gateAppropriate = false;
        t.satMaxDrive = 0.3f;
        t.mixPeakTargetDb = -6.0f; t.kitBalanceRelDb = 0.0f;
        return t;
    }

    // ------------------------------------------------------------------
    // Modern Gospel: processing baselines per family (the factory start)
    // ------------------------------------------------------------------
    ChannelParameters gospelKickBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 28.0f;
        p.gateEnabled = true; p.gateThresholdDb = -35.0f; p.gateRangeDb = 24.0f;
        p.gateAttackMs = 0.3f; p.gateHoldMs = 80.0f; p.gateReleaseMs = 120.0f; p.gateHysteresisDb = 3.0f; p.gateRatio = 6.0f;
        p.gateScHpfHz = 30.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 350.0f, -3.0f, 1.5f);
        p.compEnabled = true; p.compThresholdDb = -18.0f; p.compRatio = 4.0f; p.compAttackMs = 15.0f; p.compReleaseMs = 100.0f; p.compKneeDb = 6.0f;
        p.compScHpfHz = 60.0f;
        p.transientEnabled = true; p.transientAttack = 0.25f; p.transientSustain = 0.0f;
        p.toneBands[0] = band (true, FilterType::LowShelf, 55.0f, 2.5f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 80.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (true, FilterType::Peak, 4000.0f, 2.5f, 1.0f);
        p.toneBands[3] = band (false, FilterType::HighShelf, 8000.0f, 0.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.1f;
        return p;
    }

    ChannelParameters gospelSnareBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 80.0f;
        p.gateEnabled = true; p.gateThresholdDb = -30.0f; p.gateRangeDb = 20.0f;
        p.gateAttackMs = 0.2f; p.gateHoldMs = 60.0f; p.gateReleaseMs = 90.0f; p.gateHysteresisDb = 3.0f; p.gateRatio = 6.0f;
        p.gateScHpfHz = 150.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 450.0f, -2.0f, 1.5f);
        p.compEnabled = true; p.compThresholdDb = -15.0f; p.compRatio = 4.0f; p.compAttackMs = 8.0f; p.compReleaseMs = 80.0f; p.compKneeDb = 6.0f;
        p.compScHpfHz = 100.0f;
        p.transientEnabled = true; p.transientAttack = 0.25f; p.transientSustain = 0.0f;
        p.toneBands[0] = band (true, FilterType::LowShelf, 180.0f, 1.5f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 800.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (true, FilterType::Peak, 4500.0f, 2.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.1f;
        return p;
    }

    ChannelParameters gospelHiHatBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 300.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (false, FilterType::Peak, 3500.0f, -1.0f, 2.0f);
        p.compEnabled = true; p.compThresholdDb = -20.0f; p.compRatio = 2.5f; p.compAttackMs = 5.0f; p.compReleaseMs = 100.0f; p.compKneeDb = 8.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (false, FilterType::LowShelf, 300.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 1000.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 5000.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 8000.0f, 1.0f, 0.7f);
        return p;
    }

    ChannelParameters gospelTomBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 45.0f;
        p.gateEnabled = true; p.gateThresholdDb = -32.0f; p.gateRangeDb = 24.0f;
        p.gateAttackMs = 0.3f; p.gateHoldMs = 120.0f; p.gateReleaseMs = 160.0f; p.gateHysteresisDb = 4.0f; p.gateRatio = 6.0f;
        p.gateScHpfHz = 80.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 400.0f, -3.0f, 1.5f);
        p.compEnabled = true; p.compThresholdDb = -18.0f; p.compRatio = 3.0f; p.compAttackMs = 15.0f; p.compReleaseMs = 120.0f; p.compKneeDb = 6.0f;
        p.compScHpfHz = 60.0f;
        p.transientEnabled = true; p.transientAttack = 0.2f; p.transientSustain = -0.15f;
        p.toneBands[0] = band (true, FilterType::LowShelf, 100.0f, 2.5f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 250.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (true, FilterType::Peak, 3000.0f, 1.5f, 1.0f);
        p.toneBands[3] = band (false, FilterType::HighShelf, 8000.0f, 0.0f, 0.7f);
        return p;
    }

    ChannelParameters gospelOverheadBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 180.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (false, FilterType::Peak, 400.0f, -1.0f, 1.0f);
        p.compEnabled = true; p.compThresholdDb = -22.0f; p.compRatio = 2.0f; p.compAttackMs = 20.0f; p.compReleaseMs = 200.0f; p.compKneeDb = 10.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (false, FilterType::LowShelf, 200.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (true, FilterType::Peak, 400.0f, -1.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.2f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.5f, 0.7f);
        return p;
    }

    ChannelParameters gospelRoomBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 100.0f;
        p.gateEnabled = false;
        p.compEnabled = true; p.compThresholdDb = -25.0f; p.compRatio = 4.0f; p.compAttackMs = 5.0f; p.compReleaseMs = 150.0f; p.compKneeDb = 6.0f;
        p.transientEnabled = true; p.transientAttack = 0.0f; p.transientSustain = 0.2f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 150.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 500.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3000.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 8000.0f, 1.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.1f;
        return p;
    }

    ChannelParameters gospelBusBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = false;
        p.gateEnabled = false;
        p.compEnabled = true; p.compThresholdDb = -18.0f; p.compRatio = 2.5f; p.compAttackMs = 30.0f; p.compReleaseMs = 150.0f; p.compKneeDb = 10.0f;
        p.compScHpfHz = 80.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (true, FilterType::LowShelf, 80.0f, 1.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 400.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3000.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.15f;
        return p;
    }


    // ------------------------------------------------------------------
    // Modern Gospel: voices
    // ------------------------------------------------------------------
    SourceTargets gospelLeadVocalTargets()
    {
        SourceTargets t;
        t.intent = "Every word clear and warm, never harsh; a steady level that sits on top of the band.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -6.0f;
        //                Sub     Low    LowMid  Mid    UpMid  Pres   Brill  Air
        // Low (60-150 Hz) carries a male voice's fundamental, so its target is generous; Sub is where rumble lives.
        t.bandTargetDb   = { -40.0f, -12.0f, -7.0f, -7.0f, -8.0f, -10.0f, -15.0f, -24.0f };
        t.bandToleranceDb = { 8.0f, 7.0f, 4.0f, 4.0f, 4.0f, 4.0f, 5.0f, 6.0f };
        t.fundamentalMinHz = 70.0f; t.fundamentalMaxHz = 400.0f; t.bodyHz = 200.0f;
        t.boxinessHz = 350.0f; t.attackHz = 3500.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 6000.0f; t.airHz = 10000.0f;
        t.hpfMinHz = 70.0f; t.hpfMaxHz = 140.0f;
        t.maxEqCutDb = 6.0f; t.maxEqBoostDb = 3.0f; t.maxNotchCutDb = 5.0f;
        t.resonanceMinProminenceDb = 7.0f;
        t.crestFactorMaxDb = 18.0f; t.crestFactorMinDb = 8.0f;
        t.compTargetGrDb = 5.0f; t.compRatioMin = 3.0f; t.compRatioMax = 5.0f;
        t.compAttackMinMs = 5.0f; t.compAttackMaxMs = 20.0f; t.compReleaseMinMs = 60.0f; t.compReleaseMaxMs = 200.0f;
        t.compDetectorHpfHz = 120.0f;
        t.transientAppropriate = false;
        t.bleedGateThreshold = 0.45f; t.gateMaxRangeDb = 12.0f; t.gateDetectorHpfHz = 150.0f;
        t.satMaxDrive = 0.15f;
        t.deEssAppropriate = true; t.sibilanceMaxDb = -9.0f; t.deEssMaxRangeDb = 8.0f; t.deEssHz = 6500.0f;
        t.mixPeakTargetDb = -8.0f; t.kitBalanceRelDb = 0.0f;
        return t;
    }

    SourceTargets gospelBackingVocalTargets()
    {
        SourceTargets t = gospelLeadVocalTargets();
        t.intent = "Blends behind the lead: smooth, controlled, a little darker, never fighting for the words.";
        t.bandTargetDb   = { -42.0f, -14.0f, -8.0f, -7.0f, -8.0f, -11.0f, -17.0f, -27.0f };
        t.hpfMinHz = 90.0f; t.hpfMaxHz = 180.0f;
        t.maxEqBoostDb = 2.5f;
        t.compTargetGrDb = 5.5f; t.compRatioMin = 3.0f; t.compRatioMax = 6.0f;
        t.gateMaxRangeDb = 15.0f;
        t.sibilanceMaxDb = -10.0f;
        t.mixPeakTargetDb = -14.0f; t.kitBalanceRelDb = -6.0f;
        return t;
    }

    SourceTargets gospelChoirTargets()
    {
        SourceTargets t = gospelLeadVocalTargets();
        t.intent = "Big and natural: rumble and stage noise removed, gentle control, the room left in.";
        t.capturePeakMinDb = -24.0f; t.capturePeakMaxDb = -8.0f;
        t.bandTargetDb   = { -40.0f, -16.0f, -9.0f, -8.0f, -9.0f, -12.0f, -17.0f, -26.0f };
        t.bandToleranceDb = { 10.0f, 7.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 6.0f };
        t.hpfMinHz = 90.0f; t.hpfMaxHz = 170.0f;
        t.maxEqCutDb = 4.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 4.0f;
        t.crestFactorMaxDb = 20.0f;
        t.compTargetGrDb = 2.5f; t.compRatioMin = 2.0f; t.compRatioMax = 3.0f;
        t.compAttackMinMs = 15.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 150.0f; t.compReleaseMaxMs = 400.0f;
        t.gateAppropriate = false;
        t.saturationAppropriate = false;
        t.deEssAppropriate = true; t.sibilanceMaxDb = -8.0f; t.deEssMaxRangeDb = 5.0f;
        t.mixPeakTargetDb = -14.0f; t.kitBalanceRelDb = -6.0f;
        return t;
    }

    SourceTargets gospelSpeechTargets()
    {
        SourceTargets t = gospelLeadVocalTargets();
        t.intent = "Clear speech: no boom, no sharp S, a level that stays put whether the speaker whispers or shouts.";
        t.bandTargetDb   = { -45.0f, -16.0f, -8.0f, -6.0f, -7.0f, -10.0f, -16.0f, -28.0f };
        t.hpfMinHz = 100.0f; t.hpfMaxHz = 160.0f;
        t.attackHz = 3000.0f;
        t.maxEqBoostDb = 3.0f;
        t.crestFactorMaxDb = 16.0f;
        t.compTargetGrDb = 6.0f; t.compRatioMin = 3.0f; t.compRatioMax = 6.0f;
        t.compAttackMinMs = 3.0f; t.compAttackMaxMs = 15.0f; t.compReleaseMinMs = 80.0f; t.compReleaseMaxMs = 250.0f;
        t.bleedGateThreshold = 0.4f; t.gateMaxRangeDb = 15.0f;
        t.saturationAppropriate = false;
        t.sibilanceMaxDb = -10.0f; t.deEssMaxRangeDb = 9.0f;
        t.mixPeakTargetDb = -10.0f;
        return t;
    }

    SourceTargets gospelVocalBusTargets()
    {
        SourceTargets t = gospelLeadVocalTargets();
        t.intent = "All the voices as one: gentle glue, a smooth top, nothing pushed.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -3.0f;
        t.bandTargetDb   = { -42.0f, -13.0f, -7.0f, -7.0f, -8.0f, -10.0f, -15.0f, -24.0f };
        t.hpfMinHz = 60.0f; t.hpfMaxHz = 100.0f;
        t.maxEqCutDb = 3.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 3.0f;
        t.resonanceMinProminenceDb = 8.0f;
        t.crestFactorMaxDb = 17.0f; t.crestFactorMinDb = 7.0f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.compAttackMinMs = 15.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 120.0f; t.compReleaseMaxMs = 300.0f;
        t.gateAppropriate = false;
        t.satMaxDrive = 0.15f;
        t.deEssAppropriate = false;
        t.mixPeakTargetDb = -6.0f;
        return t;
    }

    // ------------------------------------------------------------------
    // Modern Gospel: keys
    // ------------------------------------------------------------------
    SourceTargets gospelPianoTargets()
    {
        SourceTargets t;
        t.intent = "Natural and wide, mud cleared so it sits with the bass and under the vocals.";
        t.capturePeakMinDb = -20.0f; t.capturePeakMaxDb = -6.0f;
        t.bandTargetDb   = { -30.0f, -10.0f, -8.0f, -9.0f, -11.0f, -15.0f, -20.0f, -28.0f };
        t.bandToleranceDb = { 8.0f, 5.0f, 4.0f, 4.0f, 4.0f, 4.0f, 5.0f, 6.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 120.0f;
        t.boxinessHz = 300.0f; t.attackHz = 3000.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 5000.0f; t.airHz = 10000.0f;
        t.hpfMinHz = 30.0f; t.hpfMaxHz = 70.0f;
        t.maxEqCutDb = 5.0f; t.maxEqBoostDb = 2.5f; t.maxNotchCutDb = 5.0f;
        t.resonanceMinProminenceDb = 7.0f;
        t.crestFactorMaxDb = 20.0f; t.crestFactorMinDb = 8.0f;
        t.compTargetGrDb = 2.5f; t.compRatioMin = 2.0f; t.compRatioMax = 3.0f;
        t.compAttackMinMs = 20.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 100.0f; t.compReleaseMaxMs = 300.0f;
        t.compDetectorHpfHz = 80.0f;
        t.transientAppropriate = false;
        t.gateAppropriate = false;
        t.saturationAppropriate = false;
        t.widthAppropriate = true; t.widthTarget = 1.0f; t.widthMin = 0.7f; t.widthMax = 1.3f; t.correlationMin = 0.25f; t.monoBelowHz = 120.0f;
        t.mixPeakTargetDb = -12.0f; t.kitBalanceRelDb = 0.0f;
        return t;
    }

    SourceTargets gospelElectricPianoTargets()
    {
        SourceTargets t = gospelPianoTargets();
        t.intent = "Warm and round; the bark around 2-4 kHz kept polite, a little drive allowed.";
        t.bandTargetDb   = { -28.0f, -8.0f, -7.0f, -9.0f, -12.0f, -16.0f, -22.0f, -32.0f };
        t.hpfMinHz = 40.0f; t.hpfMaxHz = 90.0f;
        t.boxinessHz = 320.0f; t.harshnessMinHz = 2000.0f; t.harshnessMaxHz = 4500.0f;
        t.compTargetGrDb = 3.5f; t.compRatioMin = 2.5f; t.compRatioMax = 4.0f;
        t.compAttackMinMs = 10.0f; t.compAttackMaxMs = 30.0f;
        t.saturationAppropriate = true; t.satMaxDrive = 0.2f;
        t.widthTarget = 1.0f; t.monoBelowHz = 150.0f;
        return t;
    }

    SourceTargets gospelOrganTargets()
    {
        SourceTargets t = gospelPianoTargets();
        t.intent = "Full and warm with the drive kept musical; the low end shares space with the bass.";
        t.bandTargetDb   = { -26.0f, -8.0f, -8.0f, -10.0f, -12.0f, -16.0f, -22.0f, -32.0f };
        t.hpfMinHz = 40.0f; t.hpfMaxHz = 80.0f;
        t.boxinessHz = 450.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 5000.0f;
        t.crestFactorMaxDb = 16.0f; t.crestFactorMinDb = 6.0f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.compAttackMinMs = 30.0f; t.compAttackMaxMs = 60.0f; t.compReleaseMinMs = 150.0f; t.compReleaseMaxMs = 400.0f;
        t.saturationAppropriate = true; t.satMaxDrive = 0.25f;
        t.widthTarget = 1.1f; t.widthMax = 1.4f; t.monoBelowHz = 150.0f;
        return t;
    }

    SourceTargets gospelSynthTargets()
    {
        SourceTargets t = gospelPianoTargets();
        t.intent = "Clear and controlled; pads sit wide behind the band, leads stay forward without harshness.";
        t.bandTargetDb   = { -30.0f, -10.0f, -9.0f, -10.0f, -11.0f, -14.0f, -18.0f, -26.0f };
        t.bandToleranceDb = { 8.0f, 6.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 6.0f };
        t.hpfMinHz = 60.0f; t.hpfMaxHz = 140.0f;
        t.boxinessHz = 350.0f;
        t.compTargetGrDb = 2.5f; t.compRatioMin = 2.0f; t.compRatioMax = 3.5f;
        t.compAttackMinMs = 10.0f; t.compAttackMaxMs = 30.0f;
        t.widthTarget = 1.15f; t.widthMax = 1.5f; t.monoBelowHz = 150.0f;
        t.mixPeakTargetDb = -14.0f; t.kitBalanceRelDb = -4.0f;
        return t;
    }

    SourceTargets gospelKeysBusTargets()
    {
        SourceTargets t = gospelPianoTargets();
        t.intent = "All the keys as one, clear of the vocals: gentle glue and a tidy low end.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -3.0f;
        t.hpfMinHz = 35.0f; t.hpfMaxHz = 60.0f;
        t.maxEqCutDb = 3.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 3.0f;
        t.resonanceMinProminenceDb = 8.0f;
        t.crestFactorMaxDb = 18.0f; t.crestFactorMinDb = 7.0f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.compAttackMinMs = 20.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 150.0f; t.compReleaseMaxMs = 350.0f;
        t.widthTarget = 1.0f; t.monoBelowHz = 120.0f;
        t.mixPeakTargetDb = -10.0f;
        return t;
    }

    // ------------------------------------------------------------------
    // Modern Gospel: guitars
    // ------------------------------------------------------------------
    SourceTargets gospelAcousticGuitarTargets()
    {
        SourceTargets t;
        t.intent = "Bright and natural: body boom and pickup quack controlled, strums even under the vocals.";
        t.capturePeakMinDb = -20.0f; t.capturePeakMaxDb = -6.0f;
        t.bandTargetDb   = { -34.0f, -12.0f, -8.0f, -9.0f, -10.0f, -12.0f, -15.0f, -22.0f };
        t.bandToleranceDb = { 8.0f, 5.0f, 4.0f, 4.0f, 4.0f, 4.0f, 5.0f, 6.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 150.0f;
        t.boxinessHz = 250.0f; t.attackHz = 3500.0f; t.harshnessMinHz = 2000.0f; t.harshnessMaxHz = 4500.0f; t.airHz = 10000.0f;
        t.hpfMinHz = 70.0f; t.hpfMaxHz = 140.0f;
        t.maxEqCutDb = 6.0f; t.maxEqBoostDb = 3.0f; t.maxNotchCutDb = 6.0f;
        t.resonanceMinProminenceDb = 7.0f;
        t.crestFactorMaxDb = 20.0f; t.crestFactorMinDb = 8.0f;
        t.compTargetGrDb = 3.0f; t.compRatioMin = 2.0f; t.compRatioMax = 4.0f;
        t.compAttackMinMs = 10.0f; t.compAttackMaxMs = 30.0f; t.compReleaseMinMs = 80.0f; t.compReleaseMaxMs = 250.0f;
        t.compDetectorHpfHz = 100.0f;
        t.transientAppropriate = false;
        t.gateAppropriate = true; t.bleedGateThreshold = 0.4f; t.gateMaxRangeDb = 18.0f; t.gateDetectorHpfHz = 100.0f;
        t.saturationAppropriate = false;
        t.widthAppropriate = true; t.widthTarget = 1.0f; t.widthMin = 0.8f; t.widthMax = 1.2f; t.correlationMin = 0.3f; t.monoBelowHz = 120.0f;
        t.mixPeakTargetDb = -14.0f; t.kitBalanceRelDb = -4.0f;
        return t;
    }

    SourceTargets gospelElectricGuitarTargets()
    {
        SourceTargets t = gospelAcousticGuitarTargets();
        t.intent = "The amp tone kept, mud and fizz tamed, level held so it sits beside the keys and under the voice.";
        t.bandTargetDb   = { -40.0f, -16.0f, -8.0f, -7.0f, -9.0f, -13.0f, -20.0f, -32.0f };
        t.bandToleranceDb = { 10.0f, 6.0f, 4.0f, 4.0f, 4.0f, 5.0f, 6.0f, 8.0f };
        t.bodyHz = 120.0f; t.boxinessHz = 350.0f; t.attackHz = 2500.0f;
        t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 5000.0f; t.airHz = 8000.0f;
        t.hpfMinHz = 60.0f; t.hpfMaxHz = 120.0f;
        t.crestFactorMaxDb = 18.0f; t.crestFactorMinDb = 6.0f;
        t.compTargetGrDb = 3.0f; t.compRatioMin = 2.0f; t.compRatioMax = 4.0f;
        t.compAttackMinMs = 15.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 100.0f; t.compReleaseMaxMs = 300.0f;
        t.bleedGateThreshold = 0.35f; t.gateMaxRangeDb = 30.0f; t.gateDetectorHpfHz = 80.0f;
        t.saturationAppropriate = true; t.satMaxDrive = 0.2f;
        t.widthTarget = 1.0f; t.widthMin = 0.8f; t.widthMax = 1.3f; t.monoBelowHz = 150.0f;
        return t;
    }

    SourceTargets gospelGuitarBusTargets()
    {
        SourceTargets t = gospelElectricGuitarTargets();
        t.intent = "All the guitars as one, clear of the vocals: gentle glue and a tidy low end.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -3.0f;
        t.bandTargetDb   = { -36.0f, -14.0f, -8.0f, -8.0f, -9.0f, -12.0f, -17.0f, -26.0f };
        t.bandToleranceDb = { 10.0f, 6.0f, 5.0f, 5.0f, 5.0f, 5.0f, 6.0f, 8.0f };
        t.hpfMinHz = 50.0f; t.hpfMaxHz = 90.0f;
        t.maxEqCutDb = 3.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 3.0f;
        t.resonanceMinProminenceDb = 8.0f;
        t.crestFactorMaxDb = 18.0f; t.crestFactorMinDb = 7.0f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.compAttackMinMs = 20.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 150.0f; t.compReleaseMaxMs = 350.0f;
        t.gateAppropriate = false;
        t.saturationAppropriate = false;
        t.widthTarget = 1.0f; t.widthMax = 1.2f; t.monoBelowHz = 120.0f;
        t.mixPeakTargetDb = -12.0f;
        return t;
    }

    // ------------------------------------------------------------------
    // Modern Gospel: bass
    // ------------------------------------------------------------------
    SourceTargets gospelBassTargets()
    {
        SourceTargets t;
        t.intent = "Deep and even: rumble below the lowest note removed, mud cleared, every note at the same level, a little grit so it reads on small speakers.";
        t.capturePeakMinDb = -20.0f; t.capturePeakMaxDb = -6.0f;
        // Measured on the church DI stem: Low 0, Low-Mid -13, Mid -37, Presence -44 re total; the targets sit a few dB
        // brighter than a raw DI so a dark one gets a bounded definition boost and a normal one is left alone.
        t.bandTargetDb   = { -12.0f, -3.0f, -12.0f, -26.0f, -30.0f, -36.0f, -52.0f, -60.0f };
        t.bandToleranceDb = { 5.0f, 4.0f, 4.0f, 5.0f, 5.0f, 6.0f, 8.0f, 10.0f };
        t.fundamentalMinHz = 30.0f; t.fundamentalMaxHz = 130.0f; t.bodyHz = 80.0f;
        t.boxinessHz = 300.0f; t.attackHz = 1500.0f; t.harshnessMinHz = 2000.0f; t.harshnessMaxHz = 5000.0f; t.airHz = 5000.0f;
        t.hpfMinHz = 25.0f; t.hpfMaxHz = 45.0f;
        t.maxEqCutDb = 6.0f; t.maxEqBoostDb = 3.0f; t.maxNotchCutDb = 5.0f;
        t.resonanceMinProminenceDb = 8.0f;
        t.crestFactorMaxDb = 16.0f; t.crestFactorMinDb = 6.0f;
        t.compTargetGrDb = 4.0f; t.compRatioMin = 3.0f; t.compRatioMax = 6.0f;
        t.compAttackMinMs = 10.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 80.0f; t.compReleaseMaxMs = 250.0f;
        t.compDetectorHpfHz = 30.0f;
        t.transientAppropriate = false;
        t.gateAppropriate = true; t.bleedGateThreshold = 0.4f; t.gateMaxRangeDb = 20.0f; t.gateDetectorHpfHz = 30.0f; // detector follows 0.7 x the measured note
        t.saturationAppropriate = true; t.satMaxDrive = 0.35f;
        t.widthAppropriate = false;
        t.mixPeakTargetDb = -10.0f; t.kitBalanceRelDb = -2.0f;
        return t;
    }

    SourceTargets gospelSynthBassTargets()
    {
        SourceTargets t = gospelBassTargets();
        t.intent = "Sub kept clean and steady so it locks with the kick; no expander on a sustained source.";
        t.bandTargetDb   = { -10.0f, -3.0f, -14.0f, -28.0f, -32.0f, -38.0f, -50.0f, -58.0f };
        t.fundamentalMinHz = 25.0f; t.fundamentalMaxHz = 130.0f;
        t.hpfMinHz = 20.0f; t.hpfMaxHz = 35.0f;
        t.crestFactorMaxDb = 14.0f; t.crestFactorMinDb = 5.0f;
        t.compTargetGrDb = 3.0f; t.compRatioMin = 2.0f; t.compRatioMax = 4.0f;
        t.compAttackMinMs = 15.0f; t.compAttackMaxMs = 40.0f;
        t.gateAppropriate = false;
        t.satMaxDrive = 0.15f;
        return t;
    }

    SourceTargets gospelBassBusTargets()
    {
        SourceTargets t = gospelBassTargets();
        t.intent = "All the bass as one: a solid, even low end with gentle glue.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -3.0f;
        t.hpfMinHz = 22.0f; t.hpfMaxHz = 35.0f;
        t.maxEqCutDb = 3.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 3.0f;
        t.resonanceMinProminenceDb = 9.0f;
        t.crestFactorMaxDb = 16.0f; t.crestFactorMinDb = 6.0f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.compAttackMinMs = 20.0f; t.compAttackMaxMs = 40.0f; t.compReleaseMinMs = 150.0f; t.compReleaseMaxMs = 350.0f;
        t.gateAppropriate = false;
        t.saturationAppropriate = false;
        t.mixPeakTargetDb = -8.0f;
        return t;
    }

    // ------------------------------------------------------------------
    // Modern Gospel: master (delivery targets per role, see targetsForRole)
    // ------------------------------------------------------------------
    SourceTargets gospelMasterTargets()
    {
        SourceTargets t;
        t.intent = "Loud enough, never clipped, one mix that translates from phones to the sanctuary.";
        t.capturePeakMinDb = -12.0f; t.capturePeakMaxDb = -1.0f;
        t.bandTargetDb   = { -14.0f, -8.0f, -8.0f, -9.0f, -10.0f, -13.0f, -17.0f, -24.0f };
        t.bandToleranceDb = { 5.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 5.0f, 6.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 80.0f;
        t.boxinessHz = 350.0f; t.attackHz = 3500.0f; t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 6000.0f; t.airHz = 10000.0f;
        t.hpfMinHz = 20.0f; t.hpfMaxHz = 30.0f;
        t.maxEqCutDb = 2.5f; t.maxEqBoostDb = 1.5f; t.maxNotchCutDb = 3.0f;
        t.resonanceMinProminenceDb = 9.0f;
        t.crestFactorMaxDb = 16.0f; t.crestFactorMinDb = 6.0f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.compAttackMinMs = 30.0f; t.compAttackMaxMs = 60.0f; t.compReleaseMinMs = 200.0f; t.compReleaseMaxMs = 400.0f;
        t.compDetectorHpfHz = 80.0f;
        t.transientAppropriate = false;
        t.gateAppropriate = false;
        t.satMaxDrive = 0.15f;
        t.widthAppropriate = true; t.widthTarget = 1.0f; t.widthMin = 0.8f; t.widthMax = 1.2f; t.correlationMin = 0.3f; t.monoBelowHz = 100.0f;
        t.loudnessTargetAppropriate = true; t.targetLufs = -14.0f; t.loudnessToleranceLu = 1.0f; t.truePeakCeilingDb = -1.0f;
        t.mixPeakTargetDb = -1.0f;
        return t;
    }

    // ------------------------------------------------------------------
    // Modern Gospel: baselines for voices, keys and master
    // ------------------------------------------------------------------
    ChannelParameters gospelLeadVocalBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 90.0f;
        p.gateEnabled = true; p.gateThresholdDb = -48.0f; p.gateRangeDb = 8.0f;
        p.gateAttackMs = 2.0f; p.gateHoldMs = 150.0f; p.gateReleaseMs = 250.0f; p.gateHysteresisDb = 3.0f; p.gateRatio = 2.0f;
        p.gateScHpfHz = 150.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 350.0f, -2.0f, 1.4f);
        p.correctiveBands[1] = band (false, FilterType::Peak, 800.0f, 0.0f, 3.0f);
        p.correctiveBands[2] = band (false, FilterType::Peak, 4000.0f, 0.0f, 1.2f);
        p.deEssEnabled = true; p.deEssHz = 6500.0f; p.deEssThresholdDb = -30.0f; p.deEssRangeDb = 5.0f;
        p.compEnabled = true; p.compThresholdDb = -22.0f; p.compRatio = 3.5f; p.compAttackMs = 10.0f; p.compReleaseMs = 120.0f; p.compKneeDb = 8.0f;
        p.compScHpfHz = 120.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (true, FilterType::LowShelf, 180.0f, 1.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 500.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (true, FilterType::Peak, 3500.0f, 1.5f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.5f, 0.7f);
        p.satEnabled = false; p.satDrive = 0.0f;
        return p;
    }

    ChannelParameters gospelBackingVocalBaseline()
    {
        ChannelParameters p = gospelLeadVocalBaseline();
        p.hpfHz = 130.0f;
        p.gateThresholdDb = -45.0f; p.gateRangeDb = 10.0f;
        p.compThresholdDb = -24.0f; p.compRatio = 4.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 180.0f, 0.0f, 0.7f);
        p.toneBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 0.5f, 0.7f);
        p.deEssRangeDb = 6.0f;
        return p;
    }

    ChannelParameters gospelChoirBaseline()
    {
        ChannelParameters p = gospelLeadVocalBaseline();
        p.hpfHz = 120.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (true, FilterType::Peak, 320.0f, -1.5f, 1.2f);
        p.deEssEnabled = true; p.deEssThresholdDb = -28.0f; p.deEssRangeDb = 3.0f;
        p.compThresholdDb = -24.0f; p.compRatio = 2.5f; p.compAttackMs = 25.0f; p.compReleaseMs = 250.0f; p.compKneeDb = 10.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 180.0f, 0.0f, 0.7f);
        p.toneBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.0f, 0.7f);
        return p;
    }

    ChannelParameters gospelSpeechBaseline()
    {
        ChannelParameters p = gospelLeadVocalBaseline();
        p.hpfHz = 120.0f;
        p.gateThresholdDb = -45.0f; p.gateRangeDb = 10.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 300.0f, -2.5f, 1.4f);
        p.deEssThresholdDb = -32.0f; p.deEssRangeDb = 6.0f;
        p.compThresholdDb = -26.0f; p.compRatio = 4.0f; p.compAttackMs = 6.0f; p.compReleaseMs = 150.0f; p.compKneeDb = 8.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 180.0f, 0.0f, 0.7f);
        p.toneBands[2] = band (true, FilterType::Peak, 3000.0f, 2.0f, 1.0f);
        p.toneBands[3] = band (false, FilterType::HighShelf, 10000.0f, 0.0f, 0.7f);
        return p;
    }

    ChannelParameters gospelVocalBusBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 70.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (false, FilterType::Peak, 350.0f, 0.0f, 1.4f);
        p.deEssEnabled = false;
        p.compEnabled = true; p.compThresholdDb = -20.0f; p.compRatio = 2.0f; p.compAttackMs = 25.0f; p.compReleaseMs = 200.0f; p.compKneeDb = 10.0f;
        p.compScHpfHz = 120.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 180.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 500.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.08f;
        return p;
    }

    ChannelParameters gospelPianoBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 40.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (true, FilterType::Peak, 300.0f, -2.0f, 1.2f);
        p.correctiveBands[1] = band (false, FilterType::Peak, 800.0f, 0.0f, 3.0f);
        p.correctiveBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.5f);
        p.compEnabled = true; p.compThresholdDb = -22.0f; p.compRatio = 2.5f; p.compAttackMs = 30.0f; p.compReleaseMs = 200.0f; p.compKneeDb = 10.0f;
        p.compScHpfHz = 80.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (false, FilterType::LowShelf, 120.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 400.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3000.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.0f, 0.7f);
        p.widthEnabled = true; p.widthAmount = 1.0f; p.widthMonoBelowHz = 120.0f;
        return p;
    }

    ChannelParameters gospelElectricPianoBaseline()
    {
        ChannelParameters p = gospelPianoBaseline();
        p.hpfHz = 55.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 320.0f, -1.5f, 1.2f);
        p.correctiveBands[2] = band (true, FilterType::Peak, 2800.0f, -1.5f, 1.5f);
        p.compThresholdDb = -22.0f; p.compRatio = 3.0f; p.compAttackMs = 20.0f; p.compReleaseMs = 150.0f;
        p.toneBands[0] = band (true, FilterType::LowShelf, 120.0f, 1.0f, 0.7f);
        p.toneBands[3] = band (false, FilterType::HighShelf, 10000.0f, 0.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.08f;
        p.widthMonoBelowHz = 150.0f;
        return p;
    }

    ChannelParameters gospelOrganBaseline()
    {
        ChannelParameters p = gospelPianoBaseline();
        p.hpfHz = 50.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 450.0f, -1.5f, 1.2f);
        p.compThresholdDb = -20.0f; p.compRatio = 2.0f; p.compAttackMs = 40.0f; p.compReleaseMs = 250.0f;
        p.toneBands[0] = band (true, FilterType::LowShelf, 100.0f, 1.0f, 0.7f);
        p.toneBands[3] = band (false, FilterType::HighShelf, 10000.0f, 0.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.12f;
        p.widthAmount = 1.1f; p.widthMonoBelowHz = 150.0f;
        return p;
    }

    ChannelParameters gospelSynthBaseline()
    {
        ChannelParameters p = gospelPianoBaseline();
        p.hpfHz = 90.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 350.0f, -1.5f, 1.2f);
        p.compThresholdDb = -22.0f; p.compRatio = 2.5f; p.compAttackMs = 20.0f; p.compReleaseMs = 200.0f;
        p.toneBands[3] = band (false, FilterType::HighShelf, 10000.0f, 0.0f, 0.7f);
        p.widthAmount = 1.15f; p.widthMonoBelowHz = 150.0f;
        return p;
    }

    ChannelParameters gospelKeysBusBaseline()
    {
        ChannelParameters p = gospelPianoBaseline();
        p.hpfHz = 40.0f;
        p.correctiveBands[0] = band (false, FilterType::Peak, 300.0f, 0.0f, 1.2f);
        p.compThresholdDb = -18.0f; p.compRatio = 2.0f; p.compAttackMs = 30.0f; p.compReleaseMs = 250.0f;
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 0.5f, 0.7f);
        p.widthAmount = 1.0f; p.widthMonoBelowHz = 120.0f;
        return p;
    }

    ChannelParameters gospelAcousticGuitarBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 90.0f;
        p.gateEnabled = false; p.gateThresholdDb = -50.0f; p.gateRangeDb = 10.0f; p.gateHoldMs = 80.0f; p.gateReleaseMs = 150.0f;
        p.gateRatio = 4.0f; p.gateHysteresisDb = 3.0f; p.gateScHpfHz = 100.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 250.0f, -2.0f, 1.2f);   // body boom
        p.correctiveBands[1] = band (false, FilterType::Peak, 150.0f, 0.0f, 4.0f);  // body resonance (Tune)
        p.correctiveBands[2] = band (true, FilterType::Peak, 2800.0f, -1.5f, 1.5f); // pickup quack
        p.compEnabled = true; p.compThresholdDb = -24.0f; p.compRatio = 3.0f; p.compAttackMs = 15.0f; p.compReleaseMs = 150.0f; p.compKneeDb = 8.0f;
        p.compScHpfHz = 100.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (false, FilterType::LowShelf, 150.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 400.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.5f, 0.7f);
        p.satEnabled = false;
        p.widthEnabled = true; p.widthAmount = 1.0f; p.widthMonoBelowHz = 120.0f;
        return p;
    }

    ChannelParameters gospelElectricGuitarBaseline()
    {
        ChannelParameters p = gospelAcousticGuitarBaseline();
        p.hpfHz = 80.0f;
        p.lpfEnabled = true; p.lpfHz = 9000.0f;                                     // cab fizz
        p.gateEnabled = true; p.gateThresholdDb = -50.0f; p.gateRangeDb = 12.0f; p.gateScHpfHz = 80.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 350.0f, -1.5f, 1.2f);
        p.correctiveBands[1] = band (false, FilterType::Peak, 500.0f, 0.0f, 4.0f);
        p.correctiveBands[2] = band (true, FilterType::Peak, 3500.0f, -1.5f, 1.5f);
        p.compThresholdDb = -22.0f; p.compRatio = 3.0f; p.compAttackMs = 20.0f; p.compReleaseMs = 200.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 120.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 500.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 2500.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (false, FilterType::HighShelf, 8000.0f, 0.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.06f;
        p.widthMonoBelowHz = 150.0f;
        return p;
    }

    ChannelParameters gospelGuitarBusBaseline()
    {
        ChannelParameters p = gospelElectricGuitarBaseline();
        p.hpfHz = 60.0f;
        p.lpfEnabled = false; p.lpfHz = 12000.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (false, FilterType::Peak, 350.0f, 0.0f, 1.2f);
        p.correctiveBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.5f);
        p.compThresholdDb = -18.0f; p.compRatio = 2.0f; p.compAttackMs = 30.0f; p.compReleaseMs = 250.0f; p.compKneeDb = 10.0f;
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 0.5f, 0.7f);
        p.satEnabled = false; p.satDrive = 0.0f;
        p.widthAmount = 1.0f; p.widthMonoBelowHz = 120.0f;
        return p;
    }

    ChannelParameters gospelBassBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 30.0f; // under 0.8 x low E (41 Hz); Tune only raises it under the measured note
        p.gateEnabled = false; p.gateThresholdDb = -55.0f; p.gateRangeDb = 10.0f; p.gateHoldMs = 80.0f; p.gateReleaseMs = 150.0f;
        p.gateRatio = 4.0f; p.gateHysteresisDb = 3.0f; p.gateScHpfHz = 30.0f;
        p.correctiveBands[0] = band (true, FilterType::Peak, 300.0f, -1.5f, 1.2f);   // mud
        p.correctiveBands[1] = band (false, FilterType::Peak, 400.0f, 0.0f, 4.0f);  // boxy note (Tune)
        p.correctiveBands[2] = band (false, FilterType::Peak, 2500.0f, 0.0f, 1.5f); // fret clank (Tune)
        p.compEnabled = true; p.compThresholdDb = -26.0f; p.compRatio = 4.0f; p.compAttackMs = 20.0f; p.compReleaseMs = 150.0f; p.compKneeDb = 8.0f;
        p.compScHpfHz = 30.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (true, FilterType::LowShelf, 80.0f, 1.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 180.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 1500.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (false, FilterType::HighShelf, 4000.0f, 0.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.1f;
        p.widthEnabled = false;
        return p;
    }

    ChannelParameters gospelSynthBassBaseline()
    {
        ChannelParameters p = gospelBassBaseline();
        p.hpfHz = 25.0f;
        p.correctiveBands[0] = band (false, FilterType::Peak, 300.0f, 0.0f, 1.2f);
        p.compThresholdDb = -24.0f; p.compRatio = 3.0f; p.compAttackMs = 25.0f; p.compReleaseMs = 200.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 80.0f, 0.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.05f;
        return p;
    }

    ChannelParameters gospelBassBusBaseline()
    {
        ChannelParameters p = gospelBassBaseline();
        p.hpfHz = 25.0f;
        p.correctiveBands[0] = band (false, FilterType::Peak, 300.0f, 0.0f, 1.2f);
        p.compThresholdDb = -20.0f; p.compRatio = 2.0f; p.compAttackMs = 30.0f; p.compReleaseMs = 250.0f; p.compKneeDb = 10.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 80.0f, 0.0f, 0.7f);
        p.satEnabled = false; p.satDrive = 0.0f;
        return p;
    }

    ChannelParameters gospelMasterBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 24.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (false, FilterType::Peak, 350.0f, 0.0f, 1.2f);
        p.correctiveBands[1] = band (false, FilterType::Peak, 800.0f, 0.0f, 3.0f);
        p.correctiveBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.5f);
        p.compEnabled = true; p.compThresholdDb = -16.0f; p.compRatio = 1.8f; p.compAttackMs = 40.0f; p.compReleaseMs = 300.0f; p.compKneeDb = 12.0f;
        p.compScHpfHz = 80.0f;
        p.toneBands[0] = band (false, FilterType::LowShelf, 80.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 350.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3500.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 0.5f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.06f;
        p.widthEnabled = true; p.widthAmount = 1.0f; p.widthMonoBelowHz = 100.0f;
        p.limiterEnabled = true; p.limiterCeilingDb = -1.0f; p.limiterReleaseMs = 120.0f;
        p.outputTrimDb = 0.0f;
        return p;
    }

    // ------------------------------------------------------------------
    // Crowd / ambience microphones
    //
    // The one thing everything in here is built around: on an ambience microphone, what
    // happens between the sounds is the sound. A gate or an expander removes the room and
    // leaves the mix sounding like a studio recording with occasional applause stuck to it,
    // so `gateAppropriate` is false and the strategy never asks. Compression is slow and
    // gentle - it is there to stop a sudden shout taking the master's headroom, not to make
    // the congregation "punchy". The high-pass is high (a building full of people is full of
    // low-frequency noise that carries nothing) and the level target is deliberately well
    // under everything else: ambience is felt before it is heard.
    // ------------------------------------------------------------------
    SourceTargets gospelAmbienceTargets()
    {
        SourceTargets t;
        t.intent = "The building: the congregation, the response, the size of the room. Present enough to be felt, "
                   "never loud enough to compete with the stage, and never gated - what is between the sounds is the point.";
        t.capturePeakMinDb = -30.0f; t.capturePeakMaxDb = -12.0f;
        t.bandTargetDb    = { -28.0f, -16.0f, -10.0f, -8.0f, -9.0f, -10.0f, -13.0f, -18.0f };
        t.bandToleranceDb = { 10.0f, 7.0f, 6.0f, 5.0f, 5.0f, 5.0f, 6.0f, 7.0f };
        t.fundamentalMinHz = 0.0f; t.fundamentalMaxHz = 0.0f; t.bodyHz = 200.0f;
        t.boxinessHz = 400.0f; t.attackHz = 4000.0f;
        t.harshnessMinHz = 2000.0f; t.harshnessMaxHz = 6000.0f; t.airHz = 12000.0f;
        // Air handling, traffic, a building settling: none of it is the congregation.
        t.hpfMinHz = 100.0f; t.hpfMaxHz = 220.0f;
        t.maxEqCutDb = 5.0f; t.maxEqBoostDb = 2.0f; t.maxNotchCutDb = 6.0f;
        // A room microphone is meant to be dynamic. It only needs control when one shout
        // would otherwise cost the master its headroom.
        t.crestFactorMaxDb = 26.0f; t.crestFactorMinDb = 8.0f;
        t.compressionAppropriate = true;
        t.compTargetGrDb = 2.5f; t.compRatioMin = 1.5f; t.compRatioMax = 3.0f;
        t.compAttackMinMs = 30.0f; t.compAttackMaxMs = 80.0f; t.compReleaseMinMs = 300.0f; t.compReleaseMaxMs = 800.0f;
        t.compDetectorHpfHz = 120.0f;
        // Nothing transient-shaped: an ambience microphone has no attack to sharpen, and
        // sharpening the room is how a broadcast ends up sounding like a stadium advert.
        t.transientAppropriate = false;
        t.gateAppropriate = false;
        t.saturationAppropriate = false; t.satMaxDrive = 0.0f;
        t.widthAppropriate = true; t.widthTarget = 1.3f; t.widthMin = 1.0f; t.widthMax = 1.6f;
        t.correlationMin = -0.1f; t.monoBelowHz = 200.0f;
        t.deEssAppropriate = false;
        t.mixPeakTargetDb = -26.0f; t.kitBalanceRelDb = -14.0f;
        return t;
    }

    SourceTargets gospelAmbienceBusTargets()
    {
        SourceTargets t = gospelAmbienceTargets();
        t.intent = "The room, taken as one: glue and a ceiling, so the crowd can be turned up as one fader without "
                   "anything sudden reaching the broadcast.";
        t.hpfMinHz = 80.0f; t.hpfMaxHz = 160.0f;
        t.maxEqCutDb = 3.0f; t.maxEqBoostDb = 1.5f;
        t.compTargetGrDb = 2.0f; t.compRatioMin = 1.5f; t.compRatioMax = 2.5f;
        t.mixPeakTargetDb = -20.0f; t.kitBalanceRelDb = -12.0f;
        return t;
    }

    ChannelParameters gospelAmbienceBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 140.0f;
        p.gateEnabled = false;                 // never, on this family
        p.correctiveBands[0] = band (true, FilterType::Peak, 350.0f, -2.0f, 1.0f);
        p.compEnabled = true; p.compThresholdDb = -26.0f; p.compRatio = 2.0f;
        p.compAttackMs = 50.0f; p.compReleaseMs = 500.0f; p.compKneeDb = 12.0f;
        p.compScHpfHz = 120.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (false, FilterType::LowShelf, 200.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 400.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (false, FilterType::Peak, 3000.0f, 0.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 10000.0f, 1.0f, 0.7f);
        p.satEnabled = false;
        p.widthEnabled = true; p.widthAmount = 1.3f; p.widthMonoBelowHz = 200.0f;
        return p;
    }

    ChannelParameters gospelAmbienceBusBaseline()
    {
        ChannelParameters p = gospelAmbienceBaseline();
        p.hpfHz = 100.0f;
        p.correctiveBands[0].enabled = false;
        p.compThresholdDb = -22.0f; p.compRatio = 1.8f; p.compAttackMs = 60.0f; p.compReleaseMs = 600.0f;
        p.widthAmount = 1.2f;
        return p;
    }

    // ------------------------------------------------------------------
    // Saxophone
    //
    // A horn is not a keyboard, which is the whole reason it is here. Three things make it
    // its own family: a hard honk between 800 Hz and 2 kHz that has to be found and notched
    // rather than shelved; a real dynamic range between a held note and a wailed one, so it
    // wants more compression than any keyboard and a slow enough attack to keep the reed's
    // bite; and a presence band that is the lead vocal's presence band, which is why it is
    // targeted a little under and gets its own pocket in the relationship rules.
    // ------------------------------------------------------------------
    SourceTargets gospelSaxTargets()
    {
        SourceTargets t;
        t.intent = "Reedy and singing, with the honk taken out and the top kept smooth: a horn that sits beside the "
                   "voice rather than fighting it for the same air.";
        t.capturePeakMinDb = -18.0f; t.capturePeakMaxDb = -6.0f;
        t.bandTargetDb    = { -34.0f, -20.0f, -9.0f, -7.0f, -8.0f, -11.0f, -16.0f, -24.0f };
        t.bandToleranceDb = { 8.0f, 6.0f, 4.0f, 4.0f, 3.5f, 4.0f, 5.0f, 6.0f };
        // A tenor's low B flat is about 116 Hz; an alto's is about 138 Hz. The range below
        // covers the family, and the high-pass rule never goes above 0.8x what it measures.
        t.fundamentalMinHz = 110.0f; t.fundamentalMaxHz = 700.0f;
        t.bodyHz = 250.0f;
        t.boxinessHz = 500.0f;
        t.attackHz = 3500.0f;
        // The honk. Narrow, real, and the one cut that makes a sax sound professional.
        t.harshnessMinHz = 900.0f; t.harshnessMaxHz = 2500.0f;
        t.airHz = 11000.0f;
        t.hpfMinHz = 60.0f; t.hpfMaxHz = 110.0f;
        t.maxEqCutDb = 6.0f; t.maxEqBoostDb = 2.5f; t.maxNotchCutDb = 6.0f;
        t.resonanceMinProminenceDb = 5.5f;    // a honk is a resonance worth finding early
        t.crestFactorMaxDb = 18.0f; t.crestFactorMinDb = 7.0f;
        t.compTargetGrDb = 4.5f; t.compRatioMin = 2.5f; t.compRatioMax = 4.0f;
        // Slow enough to let the reed's attack through, quick enough to catch a wail.
        t.compAttackMinMs = 10.0f; t.compAttackMaxMs = 30.0f;
        t.compReleaseMinMs = 120.0f; t.compReleaseMaxMs = 350.0f;
        t.compDetectorHpfHz = 100.0f;
        t.transientAppropriate = false;
        // A sustained source never gets an expander, and a horn player's breath between
        // phrases is part of the performance.
        t.gateAppropriate = false;
        t.saturationAppropriate = true; t.satMaxDrive = 0.15f;
        t.widthAppropriate = false;
        t.deEssAppropriate = false;
        t.mixPeakTargetDb = -13.0f; t.kitBalanceRelDb = 0.0f;
        return t;
    }

    ChannelParameters gospelSaxBaseline()
    {
        ChannelParameters p;
        p.hpfEnabled = true; p.hpfHz = 80.0f;
        p.gateEnabled = false;
        p.correctiveBands[0] = band (true, FilterType::Peak, 1200.0f, -2.5f, 2.2f);   // the honk
        p.correctiveBands[1] = band (false, FilterType::Peak, 400.0f, 0.0f, 1.5f);
        p.correctiveBands[2] = band (false, FilterType::Peak, 6000.0f, 0.0f, 2.0f);
        p.compEnabled = true; p.compThresholdDb = -20.0f; p.compRatio = 3.0f;
        p.compAttackMs = 15.0f; p.compReleaseMs = 200.0f; p.compKneeDb = 8.0f;
        p.compScHpfHz = 100.0f;
        p.transientEnabled = false;
        p.toneBands[0] = band (false, FilterType::LowShelf, 250.0f, 0.0f, 0.7f);
        p.toneBands[1] = band (false, FilterType::Peak, 500.0f, 0.0f, 1.0f);
        p.toneBands[2] = band (true, FilterType::Peak, 3500.0f, 1.0f, 1.0f);
        p.toneBands[3] = band (true, FilterType::HighShelf, 11000.0f, 1.0f, 0.7f);
        p.satEnabled = true; p.satDrive = 0.08f;
        return p;
    }

    ProfileDefinition buildModernGospel()
    {
        ProfileDefinition d;
        d.id = StyleProfileId::ModernGospel;
        d.name = "Modern Gospel";
        d.philosophy = "Gospel broadcast: deep controlled low end, forward defined attack, dense but breathing dynamics, "
                       "smooth top end, everything sized for a full band with keys, bass and a choir.";
        auto& t = d.targets; auto& b = d.baselines;
        t[int (RoleFamily::Kick)]     = gospelKickTargets();     b[int (RoleFamily::Kick)]     = gospelKickBaseline();
        t[int (RoleFamily::Snare)]    = gospelSnareTargets();    b[int (RoleFamily::Snare)]    = gospelSnareBaseline();
        t[int (RoleFamily::HiHat)]    = gospelHiHatTargets();    b[int (RoleFamily::HiHat)]    = gospelHiHatBaseline();
        t[int (RoleFamily::Tom)]      = gospelTomTargets();      b[int (RoleFamily::Tom)]      = gospelTomBaseline();
        t[int (RoleFamily::Overhead)] = gospelOverheadTargets(); b[int (RoleFamily::Overhead)] = gospelOverheadBaseline();
        t[int (RoleFamily::Room)]     = gospelRoomTargets();     b[int (RoleFamily::Room)]     = gospelRoomBaseline();
        t[int (RoleFamily::Bus)]      = gospelBusTargets();      b[int (RoleFamily::Bus)]      = gospelBusBaseline();
        t[int (RoleFamily::LeadVocal)]    = gospelLeadVocalTargets();    b[int (RoleFamily::LeadVocal)]    = gospelLeadVocalBaseline();
        t[int (RoleFamily::BackingVocal)] = gospelBackingVocalTargets(); b[int (RoleFamily::BackingVocal)] = gospelBackingVocalBaseline();
        t[int (RoleFamily::Choir)]        = gospelChoirTargets();        b[int (RoleFamily::Choir)]        = gospelChoirBaseline();
        t[int (RoleFamily::Speech)]       = gospelSpeechTargets();       b[int (RoleFamily::Speech)]       = gospelSpeechBaseline();
        t[int (RoleFamily::VocalBus)]     = gospelVocalBusTargets();     b[int (RoleFamily::VocalBus)]     = gospelVocalBusBaseline();
        t[int (RoleFamily::Piano)]        = gospelPianoTargets();        b[int (RoleFamily::Piano)]        = gospelPianoBaseline();
        t[int (RoleFamily::ElectricPiano)]= gospelElectricPianoTargets();b[int (RoleFamily::ElectricPiano)]= gospelElectricPianoBaseline();
        t[int (RoleFamily::Organ)]        = gospelOrganTargets();        b[int (RoleFamily::Organ)]        = gospelOrganBaseline();
        t[int (RoleFamily::Synth)]        = gospelSynthTargets();        b[int (RoleFamily::Synth)]        = gospelSynthBaseline();
        t[int (RoleFamily::KeysBus)]      = gospelKeysBusTargets();      b[int (RoleFamily::KeysBus)]      = gospelKeysBusBaseline();
        t[int (RoleFamily::Master)]       = gospelMasterTargets();       b[int (RoleFamily::Master)]       = gospelMasterBaseline();
        t[int (RoleFamily::AcousticGuitar)] = gospelAcousticGuitarTargets(); b[int (RoleFamily::AcousticGuitar)] = gospelAcousticGuitarBaseline();
        t[int (RoleFamily::ElectricGuitar)] = gospelElectricGuitarTargets(); b[int (RoleFamily::ElectricGuitar)] = gospelElectricGuitarBaseline();
        t[int (RoleFamily::GuitarBus)]      = gospelGuitarBusTargets();      b[int (RoleFamily::GuitarBus)]      = gospelGuitarBusBaseline();
        t[int (RoleFamily::ElectricBass)]   = gospelBassTargets();           b[int (RoleFamily::ElectricBass)]   = gospelBassBaseline();
        t[int (RoleFamily::SynthBass)]      = gospelSynthBassTargets();      b[int (RoleFamily::SynthBass)]      = gospelSynthBassBaseline();
        t[int (RoleFamily::BassBus)]        = gospelBassBusTargets();        b[int (RoleFamily::BassBus)]        = gospelBassBusBaseline();
        t[int (RoleFamily::Ambience)]       = gospelAmbienceTargets();       b[int (RoleFamily::Ambience)]       = gospelAmbienceBaseline();
        t[int (RoleFamily::AmbienceBus)]    = gospelAmbienceBusTargets();    b[int (RoleFamily::AmbienceBus)]    = gospelAmbienceBusBaseline();
        t[int (RoleFamily::Saxophone)]      = gospelSaxTargets();            b[int (RoleFamily::Saxophone)]      = gospelSaxBaseline();
        return d;
    }

    // ------------------------------------------------------------------
    // Modern Worship: derived from Modern Gospel with documented deltas.
    // Slightly more open dynamics, a touch less saturation and low-end
    // weight, smoother cymbals, more room.
    // ------------------------------------------------------------------
    ProfileDefinition buildModernWorship()
    {
        ProfileDefinition d = buildModernGospel();
        d.id = StyleProfileId::ModernWorship;
        d.name = "Modern Worship";
        d.philosophy = "Worship: open, natural dynamics with a clean modern low end, smooth cymbals and more room; "
                       "punch comes from definition rather than density.";
        for (auto& t : d.targets)
        {
            t.crestFactorMaxDb += 1.5f;           // accept more dynamics before compressing
            t.compTargetGrDb = std::max (1.5f, t.compTargetGrDb - 1.0f);
            t.compRatioMax = std::max (t.compRatioMin, t.compRatioMax - 1.0f);
            t.satMaxDrive = std::max (0.0f, t.satMaxDrive - 0.1f);
        }
        d.targets[int (RoleFamily::Kick)].bandTargetDb[size_t (Band::Sub)] -= 1.5f;
        d.targets[int (RoleFamily::Overhead)].bandTargetDb[size_t (Band::Brilliance)] -= 1.0f;
        d.targets[int (RoleFamily::Room)].kitBalanceRelDb = -8.0f;
        d.targets[int (RoleFamily::Room)].mixPeakTargetDb = -18.0f;

        for (auto& p : d.baselines)
        {
            p.compRatio = std::max (1.5f, p.compRatio - 0.5f);
            p.satDrive = std::max (0.0f, p.satDrive - 0.05f);
            if (p.satDrive <= 0.001f) p.satEnabled = false;
        }
        auto& kick = d.baselines[int (RoleFamily::Kick)];
        kick.toneBands[0].gainDb = 2.0f; kick.toneBands[0].freqHz = 60.0f;
        auto& oh = d.baselines[int (RoleFamily::Overhead)];
        oh.toneBands[3].gainDb = 1.0f;
        auto& room = d.baselines[int (RoleFamily::Room)];
        room.transientSustain = 0.3f;

        // Voices: a touch more air and openness, lighter de-essing (worship vocals are recorded closer and softer).
        for (auto f : { RoleFamily::LeadVocal, RoleFamily::BackingVocal, RoleFamily::Choir })
        {
            d.targets[int (f)].bandTargetDb[size_t (Band::Air)] += 1.5f;
            d.targets[int (f)].deEssMaxRangeDb = std::max (3.0f, d.targets[int (f)].deEssMaxRangeDb - 1.0f);
            d.baselines[int (f)].toneBands[3].enabled = true;
            d.baselines[int (f)].toneBands[3].gainDb += 0.5f;
        }
        // Keys: wider pads, less organ drive.
        d.targets[int (RoleFamily::Synth)].widthTarget = 1.25f;
        d.baselines[int (RoleFamily::Synth)].widthAmount = 1.25f;
        d.baselines[int (RoleFamily::Organ)].satDrive = 0.05f;
        // Guitars: a touch more air on the acoustic, cleaner electrics (less template drive, fizz roll-off a little higher).
        d.targets[int (RoleFamily::AcousticGuitar)].bandTargetDb[size_t (Band::Air)] += 1.5f;
        d.baselines[int (RoleFamily::AcousticGuitar)].toneBands[3].gainDb += 0.5f;
        d.baselines[int (RoleFamily::ElectricGuitar)].lpfHz = 10000.0f;
        // Bass: worship wants a cleaner, slightly more open low end; the generic deltas above already take 0.05 off the grit.
        d.targets[int (RoleFamily::ElectricBass)].compTargetGrDb = 3.0f;
        // Ambience: worship services want more of the room, not less - it is what stops a
        // stream sounding like a rehearsal. A touch more level and a touch more air.
        d.targets[int (RoleFamily::Ambience)].mixPeakTargetDb += 2.0f;
        d.targets[int (RoleFamily::Ambience)].kitBalanceRelDb += 2.0f;
        d.targets[int (RoleFamily::Ambience)].bandTargetDb[size_t (Band::Air)] += 1.0f;
        // Sax: a little less honk-cutting and a little more of the reed, to match the softer
        // top end the rest of the worship profile asks for.
        d.targets[int (RoleFamily::Saxophone)].bandTargetDb[size_t (Band::Air)] += 1.0f;
        d.baselines[int (RoleFamily::Saxophone)].correctiveBands[0].gainDb = -2.0f;
        // Master: a little more open dynamics and a slightly lower default loudness push.
        d.targets[int (RoleFamily::Master)].compTargetGrDb = 1.5f;
        return d;
    }

    // Role refinements on top of the family baseline (same for every profile).
    void applyRoleRefinements (ChannelRole role, ChannelParameters& p)
    {
        switch (role)
        {
            // A crowd microphone is aimed at people and an ambience microphone at the room:
            // the first wants the voices, the second wants the decay. One high-pass apart.
            case ChannelRole::CrowdMic:    p.hpfHz = 150.0f; p.toneBands[3].gainDb = 1.5f; break;
            case ChannelRole::AmbienceMic: p.hpfHz = 120.0f; p.compThresholdDb = -28.0f; break;
            // Where the honk sits moves with the horn. An alto honks higher than a tenor, and
            // a baritone is a different instrument again: more weight, a lower high-pass.
            case ChannelRole::SaxAlto:     p.hpfHz = 110.0f; p.correctiveBands[0].freqHz = 1500.0f; break;
            case ChannelRole::SaxTenor:    p.hpfHz = 85.0f;  p.correctiveBands[0].freqHz = 1100.0f; break;
            case ChannelRole::SaxBari:     p.hpfHz = 55.0f;  p.correctiveBands[0].freqHz = 800.0f;
                                           p.toneBands[0] = band (true, FilterType::LowShelf, 180.0f, 1.0f, 0.7f); break;
            case ChannelRole::KickOut:     p.hpfHz = 24.0f; p.toneBands[0].freqHz = 50.0f; p.gateScHpfHz = 25.0f; break;
            case ChannelRole::SnareBottom: p.hpfHz = 200.0f; p.toneBands[0].enabled = false; p.toneBands[3].gainDb = 2.0f; p.gateScHpfHz = 250.0f; break;
            case ChannelRole::FloorTom:    p.hpfHz = 38.0f; p.toneBands[0].freqHz = 80.0f; p.correctiveBands[0].freqHz = 300.0f; p.toneBands[2].freqHz = 2500.0f; p.gateScHpfHz = 60.0f; p.gateHoldMs = 150.0f; p.gateReleaseMs = 200.0f; break;
            case ChannelRole::RackTom:     p.toneBands[0].freqHz = 120.0f; break;
            case ChannelRole::SynthLead:   p.widthAmount = 1.0f; p.hpfHz = 70.0f; p.toneBands[2] = band (true, FilterType::Peak, 3000.0f, 1.0f, 1.0f); break;
            case ChannelRole::MasterBroadcast: p.outputTrimDb = -6.0f; p.limiterCeilingDb = -2.0f; break;
            case ChannelRole::MasterRecording: p.outputTrimDb = -3.0f; p.limiterCeilingDb = -1.0f; p.compThresholdDb = -20.0f; p.compRatio = 1.5f; p.satEnabled = false; break;
            case ChannelRole::MasterRoom:      p.compEnabled = false; p.satEnabled = false; p.widthEnabled = false; p.limiterCeilingDb = -0.5f; p.limiterReleaseMs = 80.0f; break;
            case ChannelRole::ElectricGuitarClean: p.hpfHz = 70.0f; p.lpfEnabled = false; p.lpfHz = 12000.0f; p.satEnabled = false; p.satDrive = 0.0f;
                                                   p.correctiveBands[2].enabled = false; p.toneBands[3] = band (true, FilterType::HighShelf, 8000.0f, 1.0f, 0.7f); break;
            case ChannelRole::BassAmp:   p.lpfEnabled = true; p.lpfHz = 5000.0f; p.correctiveBands[0] = band (true, FilterType::Peak, 350.0f, -1.5f, 1.2f); p.satDrive = 0.15f; break;
            default: break;
        }
    }

    // Target refinements per role (delivery loudness for the master outputs, pad width).
    void applyRoleTargetRefinements (ChannelRole role, SourceTargets& t)
    {
        switch (role)
        {
            case ChannelRole::SynthPad:        t.widthTarget = std::max (t.widthTarget, 1.15f); t.mixPeakTargetDb = -16.0f; break;
            case ChannelRole::SynthLead:       t.widthTarget = 1.0f; t.mixPeakTargetDb = -12.0f; t.kitBalanceRelDb = 0.0f; break;
            case ChannelRole::MasterBroadcast: t.targetLufs = -23.0f; t.truePeakCeilingDb = -2.0f; t.loudnessToleranceLu = 1.0f; break;
            case ChannelRole::MasterStream:    t.targetLufs = -14.0f; t.truePeakCeilingDb = -1.0f; t.loudnessToleranceLu = 1.0f; break;
            case ChannelRole::MasterRecording: t.targetLufs = -18.0f; t.truePeakCeilingDb = -1.0f; t.loudnessToleranceLu = 2.0f; t.compTargetGrDb = 1.5f; t.compRatioMax = 2.0f; break;
            case ChannelRole::MasterRoom:      t.loudnessTargetAppropriate = false; t.truePeakCeilingDb = -0.5f; t.compressionAppropriate = false; t.widthAppropriate = false; break;
            case ChannelRole::ElectricGuitarClean: t.bandTargetDb[size_t (Band::Brilliance)] = -16.0f; t.bandTargetDb[size_t (Band::Air)] = -26.0f;
                                                   t.harshnessMinHz = 2500.0f; t.harshnessMaxHz = 6000.0f; t.saturationAppropriate = false;
                                                   t.crestFactorMaxDb = 20.0f; t.crestFactorMinDb = 8.0f; break;
            case ChannelRole::ElectricGuitarDrive: t.crestFactorMaxDb = 16.0f; t.crestFactorMinDb = 5.0f; t.compTargetGrDb = 2.0f; t.compRatioMax = 3.0f; break;
            case ChannelRole::BassAmp:   t.bandTargetDb[size_t (Band::Brilliance)] = -58.0f; t.bandTargetDb[size_t (Band::Air)] = -66.0f;
                                         t.bandToleranceDb[size_t (Band::Air)] = 12.0f; t.harshnessMinHz = 1500.0f; t.harshnessMaxHz = 4000.0f; t.boxinessHz = 350.0f; break;
            default: break;
        }
    }
}

namespace Profiles
{

const ProfileDefinition& definition (StyleProfileId id)
{
    static const ProfileDefinition gospel = buildModernGospel();
    static const ProfileDefinition worship = buildModernWorship();
    switch (id)
    {
        case StyleProfileId::ModernWorship: return worship;
        case StyleProfileId::ModernGospel:
        default:                            return gospel;
    }
}

const SourceTargets& targets (StyleProfileId id, RoleFamily family)
{
    const int f = int (family) >= 0 && int (family) < int (RoleFamily::Count) ? int (family) : 0;
    return definition (id).targets[size_t (f)];
}

SourceTargets targets (StyleProfileId id, ChannelRole role)
{
    SourceTargets t = targets (id, roleFamily (role));
    applyRoleTargetRefinements (role, t);
    return t;
}

ChannelParameters baseline (StyleProfileId id, ChannelRole role)
{
    const int f = int (roleFamily (role));
    ChannelParameters p = definition (id).baselines[size_t (f)];
    applyRoleRefinements (role, p);
    return p;
}

} // namespace Profiles
} // namespace livemix
