#pragma once
#include <array>
#include <atomic>
#include <vector>
#include "Processor.h"
#include "Core/Smoother.h"
#include "Core/Constants.h"

namespace livemix
{

// Lookahead brickwall limiter. The audio is delayed by the lookahead while the
// gain is computed from a sliding minimum over the same window, so the gain is
// already down when the peak arrives. Attack smoothing spans the lookahead;
// release is a parameter. Linked across channels. A hard clip at the ceiling is
// the final safety net. Latency = lookahead samples (reported by the owner).
//
// The ceiling is a TRUE peak. What a converter, and every lossy codec on the way
// to a listener, actually reconstructs between two samples can sit well above the
// larger of the two - a full-scale signal near Nyquist by up to about 3 dB - so a
// limiter that only reads the samples lets overs through a ceiling it reports as
// safe. The detector here runs 4x oversampled (a 48-tap polyphase interpolator,
// 12 taps per phase) and takes the largest of the four reconstructed points, which
// is what "dBTP" means and what the readouts have always claimed.
class Limiter : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        float ceilingDb = -1.0f;
        float releaseMs = 120.0f;
    };

    static constexpr float kLookaheadMs = 1.5f;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;
    // A/B ORIGINAL: keep the lookahead delay (constant latency) but apply no gain.
    void processDelayOnly (AudioBlockView& block) noexcept;

    int getLatencySamples() const noexcept { return lookahead; }
    // Current gain reduction in dB (<= 0). Any thread.
    float getGainReductionDb() const noexcept { return reduction.load (std::memory_order_relaxed); }

private:
    Params params;
    double sr = 48000.0;
    int channels = 1;
    int lookahead = 0;
    float ceilingLin = 0.891f;
    float attackCoeff = 1.0f, releaseCoeff = 1.0f;
    float gain = 1.0f;

    // Delay lines (allocated in prepare) and the sliding-minimum deque of required gains.
    std::array<std::vector<float>, kMaxChannels> delay {};
    int delayPos = 0;
    std::vector<float> reqGain;      // ring of required gains, one per delayed sample
    std::vector<int> dequeIdx;       // monotonic deque of indices into reqGain (ring)
    int dequeHead = 0, dequeTail = 0; // [head, tail)
    int writeIdx = 0;                // next index in reqGain
    std::atomic<float> reduction { 0.0f };

    // ---- the true-peak detector ----
    // A 4x polyphase interpolator over the last kTpTaps input samples per channel. Each phase
    // sums to 1, so a steady value reads back as itself and the estimate is never under the
    // sample peak it contains. Everything is sized in prepare(); process() only multiplies.
    static constexpr int kTpPhases = 4;
    static constexpr int kTpTaps = 12;                      // per phase; 48 taps of prototype
    std::array<float, size_t (kTpPhases * kTpTaps)> tpCoeff {};
    std::array<std::array<float, size_t (kTpTaps)>, kMaxChannels> tpHistory {};
    int tpPos = 0;                                          // newest slot in the history ring
    void tpPush (const AudioBlockView& block, int numCh, int i) noexcept;
    float truePeakOf (int numCh) const noexcept;
};

} // namespace livemix
