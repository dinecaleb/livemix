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

// STEP 1: which device brings the inputs in. The list, the output pair, the clock,
// and a recording to practise with -> Continue.
class DevicePage : public juce::Component
{
public:
    DevicePage (MixController&, AppServices&);
    ~DevicePage() override;
    std::function<void()> onContinue;
    std::function<void()> onContinueToAssign;                  // an import happened: review the guessed assignments
    std::function<void (const juce::File&)> onImportRecording; // a folder of stems becomes tracks and clips
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DeviceRow;
    void select (int index);
    void chooseOutput (juce::Component& anchor);
    juce::Rectangle<int> body() const;

    MixController& controller;
    AppServices& services;
    juce::Array<AppServices::Device> inputs, outputs;
    int selected = -1;
    juce::String outputName;
    juce::String error;
    std::vector<std::unique_ptr<DeviceRow>> rows;
    juce::Viewport viewport;
    juce::Component listHolder;
    DinePopup outputButton;
    DineButton continueButton { "Continue", DineButton::Style::Filled };
    DineButton rescanButton { "Rescan", DineButton::Style::Standard };
    DineButton recordingButton { "Import a multitrack...", DineButton::Style::Standard };
    std::unique_ptr<juce::FileChooser> chooser;
};

// STEP 2: every device input on one row: number, name, source, stereo pair, bus.
class AssignPage : public juce::Component
{
public:
    AssignPage (MixController&, AppServices&);
    ~AssignPage() override;
    std::function<void()> onContinue, onBack;
    void refresh();                       // rebuild rows from the controller's session and the device's channel count
    void paint (juce::Graphics&) override;
    void resized() override;

    // Programmatic equivalents of the user's edits (also used by the snapshot tool).
    void assign (int input, ChannelRole role, const juce::String& name, bool linkWithNext = false);
    void clearAll();

private:
    class Row;
    void rebuild();
    void commit();                        // rows -> controller session
    void showSourceMenu (int input, juce::Component& anchor);
    int assignedCount() const;
    juce::Rectangle<int> body() const;

    MixController& controller;
    AppServices& services;
    int numInputs = 0;
    struct Entry { juce::String name; bool assigned = false; ChannelRole role = ChannelRole::KickIn; bool linkedToNext = false; bool linkedFromPrevious = false; };
    std::vector<Entry> entries;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Viewport viewport;
    juce::Component listHolder;
    DineButton continueButton { "Continue", DineButton::Style::Filled };
    DineButton backButton { "Back", DineButton::Style::Standard };
    DineButton clearButton { "Clear", DineButton::Style::Ghost };
};

// STEP 3: purpose (loudness and peaks) and sound (character), as picker cards.
class PurposePage : public juce::Component
{
public:
    PurposePage (MixController&);
    ~PurposePage() override;
    std::function<void()> onContinue, onBack;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Tile;
    juce::Rectangle<int> body() const;

    MixController& controller;
    std::vector<std::unique_ptr<Tile>> purposeTiles, soundTiles;
    DineButton continueButton { "Open the session", DineButton::Style::Filled };
    DineButton backButton { "Back", DineButton::Style::Standard };
};

} // namespace livemix
