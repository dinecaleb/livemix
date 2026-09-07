#include "DeEsser.h"
#include "Core/DbUtils.h"
#include "Core/FastMath.h"
#include <cmath>

namespace livemix
{

void DeEsser::prepare (double sampleRate, int, int)
{
    sr = sampleRate;
    detector.prepare (sr);
    detector.setAttackMs (0.0f);
    detector.setReleaseMs (2.0f);
    attackCoeff = EnvelopeFollower::coefficientFor (0.4f, sr);
    releaseCoeff = EnvelopeFollower::coefficientFor (40.0f, sr);
    freqApplied = -1.0f;
    setParams (params);
    reset();
}

void DeEsser::reset() noexcept
{
    lowA.reset(); lowB.reset(); highA.reset(); highB.reset();
    detectorHpf.reset();
    detector.reset();
    reductionDb = 0.0f;
}

void DeEsser::setParams (const Params& p) noexcept
{
    params = p;
    const float f = clamp (p.freqHz, 2000.0f, 12000.0f);
    if (f != freqApplied)
    {
        // Linkwitz-Riley 4th order (a squared 2nd-order Butterworth): |LP + HP| = 1 at every frequency, 24 dB/oct.
        const auto lp = BiquadCoefficients::make (FilterType::LowPass, sr, f, 0.7071f, 0.0f);
        const auto hp = BiquadCoefficients::make (FilterType::HighPass, sr, f, 0.7071f, 0.0f);
        lowA.setCoefficients (lp); lowB.setCoefficients (lp);
        highA.setCoefficients (hp); highB.setCoefficients (hp);
        detectorHpf.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, f * 0.8f, 0.7071f, 0.0f));
        freqApplied = f;
    }
}

void DeEsser::process (AudioBlockView& block) noexcept
{
    if (! params.enabled) { reductionDb = 0.0f; return; }

    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    const float range = clamp (params.rangeDb, 0.0f, 24.0f);

    for (int i = 0; i < n; ++i)
    {
        // Detector: level of the sibilant band (mono sum).
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) mono += block.channels[ch][i];
        mono /= float (numCh);
        const float det = detector.process (std::fabs (detectorHpf.processSample (0, mono)));
        const float detDb = fastmath::gainToDb (det);
        const float wanted = clamp (detDb - params.thresholdDb, 0.0f, range);
        const float c = wanted > reductionDb ? attackCoeff : releaseCoeff;
        reductionDb += c * (wanted - reductionDb);
        if (reductionDb < 1.0e-4f) reductionDb = 0.0f;
        const float highGain = fastmath::dbToGain (-reductionDb);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = block.channels[ch][i];
            const float lo = lowB.processSample (ch, lowA.processSample (ch, x));
            const float hi = highB.processSample (ch, highA.processSample (ch, x));
            block.channels[ch][i] = lo + hi * highGain;
        }
    }
}

} // namespace livemix
