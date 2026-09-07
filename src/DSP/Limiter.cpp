#include "Limiter.h"
#include "Core/DbUtils.h"
#include "Core/EnvelopeFollower.h"
#include <cmath>
#include <algorithm>

namespace livemix
{

void Limiter::prepare (double sampleRate, int, int numChannels)
{
    sr = sampleRate;
    channels = numChannels < kMaxChannels ? numChannels : kMaxChannels;
    lookahead = std::max (1, int (std::lround (kLookaheadMs * 0.001 * sr)));
    for (auto& d : delay) d.assign (size_t (lookahead), 0.0f);
    reqGain.assign (size_t (lookahead) + 1, 1.0f); // window = the delayed sample plus everything after it
    dequeIdx.assign (size_t (lookahead) + 2, 0);
    attackCoeff = EnvelopeFollower::coefficientFor (kLookaheadMs * 0.35f, sr);
    setParams (params);
    reset();
}

void Limiter::reset() noexcept
{
    for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
    std::fill (reqGain.begin(), reqGain.end(), 1.0f);
    delayPos = 0;
    dequeHead = dequeTail = 0;
    writeIdx = 0;
    gain = 1.0f;
    reduction.store (0.0f, std::memory_order_relaxed);
}

void Limiter::setParams (const Params& p) noexcept
{
    params = p;
    ceilingLin = dbToGain (clamp (p.ceilingDb, -12.0f, 0.0f));
    releaseCoeff = EnvelopeFollower::coefficientFor (clamp (p.releaseMs, 10.0f, 1000.0f), sr);
}

void Limiter::processDelayOnly (AudioBlockView& block) noexcept
{
    const int n = block.numSamples;
    const int numCh = block.numChannels < channels ? block.numChannels : channels;
    if (lookahead <= 0 || numCh <= 0) return;
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            float& slot = delay[size_t (ch)][size_t (delayPos)];
            const float delayed = slot;
            slot = block.channels[ch][i];
            block.channels[ch][i] = delayed;
        }
        delayPos = (delayPos + 1) % lookahead;
    }
    gain = 1.0f;
    reduction.store (0.0f, std::memory_order_relaxed);
}

void Limiter::process (AudioBlockView& block) noexcept
{
    const int n = block.numSamples;
    const int numCh = block.numChannels < channels ? block.numChannels : channels;
    if (lookahead <= 0 || numCh <= 0) return;
    const int L = lookahead;
    const int W = L + 1;                 // sliding-window length in required gains
    const int cap = int (dequeIdx.size());

    for (int i = 0; i < n; ++i)
    {
        // Required gain for the incoming sample (the one that will leave in L samples).
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) peak = std::max (peak, std::fabs (block.channels[ch][i]));
        const float req = params.enabled && peak > ceilingLin ? ceilingLin / peak : 1.0f;

        // Sliding minimum over the last W required gains (monotonic deque, no allocation).
        // Drop the entry that leaves the window.
        const int leaving = writeIdx; // reqGain[writeIdx] is W samples old and about to be overwritten
        if (dequeHead != dequeTail && dequeIdx[size_t (dequeHead)] == leaving) dequeHead = (dequeHead + 1) % cap;
        reqGain[size_t (writeIdx)] = req;
        while (dequeHead != dequeTail)
        {
            const int last = (dequeTail - 1 + cap) % cap;
            if (reqGain[size_t (dequeIdx[size_t (last)])] >= req) dequeTail = last; else break;
        }
        dequeIdx[size_t (dequeTail)] = writeIdx;
        dequeTail = (dequeTail + 1) % cap;
        writeIdx = (writeIdx + 1) % W;
        const float target = reqGain[size_t (dequeIdx[size_t (dequeHead)])];

        // Attack over the lookahead, release as set. The window minimum already covers the
        // sample about to leave, so clamping to it guarantees the ceiling; the smoothing only
        // shapes how the gain gets there.
        const float c = target < gain ? attackCoeff : releaseCoeff;
        gain += c * (target - gain);
        if (gain > target) gain = target;

        // Swap the delayed sample out, apply the gain and the safety clip.
        for (int ch = 0; ch < numCh; ++ch)
        {
            float& slot = delay[size_t (ch)][size_t (delayPos)];
            const float delayed = slot;
            slot = block.channels[ch][i];
            float y = delayed * gain;
            if (params.enabled) y = clamp (y, -ceilingLin, ceilingLin);
            block.channels[ch][i] = y;
        }
        delayPos = (delayPos + 1) % L;
    }
    reduction.store (gainToDb (gain), std::memory_order_relaxed);
}

} // namespace livemix
