#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppTheme.h"
#include "native/MixController.h"
#include "UI/Widgets.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// MIX BUDDY (the panel the user sees as "Mix Buddy"): "bring the lead vocal forward"
//
// A place to say what you want in the words you would use to a person, and have the mix
// change. It is not a second mixing engine and it does not have opinions of its own: every
// sentence goes through the same pipeline TUNE LIVE MIX uses - intent, resolve against what
// DLIVE really has, check every action against the bounds - and what comes out is an
// ordinary MixPlan. So the answer arrives on BEFORE / AFTER exactly like a Tune, KEEP and
// REVERT decide it, and the Inspector shows every value it touched.
//
// What the sheet is *for* is the part a chat window usually gets wrong: you see what it
// intends to do, and on which channels, before it is yours. Nothing is committed by asking.
// The transcript keeps DLIVE's own account of each change, so a request three turns ago can
// still be read back and undone by name.
//
// It works with no account and no network: with no cloud model configured the request is
// read by DLIVE's own parser (MixAI/MixRequestParser), which is deterministic and offline -
// the machine in a church sound booth is very often not on the internet and the service
// starts anyway.
// ---------------------------------------------------------------------------
class ChatSheet : public juce::Component
{
public:
    explicit ChatSheet (MixController&);
    ~ChatSheet() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;

    void refresh();                 // 30 Hz from the page: the run's progress and its answer
    void takeFocus();               // opening the sheet puts the caret in the box

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Transcript;

    juce::Rectangle<int> cardBounds() const;
    void send();
    void updateControls();

    MixController& controller;
    std::unique_ptr<Transcript> transcript;
    std::unique_ptr<juce::Viewport> scroller;
    juce::TextEditor input;
    DineButton sendButton { "Ask", DineButton::Style::Filled };
    DineButton keepButton { "KEEP", DineButton::Style::Filled };
    DineButton revertButton { "REVERT", DineButton::Style::Standard };
    DineButton compareButton { "BEFORE", DineButton::Style::Standard };
    DineButton undoButton { "Undo", DineButton::Style::Standard };
    DineButton redoButton { "Redo", DineButton::Style::Standard };
    DineButton close { "Close", DineButton::Style::Standard };
    size_t shownTurns = 0;
    bool wasBusy = false;
    static constexpr int kNoteH = 96;   // the what-it-is-for note under the head
};

} // namespace livemix
