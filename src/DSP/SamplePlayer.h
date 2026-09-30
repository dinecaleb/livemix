#pragma once
#include <atomic>
#include <cstdint>
#include "SampleBank.h"
#include "Core/AudioBlockView.h"

namespace livemix
{

// Plays a bank's hits: a fixed set of voices, each cubic-interpolated at its own rate
// (varispeed, so a tom sample can sit on the drum's pitch and a bank at another sample
// rate plays true), starting at a sample index inside the block, summed into every channel
// of the strip. When every voice is busy the one furthest into its hit is taken - by then a
// drum sample is a quiet tail, so the cut is not heard. Allocation-free after prepare();
// the bank pointer is read atomically and the bank itself is never touched for writing.
class SamplePlayer
{
public:
    static constexpr int kVoices = 6;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setBank (const SampleBank* b) noexcept { bank.store (b, std::memory_order_release); }
    const SampleBank* getBank() const noexcept { return bank.load (std::memory_order_acquire); }

    // Start a hit `startOffset` samples from the beginning of the next render() call, at a
    // linear gain, `rateMul` times the bank's own rate (1 = as recorded at this sample rate).
    // `skipSamples`: how far into the hit to start (output samples), so a hit recognised late is
    // not played late (SampleTrigger::Hit::lateBy). The skipped start is faded in over 0.3 ms.
    void trigger (int startOffset, float velocity01, float gainLin, double rateMul, int skipSamples = 0) noexcept;

    // Adds the voices to every channel, scaled by `gain` (the blend, with polarity in its sign).
    void render (AudioBlockView& block, float gain) noexcept;

    bool isPlaying() const noexcept;

private:
    struct Voice
    {
        const std::vector<float>* data = nullptr;
        double pos = 0.0, rate = 1.0;
        float gain = 0.0f;
        int delay = 0;                  // samples to wait before the first output sample
        int fadeLeft = 0, fadeLength = 0;   // a start taken mid-attack is faded in, never a click
        bool on = false;
    };
    Voice voices[kVoices];
    uint32_t roundRobin = 0;
    double sr = 48000.0;
    std::atomic<const SampleBank*> bank { nullptr };
};

} // namespace livemix
