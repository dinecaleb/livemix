#pragma once
#include <array>
#include "DSP/Processor.h"
#include "DSP/Biquad.h"
#include "Core/Smoother.h"
#include "Core/EnvelopeFollower.h"
#include "DelayLine.h"
#include "LFO.h"
#include "TempoSync.h"

namespace livemix
{

// Dine delay engine: mono / stereo / ping-pong, free or tempo-synced time,
// feedback with low/high cut in the loop, width, modulation and ducking from
// the dry signal (the vocal "throw"). Time changes crossfade between two read
// heads, so automation and tempo changes never click or pitch-slur. Output is
// 100 % wet at LEVEL; the FxChain does the dry/wet mix.
class DelayAlgorithm : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        int mode = 1;                 // DelayMode
        bool sync = true;
        float timeMs = 375.0f;
        int division = 4;             // NoteDivision (sync)
        float offsetPercent = 0.0f;   // right channel time relative to left, -50..50 %
        float feedback = 30.0f;       // 0..95 %
        float lowCutHz = 150.0f;
        float highCutHz = 6000.0f;
        float width = 100.0f;         // 0..100 %
        float duck = 0.0f;            // 0..100 %
        float duckReleaseMs = 400.0f;
        float modRateHz = 0.5f;
        float modDepth = 0.0f;        // 0..100 % (= 0..5 ms)
        float levelDb = 0.0f;
    };

    static constexpr float kMaxTimeMs = 2000.0f;
    static constexpr float kMaxModMs = 5.0f;
    static constexpr float kDuckRangeDb = 30.0f;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;   // replaces the block with the wet signal
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;
    const Params& getParams() const noexcept { return params; }
    void setTempo (double bpm) noexcept;                       // audio thread, from the host play head

    float getTimeMs (int channel) const noexcept;              // effective time per channel (UI)
    float getDuckingDb() const noexcept { return duckingDb; }  // current ducking (UI)
    float getTailSeconds() const noexcept;
    double getTempo() const noexcept { return bpm; }

private:
    struct Head
    {
        float current = 480.0f, target = 480.0f, from = 480.0f;
        int fadePos = 0;
        bool fading = false;
    };

    void updateTimes() noexcept;
    float readHead (int channel, Head& h, float modSamples) noexcept;

    Params params;
    double sr = 48000.0;
    double bpm = 120.0;
    int channels = 1;
    int fadeSamples = 1920;
    bool wasEnabled = false;

    DelayLine line;
    std::array<Head, 2> heads;
    std::array<LFO, 2> lfo;
    std::array<Biquad, 1> lowCut, highCut; // per-channel state lives inside the biquads
    float lowCutApplied = -1.0f, highCutApplied = -1.0f;
    EnvelopeFollower duckEnv;
    Smoother feedback, level, width, modDepthSamples, duckAmount;
    float duckingDb = 0.0f;
};

} // namespace livemix
