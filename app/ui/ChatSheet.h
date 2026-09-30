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
// MIX BUDDY: "why can't I hear channel 14?", "how do I save this mix?"
//
// The experienced DLIVE engineer sitting beside you. Ask how to do something, or why something
// sounds the way it does, and the answer is read from the session as it is right now - the
// input arriving, the fader, the group, the compressor, the master - and says what it found.
//
// It does not change the mix. TUNE MIX improves a mix and Autopilot holds one; Mix Buddy
// explains and shows you where (src/MixAI/MixBuddy.h has the boundary). Each answer ends in
// buttons - show me where, open the channel, solo it in your headphones, TUNE it - and the one
// that can change anything, a proposed change, goes to BEFORE / AFTER like every other
// proposal: KEEP and REVERT, here or on TUNE, decide it, and LIVE SAFE refuses it.
//
// It works with no account and no network: the answers are DLIVE's own, deterministic, and
// the machine in a church sound booth is very often not on the internet.
// ---------------------------------------------------------------------------
class ChatSheet : public juce::Component
{
public:
    explicit ChatSheet (MixController&);
    ~ChatSheet() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;
    // A button in an answer was pressed. MainView does what it says (MainView::performBuddyAction).
    std::function<void (const BuddyAction&)> onAction;

    void refresh();                 // 30 Hz from the page: the answer to a proposed change
    void takeFocus();               // opening the sheet puts the caret in the box
    void ask (const juce::String& question);   // as though it had been typed (the snapshot tool uses it)

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
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
    DineButton close { "Close", DineButton::Style::Standard };
    size_t shownTurns = 0;
    bool wasBusy = false;
    bool wasProposing = false;
    static constexpr int kNoteH = 96;   // the what-it-is-for note under the head
};

} // namespace livemix
