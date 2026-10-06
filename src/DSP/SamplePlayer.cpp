#include "SamplePlayer.h"
#include <algorithm>
#include <cmath>

namespace livemix
{

void SamplePlayer::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    reset();
}

void SamplePlayer::reset() noexcept
{
    for (auto& v : voices) v = Voice {};
    for (auto& v : tails) v = Voice {};
    roundRobin = 0;
}

bool SamplePlayer::isPlaying() const noexcept
{
    for (const auto& v : voices) if (v.on) return true;
    return false;
}

void SamplePlayer::trigger (int startOffset, float velocity01, float gainLin, double rateMul, int skipSamples) noexcept
{
    const SampleBank* b = bank.load (std::memory_order_acquire);
    if (b == nullptr) return;
    const std::vector<float>* hit = b->pick (velocity01, roundRobin);
    if (hit == nullptr || hit->size() < 4) return;

    // A free voice, else the one furthest into its hit.
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.on) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& v : voices) if (v.pos > slot->pos) slot = &v;
        // Its tail goes out over 2 ms instead of stopping dead (the scope's own rule).
        Voice* tail = &tails[0];
        for (auto& t : tails) { if (! t.on) { tail = &t; break; } if (t.releaseLeft < tail->releaseLeft) tail = &t; }
        *tail = *slot;
        tail->releaseLength = tail->releaseLeft = std::max (1, int (0.002 * sr));
    }
    slot->data = hit;
    slot->pos = 1.0;                    // cubic interpolation needs one sample of run-up (prepareHit fades it in)
    slot->rate = rateMul * (b->sampleRate / sr);
    if (slot->rate < 0.25) slot->rate = 0.25;
    if (slot->rate > 4.0) slot->rate = 4.0;
    slot->gain = gainLin;
    slot->delay = startOffset < 0 ? 0 : startOffset;
    // Late by `skipSamples`: start that far in, so the attack lines up with the microphone's.
    // Never past the first quarter of the hit - a recognition that late is not worth chasing.
    // And never past the hit's own peak, less half a millisecond: found in the first 20 ms, a
    // bounded scan (a drum's attack peaks within a few ms of the trimmed onset).
    double skip = 0.0;
    if (skipSamples > 0)
    {
        skip = std::min (double (skipSamples) * slot->rate, double (hit->size()) * 0.25);
        const auto& d = *hit;
        const int scan = std::min (int (d.size()) - 3, int (0.02 * b->sampleRate));
        int peak = 1;
        float best = 0.0f;
        for (int k = 1; k < scan; ++k)
            if (std::abs (d[size_t (k)]) > best) { best = std::abs (d[size_t (k)]); peak = k; }
        skip = std::min (skip, std::max (0.0, double (peak - 1) - 0.0005 * b->sampleRate));
    }
    if (skip > 0.0)
    {
        slot->pos += skip;
        slot->fadeLength = slot->fadeLeft = std::max (1, int (0.0003 * sr));
    }
    else slot->fadeLength = slot->fadeLeft = 0;
    slot->releaseLeft = slot->releaseLength = 0;
    slot->on = true;
}

void SamplePlayer::render (AudioBlockView& block, float gain) noexcept
{
    for (auto& v : voices) if (v.on) renderVoice (v, block, gain);
    for (auto& v : tails) if (v.on) renderVoice (v, block, gain);
}

void SamplePlayer::renderVoice (Voice& v, AudioBlockView& block, float gain) noexcept
{
    const int n = block.numSamples;
    const auto& d = *v.data;
    const int length = int (d.size());
    const float g = v.gain * gain;
    int i = 0;
    if (v.delay >= n) { v.delay -= n; return; }
    i = v.delay;
    v.delay = 0;
    for (; i < n; ++i)
    {
        const int i1 = int (v.pos);
        if (i1 + 2 >= length) { v.on = false; break; }
        const float t = float (v.pos - double (i1));
        const float y0 = d[size_t (i1 - 1)], y1 = d[size_t (i1)], y2 = d[size_t (i1 + 1)], y3 = d[size_t (i1 + 2)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        float s = g * (((c3 * t + c2) * t + c1) * t + y1);
        if (v.fadeLeft > 0) { s *= 1.0f - float (v.fadeLeft) / float (v.fadeLength + 1); --v.fadeLeft; }
        if (v.releaseLength > 0)
        {
            if (v.releaseLeft <= 0) { v.on = false; break; }
            s *= float (v.releaseLeft) / float (v.releaseLength + 1);
            --v.releaseLeft;
        }
        for (int ch = 0; ch < block.numChannels; ++ch) block.channels[ch][i] += s;
        v.pos += v.rate;
    }
}

} // namespace livemix
