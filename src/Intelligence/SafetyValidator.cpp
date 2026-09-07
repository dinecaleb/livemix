#include "SafetyValidator.h"
#include "State/ParameterSpecs.h"
#include "State/ParameterIDs.h"
#include <cmath>
#include <set>

namespace livemix
{

namespace SafetyValidator
{

namespace
{
    // Parameters recommendations may never write. Capture gain is a physical
    // action; role/profile/bypass/liveSafe are user decisions.
    const std::set<std::string>& forbiddenIds()
    {
        static const std::set<std::string> ids {
            ParamID::role, ParamID::profile, ParamID::bypass, ParamID::liveSafe,
            ParamID::inputTrim, ParamID::polarity,
            ParamID::character, ParamID::punch, ParamID::body, ParamID::attack, ParamID::bleed,
            ParamID::warmth, ParamID::clarity, ParamID::smooth, ParamID::steady, ParamID::cleanup,
            ParamID::shine, ParamID::width, ParamID::glue, ParamID::loud, ParamID::grit
        };
        return ids;
    }

    // Tighter bounds for AI-originated suggestions (natural units).
    bool aiLimit (const std::string& id, float& value)
    {
        const bool isEqGain = id.find ("Gain") != std::string::npos && (id.rfind ("corrEq", 0) == 0 || id.rfind ("toneEq", 0) == 0);
        if (isEqGain) { const float c = value < -6.0f ? -6.0f : (value > 4.0f ? 4.0f : value); const bool clamped = c != value; value = c; return clamped; }
        if (id == ParamID::compRatio)  { const float c = value > 8.0f ? 8.0f : value; const bool cl = c != value; value = c; return cl; }
        if (id == ParamID::compMakeup) { const float c = value > 6.0f ? 6.0f : (value < -6.0f ? -6.0f : value); const bool cl = c != value; value = c; return cl; }
        if (id == ParamID::outputTrim) { const float c = value > 6.0f ? 6.0f : (value < -12.0f ? -12.0f : value); const bool cl = c != value; value = c; return cl; }
        if (id == ParamID::gateRange)  { const float c = value > 60.0f ? 60.0f : value; const bool cl = c != value; value = c; return cl; }
        if (id == ParamID::satDrive)   { const float c = value > 0.5f ? 0.5f : value; const bool cl = c != value; value = c; return cl; }
        if (id == ParamID::deEssRange) { const float c = value > 10.0f ? 10.0f : value; const bool cl = c != value; value = c; return cl; }
        if (id == ParamID::widthAmount){ const float c = value > 1.5f ? 1.5f : (value < 0.5f ? 0.5f : value); const bool cl = c != value; value = c; return cl; }
        if (id == ParamID::limiterCeiling) { const float c = value > -0.3f ? -0.3f : (value < -6.0f ? -6.0f : value); const bool cl = c != value; value = c; return cl; }
        return false;
    }
}

RecommendationResult validate (const RecommendationResult& input, bool fromAI, Report* report)
{
    Report rep;
    RecommendationResult out = input;
    out.items.clear();

    for (const auto& item : input.items)
    {
        Recommendation r = item;
        r.changes.clear();
        for (const auto& change : item.changes)
        {
            if (forbiddenIds().count (change.paramId) > 0) { ++rep.changesDropped; continue; }
            const ParameterSpec* spec = findParameterSpec (change.paramId);
            if (spec == nullptr || ! std::isfinite (change.value)) { ++rep.changesDropped; continue; }

            ParameterChange c = change;
            const float clamped = spec->clamp (c.value);
            bool wasClamped = clamped != c.value;
            c.value = clamped;
            if (fromAI && aiLimit (c.paramId, c.value)) wasClamped = true;
            if (spec->type != ParameterSpec::Type::Float) c.value = std::round (c.value);
            if (wasClamped) ++rep.changesClamped;
            r.changes.push_back (c);
        }

        if (! item.changes.empty() && r.changes.empty())
        {
            // Every change was rejected: keep the explanation but nothing can be applied.
            r.safeToAutoApply = false;
            if (fromAI) { ++rep.itemsDropped; continue; }
        }
        if (fromAI && r.what.empty()) { ++rep.itemsDropped; continue; }
        if (fromAI) r.safeToAutoApply = false; // AI suggestions are always opt-in per item
        out.items.push_back (r);
    }

    if (report != nullptr) *report = rep;
    return out;
}

} // namespace SafetyValidator
} // namespace livemix
