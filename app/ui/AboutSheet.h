#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "AppTheme.h"

namespace livemix
{

// ABOUT DINE: the identity's splash, as the product introduces itself (DINE Identity v1,
// "Splash screen"). A black card, the wordmark in Paper, the line under it in Steel, and the
// version at the foot. Black and white only - the mark never takes the accent. Any click or
// key closes it.
class AboutSheet : public juce::Component
{
public:
    explicit AboutSheet (juce::String version);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr const char* kTagline = "The DAW built for live broadcast";

private:
    juce::Rectangle<int> cardBounds() const;
    juce::String version;
};

} // namespace livemix
