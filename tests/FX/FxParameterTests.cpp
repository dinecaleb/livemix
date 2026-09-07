#include "TestFramework.h"
#include "FX/FxParameterSpecs.h"
#include "FX/FxParameters.h"
#include "FX/FxMacroMapping.h"
#include "FX/FxProfiles.h"
#include "State/ParameterIDs.h"
#include <set>
#include <map>
#include <cstring>

using namespace livemix;

namespace
{
    std::map<std::string, float> flatten (const FxParameters& p)
    {
        std::map<std::string, float> m;
        FxParameters copy = p;
        forEachFxParameter (copy, [&] (const std::string& id, auto& v) { m[id] = float (v); });
        return m;
    }
}

TEST_CASE ("FX specs: every FX field has a spec with matching type and default; ids are unique")
{
    FxParameters p;
    forEachFxParameter (p, [&] (const std::string& id, auto& v)
    {
        using T = std::remove_reference_t<decltype (v)>;
        const ParameterSpec* s = findFxParameterSpec (id);
        REQUIRE (s != nullptr);
        if constexpr (std::is_same_v<T, bool>) CHECK (s->type == ParameterSpec::Type::Bool);
        else if constexpr (std::is_same_v<T, int>) CHECK (s->type == ParameterSpec::Type::Choice);
        else CHECK (s->type == ParameterSpec::Type::Float);
        CHECK (s->defaultValue >= s->minValue && s->defaultValue <= s->maxValue);
        CHECK_NEAR (float (v), s->defaultValue, 1e-5f);
    });
    std::set<std::string> ids;
    for (auto& s : fxParameterSpecs()) CHECK (ids.insert (s.id).second);
    // Shell ids the common UI relies on, the type and the five macros.
    for (auto* id : { ParamID::profile, ParamID::bypass, ParamID::abMatch, ParamID::liveSafe, FxParamID::fxType,
                      FxParamID::space, FxParamID::length, FxParamID::warmth, FxParamID::clarity, FxParamID::distance })
        CHECK (findFxParameterSpec (id) != nullptr);
    CHECK (! findFxParameterSpec (FxParamID::fxType)->automatable);
    CHECK (int (fxParameterSpecs().size()) == countFxParameters() + 3 + 5); // type, profile, liveSafe + macros
    CHECK (findFxParameterSpec ("gateThreshold") == nullptr); // drum parameters never leak into the FX table
}

TEST_CASE ("FX profiles: every baseline is inside its spec range and macros at 50 leave it unchanged")
{
    for (int s = 0; s < int (StyleProfileId::Count); ++s)
        for (int t = 0; t < int (FxType::Count); ++t)
        {
            const FxParameters base = FxProfiles::baseline (StyleProfileId (s), FxType (t));
            FxParameters copy = base;
            forEachFxParameter (copy, [&] (const std::string& id, auto& v)
            {
                const ParameterSpec* spec = findFxParameterSpec (id);
                REQUIRE (spec != nullptr);
                CHECK (float (v) >= spec->minValue - 1e-5f && float (v) <= spec->maxValue + 1e-5f);
            });
            const FxParameters same = FxMacroMapping::apply (base, FxMacros {}, fxFamily (FxType (t)));
            const auto a = flatten (base), b = flatten (same);
            for (const auto& [id, value] : a) CHECK_NEAR (value, b.at (id), 1e-6f);
            // Reverb types run the reverb; delay types run the delay.
            if (fxFamily (FxType (t)) == FxFamily::Reverb) CHECK (base.reverbEnabled && ! base.delayEnabled);
            else CHECK (base.delayEnabled);
            CHECK (std::strlen (FxProfiles::intent (FxType (t))) > 10);
        }
}

TEST_CASE ("FX macros: moves are bounded, monotonic in the musical direction and touch only the affected ids")
{
    const auto& affected = FxMacroMapping::affectedParameterIds();
    const std::set<std::string> allowed (affected.begin(), affected.end());
    for (int t = 0; t < int (FxType::Count); ++t)
    {
        const FxParameters base = FxProfiles::baseline (StyleProfileId::ModernGospel, FxType (t));
        const FxFamily fam = fxFamily (FxType (t));
        for (FxMacros m : { FxMacros { 100, 100, 100, 100, 100 }, FxMacros { 0, 0, 0, 0, 0 }, FxMacros { 90, 10, 20, 80, 30 } })
        {
            const FxParameters out = FxMacroMapping::apply (base, m, fam);
            const auto a = flatten (base), b = flatten (out);
            for (const auto& [id, value] : a)
            {
                if (std::fabs (value - b.at (id)) > 1e-6f) CHECK (allowed.count (id) > 0);
                const ParameterSpec* spec = findFxParameterSpec (id);
                CHECK (b.at (id) >= spec->minValue - 1e-5f && b.at (id) <= spec->maxValue + 1e-5f);
            }
        }
        FxMacros longer; longer.length = 100.0f;
        FxMacros warmer; warmer.warmth = 100.0f;
        FxMacros clearer; clearer.clarity = 100.0f;
        FxMacros bigger; bigger.space = 100.0f;
        FxMacros farther; farther.distance = 100.0f;
        CHECK (FxMacroMapping::apply (base, longer, fam).reverbDecayS > base.reverbDecayS);
        CHECK (FxMacroMapping::apply (base, longer, fam).delayFeedback > base.delayFeedback);
        CHECK (FxMacroMapping::apply (base, warmer, fam).reverbHighCutHz < base.reverbHighCutHz);
        CHECK (FxMacroMapping::apply (base, clearer, fam).reverbPreDelayMs > base.reverbPreDelayMs);
        CHECK (FxMacroMapping::apply (base, clearer, fam).delayDuck >= base.delayDuck);
        CHECK (FxMacroMapping::apply (base, bigger, fam).reverbSize > base.reverbSize || base.reverbSize >= 100.0f);
        CHECK (FxMacroMapping::apply (base, farther, fam).reverbLevelDb > base.reverbLevelDb);
    }
    for (const auto& id : affected) CHECK (findFxParameterSpec (id) != nullptr);
}

TEST_CASE ("FX use presets: named starting points resolve to valid profile/type/macros")
{
    const auto& presets = FxProfiles::usePresets();
    CHECK (presets.size() == 6);
    std::set<std::string> names;
    for (const auto& u : presets)
    {
        CHECK (names.insert (u.name).second);
        CHECK (int (u.type) >= 0 && int (u.type) < int (FxType::Count));
        CHECK (int (u.profile) >= 0 && int (u.profile) < int (StyleProfileId::Count));
        for (float m : { u.macros.space, u.macros.length, u.macros.warmth, u.macros.clarity, u.macros.distance })
            CHECK (m >= 0.0f && m <= 100.0f);
        const FxParameters p = FxMacroMapping::apply (FxProfiles::baseline (u.profile, u.type), u.macros, fxFamily (u.type));
        CHECK (p.reverbEnabled || p.delayEnabled);
    }
}
