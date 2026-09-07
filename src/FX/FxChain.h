#pragma once
#include <vector>
#include <array>
#include "DSP/Processor.h"
#include "DSP/LevelMeter.h"
#include "Core/Smoother.h"
#include "FxParameters.h"
#include "ReverbAlgorithm.h"
#include "DelayAlgorithm.h"

namespace livemix
{

// The complete Dine FX chain:
//   Input meter -> Trim -> [Delay] -> (+ delay-to-reverb) [Reverb] -> Mix -> Output trim -> Output meter
// Delay and reverb run in parallel on the trimmed dry signal; a share of the
// delay output can also feed the reverb (throws that bloom). Zero latency.
class FxChain : public Processor
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    // Audio-thread safe: copies the snapshot into the engines.
    void setParameters (const FxParameters& p) noexcept;
    const FxParameters& getParameters() const noexcept { return params; }
    void setTempo (double bpm) noexcept { delay.setTempo (bpm); }

    int getLatencySamples() const noexcept { return 0; }
    float getTailSeconds() const noexcept;
    float getLoudnessMatchGainDb() const noexcept;

    const LevelMeter& getInputMeter() const noexcept { return inputMeter; }
    const LevelMeter& getOutputMeter() const noexcept { return outputMeter; }
    LevelMeter& getInputMeter() noexcept { return inputMeter; }
    LevelMeter& getOutputMeter() noexcept { return outputMeter; }
    const ReverbAlgorithm& getReverb() const noexcept { return reverb; }
    const DelayAlgorithm& getDelay() const noexcept { return delay; }

private:
    FxParameters params;
    double sr = 48000.0;
    int channels = 1, maxBlock = 0;

    LevelMeter inputMeter, outputMeter;
    Smoother inputGain, outputGain, mix, toReverb;
    ReverbAlgorithm reverb;
    DelayAlgorithm delay;

    std::vector<float> dryStore, delayStore, reverbStore;
    std::array<float*, kMaxChannels> dryPtr {}, delayPtr {}, reverbPtr {};

    float inputMeanSquare = 0.0f, outputMeanSquare = 0.0f, loudnessCoeff = 0.01f;
    Smoother matchGain;
};

} // namespace livemix
