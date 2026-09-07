#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MeterComponent.h"
#include "LiveState.h"
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"

namespace livemix
{

// Simple-mode INPUT panel: vertical meter with scale, dBFS peak readout,
// health chip (HEALTHY / INPUT LOW / INPUT HIGH / CLIPPING / NO SIGNAL),
// headroom, a preamp recommendation card when the input is unhealthy and the
// clearly separated PLUGIN · MIX GAIN row (post-processing trim, not preamp).
class InputPanel : public juce::Component
{
public:
    static constexpr int kWidth = 198;
    enum class Health { NoSignal, Healthy, Low, High, Clipping };

    explicit InputPanel (juce::AudioProcessorValueTreeState&);
    ~InputPanel() override;

    void update (const LiveState& live, ChannelRole role, StyleProfileId style);
    Health getHealth() const { return health; }
    static const char* healthLabel (Health h) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class MixGainRow : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MixGainRow (juce::RangedAudioParameter&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
    private:
        juce::RangedAudioParameter& param;
        juce::ParameterAttachment attachment;
        float current = 0.0f, dragStartNorm = 0.0f;
    };

    MeterComponent meter;
    MixGainRow mixGain;
    Health health = Health::NoSignal;
    float peakDb = -120.0f, headroomDb = 60.0f, suggestedDb = 0.0f;
    bool clipLatched = false;
    juce::Rectangle<int> cardArea;
};

} // namespace livemix
