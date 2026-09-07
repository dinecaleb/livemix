#include "FxChain.h"
#include "Core/DbUtils.h"
#include <cmath>
#include <algorithm>

namespace livemix
{

void FxChain::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate;
    channels = numChannels < 1 ? 1 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);
    maxBlock = maxBlockSize < 1 ? 1 : maxBlockSize;

    inputMeter.prepare (sr, maxBlock, channels);
    outputMeter.prepare (sr, maxBlock, channels);
    reverb.prepare (sr, maxBlock, channels);
    delay.prepare (sr, maxBlock, channels);

    dryStore.assign (size_t (kMaxChannels * maxBlock), 0.0f);
    delayStore.assign (size_t (kMaxChannels * maxBlock), 0.0f);
    reverbStore.assign (size_t (kMaxChannels * maxBlock), 0.0f);
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        dryPtr[size_t (ch)] = dryStore.data() + ch * maxBlock;
        delayPtr[size_t (ch)] = delayStore.data() + ch * maxBlock;
        reverbPtr[size_t (ch)] = reverbStore.data() + ch * maxBlock;
    }

    inputGain.prepare (sr, 20.0f);
    outputGain.prepare (sr, 20.0f);
    mix.prepare (sr, 30.0f);
    toReverb.prepare (sr, 30.0f);
    matchGain.prepare (sr, 50.0f);
    loudnessCoeff = float (1.0 - std::exp (-double (maxBlock) / (2.0 * sr)));
    setParameters (params);
    inputGain.snapToTarget(); outputGain.snapToTarget(); mix.snapToTarget(); toReverb.snapToTarget();
    reset();
}

void FxChain::reset() noexcept
{
    inputMeter.reset();
    outputMeter.reset();
    reverb.reset();
    delay.reset();
    inputMeanSquare = outputMeanSquare = 0.0f;
    matchGain.snapTo (1.0f);
}

void FxChain::setParameters (const FxParameters& p) noexcept
{
    params = p;
    inputGain.setTarget (dbToGain (p.inputTrimDb));
    outputGain.setTarget (dbToGain (p.outputTrimDb));
    mix.setTarget (clamp (p.mix, 0.0f, 1.0f));
    toReverb.setTarget (clamp (p.delayToReverb, 0.0f, 100.0f) * 0.01f);

    ReverbAlgorithm::Params r;
    r.enabled = p.reverbEnabled; r.decayS = p.reverbDecayS; r.preDelayMs = p.reverbPreDelayMs; r.size = p.reverbSize;
    r.damping = p.reverbDamping; r.diffusion = p.reverbDiffusion; r.lowCutHz = p.reverbLowCutHz; r.highCutHz = p.reverbHighCutHz;
    r.modRateHz = p.reverbModRateHz; r.modDepth = p.reverbModDepth; r.early = p.reverbEarly; r.levelDb = p.reverbLevelDb;
    reverb.setParams (r);

    DelayAlgorithm::Params d;
    d.enabled = p.delayEnabled; d.mode = p.delayMode; d.sync = p.delaySync; d.timeMs = p.delayTimeMs; d.division = p.delayDivision;
    d.offsetPercent = p.delayOffset; d.feedback = p.delayFeedback; d.lowCutHz = p.delayLowCutHz; d.highCutHz = p.delayHighCutHz;
    d.width = p.delayWidth; d.duck = p.delayDuck; d.duckReleaseMs = p.delayDuckReleaseMs; d.modRateHz = p.delayModRateHz;
    d.modDepth = p.delayModDepth; d.levelDb = p.delayLevelDb;
    delay.setParams (d);
}

float FxChain::getTailSeconds() const noexcept
{
    float t = 0.0f;
    if (params.reverbEnabled) t = std::max (t, reverb.getTailSeconds());
    if (params.delayEnabled) t = std::max (t, delay.getTailSeconds() + (params.delayToReverb > 0.0f && params.reverbEnabled ? reverb.getTailSeconds() : 0.0f));
    return t;
}

float FxChain::getLoudnessMatchGainDb() const noexcept
{
    return params.bypassAll ? gainToDb (matchGain.getCurrent()) : 0.0f;
}

void FxChain::process (AudioBlockView& block) noexcept
{
    inputMeter.process (block);

    const int n = block.numSamples < maxBlock ? block.numSamples : maxBlock;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    if (n <= 0 || numCh <= 0) return;

    auto blockMeanSquare = [&]
    {
        double acc = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i) acc += double (block.channels[ch][i]) * block.channels[ch][i];
        return float (acc / double (n * numCh));
    };

    if (params.bypassAll)
    {
        // A/B "ORIGINAL": dry signal, optionally at the processed loudness. Match gain frozen while bypassed.
        inputGain.snapToTarget();
        outputGain.snapToTarget();
        float target = 1.0f;
        if (params.abLoudnessMatch && inputMeanSquare > 1.0e-10f && outputMeanSquare > 1.0e-10f)
            target = clamp (std::sqrt (outputMeanSquare / inputMeanSquare), 0.25f, 4.0f);
        matchGain.setTarget (target);
        if (matchGain.isSmoothing())
        {
            for (int i = 0; i < n; ++i)
            {
                const float g = matchGain.next();
                for (int ch = 0; ch < numCh; ++ch) block.channels[ch][i] *= g;
            }
        }
        else if (target != 1.0f)
        {
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < n; ++i) block.channels[ch][i] *= target;
        }
        outputMeter.process (block);
        return;
    }

    const float inMs = blockMeanSquare();
    inputMeanSquare += loudnessCoeff * (inMs - inputMeanSquare);
    matchGain.setTarget (1.0f);
    matchGain.snapToTarget();

    // Trim, keep the dry copy.
    for (int i = 0; i < n; ++i)
    {
        const float g = inputGain.next();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = block.channels[ch][i] * g;
            block.channels[ch][i] = x;
            dryPtr[size_t (ch)][i] = x;
            delayPtr[size_t (ch)][i] = x;
        }
    }

    AudioBlockView delayView { delayPtr.data(), numCh, n };
    delay.process (delayView);

    for (int i = 0; i < n; ++i)
    {
        const float send = toReverb.next();
        for (int ch = 0; ch < numCh; ++ch)
            reverbPtr[size_t (ch)][i] = dryPtr[size_t (ch)][i] + send * delayPtr[size_t (ch)][i];
    }
    AudioBlockView reverbView { reverbPtr.data(), numCh, n };
    reverb.process (reverbView);

    for (int i = 0; i < n; ++i)
    {
        const float m = mix.next();
        const float og = outputGain.next();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float wet = delayPtr[size_t (ch)][i] + reverbPtr[size_t (ch)][i];
            const float dry = dryPtr[size_t (ch)][i];
            block.channels[ch][i] = og * (dry + m * (wet - dry));
        }
    }

    const float outMs = blockMeanSquare();
    outputMeanSquare += loudnessCoeff * (outMs - outputMeanSquare);
    outputMeter.process (block);
}

} // namespace livemix
