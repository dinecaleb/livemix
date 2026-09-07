#pragma once
#include <array>
#include "DSP/Processor.h"
#include "DSP/Biquad.h"
#include "Core/Smoother.h"
#include "DelayLine.h"
#include "LFO.h"

namespace livemix
{

// Dine reverb engine: pre-delay, input filtering, early reflections and a
// modulated figure-of-eight plate tank (Dattorro, 1997) whose lengths scale
// with SIZE. One algorithm covers every reverb type (plate, hall, room,
// ambient); the type's character comes from its profile baseline. Output is
// 100 % wet at LEVEL; the FxChain does the dry/wet mix. Zero latency
// (pre-delay is part of the sound, not reported latency).
class ReverbAlgorithm : public Processor
{
public:
    struct Params
    {
        bool enabled = true;
        float decayS = 2.0f;        // RT60 of the tank's low/mid band
        float preDelayMs = 20.0f;
        float size = 50.0f;         // 0..100 %, 50 = reference plate dimensions
        float damping = 40.0f;      // 0..100 %: high-frequency loss per tank pass
        float diffusion = 70.0f;    // 0..100 %
        float lowCutHz = 120.0f;    // on the input to the tank and early reflections
        float highCutHz = 9000.0f;
        float modRateHz = 0.8f;
        float modDepth = 30.0f;     // 0..100 %
        float early = 40.0f;        // early-reflection level, 0..100 %
        float levelDb = 0.0f;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;   // replaces the block with the wet signal
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;
    const Params& getParams() const noexcept { return params; }
    float getTailSeconds() const noexcept { return params.decayS + params.preDelayMs * 0.001f + 0.2f; }

    // Size 0..100 % -> tank length multiplier (0.35 .. 1.6 x the reference plate).
    static float sizeScale (float sizePercent) noexcept { return 0.35f + 0.0125f * sizePercent; }
    // Tank feedback gain that gives the requested RT60 for a tank of this size at this sample rate.
    static float decayGainFor (float decayS, float sizePercent) noexcept;

private:
    static constexpr double kReferenceRate = 29761.0; // Dattorro's sample rate; every length is scaled from it
    static constexpr int kInputAllpasses = 4, kEarlyTaps = 6;

    // Allpass-interpolated reads keep the tank's magnitude response flat at fractional lengths
    // (linear interpolation inside a feedback loop would darken every pass).
    float allpass (DelayLine& line, float& interpState, float delaySamples, float gain, float x) noexcept;
    void updateCoefficients() noexcept;

    Params params;
    double sr = 48000.0;
    int channels = 1;
    float rateScale = 1.0f;         // sr / kReferenceRate
    bool wasEnabled = true;

    DelayLine pre;                                  // pre-delay + early reflection taps (per input channel)
    std::array<DelayLine, kInputAllpasses> inputAp;
    std::array<DelayLine, 2> tankModAp, tankDelay1, tankAp, tankDelay2;
    std::array<float, 2> dampState {}, tankFeedback {};
    std::array<float, kInputAllpasses> inputApState {};
    std::array<float, 2> modApState {}, delay1State {}, apState {}, delay2State {};
    std::array<LFO, 2> lfo;
    Biquad lowCut, highCut;
    float lowCutApplied = -1.0f, highCutApplied = -1.0f;

    Smoother size, decayGain, preDelaySamples, level, earlyLevel, dampCoeff, diffusionScale, modExcursion;
};

} // namespace livemix
