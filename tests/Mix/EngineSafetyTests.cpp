// THE ENGINE ON A SERVICE DAY: the things that must hold however the mix is set. Clips are
// read where they happen; speech priority opens for a voice and not for the band bleeding into
// the lectern microphone, and does not breathe between phrases; a muted group is silent, its
// effects included; BYPASS still has a ceiling; a feed moves, it never steps; and a device that
// restarts does not leak what was muted.
#include "TestFramework.h"
#include "TestSignals.h"
#include "Mix/MixEngine.h"
#include "DSP/ChannelProcessor.h"
#include "AllocationTracker.h"
#include <chrono>
#include <string>
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

TEST_CASE ("Engine: a clip is read at the converter, not after DINE's digital gain")
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

TEST_CASE ("ChannelProcessor: a stage switched on or off mid-stream is a fade, not a click")
{
    // A 1 kHz tone, and a +12 dB tone band at 1 kHz switched on and then off between blocks.
    // A step would jump the waveform by up to three times its level between two samples; faded,
    // no two samples are further apart than the louder tone's own steepest slope.
    ChannelProcessor cp;
    cp.prepare (kSr, 64, 1);
    ChannelParameters p;
    p.correctiveEqEnabled = false;
    p.toneEqEnabled = false;
    p.toneBands[1] = { true, FilterType::Peak, 1000.0f, 12.0f, 1.0f };
    cp.setParameters (p);
    std::vector<float> buf (64);
    float* ptr[1] = { buf.data() };
    long long pos = 0;
    float prev = 0.0f, worst = 0.0f;
    auto run = [&] (int blocks, bool measure)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 64; ++i) buf[size_t (i)] = sine (1000.0, pos + i, 0.1f);
            AudioBlockView v { ptr, 1, 64 };
            cp.process (v);
            for (int i = 0; i < 64; ++i)
            {
                if (measure) worst = std::max (worst, std::fabs (buf[size_t (i)] - prev));
                prev = buf[size_t (i)];
            }
            pos += 64;
        }
    };
    run (40, false);
    p.toneEqEnabled = true;
    cp.setParameters (p);
    run (40, true);
    p.toneEqEnabled = false;
    cp.setParameters (p);
    run (40, true);
    const float slope = float (2.0 * M_PI * 1000.0 / kSr) * 0.1f * dbToGain (12.0f);
    CHECK (worst < slope * 1.3f);
}

TEST_CASE ("Engine soak: five minutes of a 32-channel service - finite, allocation-free, and no slower at the end")
{
    // A shortened service: every channel playing, the mix republished every second (a fader,
    // an EQ band, a compressor threshold moving), speech priority on. What a three-hour service
    // would expose - a denormal slowly creeping in, a buffer growing, a NaN latching - shows up
    // here as a later minute that is slower, an allocation, or a sample that is not a number.
    MixSession s;
    const ChannelRole roles[] = { ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::HiHat, ChannelRole::RackTom,
                                  ChannelRole::FloorTom, ChannelRole::OverheadLeft, ChannelRole::OverheadRight, ChannelRole::Room,
                                  ChannelRole::BassDI, ChannelRole::Piano, ChannelRole::Organ, ChannelRole::SynthPad,
                                  ChannelRole::AcousticGuitar, ChannelRole::ElectricGuitarClean, ChannelRole::LeadVocal, ChannelRole::BackingVocal };
    for (int i = 0; i < 32; ++i)
        s.inputs.push_back ({ "In " + std::to_string (i + 1), i == 31 ? ChannelRole::Speech : roles[i % 16], i, -1 });
    MixEngine e;
    e.prepare (kSr, 64, s);
    auto p = startingPoint (s, RoutingGraph::build (s));
    p.speechDuck.enabled = true;
    e.setParameters (p);

    std::vector<std::vector<float>> in (32, std::vector<float> (64, 0.0f));
    std::vector<const float*> ip (32);
    std::vector<float> l (64), r (64);
    float* op[2] = { l.data(), r.data() };
    const long long blocksPerSecond = (long long) (kSr / 64);
    const int seconds = 300;
    double firstMinute = 0.0, lastMinute = 0.0;
    bool finite = true;
    long long pos = 0;
    int allocations = 0;
    for (int sec = 0; sec < seconds; ++sec)
    {
        // Once a second, as a person or TUNE would: something moves.
        auto& strip = p.strips[size_t (sec % 32)];
        strip.faderDb = -6.0f + float (sec % 7);
        strip.channel.compThresholdDb = -30.0f + float (sec % 11);
        strip.channel.toneBands[1] = { true, FilterType::Peak, 800.0f + 100.0f * float (sec % 9), float (sec % 5) - 2.0f, 1.0f };
        e.setParameters (p);

        const auto t0 = std::chrono::steady_clock::now();
        for (long long b = 0; b < blocksPerSecond; ++b)
        {
            for (int c = 0; c < 32; ++c)
            {
                const double hz = 60.0 + 37.0 * c;
                for (int i = 0; i < 64; ++i)
                {
                    const long long n = pos + i;
                    const bool hit = (n % 24000) < 1200;                          // a pulse every half second
                    in[size_t (c)][size_t (i)] = sine (hz, n, c < 8 ? (hit ? 0.5f : 0.001f) : 0.15f);
                }
                ip[size_t (c)] = in[size_t (c)].data();
            }
            if (sec >= 2)
            {
                alloctrack::Scope scope;
                e.process (ip.data(), 32, op, 2, 64);
                allocations += alloctrack::getCount();
            }
            else e.process (ip.data(), 32, op, 2, 64);
            for (int i = 0; i < 64; ++i) finite = finite && std::isfinite (l[size_t (i)]) && std::isfinite (r[size_t (i)]);
            pos += 64;
        }
        const double spent = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (sec >= 5 && sec < 65) firstMinute += spent;
        if (sec >= seconds - 60) lastMinute += spent;
    }
    CHECK (finite);
    CHECK (e.getNonFiniteBlocks() == 0);
    CHECK (allocations == 0);
    // The last minute costs what the first did (with room for a busy machine).
    CHECK_MESSAGE (lastMinute < firstMinute * 1.5 + 0.05,
                   "first minute " + std::to_string (firstMinute) + " s, last minute " + std::to_string (lastMinute) + " s");
}
