#include "GateExpander.h"
#include "Core/DbUtils.h"
#include "Core/FastMath.h"
#include <cmath>

namespace livemix
{

void GateExpander::prepare (double sampleRate, int, int)
{
    sr = sampleRate;
    detector.prepare (sr);
    detector.setAttackMs (0.05f);
    detector.setReleaseMs (8.0f);
    detectorHpfHzApplied = -1.0f;
    setParams (params);
    reset();
}

void GateExpander::reset() noexcept
{
    detector.reset();
    detectorHpf.reset();
    holdCounter = 0;
    open = false;
    gain = params.enabled ? 0.0f : 1.0f;
}

void GateExpander::setParams (const Params& p) noexcept
{
    params = p;
    openThresholdLin  = dbToGain (p.thresholdDb);
    closeThresholdLin = dbToGain (p.thresholdDb - p.hysteresisDb);
    attackCoeff  = EnvelopeFollower::coefficientFor (p.attackMs, sr);
    releaseCoeff = EnvelopeFollower::coefficientFor (p.releaseMs, sr);
    holdSamples  = int (p.holdMs * 0.001 * sr);
    detectorFiltered = p.detectorHpfHz >= 20.0f;
    if (detectorFiltered && p.detectorHpfHz != detectorHpfHzApplied)
    {
        // Coefficients only change when the value changes: no trig per block.
        detectorHpf.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, p.detectorHpfHz, 0.7071f, 0.0f));
        detectorHpfHzApplied = p.detectorHpfHz;
    }
}

float GateExpander::getGainReductionDb() const noexcept
{
    return gainToDb (gain);
}

void GateExpander::process (AudioBlockView& block) noexcept
{
    if (! params.enabled)
    {
        gain = 1.0f;
        open = true;
        return;
    }

    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    const float ratioMinusOne = params.ratio - 1.0f;
    const float range = params.rangeDb;
    const float thresholdDb = params.thresholdDb;

    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = block.channels[ch][i];
            const float a = std::fabs (detectorFiltered ? detectorHpf.processSample (ch, x) : x);
            if (a > peak) peak = a;
        }
        const float level = detector.process (peak);

        if (level > openThresholdLin)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (open && level < closeThresholdLin)
        {
            if (holdCounter > 0) --holdCounter;
            else open = false;
        }

        float targetGain;
        if (open)
        {
            targetGain = 1.0f;
        }
        else
        {
            const float levelDb = fastmath::gainToDb (level);
            float reductionDb = (thresholdDb - levelDb) * ratioMinusOne;
            if (reductionDb > range) reductionDb = range;
            if (reductionDb < 0.0f) reductionDb = 0.0f;
            targetGain = fastmath::dbToGain (-reductionDb);
        }

        const float c = targetGain > gain ? attackCoeff : releaseCoeff;
        gain += c * (targetGain - gain);

        for (int ch = 0; ch < numCh; ++ch)
            block.channels[ch][i] *= gain;
    }
}

} // namespace livemix
