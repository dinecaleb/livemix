#include "Compressor.h"
#include "Core/DbUtils.h"
#include "Core/FastMath.h"
#include "Core/EnvelopeFollower.h"
#include <cmath>

namespace livemix
{

void Compressor::prepare (double sampleRate, int, int)
{
    sr = sampleRate;
    preDetector.prepare (sr);
    preDetector.setAttackMs (0.0f);
    preDetector.setReleaseMs (1.5f);
    makeupGain.prepare (sr, 20.0f);
    mixSmoother.prepare (sr, 20.0f);
    detectorHpfHzApplied = -1.0f;
    setParams (params);
    makeupGain.snapToTarget();
    mixSmoother.snapToTarget();
    reset();
}

void Compressor::reset() noexcept
{
    smoothedReductionDb = 0.0f;
    preDetector.reset();
    detectorHpf.reset();
}

void Compressor::setParams (const Params& p) noexcept
{
    params = p;
    attackCoeff  = EnvelopeFollower::coefficientFor (p.attackMs, sr);
    releaseCoeff = EnvelopeFollower::coefficientFor (p.releaseMs, sr);
    makeupGain.setTarget (dbToGain (p.makeupDb));
    mixSmoother.setTarget (p.mix);
    detectorFiltered = p.detectorHpfHz >= 20.0f;
    if (detectorFiltered && p.detectorHpfHz != detectorHpfHzApplied)
    {
        detectorHpf.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, p.detectorHpfHz, 0.7071f, 0.0f));
        detectorHpfHzApplied = p.detectorHpfHz;
    }
}

float Compressor::computeGain (float x, float T, float R, float W) noexcept
{
    const float overshoot = x - T;
    if (W <= 0.0f)
        return overshoot <= 0.0f ? x : T + overshoot / R;
    if (2.0f * overshoot < -W) return x;
    if (2.0f * overshoot > W)  return T + overshoot / R;
    const float k = overshoot + W * 0.5f;
    return x + (1.0f / R - 1.0f) * k * k / (2.0f * W);
}

void Compressor::process (AudioBlockView& block) noexcept
{
    if (! params.enabled)
    {
        smoothedReductionDb = 0.0f;
        return;
    }

    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    const float T = params.thresholdDb;
    const float R = params.ratio < 1.0f ? 1.0f : params.ratio;
    const float W = params.kneeDb;

    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = block.channels[ch][i];
            const float a = std::fabs (detectorFiltered ? detectorHpf.processSample (ch, x) : x);
            if (a > peak) peak = a;
        }

        const float inDb = fastmath::gainToDb (preDetector.process (peak));
        const float outDb = computeGain (inDb, T, R, W);
        const float reduction = inDb - outDb; // >= 0

        // Smooth the control signal in the log domain: attack when reduction grows.
        const float c = reduction > smoothedReductionDb ? attackCoeff : releaseCoeff;
        smoothedReductionDb += c * (reduction - smoothedReductionDb);
        if (smoothedReductionDb < 1.0e-6f) smoothedReductionDb = 0.0f;

        const float wetGain = fastmath::dbToGain (-smoothedReductionDb) * makeupGain.next();
        const float mix = mixSmoother.next();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = block.channels[ch][i];
            block.channels[ch][i] = dry + mix * (dry * wetGain - dry);
        }
    }
}

} // namespace livemix
