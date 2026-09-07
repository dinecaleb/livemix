// Sibilance, stereo correlation and loudness measurements of the analysis engine.
#include "TestFramework.h"
#include "TestSignals.h"
#include "Analysis/AnalysisEngine.h"
#include <chrono>
#include <thread>
#include <cmath>

using namespace livemix;

namespace
{
    // Feeds a buffer in real-time-ish blocks (capture a little shorter than the buffer) and waits for the result.
    AnalysisResult analyse (testsig::Buffer& b, double sr)
    {
        AnalysisEngine engine;
        engine.prepare (sr, b.numChannels());
        engine.startCapture (float (b.numSamples() / sr) - 0.5f);
        for (int i = 0; i < 200 && ! engine.isCapturing(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        const int block = 128;
        for (int i = 0; i + block <= b.numSamples() && engine.getState() == AnalysisEngine::State::Capturing; i += block)
        {
            auto v = b.view (i, block);
            engine.pushAudio (v);
            if ((i / block) % 16 == 0) std::this_thread::sleep_for (std::chrono::microseconds (500));
        }
        for (int i = 0; i < 5000 && (engine.getState() == AnalysisEngine::State::Capturing || engine.getState() == AnalysisEngine::State::Processing); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        return engine.getResult();
    }

    // A sung phrase: a 220 Hz tone with harmonics, plus optional 7 kHz noise bursts ("S").
    void fillVoice (testsig::Buffer& b, double sr, bool sibilant, unsigned seed = 4)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (auto& c : b.data)
        {
            float hp = 0.0f, lp = 0.0f;
            for (size_t i = 0; i < c.size(); ++i)
            {
                const float t = float (i) / float (sr);
                float v = 0.0f;
                for (int h = 1; h <= 6; ++h) v += (0.3f / float (h)) * std::sin (2.0f * float (M_PI) * 220.0f * float (h) * t);
                const float phrase = 0.5f + 0.5f * std::sin (2.0f * float (M_PI) * 0.5f * t); // slow level movement
                v *= 0.8f * phrase;
                if (sibilant && std::fmod (t, 0.6f) < 0.08f)
                {
                    // 1st-order high-passed noise around 7 kHz-ish: a sharp S
                    const float n = dist (rng);
                    hp = 0.6f * (hp + n - lp); lp = n;
                    v += 0.7f * hp;
                }
                c[i] = v;
            }
        }
    }
}

TEST_CASE ("Analysis: sibilance is high for a voice with sharp S sounds and low for a smooth voice")
{
    const double sr = 48000.0;
    testsig::Buffer sharp (1, int (sr * 3));
    fillVoice (sharp, sr, true);
    auto rs = analyse (sharp, sr);
    REQUIRE (rs.valid);
    CHECK (rs.sibilanceDb > -100.0f);

    testsig::Buffer smooth (1, int (sr * 3));
    fillVoice (smooth, sr, false);
    auto rm = analyse (smooth, sr);
    REQUIRE (rm.valid);
    CHECK (rm.sibilanceDb > -100.0f);
    CHECK (rs.sibilanceDb > rm.sibilanceDb + 8.0f);
    CHECK (rs.sibilancePercent > rm.sibilancePercent);
    CHECK (rm.sibilanceDb < -12.0f);
}

TEST_CASE ("Analysis: stereo correlation, loudness and true peak")
{
    const double sr = 48000.0;
    testsig::Buffer mono (2, int (sr * 2));
    testsig::fillSine (mono, 997.0f, 0.1f, sr);
    auto rm = analyse (mono, sr);
    REQUIRE (rm.valid);
    CHECK_NEAR (rm.stereoCorrelation, 1.0f, 0.01f);
    CHECK_NEAR (rm.loudnessLufs, -20.0f, 0.3f); // a -20 dBFS sine in both channels (BS.1770: -23 per channel)
    CHECK (rm.truePeakDb >= testsig::toDb (0.1f) - 0.05f && rm.truePeakDb <= testsig::toDb (0.1f) + 0.5f);

    testsig::Buffer wide (2, int (sr * 2));
    testsig::fillNoise (wide, 0.3f, 11);
    { std::mt19937 rng (12); std::uniform_real_distribution<float> d (-1.0f, 1.0f); for (auto& x : wide.data[1]) x = 0.3f * d (rng); }
    auto rw = analyse (wide, sr);
    REQUIRE (rw.valid);
    CHECK (std::fabs (rw.stereoCorrelation) < 0.1f);

    testsig::Buffer single (1, int (sr * 2));
    testsig::fillSine (single, 997.0f, 0.1f, sr);
    auto r1 = analyse (single, sr);
    REQUIRE (r1.valid);
    CHECK_NEAR (r1.stereoCorrelation, 1.0f, 1e-6f);
    CHECK_NEAR (r1.loudnessLufs, -23.01f, 0.3f);
}
