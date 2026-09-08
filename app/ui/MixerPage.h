#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// One-screen console: every strip, group bus and the master with a meter, fader,
// mute and solo. Same kept mix as Advanced — switching views never changes the sound.
class MixerPage : public juce::Component
{
public:
    explicit MixerPage (MixController&);
    ~MixerPage() override;

    std::function<void (int strip)> onOpenStrip;
    std::function<void (MixBus bus)> onOpenBus;

    void refresh();                          // 30 Hz
    void rebuild();                          // after the session / graph changed
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class StripColumn;
    class GroupLabel;

    MixController& controller;
    juce::Viewport viewport;
    juce::Component bank;
    std::vector<std::unique_ptr<juce::Component>> items; // labels + columns, layout order
    std::vector<StripColumn*> columns;
    int builtForStrips = -1;
};

} // namespace livemix
