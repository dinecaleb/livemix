#pragma once
#include <vector>
#include <string>
#include "FxParameters.h"

namespace livemix
{

// Simple-mode controls for Dine FX. 50 = the type/profile baseline.
//   SPACE    how big the space feels (size, early reflections, delay width)
//   LENGTH   how long it lasts (decay, delay feedback)
//   WARMTH   dark <-> bright (damping, high cut, low cut)
//   CLARITY  how much it stays out of the source's way (pre-delay, low cut, ducking)
//   DISTANCE close <-> far (level, pre-delay, early/late balance, top end)
struct FxMacros
{
    float space = 50.0f;
    float length = 50.0f;
    float warmth = 50.0f;
    float clarity = 50.0f;
    float distance = 50.0f;
};

namespace FxMacroMapping
{
    // Derives the engineering parameters from the baseline plus macro offsets.
    // Bounded and musically conservative; macros at 50 return the baseline unchanged.
    FxParameters apply (const FxParameters& baseline, const FxMacros& macros, FxFamily family);

    // Parameter IDs a macro move may rewrite. Everything else the user set in Advanced mode is preserved.
    const std::vector<std::string>& affectedParameterIds();
}

} // namespace livemix
