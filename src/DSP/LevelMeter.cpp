#include "LevelMeter.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

void LevelMeter::prepare (double sampleRate, int, int numChannels)
{
    channels = numChannels < kMaxChannels ? numChannels : kMaxChannels;
    const double windowSamples = 0.3 * sampleRate; // ~300 ms RMS window
    rmsCoeff = float (1.0 - std::exp (-1.0 / windowSamples));
    reset();
}

void LevelMeter::reset() noexcept
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        meanSquare[size_t (ch)] = 0.0f;
        peak[size_t (ch)].store (0.0f, std::memory_order_relaxed);
        rms[size_t (ch)].store (0.0f, std::memory_order_relaxed);
    }
    peakSinceRead.store (0.0f, std::memory_order_relaxed);
    clipped.store (false, std::memory_order_relaxed);
}

void LevelMeter::process (AudioBlockView& block) noexcept
{
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    bool clip = false;
    float blockPeak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* data = block.channel (ch);
        float p = 0.0f;
        float ms = meanSquare[size_t (ch)];
        for (int i = 0; i < block.numSamples; ++i)
        {
            const float x = data[i];
            const float a = std::fabs (x);
            if (a > p) p = a;
            ms += rmsCoeff * (x * x - ms);
        }
        if (ms < 1.0e-20f) ms = 0.0f;
        meanSquare[size_t (ch)] = ms;
        if (p >= 1.0f) clip = true;
        if (p > blockPeak) blockPeak = p;
        peak[size_t (ch)].store (p, std::memory_order_relaxed);
        rms[size_t (ch)].store (std::sqrt (ms), std::memory_order_relaxed);
    }
    if (clip) clipped.store (true, std::memory_order_relaxed);
    // Lock-free max-accumulate: the reader exchanges it back to 0.
    float held = peakSinceRead.load (std::memory_order_relaxed);
    while (blockPeak > held && ! peakSinceRead.compare_exchange_weak (held, blockPeak, std::memory_order_relaxed)) {}
}

float LevelMeter::consumeMaxPeakDb() const noexcept
{
    return gainToDb (peakSinceRead.exchange (0.0f, std::memory_order_relaxed));
}

float LevelMeter::getPeakDb (int ch) const noexcept
{
    if (ch < 0 || ch >= kMaxChannels) return kSilenceDb;
    return gainToDb (peak[size_t (ch)].load (std::memory_order_relaxed));
}

float LevelMeter::getRmsDb (int ch) const noexcept
{
    if (ch < 0 || ch >= kMaxChannels) return kSilenceDb;
    return gainToDb (rms[size_t (ch)].load (std::memory_order_relaxed));
}

float LevelMeter::getMaxPeakDb() const noexcept
{
    float m = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        m = std::max (m, peak[size_t (ch)].load (std::memory_order_relaxed));
    return gainToDb (m);
}

float LevelMeter::getMaxRmsDb() const noexcept
{
    float m = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        m = std::max (m, rms[size_t (ch)].load (std::memory_order_relaxed));
    return gainToDb (m);
}

} // namespace livemix
