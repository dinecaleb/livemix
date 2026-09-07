#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Core/ChannelRole.h"
#include "Core/ProductDefinition.h"
#include "Core/StyleId.h"

namespace livemix
{

// Preset hierarchy of one Dine channel product: Factory -> Style -> Source
// (generated from the profiles), plus user presets stored as XML in the LiveMix
// application-support folder for that product. A preset holds source, profile,
// the five Simple knobs and every DSP parameter the product exposes.
class PresetManager
{
public:
    struct Info
    {
        juce::String name;
        bool isFactory = true;
        ChannelRole role = ChannelRole::KickIn;
        StyleProfileId style = StyleProfileId::ModernWorship;
        juce::File file; // user presets only
    };

    static juce::File getUserPresetDirectory (const ProductDefinition& product);
    static std::vector<Info> getFactoryPresets (const ProductDefinition& product);
    static std::vector<Info> getUserPresets (const ProductDefinition& product);

    // Preset tree format: <presetType name role profile> <Macros .../> <Params .../> </presetType>
    static juce::ValueTree factoryPresetTree (const ProductDefinition& product, const Info& info);
    static juce::ValueTree loadUserPreset (const ProductDefinition& product, const Info& info); // invalid tree on failure
    static bool saveUserPreset (const ProductDefinition& product, const juce::String& name, const juce::ValueTree& tree, Info* savedInfo = nullptr);
    static bool deleteUserPreset (const Info& info);
    static juce::String sanitiseName (const juce::String& name);

    // Dine Drums (older call sites)
    static juce::File getUserPresetDirectory()          { return getUserPresetDirectory (productDefinition (Product::Drums)); }
    static std::vector<Info> getFactoryPresets()        { return getFactoryPresets (productDefinition (Product::Drums)); }
    static std::vector<Info> getUserPresets()           { return getUserPresets (productDefinition (Product::Drums)); }
    static juce::ValueTree factoryPresetTree (const Info& info) { return factoryPresetTree (productDefinition (Product::Drums), info); }
    static juce::ValueTree loadUserPreset (const Info& info)    { return loadUserPreset (productDefinition (Product::Drums), info); }
    static bool saveUserPreset (const juce::String& name, const juce::ValueTree& tree, Info* savedInfo = nullptr)
    {
        return saveUserPreset (productDefinition (Product::Drums), name, tree, savedInfo);
    }
};

} // namespace livemix
