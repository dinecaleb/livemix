#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/Compressor.h"

using namespace livemix;

TEST_CASE ("Compressor: static gain computer")
{
    // Hard knee
    CHECK_NEAR (Compressor::computeGain (-30.0f, -20.0f, 4.0f, 0.0f), -30.0f, 1e-5f);
    CHECK_NEAR (Compressor::computeGain (-10.0f, -20.0f, 4.0f, 0.0f), -17.5f, 1e-5f);
    // Soft knee: continuous at the knee edges, exactly halfway through the knee
    CHECK_NEAR (Compressor::computeGain (-23.0f, -20.0f, 4.0f, 6.0f), -23.0f, 1e-5f);
    CHECK_NEAR (Compressor::computeGain (-17.0f, -20.0f, 4.0f, 6.0f), -19.25f, 1e-5f);
    const float mid = Compressor::computeGain (-20.0f, -20.0f, 4.0f, 6.0f);
    CHECK (mid < -20.0f && mid > -21.5f);
}

TEST_CASE ("Compressor: steady-state gain reduction on a sine")
{
    const double sr = 48000.0;
    Compressor comp;
    comp.prepare (sr, 512, 1);
    Compressor::Params p;
    p.enabled = true; p.thresholdDb = -20.0f; p.ratio = 4.0f; p.attackMs = 1.0f; p.releaseMs = 50.0f; p.kneeDb = 0.0f;
    comp.setParams (p);

    testsig::Buffer in (1, int (sr));
    testsig::fillSine (in, 1000.0f, 1.0f, sr); // 0 dBFS peak -> 20 dB over -> 15 dB reduction
    auto v = in.view();
    comp.process (v);
    const float outPeak = [&] { float m = 0; for (int i = in.numSamples() / 2; i < in.numSamples(); ++i) m = std::max (m, std::fabs (in.data[0][size_t (i)])); return m; }();
    CHECK_NEAR (testsig::toDb (outPeak), -15.0f, 0.4f);
    CHECK_NEAR (comp.getGainReductionDb(), -15.0f, 0.4f);
}

TEST_CASE ("Compressor: makeup and mix")
{
    const double sr = 48000.0;
    Compressor comp;
    comp.prepare (sr, 512, 2);
    Compressor::Params p;
    p.enabled = true; p.thresholdDb = -20.0f; p.ratio = 100.0f; p.attackMs = 0.1f; p.releaseMs = 10.0f; p.kneeDb = 0.0f;
    p.makeupDb = 10.0f; p.mix = 0.5f;
    comp.setParams (p);
    testsig::Buffer in (2, int (sr));
    testsig::fillSine (in, 500.0f, 1.0f, sr);
    auto v = in.view();
    comp.process (v);
    // Wet: 0 dBFS limited to -20 dBFS, +10 makeup => -10 dBFS (0.316). Dry: 1.0. Mix 50% => 0.658.
    const float outPeak = [&] { float m = 0; for (int i = in.numSamples() / 2; i < in.numSamples(); ++i) m = std::max (m, std::fabs (in.data[1][size_t (i)])); return m; }();
    CHECK_NEAR (outPeak, 0.658f, 0.02f);
}

TEST_CASE ("Compressor: silence, denormals and extreme inputs stay finite")
{
    Compressor comp;
    comp.prepare (48000.0, 64, 1);
    Compressor::Params p; p.enabled = true; p.thresholdDb = -60.0f; p.ratio = 20.0f;
    comp.setParams (p);
    testsig::Buffer in (1, 4096);
    auto v = in.view();
    comp.process (v);
    CHECK (testsig::allFinite (in));
    for (auto& x : in.data[0]) x = 1.0e-38f;
    comp.process (v);
    CHECK (testsig::allFinite (in));
    testsig::fillNoise (in, 1000.0f);
    comp.process (v);
    CHECK (testsig::allFinite (in));
}
