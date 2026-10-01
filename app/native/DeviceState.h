#pragma once
#include <juce_core/juce_core.h>

namespace livemix
{

// ---------------------------------------------------------------------------
// WHERE A DEVICE ACTUALLY IS
//
// "No audio devices" was DINE's answer to four different situations, only one of which was
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
    InputRefused,   // macOS will not let DINE hear the inputs
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
        return "DINE can play and mix, but macOS is not letting it hear the inputs. "
               "System Settings > Privacy & Security > Microphone, switch DINE on, and open it again."
               + (outputDevice.isNotEmpty() ? " The mix is going out of " + outputDevice + " in the meantime." : juce::String());

    juce::String s = inputDevice.isNotEmpty()
        ? "DINE can play and mix, but " + inputDevice + " would not open its inputs."
        : juce::String ("DINE can play and mix, but the inputs would not open.");
    if (deviceError.isNotEmpty()) s += " " + deviceError.trimCharactersAtEnd (".") + ".";
    if (outputDevice.isNotEmpty()) s += " The mix is going out of " + outputDevice + ".";
    return s;
}

// NOTHING IS LISTENING, AND NOBODY WAS OVERRULED.
//
// macOS had never been asked about the microphone, DINE said what it wanted it for, and the
// answer was Not now. The output opened on its own, so the session opens, plays, mixes, saves
// and exports exactly as it would; the meters are still, and this is the sentence that says
// so. It is not a failure and it does not read like one - it names the one press that changes
// it, and it is the same press that picks a device.
inline juce::String inputsNotAskedSentence (const juce::String& inputDevice, const juce::String& outputDevice)
{
    juce::String s = "Nothing is being heard yet: "
                   + (inputDevice.isNotEmpty() ? inputDevice : juce::String ("your console"))
                   + " has not been opened for input.";
    if (outputDevice.isNotEmpty()) s += " The mix is going out of " + outputDevice + ".";
    return s + " Pick it under Audio device when you are ready to listen.";
}

// ---------------------------------------------------------------------------
// HOT-PLUG: a console pulled out mid-service, and put back
//
// Two questions, both decided here so they are decided once and can be tested with no device
// on the machine: may DINE open the device again by itself, and what does it say while it
// waits. A reopen is only ever the device the session already had - DINE never picks a
// different console because one happened to appear - and only after the one it had went away
// on its own. Everything else (nothing was open, the user closed it, a different device
// arrived) is somebody's decision to make, not DINE's.
// ---------------------------------------------------------------------------
struct DeviceReturn
{
    bool reopen = false;      // open it again, exactly as it was opened before
    juce::String what;        // the device the answer is about, for the sentence
};

inline DeviceReturn deviceReturned (const DeviceState& d,
                                    const juce::StringArray& connectedInputs,
                                    const juce::StringArray& connectedOutputs)
{
    DeviceReturn r;
    if (d.stage != DeviceStage::Disconnected) return r;      // nothing was lost; nothing to put back

    // Both halves. An output-only session asked for no input and waits for none; a session
    // with a console waits for the console *and* whatever it was playing out of, because
    // opening one without the other is a different session from the one that was running.
    if (d.input.isNotEmpty() && ! connectedInputs.contains (d.input)) return r;
    if (d.output.isNotEmpty() && ! connectedOutputs.contains (d.output)) return r;

    r.reopen = true;
    r.what = d.input.isNotEmpty() ? d.input : d.output;
    return r;
}

// What to say when a device that was open goes away in the middle of a service, and what to
// say when it comes back. Both name the device, because "the device" is not a thing an
// operator can go and look at.
inline juce::String deviceLostSentence (const DeviceState& d, bool recording)
{
    const auto what = d.input.isNotEmpty() ? d.input : d.output;
    juce::String s = (what.isNotEmpty() ? what : juce::String ("The audio device")) + " stopped. ";
    s += recording ? "The take so far is safe on disk and the session is untouched. "
                   : "Nothing about the mix or the session has changed. ";
    s += "DINE opens it again by itself the moment it comes back.";
    return s;
}

// Checked, not assumed: a Dante card or a desk's USB interface can come back in a different
// channel mode, and "every input is on the channel it was on" is only said when it is true.
// `inputsNeeded` / `outputsNeeded` are the highest device channel the session uses (from 1),
// 0 when it uses none.
inline juce::String deviceBackSentence (const juce::String& what, int inputChannels, int outputChannels,
                                        int inputsNeeded = 0, int outputsNeeded = 0)
{
    const auto name = what.isNotEmpty() ? what : juce::String ("The audio device");
    juce::String s = name + " is back";
    if (inputChannels > 0 || outputChannels > 0)
        s += ": " + juce::String (inputChannels) + " in, " + juce::String (outputChannels) + " out";
    const bool shortIn = inputsNeeded > inputChannels;
    const bool shortOut = outputsNeeded > outputChannels;
    if (! shortIn && ! shortOut) return s + ". Every input is on the channel it was on.";
    s += ". It came back with fewer channels than this session uses:";
    if (shortIn)
        s += " inputs " + juce::String (inputChannels + 1) + (inputsNeeded > inputChannels + 1 ? "-" + juce::String (inputsNeeded) : juce::String())
           + " are silent,";
    if (shortOut)
        s += " outputs above " + juce::String (outputChannels) + " are not playing,";
    s = s.trimCharactersAtEnd (",");
    return s + ". Check its channel count (a Dante or USB mode), then choose it again under Audio device.";
}

// Is the device that is connected under this name the unit the session was set up on? A UID
// that is known on both sides and differs is a different unit of the same model - never a
// reason to refuse it, always a reason to say so before the service.
inline bool sameUnit (const juce::String& savedUid, const juce::String& connectedUid) noexcept
{
    return savedUid.isEmpty() || connectedUid.isEmpty() || savedUid == connectedUid;
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
            return d.why.isNotEmpty() ? d.why : juce::String ("DINE can play and mix, but it is not hearing any inputs.");
        case DeviceStage::Disconnected:
            return (d.input.isNotEmpty() ? d.input : d.output)
                     + " was unplugged. DINE opens it again by itself when it comes back.";
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
