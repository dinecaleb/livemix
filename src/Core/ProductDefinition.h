#pragma once
#include <array>
#include <vector>
#include "ChannelRole.h"

namespace livemix
{

// Stages of the shared channel chain (ChannelProcessor), in processing order.
// Each product shows the stages that make sense for its sources; the others
// stay off and cost nothing.
enum class ChainStage : int { Input = 0, Gate, EQ, DeEss, Comp, Transient, Width, Limiter, Output, Count };

// One Simple-mode knob: a host parameter plus the plain words shown to the user.
struct MacroSpec
{
    const char* id = "";        // host parameter id (never renamed once released)
    const char* label = "";     // knob label, e.g. "WARMTH"
    const char* tooltip = "";   // one or two plain sentences
    float minValue = 0.0f, maxValue = 100.0f, defaultValue = 50.0f;
};

// Everything the shell needs to know about a Dine channel product. Pure data,
// no JUCE: the plugin layer (modules/Common) reads it, the engine uses it for
// parameter tables and macro mapping.
struct ProductDefinition
{
    Product product = Product::Drums;
    const char* name = "";             // "Dine Drums"
    const char* shortName = "";        // "DRUMS" (module button)
    const char* sourceCaption = "";    // "SOURCE" / "OUTPUT"
    const char* stateType = "";        // root ValueTree type of the saved state
    const char* presetType = "";       // ValueTree type of a preset
    const char* presetFolder = "";     // ~/Library/Application Support/LiveMix/Presets/<folder>
    const char* defaultGroup = "";     // instance registry group
    const char* playerPrompt = "";     // "have the drummer play normally"
    const char* sourceNoun = "";       // "the drum" (waiting-for-signal copy)
    const char* mixNoun = "";          // "a drum mix"
    const char* eventNoun = "";        // "hit" / "note" / "phrase"
    bool hasKit = false;               // KIT tab (group tune) available
    bool hasLoudness = false;          // loudness meter + limiter (Dine Master)
    ChannelRole defaultRole = ChannelRole::KickIn;
    std::vector<ChannelRole> roles;    // the SOURCE menu, in order
    std::array<MacroSpec, 5> macros {};
    std::vector<ChainStage> stages;    // chain strip / Advanced rail, in order
};

const ProductDefinition& productDefinition (Product p);
inline const ProductDefinition& productDefinitionFor (ChannelRole r) { return productDefinition (productOf (r)); }

// Plain-language one-liner for the Simple view ("Tight low end · controlled beater click · deep body").
const char* roleHint (ChannelRole r) noexcept;

// Position of a role inside its product's SOURCE menu (the value stored in the product's "role" parameter).
int roleIndexInProduct (const ProductDefinition& d, ChannelRole r) noexcept;
ChannelRole roleFromProductIndex (const ProductDefinition& d, int index) noexcept;

} // namespace livemix
