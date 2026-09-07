#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "DSP/ChannelParameters.h"

namespace livemix
{

// Combined magnitude response of the linear stages (filters + both EQs) with
// band markers, drawn on the design's inset scope ground. An optional flag
// marks a frequency the analysis wants reviewed (e.g. "290 Hz RESONANCE").
class EqCurveComponent : public juce::Component
{
public:
    void update (const ChannelParameters& params, double sampleRate, float levelNorm);
    void setFlag (float hz, const juce::String& title, const juce::String& subtitle);
    void clearFlag() { flagHz = 0.0f; repaint(); }
    void paint (juce::Graphics& g) override;

    static float xForFreq (float hz, float width) noexcept;

private:
    ChannelParameters current;
    double sr = 48000.0;
    std::vector<float> curveDb;
    float level = 0.0f;
    float flagHz = 0.0f;
    juce::String flagTitle, flagSub;
};

} // namespace livemix
