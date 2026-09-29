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
    // One line of the list. Three kinds, because a favourite, a place to go back to and the
    // caption between them are the same shape on screen and there is no sense in three classes.
    struct Row
    {
        enum class Kind { Caption, Favourite, Checkpoint };
        Kind kind = Kind::Checkpoint;
        int index = 0;                 // into getCheckpoints(), or the favourite's own index
        juce::String when, what;
        bool fromTune = false;
        bool sameConsole = true;       // false: kept with different inputs, so it cannot go back on
        bool measured = false;         // a favourite DLIVE heard, so it can be aimed at
        std::unique_ptr<DineButton> restore;
        std::unique_ptr<DineButton> aim;      // favourites only: aim the mix at this one
        std::unique_ptr<DineButton> drop;     // favourites only: it is not a favourite any more
    };

    static int rowHeight (const Row&) noexcept;
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
    // MARK AS FAVOURITE: the mix that is running, kept by name with what it sounds like
    // measured beside it, so a later mix can be aimed at it.
    DineButton favouriteButton { "Mark this mix as a favourite", DineButton::Style::Filled };
    std::unique_ptr<juce::AlertWindow> nameDialog;
    void askForFavouriteName();
    size_t builtFor = 0;
    int builtFavourites = -1;
    static constexpr int kCardW = 660, kRowH = 42, kCaptionH = 26;
};

} // namespace livemix
