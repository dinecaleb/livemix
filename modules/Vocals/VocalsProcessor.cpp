#include "VocalsProcessor.h"

// JUCE entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new livemix::VocalsProcessor();
}
