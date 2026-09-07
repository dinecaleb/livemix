#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/GateExpander.h"
#include "DSP/Compressor.h"

using namespace livemix;

TEST_CASE ("Gate detector HPF: a 40 Hz rumble no longer opens a gate whose detector sits at 120 Hz")
{
    const double sr = 48000.0;
    auto run = [&] (float detHpf)
    {
        GateExpander g;
        g.prepare (sr, 512, 1);
        GateExpander::Params p;
        p.enabled = true; p.thresholdDb = -20.0f; p.rangeDb = 40.0f; p.attackMs = 0.1f; p.holdMs = 5.0f; p.releaseMs = 20.0f;
        p.hysteresisDb = 3.0f; p.ratio = 20.0f; p.detectorHpfHz = detHpf;
        g.setParams (p);
        testsig::Buffer b (1, 48000);
        testsig::fillSine (b, 40.0f, 0.5f, sr); // -6 dBFS rumble, well above threshold
        for (int i = 0; i < 48000; i += 512) { auto v = b.view (i, 512); g.process (v); }
        return testsig::rms (b, 0, 24000, 48000);
    };
    const float open = run (0.0f);
    const float filtered = run (120.0f);
    CHECK (open > 0.3f);              // unfiltered: gate opens, rumble passes
    CHECK (filtered < open * 0.2f);   // filtered detector: rumble is below threshold, gate stays closed
}

TEST_CASE ("Compressor detector HPF: sub content drives far less gain reduction; audio path stays full band")
{
    const double sr = 48000.0;
    auto grFor = [&] (float detHpf)
    {
        Compressor c;
        c.prepare (sr, 512, 1);
        Compressor::Params p;
        p.enabled = true; p.thresholdDb = -30.0f; p.ratio = 4.0f; p.attackMs = 1.0f; p.releaseMs = 50.0f; p.kneeDb = 0.0f; p.detectorHpfHz = detHpf;
        c.setParams (p);
        testsig::Buffer b (1, 24000);
        testsig::fillSine (b, 40.0f, 0.5f, sr);
        for (int i = 0; i < 24000; i += 512) { auto v = b.view (i, 512); c.process (v); }
        return -c.getGainReductionDb();
    };
    const float full = grFor (0.0f);
    const float filtered = grFor (150.0f);
    CHECK (full > 10.0f);
    CHECK (filtered < full - 8.0f);

    // Same compressor on 1 kHz: the detector filter is transparent there.
    Compressor c;
    c.prepare (sr, 512, 1);
    Compressor::Params p;
    p.enabled = true; p.thresholdDb = -30.0f; p.ratio = 4.0f; p.attackMs = 1.0f; p.releaseMs = 50.0f; p.kneeDb = 0.0f; p.detectorHpfHz = 150.0f;
    c.setParams (p);
    testsig::Buffer b (1, 24000);
    testsig::fillSine (b, 1000.0f, 0.5f, sr);
    for (int i = 0; i < 24000; i += 512) { auto v = b.view (i, 512); c.process (v); }
    CHECK_NEAR (-c.getGainReductionDb(), 18.0f, 1.5f); // (-6 - -30) * (1 - 1/4)
}
