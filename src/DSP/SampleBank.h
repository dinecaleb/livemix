#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"

namespace livemix
{

// One drum sound for sample replacement: a few velocity layers, soft to loud, each with one
// or more round-robin hits. Every hit is mono, at the bank's sample rate, trimmed to its own
// onset and peak-normalised to 0 dBFS on load, so a bank plays the same whatever it came
// from. Built on the message thread (or in a test), read on the audio thread, never changed
// after it is published: the audio thread only ever holds a pointer to a bank that outlives
// it. src/ stays JUCE-free - decoding a file is the app's job (app/native/SampleLibrary).
struct SampleBank
{
    struct Layer
    {
        std::vector<std::vector<float>> hits;   // round-robins of the same velocity
    };

    std::string name;                   // "Tight kick" - what the SOUND list shows
    double sampleRate = 48000.0;
    float fundamentalHz = 0.0f;         // 0 = not measured (a tom sample records its pitch so it can follow the drum's)
    std::vector<Layer> layers;          // soft first, loud last

    bool empty() const noexcept { return layers.empty(); }

    // The hit for a velocity (0 = softest, 1 = loudest), cycling the layer's round-robins.
    // `counter` is the caller's: the player owns one per voice slot so a roll alternates.
    const std::vector<float>* pick (float velocity01, uint32_t& counter) const noexcept;
};

// Every sound the engine can play, by family and slot: what the app publishes and a strip
// reads when its `replaceSound` changes. Slot 0 is the profile's own pick for the family;
// an empty slot plays nothing. Owned by the app for the engine's lifetime; the engine holds
// a pointer and never frees anything.
struct SampleBankTable
{
    static constexpr int kSounds = 8;
    std::array<std::array<const SampleBank*, kSounds>, int (RoleFamily::Count)> banks {};

    const SampleBank* bank (RoleFamily family, int sound) const noexcept
    {
        if (int (family) < 0 || int (family) >= int (RoleFamily::Count)) return nullptr;
        if (sound < 0 || sound >= kSounds) return nullptr;
        return banks[size_t (family)][size_t (sound)];
    }
    void set (RoleFamily family, int sound, const SampleBank* b) noexcept
    {
        if (int (family) < 0 || int (family) >= int (RoleFamily::Count) || sound < 0 || sound >= kSounds) return;
        banks[size_t (family)][size_t (sound)] = b;
    }
};

// Which families the stage exists for. A room microphone is never gated and never replaced;
// hats and overheads carry the whole kit and are not one drum.
inline constexpr bool sampleReplacementAppropriate (RoleFamily f) noexcept
{
    return f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom;
}

// Prepares one decoded hit for a bank: drops the leading silence (everything before the first
// sample above `trimBelowDb`), fades the first few samples in so a trim can never click, and
// peak-normalises to 0 dBFS. A hit that never rises above the trim level comes back empty.
void prepareHit (std::vector<float>& hit, float trimBelowDb = -40.0f);

// Placeholder sounds, synthesised, so the stage works and is tested before a recorded bank
// exists: variant 0..2 per family (kick: tight / deep / soft-beater; snare: tight / fat /
// rimshot-bright; tom: high / mid / floor). Four velocity layers, two round-robins each.
SampleBank synthesizeBank (RoleFamily family, int variant, double sampleRate);

} // namespace livemix
