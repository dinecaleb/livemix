#include "TestFramework.h"
#include "Recommendations/RecommendationEngine.h"
#include "Profiles/StyleProfile.h"

using namespace livemix;

TEST_CASE ("Recommendations: mix gain uses processed output stats and writes output trim only")
{
    AnalysisResult a;
    a.valid = true;
    a.peakDb = -12.0f; a.rmsDb = -26.0f; a.crestFactorDb = 14.0f;
    a.hitLevelDb = -14.0f; a.noiseFloorDb = -60.0f; a.dynamicRangeDb = 46.0f;
    a.silencePercent = 20.0f; a.transientCount = 20; a.meanTransientRiseDb = 25.0f;
    a.bandEnergyDb = StyleProfile::targets (ChannelRole::RackTom, StyleProfileId::ModernWorship).bandTargetDb;

    OutputStats out; out.valid = true; out.peakDb = -22.0f; out.rmsDb = -34.0f; // tom target is -13
    ChannelParameters current; current.outputTrimDb = 1.0f;
    auto r = RecommendationEngine::recommend (a, ChannelRole::RackTom, StyleProfileId::ModernWorship, current, &out);
    bool found = false;
    for (auto& i : r.items)
        if (i.kind == Recommendation::Kind::MixGain)
        {
            found = true;
            REQUIRE (i.changes.size() == 1);
            CHECK (i.changes[0].paramId == "outputTrim");
            CHECK_NEAR (i.changes[0].value, 1.0f + 8.0f, 1e-4f); // 9 dB wanted, bounded to the 8 dB step
            CHECK (i.safeToAutoApply);
            CHECK (i.what.find ("mix level") != std::string::npos);
        }
    CHECK (found);

    // Close to target: no item. No stats: no item.
    out.peakDb = -14.0f;
    r = RecommendationEngine::recommend (a, ChannelRole::RackTom, StyleProfileId::ModernWorship, current, &out);
    for (auto& i : r.items) CHECK (i.kind != Recommendation::Kind::MixGain);
    r = RecommendationEngine::recommend (a, ChannelRole::RackTom, StyleProfileId::ModernWorship, current, nullptr);
    for (auto& i : r.items) CHECK (i.kind != Recommendation::Kind::MixGain);
}
