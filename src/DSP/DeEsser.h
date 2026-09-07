#pragma once
#include "Processor.h"
#include "Biquad.h"
#include "Core/EnvelopeFollower.h"
#include "Core/Smoother.h"

namespace livemix
{

// Split-band de-esser. The signal is divided at `freqHz` with a 4th-order
// Linkwitz-Riley pair (low + high sums to an allpass, so the tone is unchanged
// when nothing is reduced); the high band is turned down when its level exceeds
// the threshold, by at most `rangeDb`. Fast attack, ~40 ms release. Zero latency.
class DeEsser : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        float freqHz = 6500.0f;
        float thresholdDb = -30.0f;
        float rangeDb = 6.0f;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;
    float getGainReductionDb() const noexcept { return -reductionDb; } // <= 0

private:
    Params params;
    double sr = 48000.0;
    float freqApplied = -1.0f;
    Biquad lowA, lowB, highA, highB;   // Linkwitz-Riley 4th order: two cascaded Butterworth stages per band; low + high is allpass
    Biquad detectorHpf;
    EnvelopeFollower detector;
    float attackCoeff = 1.0f, releaseCoeff = 1.0f;
    float reductionDb = 0.0f;          // positive = reducing
};

} // namespace livemix
