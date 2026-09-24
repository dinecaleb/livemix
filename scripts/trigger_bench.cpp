// Scratch benchmark for docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md: what a trigger detector and a few
// interpolated sample voices cost per block, with the engine's own Biquad and EnvelopeFollower.
// Not part of the build. From the repo root:
//   c++ -std=c++20 -O3 -I src scripts/trigger_bench.cpp src/DSP/Biquad.cpp -o /tmp/trigger_bench && /tmp/trigger_bench
#include "DSP/Biquad.h"
#include "Core/EnvelopeFollower.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace livemix;

// The detector as the scope describes it: band-pass the detector copy (HPF + LPF), a fast
// and a slow peak follower, a hit when fast exceeds slow by the rise and the absolute floor.
struct Detector
{
    Biquad hpf, lpf; EnvelopeFollower fast, slow;
    float riseLin = 4.0f, floorLin = 0.05f; int mask = 0, maskSamples = 1440;
    int process (const float* in, int n, int* offsets, float* vel) noexcept
    {
        int hits = 0;
        for (int i = 0; i < n; ++i)
        {
            float d = lpf.processSample (0, hpf.processSample (0, in[i]));
            d = std::fabs (d);
            const float f = fast.process (d), s = slow.process (d);
            if (mask > 0) --mask;
            else if (f > floorLin && f > s * riseLin) { offsets[hits] = i; vel[hits] = f; ++hits; mask = maskSamples; }
        }
        return hits;
    }
};

// A voice: cubic-interpolated playback at a rate (varispeed) with a gain, summed into the block.
struct Voice
{
    const float* data = nullptr; int length = 0; double pos = 0.0, rate = 1.0; float gain = 0.0f; bool on = false;
    void render (float* out, int n) noexcept
    {
        if (! on) return;
        for (int i = 0; i < n; ++i)
        {
            const int i1 = int (pos); if (i1 + 2 >= length) { on = false; return; }
            const float t = float (pos - i1);
            const float y0 = data[i1 - 1 < 0 ? 0 : i1 - 1], y1 = data[i1], y2 = data[i1 + 1], y3 = data[i1 + 2];
            const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            out[i] += gain * (((c3 * t + c2) * t + c1) * t + c0);
            pos += rate;
        }
    }
};

int main()
{
    const double sr = 48000.0; const int block = 128, blocks = 200000;
    Detector det;
    det.hpf.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, 40.0f, 0.707f, 0.0f));
    det.lpf.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, sr, 150.0f, 0.707f, 0.0f));
    det.fast.prepare (sr); det.slow.prepare (sr);
    det.fast.setAttackMs (0.1f); det.fast.setReleaseMs (20.0f);
    det.slow.setAttackMs (10.0f); det.slow.setReleaseMs (200.0f);
    std::vector<float> in (block), out (block), sample (48000);
    for (int i = 0; i < 48000; ++i) sample[i] = std::sin (i * 0.02f) * std::exp (-i / 8000.0f);
    Voice voices[4];
    for (auto& v : voices) { v.data = sample.data(); v.length = 48000; v.gain = 0.5f; v.rate = 1.0594; }
    int offsets[16]; float vel[16]; long long hits = 0; float sink = 0.0f;

    auto time = [&] (auto&& body, const char* name)
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i) in[i] = std::sin ((b * block + i) * 0.007f) * ((b % 48) == 0 && i < 8 ? 1.0f : 0.02f);
            body (b);
        }
        const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / blocks;
        std::printf ("%-44s %6.2f us per 128-sample block\n", name, us);
    };
    time ([&] (int) { }, "signal generation alone (subtract this)");
    time ([&] (int) { hits += det.process (in.data(), block, offsets, vel); }, "detector: HPF+LPF, fast+slow follower, mask");
    time ([&] (int b) { for (auto& v : voices) { if (! v.on) { v.on = true; v.pos = 1.0; } } std::fill (out.begin(), out.end(), 0.0f); for (auto& v : voices) v.render (out.data(), block); sink += out[7]; (void) b; },
          "4 voices, cubic interpolation, varispeed");
    time ([&] (int b) { for (auto& v : voices) { if (! v.on) { v.on = true; v.pos = 1.0; } } std::fill (out.begin(), out.end(), 0.0f); voices[0].render (out.data(), block); sink += out[7]; (void) b; },
          "1 voice, cubic interpolation, varispeed");
    std::printf ("(hits %lld, sink %f)\n", hits, sink);
}
