#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "AppTheme.h"
#include "native/MixController.h"
#include "native/ReferenceAudio.h"
#include "UI/Widgets.h"

namespace livemix
{

// REFERENCE MIX: "make it sound like this."
//
// Add a finished song and DLIVE aims the master at it - the tonal balance it has, how wide
// it sits, how dense it is - using the listen it already has of the band. The sheet's job
// is to be honest about the trade: it draws the reference's balance against this mix's,
// says in sentences what matching will aim for, and lists what a reference is not allowed
// to copy (how loud the stream is delivered, and who is loud inside the mix) before the
// button is pressed rather than after.
//
// Measuring a five-minute song takes a moment, so it happens on a thread of its own: a
// sheet that freezes the console while it thinks is not something to put in front of a
// volunteer during a service.
class ReferenceSheet : public juce::Component
{
public:
    explicit ReferenceSheet (MixController&);
    ~ReferenceSheet() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;

    void refresh();                       // 30 Hz from the page: the measuring thread, then the result
    void chooseFile();                    // also the File menu's "Add a Reference Mix..."
    // A song is still being read. The page keeps refreshing a hidden sheet in that state, and
    // will not put it away, so a reference chosen and then dismissed still arrives.
    bool isMeasuring() const noexcept { return measurer != nullptr; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Measurer;

    enum class State { Empty, Measuring, Chosen, Refused };
    State state() const;
    juce::Rectangle<int> cardBounds() const;
    void updateControls();
    ReferenceMatch preview() const;       // what matching would aim for, from the listen DLIVE already has
    int listHeight() const;               // exactly what the aims and the limits take, so the card fits them
    void drawBalance (juce::Graphics&, juce::Rectangle<int>) const;

    MixController& controller;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Measurer> measurer;
    juce::String measuringName;
    // What matching would aim for, worked out once a frame rather than once a paint: the card
    // is measured from it as well as drawn from it.
    ReferenceMatch shown;
    juce::String refusedReason, refusedGuidance, error;
    State lastState = State::Empty;

    DineButton choose { "Choose a song...", DineButton::Style::Filled };
    DineButton another { "Choose another...", DineButton::Style::Standard };
    DineButton remove { "Remove", DineButton::Style::Ghost };
    DineButton match { "Match to reference", DineButton::Style::Filled };
    DineButton close { "Close", DineButton::Style::Standard };
};

} // namespace livemix
