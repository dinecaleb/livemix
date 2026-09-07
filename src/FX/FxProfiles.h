#pragma once
#include <vector>
#include "Core/StyleId.h"
#include "FxParameters.h"
#include "FxMacroMapping.h"

namespace livemix
{

// Dine FX profile data: the engineering baseline for every (profile, type)
// and the named use presets built from them. Numbers only, in FxProfiles.cpp.
namespace FxProfiles
{
    FxParameters baseline (StyleProfileId profile, FxType type);
    const char* intent (FxType type);           // one line of engineering intent, shown to the user

    // Named starting points from the product brief, expressed as profile + type + macro offsets.
    struct UsePreset
    {
        const char* name;
        StyleProfileId profile;
        FxType type;
        FxMacros macros;
    };
    const std::vector<UsePreset>& usePresets();
}

} // namespace livemix
