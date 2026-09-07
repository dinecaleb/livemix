#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/GateExpander.h"

using namespace livemix;

TEST_CASE ("GateExpander: attenuates below threshold, opens on hits")
{
    const double sr = 48000.0;
    GateExpander gate;
    gate.prepare (sr, 512, 1);
    GateExpander::Params p;
    p.enabled = true; p.thresholdDb = -30.0f; p.rangeDb = 40.0f;
    p.attackMs = 0.2f; p.holdMs = 20.0f; p.releaseMs = 30.0f; p.hysteresisDb = 3.0f; p.ratio = 20.0f;
    gate.setParams (p);

    // 0.5 s of quiet noise at -50 dBFS, then a loud burst at -6 dBFS.
    const int n = int (sr);
    testsig::Buffer in (1, n);
    testsig::fillNoise (in, 0.00316f);
    for (int i = n / 2; i < n / 2 + 4800; ++i) in.data[0][size_t (i)] = 0.5f * ((i % 7) - 3) / 3.0f;
    auto v = in.view();
    gate.process (v);

    const float quietRms = testsig::rms (in, 0, 4800, n / 2 - 100);
    CHECK (testsig::toDb (quietRms) < -80.0f); // -50 dBFS - 40 dB range
    const float burstPeak = [&] { float m = 0; for (int i = n / 2 + 480; i < n / 2 + 4800; ++i) m = std::max (m, std::fabs (in.data[0][size_t (i)])); return m; }();
    CHECK (burstPeak > 0.45f); // open within the burst
    CHECK (testsig::allFinite (in));
}

TEST_CASE ("GateExpander: opens within a fraction of a millisecond")
{
    const double sr = 48000.0;
    GateExpander gate;
    gate.prepare (sr, 64, 1);
    GateExpander::Params p;
    p.enabled = true; p.thresholdDb = -20.0f; p.rangeDb = 60.0f; p.attackMs = 0.1f; p.ratio = 20.0f;
    gate.setParams (p);

    testsig::Buffer in (1, 4800);
    // silence then a step to 0.8
    for (int i = 2400; i < 4800; ++i) in.data[0][size_t (i)] = 0.8f;
    auto v = in.view();
    gate.process (v);
    // Within 0.5 ms (24 samples) the gate must be > 90% open.
    CHECK (in.data[0][2400 + 24] > 0.72f);
}

TEST_CASE ("GateExpander: disabled is unity and hold keeps it open")
{
    const double sr = 44100.0;
    GateExpander gate;
    gate.prepare (sr, 256, 2);
    GateExpander::Params p;
    p.enabled = false;
    gate.setParams (p);
    testsig::Buffer in (2, 256);
    testsig::fillNoise (in, 0.001f);
    auto ref = in.data[0];
    auto v = in.view();
    gate.process (v);
    CHECK (in.data[0] == ref);

    p.enabled = true; p.thresholdDb = -30.0f; p.holdMs = 200.0f; p.releaseMs = 5.0f; p.rangeDb = 60.0f; p.ratio = 20.0f;
    gate.setParams (p);
    gate.reset();
    testsig::Buffer h (2, int (sr) / 2);
    for (int i = 0; i < 100; ++i) { h.data[0][size_t (i)] = 0.5f; h.data[1][size_t (i)] = 0.5f; }
    for (int i = 100; i < h.numSamples(); ++i) { h.data[0][size_t (i)] = 0.01f; h.data[1][size_t (i)] = 0.01f; }
    auto hv = h.view();
    gate.process (hv);
    // 100 ms after the hit (inside the 200 ms hold) the gate is still open.
    CHECK_NEAR (h.data[0][size_t (int (sr) / 10)], 0.01f, 0.001f);
    // 400 ms after the hit (past hold + release) the gate is closed.
    CHECK (std::fabs (h.data[0][size_t (int (sr) * 4 / 10)]) < 0.001f);
}
