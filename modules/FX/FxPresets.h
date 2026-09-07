#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Core/StyleId.h"
#include "FX/FxParameters.h"

namespace livemix
{

// Dine FX presets: Factory (Profile / Type, generated from FxProfiles), Use
// (the named starting points from the brief) and User presets stored as XML
// in ~/Library/Application Support/LiveMix/Presets/FX/User.
// Preset tree: <LiveMixFxPreset name type profile> <Macros .../> <Params .../> </LiveMixFxPreset>
class FxPresets
{
public:
    enum class Kind { Factory, Use, User };
    struct Info
    {
        juce::String name;
        Kind kind = Kind::Factory;
        FxType type = FxType::VocalPlate;
        StyleProfileId style = StyleProfileId::ModernGospel;
        int useIndex = -1;   // Kind::Use
        juce::File file;     // Kind::User
    };

    static juce::File getUserPresetDirectory();
    static std::vector<Info> getFactoryPresets();
    static std::vector<Info> getUsePresets();
    static std::vector<Info> getUserPresets();

    static juce::ValueTree presetTree (const Info& info);           // Factory / Use: generated; User: loaded (invalid on failure)
    static bool saveUserPreset (const juce::String& name, const juce::ValueTree& tree, Info* savedInfo = nullptr);
    static bool deleteUserPreset (const Info& info);
    static juce::String sanitiseName (const juce::String& name);
};

} // namespace livemix
