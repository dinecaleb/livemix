#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "AppTheme.h"

namespace livemix
{

// WHAT THIS WORKSPACE IS FOR, once.
//
// Getting started (`Tutorial`) is the tour somebody takes on their first Sunday, end to end.
// This is the other half of the same idea and it is smaller: the first time a workspace is
// opened, a card in the corner says in two sentences what the workspace is for and what the
// one thing to press is. It is dismissed for good with GOT IT, and every one of them can be
// switched off together with the second chip.
//
// It is a card, not a modal: nothing is blocked behind it, it never takes the keyboard, it
// sits out of the way of the thing it is describing, and it never touches the session. What
// has been dismissed lives beside the theme (`Guides`, app/native/ThemeStore), so a volunteer
// who has been shown MIXER once is not shown it again next Sunday - and Help > Show the guides
// again brings every one of them back.
class WorkspaceGuide : public juce::Component
{
public:
    // What one workspace says for itself.
    struct Entry
    {
        const char* key;        // the stable name Guides::seen() remembers
        const char* title;
        const char* body;
    };

    // The card for a workspace, or nullptr where there is nothing to say about it.
    static const Entry* entryFor (int page);

    WorkspaceGuide (const Entry&);

    std::function<void()> onDismiss;         // GOT IT: this one, for good
    std::function<void()> onTurnOff;         // NOT THESE: every one of them, for good

    // The card sits in the bottom-left of the workspace, clear of the chain foot.
    static constexpr int width = 340, pad = 18, gap = 20;
    int wantedHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, body;
    DineButton gotIt { "Got it", DineButton::Style::Filled };
    DineButton noMore { "Not these", DineButton::Style::Ghost };
};

} // namespace livemix
