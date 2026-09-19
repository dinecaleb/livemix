#include "ChannelProcessor.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

void ChannelProcessor::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate;
    channels = numChannels < kMaxChannels ? numChannels : kMaxChannels;

    inputMeter.prepare (sr, maxBlockSize, channels);
    outputMeter.prepare (sr, maxBlockSize, channels);
    inputGain.prepare (sr, 20.0f);
    outputGain.prepare (sr, 20.0f);
    matchGain.prepare (sr, 50.0f);
    matchGain.snapTo (1.0f);
    // Block-rate one-pole over ~2 s.
    loudnessCoeff = float (1.0 - std::exp (-double (maxBlockSize) / (2.0 * sr)));
    filters.prepare (sr, maxBlockSize, channels);
    gate.prepare (sr, maxBlockSize, channels);
    correctiveEq.setNumBands (ParamID::kCorrectiveBands);
    correctiveEq.prepare (sr, maxBlockSize, channels);
    compressor.prepare (sr, maxBlockSize, channels);
    transient.prepare (sr, maxBlockSize, channels);
    toneEq.setNumBands (ParamID::kToneBands);
    toneEq.prepare (sr, maxBlockSize, channels);
    saturator.prepare (sr, maxBlockSize, channels);
    deEsser.prepare (sr, maxBlockSize, channels);
    width.prepare (sr, maxBlockSize, channels);
    width.setMeteringEnabled (options.widthMeter);
    if (options.limiter) limiter.prepare (sr, maxBlockSize, channels);
    if (options.loudnessMeter) loudness.prepare (sr, maxBlockSize, channels);

    setParameters (params);
    inputGain.snapToTarget();
    outputGain.snapToTarget();
    reset();
}

void ChannelProcessor::reset() noexcept
{
    inputMeanSquare = outputMeanSquare = 0.0f;
    matchGain.snapTo (1.0f);
    inputMeter.reset();
    outputMeter.reset();
    filters.reset();
    gate.reset();
    correctiveEq.reset();
    compressor.reset();
    transient.reset();
    toneEq.reset();
    saturator.reset();
    deEsser.reset();
    width.reset();
    if (options.limiter) limiter.reset();
    if (options.loudnessMeter) loudness.reset();
}

void ChannelProcessor::setParameters (const ChannelParameters& p) noexcept
{
    params = p;

    const float polaritySign = p.polarityInvert ? -1.0f : 1.0f;
    inputGain.setTarget (dbToGain (p.inputTrimDb) * polaritySign);
    outputGain.setTarget (dbToGain (p.outputTrimDb));

    FilterProcessor::Params fp;
    fp.hpfEnabled = p.hpfEnabled; fp.hpfHz = p.hpfHz; fp.hpfSlopeDbPerOct = p.hpfSlope == 1 ? 24 : 12;
    fp.lpfEnabled = p.lpfEnabled; fp.lpfHz = p.lpfHz; fp.lpfSlopeDbPerOct = p.lpfSlope == 1 ? 24 : 12;
    filters.setParams (fp);

    GateExpander::Params gp;
    gp.enabled = p.gateEnabled; gp.thresholdDb = p.gateThresholdDb; gp.rangeDb = p.gateRangeDb;
    gp.attackMs = p.gateAttackMs; gp.holdMs = p.gateHoldMs; gp.releaseMs = p.gateReleaseMs;
    gp.hysteresisDb = p.gateHysteresisDb; gp.ratio = p.gateRatio; gp.detectorHpfHz = p.gateScHpfHz;
    gate.setParams (gp);

    correctiveEq.setEnabled (p.correctiveEqEnabled);
    for (int i = 0; i < ParamID::kCorrectiveBands; ++i)
        correctiveEq.setBand (i, p.correctiveBands[size_t (i)]);

    Compressor::Params cp;
    cp.enabled = p.compEnabled; cp.thresholdDb = p.compThresholdDb; cp.ratio = p.compRatio;
    cp.attackMs = p.compAttackMs; cp.releaseMs = p.compReleaseMs; cp.kneeDb = p.compKneeDb;
    cp.makeupDb = p.compMakeupDb; cp.mix = p.compMix; cp.detectorHpfHz = p.compScHpfHz;
    compressor.setParams (cp);

    TransientProcessor::Params tp;
    tp.enabled = p.transientEnabled; tp.attack = p.transientAttack; tp.sustain = p.transientSustain;
    transient.setParams (tp);

    toneEq.setEnabled (p.toneEqEnabled);
    for (int i = 0; i < ParamID::kToneBands; ++i)
        toneEq.setBand (i, p.toneBands[size_t (i)]);

    Saturator::Params sp;
    sp.enabled = p.satEnabled; sp.drive = p.satDrive; sp.mix = p.satMix;
    saturator.setParams (sp);

    DeEsser::Params dp;
    dp.enabled = p.deEssEnabled; dp.freqHz = p.deEssHz; dp.thresholdDb = p.deEssThresholdDb; dp.rangeDb = p.deEssRangeDb;
    deEsser.setParams (dp);

    StereoWidth::Params wp;
    wp.enabled = p.widthEnabled; wp.width = p.widthAmount; wp.monoBelowHz = p.widthMonoBelowHz;
    width.setParams (wp);

    if (options.limiter)
    {
        Limiter::Params lp;
        lp.enabled = p.limiterEnabled; lp.ceilingDb = p.limiterCeilingDb; lp.releaseMs = p.limiterReleaseMs;
        limiter.setParams (lp);
    }
}

void ChannelProcessor::process (AudioBlockView& block) noexcept LIVEMIX_NONBLOCKING
{
    inputMeter.process (block);

    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;

    auto blockMeanSquare = [&]
    {
        double acc = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i) acc += double (block.channels[ch][i]) * block.channels[ch][i];
        return float (acc / double (n * numCh));
    };

    if (params.bypassAll)
    {
        // A/B "ORIGINAL": pass audio untouched, optionally at the processed loudness so the
        // comparison is fair. The match gain is frozen while bypassed.
        inputGain.snapToTarget();
        outputGain.snapToTarget();
        float target = 1.0f;
        if (params.abLoudnessMatch && inputMeanSquare > 1.0e-10f && outputMeanSquare > 1.0e-10f)
            target = clamp (std::sqrt (outputMeanSquare / inputMeanSquare), 0.25f, 4.0f); // ±12 dB
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
        if (options.limiter) limiter.processDelayOnly (block); // constant latency while comparing
        if (options.loudnessMeter) loudness.process (block);
        outputMeter.process (block);
        return;
    }

    // Track the raw input loudness (before trim) for the A/B match.
    const float inMs = blockMeanSquare();
    inputMeanSquare += loudnessCoeff * (inMs - inputMeanSquare);
    matchGain.setTarget (1.0f);
    matchGain.snapToTarget();

    // Trim + polarity
    if (inputGain.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = inputGain.next();
            for (int ch = 0; ch < numCh; ++ch) block.channels[ch][i] *= g;
        }
    }
    else
    {
        const float g = inputGain.getCurrent();
        if (g != 1.0f)
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < n; ++i) block.channels[ch][i] *= g;
    }

    filters.process (block);
    gate.process (block);
    correctiveEq.process (block);
    deEsser.process (block);
    compressor.process (block);
    transient.process (block);
    toneEq.process (block);
    saturator.process (block);
    width.process (block);

    if (outputGain.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = outputGain.next();
            for (int ch = 0; ch < numCh; ++ch) block.channels[ch][i] *= g;
        }
    }
    else
    {
        const float g = outputGain.getCurrent();
        if (g != 1.0f)
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < n; ++i) block.channels[ch][i] *= g;
    }

    if (options.limiter) limiter.process (block);
    if (options.loudnessMeter) loudness.process (block);

    const float outMs = blockMeanSquare();
    outputMeanSquare += loudnessCoeff * (outMs - outputMeanSquare);

    outputMeter.process (block);
}

float ChannelProcessor::getLoudnessMatchGainDb() const noexcept
{
    return params.bypassAll ? gainToDb (matchGain.getCurrent()) : 0.0f;
}

} // namespace livemix
