#pragma once
#include <atomic>
#include <array>
#include "Processor.h"
#include "Core/Constants.h"

namespace livemix
{

// Peak / RMS meter. Audio thread writes atomics; UI reads them.
// Used for both the InputMeter and OutputMeter roles.
class LevelMeter : public Processor
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (AudioBlockView& block) noexcept override;
    void reset() noexcept override;

    // Readers (any thread). Peak is the highest absolute sample of the last block.
    float getPeakDb (int channel) const noexcept;
    float getRmsDb (int channel) const noexcept;
    float getMaxPeakDb() const noexcept; // across channels
    float getMaxRmsDb() const noexcept;
    // The highest absolute sample across channels since the previous call (single UI reader).
    // A UI timer only sees one block in many; this is what a meter or health chip must read,
    // otherwise short peaks between timer ticks are missed and the input reads too low.
    float consumeMaxPeakDb() const noexcept; // const: the accumulator is a reader-owned atomic, the editors hold const refs
    bool hasClipped() const noexcept { return clipped.load (std::memory_order_relaxed); }
    void clearClip() noexcept { clipped.store (false, std::memory_order_relaxed); }
    int getNumChannels() const noexcept { return channels; }

private:
    int channels = 1;
    float rmsCoeff = 0.01f;
    std::array<float, kMaxChannels> meanSquare {};
    std::array<std::atomic<float>, kMaxChannels> peak {};
    mutable std::atomic<float> peakSinceRead { 0.0f };
    std::array<std::atomic<float>, kMaxChannels> rms {};
    std::atomic<bool> clipped { false };
};

} // namespace livemix
