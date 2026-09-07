#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "FX/ReverbAlgorithm.h"
#include "DSP/Biquad.h"
#include <cmath>
#include <vector>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    // Neutral engineering settings so the tank alone is measured.
    ReverbAlgorithm::Params bare()
    {
        ReverbAlgorithm::Params p;
        p.enabled = true; p.decayS = 2.0f; p.preDelayMs = 0.0f; p.size = 50.0f; p.damping = 0.0f; p.diffusion = 70.0f;
        p.lowCutHz = 20.0f; p.highCutHz = 20000.0f; p.modRateHz = 0.5f; p.modDepth = 0.0f; p.early = 0.0f; p.levelDb = 0.0f;
        return p;
    }

    // Impulse response of the reverb (stereo), `seconds` long.
    std::vector<std::vector<float>> impulseResponse (ReverbAlgorithm& r, double seconds, int channels = 2)
    {
        const int n = int (seconds * kSr);
        testsig::Buffer buf (channels, n);
        buf.data[0][0] = 1.0f;
        if (channels > 1) buf.data[1][0] = 1.0f;
        for (int off = 0; off < n; off += 512)
        {
            auto v = buf.view (off, std::min (512, n - off));
            r.process (v);
        }
        return buf.data;
    }

    // RT60 from the Schroeder backward-integrated energy decay curve (-5 .. -35 dB slope).
    double measureRt60 (const std::vector<float>& ir)
    {
        std::vector<double> edc (ir.size());
        double acc = 0.0;
        for (size_t i = ir.size(); i-- > 0;) { acc += double (ir[i]) * ir[i]; edc[i] = acc; }
        const double total = edc[0];
        double t5 = -1.0, t35 = -1.0;
        for (size_t i = 0; i < edc.size(); ++i)
        {
            const double db = 10.0 * std::log10 (edc[i] / total + 1e-30);
            if (t5 < 0.0 && db <= -5.0) t5 = double (i) / kSr;
            if (t35 < 0.0 && db <= -35.0) { t35 = double (i) / kSr; break; }
        }
        return t5 >= 0.0 && t35 > t5 ? 2.0 * (t35 - t5) : -1.0;
    }

    double energy (const std::vector<float>& x, size_t from = 0, size_t to = size_t (-1))
    {
        double e = 0.0;
        for (size_t i = from; i < x.size() && i < to; ++i) e += double (x[i]) * x[i];
        return e;
    }

    // Share of the tail's energy (0.3 .. 1.0 s, after the undamped first pass) above 4 kHz.
    double highBandRatio (std::vector<float> x)
    {
        Biquad hp;
        hp.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, kSr, 4000.0f, 0.7071f, 0.0f));
        const size_t from = size_t (0.3 * kSr), to = size_t (1.0 * kSr);
        const double total = energy (x, from, to);
        hp.processBlock (0, x.data(), int (x.size()));
        return energy (x, from, to) / (total + 1e-30);
    }
}

TEST_CASE ("Reverb: RT60 follows the decay parameter and grows with it")
{
    double previous = 0.0;
    for (float decay : { 0.6f, 1.5f, 4.0f })
    {
        ReverbAlgorithm r;
        r.prepare (kSr, 512, 2);
        auto p = bare();
        p.decayS = decay;
        r.setParams (p);
        r.prepare (kSr, 512, 2); // snap the smoothers to the new targets
        const auto ir = impulseResponse (r, double (decay) * 3.0 + 1.0);
        const double rt = measureRt60 (ir[0]);
        CHECK (rt > 0.0);
        CHECK (rt > double (decay) * 0.6 && rt < double (decay) * 1.6);
        CHECK (rt > previous);
        previous = rt;
    }
}

TEST_CASE ("Reverb: pre-delay holds the onset; early reflections arrive before the tank")
{
    ReverbAlgorithm r;
    r.prepare (kSr, 512, 2);
    auto p = bare();
    p.preDelayMs = 100.0f;
    r.setParams (p);
    r.prepare (kSr, 512, 2);
    const auto ir = impulseResponse (r, 0.5);
    const size_t preSamples = size_t (0.1 * kSr);
    for (size_t i = 0; i < preSamples - 1; ++i) CHECK (std::fabs (ir[0][i]) < 1e-9f);
    CHECK (energy (ir[0], preSamples, preSamples + size_t (0.05 * kSr)) > 1e-4);

    // With early reflections the first energy lands within the early window (< 60 ms after pre-delay).
    p.early = 100.0f;
    ReverbAlgorithm e;
    e.prepare (kSr, 512, 2);
    e.setParams (p);
    e.prepare (kSr, 512, 2);
    const auto ir2 = impulseResponse (e, 0.5);
    size_t first = 0;
    while (first < ir2[0].size() && std::fabs (ir2[0][first]) < 1e-4f) ++first;
    CHECK (first >= preSamples - 1 && first < preSamples + size_t (0.012 * kSr));
}

TEST_CASE ("Reverb: extreme settings stay finite and bounded, and process() never allocates")
{
    ReverbAlgorithm r;
    r.prepare (kSr, 256, 2);
    auto p = bare();
    p.decayS = 20.0f; p.size = 100.0f; p.modDepth = 100.0f; p.modRateHz = 5.0f; p.diffusion = 100.0f; p.damping = 0.0f; p.early = 100.0f; p.levelDb = 6.0f;
    r.setParams (p);
    testsig::Buffer src (2, int (kSr) * 8);
    testsig::fillNoise (src, 0.5f, 3);
    double inSq = 0.0, outSq = 0.0;
    bool finite = true;
    {
        alloctrack::Scope scope;
        for (int off = 0; off + 256 <= src.numSamples(); off += 256)
        {
            for (int i = 0; i < 256; ++i) inSq += double (src.data[0][size_t (off + i)]) * src.data[0][size_t (off + i)];
            auto v = src.view (off, 256);
            if (off % 4096 == 0) { p.size = off % 8192 == 0 ? 100.0f : 20.0f; r.setParams (p); } // size sweeps while running
            r.process (v);
            for (int i = 0; i < 256; ++i)
            {
                const float y = src.data[0][size_t (off + i)];
                if (! std::isfinite (y)) finite = false;
                outSq += double (y) * y;
            }
        }
        CHECK (alloctrack::getCount() == 0);
    }
    CHECK (finite);
    CHECK (outSq < inSq * 400.0); // never more than +26 dB above the input energy
}

TEST_CASE ("Reverb: stereo tail is decorrelated; a mono stream processes cleanly")
{
    ReverbAlgorithm r;
    r.prepare (kSr, 512, 2);
    r.setParams (bare());
    r.prepare (kSr, 512, 2);
    const auto ir = impulseResponse (r, 2.0);
    const size_t from = size_t (0.1 * kSr), to = size_t (1.5 * kSr);
    double ll = 0.0, rr = 0.0, lr = 0.0;
    for (size_t i = from; i < to; ++i) { ll += double (ir[0][i]) * ir[0][i]; rr += double (ir[1][i]) * ir[1][i]; lr += double (ir[0][i]) * ir[1][i]; }
    const double corr = lr / std::sqrt (ll * rr + 1e-30);
    CHECK (std::fabs (corr) < 0.5);
    CHECK (ll > 0.0 && rr > 0.0);

    ReverbAlgorithm mono;
    mono.prepare (kSr, 512, 1);
    mono.setParams (bare());
    const auto irm = impulseResponse (mono, 1.0, 1);
    CHECK (energy (irm[0]) > 1e-4);
    for (float y : irm[0]) if (! std::isfinite (y)) { CHECK (false); break; }
}

TEST_CASE ("Reverb: damping and high cut darken the tail; low cut thins it")
{
    auto tailRatio = [] (float damping, float highCut, float lowCut)
    {
        ReverbAlgorithm r;
        r.prepare (kSr, 512, 2);
        auto p = bare();
        p.damping = damping; p.highCutHz = highCut; p.lowCutHz = lowCut;
        r.setParams (p);
        r.prepare (kSr, 512, 2);
        auto ir = impulseResponse (r, 2.0);
        return highBandRatio (ir[0]);
    };
    const double bright = tailRatio (0.0f, 20000.0f, 20.0f);
    const double damped = tailRatio (100.0f, 20000.0f, 20.0f);
    const double cut = tailRatio (0.0f, 2000.0f, 20.0f);
    CHECK (damped < bright * 0.5);
    CHECK (cut < bright * 0.5);

    auto lowBand = [] (float lowCut)
    {
        ReverbAlgorithm r;
        r.prepare (kSr, 512, 2);
        auto p = bare();
        p.lowCutHz = lowCut;
        r.setParams (p);
        r.prepare (kSr, 512, 2);
        auto ir = impulseResponse (r, 2.0);
        Biquad lp;
        lp.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, kSr, 150.0f, 0.7071f, 0.0f));
        const double total = energy (ir[0]);
        lp.processBlock (0, ir[0].data(), int (ir[0].size()));
        return energy (ir[0]) / (total + 1e-30);
    };
    CHECK (lowBand (500.0f) < lowBand (20.0f) * 0.5);
}

TEST_CASE ("Reverb: level scales the wet signal, disabled is silent, and the tail dies out")
{
    auto rms = [] (float levelDb, bool enabled)
    {
        ReverbAlgorithm r;
        r.prepare (kSr, 512, 2);
        auto p = bare();
        p.levelDb = levelDb; p.enabled = enabled; p.decayS = 1.0f;
        r.setParams (p);
        r.prepare (kSr, 512, 2);
        auto ir = impulseResponse (r, 1.0);
        return std::sqrt (energy (ir[0]) / double (ir[0].size()));
    };
    const double unity = rms (0.0f, true), quiet = rms (-20.0f, true);
    CHECK (unity > 0.0);
    CHECK_NEAR (20.0 * std::log10 (quiet / unity), -20.0, 0.2);
    CHECK (rms (0.0f, false) == 0.0);

    ReverbAlgorithm r;
    r.prepare (kSr, 512, 2);
    auto p = bare();
    p.decayS = 0.5f;
    r.setParams (p);
    r.prepare (kSr, 512, 2);
    auto ir = impulseResponse (r, 4.0);
    const double late = energy (ir[0], size_t (3.0 * kSr)) / double (kSr);
    CHECK (10.0 * std::log10 (late + 1e-30) < -90.0);
}
