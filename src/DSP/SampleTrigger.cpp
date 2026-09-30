#include "SampleTrigger.h"
#include "Core/DbUtils.h"
#include <algorithm>
#include <cmath>

namespace livemix
{

void SampleTrigger::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    fast.prepare (sr);
    fast.setAttackMs (0.1f);
    fast.setReleaseMs (20.0f);
    jumpSamples = int (0.002 * sr);
    if (jumpSamples < 1) jumpSamples = 1;
    if (jumpSamples > kMaxJumpSamples) jumpSamples = kMaxJumpSamples;
    setParams (params);
    reset();
}

void SampleTrigger::reset() noexcept
{
    hpf.reset();
    lpf.reset();
    fast.reset();
    for (auto& h : history) h = 0.0f;
    historyIndex = 0;
    maskLeft = 0;
    maskAge = 0;
    maskPeak = 0.0f;
    armed = true;
    pendingLeft = 0;
    pendingPeak = 0.0f;
    pendingLate = 0;
    hitCount.store (0, std::memory_order_relaxed);
    lastLevelDb.store (-120.0f, std::memory_order_relaxed);
}

void SampleTrigger::setParams (const Params& p) noexcept
{
    params = p;
    const float hp = clamp (p.hpfHz, 20.0f, 2000.0f);
    const float lp = clamp (p.lpfHz, hp * 1.5f, 20000.0f);
    hpf.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, hp, 0.707f, 0.0f));
    lpf.setCoefficients (BiquadCoefficients::make (FilterType::LowPass, sr, lp, 0.707f, 0.0f));
    // The detector hears the drum through its own band-pass, and the low-pass holds it back by
    // its group delay - sqrt(2) / (2 pi fc) for this Butterworth, about 0.9 ms on a kick's
    // 250 Hz. That is time the hit is late by before the follower sees anything.
    filterDelaySamples = int (std::lround (0.2251 / double (lp) * sr));
    thresholdLin = dbToGain (clamp (p.thresholdDb, -80.0f, 0.0f));
    riseLin = dbToGain (clamp (p.riseDb, 0.0f, 40.0f));
    retriggerLin = dbToGain (clamp (p.retriggerDb, 0.0f, 24.0f));
    maskSamples = int (clamp (p.maskMs, 1.0f, 500.0f) * 0.001f * float (sr));
    measureSamples = int (0.0015 * sr);
    // The hit that opened the mask is measured until its body has peaked (a kick peaks 4 ms
    // in, a floor tom later), so its own swell never re-triggers it.
    maskMeasureSamples = std::min (maskSamples / 2, int (0.010 * sr));
}

int SampleTrigger::process (const float* mono, int n, Hit* out, int maxHits) noexcept
{
    if (! params.enabled) return 0;
    int hits = 0;
    const float range = params.velocityRangeDb > 0.5f ? params.velocityRangeDb : 0.5f;
    const float conf = params.confidenceDb > 0.1f ? params.confidenceDb : 0.1f;
    for (int i = 0; i < n; ++i)
    {
        float d = lpf.processSample (0, hpf.processSample (0, mono[i]));
        d = d < 0.0f ? -d : d;
        const float f = fast.process (d);
        // What the follower read two milliseconds ago: a hit is a jump from there, a ring
        // or a decaying tail is not.
        const float before = history[size_t (historyIndex)];
        history[size_t (historyIndex)] = f;
        historyIndex = historyIndex + 1 >= jumpSamples ? 0 : historyIndex + 1;

        // A hit already recognised is being measured: its level is the loudest the fast
        // follower reaches in its first 1.5 ms, and the hit is reported at the end of that.
        // Those 1.5 ms are the only delay the sample ever has.
        if (pendingLeft > 0)
        {
            if (f > pendingPeak) pendingPeak = f;
            if (--pendingLeft == 0)
            {
                const float levelDb = gainToDb (pendingPeak);
                hitCount.fetch_add (1, std::memory_order_relaxed);
                lastLevelDb.store (levelDb, std::memory_order_relaxed);
                if (hits < maxHits)
                {
                    Hit& h = out[hits++];
                    h.offset = i;
                    h.levelDb = levelDb;
                    h.velocity = clamp ((levelDb - params.thresholdDb) / range, 0.0f, 1.0f);
                    h.confidence = clamp ((levelDb - params.thresholdDb) / conf, 0.0f, 1.0f);
                    h.lateBy = pendingLate + measureSamples + filterDelaySamples;
                }
            }
        }

        bool fire = false;
        if (maskLeft > 0)
        {
            --maskLeft;
            ++maskAge;
            // Inside the mask: the hit that opened it is measured until it has peaked; after
            // that a clearly louder onset is a new hit (a flam, the second stroke of a roll)
            // and anything else is the same hit still ringing.
            if (maskAge <= maskMeasureSamples) { if (f > maskPeak) maskPeak = f; }
            else if (f > maskPeak * retriggerLin && f > thresholdLin) fire = true;
        }
        else
        {
            // After the mask the detector re-arms once the hit has fallen 6 dB from its
            // peak, so a long ring is one hit however long it lasts.
            if (! armed && f < maskPeak * 0.5f) armed = true;
            if (armed && f > thresholdLin && f > before * riseLin) fire = true;
        }
        if (! fire) continue;

        maskLeft = maskSamples;
        maskAge = 0;
        maskPeak = f;
        armed = false;
        pendingLeft = measureSamples;
        pendingPeak = f;
        // Where the drum began: the follower's history (two milliseconds of it) read back from
        // now until it was under a tenth of this level. That rise is time the sample would
        // otherwise be late by.
        // Measured from what was there before - silence, or the last hit's tail - rather than
        // from zero: the onset is where the rise had covered a tenth of the way from that to here.
        pendingLate = 0;
        const float floorLevel = before + 0.1f * (f - before);
        for (int back = 1; back < jumpSamples; ++back)
        {
            int idx = historyIndex - 1 - back;
            while (idx < 0) idx += jumpSamples;
            if (history[size_t (idx)] <= floorLevel) break;
            pendingLate = back;
        }
    }
    return hits;
}

} // namespace livemix
