#include "TestFramework.h"
#include "TestSignals.h"
#include "Mix/MixEngine.h"
#include "Mix/OfflineCapture.h"
#include "Mix/MixPlanner.h"
#include "Core/DbUtils.h"
#include <chrono>
#include <cstdio>

using namespace livemix;

namespace
{
    MixSession sixteen()
    {
        MixSession s;
        const ChannelRole roles[] = { ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::RackTom, ChannelRole::FloorTom, ChannelRole::HiHat, ChannelRole::Room,
                                      ChannelRole::BassDI, ChannelRole::Piano, ChannelRole::Organ, ChannelRole::AcousticGuitar, ChannelRole::ElectricGuitarClean,
                                      ChannelRole::LeadVocal, ChannelRole::BackingVocal, ChannelRole::BackingVocal, ChannelRole::Choir, ChannelRole::Speech };
        for (int i = 0; i < 16; ++i) s.inputs.push_back ({ "In " + std::to_string (i + 1), roles[i], i, -1 });
        return s;
    }

    struct Run
    {
        double peakL = 0.0, peakR = 0.0;
        bool finite = true;
        double microsPerBlock = 0.0;
    };

    Run run (MixEngine& e, double sr, int block, int numInputs, float amp, bool bypass)
    {
        MixParameters p = e.getAppliedParameters();
        p.bypassProcessing = bypass;
        e.setParameters (p);
        const int seconds = 1;
        const int total = int (sr) * seconds;
        testsig::Buffer in (numInputs, total);
        for (int c = 0; c < numInputs; ++c)
            for (int i = 0; i < total; ++i)
                in.data[size_t (c)][size_t (i)] = amp * std::sin (2.0f * float (M_PI) * (80.0f + 37.0f * float (c)) * float (i) / float (sr));
        std::vector<const float*> ip (static_cast<size_t> (numInputs));
        std::vector<float> l (static_cast<size_t> (block)), r (static_cast<size_t> (block));
        float* op[2] = { l.data(), r.data() };
        Run out;
        int blocks = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int pos = 0; pos + block <= total; pos += block, ++blocks)
        {
            for (int c = 0; c < numInputs; ++c) ip[size_t (c)] = in.ptrs[size_t (c)] + pos;
            e.process (ip.data(), numInputs, op, 2, block);
            if (pos > total / 2)
                for (int i = 0; i < block; ++i)
                {
                    if (! std::isfinite (l[size_t (i)]) || ! std::isfinite (r[size_t (i)])) out.finite = false;
                    out.peakL = std::max (out.peakL, double (std::fabs (l[size_t (i)])));
                    out.peakR = std::max (out.peakR, double (std::fabs (r[size_t (i)])));
                }
        }
        out.microsPerBlock = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / std::max (1, blocks);
        return out;
    }
}

TEST_CASE ("MixEngine: 44.1 / 48 / 88.2 / 96 kHz at 32 / 64 / 128 / 256 samples: finite, sensible level, inside the callback budget")
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int blocks[] = { 32, 64, 128, 256 };
    std::printf ("    rate    block   16 strips processed us/block (budget %%)\n");
    for (double sr : rates)
        for (int block : blocks)
        {
            MixEngine e;
            e.prepare (sr, block, sixteen());
            REQUIRE (e.getNumStrips() == 16);
            // Bypassed: sixteen -20 dBFS sines of different frequencies sum to roughly the same level everywhere.
            const auto raw = run (e, sr, block, 16, 0.1f, true);
            CHECK (raw.finite);
            CHECK (raw.peakL > 0.15);
            CHECK (raw.peakL < 1.6);
            CHECK_NEAR (raw.peakL, raw.peakR, 0.3);
            // Processed: still finite, the master limiter holds the ceiling.
            const auto processed = run (e, sr, block, 16, 0.1f, false);
            CHECK (processed.finite);
            CHECK (processed.peakL <= 1.0);
            CHECK (processed.peakL > 0.05);
            const double budget = double (block) / sr * 1.0e6;
            std::printf ("    %6.0f  %5d   %7.0f (%3.0f%%)\n", sr, block, processed.microsPerBlock, 100.0 * processed.microsPerBlock / budget);
            CHECK (processed.microsPerBlock < 0.6 * budget);
            CHECK (e.getLatencySamples() > 0);
            CHECK (e.getLatencySamples() <= int (0.002 * sr) + 1);   // the limiter's lookahead scales with the rate
        }
}

TEST_CASE ("MixPlanner: the same band plans sensibly at 96 kHz and at 44.1 kHz")
{
    for (double sr : { 44100.0, 96000.0 })
    {
        MixSession s;
        s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Bass", ChannelRole::BassDI, 1, -1 }, { "Lead", ChannelRole::LeadVocal, 2, -1 }, { "Keys", ChannelRole::Piano, 3, 4 } };
        MixEngine e;
        e.prepare (sr, 128, s);
        OfflineCapture cap;
        cap.prepare (sr, e.getGraph());
        e.setTap (&cap);
        const int total = int (sr * 4);
        testsig::Buffer in (5, total);
        for (int i = 0; i < total; ++i)
        {
            const float t = float (i) / float (sr);
            in.data[0][size_t (i)] = std::fmod (t, 0.5f) < 0.1f ? 0.6f * std::sin (2.0f * float (M_PI) * 100.0f * t) : 0.0f;
            in.data[1][size_t (i)] = 0.5f * std::sin (2.0f * float (M_PI) * 41.0f * t);
            in.data[2][size_t (i)] = 0.3f * std::sin (2.0f * float (M_PI) * 220.0f * t);
            in.data[3][size_t (i)] = 0.2f * std::sin (2.0f * float (M_PI) * 262.0f * t) + 0.15f * std::sin (2.0f * float (M_PI) * 2700.0f * t);
            in.data[4][size_t (i)] = 0.2f * std::sin (2.0f * float (M_PI) * 330.0f * t) + 0.15f * std::sin (2.0f * float (M_PI) * 2900.0f * t);
        }
        std::vector<const float*> ip (5);
        std::vector<float> l (128), r (128);
        float* op[2] = { l.data(), r.data() };
        cap.start();
        for (int pos = 0; pos + 128 <= total; pos += 128)
        {
            for (int c = 0; c < 5; ++c) ip[size_t (c)] = in.ptrs[size_t (c)] + pos;
            e.process (ip.data(), 5, op, 2, 128);
        }
        const auto listened = cap.finish();
        REQUIRE (listened.valid);
        CHECK_NEAR (listened.strips[2].fundamentalHz, 220.0f, 220.0f * 0.06f);
        MixPlanContext ctx;
        ctx.session = s; ctx.graph = e.getGraph(); ctx.current = e.getAppliedParameters(); ctx.atCapture = ctx.current; ctx.capture = listened;
        const auto plan = MixPlanner::plan (ctx);
        REQUIRE (plan.valid);
        CHECK (plan.stripsHeard == 4);
        CHECK (plan.headline == "MIX TUNED");
        bool bassRule = false, pocket = false;
        for (const auto& rel : plan.relationships)
        {
            if (rel.what.find ("Bass high-pass") != std::string::npos) bassRule = true;
            if (rel.what.find ("Made room for the lead vocal") != std::string::npos) pocket = true;
        }
        CHECK (bassRule);
        CHECK (pocket);
        ctx.current = plan.proposed;
        CHECK (MixPlanner::plan (ctx).noChangeRequired);
    }
}
