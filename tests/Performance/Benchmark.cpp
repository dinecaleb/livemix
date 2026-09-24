// Measures ChannelProcessor cost per block for N simultaneous instances at
// 48 kHz with typical live buffer sizes. Prints per-instance cost and the
// share of the real-time budget consumed on one core.
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
#include "DSP/ChannelProcessor.h"
#include "DSP/SampleBank.h"
#include "FX/FxChain.h"
#include "FX/FxProfiles.h"
#include "Profiles/StyleProfile.h"
#include "Core/Denormals.h"
#include "TestSignals.h"

using namespace livemix;

int main()
{
    ScopedNoDenormals noDenormals;
    const double sr = 48000.0;
    const int seconds = 5;

    std::printf ("LiveMix ChannelProcessor benchmark @ %.0f Hz, stereo, full chain (gate+3 EQ+comp+transient+4 EQ+sat)\n", sr);
    std::printf ("%-10s %-10s %-14s %-14s %-12s\n", "instances", "block", "us/block/inst", "us/block/all", "budget%");

    for (int block : { 32, 64, 128, 256 })
    {
        for (int instances : { 1, 8, 16, 32, 48 })
        {
            std::vector<std::unique_ptr<ChannelProcessor>> procs;
            for (int i = 0; i < instances; ++i)
            {
                auto p = std::make_unique<ChannelProcessor>();
                p->prepare (sr, block, 2);
                auto params = StyleProfile::baseline (ChannelRole (i % int (ChannelRole::Count)), StyleProfileId::ModernWorship);
                params.satEnabled = true; params.satDrive = 0.2f;
                for (auto& b : params.correctiveBands) b.enabled = true;
                for (auto& b : params.toneBands) b.enabled = true;
                params.transientEnabled = true; params.transientAttack = 0.3f;
                p->setParameters (params);
                procs.push_back (std::move (p));
            }

            testsig::Buffer src (2, block * 64);
            testsig::fillDrumHits (src, sr, 0.6f, 0.02f, 0.25f, 0.1f);
            testsig::Buffer work (2, block);

            const int totalBlocks = int (sr * seconds) / block;
            auto start = std::chrono::steady_clock::now();
            for (int b = 0; b < totalBlocks; ++b)
            {
                const int off = (b % 64) * block;
                for (auto& p : procs)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        std::copy (src.data[size_t (ch)].begin() + off, src.data[size_t (ch)].begin() + off + block, work.data[size_t (ch)].begin());
                    auto v = work.view();
                    p->process (v);
                }
            }
            auto end = std::chrono::steady_clock::now();
            const double totalUs = std::chrono::duration<double, std::micro> (end - start).count();
            const double usPerBlockAll = totalUs / totalBlocks;
            const double usPerBlockInst = usPerBlockAll / instances;
            const double budgetUs = 1.0e6 * block / sr;
            std::printf ("%-10d %-10d %-14.2f %-14.1f %-12.1f\n", instances, block, usPerBlockInst, usPerBlockAll, 100.0 * usPerBlockAll / budgetUs);
        }
    }

    // Sample replacement: the full chain with the stage on, on a kick that fires it twice a
    // second, against the same chain with the stage off. Its own section, so the chain rows
    // above (and their 15 % fence) never carry a stage that is off by default.
    {
        std::printf ("\nSample replacement benchmark @ %.0f Hz, mono, full chain + detector + voices (a kick, two hits a second)\n", sr);
        std::printf ("%-10s %-10s %-14s %-14s %-12s\n", "instances", "block", "us/block/inst", "us/block/all", "budget%");
        const auto bank = synthesizeBank (RoleFamily::Kick, 0, sr);
        for (int block : { 32, 128 })
            for (bool on : { false, true })
            {
                const int instances = on ? 6 : 6;
                std::vector<std::unique_ptr<ChannelProcessor>> procs;
                for (int i = 0; i < instances; ++i)
                {
                    auto p = std::make_unique<ChannelProcessor>();
                    ChannelProcessor::Options o; o.sampleReplacement = true;
                    p->configure (o);
                    p->prepare (sr, block, 1);
                    p->setSampleBank (&bank);
                    auto params = StyleProfile::baseline (ChannelRole::KickIn, StyleProfileId::ModernGospel);
                    params.replaceEnabled = on; params.replaceThresholdDb = -30.0f; params.replaceGainDb = -12.0f;
                    p->setParameters (params);
                    procs.push_back (std::move (p));
                }
                testsig::Buffer src (1, block * 64);
                testsig::fillDrumHits (src, sr, 0.6f, 0.002f, 0.5f, 0.1f, 60.0f);
                testsig::Buffer work (1, block);
                const int totalBlocks = int (sr * seconds) / block;
                auto start = std::chrono::steady_clock::now();
                for (int b = 0; b < totalBlocks; ++b)
                {
                    const int off = (b % 64) * block;
                    for (auto& p : procs)
                    {
                        std::copy (src.data[0].begin() + off, src.data[0].begin() + off + block, work.data[0].begin());
                        auto v = work.view();
                        p->process (v);
                    }
                }
                auto end = std::chrono::steady_clock::now();
                const double usPerBlockAll = std::chrono::duration<double, std::micro> (end - start).count() / totalBlocks;
                const double budgetUs = 1.0e6 * block / sr;
                // The "instances" column carries on/off so the two rows are distinct keys: 6 strips off, 106 = 6 strips on.
                std::printf ("%-10d %-10d %-14.2f %-14.1f %-12.1f\n", on ? 100 + instances : instances, block, usPerBlockAll / instances, usPerBlockAll, 100.0 * usPerBlockAll / budgetUs);
            }
    }

    // Dine FX: reverb + delay chains (Vocal Throw runs both engines), stereo, 128 blocks of source.
    std::printf ("\nDine FX FxChain benchmark @ %.0f Hz, stereo (Vocal Plate / Vocal Throw alternating)\n", sr);
    std::printf ("%-10s %-10s %-14s %-14s %-12s\n", "instances", "block", "us/block/inst", "us/block/all", "budget%");
    for (int block : { 64, 128, 256 })
    {
        for (int instances : { 1, 4, 8, 16 })
        {
            std::vector<std::unique_ptr<FxChain>> chains;
            for (int i = 0; i < instances; ++i)
            {
                auto c = std::make_unique<FxChain>();
                c->prepare (sr, block, 2);
                c->setParameters (FxProfiles::baseline (StyleProfileId::ModernGospel, i % 2 == 0 ? FxType::VocalPlate : FxType::VocalThrow));
                c->setTempo (120.0);
                chains.push_back (std::move (c));
            }
            testsig::Buffer src (2, block * 64);
            testsig::fillPinkNoise (src, 0.3f, 2);
            testsig::Buffer work (2, block);
            const int totalBlocks = int (sr * 3) / block;
            auto start = std::chrono::steady_clock::now();
            for (int b = 0; b < totalBlocks; ++b)
            {
                const int off = (b % 64) * block;
                for (auto& c : chains)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        std::copy (src.data[size_t (ch)].begin() + off, src.data[size_t (ch)].begin() + off + block, work.data[size_t (ch)].begin());
                    auto v = work.view();
                    c->process (v);
                }
            }
            auto end = std::chrono::steady_clock::now();
            const double usPerBlockAll = std::chrono::duration<double, std::micro> (end - start).count() / totalBlocks;
            const double budgetUs = 1.0e6 * block / sr;
            std::printf ("%-10d %-10d %-14.2f %-14.1f %-12.1f\n", instances, block, usPerBlockAll / instances, usPerBlockAll, 100.0 * usPerBlockAll / budgetUs);
        }
    }
    return 0;
}
