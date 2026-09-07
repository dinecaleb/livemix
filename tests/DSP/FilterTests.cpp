#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/FilterProcessor.h"

using namespace livemix;

namespace
{
    float responseDb (FilterProcessor& f, double sr, float freq)
    {
        const int n = int (sr);
        testsig::Buffer in (2, n);
        testsig::fillSine (in, freq, 0.5f, sr);
        f.reset();
        auto v = in.view();
        f.process (v);
        return testsig::toDb (testsig::rms (in, 1, n / 2, n) / (0.5f / std::sqrt (2.0f)));
    }
}

TEST_CASE ("FilterProcessor: HPF 12 vs 24 dB/oct slopes")
{
    const double sr = 48000.0;
    FilterProcessor f;
    FilterProcessor::Params p;
    p.hpfEnabled = true; p.hpfHz = 200.0f; p.hpfSlopeDbPerOct = 12;
    f.prepare (sr, 512, 2);
    f.setParams (p);
    // Settle smoothing
    { testsig::Buffer s (2, 8192); auto v = s.view(); f.process (v); }
    const float oneOctBelow12 = responseDb (f, sr, 100.0f);
    CHECK_NEAR (oneOctBelow12, -12.0f, 1.5f);
    CHECK_NEAR (responseDb (f, sr, 4000.0f), 0.0f, 0.2f);

    p.hpfSlopeDbPerOct = 24;
    f.setParams (p);
    { testsig::Buffer s (2, 8192); auto v = s.view(); f.process (v); }
    const float oneOctBelow24 = responseDb (f, sr, 100.0f);
    CHECK_NEAR (oneOctBelow24, -24.0f, 2.0f);
}

TEST_CASE ("FilterProcessor: disabled is bit-exact pass-through")
{
    FilterProcessor f;
    f.prepare (44100.0, 256, 1);
    testsig::Buffer in (1, 256);
    testsig::fillNoise (in, 0.9f);
    auto ref = in.data[0];
    auto v = in.view();
    f.process (v);
    CHECK (in.data[0] == ref);
}

TEST_CASE ("FilterProcessor: frequency sweep produces finite output")
{
    FilterProcessor f;
    f.prepare (48000.0, 64, 2);
    testsig::Buffer in (2, 64 * 200);
    testsig::fillNoise (in, 0.5f);
    for (int blk = 0; blk < 200; ++blk)
    {
        FilterProcessor::Params p;
        p.hpfEnabled = true; p.hpfHz = 20.0f + float (blk) * 5.0f;
        p.lpfEnabled = true; p.lpfHz = 20000.0f - float (blk) * 80.0f; p.lpfSlopeDbPerOct = 24;
        f.setParams (p);
        auto v = in.view (blk * 64, 64);
        f.process (v);
    }
    CHECK (testsig::allFinite (in));
}
