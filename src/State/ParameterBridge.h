#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/ChannelParameters.h"
#include "Profiles/MacroMapping.h"
#include "Core/ChannelRole.h"
#include "Core/ProductDefinition.h"
#include "Core/StyleId.h"

namespace livemix
{

// Connects the engine's ChannelParameters to the host parameter tree of one
// Dine channel product. read() is audio-thread safe (cached atomics, no
// lookups); the write functions are message-thread only and go through the
// normal host notification path so automation, undo and the UI all stay in
// sync. Fields the product does not expose stay at their struct defaults.
class ParameterBridge
{
public:
    ParameterBridge (juce::AudioProcessorValueTreeState& state, const ProductDefinition& product);

    const ProductDefinition& getProduct() const noexcept { return product; }

    ChannelParameters read() const noexcept;
    bool isLiveSafe() const noexcept { return liveSafeRaw != nullptr && liveSafeRaw->load() >= 0.5f; }
    ChannelRole readRole() const noexcept;
    StyleProfileId readStyle() const noexcept;
    MacroValues readMacroValues() const noexcept;       // in ProductDefinition::macros order
    Macros readMacros() const noexcept { return MacroMapping::toDrums (readMacroValues()); } // Dine Drums

    // Message thread
    void writeAll (const ChannelParameters& p);
    void writeSubset (const ChannelParameters& p, const std::vector<std::string>& ids);
    void setParameterValue (const std::string& id, float naturalValue);
    float getParameterValue (const std::string& id) const;
    void setRole (ChannelRole role);

private:
    juce::AudioProcessorValueTreeState& apvts;
    const ProductDefinition& product;
    std::vector<std::atomic<float>*> dspRaw;   // in forEachDspParameter order; nullptr where the product has no such parameter
    std::atomic<float>* liveSafeRaw = nullptr;
    std::atomic<float>* roleRaw = nullptr;
    std::atomic<float>* styleRaw = nullptr;
    std::atomic<float>* macroRaw[5] {};
};

} // namespace livemix
