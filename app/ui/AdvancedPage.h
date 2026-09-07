#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "UI/Widgets.h"
#include "UI/MeterComponent.h"

namespace livemix
{

// Advanced: the engineer's drill-down. Left, every strip and bus with its meter and
// level; right, the selected one: input gain, fader, mute, sends, and every decision
// Tune Mix made about it in plain WHAT / WHY sentences. Same state as the overview:
// switching views never changes the sound.
class AdvancedPage : public juce::Component
{
public:
    explicit AdvancedPage (MixController&);
    ~AdvancedPage() override;

    std::function<void()> onBack;

    void refresh();                          // 30 Hz
    void rebuild();                          // after the session / graph changed
    void select (int strip);                 // programmatic (snapshot tool)
    void selectBus (MixBus bus);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    class Detail;
    struct Selection { bool isBus = false; int strip = -1; MixBus bus = MixBus::Master; };

    MixController& controller;
    Selection selection;
    std::vector<std::unique_ptr<Row>> rows;    // strips then buses
    juce::Viewport viewport;
    juce::Component listHolder;
    std::unique_ptr<Detail> detail;
    FlatButton backButton { "MIX", FlatButton::Style::Outline };
    int builtForStrips = -1;
};

} // namespace livemix
