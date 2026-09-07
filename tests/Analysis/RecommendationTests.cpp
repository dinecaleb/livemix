#include "TestFramework.h"
#include "Recommendations/RecommendationEngine.h"
#include "Profiles/StyleProfile.h"
#include "State/ParameterIDs.h"

using namespace livemix;

namespace
{
    AnalysisResult healthySnare()
    {
        AnalysisResult a;
        a.valid = true;
        a.peakDb = -12.0f; a.rmsDb = -26.0f; a.crestFactorDb = 14.0f;
        a.hitLevelDb = -14.0f; a.noiseFloorDb = -60.0f; a.dynamicRangeDb = 46.0f;
        a.silencePercent = 20.0f;
        a.transientCount = 20; a.meanTransientRiseDb = 25.0f;
        a.bandEnergyDb = StyleProfile::targets (ChannelRole::SnareTop, StyleProfileId::ModernWorship).bandTargetDb;
        a.bleedEstimate = 0.1f;
        return a;
    }
    bool hasKind (const RecommendationResult& r, Recommendation::Kind k)
    {
        for (auto& i : r.items) if (i.kind == k) return true;
        return false;
    }
}

TEST_CASE ("Recommendations: healthy capture yields no capture-gain item")
{
    auto r = RecommendationEngine::recommend (healthySnare(), ChannelRole::SnareTop, StyleProfileId::ModernWorship, StyleProfile::baseline (ChannelRole::SnareTop, StyleProfileId::ModernWorship));
    CHECK (r.valid);
    CHECK (r.inputHealth == "Healthy");
    CHECK (! hasKind (r, Recommendation::Kind::CaptureGain));
    CHECK (! r.items.empty());
}

TEST_CASE ("Recommendations: low input produces a capture-gain recommendation that is NOT plugin-applicable")
{
    auto a = healthySnare();
    a.peakDb = -28.0f; a.hitLevelDb = -30.0f;
    auto r = RecommendationEngine::recommend (a, ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {});
    CHECK (r.inputHealth == "Low");
    CHECK (r.suggestedCaptureGainDb >= 10.0f);
    bool found = false;
    for (auto& i : r.items)
        if (i.kind == Recommendation::Kind::CaptureGain)
        {
            found = true;
            CHECK (i.changes.empty());
            CHECK (! i.safeToAutoApply);
            CHECK (i.confidence == Confidence::High);
            CHECK (i.what.find ("Increase") != std::string::npos);
        }
    CHECK (found);
}

TEST_CASE ("Recommendations: clipping and hot signals are flagged with high confidence")
{
    auto a = healthySnare();
    a.peakDb = -0.1f; a.clipCount = 40;
    auto r = RecommendationEngine::recommend (a, ChannelRole::KickIn, StyleProfileId::ModernGospel, ChannelParameters {});
    CHECK (r.inputHealth == "Clipping");
    CHECK (r.suggestedCaptureGainDb < -5.0f);
    a.clipCount = 0; a.peakDb = -3.0f;
    r = RecommendationEngine::recommend (a, ChannelRole::KickIn, StyleProfileId::ModernGospel, ChannelParameters {});
    CHECK (r.inputHealth == "Hot");
}

TEST_CASE ("Recommendations: bleed suggests a gate on close mics only; crest suggests compression")
{
    auto a = healthySnare();
    a.bleedEstimate = 0.7f; a.noiseFloorDb = -30.0f; a.hitLevelDb = -14.0f;
    auto r = RecommendationEngine::recommend (a, ChannelRole::RackTom, StyleProfileId::ModernWorship, ChannelParameters {});
    CHECK (hasKind (r, Recommendation::Kind::Gate));
    for (auto& i : r.items)
        if (i.kind == Recommendation::Kind::Gate)
        {
            CHECK (i.safeToAutoApply);
            CHECK (i.changes.size() >= 2);
            for (auto& c : i.changes)
                if (c.paramId == ParamID::gateThreshold) CHECK (c.value > -30.0f && c.value < -14.0f);
        }
    auto oh = RecommendationEngine::recommend (a, ChannelRole::OverheadLeft, StyleProfileId::ModernWorship, ChannelParameters {});
    CHECK (! hasKind (oh, Recommendation::Kind::Gate));

    auto c = healthySnare();
    c.crestFactorDb = 28.0f;
    auto rc = RecommendationEngine::recommend (c, ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {});
    CHECK (hasKind (rc, Recommendation::Kind::Compression));
}

TEST_CASE ("Recommendations: low-mid buildup yields an EQ cut that is safe to apply")
{
    auto a = healthySnare();
    a.bandEnergyDb[size_t (Band::LowMid)] += 9.0f;
    auto r = RecommendationEngine::recommend (a, ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {});
    bool found = false;
    for (auto& i : r.items)
        if (i.kind == Recommendation::Kind::EQ)
        {
            found = true;
            CHECK (i.safeToAutoApply);
            CHECK (i.why.find ("ow-mid") != std::string::npos);
            bool gainNegative = false;
            for (auto& ch : i.changes) if (ch.paramId.find ("Gain") != std::string::npos) gainNegative = ch.value < 0.0f;
            CHECK (gainNegative);
        }
    CHECK (found);

    AnalysisResult none; none.valid = false;
    CHECK (! RecommendationEngine::recommend (none, ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {}).valid);
}
