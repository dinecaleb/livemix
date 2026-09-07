#include "FxParameterBridge.h"
#include "ParameterIDs.h"
#include <set>

namespace livemix
{

FxParameterBridge::FxParameterBridge (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    FxParameters p;
    forEachFxParameter (p, [&] (const std::string& id, auto&)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr); // every visited field must exist in the layout
        dspRaw.push_back (raw);
    });
    liveSafeRaw = apvts.getRawParameterValue (ParamID::liveSafe);
    typeRaw = apvts.getRawParameterValue (FxParamID::fxType);
    styleRaw = apvts.getRawParameterValue (ParamID::profile);
    macroRaw[0] = apvts.getRawParameterValue (FxParamID::space);
    macroRaw[1] = apvts.getRawParameterValue (FxParamID::length);
    macroRaw[2] = apvts.getRawParameterValue (FxParamID::warmth);
    macroRaw[3] = apvts.getRawParameterValue (FxParamID::clarity);
    macroRaw[4] = apvts.getRawParameterValue (FxParamID::distance);
}

FxParameters FxParameterBridge::read() const noexcept
{
    FxParameters p;
    size_t i = 0;
    forEachFxField (p, [&] (auto, auto& v)
    {
        using T = std::remove_reference_t<decltype (v)>;
        const float raw = dspRaw[i] != nullptr ? dspRaw[i]->load (std::memory_order_relaxed) : 0.0f;
        ++i;
        if constexpr (std::is_same_v<T, bool>) v = raw >= 0.5f;
        else if constexpr (std::is_same_v<T, int>) v = int (raw + 0.5f);
        else v = raw;
    });
    return p;
}

FxType FxParameterBridge::readType() const noexcept
{
    return fxTypeFromIndex (typeRaw != nullptr ? int (typeRaw->load() + 0.5f) : 0);
}

StyleProfileId FxParameterBridge::readStyle() const noexcept
{
    return styleProfileFromIndex (styleRaw != nullptr ? int (styleRaw->load() + 0.5f) : 0);
}

FxMacros FxParameterBridge::readMacros() const noexcept
{
    FxMacros m;
    if (macroRaw[0]) m.space = macroRaw[0]->load();
    if (macroRaw[1]) m.length = macroRaw[1]->load();
    if (macroRaw[2]) m.warmth = macroRaw[2]->load();
    if (macroRaw[3]) m.clarity = macroRaw[3]->load();
    if (macroRaw[4]) m.distance = macroRaw[4]->load();
    return m;
}

void FxParameterBridge::setParameterValue (const std::string& id, float naturalValue)
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

float FxParameterBridge::getParameterValue (const std::string& id) const
{
    if (auto* raw = apvts.getRawParameterValue (id)) return raw->load();
    return 0.0f;
}

void FxParameterBridge::writeAll (const FxParameters& p)
{
    FxParameters copy = p;
    forEachFxParameter (copy, [&] (const std::string& id, auto& v)
    {
        if (id == ParamID::bypass || id == ParamID::abMatch) return; // A/B state is never part of a preset
        setParameterValue (id, float (v));
    });
}

void FxParameterBridge::writeSubset (const FxParameters& p, const std::vector<std::string>& ids)
{
    const std::set<std::string> wanted (ids.begin(), ids.end());
    FxParameters copy = p;
    forEachFxParameter (copy, [&] (const std::string& id, auto& v)
    {
        if (wanted.count (id) > 0) setParameterValue (id, float (v));
    });
}

} // namespace livemix
