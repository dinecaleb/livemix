#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/LevelMeter.h"

using namespace livemix;

TEST_CASE ("LevelMeter: peak and RMS of a sine, clip flag")
{
    const double sr = 48000.0;
    LevelMeter m;
    m.prepare (sr, 512, 2);
    testsig::Buffer in (2, int (sr));
    testsig::fillSine (in, 1000.0f, 0.5f, sr);
    for (int i = 0; i < in.numSamples(); i += 512)
    {
        auto v = in.view (i, std::min (512, in.numSamples() - i));
        m.process (v);
    }
    CHECK_NEAR (m.getPeakDb (0), -6.02f, 0.1f);
    CHECK_NEAR (m.getRmsDb (1), -9.03f, 0.3f);
    CHECK (! m.hasClipped());

    in.data[0][10] = 1.2f;
    auto v = in.view (0, 512);
    m.process (v);
    CHECK (m.hasClipped());
    m.clearClip();
    CHECK (! m.hasClipped());
}

TEST_CASE ("LevelMeter: a short peak between UI reads is kept until the UI consumes it")
{
    // A pluck / hit lasting one 64-sample block, followed by many quiet blocks, as a 60 Hz UI timer would see it
    // (one read every ~12 blocks at 48 kHz). The last-block peak misses the hit; the consumed peak does not.
    LevelMeter m;
    m.prepare (48000.0, 64, 1);
    testsig::Buffer hit (1, 64), quiet (1, 64);
    hit.data[0][5] = 0.9f;                       // -0.9 dBFS
    for (int i = 0; i < 64; ++i) quiet.data[0][i] = 0.01f * ((i % 2) ? 1.0f : -1.0f); // -40 dBFS
    auto hv = hit.view(); m.process (hv);
    for (int b = 0; b < 11; ++b) { auto qv = quiet.view(); m.process (qv); }
    CHECK_NEAR (m.getPeakDb (0), -40.0f, 0.1f);     // last block only
    CHECK_NEAR (m.consumeMaxPeakDb(), -0.92f, 0.1f); // the hit survived until the UI read it
    CHECK (m.consumeMaxPeakDb() <= -120.0f);         // and was consumed
    auto qv = quiet.view(); m.process (qv);
    CHECK_NEAR (m.consumeMaxPeakDb(), -40.0f, 0.1f);
    m.reset();
    CHECK (m.consumeMaxPeakDb() <= -120.0f);
}

TEST_CASE ("LevelMeter: silence reads as floor")
{
    LevelMeter m;
    m.prepare (44100.0, 256, 1);
    testsig::Buffer in (1, 256);
    auto v = in.view();
    m.process (v);
    CHECK (m.getPeakDb (0) <= -120.0f);
    CHECK (m.getRmsDb (0) <= -120.0f);
}
