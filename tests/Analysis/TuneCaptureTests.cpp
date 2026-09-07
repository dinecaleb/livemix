#include "TestFramework.h"
#include "TestSignals.h"
#include "Analysis/AnalysisEngine.h"
#include <thread>
#include <chrono>

using namespace livemix;

namespace
{
    void waitFor (AnalysisEngine& e, AnalysisEngine::State s, int maxMs = 5000)
    {
        for (int i = 0; i < maxMs && e.getState() != s; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    void feed (AnalysisEngine& e, testsig::Buffer& b, int block = 256)
    {
        for (int i = 0; i + block <= b.numSamples(); i += block)
        {
            auto v = b.view (i, block);
            e.pushAudio (v);
            if ((i / block) % 8 == 0) std::this_thread::sleep_for (std::chrono::microseconds (300));
        }
    }
}

TEST_CASE ("Tune capture: waits for signal, then the capture window starts at the onset")
{
    const double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 1);
    engine.startCapture (1.0f, -40.0f, 10.0f);
    waitFor (engine, AnalysisEngine::State::Waiting);
    REQUIRE (engine.isWaitingForSignal());
    REQUIRE (engine.isActive());

    testsig::Buffer silence (1, int (sr * 1.5));
    feed (engine, silence);
    std::this_thread::sleep_for (std::chrono::milliseconds (60));
    CHECK (engine.isWaitingForSignal());       // 1.5 s of silence did not start the window
    CHECK_NEAR (engine.getProgress(), 0.0f, 0.001f);

    testsig::Buffer hits (1, int (sr * 1.5));
    testsig::fillDrumHits (hits, sr, 0.5f, 0.001f, 0.25f, 0.05f, 100.0f);
    feed (engine, hits);
    for (int i = 0; i < 5000 && (engine.getState() == AnalysisEngine::State::Waiting || engine.getState() == AnalysisEngine::State::Capturing || engine.getState() == AnalysisEngine::State::Processing); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    REQUIRE (engine.getState() == AnalysisEngine::State::Complete);
    auto r = engine.getResult();
    CHECK (r.valid);
    CHECK_NEAR (r.durationSeconds, 1.0f, 0.05f);
    CHECK (r.silencePercent < 30.0f);           // the window contains the hits, not the silence
    CHECK (r.transientCount >= 3);
}

TEST_CASE ("Tune capture: the wait times out and captures anyway; abort while waiting returns to Idle")
{
    const double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 1);
    engine.startCapture (0.3f, -40.0f, 0.5f);
    waitFor (engine, AnalysisEngine::State::Waiting);
    testsig::Buffer silence (1, int (sr * 1.2));
    feed (engine, silence);
    for (int i = 0; i < 5000 && engine.getState() != AnalysisEngine::State::Complete; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    REQUIRE (engine.getState() == AnalysisEngine::State::Complete);
    CHECK (engine.getResult().silencePercent > 95.0f);

    engine.startCapture (1.0f, -40.0f, 30.0f);
    waitFor (engine, AnalysisEngine::State::Waiting);
    REQUIRE (engine.isWaitingForSignal());
    engine.abort();
    CHECK (engine.getState() == AnalysisEngine::State::Idle);
    CHECK (! engine.isActive());
}

TEST_CASE ("Analysis: fundamental and decay are measured on a synthetic tom")
{
    const double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 1);
    testsig::Buffer tom (1, int (sr * 3));
    // 90 Hz tone bursts every 0.5 s, decaying with a 120 ms time constant (about 240 ms to -20 dB).
    testsig::fillDrumHits (tom, sr, 0.6f, 0.0005f, 0.5f, 0.12f, 90.0f);
    engine.startCapture (2.5f);
    waitFor (engine, AnalysisEngine::State::Capturing);
    feed (engine, tom);
    for (int i = 0; i < 5000 && engine.getState() != AnalysisEngine::State::Complete; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    REQUIRE (engine.getState() == AnalysisEngine::State::Complete);
    auto r = engine.getResult();
    CHECK_NEAR (r.fundamentalHz, 90.0f, 8.0f);
    CHECK (r.fundamentalLevelDb > -20.0f);
    CHECK (r.decayCount >= 3);
    CHECK (r.meanDecayMs > 150.0f && r.meanDecayMs < 400.0f);
}
