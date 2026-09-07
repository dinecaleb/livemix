#include "ParameterBridge.h"
#include "ParameterIDs.h"
#include <set>

namespace livemix
{

ParameterBridge::ParameterBridge (juce::AudioProcessorValueTreeState& state, const ProductDefinition& def)
    : apvts (state), product (def)
{
    ChannelParameters p;
    forEachDspParameter (p, [&] (const std::string& id, auto&)
    {
        dspRaw.push_back (apvts.getRawParameterValue (id)); // nullptr: the product hides this stage
    });
    liveSafeRaw = apvts.getRawParameterValue (ParamID::liveSafe);
    roleRaw = apvts.getRawParameterValue (ParamID::role);
    styleRaw = apvts.getRawParameterValue (ParamID::profile);
    for (size_t i = 0; i < 5; ++i)
    {
        macroRaw[i] = apvts.getRawParameterValue (product.macros[i].id);
        jassert (macroRaw[i] != nullptr);
    }
}

ChannelParameters ParameterBridge::read() const noexcept
{
    // Audio thread: no string building, no lookups, cached atomics only.
    ChannelParameters p;
    size_t i = 0;
    forEachDspField (p, [&] (auto, auto& v)
    {
        using T = std::remove_reference_t<decltype (v)>;
        const std::atomic<float>* raw = dspRaw[i++];
        if (raw == nullptr) return; // keep the struct default
        const float value = raw->load (std::memory_order_relaxed);
        if constexpr (std::is_same_v<T, bool>) v = value >= 0.5f;
        else if constexpr (std::is_same_v<T, int>) v = int (value + 0.5f);
        else v = value;
    });
    return p;
}

ChannelRole ParameterBridge::readRole() const noexcept
{
    return roleFromProductIndex (product, roleRaw != nullptr ? int (roleRaw->load() + 0.5f) : 0);
}

void ParameterBridge::setRole (ChannelRole role)
{
    setParameterValue (ParamID::role, float (roleIndexInProduct (product, role)));
}

StyleProfileId ParameterBridge::readStyle() const noexcept
{
    return styleProfileFromIndex (styleRaw != nullptr ? int (styleRaw->load() + 0.5f) : 0);
}

MacroValues ParameterBridge::readMacroValues() const noexcept
{
    MacroValues m;
    for (size_t i = 0; i < 5; ++i)
        m.v[i] = macroRaw[i] != nullptr ? macroRaw[i]->load() : product.macros[i].defaultValue;
    return m;
}

void ParameterBridge::setParameterValue (const std::string& id, float naturalValue)
{
    if (auto* param = apvts.getParameter (id))
    {
        const float normalised = param->convertTo0to1 (naturalValue);
        if (std::abs (param->getValue() - normalised) < 1.0e-6f) return;
        param->beginChangeGesture();
        param->setValueNotifyingHost (normalised);
        param->endChangeGesture();
    }
}

float ParameterBridge::getParameterValue (const std::string& id) const
{
    if (auto* raw = apvts.getRawParameterValue (id)) return raw->load();
    return 0.0f;
}

void ParameterBridge::writeAll (const ChannelParameters& p)
{
    ChannelParameters copy = p;
    forEachDspParameter (copy, [&] (const std::string& id, auto& v)
    {
        if (id == ParamID::bypass || id == ParamID::abMatch) return; // A/B state is never part of a preset
        setParameterValue (id, float (v));                          // unknown ids (hidden stages) are ignored
    });
}

void ParameterBridge::writeSubset (const ChannelParameters& p, const std::vector<std::string>& ids)
{
    const std::set<std::string> wanted (ids.begin(), ids.end());
    ChannelParameters copy = p;
    forEachDspParameter (copy, [&] (const std::string& id, auto& v)
    {
        if (wanted.count (id) > 0) setParameterValue (id, float (v));
    });
}

} // namespace livemix
