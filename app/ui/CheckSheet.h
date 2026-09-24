#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"

namespace livemix
{

// CHECK INPUTS: the page you look at at 9:55. Every assigned input on one sheet - its name,
// what it is, the level it is reaching right now, and one word about it: OK, SILENT (nothing
// above -60 dBFS for three seconds), LOW (never above -30), HOT (over -6) or CLIP (the
// converter clipped since the sheet opened). The headline counts them, so "13 inputs, 11 OK,
// 2 silent" is read before any strip is. Reading only: nothing here changes the mix.
class CheckSheet : public juce::Component
{
public:
    CheckSheet (MixController&, AppServices&);

    std::function<void()> onClose;

    void refresh();                       // 30 Hz from MainView: the meters
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    enum class State { Silent, Low, Ok, Hot, Clip };
    struct Row
    {
        juce::String name, role;
        float peakDb = -120.0f;           // the loudest moment in the last second
        float holdDb = -120.0f;           // the loudest since the sheet opened
        double lastHeard = -1.0;          // seconds since the sheet opened when it was last above -60
        bool clipped = false;
        State state = State::Silent;
    };
    const std::vector<Row>& getRows() const noexcept { return rows; }
    juce::String headline() const;

private:
    juce::Rectangle<int> cardBounds() const;
    void resetClips();

    MixController& controller;
    AppServices& services;
    std::vector<Row> rows;
    double opened = 0.0, now = 0.0;
    int ticks = 0;
    DineButton resetButton { "Reset clips", DineButton::Style::Standard };
    DineButton doneButton { "Close", DineButton::Style::Standard };
    static constexpr int kCardW = 720, kRowH = 30;
};

} // namespace livemix
