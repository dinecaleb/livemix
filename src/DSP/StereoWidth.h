#pragma once
#include <atomic>
#include "Processor.h"
#include "Biquad.h"
#include "Core/Smoother.h"

namespace livemix
{

// Mid/side width control with a mono-below frequency (the side signal is
// high-passed so the low end stays centred). Also measures the L/R correlation
// of the output for the UI. Does nothing on mono streams. Zero latency.
class StereoWidth : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        float width = 1.0f;         // 0 = mono, 1 = unchanged, 2 = double side
        float monoBelowHz = 0.0f;   // < 20 = off
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;
    // The correlation meter costs a pass over the block; products without a WIDTH stage leave it off.
    void setMeteringEnabled (bool on) noexcept { metering = on; }

    // -1 .. 1 (1 = mono, 0 = uncorrelated, < 0 = out of phase). Any thread.
    float getCorrelation() const noexcept { return correlation.load (std::memory_order_relaxed); }

private:
    Params params;
    double sr = 48000.0;
    Smoother widthSmoother;
    Biquad sideHpf;
    float monoBelowApplied = -1.0f;
    bool sideFiltered = false;
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;
    float corrCoeff = 0.01f;
    bool metering = false;
    std::atomic<float> correlation { 1.0f };
};

} // namespace livemix
