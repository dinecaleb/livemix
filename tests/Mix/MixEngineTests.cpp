#include "TestFramework.h"
#include "DSP/Limiter.h"
#include <algorithm>
#include <cmath>
#include <limits>
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
    // The master limiter's lookahead, reported constantly, and nothing else in the graph adds to it.
    CHECK (e.getLatencySamples() == std::max (1, int (std::lround (Limiter::kLookaheadMs * 0.001 * kSr))));
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

TEST_CASE ("MixEngine: SOLO IN PLACE mutes everything else; mute still wins")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    MixParameters p = rawMix (e);
    p.monitor.mode = SoloMode::InPlace;      // the destructive listen, chosen on purpose
    p.strips[0].pan = -1.0f;                 // kick hard left so the right channel is a clean check
    p.strips[0].solo = true;                 // kick solo
    p.strips[1].solo = true;                 // snare also solo (additive)
    p.strips[1].mute = true;                 // but snare is muted — mute wins
    e.setParameters (p);

    Device d (8, 2, 48000);
    sineOnInput (d, 0, 100.0f, 0.5f);        // kick
    sineOnInput (d, 1, 1000.0f, 0.5f);       // snare
    sineOnInput (d, 5, 440.0f, 0.5f);        // lead (not soloed)
    d.run (e, 64);
    const int settle = 24000;
    CHECK (d.peak (0, settle) > 0.2f);       // kick still heard on the left
    CHECK (d.peak (1, settle) < 0.05f);      // snare muted despite solo; lead silenced by solo
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
    // DRUMS, BASS, MUSIC, LEAD and BGV: five groups on this console since the lead became
    // its own (2026-09-28).
    CHECK (tap.buses == 5 * 10);
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

// ---------------------------------------------------------------------------
// The monitor (solo) bus. The rule the whole feature stands on is one sentence:
// pressing S must never change what leaves the master.
// ---------------------------------------------------------------------------

namespace
{
    // Feed 0 = the broadcast on outputs 0/1, feed 1 = the engineer's headphones on 2/3.
    OutputFeeds mainAndMonitor()
    {
        OutputFeeds f;
        f.count = 2;
        f.feeds[0] = { 0, 1, MixBus::Master, 0.0f, false, false, false };
        f.feeds[1] = { 2, 3, MixBus::Master, 0.0f, false, false, true };
        return f;
    }
}

TEST_CASE ("MixEngine: solo feeds the monitor output and leaves the live master untouched")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setOutputFeeds (mainAndMonitor());

    MixParameters p = rawMix (e);
    e.setParameters (p);
    Device before (8, 4, 48000);
    sineOnInput (before, 0, 100.0f, 0.5f);   // kick
    sineOnInput (before, 5, 440.0f, 0.5f);   // lead
    before.run (e, 64);
    const int settle = 24000;
    const float masterBefore = before.peak (0, settle);
    CHECK (masterBefore > 0.2f);

    // Now solo the kick. The default is MONITOR SOLO.
    p.strips[0].solo = true;
    e.setParameters (p);
    e.reset();
    Device after (8, 4, 48000);
    sineOnInput (after, 0, 100.0f, 0.5f);
    sineOnInput (after, 5, 440.0f, 0.5f);
    after.run (e, 64);

    // The broadcast is bit-for-bit the same mix: both sources still there, same level.
    CHECK_NEAR (after.peak (0, settle), masterBefore, 0.01);
    // The monitor pair carries the kick on its own.
    CHECK (after.peak (2, settle) > 0.2f);
}

TEST_CASE ("MixEngine: with nothing soloed the monitor output carries the mix")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setOutputFeeds (mainAndMonitor());
    e.setParameters (rawMix (e));
    Device d (8, 4, 48000);
    sineOnInput (d, 5, 440.0f, 0.5f);
    d.run (e, 64);
    const int settle = 24000;
    CHECK (d.peak (0, settle) > 0.2f);
    CHECK_NEAR (d.peak (2, settle), d.peak (0, settle), 0.01);
}

TEST_CASE ("MixEngine: PFL hears a muted channel, AFL does not; neither reaches the master")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setOutputFeeds (mainAndMonitor());
    MixParameters p = rawMix (e);
    p.strips[4].mute = true;                 // the lead is muted in the mix
    p.strips[4].solo = true;                 // and soloed into the monitor
    p.monitor.point = SoloPoint::PFL;
    e.setParameters (p);
    Device pfl (8, 4, 48000);
    sineOnInput (pfl, 5, 440.0f, 0.5f);
    pfl.run (e, 64);
    const int settle = 24000;
    CHECK (pfl.peak (0, settle) < 0.02f);    // muted: nothing on the broadcast
    CHECK (pfl.peak (2, settle) > 0.2f);     // but the engineer can still hear it

    p.monitor.point = SoloPoint::AFL;
    e.setParameters (p);
    e.reset();
    Device afl (8, 4, 48000);
    sineOnInput (afl, 5, 440.0f, 0.5f);
    afl.run (e, 64);
    CHECK (afl.peak (0, settle) < 0.02f);
    CHECK (afl.peak (2, settle) < 0.02f);    // after the fader, a mute is a mute
}

TEST_CASE ("MixEngine: a soloed group and a soloed return reach the monitor only")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setOutputFeeds (mainAndMonitor());
    MixParameters p = rawMix (e);
    p.buses[size_t (MixBus::Drums)].solo = true;
    e.setParameters (p);
    Device d (8, 4, 48000);
    sineOnInput (d, 0, 100.0f, 0.5f);        // kick -> DRUMS
    sineOnInput (d, 5, 440.0f, 0.5f);        // lead -> VOCALS
    d.run (e, 64);
    const int settle = 24000;
    CHECK (d.peak (0, settle) > 0.2f);       // the broadcast still has the whole band
    CHECK (d.peak (2, settle) > 0.2f);       // the drums alone are in the headphones
}

TEST_CASE ("MixEngine: the monitor level, dim and mute never touch the master")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setOutputFeeds (mainAndMonitor());
    MixParameters p = rawMix (e);
    e.setParameters (p);
    Device open (8, 4, 48000);
    sineOnInput (open, 5, 440.0f, 0.5f);
    open.run (e, 64);
    const int settle = 24000;
    const float master = open.peak (0, settle);

    p.monitor.dim = true;
    e.setParameters (p);
    e.reset();
    Device dimmed (8, 4, 48000);
    sineOnInput (dimmed, 5, 440.0f, 0.5f);
    dimmed.run (e, 64);
    CHECK_NEAR (dimmed.peak (0, settle), master, 0.01);
    CHECK (dimmed.peak (2, settle) < master * 0.25f);

    p.monitor.dim = false;
    p.monitor.mute = true;
    e.setParameters (p);
    e.reset();
    Device muted (8, 4, 48000);
    sineOnInput (muted, 5, 440.0f, 0.5f);
    muted.run (e, 64);
    CHECK_NEAR (muted.peak (0, settle), master, 0.01);
    CHECK (muted.peak (2, settle) < 0.02f);
}

TEST_CASE ("MixEngine: with no monitor feed routed, solo changes nothing at all")
{
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setOutputFeeds (OutputFeeds::mainOnly());
    MixParameters p = rawMix (e);
    e.setParameters (p);
    Device before (8, 2, 48000);
    sineOnInput (before, 0, 100.0f, 0.5f);
    sineOnInput (before, 5, 440.0f, 0.5f);
    before.run (e, 64);
    const int settle = 24000;
    const float master = before.peak (0, settle);

    p.strips[0].solo = true;
    e.setParameters (p);
    e.reset();
    Device after (8, 2, 48000);
    sineOnInput (after, 0, 100.0f, 0.5f);
    sineOnInput (after, 5, 440.0f, 0.5f);
    after.run (e, 64);
    CHECK_NEAR (after.peak (0, settle), master, 0.01);
}

// ---------------------------------------------------------------------------
// The broadcast is always a real stereo pair
//
// Not "usually": a mix that reaches the stream summed to mono, or on one leg because a pair
// was half-chosen, is the kind of fault nobody notices until it is on the recording. The
// shape is enforced where feeds enter the system, so no caller can get it wrong.
// ---------------------------------------------------------------------------
TEST_CASE ("OutputFeeds: the broadcast and the engineer's listen are always a stereo pair")
{
    OutputFeeds f;
    f.count = 4;
    // Feed 0: somebody summed the broadcast to mono.
    f.feeds[0] = { 0, 1, MixBus::Master, 0.0f, false, true, false };
    // Feed 1: the engineer's listen, with only one leg chosen.
    f.feeds[1] = { 5, -1, MixBus::Master, 0.0f, false, false, true };
    // Feed 2: a monitor feed somebody made mono.
    f.feeds[2] = { 2, 3, MixBus::Master, 0.0f, false, true, true };
    // Feed 3: an ordinary extra feed, deliberately mono - a single fill speaker.
    f.feeds[3] = { 7, 8, MixBus::Drums, -6.0f, false, true, false };

    normaliseOutputs (f);

    CHECK (! f.feeds[0].mono);                  // the broadcast is never summed
    CHECK (f.feeds[0].left == 0);
    CHECK (f.feeds[0].right == 1);

    // Half a pair is completed rather than summed onto the one leg that was chosen. The
    // channel that exists becomes the *left* of the pair rather than being snapped down to an
    // even boundary: an aggregate device's second sub-device starts wherever the first one
    // ends, which is not always an even index, and snapping would quietly move the engineer's
    // listen onto the broadcast's last channel.
    CHECK (! f.feeds[1].mono);
    CHECK (f.feeds[1].left == 5);
    CHECK (f.feeds[1].right == 6);

    CHECK (! f.feeds[2].mono);                  // a monitor feed is stereo too

    CHECK (f.feeds[3].mono);                    // ...but a fill speaker keeps its mono switch
    CHECK (f.feeds[3].left == 7);

    // One feed per channel. Two on one pair are summed - the mix 6 dB over its own ceiling -
    // and a listen sharing a pair with the broadcast is every solo on the air. The earlier
    // feed keeps the channel; the one that collides comes off the device.
    OutputFeeds stacked;
    stacked.count = 3;
    stacked.feeds[0] = { 0, 1, MixBus::Master, 0.0f, false, false, false };
    stacked.feeds[1] = { 0, 1, MixBus::Master, 0.0f, false, false, true };    // the listen, on the broadcast's pair
    stacked.feeds[2] = { 1, 2, MixBus::Drums, 0.0f, false, false, false };    // half on it
    normaliseOutputs (stacked, 8);
    CHECK (stacked.feeds[0].left == 0);
    CHECK (! stacked.feeds[1].routed());
    CHECK (! stacked.feeds[2].routed());
    // And one channel named twice is that channel once, not the mix doubled onto it.
    OutputFeeds twice;
    twice.count = 2;
    twice.feeds[1] = { 4, 4, MixBus::Master, 0.0f, false, false, false };
    normaliseOutputs (twice, 8);
    CHECK (twice.feeds[1].mono);

    // A device smaller than the routing expects never gets written past its end.
    OutputFeeds small;
    small.count = 2;
    small.feeds[0] = { 0, 1, MixBus::Master, 0.0f, false, false, false };
    small.feeds[1] = { 8, 9, MixBus::Master, 0.0f, false, false, true };
    normaliseOutputs (small, 4);
    CHECK (small.feeds[0].routed());
    CHECK (! small.feeds[1].routed());

    // ...but with no device open the routing survives, because it belongs to the session and
    // has to come back when the interface is plugged in again.
    OutputFeeds stored;
    stored.count = 2;
    stored.feeds[1] = { 8, 9, MixBus::Master, 0.0f, false, false, true };
    normaliseOutputs (stored, 0);
    CHECK (stored.feeds[1].routed());
    CHECK (stored.feeds[1].left == 8);
}

TEST_CASE ("MixEngine: EFFECTS OFF on one channel stops feeding the returns and leaves the levels alone")
{
    // The lead vocal into the vocal plate, measured at the plate's own output: what arrives at
    // the return is the only thing this asks about. EFFECTS OFF has to silence it WITHOUT
    // touching sendDb, because the levels are what make the press back exact.
    //
    // The engine is warmed on silence first, deliberately. A send is a smoothed gain, so the
    // block where it is asked to change still carries the ramp down from where prepare() left
    // it - which is the point (the reverb rings out rather than stopping dead) - and a plate
    // rings for seconds after it, so anything fed during the warmup would still be decaying
    // through the measurement. Neither is what is being asked about here.
    MixSession s = smallSession();
    const auto graph = RoutingGraph::build (s);
    const int lead = 4;
    REQUIRE (graph.fxUsed[size_t (FxSlot::VocalPlate)]);

    auto intoPlate = [&] (bool effectsOff)
    {
        MixEngine e;
        e.prepare (kSr, 64, s);
        auto p = startingPoint (s, graph);
        p.strips[size_t (lead)].effectsOff = effectsOff;
        REQUIRE (p.strips[size_t (lead)].sendDb[size_t (FxSlot::VocalPlate)] > kSilenceDb);
        e.setParameters (p);

        Device warmup (8, 4, int (kSr) / 4);         // silence in: the send settles, the plate stays empty
        warmup.run (e, 64);
        (void) e.getFx (FxSlot::VocalPlate).getOutputMeter().consumeMaxPeakDb();   // the ramp, discarded

        Device d (8, 4, int (kSr) / 2);
        sineOnInput (d, 5, 440.0f, 0.3f);            // the lead vocal's input
        d.run (e, 64);
        return e.getFx (FxSlot::VocalPlate).getOutputMeter().consumeMaxPeakDb();
    };

    const float wet = intoPlate (false);
    const float dry = intoPlate (true);
    REQUIRE (wet > -60.0f);                          // the plate is being fed at all
    CHECK (dry <= kSilenceDb + 1.0f);                // ...and EFFECTS OFF feeds it nothing
    CHECK (wet - dry > 40.0f);
}

TEST_CASE ("MixEngine: speech priority steps the band back into the broadcast, never into the engineer's listen")
{
    // A band and a speech microphone. Off, the band is where the faders put it; on, it steps
    // back while the speech group carries somebody speaking - and the listen never moves. The
    // kit and the pastor are at different frequencies so each can be measured on its own.
    MixSession s = smallSession();
    s.inputs.push_back ({ "Pastor", ChannelRole::Speech, 7, -1 });
    MixEngine e;
    e.prepare (kSr, 64, s);

    // How much 110 Hz (the kit, and nothing else) came out of one channel, over the second half.
    auto bandLevel = [] (const Device& d, int ch)
    {
        double re = 0.0, im = 0.0;
        const int from = int (kSr) / 2, to = int (kSr);
        for (int i = from; i < to; ++i)
        {
            const double ph = 2.0 * M_PI * 110.0 * double (i) / kSr;
            re += double (d.out.data[size_t (ch)][size_t (i)]) * std::cos (ph);
            im += double (d.out.data[size_t (ch)][size_t (i)]) * std::sin (ph);
        }
        return float (2.0 * std::sqrt (re * re + im * im) / double (to - from));
    };

    auto measure = [&] (bool speechPriority, bool speaking, float& broadcast, float& listen)
    {
        e.reset();
        auto p = e.getAppliedParameters();
        p.bypassProcessing = true;                       // the duck, not a chain, is what is measured
        p.speechDuck.enabled = speechPriority;
        p.speechDuck.depthDb = 6.0f;
        p.speechDuck.thresholdDb = -38.0f;
        p.speechDuck.attackMs = 20.0f; p.speechDuck.releaseMs = 20.0f; p.speechDuck.holdMs = 10.0f;
        p.buses[size_t (MixBus::Drums)].solo = true;     // the engineer is listening to the kit alone
        e.setParameters (p);

        OutputFeeds feeds;
        feeds.count = 2;
        feeds.feeds[0] = OutputFeed { 0, 1, MixBus::Master, 0.0f, false, false, false };   // the broadcast
        feeds.feeds[1] = OutputFeed { 2, 3, MixBus::Master, 0.0f, false, false, true };    // the engineer's listen
        e.setOutputFeeds (feeds);

        Device d (8, 4, int (kSr));
        sineOnInput (d, 0, 110.0f, 0.3f);                // the kit
        if (speaking) sineOnInput (d, 7, 300.0f, 0.4f);  // the pastor
        d.run (e, 64);
        broadcast = bandLevel (d, 0);
        listen = bandLevel (d, 2);
    };

    float offBand = 0.0f, offListen = 0.0f, onQuiet = 0.0f, onQuietListen = 0.0f, onSpeaking = 0.0f, onSpeakingListen = 0.0f;
    measure (false, true, offBand, offListen);
    measure (true, false, onQuiet, onQuietListen);
    measure (true, true, onSpeaking, onSpeakingListen);

    REQUIRE (offBand > 0.001f);
    REQUIRE (offListen > 0.001f);
    // Nobody speaking: speech priority changes nothing at all.
    CHECK_NEAR (gainToDb (onQuiet / offBand), 0.0f, 0.5f);
    // Speaking: the band steps back, by about the depth and never more.
    CHECK (gainToDb (onSpeaking / offBand) < -3.0f);
    CHECK (gainToDb (onSpeaking / offBand) > -7.0f);
    // The engineer's listen is what is really there, whatever the broadcast is doing.
    CHECK_NEAR (gainToDb (onSpeakingListen / offListen), 0.0f, 0.5f);

    Device d (8, 4, 4096);
    sineOnInput (d, 0, 110.0f, 0.3f);
    sineOnInput (d, 7, 300.0f, 0.4f);
    d.run (e, 64);                                       // warm the ramps before counting
    {
        alloctrack::Scope scope;
        d.run (e, 64);
        CHECK (alloctrack::getCount() == 0);
    }
}

TEST_CASE ("MixEngine: a stage that stops producing numbers drops out for a block, and the service goes on")
{
    // One NaN latched in a strip's filter state would otherwise ride the bus, the master, the
    // reverb's feedback and the limiter's delay line to the broadcast until the next prepare.
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    e.setParameters (rawMix (e));
    Device d (8, 2, 48000);
    sineOnInput (d, 5, 440.0f, 0.25f);                                     // the lead, singing
    for (int i = 100; i < 200; ++i) d.in.data[0][size_t (i)] = std::numeric_limits<float>::quiet_NaN();
    d.in.data[0][300] = std::numeric_limits<float>::infinity();            // and the kick mic goes wild
    d.run (e, 64);

    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (float x : d.out.data[size_t (ch)]) finite = finite && std::isfinite (x);
    CHECK (finite);
    CHECK (e.getNonFiniteBlocks() > 0);
    // The lead is still on the air after the fault.
    CHECK_NEAR (d.peak (0, 24000), 0.25f * std::cos (float (M_PI) / 4.0f), 0.01);
}

TEST_CASE ("MixEngine: the device is never handed more than full scale")
{
    // With the master limiter off (BYPASS, a volunteer's chain, a group feed) nothing else in
    // the graph has a ceiling, and a converter handed more clips anyway.
    MixEngine e;
    e.prepare (kSr, 64, smallSession());
    auto p = rawMix (e);
    p.strips[4].faderDb = 12.0f;
    e.setParameters (p);
    // A feed turned up past the master's own ceiling: +12 dB after the limiter.
    OutputFeeds hot;
    hot.count = 1;
    hot.feeds[0] = { 0, 1, MixBus::Master, 12.0f, false, false, false };
    e.setOutputFeeds (hot);
    Device d (8, 2, 24000);
    sineOnInput (d, 5, 440.0f, 0.9f);
    d.run (e, 64);
    CHECK (d.peak (0) <= 1.0f);
    CHECK (d.peak (1) <= 1.0f);
    CHECK (e.getClampedOutputBlocks() > 0);

    // ... and a mix that is right never touches it.
    MixEngine quiet;
    quiet.prepare (kSr, 64, smallSession());
    quiet.setParameters (rawMix (quiet));
    Device q (8, 2, 24000);
    sineOnInput (q, 5, 440.0f, 0.25f);
    q.run (quiet, 64);
    CHECK (quiet.getClampedOutputBlocks() == 0);
    CHECK (quiet.getNonFiniteBlocks() == 0);
}
