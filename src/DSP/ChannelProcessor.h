#pragma once
#include "Processor.h"
#include "ChannelParameters.h"
#include "FilterProcessor.h"
#include "ParametricEQ.h"
#include "GateExpander.h"
#include "Compressor.h"
#include "TransientProcessor.h"
#include "Saturator.h"
#include "DeEsser.h"
#include "StereoWidth.h"
#include "Limiter.h"
#include "LoudnessMeter.h"
#include "LevelMeter.h"
#include "Core/Smoother.h"

namespace livemix
{

// The complete Dine channel chain, shared by Drums, Vocals, Keys and Master:
//   Input meter -> Trim -> Polarity -> HPF/LPF -> Gate -> Corrective EQ -> De-esser ->
//   Compressor -> Transient -> Tone EQ -> Saturation -> Width -> Output trim ->
//   [Limiter] -> [Loudness meter] -> Output meter
// Fixed order. Every stage is minimum-phase and sample-synchronous, so the chain
// reports zero latency; only a product that enables the limiter (Dine Master)
// reports the limiter's lookahead, and it does so constantly, on or off.
class ChannelProcessor : public Processor
{
public:
    struct Options
    {
        bool limiter = false;        // the limiter stage exists (latency is reported even when bypassed)
        bool loudnessMeter = false;  // run the BS.1770 meter on the output
        bool widthMeter = false;     // measure stereo correlation after the width stage
    };
    void configure (const Options& o) noexcept { options = o; }
    const Options& getOptions() const noexcept { return options; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    // Audio-thread safe: copies the snapshot into the modules.
    void setParameters (const ChannelParameters& p) noexcept;
    const ChannelParameters& getParameters() const noexcept { return params; }

    int getLatencySamples() const noexcept { return options.limiter ? limiter.getLatencySamples() : 0; }

    // Gain applied to the ORIGINAL signal during a loudness-matched A/B (dB). 0 when not matching.
    float getLoudnessMatchGainDb() const noexcept;

    const LevelMeter& getInputMeter() const noexcept { return inputMeter; }
    const LevelMeter& getOutputMeter() const noexcept { return outputMeter; }
    LevelMeter& getInputMeter() noexcept { return inputMeter; }
    LevelMeter& getOutputMeter() noexcept { return outputMeter; }
    const GateExpander& getGate() const noexcept { return gate; }
    const Compressor& getCompressor() const noexcept { return compressor; }
    const TransientProcessor& getTransient() const noexcept { return transient; }
    const DeEsser& getDeEsser() const noexcept { return deEsser; }
    const StereoWidth& getWidth() const noexcept { return width; }
    const Limiter& getLimiter() const noexcept { return limiter; }
    const LoudnessMeter& getLoudness() const noexcept { return loudness; }
    LoudnessMeter& getLoudness() noexcept { return loudness; }

private:
    Options options;
    ChannelParameters params;
    double sr = 48000.0;
    int channels = 1;

    LevelMeter inputMeter, outputMeter;
    Smoother inputGain, outputGain;
    FilterProcessor filters;
    GateExpander gate;
    ParametricEQ correctiveEq;
    Compressor compressor;
    TransientProcessor transient;
    ParametricEQ toneEq;
    Saturator saturator;
    DeEsser deEsser;
    StereoWidth width;
    Limiter limiter;
    LoudnessMeter loudness;

    // Long-term (≈2 s) mean-square tracking for loudness-matched A/B.
    float inputMeanSquare = 0.0f, outputMeanSquare = 0.0f, loudnessCoeff = 0.01f;
    Smoother matchGain;
};

} // namespace livemix
