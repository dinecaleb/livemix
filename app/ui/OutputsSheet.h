#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// Where the sound leaves this Mac. A sheet over the workspace: the output device, then one
// row per feed - what it carries, which pair of device outputs it leaves by, its level, mono
// and mute. Feed 1 is the main output and cannot be removed.
//
// Monitoring only: nothing here changes the mix, the plan or an export. Two different
// devices at once (an interface and the headphone jack) is a macOS Aggregate Device, which
// then appears here as one device with every channel - the sheet says so and can open
// Audio MIDI Setup.
class OutputsSheet : public juce::Component
{
public:
    OutputsSheet (MixController&, AppServices&);
    ~OutputsSheet() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;
    std::function<void (const juce::String& device)> onChooseDevice;   // the host reopens the device

    void refresh();                       // device, channel count and the feeds
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Row;

    juce::Rectangle<int> cardBounds() const;
    void commit();                        // rows -> controller (and the engine)
    void removeFeed (int index);
    int numPairs() const;
    bool pairExists (int pair) const;
    bool busAvailable (MixBus) const;
    juce::String pairName (int pair) const;
    void chooseDevice();
    void addFeed();
    // The second of the two choices this sheet exists for: which device the engineer listens
    // on. Different from the broadcast is the normal case; the host joins the two.
    void chooseSoloDevice();

    MixController& controller;
    AppServices& services;

    std::array<std::unique_ptr<Row>, kMaxOutputFeeds> rows;
    DinePopup deviceButton;
    DineButton addButton { "Add an output", DineButton::Style::Standard };
    DinePopup soloDeviceButton;
    DineButton doneButton { "Done", DineButton::Style::Filled };
    int channels = 0;
};

} // namespace livemix
