#include "Limiter.h"
#include "Core/DbUtils.h"
#include "Core/EnvelopeFollower.h"
#include <cmath>
#include <algorithm>

namespace livemix
{

namespace
{
    // The 4x interpolation prototype: a windowed sinc cut at the original Nyquist, split into
    // four phases. Each phase is normalised to sum to 1 so a steady value reconstructs as
    // itself - which is what makes the estimate an upper bound on the samples it is built from
    // rather than a filter's idea of them.
    void buildTruePeakPhases (float* coeff, int phases, int tapsPerPhase)
    {
        const int n = phases * tapsPerPhase;
        const double centre = 0.5 * (n - 1);
        for (int p = 0; p < phases; ++p)
        {
            double sum = 0.0;
            for (int t = 0; t < tapsPerPhase; ++t)
            {
                const int k = t * phases + p;                       // this phase's taps, strided
                const double x = (double (k) - centre) / double (phases);
                const double sinc = std::fabs (x) < 1.0e-9 ? 1.0 : std::sin (M_PI * x) / (M_PI * x);
                const double window = 0.54 - 0.46 * std::cos (2.0 * M_PI * double (k) / double (n - 1));
                const double h = sinc * window;
                coeff[size_t (p * tapsPerPhase + t)] = float (h);
                sum += h;
            }
            if (std::fabs (sum) > 1.0e-9)
                for (int t = 0; t < tapsPerPhase; ++t) coeff[size_t (p * tapsPerPhase + t)] /= float (sum);
        }
    }
}

// The newest `kTpTaps` input samples per channel, newest at tpPos.
void Limiter::tpPush (const AudioBlockView& block, int numCh, int i) noexcept
{
    tpPos = (tpPos + 1) % kTpTaps;
    for (int ch = 0; ch < numCh; ++ch) tpHistory[size_t (ch)][size_t (tpPos)] = block.channels[ch][i];
}

// The largest of the four points the reconstruction filter puts between the samples, across
// every channel: the peak a converter or a codec will actually produce.
float Limiter::truePeakOf (int numCh) const noexcept
{
    float peak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto& h = tpHistory[size_t (ch)];
        for (int p = 0; p < kTpPhases; ++p)
        {
            const float* c = tpCoeff.data() + p * kTpTaps;
            float acc = 0.0f;
            int idx = tpPos;
            for (int t = 0; t < kTpTaps; ++t)
            {
                acc += c[t] * h[size_t (idx)];
                idx = idx == 0 ? kTpTaps - 1 : idx - 1;
            }
            const float a = std::fabs (acc);
            if (a > peak) peak = a;
        }
    }
    return peak;
}

void Limiter::prepare (double sampleRate, int, int numChannels)
{
    sr = sampleRate;
    channels = numChannels < kMaxChannels ? numChannels : kMaxChannels;
    buildTruePeakPhases (tpCoeff.data(), kTpPhases, kTpTaps);
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
    for (auto& h : tpHistory) h.fill (0.0f);
    tpPos = 0;
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
        tpPush (block, numCh, i);
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
        // Required gain for the incoming sample (the one that will leave in L samples), read
        // from what the reconstruction puts between the samples rather than from the samples.
        // The detector's own group delay is about six input samples, and the window below is
        // seventy-two long, so the gain is still down well before the peak reaches the output.
        tpPush (block, numCh, i);
        const float peak = truePeakOf (numCh);
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
