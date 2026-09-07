#include "TestFramework.h"
#include "State/ParameterSpecs.h"
#include "DSP/ChannelParameters.h"
#include <set>

using namespace livemix;

TEST_CASE ("ParameterSpecs: every DSP field has a spec with matching type and in-range default")
{
    ChannelParameters p;
    forEachDspParameter (p, [&] (const std::string& id, auto& v)
    {
        using T = std::remove_reference_t<decltype (v)>;
        const ParameterSpec* s = findParameterSpec (id);
        REQUIRE (s != nullptr);
        if constexpr (std::is_same_v<T, bool>) CHECK (s->type == ParameterSpec::Type::Bool);
        else if constexpr (std::is_same_v<T, int>) CHECK (s->type == ParameterSpec::Type::Choice);
        else CHECK (s->type == ParameterSpec::Type::Float);
        CHECK (s->defaultValue >= s->minValue && s->defaultValue <= s->maxValue);
        CHECK_NEAR (float (v), s->defaultValue, 1e-5f); // spec default == struct default
    });

    std::set<std::string> ids;
    for (auto& s : allParameterSpecs()) CHECK (ids.insert (s.id).second);
    CHECK (findParameterSpec ("role") != nullptr && ! findParameterSpec ("role")->automatable);
    CHECK (findParameterSpec ("doesNotExist") == nullptr);
}
