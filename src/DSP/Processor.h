#pragma once
#include "Core/AudioBlockView.h"

namespace livemix
{

// Common lifecycle for every DSP module.
//   prepare(): allocate/resize everything, may allocate.
//   process(): real-time safe. Never allocates, locks, or blocks.
//   reset():   clear state without reallocating.
class Processor
{
public:
    virtual ~Processor() = default;
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void process (AudioBlockView& block) noexcept = 0;
    virtual void reset() noexcept = 0;
};

} // namespace livemix
