#include "DelayAlgorithm.h"
#include "FxParameters.h"
#include "Core/DbUtils.h"
#include "Core/FastMath.h"
#include <cmath>

namespace livemix
{

namespace
{
    inline float guard (float v) noexcept { return v > 4.0f ? 4.0f : (v < -4.0f ? -4.0f : v); }
}

void DelayAlgorithm::prepare (double sampleRate, int, int numChannels)
{
    sr = sampleRate;
    channels = numChannels < 1 ? 1 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);
    line.prepare (int ((kMaxTimeMs * 1.5f + kMaxModMs) * 0.001f * float (sr)) + 8, kMaxChannels);
    fadeSamples = int (0.04 * sr);
    for (auto& l : lfo) l.prepare (sr);
    lfo[1].setPhase (0.25f);
    duckEnv.prepare (sr);
    duckEnv.setAttackMs (5.0f);
    feedback.prepare (sr, 30.0f);
    level.prepare (sr, 20.0f);
    width.prepare (sr, 30.0f);
    modDepthSamples.prepare (sr, 50.0f);
    duckAmount.prepare (sr, 30.0f);
    lowCutApplied = highCutApplied = -1.0f;
    setParams (params);
    feedback.snapToTarget(); level.snapToTarget(); width.snapToTarget(); modDepthSamples.snapToTarget(); duckAmount.snapToTarget();
    for (auto& h : heads) { h.current = h.from = h.target; h.fading = false; }
    reset();
}

void DelayAlgorithm::reset() noexcept
{
    line.reset();
    for (auto& b : lowCut) b.reset();
    for (auto& b : highCut) b.reset();
    duckEnv.reset();
    duckingDb = 0.0f;
}

void DelayAlgorithm::setTempo (double newBpm) noexcept
{
    const double clamped = newBpm < 20.0 ? 20.0 : (newBpm > 300.0 ? 300.0 : newBpm);
    if (std::fabs (clamped - bpm) < 1.0e-6) return;
    bpm = clamped;
    if (params.sync) updateTimes();
}

float DelayAlgorithm::getTimeMs (int channel) const noexcept
{
    const float base = params.sync ? TempoSync::delayMs (NoteDivision (params.division), bpm) : params.timeMs;
    const bool offsetApplies = channel == 1 && params.mode != int (DelayMode::Mono);
    const float t = offsetApplies ? base * (1.0f + clamp (params.offsetPercent, -50.0f, 50.0f) * 0.01f) : base;
    return clamp (t, 1.0f, kMaxTimeMs * 1.5f);
}

float DelayAlgorithm::getTailSeconds() const noexcept
{
    // Echoes fall below -60 dB after log(0.001)/log(fb) repeats.
    const float fb = clamp (params.feedback, 0.0f, 95.0f) * 0.01f;
    const float repeats = fb <= 0.01f ? 1.0f : std::log (0.001f) / std::log (fb);
    return (repeats + 1.0f) * getTimeMs (1) * 0.001f;
}

void DelayAlgorithm::updateTimes() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& h = heads[size_t (ch)];
        const float wanted = getTimeMs (ch) * 0.001f * float (sr);
        if (std::fabs (wanted - h.target) < 1.0f) continue;
        // A new target while a fade is running: finish the fade instantly and start a fresh one.
        h.from = h.fading ? h.target : h.current;
        h.current = h.from;
        h.target = wanted;
        h.fadePos = 0;
        h.fading = true;
    }
}

void DelayAlgorithm::setParams (const Params& p) noexcept
{
    params = p;
    updateTimes();
    feedback.setTarget (clamp (params.feedback, 0.0f, 95.0f) * 0.01f);
    level.setTarget (params.enabled ? dbToGain (params.levelDb) : 0.0f);
    width.setTarget (clamp (params.width, 0.0f, 100.0f) * 0.01f);
    modDepthSamples.setTarget (clamp (params.modDepth, 0.0f, 100.0f) * 0.01f * kMaxModMs * 0.001f * float (sr));
    duckAmount.setTarget (clamp (params.duck, 0.0f, 100.0f) * 0.01f);
    duckEnv.setReleaseMs (clamp (params.duckReleaseMs, 20.0f, 5000.0f));
    const float rate = clamp (params.modRateHz, 0.05f, 10.0f);
    lfo[0].setRateHz (rate);
    lfo[1].setRateHz (rate);
    if (params.lowCutHz != lowCutApplied)
    {
        lowCutApplied = params.lowCutHz;
        lowCut[0].setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, clamp (params.lowCutHz, 10.0f, 2000.0f), 0.7071f, 0.0f));
    }
    if (params.highCutHz != highCutApplied)
    {
        highCutApplied = params.highCutHz;
        highCut[0].setCoefficients (BiquadCoefficients::make (FilterType::LowPass, sr, clamp (params.highCutHz, 500.0f, 20000.0f), 0.7071f, 0.0f));
    }
}

inline float DelayAlgorithm::readHead (int channel, Head& h, float modSamples) noexcept
{
    if (! h.fading)
        return line.readFractional (channel, h.current + modSamples);
    const float t = float (h.fadePos) / float (fadeSamples);
    const float a = line.readFractional (channel, h.from + modSamples);
    const float b = line.readFractional (channel, h.target + modSamples);
    if (++h.fadePos >= fadeSamples) { h.fading = false; h.current = h.target; }
    return a + t * (b - a);
}

void DelayAlgorithm::process (AudioBlockView& block) noexcept
{
    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
    if (numCh <= 0) return;

    if (! params.enabled)
    {
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i) block.channels[ch][i] = 0.0f;
        wasEnabled = false;
        duckingDb = 0.0f;
        return;
    }
    if (! wasEnabled) { reset(); wasEnabled = true; }

    float* outL = block.channels[0];
    float* outR = numCh > 1 ? block.channels[1] : nullptr;
    const auto mode = DelayMode (params.mode);
    float lastDuckDb = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float inL = outL[i];
        const float inR = outR != nullptr ? outR[i] : inL;
        const float mono = 0.5f * (inL + inR);

        const float fb = feedback.next();
        const float lvl = level.next();
        const float w = width.next();
        const float md = modDepthSamples.next();
        const float duck = duckAmount.next();

        const float modL = md * lfo[0].next();
        const float modR = md * lfo[1].next();

        // Read both taps, filter them in the loop.
        float y0 = readHead (0, heads[0], modL);
        float y1 = readHead (1, heads[1], modR);
        y0 = highCut[0].processSample (0, lowCut[0].processSample (0, y0));
        y1 = highCut[0].processSample (1, lowCut[0].processSample (1, y1));
        y0 = flushDenormal (y0);
        y1 = flushDenormal (y1);

        // Feed the line. The loop gain is < 1 by construction (feedback <= 95 %, unity-gain filters);
        // the clamp only guards against non-finite input ever circulating.
        switch (mode)
        {
            case DelayMode::Mono:
                line.write (0, mono + fb * guard (y0));
                line.write (1, mono + fb * guard (y0));
                y1 = y0;
                break;
            case DelayMode::PingPong:
                line.write (0, mono + fb * guard (y1));
                line.write (1, guard (y0));
                break;
            case DelayMode::Stereo:
            case DelayMode::Count:
            default:
                line.write (0, inL + fb * guard (y0));
                line.write (1, inR + fb * guard (y1));
                break;
        }
        line.advance();

        // Ducking from the dry signal: up to 30 dB of reduction while the source is present.
        float duckGain = 1.0f;
        if (duck > 0.0f)
        {
            const float env = duckEnv.process (std::fabs (mono));
            const float envDb = fastmath::gainToDb (env);
            const float presence = clamp ((envDb + 50.0f) / 30.0f, 0.0f, 1.0f);
            lastDuckDb = -kDuckRangeDb * duck * presence;
            duckGain = fastmath::dbToGain (lastDuckDb);
        }
        else
        {
            duckEnv.process (std::fabs (mono));
            lastDuckDb = 0.0f;
        }

        const float gain = lvl * duckGain;
        if (outR != nullptr)
        {
            const float mid = 0.5f * (y0 + y1), side = 0.5f * (y0 - y1) * w;
            outL[i] = gain * (mid + side);
            outR[i] = gain * (mid - side);
        }
        else
            outL[i] = gain * 0.5f * (y0 + y1);
    }
    duckingDb = lastDuckDb;
}

} // namespace livemix
