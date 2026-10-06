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
// of the strip. When every voice is busy the one furthest into its hit is taken, and its tail
// is faded out over 2 ms rather than cut, so a fast roll never clicks. Allocation-free after prepare();
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
    // not played late (SampleTrigger::Hit::lateBy). Never past the hit's own peak: the first
    // millisecond or two IS the drum (a snare's crack, a kick's beater), and skipping it is what
    // made a triggered sample duller and softer than the same sample on HEAR IT. The skipped
    // start is faded in over 0.3 ms.
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
        int releaseLeft = 0, releaseLength = 0;   // a stolen voice, fading out (tails only)
        bool on = false;
    };
    static void renderVoice (Voice&, AudioBlockView&, float gain) noexcept;
    Voice voices[kVoices];
    Voice tails[kVoices];               // stolen voices on their way out, 2 ms each
    uint32_t roundRobin = 0;
    double sr = 48000.0;
    std::atomic<const SampleBank*> bank { nullptr };
};

} // namespace livemix
