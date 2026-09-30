#include "ChannelProcessor.h"
#include <algorithm>
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
    if (options.sampleReplacement) sampler.prepare (sr, maxBlockSize, channels);
    if (options.loudnessMeter) loudness.prepare (sr, maxBlockSize, channels);
    for (auto& f : fadeScratch) f.assign (size_t (maxBlockSize > 0 ? maxBlockSize : 1), 0.0f);
    fadeLength = std::max (1, int (0.010 * sr));
    running = false;
    fades.fill ({});

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
    if (options.sampleReplacement) sampler.reset();
    if (options.loudnessMeter) loudness.reset();
    // A reset is a fresh start: whatever was fading has finished.
    running = false;
    for (int st = 0; st < StCount; ++st) if (fades[size_t (st)].left > 0) { fades[size_t (st)] = {}; applyStage (Stage (st), params); }
}

void ChannelProcessor::setParameters (const ChannelParameters& p) noexcept
{
    params = p;

    const float polaritySign = p.polarityInvert ? -1.0f : 1.0f;
    inputGain.setTarget (dbToGain (p.inputTrimDb) * polaritySign);
    outputGain.setTarget (dbToGain (p.outputTrimDb));

    for (int st = 0; st < StCount; ++st)
    {
        const auto stage = Stage (st);
        const int now = stageSignature (stage, p);
        const int was = stageState[size_t (st)];
        auto& fade = fades[size_t (st)];
        if (running && now != was)
        {
            if (now == 0)
            {
                // Switched off: keep playing the old settings while it fades out; applied at the end.
                fade = { fadeLength, false };
            }
            else
            {
                applyStage (stage, p);
                fade = { fadeLength, true };
            }
        }
        else if (! (fade.left > 0 && ! fade.turningOn && now == 0))
        {
            // Unchanged, or not running yet: the settings are simply the settings. (A stage that
            // is fading out keeps its old ones until the fade finishes.)
            if (now != 0 || fade.left <= 0) applyStage (stage, p);
            if (! running) fade = {};
        }
        stageState[size_t (st)] = now;
    }

    if (options.limiter)
    {
        Limiter::Params lp;
        lp.enabled = p.limiterEnabled; lp.ceilingDb = p.limiterCeilingDb; lp.releaseMs = p.limiterReleaseMs;
        limiter.setParams (lp);
    }

    if (options.sampleReplacement)
    {
        SampleReplacer::Params rp;
        rp.enabled = p.replaceEnabled; rp.blend = p.replaceBlend; rp.thresholdDb = p.replaceThresholdDb; rp.riseDb = p.replaceRiseDb;
        rp.detHpfHz = p.replaceDetHpfHz; rp.detLpfHz = p.replaceDetLpfHz; rp.maskMs = p.replaceMaskMs; rp.steady = p.replaceSteady;
        rp.offsetMs = p.replaceOffsetMs; rp.polarityFlip = p.replacePolarity != 0; rp.rateSemitones = p.replaceRateSemitones;
        rp.gainDb = p.replaceGainDb;
        rp.followDrum = p.replaceFollowDrum; rp.drumHz = p.replaceDrumHz;
        sampler.setParams (rp);
    }
}


int ChannelProcessor::stageSignature (Stage st, const ChannelParameters& p) noexcept
{
    switch (st)
    {
        case StFilters:      return (p.hpfEnabled ? 1 : 0) | (p.lpfEnabled ? 2 : 0);
        case StGate:         return p.gateEnabled ? 1 : 0;
        case StCorrectiveEq: return p.correctiveEqEnabled ? 1 : 0;
        case StDeEsser:      return p.deEssEnabled ? 1 : 0;
        case StComp:         return p.compEnabled ? 1 : 0;
        case StTransient:    return p.transientEnabled ? 1 : 0;
        case StToneEq:       return p.toneEqEnabled ? 1 : 0;
        case StSat:          return p.satEnabled ? 1 : 0;
        case StWidth:        return p.widthEnabled ? 1 : 0;
        case StCount:
        default:             return 0;
    }
}

void ChannelProcessor::applyStage (Stage st, const ChannelParameters& p) noexcept
{
    switch (st)
    {
        case StFilters:
        {
            FilterProcessor::Params fp;
            fp.hpfEnabled = p.hpfEnabled; fp.hpfHz = p.hpfHz; fp.hpfSlopeDbPerOct = p.hpfSlope == 1 ? 24 : 12;
            fp.lpfEnabled = p.lpfEnabled; fp.lpfHz = p.lpfHz; fp.lpfSlopeDbPerOct = p.lpfSlope == 1 ? 24 : 12;
            filters.setParams (fp);
            break;
        }
        case StGate:
        {
            GateExpander::Params gp;
            gp.enabled = p.gateEnabled; gp.thresholdDb = p.gateThresholdDb; gp.rangeDb = p.gateRangeDb;
            gp.attackMs = p.gateAttackMs; gp.holdMs = p.gateHoldMs; gp.releaseMs = p.gateReleaseMs;
            gp.hysteresisDb = p.gateHysteresisDb; gp.ratio = p.gateRatio; gp.detectorHpfHz = p.gateScHpfHz;
            gate.setParams (gp);
            break;
        }
        case StCorrectiveEq:
            correctiveEq.setEnabled (p.correctiveEqEnabled);
            for (int i = 0; i < ParamID::kCorrectiveBands; ++i) correctiveEq.setBand (i, p.correctiveBands[size_t (i)]);
            break;
        case StDeEsser:
        {
            DeEsser::Params dp;
            dp.enabled = p.deEssEnabled; dp.freqHz = p.deEssHz; dp.thresholdDb = p.deEssThresholdDb; dp.rangeDb = p.deEssRangeDb;
            deEsser.setParams (dp);
            break;
        }
        case StComp:
        {
            Compressor::Params cp;
            cp.enabled = p.compEnabled; cp.thresholdDb = p.compThresholdDb; cp.ratio = p.compRatio;
            cp.attackMs = p.compAttackMs; cp.releaseMs = p.compReleaseMs; cp.kneeDb = p.compKneeDb;
            cp.makeupDb = p.compMakeupDb; cp.mix = p.compMix; cp.detectorHpfHz = p.compScHpfHz;
            compressor.setParams (cp);
            break;
        }
        case StTransient:
        {
            TransientProcessor::Params tp;
            tp.enabled = p.transientEnabled; tp.attack = p.transientAttack; tp.sustain = p.transientSustain;
            transient.setParams (tp);
            break;
        }
        case StToneEq:
            toneEq.setEnabled (p.toneEqEnabled);
            for (int i = 0; i < ParamID::kToneBands; ++i) toneEq.setBand (i, p.toneBands[size_t (i)]);
            break;
        case StSat:
        {
            Saturator::Params sp;
            sp.enabled = p.satEnabled; sp.drive = p.satDrive; sp.mix = p.satMix;
            saturator.setParams (sp);
            break;
        }
        case StWidth:
        {
            StereoWidth::Params wp;
            wp.enabled = p.widthEnabled; wp.width = p.widthAmount; wp.monoBelowHz = p.widthMonoBelowHz;
            width.setParams (wp);
            break;
        }
        case StCount:
        default: break;
    }
}

template <typename Fn>
void ChannelProcessor::runStage (Stage st, AudioBlockView& block, Fn&& process) noexcept
{
    auto& fade = fades[size_t (st)];
    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    if (fade.left <= 0 || n > int (fadeScratch[0].size())) { process(); return; }
    for (int ch = 0; ch < numCh; ++ch)
        std::copy (block.channels[ch], block.channels[ch] + n, fadeScratch[size_t (ch)].begin());
    process();
    int left = fade.left;
    for (int i = 0; i < n; ++i)
    {
        const float through = left > 0 ? float (left) / float (fadeLength) : 0.0f;    // 1 -> 0 over the fade
        const float w = fade.turningOn ? 1.0f - through : through;                   // how much of the stage's output
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = fadeScratch[size_t (ch)][size_t (i)];
            block.channels[ch][i] = dry + (block.channels[ch][i] - dry) * w;
        }
        if (left > 0) --left;
    }
    fade.left = left;
    if (left == 0 && ! fade.turningOn) applyStage (st, params);    // faded out: now it is really off
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

    // The sample stage's detector hears the microphone before the filters and the gate;
    // its sample lands after the gate, so a gate never chops a sample's tail. And every hit
    // the sample fires on opens the gate as well, from the hit's own onset: TUNE gates a
    // sampled microphone hard, and a stroke soft enough to sit under that gate but loud enough
    // to fire the sample was a sample with no microphone under it - the attack cut off, the
    // body arriving from nowhere. The gate's own detector still opens it on everything else.
    const bool sampling = options.sampleReplacement && params.replaceEnabled;
    if (sampling)
    {
        sampler.detect (block, blockStart);
        gate.openAt (sampler.hitOffsets(), sampler.numHits(), sampler.hitLookbackSamples());
    }
    runStage (StFilters, block, [&] { filters.process (block); });
    runStage (StGate, block, [&] { gate.process (block); });
    if (sampling) sampler.apply (block);
    runStage (StCorrectiveEq, block, [&] { correctiveEq.process (block); });
    runStage (StDeEsser, block, [&] { deEsser.process (block); });
    runStage (StComp, block, [&] { compressor.process (block); });
    runStage (StTransient, block, [&] { transient.process (block); });
    runStage (StToneEq, block, [&] { toneEq.process (block); });
    runStage (StSat, block, [&] { saturator.process (block); });
    runStage (StWidth, block, [&] { width.process (block); });
    running = true;

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
