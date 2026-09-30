#pragma once
#include <atomic>
#include "Biquad.h"
#include "Core/EnvelopeFollower.h"

namespace livemix
{

// Finds the hits on a close drum microphone, with no lookahead, from a copy of the signal:
// a band filter (the gate's detector high-pass idea, plus a ceiling) so a snare does not
// fire the kick and a kick does not fire the snare bottom; a fast peak follower; and a hit
// the moment the follower is above the threshold *and* has jumped by `riseDb` in the last
// two milliseconds - a stroke jumps, a ring or a tail does not, and bleed sits under the
// threshold TUNE places above it. After a hit the detector is masked; inside the mask only
// a louder onset (by `retriggerDb`) counts, so a flam re-triggers and a ring does not, and
// after the mask it re-arms once the hit has fallen 6 dB from its peak. The hit is reported
// 1.5 ms after it was recognised, with the loudest level the detector saw in that time as
// its velocity: that is the only delay a sample ever has, and nothing delays the microphone.
class SampleTrigger
{
public:
    struct Params
    {
        bool enabled = false;
        float thresholdDb = -30.0f;     // absolute, dBFS at the detector
        float riseDb = 6.0f;            // the jump, in two milliseconds, that makes a hit
        float hpfHz = 40.0f;            // detector band
        float lpfHz = 8000.0f;
        float maskMs = 40.0f;           // no second hit inside this ...
        float retriggerDb = 6.0f;       // ... unless it is this much louder than the one that opened the mask
        float velocityRangeDb = 18.0f;  // threshold .. threshold + range maps onto velocity 0 .. 1
        float confidenceDb = 3.0f;      // a hit less than this over the threshold plays quietly
    };

    struct Hit
    {
        int offset = 0;                 // sample index inside the block
        float levelDb = -120.0f;        // the detector's peak in the 1.5 ms after the crossing
        float velocity = 0.0f;          // 0 .. 1
        float confidence = 1.0f;        // 0 .. 1, a gain: doubtful triggers are quiet, not absent
        // How far `offset` is behind the drum's own onset, in samples: the 1.5 ms the hit is
        // measured for, plus the time the drum took to rise to the crossing. The player starts
        // the sample this far into itself, so its attack lands where the microphone's did.
        int lateBy = 0;
    };
    static constexpr int kMaxHits = 16;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setParams (const Params& p) noexcept;

    // Reads `n` mono samples; writes up to `maxHits` hits; returns how many. Allocation-free.
    int process (const float* mono, int n, Hit* out, int maxHits) noexcept;

    // How many samples after the crossing a hit is reported (its level is measured in that
    // time). The gate that a sample's hit opens is opened this much before the report, so
    // the microphone's own attack is never cut under the sample.
    int reportDelaySamples() const noexcept { return measureSamples; }

    // For the UI (any thread): how many hits since prepare, and the last one's level.
    int getHitCount() const noexcept { return hitCount.load (std::memory_order_relaxed); }
    float getLastLevelDb() const noexcept { return lastLevelDb.load (std::memory_order_relaxed); }

private:
    Params params;
    double sr = 48000.0;
    Biquad hpf, lpf;
    EnvelopeFollower fast;
    static constexpr int kMaxJumpSamples = 512;    // two milliseconds at up to 256 kHz
    float history[kMaxJumpSamples] {};             // the follower, two milliseconds back
    int historyIndex = 0, jumpSamples = 96;
    int pendingLate = 0;                           // the hit being measured: how far behind its onset it already was
    int filterDelaySamples = 0;                    // the detector's low-pass group delay
    float thresholdLin = 0.03f, riseLin = 2.0f, retriggerLin = 2.0f;
    int maskSamples = 1920, maskLeft = 0, maskAge = 0, measureSamples = 72, maskMeasureSamples = 480;
    float maskPeak = 0.0f;
    bool armed = true;
    int pendingLeft = 0;               // samples until a recognised hit is reported, with its level measured
    float pendingPeak = 0.0f;
    std::atomic<int> hitCount { 0 };
    std::atomic<float> lastLevelDb { -120.0f };
};

} // namespace livemix
