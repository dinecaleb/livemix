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
    hitOffsetCount = 0;
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
            // The veto: on a tom or a hat, a hit well under its own loudest within two
            // milliseconds of a hard kick or snare is that drum through the air.
            // The same for a snare hearing the hi-hat: a soft snare hit at the moment of a hard
            // hat hit is the hat through the air (the snare detector's band reaches the hat).
            const bool quietHere = underDb > 9.0f;
            const bool tomOrHat = family == RoleFamily::Tom || family == RoleFamily::HiHat;
            if (quietHere && (tomOrHat || family == RoleFamily::Snare))
            {
                const long long window = (long long) (0.002 * sr);
                bool veto = false;
                auto heard = [&] (RoleFamily other)
                {
                    const auto& e = kit->last[size_t (other)];
                    return e.time >= 0 && e.underDb <= 6.0f && when - e.time <= window && when - e.time >= -window;
                };
                if (tomOrHat) veto = heard (RoleFamily::Kick) || heard (RoleFamily::Snare);
                else          veto = heard (RoleFamily::HiHat);
                if (veto) { vetoed.fetch_add (1, std::memory_order_relaxed); continue; }
            }
            kit->note (family, when, underDb);
        }
        // Velocity: the softest hit plays velocityDepth quieter than the loudest, linearly in
        // dB; STEADY plays every hit at the full level. Confidence scales a doubtful hit down.
        const float velocityGain = params.steady ? 1.0f : velocityDepthLin + (1.0f - velocityDepthLin) * hit.velocity;
        const float gain = sampleGainLin * velocityGain * hit.confidence;
        // On time: the sample starts as far into itself as the hit was recognised late, so its
        // attack lands on the microphone's (a partial blend combed at 3-5 ms of lag). ALIGN
        // then moves it later from there, for taste - it is no longer making up for lateness.
        player.trigger (hit.offset + offsetSamples, hit.velocity, gain, rate, hit.lateBy);
        if (hitOffsetCount < SampleTrigger::kMaxHits) hitOffsetsBlock[hitOffsetCount++] = hit.offset;
    }
}

void SampleReplacer::apply (AudioBlockView& postGate) noexcept
{
    if (! params.enabled) return;
    // THE MICROPHONE IS ONLY TAKEN AWAY BY SOMETHING THAT REPLACES IT.
    //
    // The stage is a crossfade, so at a full blend the microphone is gone and the sample is the
    // channel. With no sound loaded to play - the bank the session names was taken out of the
    // samples folder, the library failed to decode it - that crossfade is a drum turned down by
    // the blend, and at 100 % it is a drum that has gone silent. On a Sunday.
    //
    // So a stage with nothing to play is a no-op: the block passes through bit-identical, the
    // same as switching the stage off. The session still says which sound it wants and DINE
    // still says out loud that it could not find it; what it does not do is take the kick away.
    if (player.getBank() == nullptr) return;

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
