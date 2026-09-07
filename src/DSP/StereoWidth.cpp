#include "StereoWidth.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

void StereoWidth::prepare (double sampleRate, int maxBlockSize, int)
{
    sr = sampleRate;
    widthSmoother.prepare (sr, 30.0f);
    monoBelowApplied = -1.0f;
    corrCoeff = float (1.0 - std::exp (-double (maxBlockSize) / (0.5 * sr))); // ~0.5 s block-rate average
    setParams (params);
    widthSmoother.snapToTarget();
    reset();
}

void StereoWidth::reset() noexcept
{
    sideHpf.reset();
    sumLL = sumRR = sumLR = 0.0;
    correlation.store (1.0f, std::memory_order_relaxed);
}

void StereoWidth::setParams (const Params& p) noexcept
{
    params = p;
    widthSmoother.setTarget (clamp (p.width, 0.0f, 2.0f));
    sideFiltered = p.monoBelowHz >= 20.0f;
    if (sideFiltered && p.monoBelowHz != monoBelowApplied)
    {
        sideHpf.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, clamp (p.monoBelowHz, 20.0f, 500.0f), 0.7071f, 0.0f));
        monoBelowApplied = p.monoBelowHz;
    }
}

void StereoWidth::process (AudioBlockView& block) noexcept
{
    if (block.numChannels < 2) return;
    float* L = block.channels[0];
    float* R = block.channels[1];
    const int n = block.numSamples;

    if (params.enabled)
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = widthSmoother.next();
            const float mid = 0.5f * (L[i] + R[i]);
            float side = 0.5f * (L[i] - R[i]);
            if (sideFiltered) side = sideHpf.processSample (0, side);
            side *= w;
            L[i] = mid + side;
            R[i] = mid - side;
        }
    }

    if (! metering) return;
    // Correlation of what leaves the stage (block-rate average).
    double ll = 0.0, rr = 0.0, lr = 0.0;
    for (int i = 0; i < n; ++i) { ll += double (L[i]) * L[i]; rr += double (R[i]) * R[i]; lr += double (L[i]) * R[i]; }
    sumLL += corrCoeff * (ll - sumLL);
    sumRR += corrCoeff * (rr - sumRR);
    sumLR += corrCoeff * (lr - sumLR);
    const double denom = std::sqrt (sumLL * sumRR);
    correlation.store (denom > 1.0e-12 ? float (sumLR / denom) : 1.0f, std::memory_order_relaxed);
}

} // namespace livemix
