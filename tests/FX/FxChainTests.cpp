#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "FX/FxChain.h"
#include "FX/FxProfiles.h"
#include <cmath>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    void run (FxChain& c, testsig::Buffer& buf, int block = 256)
    {
        for (int off = 0; off < buf.numSamples(); off += block)
        {
            auto v = buf.view (off, std::min (block, buf.numSamples() - off));
            c.process (v);
        }
    }

    double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double e = 0.0; size_t n = 0;
        for (size_t i = from; i < to && i < x.size(); ++i, ++n) e += double (x[i]) * x[i];
        return n > 0 ? std::sqrt (e / double (n)) : 0.0;
    }
}

TEST_CASE ("FxChain: mix 0 and A/B ORIGINAL pass the dry signal through untouched")
{
    FxChain c;
    c.prepare (kSr, 256, 2);
    FxParameters p = FxProfiles::baseline (StyleProfileId::ModernGospel, FxType::VocalPlate);
    p.mix = 0.0f;
    c.setParameters (p);
    testsig::Buffer buf (2, int (kSr));
    testsig::fillNoise (buf, 0.3f, 9);
    const auto original = buf.data;
    run (c, buf);
    for (size_t i = size_t (0.9 * kSr); i < buf.data[0].size(); ++i) // the mix smoother settles from 1 to 0 first
        if (std::fabs (buf.data[0][i] - original[0][i]) > 1e-6f) { CHECK (false); break; }

    p.mix = 1.0f; p.bypassAll = true; p.abLoudnessMatch = false;
    c.setParameters (p);
    buf.data = original;
    run (c, buf);
    for (size_t i = 0; i < buf.data[1].size(); ++i)
        if (std::fabs (buf.data[1][i] - original[1][i]) > 1e-7f) { CHECK (false); break; }
}

TEST_CASE ("FxChain: reverb and delay leave a tail after the source stops, and never allocate")
{
    FxChain c;
    c.prepare (kSr, 256, 2);
    FxParameters p = FxProfiles::baseline (StyleProfileId::ModernGospel, FxType::VocalThrow);
    p.delaySync = false; p.delayTimeMs = 250.0f;
    c.setParameters (p);
    c.setTempo (128.0);
    testsig::Buffer buf (2, int (3.0 * kSr));
    testsig::fillSine (buf, 330.0f, 0.3f, kSr);
    for (auto& ch : buf.data) for (size_t i = size_t (kSr); i < ch.size(); ++i) ch[i] = 0.0f; // 1 s of tone, then silence
    {
        alloctrack::Scope scope;
        for (int off = 0; off < buf.numSamples(); off += 256)
        {
            if (off % 8192 == 0) { p.reverbSize = off % 16384 == 0 ? 80.0f : 40.0f; c.setParameters (p); }
            auto v = buf.view (off, 256);
            c.process (v);
        }
        CHECK (alloctrack::getCount() == 0);
    }
    CHECK (rms (buf.data[0], size_t (1.3 * kSr), size_t (1.8 * kSr)) > 0.003); // tail present
    CHECK (rms (buf.data[0], size_t (2.8 * kSr), size_t (3.0 * kSr)) < rms (buf.data[0], size_t (1.3 * kSr), size_t (1.8 * kSr))); // and decaying
    CHECK (c.getTailSeconds() > 2.0f);
    CHECK (c.getLatencySamples() == 0);
    for (float y : buf.data[1]) if (! std::isfinite (y)) { CHECK (false); break; }
}

TEST_CASE ("FxChain: loudness-matched A/B plays the dry signal at the processed level")
{
    FxChain c;
    c.prepare (kSr, 256, 2);
    FxParameters p = FxProfiles::baseline (StyleProfileId::ModernGospel, FxType::WorshipHall);
    p.mix = 0.5f; p.reverbLevelDb = 6.0f;
    c.setParameters (p);
    testsig::Buffer buf (2, int (6.0 * kSr));
    testsig::fillPinkNoise (buf, 0.2f, 4);
    const auto original = buf.data;
    run (c, buf);
    const double processed = rms (buf.data[0], size_t (3.0 * kSr), size_t (6.0 * kSr));

    p.bypassAll = true; p.abLoudnessMatch = true;
    c.setParameters (p);
    buf.data = original;
    run (c, buf);
    const double matched = rms (buf.data[0], size_t (1.0 * kSr), size_t (6.0 * kSr));
    CHECK (std::fabs (20.0 * std::log10 (matched / processed)) < 2.0);
    CHECK (std::fabs (c.getLoudnessMatchGainDb()) > 0.01f);
}

TEST_CASE ("FxChain: mono streams and every profile/type baseline process cleanly")
{
    for (int s = 0; s < int (StyleProfileId::Count); ++s)
        for (int t = 0; t < int (FxType::Count); ++t)
            for (int channels : { 1, 2 })
            {
                FxChain c;
                c.prepare (44100.0, 128, channels);
                c.setParameters (FxProfiles::baseline (StyleProfileId (s), FxType (t)));
                c.setTempo (96.0);
                testsig::Buffer buf (channels, 44100 * 3 / 2); // long enough for a quarter note at 96 BPM to repeat
                testsig::fillSine (buf, 440.0f, 0.4f, 44100.0);
                run (c, buf, 128);
                bool finite = true; double e = 0.0;
                for (float y : buf.data[0]) { if (! std::isfinite (y)) finite = false; e += double (y) * y; }
                CHECK (finite);
                CHECK (e > 1e-6); // wet output present for every type (all types produce sound on a held tone)
            }
}
