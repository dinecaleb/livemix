#pragma once
#include <array>
#include "DSP/ChannelParameters.h"
#include "Core/ProductDefinition.h"

namespace livemix
{

// One UI-rate sample of what the audio engine is doing. Filled by the editor
// timer from the meters' atomics; never touched by the audio thread.
struct LiveSample
{
    float inDb = -120.0f;
    float outDb = -120.0f;
    bool gateOpen = false;
    float compGrDb = 0.0f;
};

class LevelHistory
{
public:
    static constexpr int kCapacity = 720; // 12 s at 60 Hz

    void push (const LiveSample& s) noexcept
    {
        ring[size_t (head)] = s;
        head = (head + 1) % kCapacity;
        if (count < kCapacity) ++count;
    }
    void clear() noexcept { head = 0; count = 0; }
    int size() const noexcept { return count; }
    // 0 = newest
    const LiveSample& fromNewest (int i) const noexcept
    {
        return ring[size_t ((head - 1 - i + kCapacity * 2) % kCapacity)];
    }

private:
    std::array<LiveSample, kCapacity> ring {};
    int head = 0, count = 0;
};

struct LiveState
{
    LevelHistory history;
    float inPeakDb = -120.0f;      // decaying visual peak
    float inHoldDb = -120.0f;      // 2 s peak hold
    float inRecentMaxDb = -120.0f; // ~3 s window, drives the health chip
    float outPeakDb = -120.0f;
    float outHoldDb = -120.0f;
    bool inClipped = false;
    bool outClipped = false;
    bool gateOpen = false;
    float gateEnvelope = 0.0f;     // 0..1 visual open/close envelope
    float gateGrDb = 0.0f;
    float compGrDb = 0.0f;         // positive dB of reduction
    float abMatchDb = 0.0f;
    float activity = 0.0f;         // 0..1 input level envelope for the chain strips
    float deEssGrDb = 0.0f;        // positive dB of S reduction
    float limiterGrDb = 0.0f;      // positive dB of limiting
    float correlation = 1.0f;      // stereo correlation after the width stage (-1..1)
    float momentaryLufs = -120.0f, shortTermLufs = -120.0f, integratedLufs = -120.0f;
    float truePeakDb = -120.0f, truePeakHoldDb = -120.0f;
    float targetLufs = -120.0f;    // profile target for this output (-120 = none)
    ChannelParameters params;
    double sampleRate = 48000.0;
    const ProductDefinition* product = nullptr;
};

} // namespace livemix
