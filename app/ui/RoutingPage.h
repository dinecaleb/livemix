#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"
#include "SetupPages.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// ROUTING (the v2 design, frame "02 - ROUTING").
//
// The three things that decide what the room hears - which device the sound comes in on,
// what each input is, and where the mix goes out - used to be three separate screens with
// Back and Continue between them. That is the right shape the first time somebody sets a
// service up and the wrong one every Sunday after it, when the question is never "what is
// step two" but "why is there nothing on channel 9".
//
// So they are one workspace, in three columns, the way the design draws it:
//
//   +----------------+  +----------------------------+  +----------------+
//   | AUDIO DEVICES  |  | INPUT MAP                  |  | OUTPUTS        |
//   | input access   |  | in - channel - source -    |  | broadcast,     |
//   | format         |  | group - rec - level        |  | headphones     |
//   |                |  |                            |  | MONITORING     |
//   +----------------+  +----------------------------+  +----------------+
//
// Nothing here is new behaviour: the columns are DevicePage and AssignPage themselves, shown
// embedded (`setEmbedded`) so they give up the title and the Back / Continue footer that
// belong to a whole page and keep everything else - the device list, the microphone answer,
// the saved input maps, the stereo pairs, the bulk actions. The third column states the
// output feeds and the monitoring and opens the Outputs sheet to change them.
//
// ROUTING is the design's "Deliberate" tab - outlined, divided off from the five mixing
// workspaces - because what happens here changes what the room hears, and under LIVE SAFE it
// says so before it does anything.
// ---------------------------------------------------------------------------
class RoutingPage : public juce::Component
{
public:
    RoutingPage (MixController&, AppServices&, DevicePage&, AssignPage&);
    ~RoutingPage() override;

    std::function<void()> onOpenOutputs;                 // the Outputs sheet
    std::function<void()> onSaveMapping, onApplyMapping;  // the saved input maps
    std::function<void()> onContinue;                     // first run: on to the purpose
    std::function<void (const juce::String&)> onToast;
    // LIVE SAFE is on and something here would change what the room hears. Returns true when
    // the user said to go ahead. The window owns the sentence, so it reads the same everywhere.
    std::function<bool (const juce::String& what)> onConfirmUnderLiveSafe;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    // Which columns are on screen at this width, for the snapshot tool and the layout test.
    bool showsInputMap() const noexcept;
    bool showsOutputs() const noexcept;

private:
    class OutputsColumn;

    MixController& controller;
    AppServices& services;
    DevicePage& devicePage;
    AssignPage& assignPage;

    std::unique_ptr<OutputsColumn> outputs;
    // The saved input maps: the same console patches itself the same way next Sunday.
    DineButton mapsButton { "Input map...", DineButton::Style::Standard };
    DineButton saveMapButton { "Save map", DineButton::Style::Standard };
    DineButton applyButton { "Apply", DineButton::Style::Filled };
    juce::String liveSafeNote;

    juce::Rectangle<int> headArea, deviceArea, mapArea, outputArea;
    void measure();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingPage)
};

} // namespace livemix
