#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "FX/DelayAlgorithm.h"
#include "FX/FxParameters.h"
#include <cmath>
#include <vector>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    DelayAlgorithm::Params bare()
    {
        DelayAlgorithm::Params p;
        p.enabled = true; p.mode = int (DelayMode::Mono); p.sync = false; p.timeMs = 100.0f; p.division = int (NoteDivision::Quarter);
        p.offsetPercent = 0.0f; p.feedback = 50.0f; p.lowCutHz = 20.0f; p.highCutHz = 20000.0f; p.width = 100.0f;
        p.duck = 0.0f; p.duckReleaseMs = 300.0f; p.modRateHz = 0.5f; p.modDepth = 0.0f; p.levelDb = 0.0f;
        return p;
    }

    void run (DelayAlgorithm& d, testsig::Buffer& buf, int block = 512)
    {
        for (int off = 0; off < buf.numSamples(); off += block)
        {
            auto v = buf.view (off, std::min (block, buf.numSamples() - off));
            d.process (v);
        }
    }

    std::vector<std::vector<float>> impulse (DelayAlgorithm& d, double seconds)
    {
        testsig::Buffer buf (2, int (seconds * kSr));
        buf.data[0][0] = 1.0f; buf.data[1][0] = 1.0f;
        run (d, buf);
        return buf.data;
    }

    size_t argmaxAbs (const std::vector<float>& x, size_t from, size_t to)
    {
        size_t best = from; float bv = -1.0f;
        for (size_t i = from; i < to && i < x.size(); ++i) if (std::fabs (x[i]) > bv) { bv = std::fabs (x[i]); best = i; }
        return best;
    }

    double energy (const std::vector<float>& x, size_t from, size_t to)
    {
        double e = 0.0;
        for (size_t i = from; i < to && i < x.size(); ++i) e += double (x[i]) * x[i];
        return e;
    }
}

TEST_CASE ("Delay: free time lands on the sample and feedback scales every repeat")
{
    DelayAlgorithm d;
    d.prepare (kSr, 512, 2);
    d.setParams (bare());
    d.prepare (kSr, 512, 2);
    const auto ir = impulse (d, 1.0);
    const size_t t = size_t (0.1 * kSr);
    CHECK (std::fabs (double (argmaxAbs (ir[0], 0, ir[0].size())) - double (t)) <= 2.0);
    const double e1 = energy (ir[0], t - 50, t + 50), e2 = energy (ir[0], 2 * t - 50, 2 * t + 50), e3 = energy (ir[0], 3 * t - 50, 3 * t + 50);
    CHECK (e1 > 0.5);
    CHECK_NEAR (e2 / e1, 0.25, 0.05);
    CHECK_NEAR (e3 / e2, 0.25, 0.05);
    CHECK (energy (ir[0], 100, t - 50) < 1e-8); // nothing between the dry impulse and the first repeat
}

TEST_CASE ("Delay: tempo sync follows the host tempo and retimes without clicks")
{
    DelayAlgorithm d;
    d.prepare (kSr, 512, 2);
    auto p = bare();
    p.sync = true; p.division = int (NoteDivision::Quarter); p.feedback = 0.0f;
    d.setTempo (120.0);
    d.setParams (p);
    d.prepare (kSr, 512, 2);
    auto ir = impulse (d, 1.0);
    CHECK (std::fabs (double (argmaxAbs (ir[0], 100, ir[0].size())) - 0.5 * kSr) <= 2.0);
    CHECK_NEAR (d.getTimeMs (0), 500.0f, 0.01f);

    d.setTempo (100.0);
    CHECK_NEAR (d.getTimeMs (0), 600.0f, 0.01f);
    testsig::Buffer settle (2, int (0.2 * kSr));
    run (d, settle); // let the crossfade finish
    ir = impulse (d, 1.0);
    CHECK (std::fabs (double (argmaxAbs (ir[0], 100, ir[0].size())) - 0.6 * kSr) <= 2.0);

    // A sine through a retiming delay must stay bounded (crossfade, not a jump).
    p.sync = false; p.timeMs = 200.0f;
    d.setParams (p);
    testsig::Buffer sine (2, int (2.0 * kSr));
    testsig::fillSine (sine, 440.0f, 0.5f, kSr);
    float peak = 0.0f;
    bool finite = true;
    {
        alloctrack::Scope scope;
        for (int off = 0; off < sine.numSamples(); off += 256)
        {
            if ((off / 256) % 8 == 0) { p.timeMs = p.timeMs > 250.0f ? 200.0f : 350.0f; d.setParams (p); }
            auto v = sine.view (off, 256);
            d.process (v);
            for (int i = 0; i < 256; ++i) { const float y = sine.data[0][size_t (off + i)]; if (! std::isfinite (y)) finite = false; peak = std::max (peak, std::fabs (y)); }
        }
        CHECK (alloctrack::getCount() == 0);
    }
    CHECK (finite);
    CHECK (peak <= 0.5f * 1.05f);
}

TEST_CASE ("Delay: ping-pong alternates sides and stereo offset retimes the right channel")
{
    DelayAlgorithm d;
    d.prepare (kSr, 512, 2);
    auto p = bare();
    p.mode = int (DelayMode::PingPong); p.timeMs = 100.0f; p.feedback = 50.0f;
    d.setParams (p);
    d.prepare (kSr, 512, 2);
    const auto ir = impulse (d, 1.0);
    const size_t t = size_t (0.1 * kSr);
    CHECK (energy (ir[0], t - 20, t + 20) > 0.5);
    CHECK (energy (ir[1], t - 20, t + 20) < 1e-6);
    CHECK (energy (ir[1], 2 * t - 20, 2 * t + 20) > 0.5);
    CHECK (energy (ir[0], 2 * t - 20, 2 * t + 20) < 1e-6);
    CHECK (energy (ir[0], 3 * t - 20, 3 * t + 20) > 0.1);

    DelayAlgorithm s;
    s.prepare (kSr, 512, 2);
    auto q = bare();
    q.mode = int (DelayMode::Stereo); q.offsetPercent = 50.0f; q.feedback = 0.0f;
    s.setParams (q);
    s.prepare (kSr, 512, 2);
    const auto ir2 = impulse (s, 1.0);
    CHECK (std::fabs (double (argmaxAbs (ir2[0], 100, ir2[0].size())) - double (t)) <= 2.0);
    CHECK (std::fabs (double (argmaxAbs (ir2[1], 100, ir2[1].size())) - 1.5 * double (t)) <= 2.0);
    CHECK_NEAR (s.getTimeMs (1), 150.0f, 0.01f);
}

TEST_CASE ("Delay: ducking pulls the repeats down while the source plays; width 0 is mono")
{
    auto wetRms = [] (float duck)
    {
        DelayAlgorithm d;
        d.prepare (kSr, 512, 2);
        auto p = bare();
        p.mode = int (DelayMode::Stereo); p.duck = duck; p.feedback = 30.0f;
        d.setParams (p);
        d.prepare (kSr, 512, 2);
        testsig::Buffer sine (2, int (3.0 * kSr));
        testsig::fillSine (sine, 220.0f, 0.25f, kSr);
        run (d, sine);
        return std::sqrt (energy (sine.data[0], size_t (2.0 * kSr), size_t (3.0 * kSr)) / kSr);
    };
    const double open = wetRms (0.0f), ducked = wetRms (100.0f);
    CHECK (open > 0.05);
    CHECK (20.0 * std::log10 (ducked / open) < -20.0);

    DelayAlgorithm d;
    d.prepare (kSr, 512, 2);
    auto p = bare();
    p.mode = int (DelayMode::Stereo); p.offsetPercent = 30.0f; p.width = 0.0f;
    d.setParams (p);
    d.prepare (kSr, 512, 2);
    const auto ir = impulse (d, 1.0);
    for (size_t i = 0; i < ir[0].size(); ++i) if (std::fabs (ir[0][i] - ir[1][i]) > 1e-6f) { CHECK (false); break; }
}

TEST_CASE ("Delay: loop filters shape the repeats; disabled is silent; tail estimate is sane")
{
    auto highRatio = [] (float highCut)
    {
        DelayAlgorithm d;
        d.prepare (kSr, 512, 2);
        auto p = bare();
        p.highCutHz = highCut; p.feedback = 60.0f;
        d.setParams (p);
        d.prepare (kSr, 512, 2);
        testsig::Buffer noise (2, int (2.0 * kSr));
        testsig::fillNoise (noise, 0.3f, 5);
        run (d, noise);
        Biquad hp;
        hp.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, kSr, 6000.0f, 0.7071f, 0.0f));
        const double total = energy (noise.data[0], size_t (kSr), size_t (2.0 * kSr));
        hp.processBlock (0, noise.data[0].data(), noise.numSamples());
        return energy (noise.data[0], size_t (kSr), size_t (2.0 * kSr)) / (total + 1e-30);
    };
    CHECK (highRatio (2000.0f) < highRatio (20000.0f) * 0.3);

    DelayAlgorithm d;
    d.prepare (kSr, 512, 2);
    auto p = bare();
    p.enabled = false;
    d.setParams (p);
    const auto ir = impulse (d, 0.5);
    CHECK (energy (ir[0], 0, ir[0].size()) == 0.0);

    p.enabled = true; p.feedback = 50.0f; p.timeMs = 500.0f;
    d.setParams (p);
    CHECK (d.getTailSeconds() > 4.0f && d.getTailSeconds() < 8.0f);
}
