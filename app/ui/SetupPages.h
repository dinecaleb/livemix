#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "UI/Widgets.h"

namespace livemix
{

// STEP 1: which device brings the inputs in. "32 inputs detected" -> CONTINUE.
class DevicePage : public juce::Component
{
public:
    DevicePage (MixController&, AppServices&);
    ~DevicePage() override;
    std::function<void()> onContinue;
    std::function<void()> onContinueToAssign;   // a recording was opened: review the guessed assignments
    void openRecording (const juce::File& folder);  // programmatic (snapshot tool / tests)
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DeviceRow;
    void select (int index);
    void chooseOutput (juce::Component& anchor);

    MixController& controller;
    AppServices& services;
    juce::Array<AppServices::Device> inputs, outputs;
    int selected = -1;
    juce::String outputName;
    juce::String error;
    std::vector<std::unique_ptr<DeviceRow>> rows;
    juce::Viewport viewport;
    juce::Component listHolder;
    DropdownButton outputButton { "OUTPUT" };
    FlatButton continueButton { "CONTINUE", FlatButton::Style::Solid };
    FlatButton rescanButton { "RESCAN", FlatButton::Style::Outline };
    FlatButton recordingButton { "PLAY A RECORDING...", FlatButton::Style::Outline };
    std::unique_ptr<juce::FileChooser> chooser;
};

// STEP 2: every device input on one row: number, name, source, stereo link. Fast:
// type a name, pick a source from a grouped menu, link a pair with one click.
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

    MixController& controller;
    AppServices& services;
    int numInputs = 0;
    struct Entry { juce::String name; bool assigned = false; ChannelRole role = ChannelRole::KickIn; bool linkedToNext = false; bool linkedFromPrevious = false; };
    std::vector<Entry> entries;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Viewport viewport;
    juce::Component listHolder;
    FlatButton continueButton { "CONTINUE", FlatButton::Style::Solid };
    FlatButton backButton { "BACK", FlatButton::Style::Ghost };
    FlatButton clearButton { "CLEAR ALL", FlatButton::Style::Ghost };
};

// STEP 3 + 4: WHAT ARE WE MIXING? and the sound. Two rows of big tiles, CONTINUE.
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
    MixController& controller;
    std::vector<std::unique_ptr<Tile>> purposeTiles, soundTiles;
    FlatButton continueButton { "CONTINUE", FlatButton::Style::Solid };
    FlatButton backButton { "BACK", FlatButton::Style::Ghost };
};

} // namespace livemix
