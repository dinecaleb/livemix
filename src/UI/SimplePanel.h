#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "Widgets.h"
#include "InputPanel.h"
#include "ChainStrip.h"
#include "LiveState.h"
#include "Core/ProductDefinition.h"

namespace livemix
{

// Simple mode: INPUT panel on the left, the product's five knobs with TUNE +
// AI ASSIST in the centre, chain strip at the bottom. Every knob is bound to a
// host parameter, so Simple and Advanced always agree. Dine Master adds a
// loudness readout under the knobs.
class SimplePanel : public juce::Component
{
public:
    SimplePanel (juce::AudioProcessorValueTreeState&, const ProductDefinition& product);

    std::function<void()> onAnalyze, onToggleAI;
    std::function<void (ChainModule)> onOpenModule;

    struct Status
    {
        ChannelRole role = ChannelRole::KickIn;
        StyleProfileId style = StyleProfileId::ModernGospel;
        bool liveSafe = false, aiOn = false, aiAvailable = false, analyzeBusy = false, tuned = false;
    };
    void update (const LiveState& live, const Status& s);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    const ProductDefinition& product;
    InputPanel input;
    std::vector<std::unique_ptr<MacroKnob>> knobs;
    FlatButton analyzeButton { "TUNE", FlatButton::Style::Solid };
    FlatButton aiButton { "AI ASSIST OFF", FlatButton::Style::Outline };
    ChainStrip chain;
    Status status;
    juce::String hint, loudnessLine, loudnessValue;
    juce::Colour loudnessColour;
};

} // namespace livemix
