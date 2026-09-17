#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "AppTheme.h"

namespace livemix
{

// FIRST SUNDAY: what DLIVE says to somebody who has never opened it.
//
// The person in the booth twenty minutes before a service may be on their second ever
// shift. They do not know what a bus is, they will not read a manual, and the worst
// possible moment to teach them a word is the error message that uses it. So the app
// teaches itself once, in seven sentences of plain English, in the order the work actually
// happens: name the inputs, press record, let DLIVE listen, keep or undo what it did, and
// lock the desk before the service starts.
//
// It is a coach, not a wizard: nothing is blocked behind it, every step can be skipped, it
// never touches the session, and it puts the workspace it is talking about on screen so the
// sentence is read against the real thing rather than a picture of it. Each step can point
// at a rectangle of the window (`spot`), which is drawn as a lime ring in the scrim.
//
// It opens by itself the first time there is no library to open into, and after that only
// from Help > Getting started.
class Tutorial : public juce::Component
{
public:
    // What one step wants on screen: which workspace to show, and what to ring.
    struct Step
    {
        const char* eyebrow;
        const char* title;
        const char* body;
        int page;                // MainView::Page, as an int so this header stays independent
        const char* spot;        // a name the host resolves to a rectangle; "" = no ring
    };

    Tutorial();

    static const std::vector<Step>& steps();

    // The host shows the workspace and hands back the rectangle to ring (empty = none).
    std::function<void (int page)> onStep;
    std::function<juce::Rectangle<int> (const juce::String& spot)> spotFor;
    std::function<void()> onFinished;

    void start();                                  // from the beginning
    void go (int index);
    int index() const noexcept { return step; }

    // "Do not show this again", kept as a file beside the sessions so it survives a rebuild.
    static bool hasBeenSeen();
    static void markSeen();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override {}      // the scrim swallows clicks

private:
    static constexpr int kFooterH = 26;        // the button row along the foot of the card
    juce::Rectangle<int> cardBounds() const;

    int step = 0;
    juce::Rectangle<int> spot;
    DineButton backButton { "Back", DineButton::Style::Standard };
    DineButton nextButton { "Next", DineButton::Style::Filled };
    DineButton skipButton { "Skip the tour", DineButton::Style::Ghost };
};

} // namespace livemix
