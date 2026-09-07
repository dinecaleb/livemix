#include "ParameterLayout.h"
#include "ParameterSpecs.h"

namespace livemix
{

namespace
{
    juce::String formatFloat (const ParameterSpec& spec, float v)
    {
        if (spec.unit == "Hz")
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
        if (spec.unit == "s")
            return juce::String (v, 2) + " s";
        if (spec.unit == "ms")
            return juce::String (v, v < 10.0f ? 2 : 1) + " ms";
        if (spec.unit == "dB")
            return juce::String (v, 1) + " dB";
        if (spec.unit == ":1")
            return juce::String (v, 1) + ":1";
        return juce::String (v, 2);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    return createParameterLayout (allParameterSpecs());
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const std::vector<ParameterSpec>& specs)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : specs)
    {
        const juce::ParameterID pid { spec.id, 1 };

        switch (spec.type)
        {
            case ParameterSpec::Type::Float:
            {
                juce::NormalisableRange<float> range (spec.minValue, spec.maxValue, 0.0f);
                if (spec.skewMidpoint > 0.0f)
                    range.setSkewForCentre (spec.skewMidpoint);

                auto attrs = juce::AudioParameterFloatAttributes()
                                 .withLabel (spec.unit)
                                 .withAutomatable (spec.automatable)
                                 .withStringFromValueFunction ([spec] (float v, int) { return formatFloat (spec, v); });
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, spec.name, range, spec.defaultValue, attrs));
                break;
            }
            case ParameterSpec::Type::Bool:
            {
                auto attrs = juce::AudioParameterBoolAttributes().withAutomatable (spec.automatable);
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, spec.name, spec.defaultValue >= 0.5f, attrs));
                break;
            }
            case ParameterSpec::Type::Choice:
            {
                juce::StringArray choices;
                for (const auto& c : spec.choices) choices.add (c);
                auto attrs = juce::AudioParameterChoiceAttributes().withAutomatable (spec.automatable);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, spec.name, choices, int (spec.defaultValue), attrs));
                break;
            }
        }
    }
    return layout;
}

} // namespace livemix
