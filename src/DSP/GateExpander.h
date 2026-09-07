#pragma once
#include "Processor.h"
#include "Biquad.h"
#include "Core/EnvelopeFollower.h"

namespace livemix
{

// Low-latency downward expander / gate for drums. No lookahead; detection uses
// a very fast peak follower so kick and snare transients open the gate within
// a fraction of a millisecond. Hysteresis and hold prevent chattering. An
// optional high-pass on the detector keeps kick bleed and rumble from opening
// tom and snare gates; the audio path itself is unfiltered.
class GateExpander : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        float thresholdDb = -40.0f;
        float rangeDb = 40.0f;      // maximum attenuation when closed (positive number)
        float attackMs = 0.5f;
        float holdMs = 50.0f;
        float releaseMs = 100.0f;
        float hysteresisDb = 3.0f;
        float ratio = 4.0f;         // expansion ratio below threshold (1 = off, >=20 = hard gate)
        float detectorHpfHz = 0.0f; // < 20 Hz = off
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;

    // For UI / tests: current gain reduction in dB (0 = open).
    float getGainReductionDb() const noexcept;
    bool isOpen() const noexcept { return open; }

private:
    Params params;
    double sr = 48000.0;
    EnvelopeFollower detector;
    Biquad detectorHpf;
    float detectorHpfHzApplied = -1.0f;
    bool detectorFiltered = false;
    float openThresholdLin = 0.01f, closeThresholdLin = 0.007f;
    float attackCoeff = 1.0f, releaseCoeff = 1.0f;
    int holdSamples = 0;
    int holdCounter = 0;
    bool open = false;
    float gain = 1.0f;
};

} // namespace livemix
