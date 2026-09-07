#pragma once
#include <vector>
#include "State/ParameterSpecs.h"

namespace livemix
{

// The Dine FX parameter table: shell ids (profile, bypass, abMatch, liveSafe),
// the type, the five Simple-mode macros and every FxParameters field.
// The JUCE layout for the FX plugin is generated from this table.
const std::vector<ParameterSpec>& fxParameterSpecs();
const ParameterSpec* findFxParameterSpec (const std::string& id);

} // namespace livemix
