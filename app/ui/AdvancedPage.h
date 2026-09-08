#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"

namespace livemix
{

// Advanced: the engineer's drill-down. Left, a source list of every channel under its
// bus; right, the channel itself — meter, input gain, fader, sends — and every TUNE MIX
// decision in plain WHAT / WHY sentences. Same state as the overview: switching views
// never changes the sound.
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
    class SectionHeader;
    class Row;
    class Detail;
    struct Selection { bool isBus = false; int strip = -1; MixBus bus = MixBus::Master; };

    MixController& controller;
    Selection selection;
    std::vector<std::unique_ptr<juce::Component>> listItems; // headers + rows, layout order
    std::vector<Row*> rows;                  // only the selectable rows, for refresh/selection
    juce::Viewport viewport;
    juce::Component listHolder;
    std::unique_ptr<Detail> detail;
    int builtForStrips = -1;
};

} // namespace livemix
