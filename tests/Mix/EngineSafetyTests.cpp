// THE ENGINE ON A SERVICE DAY: the things that must hold however the mix is set. Clips are
// read where they happen; speech priority opens for a voice and not for the band bleeding into
// the lectern microphone, and does not breathe between phrases; a muted group is silent, its
// effects included; BYPASS still has a ceiling; a feed moves, it never steps; and a device that
// restarts does not leak what was muted.
#include "TestFramework.h"
#include "TestSignals.h"
#include "Mix/MixEngine.h"
#include "Core/DbUtils.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    MixSession service()
    {
        MixSession s;
        s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Keys", ChannelRole::Piano, 1, 2 },
                     { "Lead", ChannelRole::LeadVocal, 3, -1 }, { "BGV", ChannelRole::BackingVocal, 4, -1 },
                     { "Pastor", ChannelRole::Speech, 5, -1 } };
        return s;
    }
    constexpr int kKick = 0, kLead = 2;

    struct Rig
    {
        MixEngine e;
        std::vector<std::vector<float>> in;
        std::vector<std::vector<float>> out;
        std::vector<const float*> ip;
        std::vector<float*> op;
        long long pos = 0;
        explicit Rig (const MixSession& s, int outputs = 2) : in (8, std::vector<float> (64, 0.0f)), out (size_t (outputs), std::vector<float> (64, 0.0f)),
                                                            ip (8), op (size_t (outputs))
        {
            e.prepare (kSr, 64, s);
        }
        // Runs `seconds`, with `fill (channel, sampleIndex)` giving each input; returns the loudest output sample.
        template <typename F> float run (double seconds, F&& fill)
        {
            float peak = 0.0f;
            for (int b = 0; b < int (seconds * kSr / 64); ++b)
            {
                for (int c = 0; c < 8; ++c)
                {
                    for (int i = 0; i < 64; ++i) in[size_t (c)][size_t (i)] = fill (c, pos + i);
                    ip[size_t (c)] = in[size_t (c)].data();
                }
                for (size_t o = 0; o < out.size(); ++o) op[o] = out[o].data();
                e.process (ip.data(), 8, op.data(), int (op.size()), 64);
                for (auto& o : out) for (float x : o) peak = std::max (peak, std::fabs (x));
                pos += 64;
            }
            return peak;
        }
    };

    float sine (double hz, long long i, float amp) { return amp * float (std::sin (2.0 * M_PI * hz * double (i) / kSr)); }
}

TEST_CASE ("Engine: a clip is read at the converter, not after DLIVE's digital gain")
{
    Rig r (service());
    auto p = r.e.getAppliedParameters();
    p.strips[kKick].inputGainDb = -12.0f;             // a clipping kick turned down digitally...
    p.strips[kLead].inputGainDb = 12.0f;              // ... and a clean lead turned up
    r.e.setParameters (p);
    r.run (0.2, [] (int c, long long i) {
        if (c == 0) return std::clamp (sine (60.0, i, 1.6f), -1.0f, 1.0f);   // flat-topped at the converter
        if (c == 3) return sine (300.0, i, 0.3f);                           // -10 dBFS at the converter
        return 0.0f;
    });
    CHECK (r.e.converterClipped (kKick));             // still a clip, whatever the trim says
    CHECK (! r.e.converterClipped (kLead));           // and a trim is not a clip
    r.e.clearConverterClips();
    CHECK (! r.e.converterClipped (kKick));
}

TEST_CASE ("Engine: speech priority opens for a voice, not for the band in the lectern microphone, and holds between phrases")
{
    auto withDuck = [] (MixEngine& e)
    {
        auto p = e.getAppliedParameters();
        p.speechDuck.enabled = true;
        p.speechDuck.depthDb = 6.0f;
        p.speechDuck.thresholdDb = -38.0f;
        p.speechDuck.attackMs = 20.0f; p.speechDuck.releaseMs = 40.0f; p.speechDuck.holdMs = 1200.0f;
        p.bypassProcessing = true;                        // the detector, not a chain, is what is measured
        e.setParameters (p);
    };

    // The band bleeding into an open lectern microphone: kick thumps and cymbal wash, peaking
    // around -20 dBFS - well over the old detector's -38 dBFS peak, which ducked the band for it.
    {
        Rig r (service());
        withDuck (r.e);
        r.run (2.0, [] (int c, long long i) {
            if (c != 5) return 0.0f;
            const bool hit = (i % 24000) < 2400;
            return (hit ? sine (55.0, i, 0.08f) : 0.0f) + sine (9000.0, i, 0.03f);
        });
        CHECK (r.e.getSpeechDuckDb() > -0.5f);            // nobody is speaking
    }

    // Somebody speaking, with a breath between phrases: it opens, and it does not come back up in the gap.
    {
        Rig r (service());
        withDuck (r.e);
        r.run (1.0, [] (int c, long long i) { return c == 5 ? sine (400.0, i, 0.1f) : 0.0f; });   // a phrase
        CHECK (r.e.getSpeechDuckDb() < -4.0f);
        r.run (0.5, [] (int, long long) { return 0.0f; });                                      // a breath
        CHECK (r.e.getSpeechDuckDb() < -4.0f);            // the band stays back
        r.run (2.5, [] (int, long long) { return 0.0f; });                                      // finished
        CHECK (r.e.getSpeechDuckDb() > -1.0f);            // and it comes back
    }
}

TEST_CASE ("Engine: a muted group is silent, its effects included")
{
    const auto s = service();
    const auto graph = RoutingGraph::build (s);
    REQUIRE (graph.fxUsed[size_t (FxSlot::VocalPlate)]);
    auto plateFrom = [&] (bool muteGroup)
    {
        Rig r (s);
        auto p = startingPoint (s, graph);
        p.buses[size_t (MixBus::Lead)].mute = muteGroup;
        REQUIRE (p.strips[kLead].sendDb[size_t (FxSlot::VocalPlate)] > kSilenceDb);
        r.e.setParameters (p);
        r.run (0.25, [] (int, long long) { return 0.0f; });
        (void) r.e.getFx (FxSlot::VocalPlate).getOutputMeter().consumeMaxPeakDb();
        r.run (0.5, [] (int c, long long i) { return c == 3 ? sine (440.0, i, 0.3f) : 0.0f; });   // the lead sings
        return r.e.getFx (FxSlot::VocalPlate).getOutputMeter().consumeMaxPeakDb();
    };
    const float open = plateFrom (false);
    const float muted = plateFrom (true);
    REQUIRE (open > -60.0f);
    CHECK (muted < open - 40.0f);
}

TEST_CASE ("Engine: BYPASS is the raw inputs, still under the master's ceiling")
{
    Rig r (service());
    auto p = r.e.getAppliedParameters();
    p.bypassProcessing = true;
    p.master().channel.limiterEnabled = false;            // even with the master's own limiter switched off
    for (int i = 0; i < p.numStrips; ++i) p.strips[size_t (i)].faderDb = 0.0f;
    r.e.setParameters (p);
    const float peak = r.run (1.0, [] (int c, long long i) { return c < 6 ? sine (200.0 + 37.0 * c, i, 0.8f) : 0.0f; });
    CHECK (peak <= dbToGain (-0.9f));                     // the -1 dB ceiling held (plus inter-sample slack)
}

TEST_CASE ("Engine: a device that restarts does not leak what was muted")
{
    Rig r (service());
    auto p = r.e.getAppliedParameters();
    p.broadcastMute = true;
    r.e.setParameters (p);
    r.run (0.2, [] (int c, long long i) { return c == 3 ? sine (440.0, i, 0.5f) : 0.0f; });
    // The device comes back: prepare again, and the controller publishes the same mix.
    r.e.prepare (kSr, 64, service());
    r.e.setParameters (p);
    const float leak = r.run (0.05, [] (int c, long long i) { return c == 3 ? sine (440.0, i, 0.5f) : 0.0f; });
    CHECK (leak < 1.0e-4f);                               // not one ramp's worth of the muted broadcast
}
