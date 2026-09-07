#include "FxMacroMapping.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace FxMacroMapping
{

FxParameters apply (const FxParameters& base, const FxMacros& m, FxFamily family)
{
    FxParameters p = base;

    const float space    = clamp ((m.space    - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float length   = clamp ((m.length   - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float warmth   = clamp ((m.warmth   - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float clarity  = clamp ((m.clarity  - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float distance = clamp ((m.distance - 50.0f) / 50.0f, -1.0f, 1.0f);

    // --- Reverb ---
    p.reverbSize = clamp (base.reverbSize + 35.0f * space, 0.0f, 100.0f);
    p.reverbDecayS = clamp (base.reverbDecayS * std::pow (2.0f, 1.5f * length), 0.2f, 20.0f);
    p.reverbDamping = clamp (base.reverbDamping + 30.0f * warmth, 0.0f, 100.0f);
    p.reverbDiffusion = clamp (base.reverbDiffusion - 10.0f * space, 0.0f, 100.0f);
    p.reverbHighCutHz = clamp (base.reverbHighCutHz * std::pow (2.0f, -1.2f * warmth - 0.6f * distance), 1000.0f, 20000.0f);
    p.reverbLowCutHz = clamp (base.reverbLowCutHz * std::pow (2.0f, 1.0f * clarity - 0.5f * warmth), 20.0f, 1000.0f);
    // Pre-delay: clarity separates the tail from the source; distance closes the gap; space widens it a little.
    p.reverbPreDelayMs = clamp (base.reverbPreDelayMs * (1.0f + 0.3f * space) * (1.0f - 0.6f * distance) + 30.0f * clarity, 0.0f, 250.0f);
    // Early reflections: a close, small space is mostly early energy; far and large is mostly tail.
    p.reverbEarly = clamp (base.reverbEarly - 15.0f * space - 20.0f * distance, 0.0f, 100.0f);
    p.reverbLevelDb = clamp (base.reverbLevelDb + 6.0f * distance, -40.0f, 12.0f);

    // --- Delay ---
    p.delayFeedback = clamp (base.delayFeedback + 30.0f * length, 0.0f, 95.0f);
    p.delayHighCutHz = clamp (base.delayHighCutHz * std::pow (2.0f, -1.2f * warmth - 0.4f * distance), 1000.0f, 20000.0f);
    p.delayLowCutHz = clamp (base.delayLowCutHz * std::pow (2.0f, 1.0f * clarity), 20.0f, 1000.0f);
    p.delayDuck = clamp (base.delayDuck + 40.0f * clarity, 0.0f, 100.0f);
    p.delayLevelDb = clamp (base.delayLevelDb + 4.0f * distance, -40.0f, 12.0f);
    if (family == FxFamily::Delay)
    {
        p.delayWidth = clamp (base.delayWidth + 40.0f * space, 0.0f, 100.0f);
        p.delayToReverb = clamp (base.delayToReverb + 30.0f * space, 0.0f, 100.0f);
    }
    else
    {
        p.delayWidth = base.delayWidth;
        p.delayToReverb = base.delayToReverb;
    }
    return p;
}

const std::vector<std::string>& affectedParameterIds()
{
    static const std::vector<std::string> ids = [] {
        using namespace FxParamID;
        return std::vector<std::string> {
            rvSize, rvDecay, rvDamping, rvDiffusion, rvHighCut, rvLowCut, rvPreDelay, rvEarly, rvLevel,
            dlFeedback, dlHighCut, dlLowCut, dlDuck, dlLevel, dlWidth, dlToReverb
        };
    }();
    return ids;
}

} // namespace FxMacroMapping
} // namespace livemix
