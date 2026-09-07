#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "FX/FxParameters.h"
#include "FX/FxMacroMapping.h"
#include "Core/StyleId.h"

namespace livemix
{

// Connects FxParameters to the host parameter tree of a Dine FX instance.
// read() is audio-thread safe (cached atomics, no lookups); the write
// functions are message-thread only and go through the host notification path.
class FxParameterBridge
{
public:
    explicit FxParameterBridge (juce::AudioProcessorValueTreeState& state);

    FxParameters read() const noexcept;
    bool isLiveSafe() const noexcept { return liveSafeRaw != nullptr && liveSafeRaw->load() >= 0.5f; }
    FxType readType() const noexcept;
    StyleProfileId readStyle() const noexcept;
    FxMacros readMacros() const noexcept;

    // Message thread
    void writeAll (const FxParameters& p);
    void writeSubset (const FxParameters& p, const std::vector<std::string>& ids);
    void setParameterValue (const std::string& id, float naturalValue);
    float getParameterValue (const std::string& id) const;

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::atomic<float>*> dspRaw;   // in forEachFxParameter order
    std::atomic<float>* liveSafeRaw = nullptr;
    std::atomic<float>* typeRaw = nullptr;
    std::atomic<float>* styleRaw = nullptr;
    std::atomic<float>* macroRaw[5] {};
};

} // namespace livemix
