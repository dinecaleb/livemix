#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"
#include "native/MixController.h"

namespace livemix
{

// MIX HISTORY: the whole mix as it was, hours ago, by name.
//
// Not UNDO. Undo is for the last thing you did; this is the list you open on Monday - or in
// the ten minutes between the rehearsal and the service - to find the mix that was working.
// Newest first, with the time, what made it, and one button. Going back is itself remembered,
// so the list never costs anything to look at.
class HistorySheet : public juce::Component
{
public:
    HistorySheet (MixController&, AppServices&);

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Row
    {
        int index = 0;                 // into MixController::getCheckpoints()
        juce::String when, what;
        bool fromTune = false;
        bool sameConsole = true;       // false: kept with different inputs, so it cannot go back on
        std::unique_ptr<DineButton> restore;
    };

    juce::Rectangle<int> cardBounds() const;
    void rebuild();
    void paintRows (juce::Graphics&);

    // The scrolled body. Its rows are drawn here rather than as components: a line of text, a
    // time and one button is not worth a class each, and the sheet's `Look` moves with it.
    struct ListBody : juce::Component
    {
        explicit ListBody (HistorySheet& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintRows (g); }
        HistorySheet& owner;
    };

    MixController& controller;
    AppServices& services;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Viewport viewport;
    ListBody list { *this };
    DineButton doneButton { "Close", DineButton::Style::Standard };
    size_t builtFor = 0;
    static constexpr int kCardW = 660, kRowH = 42;
};

} // namespace livemix
