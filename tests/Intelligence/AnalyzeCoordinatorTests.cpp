#include "TestFramework.h"
#include "Intelligence/AnalyzeCoordinator.h"
#include "Profiles/StyleProfile.h"
#include <thread>
#include <chrono>

using namespace livemix;

namespace
{
    AnalysisResult lowSnare()
    {
        AnalysisResult a;
        a.valid = true;
        a.peakDb = -28.0f; a.rmsDb = -40.0f; a.crestFactorDb = 12.0f;
        a.hitLevelDb = -30.0f; a.noiseFloorDb = -70.0f; a.dynamicRangeDb = 40.0f;
        a.silencePercent = 10.0f; a.transientCount = 20; a.meanTransientRiseDb = 20.0f;
        a.bandEnergyDb = StyleProfile::targets (ChannelRole::SnareTop, StyleProfileId::ModernWorship).bandTargetDb;
        return a;
    }

    struct MockProvider : IIntelligenceProvider
    {
        bool available = true;
        bool external = true;
        int delayMs = 0;
        bool returnInvalid = false;
        std::string getName() const override { return "Mock"; }
        bool isAvailable() const override { return available; }
        bool sendsDataExternally() const override { return external; }
        IntelligenceResponse interpret (const IntelligenceRequest& req, const std::atomic<bool>& cancel) override
        {
            for (int i = 0; i < delayMs && ! cancel.load(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
            IntelligenceResponse r;
            if (returnInvalid) { r.error = "bad json"; return r; }
            r.valid = true;
            r.providerName = getName();
            r.interpretation = "Sounds like a snare with " + std::to_string (req.standardRecommendations.items.size()) + " standard items.";
            Recommendation rec;
            rec.what = "Boost 5 kHz"; rec.why = "definition";
            rec.changes = { { "toneEq3Gain", 9.0f }, { "role", 1.0f } };
            rec.safeToAutoApply = true;
            r.recommendations.push_back (rec);
            return r;
        }
    };

    void waitUntilNotBusy (AnalyzeCoordinator& c)
    {
        for (int i = 0; i < 5000 && c.isBusy(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

TEST_CASE ("AnalyzeCoordinator: Standard mode never touches the provider")
{
    AnalyzeCoordinator c;
    auto mock = std::make_shared<MockProvider>();
    c.setProvider (mock);
    AnalyzeCoordinator::Options o; o.useAI = false;
    c.run (lowSnare(), ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {}, o);
    CHECK (c.getStage() == AnalyzeCoordinator::Stage::StandardComplete);
    auto r = c.getResult();
    CHECK (r.valid);
    CHECK (r.inputHealth == "Low");
    CHECK (c.getAIInterpretation().empty());
    for (auto& i : r.items) CHECK (i.what.rfind ("[AI]", 0) != 0);
}

TEST_CASE ("AnalyzeCoordinator: AI results are merged after validation")
{
    AnalyzeCoordinator c;
    auto mock = std::make_shared<MockProvider>();
    c.setProvider (mock);
    AnalyzeCoordinator::Options o; o.useAI = true; o.aiTimeoutMs = 2000;
    c.run (lowSnare(), ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {}, o);
    waitUntilNotBusy (c);
    CHECK (c.getStage() == AnalyzeCoordinator::Stage::AIComplete);
    auto r = c.getResult();
    CHECK (! c.getAIInterpretation().empty());
    bool foundAI = false;
    for (auto& i : r.items)
        if (i.what.rfind ("[AI]", 0) == 0)
        {
            foundAI = true;
            REQUIRE (i.changes.size() == 1);            // "role" dropped
            CHECK_NEAR (i.changes[0].value, 4.0f, 1e-6f); // 9 dB clamped to AI limit
            CHECK (! i.safeToAutoApply);
        }
    CHECK (foundAI);
    CHECK (r.inputHealth == "Low"); // standard result preserved
}

TEST_CASE ("AnalyzeCoordinator: unavailable, invalid and slow providers fall back to Standard")
{
    AnalyzeCoordinator c;
    auto mock = std::make_shared<MockProvider>();
    c.setProvider (mock);
    AnalyzeCoordinator::Options o; o.useAI = true; o.aiTimeoutMs = 150;

    mock->available = false;
    c.run (lowSnare(), ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {}, o);
    CHECK (c.getStage() == AnalyzeCoordinator::Stage::AIFallback);
    CHECK (c.getResult().valid);

    mock->available = true; mock->returnInvalid = true;
    c.run (lowSnare(), ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {}, o);
    waitUntilNotBusy (c);
    CHECK (c.getStage() == AnalyzeCoordinator::Stage::AIFallback);
    CHECK (c.getResult().inputHealth == "Low");

    mock->returnInvalid = false; mock->delayMs = 2000;
    c.run (lowSnare(), ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {}, o);
    waitUntilNotBusy (c);
    CHECK (c.getStage() == AnalyzeCoordinator::Stage::AIFallback);
    CHECK (c.getStatusMessage().find ("timed out") != std::string::npos);
    CHECK (c.getResult().valid);

    // Null provider: nothing configured.
    AnalyzeCoordinator d;
    d.run (lowSnare(), ChannelRole::SnareTop, StyleProfileId::ModernWorship, ChannelParameters {}, o);
    CHECK (d.getStage() == AnalyzeCoordinator::Stage::AIFallback);
    CHECK (d.getResult().valid);
}
