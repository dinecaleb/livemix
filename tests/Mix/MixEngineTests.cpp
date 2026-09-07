#include "TestFramework.h"
#include "TestSignals.h"
#include "AllocationTracker.h"
#include "Mix/MixEngine.h"
#include "Core/DbUtils.h"
#include <chrono>
#include <cstdio>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    MixSession smallSession()
    {
        MixSession s;
        s.inputs = {
            { "Kick",  ChannelRole::KickIn,       0, -1 },
            { "Snare", ChannelRole::SnareTop,     1, -1 },
            { "Bass",  ChannelRole::BassDI,       2, -1 },
            { "Keys",  ChannelRole::Piano,        3,  4 },
            { "Lead",  ChannelRole::LeadVocal,    5, -1 },
            { "Vox",   ChannelRole::BackingVocal, 6, -1 },
        };
        return s;
    }

    // Device-like multichannel buffers.
    struct Device
    {
        testsig::Buffer in, out;
        std::vector<const float*> ip;
        std::vector<float*> op;
        Device (int inputs, int outputs, int samples) : in (inputs, samples), out (outputs, samples), ip (size_t (inputs)), op (size_t (outputs)) {}
        void run (MixEngine& e, int block)   // allocation-free once constructed
        {
            for (int i = 0; i + block <= in.numSamples(); i += block)
            {
                for (size_t c = 0; c < ip.size(); ++c) ip[c] = in.ptrs[c] + i;
                for (size_t c = 0; c < op.size(); ++c) op[c] = out.ptrs[c] + i;
                e.process (ip.data(), int (ip.size()), op.data(), int (op.size()), block);
            }
        }
        float peak (int ch, int from = 0) const
        {
            float p = 0.0f;
            for (size_t i = size_t (from); i < out.data[size_t (ch)].size(); ++i) p = std::max (p, std::fabs (out.data[size_t (ch)][i]));
            return p;
        }
        float rms (int ch, int from = 0) const
        {
            double acc = 0.0; int n = 0;
            for (size_t i = size_t (from); i < out.data[size_t (ch)].size(); ++i) { acc += double (out.data[size_t (ch)][i]) * out.data[size_t (ch)][i]; ++n; }
            return n > 0 ? float (std::sqrt (acc / n)) : 0.0f;
        }
    };

    void sineOnInput (Device& d, int input, float hz, float amp)
    {
        auto& c = d.in.data[size_t (input)];
        for (size_t i = 0; i < c.size(); ++i) c[i] = amp * std::sin (2.0f * float (M_PI) * hz * float (i) / float (kSr));
    }

    MixParameters rawMix (MixEngine& e)
    {
        MixParameters p = startingPoint (MixSession {}, e.getGraph());
        p = e.getAppliedParameters();
        p.bypassProcessing = true;
        return p;
    }
}

TEST_CASE ("MixEngine: with processing bypassed a centred mono strip reaches both outputs at the pan law, after the master's constant latency")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setParameters (rawMix (e));
    Device d (8, 2, 48000);
    sineOnInput (d, 0, 100.0f, 0.5f);
    d.run (e, 64);
    const int settle = 24000;
    CHECK_NEAR (d.peak (0, settle), 0.5f * std::cos (float (M_PI) / 4.0f), 0.01);
    CHECK_NEAR (d.peak (1, settle), 0.5f * std::cos (float (M_PI) / 4.0f), 0.01);
    CHECK (e.getLatencySamples() > 0);   // the master limiter's lookahead, reported constantly
    CHECK (e.getLatencySamples() < 200);
}

TEST_CASE ("MixEngine: pan, fader and mute behave like a console")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    MixParameters p = rawMix (e);
    p.strips[0].pan = -1.0f;                 // kick hard left
    p.strips[4].faderDb = -6.0f;             // lead down 6 dB
    p.strips[1].mute = true;                 // snare muted
    e.setParameters (p);
    Device d (8, 2, 48000);
    sineOnInput (d, 0, 100.0f, 0.5f);
    sineOnInput (d, 1, 1000.0f, 0.5f);
    d.run (e, 64);
    const int settle = 24000;
    CHECK_NEAR (d.peak (0, settle), 0.5f, 0.02);   // kick fully left
    CHECK (d.peak (1, settle) < 0.02f);            // nothing on the right: snare is muted, kick is left

    Device d2 (8, 2, 48000);
    sineOnInput (d2, 5, 440.0f, 0.5f);
    e.reset();
    d2.run (e, 64);
    CHECK_NEAR (d2.peak (0, settle), 0.5f * dbToGain (-6.0f) * std::cos (float (M_PI) / 4.0f), 0.01);
}

TEST_CASE ("MixEngine: the digital input gain sits before the chain and the listen tap")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    MixParameters p = rawMix (e);
    p.strips[4].inputGainDb = 6.0f;   // lead
    e.setParameters (p);
    Device d (8, 2, 48000);
    sineOnInput (d, 5, 440.0f, 0.2f);
    d.run (e, 64);
    CHECK_NEAR (d.peak (0, 24000), 0.2f * dbToGain (6.0f) * std::cos (float (M_PI) / 4.0f), 0.01);
    CHECK (e.getStrip (4).getInputMeter().consumeMaxPeakDb() > gainToDb (0.2f) + 5.0f);   // the strip's own input meter sees the gained signal
}

TEST_CASE ("MixEngine: a stereo strip keeps its channels apart and is not panned")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setParameters (rawMix (e));
    Device d (8, 2, 24000);
    sineOnInput (d, 3, 200.0f, 0.4f);   // keys left only
    d.run (e, 64);
    CHECK_NEAR (d.peak (0, 12000), 0.4f, 0.01);
    CHECK (d.peak (1, 12000) < 0.01f);
}

TEST_CASE ("MixEngine: sends feed the returns and the returns reach the master")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    // One second of lead vocal, then half a second of silence: the plate and the delay keep ringing.
    Device d (8, 2, 72000);
    sineOnInput (d, 5, 440.0f, 0.3f);
    std::fill (d.in.data[5].begin() + 48000, d.in.data[5].end(), 0.0f);
    d.run (e, 64);
    CHECK (e.getFx (FxSlot::VocalPlate).getInputMeter().consumeMaxPeakDb() > -40.0f);
    CHECK (e.getFx (FxSlot::VocalDelay).getInputMeter().consumeMaxPeakDb() > -50.0f);
    CHECK (e.getFx (FxSlot::BgvHall).getInputMeter().consumeMaxPeakDb() < -80.0f);   // the lead does not send to the backing hall
    const float tailWithFx = d.rms (0, 48000 + 2400);
    CHECK (gainToDb (tailWithFx) > -60.0f);

    MixParameters p = e.getAppliedParameters();
    for (auto& s : p.strips[4].sendDb) s = kSilenceDb;
    e.setParameters (p);
    e.reset();
    Device d2 (8, 2, 72000);
    sineOnInput (d2, 5, 440.0f, 0.3f);
    std::fill (d2.in.data[5].begin() + 48000, d2.in.data[5].end(), 0.0f);
    d2.run (e, 64);
    const float tailDry = d2.rms (0, 48000 + 2400);
    CHECK (tailDry < tailWithFx * 0.1f);
}

TEST_CASE ("MixEngine: unpatched inputs are silent and an input index past the device is ignored")
{
    MixSession s = smallSession();
    s.inputs.push_back ({ "Ghost", ChannelRole::Organ, 40, -1 });
    MixEngine e;
    e.prepare (kSr, 64, s);
    e.setParameters (rawMix (e));
    Device d (8, 2, 4096);
    d.run (e, 64);
    CHECK (d.peak (0) == 0.0f);
    CHECK (d.peak (1) == 0.0f);
}

TEST_CASE ("MixEngine: blocks larger than the prepared size are processed in chunks")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setParameters (rawMix (e));
    Device d (8, 2, 48000);
    sineOnInput (d, 0, 100.0f, 0.5f);
    d.run (e, 256);
    CHECK_NEAR (d.peak (0, 24000), 0.5f * std::cos (float (M_PI) / 4.0f), 0.01);
}

TEST_CASE ("MixEngine: a mono output gets the sum, extra outputs are cleared")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setParameters (rawMix (e));
    Device d (8, 4, 24000);
    for (auto& c : d.out.data) std::fill (c.begin(), c.end(), 1.0f);
    sineOnInput (d, 0, 100.0f, 0.5f);
    d.run (e, 64);
    CHECK (d.peak (0, 12000) > 0.3f);
    CHECK (d.peak (1, 12000) > 0.3f);
    CHECK (d.peak (2) == 0.0f);
    CHECK (d.peak (3) == 0.0f);
    Device m (8, 1, 24000);
    sineOnInput (m, 0, 100.0f, 0.5f);
    m.run (e, 64);
    CHECK_NEAR (m.peak (0, 12000), 0.5f * std::cos (float (M_PI) / 4.0f), 0.01);
}

namespace
{
    struct CountingTap : public MixTap
    {
        bool active = true;
        int inputs = 0, processed = 0, buses = 0, masters = 0;
        bool isActive() const noexcept override { return active; }
        void pushStripInput (int, const AudioBlockView&) noexcept override { ++inputs; }
        void pushStripProcessed (int, const AudioBlockView&) noexcept override { ++processed; }
        void pushBus (MixBus b, const AudioBlockView&) noexcept override { if (b == MixBus::Master) ++masters; else ++buses; }
        void pushMasterOutput (const AudioBlockView&) noexcept override { ++outputs; }
        int outputs = 0;
    };
}

TEST_CASE ("MixEngine: the listening tap sees every strip raw and processed, every used bus and the master, only while active")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    CountingTap tap;
    e.setTap (&tap);
    Device d (8, 2, 640);
    d.run (e, 64);
    CHECK (tap.inputs == 6 * 10);
    CHECK (tap.processed == 6 * 10);
    CHECK (tap.buses == 4 * 10);
    CHECK (tap.masters == 10);
    CHECK (tap.outputs == 10);
    tap.active = false;
    d.run (e, 64);
    CHECK (tap.inputs == 60);
    e.setTap (nullptr);
}

TEST_CASE ("MixEngine: steady-state processing and a parameter publish never allocate on the audio thread")
{
    MixEngine e;
    e.prepare (kSr, 128, smallSession());
    Device d (8, 2, 128 * 8);
    testsig::fillNoise (d.in, 0.3f);
    d.run (e, 128);   // warm up
    MixParameters p = e.getAppliedParameters();
    p.strips[0].faderDb = -3.0f;
    e.setParameters (p);   // message thread: may allocate, the audio side must not
    {
        alloctrack::Scope scope;
        d.run (e, 128);
        CHECK (alloctrack::getCount() == 0);
    }
}

TEST_CASE ("MixEngine: 64 processed strips with buses and returns fit comfortably inside a 64-sample callback at 48 kHz")
{
    MixSession s;
    const ChannelRole roles[] = { ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::RackTom, ChannelRole::Overhead,
                                  ChannelRole::BassDI, ChannelRole::Piano, ChannelRole::Organ, ChannelRole::AcousticGuitar,
                                  ChannelRole::LeadVocal, ChannelRole::BackingVocal, ChannelRole::Choir, ChannelRole::Speech };
    for (int i = 0; i < 64; ++i) s.inputs.push_back ({ "In " + std::to_string (i + 1), roles[i % 12], i, -1 });
    MixEngine e;
    e.prepare (kSr, 64, s);
    REQUIRE (e.getNumStrips() == 64);
    Device d (64, 2, 64 * 400);
    testsig::fillPinkNoise (d.in, 0.2f);
    d.run (e, 64);   // warm up
    e.resetStats();
    const auto t0 = std::chrono::steady_clock::now();
    d.run (e, 64);
    const double totalMicros = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
    const double perBlock = totalMicros / 400.0;
    const double budget = 64.0 / kSr * 1.0e6;
    std::printf ("    64 strips @ 64 samples: %.0f us/block (%.0f%% of %.0f us), peak %.0f us\n", perBlock, 100.0 * perBlock / budget, budget, double (e.getStats().peakBlockMicros));
    CHECK (perBlock < 0.6 * budget);
    CHECK (e.getStats().blocks == 400);
}
