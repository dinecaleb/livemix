#pragma once
#include <array>
#include <atomic>
#include <vector>
#include "Processor.h"
#include "Biquad.h"
#include "Core/Constants.h"
#include <cmath>

namespace livemix
{

// ITU-R BS.1770 loudness: K-weighting (high shelf + high-pass), momentary
// (400 ms), short-term (3 s) and gated integrated loudness, plus a sample peak
// with 4x linear-interpolated true-peak estimate. Audio thread writes atomics;
// the UI reads them. Integrated loudness restarts with resetIntegrated().
class LoudnessMeter : public Processor
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;
    void resetIntegrated() noexcept { resetRequested.store (true, std::memory_order_relaxed); }

    float getMomentaryLufs() const noexcept { return momentary.load (std::memory_order_relaxed); }
    float getShortTermLufs() const noexcept { return shortTerm.load (std::memory_order_relaxed); }
    float getIntegratedLufs() const noexcept { return integrated.load (std::memory_order_relaxed); }
    float getTruePeakDb() const noexcept { return gainToDbSafe (truePeak.load (std::memory_order_relaxed)); } // of the last block

    // Loudness in LUFS of a channel mean-square sum, and the K-weighting stages (shared with the analysis engine).
    static float lufsFromMeanSquare (double meanSquareSum) noexcept;
    static BiquadCoefficients kWeightingShelf (double sampleRate) noexcept;
    static BiquadCoefficients kWeightingHighPass (double sampleRate) noexcept;

private:
    static float gainToDbSafe (float g) noexcept { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
    void finishHundredMs() noexcept;

    double sr = 48000.0;
    int channels = 1;
    Biquad shelf, highpass;
    int hopSamples = 4800, hopPos = 0;
    double hopSumSquares = 0.0;
    std::vector<double> ring;         // 100 ms mean-square values, last 3 s (30)
    int ringPos = 0, ringCount = 0;
    // Integrated: histogram of 400 ms block loudness (0.1 LU bins, -70 .. +5 LUFS)
    std::array<double, 751> histogram {};
    std::array<double, 751> histogramPower {};
    double integratedSum = 0.0;
    float lastSample[kMaxChannels] {};
    std::atomic<bool> resetRequested { false };
    std::atomic<float> momentary { -120.0f }, shortTerm { -120.0f }, integrated { -120.0f }, truePeak { -120.0f };
};

} // namespace livemix
