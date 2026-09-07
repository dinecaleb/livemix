#pragma once
#include "Processor.h"
#include "Core/Smoother.h"
#include "Core/Constants.h"

namespace livemix
{

// Gentle tanh-style soft saturation with drive and mix. Runs without
// oversampling; drive is bounded so aliasing stays low on drum material.
class Saturator : public Processor
{
public:
    struct Params
    {
        bool enabled = false;
        float drive = 0.0f; // 0..1
        float mix = 1.0f;   // 0..1
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    void setParams (const Params& p) noexcept;

private:
    Params params;
    Smoother driveSmoother, mixSmoother;
};

} // namespace livemix
