#include "TestFramework.h"
#include "Analysis/KitAnalysis.h"
#include "Recommendations/RecommendationEngine.h"
#include "Profiles/StyleProfile.h"

using namespace livemix;

namespace
{
    KitMember member (uint64_t id, ChannelRole role, float peakDb, float outputPeakDb, float lowMidExcess = 0.0f, float bleed = 0.1f)
    {
        KitMember m;
        m.instanceId = id; m.role = role; m.name = channelRoleName (role);
        AnalysisResult a;
        a.valid = true;
        a.peakDb = peakDb; a.rmsDb = peakDb - 14.0f; a.crestFactorDb = 14.0f;
        a.hitLevelDb = peakDb - 2.0f; a.noiseFloorDb = -60.0f; a.dynamicRangeDb = 46.0f;
        a.silencePercent = 20.0f; a.transientCount = 20; a.meanTransientRiseDb = 25.0f;
        a.bandEnergyDb = StyleProfile::targets (role, StyleProfileId::ModernWorship).bandTargetDb;
        a.bandEnergyDb[size_t (Band::LowMid)] += lowMidExcess;
        a.bleedEstimate = bleed;
        m.analysis = a;
        m.output.valid = true; m.output.peakDb = outputPeakDb; m.output.rmsDb = outputPeakDb - 14.0f;
        m.recommendations = RecommendationEngine::recommend (a, role, StyleProfileId::ModernWorship, ChannelParameters {}, nullptr);
        return m;
    }
}

TEST_CASE ("KitAnalysis: capture table, processing highlights, balance and notes")
{
    std::vector<KitMember> kit {
        member (1, ChannelRole::KickIn, -12.0f, -10.0f),
        member (2, ChannelRole::SnareTop, -13.0f, -11.0f, 9.0f),          // low-mid buildup -> processing note
        member (3, ChannelRole::RackTom, -30.0f, -22.0f),                 // low capture, 9 dB below balance target
        member (4, ChannelRole::FloorTom, -12.0f, -13.0f, 0.0f, 0.8f),    // bleed -> gate
        member (5, ChannelRole::OverheadLeft, -20.0f, -8.0f),             // too loud -> overheads dominate
        member (6, ChannelRole::OverheadRight, -20.0f, -8.0f),
    };
    auto r = KitAnalysis::analyze (kit, StyleProfileId::ModernWorship);
    CHECK (r.valid);
    CHECK (r.membersAnalyzed == 6);
    CHECK (r.capture.size() == 6);
    bool tomLow = false;
    for (auto& c : r.capture) if (c.instanceId == 3) { tomLow = c.health == "Low" && c.captureGainDb >= 10.0f; }
    CHECK (tomLow);

    bool snareNote = false, floorGate = false;
    for (auto& n : r.processing)
    {
        if (n.instanceId == 2 && n.why.find ("ow-mid") != std::string::npos) snareNote = true;
        if (n.instanceId == 4 && n.what.find ("Expander") != std::string::npos) floorGate = true;
    }
    CHECK (snareNote);
    CHECK (floorGate);

    bool tomBalance = false, ohBalance = false;
    for (auto& b : r.balance)
    {
        if (b.instanceId == 3) { tomBalance = true; CHECK (b.outputTrimDeltaDb >= 5.0f); CHECK (b.confidence == Confidence::High); }
        if (b.instanceId == 5) { ohBalance = true; CHECK (b.outputTrimDeltaDb <= -5.0f); }
    }
    CHECK (tomBalance);
    CHECK (ohBalance);

    bool healthy = false, ohDominate = false;
    for (auto& n : r.notes)
    {
        if (n.find ("Kick/snare relationship is healthy") != std::string::npos) healthy = true;
        if (n.find ("Overheads dominate") != std::string::npos) ohDominate = true;
    }
    CHECK (healthy);
    CHECK (ohDominate);
}

TEST_CASE ("KitAnalysis: empty or invalid members produce an invalid result; balanced kit is quiet")
{
    CHECK (! KitAnalysis::analyze ({}, StyleProfileId::ModernWorship).valid);
    KitMember bad; bad.instanceId = 9;
    CHECK (! KitAnalysis::analyze ({ bad }, StyleProfileId::ModernWorship).valid);

    std::vector<KitMember> kit { member (1, ChannelRole::KickIn, -12.0f, -10.0f), member (2, ChannelRole::SnareTop, -12.0f, -11.0f),
                                 member (3, ChannelRole::RackTom, -12.0f, -13.0f), member (4, ChannelRole::Overhead, -18.0f, -16.0f) };
    auto r = KitAnalysis::analyze (kit, StyleProfileId::ModernWorship);
    CHECK (r.valid);
    CHECK (r.balance.empty());
    CHECK (r.processing.empty());
}
