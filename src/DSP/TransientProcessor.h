#pragma once
#include "Processor.h"
#include "Core/EnvelopeFollower.h"
#include "Core/Smoother.h"

namespace livemix
{

// Differential-envelope transient shaper. Attack compares a fast and a slow
// attack follower; sustain compares a fast and a slow release follower.
// Level-independent, zero latency.
class TransientProcessor : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        float attack = 0.0f;   // -1 .. +1
        float sustain = 0.0f;  // -1 .. +1
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;

    float getCurrentGainDb() const noexcept { return lastGainDb; }

private:
    Params params;
    double sr = 48000.0;
    EnvelopeFollower attackFast, attackSlow, sustainFast, sustainSlow;
    Smoother gainSmoother;
    float lastGainDb = 0.0f;
};

} // namespace livemix
