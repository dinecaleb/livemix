// De-esser, stereo width, limiter and loudness meter.
#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "DSP/DeEsser.h"
#include "DSP/StereoWidth.h"
#include "DSP/Limiter.h"
#include "DSP/LoudnessMeter.h"
#include "DSP/ChannelProcessor.h"
#include "Core/Denormals.h"
#include <cmath>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    template <typename P>
    void run (P& proc, testsig::Buffer& b, int block = 256)
    {
        for (int i = 0; i + block <= b.numSamples(); i += block) { auto v = b.view (i, block); proc.process (v); }
    }
}

TEST_CASE ("DeEsser: flat when nothing exceeds the threshold, reduces only the high band when it does")
{
    for (float hz : { 200.0f, 1000.0f, 4000.0f, 6500.0f, 9000.0f })
    {
        DeEsser d;
        d.prepare (kSr, 256, 1);
        DeEsser::Params p; p.enabled = true; p.freqHz = 6500.0f; p.thresholdDb = -3.0f; p.rangeDb = 12.0f; // never triggers at -20 dBFS
        d.setParams (p);
        testsig::Buffer b (1, 48000);
        testsig::fillSine (b, hz, 0.1f, kSr);
        run (d, b);
        const float outDb = testsig::toDb (testsig::rms (b, 0, 24000, 48000));
        CHECK_NEAR (outDb, testsig::toDb (0.1f / std::sqrt (2.0f)), 0.15f); // |LP - HP| = 1: the split is inaudible
        CHECK (d.getGainReductionDb() > -0.1f);
    }

    // A loud 8 kHz component above the threshold is reduced; a 300 Hz component in the same signal is not.
    DeEsser d;
    d.prepare (kSr, 256, 1);
    DeEsser::Params p; p.enabled = true; p.freqHz = 6000.0f; p.thresholdDb = -30.0f; p.rangeDb = 8.0f;
    d.setParams (p);
    testsig::Buffer b (1, 48000);
    for (int i = 0; i < b.numSamples(); ++i)
        b.data[0][size_t (i)] = 0.3f * std::sin (2.0f * float (M_PI) * 300.0f * float (i) / float (kSr)) + 0.3f * std::sin (2.0f * float (M_PI) * 8000.0f * float (i) / float (kSr));
    run (d, b);
    CHECK (d.getGainReductionDb() < -5.0f);
    CHECK (d.getGainReductionDb() >= -8.1f);
    // Measure the two components at the output with a crude DFT over the last 0.5 s.
    auto level = [&] (float hz)
    {
        double re = 0.0, im = 0.0;
        for (int i = 24000; i < 48000; ++i) { const float ph = 2.0f * float (M_PI) * hz * float (i) / float (kSr); re += b.data[0][size_t (i)] * std::cos (ph); im += b.data[0][size_t (i)] * std::sin (ph); }
        return testsig::toDb (float (2.0 * std::sqrt (re * re + im * im) / 24000.0));
    };
    CHECK_NEAR (level (300.0f), testsig::toDb (0.3f), 0.3f);
    CHECK (level (8000.0f) < testsig::toDb (0.3f) - 4.5f);

    {
        alloctrack::Scope scope;
        run (d, b);
        CHECK (alloctrack::getCount() == 0);
    }
}

TEST_CASE ("StereoWidth: 0 = mono, 1 = unchanged, mono-below keeps the low end centred, correlation meter reads the image")
{
    StereoWidth w;
    w.prepare (kSr, 256, 2);
    w.setMeteringEnabled (true);
    testsig::Buffer b (2, 4096);
    testsig::fillNoise (b, 0.5f, 3);
    b.data[1] = b.data[0];
    for (auto& x : b.data[1]) x = -x * 0.5f; // right = the left inverted and quieter: out of phase
    auto ref = b.data;

    StereoWidth::Params p; p.enabled = true; p.width = 1.0f;
    w.setParams (p);
    run (w, b);
    for (int i = 0; i < 4096; ++i) { CHECK_NEAR (b.data[0][size_t (i)], ref[0][size_t (i)], 1e-5f); CHECK_NEAR (b.data[1][size_t (i)], ref[1][size_t (i)], 1e-5f); }
    CHECK (w.getCorrelation() < -0.9f);

    StereoWidth w0;
    w0.prepare (kSr, 256, 2);
    w0.setMeteringEnabled (true);
    p.width = 0.0f; w0.setParams (p);
    { testsig::Buffer settle (2, 48000); run (w0, settle); } // the width smoother needs a few time constants
    b.data = ref;
    run (w0, b);
    for (int i = 0; i < 4096; ++i) CHECK_NEAR (b.data[0][size_t (i)], b.data[1][size_t (i)], 1e-5f);
    CHECK (w0.getCorrelation() > 0.99f);

    // Mono below 200 Hz: a 60 Hz side-only component disappears, a 2 kHz one stays.
    StereoWidth w2;
    w2.prepare (kSr, 256, 2);
    StereoWidth::Params p2; p2.enabled = true; p2.width = 1.0f; p2.monoBelowHz = 200.0f;
    w2.setParams (p2);
    testsig::Buffer s (2, 48000);
    for (int i = 0; i < 48000; ++i)
    {
        const float lo = 0.3f * std::sin (2.0f * float (M_PI) * 60.0f * float (i) / float (kSr));
        const float hi = 0.3f * std::sin (2.0f * float (M_PI) * 2000.0f * float (i) / float (kSr));
        s.data[0][size_t (i)] = lo + hi; s.data[1][size_t (i)] = -lo - hi;
    }
    run (w2, s);
    auto sideLevel = [&] (float hz)
    {
        double re = 0.0, im = 0.0;
        for (int i = 24000; i < 48000; ++i) { const float side = 0.5f * (s.data[0][size_t (i)] - s.data[1][size_t (i)]); const float ph = 2.0f * float (M_PI) * hz * float (i) / float (kSr); re += side * std::cos (ph); im += side * std::sin (ph); }
        return testsig::toDb (float (2.0 * std::sqrt (re * re + im * im) / 24000.0));
    };
    CHECK (sideLevel (60.0f) < testsig::toDb (0.3f) - 12.0f);
    CHECK_NEAR (sideLevel (2000.0f), testsig::toDb (0.3f), 0.5f);

    // Mono streams are untouched.
    StereoWidth w3;
    w3.prepare (kSr, 256, 1);
    w3.setParams (p);
    testsig::Buffer m (1, 1024);
    testsig::fillNoise (m, 0.5f);
    auto mref = m.data[0];
    run (w3, m);
    CHECK (m.data[0] == mref);
}

TEST_CASE ("Limiter: never exceeds the ceiling, passes quiet audio with only the lookahead delay, reports latency")
{
    ScopedNoDenormals nd;
    Limiter lim;
    lim.prepare (kSr, 64, 2);
    const int L = lim.getLatencySamples();
    CHECK (L == 72);
    Limiter::Params p; p.enabled = true; p.ceilingDb = -1.0f; p.releaseMs = 100.0f;
    lim.setParams (p);
    const float ceiling = std::pow (10.0f, -1.0f / 20.0f);

    // Loud: +6 dBFS drum hits with a sine underneath.
    testsig::Buffer loud (2, 48000);
    testsig::fillDrumHits (loud, kSr, 2.0f, 0.02f, 0.2f, 0.08f, 90.0f, 5);
    run (lim, loud, 64);
    CHECK (testsig::peak (loud, 0) <= ceiling + 1e-4f);
    CHECK (testsig::peak (loud, 1) <= ceiling + 1e-4f);
    CHECK (lim.getGainReductionDb() < 0.0f);
    CHECK (testsig::rms (loud, 0, 24000, 48000) > 0.05f); // still audio, not silence

    // Quiet: output = input delayed by L samples, sample-exact.
    Limiter lim2;
    lim2.prepare (kSr, 64, 1);
    lim2.setParams (p);
    testsig::Buffer q (1, 4096);
    testsig::fillNoise (q, 0.2f, 9);
    auto qref = q.data[0];
    run (lim2, q, 64);
    for (int i = L; i < 4096; ++i) CHECK_NEAR (q.data[0][size_t (i)], qref[size_t (i - L)], 1e-6f);

    // Bypassed: same delay, no gain (a +6 dB signal passes above the ceiling).
    Limiter lim3;
    lim3.prepare (kSr, 64, 1);
    Limiter::Params off; off.enabled = false;
    lim3.setParams (off);
    testsig::Buffer h (1, 4096);
    for (auto& x : h.data[0]) x = 1.5f;
    run (lim3, h, 64);
    CHECK_NEAR (h.data[0][4000], 1.5f, 1e-6f);

    {
        alloctrack::Scope scope;
        run (lim, loud, 64);
        CHECK (alloctrack::getCount() == 0);
    }
}

TEST_CASE ("LoudnessMeter: 997 Hz sine at -20 dBFS reads -23 LUFS in one channel and -20 in two (BS.1770); true peak >= sample peak")
{
    LoudnessMeter m;
    m.prepare (kSr, 256, 1);
    testsig::Buffer b (1, int (kSr * 4));
    testsig::fillSine (b, 997.0f, 0.1f, kSr);
    run (m, b);
    CHECK_NEAR (m.getMomentaryLufs(), -23.01f, 0.2f);
    CHECK_NEAR (m.getShortTermLufs(), -23.01f, 0.2f);
    CHECK_NEAR (m.getIntegratedLufs(), -23.01f, 0.2f);

    LoudnessMeter s;
    s.prepare (kSr, 256, 2);
    testsig::Buffer sb (2, int (kSr * 4));
    testsig::fillSine (sb, 997.0f, 0.1f, kSr);
    run (s, sb);
    CHECK_NEAR (s.getIntegratedLufs(), -20.0f, 0.2f);
    CHECK (s.getTruePeakDb() >= testsig::toDb (0.1f) - 0.05f);

    // Integrated loudness ignores silence (absolute gate) and quiet passages (relative gate).
    LoudnessMeter g;
    g.prepare (kSr, 256, 1);
    testsig::Buffer loudPart (1, int (kSr * 3)); testsig::fillSine (loudPart, 997.0f, 0.1f, kSr);
    testsig::Buffer quietPart (1, int (kSr * 3)); testsig::fillSine (quietPart, 997.0f, 0.001f, kSr); // -60 dBFS: gated out
    testsig::Buffer silence (1, int (kSr * 3));
    run (g, loudPart); run (g, quietPart); run (g, silence);
    CHECK_NEAR (g.getIntegratedLufs(), -23.01f, 0.3f);
    CHECK (g.getMomentaryLufs() < -100.0f);
}

TEST_CASE ("ChannelProcessor: limiter option reports constant latency and holds the ceiling; other products stay at zero")
{
    ChannelProcessor plain;
    plain.prepare (kSr, 128, 2);
    CHECK (plain.getLatencySamples() == 0);

    ChannelProcessor master;
    ChannelProcessor::Options o; o.limiter = true; o.loudnessMeter = true;
    master.configure (o);
    master.prepare (kSr, 128, 2);
    CHECK (master.getLatencySamples() == 72);
    ChannelParameters p;
    p.limiterEnabled = true; p.limiterCeilingDb = -1.0f; p.outputTrimDb = 12.0f;
    master.setParameters (p);
    { testsig::Buffer settle (2, 8192); auto v = settle.view(); master.process (v); }
    testsig::Buffer b (2, 48000);
    testsig::fillDrumHits (b, kSr, 0.8f, 0.02f, 0.25f, 0.1f, 100.0f, 4);
    run (master, b, 128);
    CHECK (testsig::peak (b, 0) <= std::pow (10.0f, -1.0f / 20.0f) + 1e-4f);
    CHECK (master.getLoudness().getShortTermLufs() > -30.0f);
    CHECK (master.getLimiter().getGainReductionDb() <= 0.0f);

    // A/B ORIGINAL keeps the delay and drops the gain.
    p.bypassAll = true; p.abLoudnessMatch = false;
    master.setParameters (p);
    testsig::Buffer q (2, 4096);
    testsig::fillNoise (q, 0.2f, 2);
    auto qref = q.data[0];
    run (master, q, 128);
    for (int i = 72; i < 4096; ++i) CHECK_NEAR (q.data[0][size_t (i)], qref[size_t (i - 72)], 1e-6f);
}
