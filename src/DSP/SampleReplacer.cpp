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
    vetoed.store (0, std::memory_order_relaxed);
    loudestDb = -120.0f;
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

double SampleReplacer::currentRate() const noexcept
{
    double rate = rateMul;
    const SampleBank* b = player.getBank();
    if (params.followDrum && params.drumHz > 0.0f && b != nullptr && b->fundamentalHz > 0.0f)
    {
        double follow = double (params.drumHz) / double (b->fundamentalHz);
        // Never more than five semitones either way: further than that is a different drum.
        const double limit = std::pow (2.0, 5.0 / 12.0);
        if (follow > limit) follow = limit;
        if (follow < 1.0 / limit) follow = 1.0 / limit;
        rate *= follow;
    }
    return rate;
}

void SampleReplacer::detect (const AudioBlockView& preGate, long long blockStart) noexcept
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
    const double rate = currentRate();
    loudestDb -= float (n) / float (sr);            // 1 dB a second
    for (int h = 0; h < found; ++h)
    {
        const auto& hit = hits[h];
        // How far under this strip's own loudest recent hit this one landed, in the detector's
        // own frame: 0 for the loudest, 20 for one 20 dB softer. A tom's own hits are loud on
        // its own microphone; what it hears of the rest of the kit is not.
        if (hit.levelDb > loudestDb) loudestDb = hit.levelDb;
        const float underDb = loudestDb - hit.levelDb;
        const long long when = blockStart + hit.offset;
        if (kit != nullptr)
        {
            // The veto: on a tom, a hit well under its own loudest within two milliseconds of a
            // hard kick or snare is that drum through the air.
            if (family == RoleFamily::Tom && underDb > 9.0f)
            {
                const long long window = (long long) (0.002 * sr);
                bool veto = false;
                for (auto other : { RoleFamily::Kick, RoleFamily::Snare })
                {
                    const auto& e = kit->last[size_t (other)];
                    if (e.time >= 0 && e.underDb <= 6.0f && when - e.time <= window && when - e.time >= -window) veto = true;
                }
                if (veto) { vetoed.fetch_add (1, std::memory_order_relaxed); continue; }
            }
            kit->note (family, when, underDb);
        }
        // Velocity: the softest hit plays velocityDepth quieter than the loudest, linearly in
        // dB; STEADY plays every hit at the full level. Confidence scales a doubtful hit down.
        const float velocityGain = params.steady ? 1.0f : velocityDepthLin + (1.0f - velocityDepthLin) * hit.velocity;
        const float gain = sampleGainLin * velocityGain * hit.confidence;
        player.trigger (hit.offset + offsetSamples, hit.velocity, gain, rate);
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
