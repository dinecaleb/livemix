#pragma once
#include <array>
#include <vector>
#include <string>
#include "Core/ChannelRole.h"
#include "DSP/ChannelParameters.h"

namespace livemix
{

// Dine Drums Simple-mode controls. 50 = the role/profile baseline for
// Punch/Body/Attack/Bleed; Character 0 = Clean, 1 = Aggressive (baseline at 0.35).
struct Macros
{
    float character = 0.35f;
    float punch = 50.0f;
    float body = 50.0f;
    float attack = 50.0f;
    float bleedReduction = 50.0f;
};

// Any product's five Simple knobs, in the order of its ProductDefinition::macros.
struct MacroValues
{
    std::array<float, 5> v {};
};

namespace MacroMapping
{
    // Derives the underlying DSP parameters from the baseline plus macro offsets.
    // Every mapping is bounded and musically conservative.
    ChannelParameters apply (const ChannelParameters& baseline, const Macros& macros, RoleFamily family); // Dine Drums
    ChannelParameters apply (Product product, const ChannelParameters& baseline, const MacroValues& values, RoleFamily family);

    // Parameter IDs that macros are allowed to modify. The plugin writes only these
    // when a macro moves, so unrelated Advanced-mode edits are preserved.
    const std::vector<std::string>& affectedParameterIds();               // Dine Drums
    const std::vector<std::string>& affectedParameterIds (Product product);

    MacroValues defaults (Product product);                 // every knob at its baseline position
    MacroValues fromDrums (const Macros& m);                 // Punch Body Attack Tone Bleed order
    Macros toDrums (const MacroValues& v);
}

} // namespace livemix
