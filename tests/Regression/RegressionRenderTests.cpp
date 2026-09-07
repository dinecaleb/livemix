// Audio regression renders: deterministic synthetic drum signal through the full
// chain for several presets, compared against stored reference renders.
// Regenerate with: LIVEMIX_REGEN_REFERENCES=1 build/tests/livemix_tests Regression
#include "TestFramework.h"
#include "TestSignals.h"
#include "DSP/ChannelProcessor.h"
#include "Profiles/StyleProfile.h"
#include "Core/Denormals.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace livemix;

namespace
{
    struct Case { const char* name; ChannelRole role; StyleProfileId style; int channels; };

    std::vector<float> render (const Case& c)
    {
        const double sr = 48000.0;
        const int block = 128;
        const int total = int (sr * 0.5);
        ChannelProcessor cp;
        cp.prepare (sr, block, c.channels);
        auto p = StyleProfile::baseline (c.role, c.style);
        p.satEnabled = true; p.satDrive = 0.2f;
        cp.setParameters (p);
        testsig::Buffer sig (c.channels, total);
        testsig::fillDrumHits (sig, sr, 0.6f, 0.02f, 0.2f, 0.09f, 110.0f, 42);
        for (int i = 0; i < total; i += block) { auto v = sig.view (i, block); cp.process (v); }
        std::vector<float> out;
        for (auto& ch : sig.data) out.insert (out.end(), ch.begin(), ch.end());
        return out;
    }

    std::string refPath (const char* name) { return std::string (LIVEMIX_REFERENCE_DIR) + "/" + name + ".f32"; }
}

TEST_CASE ("Regression: renders match stored references")
{
    ScopedNoDenormals nd;
    const bool regen = std::getenv ("LIVEMIX_REGEN_REFERENCES") != nullptr;
    const Case cases[] = {
        { "kick_modern_gospel", ChannelRole::KickIn, StyleProfileId::ModernGospel, 1 },
        { "snare_modern_worship", ChannelRole::SnareTop, StyleProfileId::ModernWorship, 1 },
        { "overhead_modern_gospel_stereo", ChannelRole::Overhead, StyleProfileId::ModernGospel, 2 },
        { "bus_modern_worship_stereo", ChannelRole::DrumBus, StyleProfileId::ModernWorship, 2 },
    };
    for (const auto& c : cases)
    {
        auto out = render (c);
        const auto path = refPath (c.name);
        if (regen)
        {
            std::ofstream f (path, std::ios::binary);
            f.write (reinterpret_cast<const char*> (out.data()), std::streamsize (out.size() * sizeof (float)));
            std::printf ("    wrote %s (%zu samples)\n", path.c_str(), out.size());
            continue;
        }
        std::ifstream f (path, std::ios::binary);
        if (! f)
        {
            testfw::reportFailure (__FILE__, __LINE__, "missing reference " + path + " (run with LIVEMIX_REGEN_REFERENCES=1)");
            continue;
        }
        std::vector<float> ref (out.size());
        f.read (reinterpret_cast<char*> (ref.data()), std::streamsize (ref.size() * sizeof (float)));
        REQUIRE (f.gcount() == std::streamsize (ref.size() * sizeof (float)));
        double maxDiff = 0.0, sq = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double d = double (out[i]) - ref[i];
            maxDiff = std::max (maxDiff, std::fabs (d));
            sq += d * d;
        }
        const double rmsDiff = std::sqrt (sq / double (out.size()));
        if (maxDiff > 2.0e-4 || rmsDiff > 5.0e-5) // tolerances allow FMA/arch differences (refs generated on arm64)
            testfw::reportFailure (__FILE__, __LINE__, std::string (c.name) + ": maxDiff " + std::to_string (maxDiff) + " rmsDiff " + std::to_string (rmsDiff));
    }
}
