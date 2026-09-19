#pragma once
#include <juce_core/juce_core.h>

namespace livemix
{

// ---------------------------------------------------------------------------
// "USE MY HEADPHONES"
//
// The problem this solves, in the words of the person who has it: the console arrives on a
// Dante Virtual Soundcard, the headphones are on a USB interface, and macOS will only open
// one audio device at a time. The usual answer is to go into Audio MIDI Setup and build a
// device by hand - which is four steps, in an application the volunteer has never opened,
// twenty minutes before a service.
//
// So DLIVE builds it. One button.
//
// The distinction that matters, and the reason solo could never be private before: a
// **Multi-Output Device** sends the *same* stereo to every device in it. One bus, mirrored -
// so there is no second pair for the engineer's listen to go to, and soloing a channel is
// necessarily something the congregation hears. An **Aggregate Device** concatenates the
// channels instead: the Dante is outputs 1..N and the interface is N+1 and N+2. Two real,
// separate pairs. That is what this builds, and it is what makes a private solo possible
// at all.
//
// Drift correction is switched on for the headphone device, because the Dante and a USB
// interface run off different clocks and without it the monitor ticks. It is the same
// checkbox Audio MIDI Setup offers, and getting it wrong is the commonest reason a
// hand-built aggregate device sounds broken.
//
// macOS only. On any other platform every call fails politely and the UI falls back to
// telling the user how to do it themselves.
// ---------------------------------------------------------------------------
namespace MonitorDevice
{
    // A real output device, with the UID an aggregate device is built from. JUCE gives names
    // but not UIDs, and a name is not an identity - two interfaces of the same model share one.
    struct Device
    {
        juce::String name;
        juce::String uid;
        int outputChannels = 0;
        bool isAggregate = false;       // already a combined device: never a building block
        bool isDliveBuilt = false;      // one of ours, from a previous run
        int inputChannels = 0;          // last, so the brace-initialised lists in the tests still read
    };

    bool available();                   // false off macOS: the caller offers the manual route
    juce::Array<Device> allDevices();   // every device, input-only ones included (the console may be one)
    juce::Array<Device> outputDevices();
    Device findDevice (const juce::String& name);   // empty uid when there is no such device

    // What DLIVE would pick if the user just presses the button. The broadcast is whatever is
    // already carrying the mix; the headphones are the best remaining real device - preferring
    // something that looks like an interface over the Mac's own speakers, because a booth has
    // headphones plugged into an interface and nobody wants their solo on the laptop speaker.
    struct Suggestion
    {
        bool valid = false;
        Device broadcast;
        Device headphones;
        juce::String why;               // shown before anything happens, in plain words
        juce::String problem;           // when there is nothing sensible to suggest
    };
    Suggestion suggest (const juce::String& currentOutputDeviceName);
    // The same choice, made from a list rather than from the machine. The rules are the half
    // of this worth testing - "prefer an interface over the laptop speaker" is a judgement,
    // not a platform call - so they live apart from CoreAudio and are tested without a device.
    Suggestion suggestFrom (const juce::Array<Device>&, const juce::String& currentOutputDeviceName);

    // The channel layout of the device DLIVE builds, decided before CoreAudio is asked for anything
    // and tested without it. CoreAudio lays an aggregate device's channels out in the order of its
    // pieces, so the order here *is* the channel map: the console's input device goes first (its
    // inputs keep the numbers every track and assignment already uses), then the broadcast, then the
    // headphones. A piece that plays two parts - the console is the broadcast, or the console is the
    // interface the headphones are in - appears once.
    //
    // Why the input device is in there at all: macOS opens one device per stream, and JUCE glues a
    // different input and output device together with a combiner of its own - two clocks, a FIFO,
    // and the console opened twice (once for its inputs, once inside the built device for its
    // outputs). With Dante that came back as silence. Putting the console inside the built device
    // makes it one CoreAudio device, opened once, for both directions.
    struct Layout
    {
        juce::Array<Device> pieces;     // sub-devices in channel order
        int broadcastChannel = 0;       // first output channel of the broadcast pair
        int headphoneChannel = 0;       // first output channel of the headphone pair
        bool carriesInput = false;      // the console's input device is a piece: open the built device for input too
        juce::String problem;           // when the two cannot be combined; empty otherwise
    };
    Layout layoutFor (const Device& broadcast, const Device& headphones, const Device* input);
    // Which of the built device's output channels to open, given the engine feeds at most `capacity`
    // of them: the solo pair always, and as much of the broadcast device as fits beside it. What the
    // engine and the feeds then see is the open channels, packed in device order - so a solo pair at
    // 64-65 behind sixty-four Dante channels is addressed as the pair after the fourteen Dante
    // channels that were opened, and never falls off the end of the engine.
    juce::BigInteger outputChannelsToOpen (const Layout& layout, int broadcastOutputs, int capacity);

    struct Result
    {
        bool ok = false;
        juce::String deviceName;        // open this one
        juce::String error;
        int broadcastChannel = 0;       // first channel of the broadcast pair, 0-based
        int headphoneChannel = 0;       // first channel of the headphone pair, 0-based
        bool carriesInput = false;      // open it for input as well: the console's inputs are inside it, first
        juce::String summary;           // one sentence for the toast
    };

    // Builds (or rebuilds) DLIVE's own aggregate device from these two - and the console's input
    // device, when there is one, so the built device is the only device open (see Layout). Replacing
    // one it made earlier is safe and is what happens when the interface changes; a device the *user*
    // made is never touched.
    Result combine (const Device& broadcast, const Device& headphones, const Device* input = nullptr);

    // Removes the device DLIVE built, if it exists. Used by "stop using my headphones" so the
    // Mac is left the way it was found.
    bool removeDliveDevice();
    bool dliveDeviceExists();

    // The manual route, for when creating fails or the platform has no aggregate devices.
    void openAudioMidiSetup();
    juce::StringArray manualSteps();
}

} // namespace livemix
