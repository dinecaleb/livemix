#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "Analysis/AnalysisEngine.h"
#include <thread>
#include <chrono>

using namespace livemix;

namespace
{
    // Feeds a buffer in real-time-ish blocks and waits for the result.
    AnalysisResult runAnalysis (AnalysisEngine& engine, testsig::Buffer& buf, float seconds, int block = 128)
    {
        engine.startCapture (seconds);
        for (int i = 0; i < 200 && ! engine.isCapturing(); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        REQUIRE (engine.isCapturing());
        for (int i = 0; i + block <= buf.numSamples() && engine.getState() == AnalysisEngine::State::Capturing; i += block)
        {
            auto v = buf.view (i, block);
            engine.pushAudio (v);
            if ((i / block) % 16 == 0) std::this_thread::sleep_for (std::chrono::microseconds (500));
        }
        for (int i = 0; i < 5000 && (engine.getState() == AnalysisEngine::State::Capturing || engine.getState() == AnalysisEngine::State::Processing); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        REQUIRE (engine.getState() == AnalysisEngine::State::Complete);
        return engine.getResult();
    }
}

TEST_CASE ("AnalysisEngine: sine level, DC and spectrum")
{
    const double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 1);
    testsig::Buffer buf (1, int (sr * 3));
    testsig::fillSine (buf, 1000.0f, 0.5f, sr);
    auto r = runAnalysis (engine, buf, 2.0f);
    CHECK (r.valid);
    CHECK_NEAR (r.peakDb, -6.02f, 0.1f);
    CHECK_NEAR (r.rmsDb, -9.03f, 0.2f);
    CHECK_NEAR (r.crestFactorDb, 3.0f, 0.3f);
    CHECK_NEAR (r.dcOffset, 0.0f, 0.01f);
    CHECK (r.clipCount == 0);
    CHECK (r.silencePercent < 1.0f);
    CHECK_NEAR (r.durationSeconds, 2.0f, 0.05f);
    // Energy concentrated in the Mid band (400-1k). 1 kHz is on the edge; UpperMid or Mid dominates.
    const float mid = r.bandEnergyDb[size_t (Band::Mid)], upper = r.bandEnergyDb[size_t (Band::UpperMid)];
    CHECK (std::max (mid, upper) > -3.0f);
    CHECK (r.bandEnergyDb[size_t (Band::Sub)] < -30.0f);
    CHECK_NEAR (r.spectralCentroidHz, 1000.0f, 60.0f);
}

TEST_CASE ("AnalysisEngine: silence, clipping and drum hits")
{
    const double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 2);

    testsig::Buffer silence (2, int (sr * 2));
    auto s = runAnalysis (engine, silence, 1.0f);
    CHECK (s.valid);
    CHECK (s.silencePercent > 99.0f);
    CHECK (s.peakDb <= -120.0f);

    testsig::Buffer clipped (2, int (sr * 2));
    testsig::fillSine (clipped, 200.0f, 1.5f, sr);
    for (auto& c : clipped.data) for (auto& x : c) x = std::max (-1.0f, std::min (1.0f, x));
    auto c = runAnalysis (engine, clipped, 1.0f);
    CHECK (c.clipCount > 100);

    testsig::Buffer hits (2, int (sr * 4));
    testsig::fillDrumHits (hits, sr, 0.5f, 0.002f, 0.5f, 0.06f, 120.0f);
    auto h = runAnalysis (engine, hits, 3.0f);
    CHECK (h.valid);
    CHECK (h.transientCount >= 4 && h.transientCount <= 8);
    CHECK (h.crestFactorDb > 10.0f);
    CHECK (h.dynamicRangeDb > 20.0f);
    CHECK (h.noiseFloorDb < -40.0f);
    CHECK (h.bleedEstimate < 0.3f);
    CHECK_NEAR (h.stereoBalanceDb, 0.0f, 1.0f);

    // Heavy bleed: floor only 15 dB under the hits.
    testsig::Buffer bleed (2, int (sr * 4));
    testsig::fillDrumHits (bleed, sr, 0.5f, 0.06f, 0.5f, 0.06f, 120.0f);
    auto b = runAnalysis (engine, bleed, 3.0f);
    CHECK (b.bleedEstimate > 0.5f);
}

TEST_CASE ("AnalysisEngine: pushAudio never allocates, abort returns to idle")
{
    AnalysisEngine engine;
    engine.prepare (48000.0, 2);
    testsig::Buffer buf (2, 64);
    testsig::fillNoise (buf, 0.3f);
    auto v = buf.view();
    engine.startCapture (5.0f);
    for (int i = 0; i < 200 && ! engine.isCapturing(); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    {
        alloctrack::Scope scope;
        for (int i = 0; i < 1000; ++i) engine.pushAudio (v);
        CHECK (alloctrack::getCount() == 0);
    }
    engine.abort();
    CHECK (engine.getState() == AnalysisEngine::State::Idle);
    CHECK (! engine.isCapturing());
    // Idle engine ignores audio (no capture) and is still allocation-free.
    {
        alloctrack::Scope scope;
        for (int i = 0; i < 1000; ++i) engine.pushAudio (v);
        CHECK (alloctrack::getCount() == 0);
    }
}

TEST_CASE ("AnalysisEngine: rapid construct/start/destroy never deadlocks")
{
    // Regression for a lost-wakeup race between the destructor and the worker's
    // condition-variable wait (surfaced as an intermittent CTest hang).
    for (int i = 0; i < 400; ++i)
    {
        AnalysisEngine engine;
        engine.prepare (48000.0, 1);
        if (i % 3 == 0) engine.startCapture (1.0f);
        if (i % 5 == 0) engine.abort();
    }
    CHECK (true);
}
