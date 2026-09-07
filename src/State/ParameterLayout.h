#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "ParameterSpecs.h"

namespace livemix
{

// Builds the host parameter layout from a ParameterSpec table: the drum table
// by default, or any product's table (Dine FX passes fxParameterSpecs()).
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const std::vector<ParameterSpec>& specs);

} // namespace livemix
