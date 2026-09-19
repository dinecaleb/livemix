#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "DSP/ChannelProcessor.h"
#include "Profiles/StyleProfile.h"
#include "Core/Denormals.h"

using namespace livemix;

namespace
{
    ChannelParameters everythingOn()
    {
        ChannelParameters p = StyleProfile::baseline (ChannelRole::SnareTop, StyleProfileId::ModernWorship);
        p.satEnabled = true; p.satDrive = 0.3f;
        p.lpfEnabled = true; p.lpfHz = 16000.0f;
        for (auto& b : p.correctiveBands) b.enabled = true;
        for (auto& b : p.toneBands) b.enabled = true;
        return p;
    }
}

TEST_CASE ("ChannelProcessor: default parameters are bit-exact pass-through")
{
    ChannelProcessor cp;
    cp.prepare (48000.0, 256, 2);
    testsig::Buffer in (2, 2048);
    testsig::fillNoise (in, 0.7f);
    auto ref0 = in.data[0]; auto ref1 = in.data[1];
    for (int i = 0; i < 2048; i += 256) { auto v = in.view (i, 256); cp.process (v); }
    CHECK (in.data[0] == ref0);
    CHECK (in.data[1] == ref1);
}

TEST_CASE ("ChannelProcessor: full chain without the limiter reports zero latency (impulse lands at sample 0)")
{
    ChannelProcessor cp;
    cp.prepare (48000.0, 512, 1);
    ChannelParameters p = everythingOn();
    // Keep the gate open so the impulse isn't swallowed; drop the two stages that
    // legitimately reshape an impulse (16 kHz LPF group delay, saturation curve).
    p.gateEnabled = false;
    p.lpfEnabled = false;
    p.satEnabled = false;
    cp.setParameters (p);
    { testsig::Buffer s (1, 8192); auto v = s.view(); cp.process (v); } // settle smoothers
    testsig::Buffer in (1, 512);
    in.data[0][0] = 1.0f;
    auto v = in.view();
    cp.process (v);
    int argmax = 0;
    for (int i = 1; i < 512; ++i) if (std::fabs (in.data[0][size_t (i)]) > std::fabs (in.data[0][size_t (argmax)])) argmax = i;
    CHECK (argmax == 0);
    CHECK (cp.getLatencySamples() == 0);
}

TEST_CASE ("ChannelProcessor: steady-state process never allocates")
{
    ChannelProcessor cp;
    cp.prepare (48000.0, 128, 2);
    cp.setParameters (everythingOn());
    testsig::Buffer in (2, 128);
    testsig::fillDrumHits (in, 48000.0, 0.6f, 0.01f, 0.25f, 0.1f);
    auto v = in.view();
    cp.process (v); // first block may touch lazy state
    {
        alloctrack::Scope scope;
        for (int i = 0; i < 500; ++i)
        {
            ChannelParameters p = everythingOn();
            p.compThresholdDb = -20.0f + float (i % 10);
            p.toneBands[2].gainDb = float (i % 5);
            cp.setParameters (p);
            cp.process (v);
        }
        CHECK (alloctrack::getCount() == 0);
    }
}

TEST_CASE ("ChannelProcessor: bypass (A/B) passes audio untouched but keeps metering")
{
    ChannelProcessor cp;
    cp.prepare (48000.0, 256, 1);
    ChannelParameters p = everythingOn();
    p.bypassAll = true;
    cp.setParameters (p);
    testsig::Buffer in (1, 256);
    testsig::fillSine (in, 1000.0f, 0.5f, 48000.0);
    auto ref = in.data[0];
    auto v = in.view();
    cp.process (v);
    CHECK (in.data[0] == ref);
    CHECK_NEAR (cp.getInputMeter().getPeakDb (0), -6.02f, 0.1f);
    CHECK_NEAR (cp.getOutputMeter().getPeakDb (0), -6.02f, 0.1f);
}

TEST_CASE ("ChannelProcessor: input/output trim and polarity")
{
    ChannelProcessor cp;
    cp.prepare (48000.0, 256, 1);
    ChannelParameters p;
    p.inputTrimDb = -6.0f; p.outputTrimDb = +12.0f; p.polarityInvert = true;
    cp.setParameters (p);
    { testsig::Buffer s (1, 8192); auto v = s.view(); cp.process (v); }
    testsig::Buffer in (1, 256);
    for (auto& x : in.data[0]) x = 0.25f;
    auto v = in.view();
    cp.process (v);
    CHECK_NEAR (in.data[0][100], -0.25f * 2.0f, 0.002f);
}

TEST_CASE ("ChannelProcessor: robust across sample rates, block sizes and hostile input")
{
    ScopedNoDenormals noDenormals;
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        for (int block : { 32, 64, 128, 256 })
        {
            ChannelProcessor cp;
            cp.prepare (sr, block, 2);
            cp.setParameters (everythingOn());
            testsig::Buffer in (2, block * 40);
            testsig::fillDrumHits (in, sr, 0.9f, 0.02f, 0.2f, 0.08f);
            // hostile: clipped section, denormal section, silence
            for (int i = 0; i < block * 5; ++i) in.data[0][size_t (i)] = 4.0f;
            for (int i = block * 10; i < block * 15; ++i) in.data[0][size_t (i)] = 1e-39f;
            for (int i = block * 20; i < block * 25; ++i) { in.data[0][size_t (i)] = 0.0f; in.data[1][size_t (i)] = 0.0f; }
            for (int i = 0; i < in.numSamples(); i += block)
            {
                ChannelParameters p = everythingOn();
                p.compAttackMs = 0.1f + float (i % 7);
                p.gateThresholdDb = -60.0f + float (i % 50);
                cp.setParameters (p);
                auto v = in.view (i, block);
                cp.process (v);
            }
            CHECK (testsig::allFinite (in));
        }
    }
}

TEST_CASE ("ChannelProcessor: reset clears state after silence")
{
    ChannelProcessor cp;
    cp.prepare (48000.0, 256, 1);
    cp.setParameters (everythingOn());
    testsig::Buffer in (1, 4096);
    testsig::fillDrumHits (in, 48000.0, 0.8f, 0.0f, 0.1f, 0.05f);
    auto v = in.view();
    cp.process (v);
    cp.reset();
    testsig::Buffer silence (1, 4096);
    auto sv = silence.view();
    cp.process (sv);
    CHECK (testsig::peak (silence) == 0.0f);
}
