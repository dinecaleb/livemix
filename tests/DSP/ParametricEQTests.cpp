#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/ParametricEQ.h"

using namespace livemix;

namespace
{
    float responseDb (ParametricEQ& eq, double sr, float freq)
    {
        const int n = int (sr);
        testsig::Buffer in (1, n);
        testsig::fillSine (in, freq, 0.25f, sr);
        eq.reset();
        auto v = in.view();
        eq.process (v);
        return testsig::toDb (testsig::rms (in, 0, n / 2, n) / (0.25f / std::sqrt (2.0f)));
    }
}

TEST_CASE ("ParametricEQ: cascaded bands combine")
{
    const double sr = 48000.0;
    ParametricEQ eq;
    eq.setNumBands (4);
    eq.prepare (sr, 512, 1);
    eq.setBand (0, { true, FilterType::LowShelf, 100.0f, 3.0f, 0.7f });
    eq.setBand (1, { true, FilterType::Peak, 400.0f, -4.0f, 1.5f });
    eq.setBand (2, { true, FilterType::Peak, 4000.0f, 2.0f, 1.0f });
    eq.setBand (3, { false, FilterType::HighShelf, 8000.0f, 10.0f, 0.7f }); // disabled
    { testsig::Buffer s (1, 8192); auto v = s.view(); eq.process (v); } // settle smoothing

    CHECK_NEAR (responseDb (eq, sr, 30.0f), 3.0f, 0.3f);
    CHECK_NEAR (responseDb (eq, sr, 400.0f), -4.0f, 0.3f);
    CHECK_NEAR (responseDb (eq, sr, 4000.0f), 2.0f, 0.3f);
    CHECK_NEAR (responseDb (eq, sr, 15000.0f), 0.0f, 0.3f); // disabled band contributes nothing
}

TEST_CASE ("ParametricEQ: gain automation ramps without discontinuity")
{
    const double sr = 48000.0;
    ParametricEQ eq;
    eq.setNumBands (1);
    eq.prepare (sr, 64, 1);
    testsig::Buffer in (1, 64 * 100);
    testsig::fillSine (in, 1000.0f, 0.5f, sr);
    for (int blk = 0; blk < 100; ++blk)
    {
        eq.setBand (0, { true, FilterType::Peak, 1000.0f, (blk % 2 == 0) ? 6.0f : -6.0f, 1.0f });
        auto v = in.view (blk * 64, 64);
        eq.process (v);
    }
    CHECK (testsig::allFinite (in));
    // Max sample-to-sample jump must stay bounded (no zipper clicks).
    float maxJump = 0.0f;
    for (int i = 1; i < in.numSamples(); ++i)
        maxJump = std::max (maxJump, std::fabs (in.data[0][size_t (i)] - in.data[0][size_t (i - 1)]));
    CHECK (maxJump < 0.25f);
}
