#pragma once
#include <juce_core/juce_core.h>

namespace livemix
{

// ---------------------------------------------------------------------------
// WHERE A DEVICE ACTUALLY IS
//
// "No audio devices" was DLIVE's answer to four different situations, only one of which was
// true, and the one it was most often wrong about is the one that matters at 9:55 on a
// Sunday: the console is plugged in, macOS is refusing the microphone, and the app looks
// broken. So the states are named, and each one carries the sentence that says what to do.
//
// Enumeration never needs permission - listing devices and reading their channel counts are
// CoreAudio property queries - so `Absent` really does mean nothing is connected.
// ---------------------------------------------------------------------------
enum class DeviceStage
{
    Absent = 0,     // nothing by that name is connected
    Present,        // it is there; nothing has been chosen
    Selected,       // the session asks for it, and it has not been opened yet
    ChannelsKnown,  // enumerated: how many inputs and outputs it has is known
    OutputOpen,     // playing and mixing, but not hearing the inputs
    Open,           // inputs and outputs both running
    InputRefused,   // macOS will not let DLIVE hear the inputs
    Disconnected,   // it was open and went away
    Count
};

inline constexpr const char* deviceStageName (DeviceStage s) noexcept
{
    switch (s)
    {
        case DeviceStage::Absent:        return "not connected";
        case DeviceStage::Present:       return "connected";
        case DeviceStage::Selected:      return "chosen";
        case DeviceStage::ChannelsKnown: return "counted";
        case DeviceStage::OutputOpen:    return "playing only";
        case DeviceStage::Open:          return "running";
        case DeviceStage::InputRefused:  return "inputs refused";
        case DeviceStage::Disconnected:  return "disconnected";
        case DeviceStage::Count:
        default:                         return "?";
    }
}

struct DeviceState
{
    DeviceStage stage = DeviceStage::Absent;
    juce::String input, output;
    int inputChannels = 0, outputChannels = 0;
    juce::String why;        // the sentence, when the stage is not a happy one

    bool hearing() const noexcept { return stage == DeviceStage::Open && inputChannels > 0; }
    bool playing() const noexcept { return stage == DeviceStage::Open || stage == DeviceStage::OutputOpen; }
};

// WHY THE INPUTS ARE NOT BEING HEARD, in the words of what to do about it.
//
// Pure, so the sentence is decided in one place and tested without a device. `micDenied` is
// what the operating system says about the microphone, which is a different question from
// whether the stream opened - a device can refuse its inputs for its own reasons, and saying
// "check System Settings" when that is not the problem sends somebody to the wrong screen.
inline juce::String inputRefusedSentence (const juce::String& inputDevice, const juce::String& outputDevice,
                                          bool micDenied, const juce::String& deviceError)
{
    if (micDenied)
        return "DLIVE can play and mix, but macOS is not letting it hear the inputs. "
               "System Settings > Privacy & Security > Microphone, switch DLIVE on, and open it again."
               + (outputDevice.isNotEmpty() ? " The mix is going out of " + outputDevice + " in the meantime." : juce::String());

    juce::String s = inputDevice.isNotEmpty()
        ? "DLIVE can play and mix, but " + inputDevice + " would not open its inputs."
        : juce::String ("DLIVE can play and mix, but the inputs would not open.");
    if (deviceError.isNotEmpty()) s += " " + deviceError.trimCharactersAtEnd (".") + ".";
    if (outputDevice.isNotEmpty()) s += " The mix is going out of " + outputDevice + ".";
    return s;
}

// What the status line says about a device, whatever state it is in. Never "no audio devices"
// when there are devices: that sentence is only true for Absent with nothing connected.
inline juce::String deviceSentence (const DeviceState& d, bool anyDeviceConnected)
{
    switch (d.stage)
    {
        case DeviceStage::Open:
            return d.input + "  " + juce::String (d.inputChannels) + " in, " + juce::String (d.outputChannels) + " out";
        case DeviceStage::OutputOpen:
        case DeviceStage::InputRefused:
            return d.why.isNotEmpty() ? d.why : juce::String ("DLIVE can play and mix, but it is not hearing any inputs.");
        case DeviceStage::Disconnected:
            return (d.input.isNotEmpty() ? d.input : d.output) + " was unplugged. Pick a device under Audio device.";
        case DeviceStage::Selected:
        case DeviceStage::ChannelsKnown:
        case DeviceStage::Present:
            return "Ready to open. Pick a device under Audio device.";
        case DeviceStage::Absent:
        case DeviceStage::Count:
        default:
            return anyDeviceConnected ? juce::String ("No device is open. Pick one under Audio device.")
                                      : juce::String ("No audio devices are connected.");
    }
}

} // namespace livemix
