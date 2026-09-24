#include "SampleReplacer.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

void SampleReplacer::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    (void) numChannels;
    sr = sampleRate;
    maxBlock = maxBlockSize > 0 ? maxBlockSize : 1;
    mono.assign (size_t (maxBlock), 0.0f);
    trigger.prepare (sr);
    player.prepare (sr);
    setParams (params);
    reset();
}

void SampleReplacer::reset() noexcept
{
    trigger.reset();
    player.reset();
}

void SampleReplacer::setParams (const Params& p) noexcept
{
    params = p;
    SampleTrigger::Params tp;
    tp.enabled = p.enabled;
    tp.thresholdDb = p.thresholdDb;
    tp.riseDb = p.riseDb;
    tp.hpfHz = p.detHpfHz;
    tp.lpfHz = p.detLpfHz;
    tp.maskMs = p.maskMs;
    trigger.setParams (tp);
    sampleGainLin = dbToGain (clamp (p.gainDb, -60.0f, 12.0f));
    rateMul = std::pow (2.0, double (clamp (p.rateSemitones, -12.0f, 12.0f)) / 12.0);
    offsetSamples = int (clamp (p.offsetMs, 0.0f, 20.0f) * 0.001f * float (sr));
    velocityDepthLin = dbToGain (-clamp (p.velocityDepthDb, 0.0f, 40.0f));
}

void SampleReplacer::detect (const AudioBlockView& preGate) noexcept
{
    if (! params.enabled || maxBlock == 0) return;
    const int n = preGate.numSamples < maxBlock ? preGate.numSamples : maxBlock;
    const int numCh = preGate.numChannels;
    if (numCh <= 0 || n <= 0) return;
    float* m = mono.data();
    if (numCh == 1)
    {
        const float* x = preGate.channels[0];
        for (int i = 0; i < n; ++i) m[i] = x[i];
    }
    else
    {
        const float scale = 1.0f / float (numCh);
        for (int i = 0; i < n; ++i)
        {
            float acc = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) acc += preGate.channels[ch][i];
            m[i] = acc * scale;
        }
    }

    SampleTrigger::Hit hits[SampleTrigger::kMaxHits];
    const int found = trigger.process (m, n, hits, SampleTrigger::kMaxHits);
    for (int h = 0; h < found; ++h)
    {
        const auto& hit = hits[h];
        // Velocity: the softest hit plays velocityDepth quieter than the loudest, linearly in
        // dB; STEADY plays every hit at the full level. Confidence scales a doubtful hit down.
        const float velocityGain = params.steady ? 1.0f : velocityDepthLin + (1.0f - velocityDepthLin) * hit.velocity;
        const float gain = sampleGainLin * velocityGain * hit.confidence;
        player.trigger (hit.offset + offsetSamples, hit.velocity, gain, rateMul);
    }
}

void SampleReplacer::apply (AudioBlockView& postGate) noexcept
{
    if (! params.enabled) return;
    const float blend = clamp (params.blend, 0.0f, 1.0f);
    const int n = postGate.numSamples;
    if (blend > 0.0f)
    {
        const float keep = 1.0f - blend;
        for (int ch = 0; ch < postGate.numChannels; ++ch)
        {
            float* x = postGate.channels[ch];
            for (int i = 0; i < n; ++i) x[i] *= keep;
        }
    }
    player.render (postGate, params.polarityFlip ? -blend : blend);
}

} // namespace livemix
