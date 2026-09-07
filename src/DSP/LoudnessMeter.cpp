#include "LoudnessMeter.h"
#include "Core/DbUtils.h"
#include <cmath>
#include <algorithm>

namespace livemix
{

namespace
{
    constexpr int kRingBlocks = 30;      // 30 x 100 ms = 3 s
    constexpr float kAbsoluteGate = -70.0f;
    constexpr float kRelativeGate = -10.0f;
    constexpr float kHistMin = -70.0f;

    // BS.1770 stage 1 (spherical-head high shelf) and stage 2 (RLB high-pass), designed at any sample
    // rate from the analogue prototypes; at 48 kHz these reproduce the coefficients printed in the standard.
    BiquadCoefficients kShelf (double sr) noexcept
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (M_PI * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        BiquadCoefficients c;
        c.b0 = float ((Vh + Vb * K / Q + K * K) / a0);
        c.b1 = float (2.0 * (K * K - Vh) / a0);
        c.b2 = float ((Vh - Vb * K / Q + K * K) / a0);
        c.a1 = float (2.0 * (K * K - 1.0) / a0);
        c.a2 = float ((1.0 - K / Q + K * K) / a0);
        return c;
    }
    BiquadCoefficients kHighPass (double sr) noexcept
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (M_PI * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        BiquadCoefficients c;
        c.b0 = float (1.0 / a0);
        c.b1 = float (-2.0 / a0);
        c.b2 = float (1.0 / a0);
        c.a1 = float (2.0 * (K * K - 1.0) / a0);
        c.a2 = float ((1.0 - K / Q + K * K) / a0);
        return c;
    }
}

BiquadCoefficients LoudnessMeter::kWeightingShelf (double sampleRate) noexcept { return kShelf (sampleRate); }
BiquadCoefficients LoudnessMeter::kWeightingHighPass (double sampleRate) noexcept { return kHighPass (sampleRate); }

float LoudnessMeter::lufsFromMeanSquare (double meanSquareSum) noexcept
{
    return meanSquareSum <= 1.0e-12 ? -120.0f : float (-0.691 + 10.0 * std::log10 (meanSquareSum));
}

void LoudnessMeter::prepare (double sampleRate, int, int numChannels)
{
    sr = sampleRate;
    channels = numChannels < kMaxChannels ? numChannels : kMaxChannels;
    shelf.setCoefficients (kShelf (sr));
    highpass.setCoefficients (kHighPass (sr));
    hopSamples = std::max (1, int (sr / 10.0));
    ring.assign (kRingBlocks, 0.0);
    reset();
}

void LoudnessMeter::reset() noexcept
{
    shelf.reset();
    highpass.reset();
    hopPos = 0;
    hopSumSquares = 0.0;
    std::fill (ring.begin(), ring.end(), 0.0);
    ringPos = ringCount = 0;
    histogram.fill (0.0);
    histogramPower.fill (0.0);
    integratedSum = 0.0;
    for (auto& s : lastSample) s = 0.0f;
    momentary.store (-120.0f); shortTerm.store (-120.0f); integrated.store (-120.0f); truePeak.store (-120.0f);
}

void LoudnessMeter::finishHundredMs() noexcept
{
    // Per-channel mean squares were summed already (channel weights 1 for L/R).
    const double ms = hopSumSquares / double (hopSamples);
    ring[size_t (ringPos)] = ms;
    ringPos = (ringPos + 1) % kRingBlocks;
    if (ringCount < kRingBlocks) ++ringCount;

    auto meanOfLast = [&] (int blocks)
    {
        const int n = std::min (blocks, ringCount);
        if (n <= 0) return 0.0;
        double acc = 0.0;
        for (int i = 1; i <= n; ++i) acc += ring[size_t ((ringPos - i + kRingBlocks) % kRingBlocks)];
        return acc / double (n);
    };
    const double m400 = meanOfLast (4);
    const float mLufs = lufsFromMeanSquare (m400);
    momentary.store (mLufs, std::memory_order_relaxed);
    shortTerm.store (lufsFromMeanSquare (meanOfLast (30)), std::memory_order_relaxed);

    // Integrated: every 400 ms block above the absolute gate enters the histogram (75 % overlap: one per 100 ms).
    if (ringCount >= 4 && mLufs > kAbsoluteGate)
    {
        const int bin = std::clamp (int (std::lround ((mLufs - kHistMin) * 10.0f)), 0, int (histogram.size()) - 1);
        histogram[size_t (bin)] += 1.0;
        histogramPower[size_t (bin)] += m400;
        // Relative gate: mean of blocks above the absolute gate, minus 10 LU.
        double count = 0.0, power = 0.0;
        for (size_t i = 0; i < histogram.size(); ++i) { count += histogram[i]; power += histogramPower[i]; }
        if (count > 0.0)
        {
            const float gateLufs = lufsFromMeanSquare (power / count) + kRelativeGate;
            const int gateBin = std::clamp (int (std::lround ((gateLufs - kHistMin) * 10.0f)), 0, int (histogram.size()) - 1);
            double gc = 0.0, gp = 0.0;
            for (size_t i = size_t (gateBin); i < histogram.size(); ++i) { gc += histogram[i]; gp += histogramPower[i]; }
            integrated.store (gc > 0.0 ? lufsFromMeanSquare (gp / gc) : -120.0f, std::memory_order_relaxed);
        }
    }
}

void LoudnessMeter::process (AudioBlockView& block) noexcept
{
    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        histogram.fill (0.0);
        histogramPower.fill (0.0);
        integrated.store (-120.0f, std::memory_order_relaxed);
    }
    const int n = block.numSamples;
    const int numCh = block.numChannels < channels ? block.numChannels : channels;
    float peak = 0.0f; // highest (interpolated) peak of this block; the UI holds it

    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = block.channels[ch][i];
            const float k = highpass.processSample (ch, shelf.processSample (ch, x));
            hopSumSquares += double (k) * k;

            // 4x linear-interpolated peak between consecutive samples (cheap true-peak estimate).
            const float prev = lastSample[ch];
            const float a = std::fabs (x), b = std::fabs (prev);
            float p = a > b ? a : b;
            const float m1 = std::fabs (prev + 0.25f * (x - prev)), m2 = std::fabs (prev + 0.5f * (x - prev)), m3 = std::fabs (prev + 0.75f * (x - prev));
            if (m1 > p) p = m1; if (m2 > p) p = m2; if (m3 > p) p = m3;
            if (p > peak) peak = p;
            lastSample[ch] = x;
        }
        if (++hopPos >= hopSamples)
        {
            finishHundredMs();
            hopPos = 0;
            hopSumSquares = 0.0;
        }
    }
    truePeak.store (peak, std::memory_order_relaxed);
}

} // namespace livemix
