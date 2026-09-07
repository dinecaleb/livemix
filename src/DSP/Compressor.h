#pragma once
#include "Processor.h"
#include "Biquad.h"
#include "Core/Smoother.h"
#include "Core/EnvelopeFollower.h"

namespace livemix
{

// Feed-forward compressor with log-domain gain smoothing (Giannoulis et al.),
// soft knee, makeup and dry/wet mix. Zero latency, no lookahead. An optional
// high-pass on the detector stops sub energy from driving the gain (kick,
// drum bus); the audio path stays full-band.
class Compressor : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        float thresholdDb = -20.0f;
        float ratio = 4.0f;
        float attackMs = 10.0f;
        float releaseMs = 100.0f;
        float kneeDb = 6.0f;
        float makeupDb = 0.0f;
        float mix = 1.0f;           // 0 = dry, 1 = wet
        float detectorHpfHz = 0.0f; // < 20 Hz = off
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;

    float getGainReductionDb() const noexcept { return -smoothedReductionDb; }

    // Static gain computer, exposed for tests: returns output level in dB for an input level in dB.
    static float computeGain (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;

private:
    Params params;
    double sr = 48000.0;
    float attackCoeff = 1.0f, releaseCoeff = 1.0f;
    float smoothedReductionDb = 0.0f; // positive = reducing
    Smoother makeupGain, mixSmoother;
    EnvelopeFollower preDetector; // instant attack, ~1.5 ms release: removes waveform ripple from the control signal
    Biquad detectorHpf;
    float detectorHpfHzApplied = -1.0f;
    bool detectorFiltered = false;
};

} // namespace livemix
