#include "ReverbAlgorithm.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace
{
    // Dattorro plate lengths at 29761 Hz.
    constexpr float kInputApLen[4] { 142.0f, 107.0f, 379.0f, 277.0f };
    constexpr float kInputApGain[4] { 0.75f, 0.75f, 0.625f, 0.625f };
    constexpr float kTankModApLen[2] { 672.0f, 908.0f };
    constexpr float kTankDelay1Len[2] { 4453.0f, 4217.0f };
    constexpr float kTankApLen[2] { 1800.0f, 2656.0f };
    constexpr float kTankDelay2Len[2] { 3720.0f, 3163.0f };
    constexpr float kDecayDiffusion1 = 0.70f, kDecayDiffusion2 = 0.50f;
    constexpr float kMaxExcursion = 16.0f;
    constexpr float kMaxSizeScale = 1.6f;

    // Output taps: {line, position}. Lines: 0 = delay1 L, 1 = delay1 R, 2 = ap L, 3 = ap R, 4 = delay2 L, 5 = delay2 R.
    struct Tap { int line; float pos; float sign; };
    constexpr Tap kTapsL[7] { { 1, 266.0f, 1.0f }, { 1, 2974.0f, 1.0f }, { 3, 1913.0f, -1.0f }, { 5, 1996.0f, 1.0f },
                              { 0, 1990.0f, -1.0f }, { 2, 187.0f, -1.0f }, { 4, 1066.0f, -1.0f } };
    constexpr Tap kTapsR[7] { { 0, 353.0f, 1.0f }, { 0, 3627.0f, 1.0f }, { 2, 1228.0f, -1.0f }, { 4, 2673.0f, 1.0f },
                              { 1, 2111.0f, -1.0f }, { 3, 335.0f, -1.0f }, { 5, 121.0f, -1.0f } };

    // Early reflections (ms at size 50 %), alternating sides, decaying.
    constexpr float kEarlyMsL[6] { 7.9f, 13.7f, 21.3f, 29.1f, 37.7f, 44.3f };
    constexpr float kEarlyMsR[6] { 9.1f, 15.9f, 23.4f, 31.7f, 39.2f, 47.1f };
    constexpr float kEarlyGain[6] { 0.90f, 0.70f, 0.55f, 0.45f, 0.35f, 0.25f };
    constexpr float kMaxPreDelayMs = 250.0f, kMaxEarlyMs = 48.0f;
}

float ReverbAlgorithm::decayGainFor (float decayS, float sizePercent) noexcept
{
    // Mean length of one tank half at the reference rate, in seconds, times the size scale.
    const float halfLoopSeconds = 0.5f * ((672.0f + 4453.0f + 1800.0f + 3720.0f) + (908.0f + 4217.0f + 2656.0f + 3163.0f)) / float (kReferenceRate);
    const float t = halfLoopSeconds * sizeScale (sizePercent);
    const float rt = decayS < 0.05f ? 0.05f : decayS;
    return clamp (std::pow (10.0f, -3.0f * t / rt), 0.0f, 0.985f);
}

void ReverbAlgorithm::prepare (double sampleRate, int, int numChannels)
{
    sr = sampleRate;
    channels = numChannels < 1 ? 1 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);
    rateScale = float (sr / kReferenceRate);
    const float maxScale = rateScale * kMaxSizeScale;

    pre.prepare (int ((kMaxPreDelayMs + kMaxEarlyMs * kMaxSizeScale / sizeScale (50.0f)) * 0.001 * sr) + 8, kMaxChannels);
    for (int i = 0; i < kInputAllpasses; ++i) inputAp[size_t (i)].prepare (int (kInputApLen[i] * maxScale) + 4, 1);
    for (int h = 0; h < 2; ++h)
    {
        tankModAp[size_t (h)].prepare (int (kTankModApLen[h] * maxScale + kMaxExcursion * rateScale) + 8, 1);
        tankDelay1[size_t (h)].prepare (int (kTankDelay1Len[h] * maxScale) + 4, 1);
        tankAp[size_t (h)].prepare (int (kTankApLen[h] * maxScale) + 4, 1);
        tankDelay2[size_t (h)].prepare (int (kTankDelay2Len[h] * maxScale) + 4, 1);
        lfo[size_t (h)].prepare (sr);
    }
    lfo[1].setPhase (0.37f);

    size.prepare (sr, 120.0f);
    decayGain.prepare (sr, 50.0f);
    preDelaySamples.prepare (sr, 60.0f);
    level.prepare (sr, 20.0f);
    earlyLevel.prepare (sr, 20.0f);
    dampCoeff.prepare (sr, 50.0f);
    diffusionScale.prepare (sr, 50.0f);
    modExcursion.prepare (sr, 50.0f);
    lowCutApplied = highCutApplied = -1.0f;
    updateCoefficients();
    size.snapToTarget(); decayGain.snapToTarget(); preDelaySamples.snapToTarget(); level.snapToTarget();
    earlyLevel.snapToTarget(); dampCoeff.snapToTarget(); diffusionScale.snapToTarget(); modExcursion.snapToTarget();
    reset();
}

void ReverbAlgorithm::reset() noexcept
{
    pre.reset();
    for (auto& l : inputAp) l.reset();
    for (int h = 0; h < 2; ++h)
    {
        tankModAp[size_t (h)].reset(); tankDelay1[size_t (h)].reset(); tankAp[size_t (h)].reset(); tankDelay2[size_t (h)].reset();
        dampState[size_t (h)] = 0.0f; tankFeedback[size_t (h)] = 0.0f;
        modApState[size_t (h)] = delay1State[size_t (h)] = apState[size_t (h)] = delay2State[size_t (h)] = 0.0f;
    }
    for (auto& s : inputApState) s = 0.0f;
    lowCut.reset();
    highCut.reset();
}

void ReverbAlgorithm::setParams (const Params& p) noexcept
{
    params = p;
    updateCoefficients();
}

void ReverbAlgorithm::updateCoefficients() noexcept
{
    size.setTarget (sizeScale (clamp (params.size, 0.0f, 100.0f)));
    decayGain.setTarget (decayGainFor (params.decayS, params.size));
    preDelaySamples.setTarget (clamp (params.preDelayMs, 0.0f, kMaxPreDelayMs) * 0.001f * float (sr));
    level.setTarget (params.enabled ? dbToGain (params.levelDb) : 0.0f);
    earlyLevel.setTarget (0.5f * clamp (params.early, 0.0f, 100.0f) * 0.01f);
    // Damping: one-pole low-pass in the tank; 0 % = 20 kHz (none), 100 % = 625 Hz.
    const float dampHz = 20000.0f * std::pow (2.0f, -5.0f * clamp (params.damping, 0.0f, 100.0f) * 0.01f);
    dampCoeff.setTarget (std::exp (-2.0f * float (M_PI) * dampHz / float (sr)));
    diffusionScale.setTarget (0.25f + 0.75f * clamp (params.diffusion, 0.0f, 100.0f) * 0.01f);
    modExcursion.setTarget (kMaxExcursion * rateScale * clamp (params.modDepth, 0.0f, 100.0f) * 0.01f);
    const float rate = clamp (params.modRateHz, 0.05f, 10.0f);
    lfo[0].setRateHz (rate);
    lfo[1].setRateHz (rate * 1.13f);

    if (params.lowCutHz != lowCutApplied)
    {
        lowCutApplied = params.lowCutHz;
        lowCut.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, clamp (params.lowCutHz, 10.0f, 2000.0f), 0.7071f, 0.0f));
    }
    if (params.highCutHz != highCutApplied)
    {
        highCutApplied = params.highCutHz;
        highCut.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, sr, clamp (params.highCutHz, 500.0f, 20000.0f), 0.7071f, 0.0f));
    }
}

inline float ReverbAlgorithm::allpass (DelayLine& line, float& interpState, float delaySamples, float gain, float x) noexcept
{
    const float delayed = line.readAllpass (0, delaySamples, interpState);
    const float v = x + gain * delayed;
    line.write (0, v);
    return delayed - gain * v;
}

void ReverbAlgorithm::process (AudioBlockView& block) noexcept
{
    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    if (numCh <= 0) return;

    if (! params.enabled)
    {
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i) block.channels[ch][i] = 0.0f;
        wasEnabled = false;
        return;
    }
    if (! wasEnabled) { reset(); wasEnabled = true; }

    float* outL = block.channels[0];
    float* outR = numCh > 1 ? block.channels[1] : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float s = size.next();
        const float scale = rateScale * s;
        const float g = decayGain.next();
        const float damp = dampCoeff.next();
        const float diff = diffusionScale.next();
        const float exc = modExcursion.next();
        const float preD = preDelaySamples.next();
        const float lvl = level.next();
        const float eLvl = earlyLevel.next();

        // Input: filter, pre-delay, early reflections, mono sum for the tank.
        const float inL = outL[i];
        const float inR = outR != nullptr ? outR[i] : inL;
        const float xL = highCut.processSample (0, lowCut.processSample (0, inL));
        const float xR = highCut.processSample (1, lowCut.processSample (1, inR));
        pre.write (0, xL);
        pre.write (1, xR);
        const float preRead = preD < 1.0f ? 1.0f : preD;
        const float tankIn = 0.5f * (pre.readFractional (0, preRead) + pre.readFractional (1, preRead));
        float earlyL = 0.0f, earlyR = 0.0f;
        const float earlyScale = s / sizeScale (50.0f) * 0.001f * float (sr);
        for (int k = 0; k < kEarlyTaps; ++k)
        {
            earlyL += kEarlyGain[k] * pre.readFractional (0, preRead + kEarlyMsL[k] * earlyScale);
            earlyR += kEarlyGain[k] * pre.readFractional (1, preRead + kEarlyMsR[k] * earlyScale);
        }
        pre.advance();

        // Input diffusion.
        float x = tankIn;
        for (int k = 0; k < kInputAllpasses; ++k)
        {
            x = allpass (inputAp[size_t (k)], inputApState[size_t (k)], kInputApLen[k] * scale, kInputApGain[k] * diff, x);
            inputAp[size_t (k)].advance();
        }

        // Tank: two cross-coupled halves, each = modulated allpass -> delay -> damping -> decay -> allpass -> delay.
        const float mod[2] { lfo[0].next(), lfo[1].next() };
        for (int h = 0; h < 2; ++h)
        {
            auto& modAp = tankModAp[size_t (h)];
            auto& d1 = tankDelay1[size_t (h)];
            auto& ap = tankAp[size_t (h)];
            auto& d2 = tankDelay2[size_t (h)];
            float t = x + tankFeedback[size_t (1 - h)];
            t = allpass (modAp, modApState[size_t (h)], kTankModApLen[h] * scale + exc * mod[h], -kDecayDiffusion1 * diff, t);
            d1.write (0, t);
            float a = d1.readAllpass (0, kTankDelay1Len[h] * scale, delay1State[size_t (h)]);
            float& lp = dampState[size_t (h)];
            lp = a * (1.0f - damp) + lp * damp;
            a = flushDenormal (lp);
            a = allpass (ap, apState[size_t (h)], kTankApLen[h] * scale, kDecayDiffusion2 * diff, a);
            d2.write (0, a);
            tankFeedback[size_t (h)] = flushDenormal (d2.readAllpass (0, kTankDelay2Len[h] * scale, delay2State[size_t (h)]) * g);
        }

        // Output taps (read before advancing so positions match the reference network).
        DelayLine* lines[6] { &tankDelay1[0], &tankDelay1[1], &tankAp[0], &tankAp[1], &tankDelay2[0], &tankDelay2[1] };
        float yL = 0.0f, yR = 0.0f;
        for (const auto& tap : kTapsL) yL += tap.sign * lines[tap.line]->readFractional (0, tap.pos * scale);
        for (const auto& tap : kTapsR) yR += tap.sign * lines[tap.line]->readFractional (0, tap.pos * scale);
        for (auto* l : lines) l->advance();
        tankModAp[0].advance();
        tankModAp[1].advance();

        yL = 0.6f * yL + eLvl * earlyL;
        yR = 0.6f * yR + eLvl * earlyR;
        if (outR != nullptr) { outL[i] = lvl * yL; outR[i] = lvl * yR; }
        else outL[i] = lvl * 0.5f * (yL + yR);
    }
}

} // namespace livemix
