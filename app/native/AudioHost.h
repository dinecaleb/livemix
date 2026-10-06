#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "DawEngine.h"
#include "MixController.h"
#include "Core/Realtime.h"
#include "DeviceState.h"

namespace livemix
{

// The only place DINE touches an audio device. Wraps juce::AudioDeviceManager
// (CoreAudio on macOS: Dante Virtual Soundcard, USB consoles and interfaces all appear
// here) and hands every block to DawEngine, which records the raw inputs, plays the
// timeline back and mixes. Every output channel the device has (up to kMaxOutputs) is
// opened, so the mix can leave by more than one pair at once - see OutputFeeds.
class AudioHost : private juce::AudioIODeviceCallback,
                  private juce::AudioIODeviceType::Listener,
                  private juce::AsyncUpdater
{
public:
    AudioHost (MixController& controller, DawEngine& daw);
    ~AudioHost() override;

    struct DeviceInfo { juce::String name; int inputChannels = 0; int outputChannels = 0; };
    juce::Array<DeviceInfo> listInputDevices();
    juce::Array<DeviceInfo> listOutputDevices();

    // Opens the devices and starts the callback. Returns an empty string on success.
    // `outputChannels` says which of the device's output channels to open (empty = the first
    // kMaxOutputs). The engine and the feeds count the *open* channels, in device order - so with
    // channels 1-2 and 65-66 open, the feeds address them as 0-1 and 2-3 (see slotForOutputChannel).
    // This is what lets solo reach a pair that sits past sixty-four Dante channels.
    // If the input side will not open - macOS refusing the microphone, a device that will not
    // give up its inputs - DINE opens the output alone rather than refusing, so the session
    // still plays, still mixes and still saves. The returned string is empty in that case too;
    // `state()` says what happened and carries the sentence. A hard failure (no output either)
    // returns the device's own error.
    //
    // The input is only opened when macOS has already said yes (inputAccessFor, DeviceState.h):
    // so no call here ever puts the system prompt up by itself, and a refused input is never
    // opened silent. `listen` false is "Not now": the input is remembered, not opened.
    juce::String open (const juce::String& inputDevice, const juce::String& outputDevice,
                       double preferredSampleRate = 48000.0, int preferredBufferSize = 64,
                       const juce::BigInteger& outputChannels = {}, bool listen = true);

    // Why the input is held back, when it is (Listen = it is not, or there is none).
    InputAccess inputHeldBack() const noexcept { return inputHeld; }
    // macOS has said yes since the input was held back (the prompt was answered, or the switch
    // turned on in System Settings): open it, exactly as the session asked. True if it did.
    // Message thread; never during a take, which a device reopen would interrupt.
    bool retryHeldInput (bool recording);

    // ---- HOT-PLUG: a console unplugged mid-service, and put back ----
    //
    // CoreAudio tells JUCE when the device list changes and JUCE tells every device type's
    // listeners; a device that is open and goes away stops the callback. Both arrive on
    // threads DINE does not own, so both only set a flag here and the work happens on the
    // message thread through AsyncUpdater. What the work is: rescan (JUCE caches the list per
    // device type, so a device that appeared since the last scan does not exist as far as it
    // is concerned), and if the device this session was opened with has come back, open it
    // again exactly as it was opened - same rate, same buffer, same output channels.
    //
    // DINE never opens a device it was not already using. `deviceReturned` in DeviceState.h
    // is that rule, and it is a pure function so it is tested with no hardware at all.
    //
    // The mix, the assignments and the timeline are untouched by any of this: reopening calls
    // prepare(), which builds the audio graph and does not decide whether the session's state
    // exists (docs/SESSION-STATE.md). So every input is on the channel it was on.
    //
    // Both are called on the message thread, after the state has settled.
    std::function<void (DeviceState)> onDeviceLost;       // it was open and went away
    std::function<void (DeviceState)> onDeviceReturned;   // ... and it is open again
    std::function<void()> onDeviceListChanged;            // something was plugged in or pulled out
    // A device with the lost one's name came back, but it is a different unit (its CoreAudio
    // UID differs): DINE does not open it by itself, because its channels are not the
    // session's channels. Said once, with the name.
    std::function<void (juce::String)> onDifferentUnitReturned;

    // Open it again by itself when it comes back. On by default; off is for a tool or a test
    // that must not have a device opened behind it.
    void setReopenOnReturn (bool on) noexcept { reopenOnReturn = on; }
    bool reopensOnReturn() const noexcept { return reopenOnReturn; }
    // Do the check now rather than waiting for CoreAudio to say something. Returns true when a
    // device was opened again.
    bool checkForReturnedDevice();

    // Where the device actually is, and why, in one place. See DeviceState.h.
    DeviceState state() const;
    // Where a device output channel sits among the open ones (what a feed addresses); -1 if it is not open.
    int slotForOutputChannel (int deviceChannel) const;
    // Output only: playing a recorded session back with no console connected.
    juce::String openOutputOnly (const juce::String& outputDevice,
                                 double preferredSampleRate = 48000.0, int preferredBufferSize = 128);
    // Swap the stereo output while keeping the same input graph. Caller should snapshot/restore
    // the kept mix around this (prepare rebuilds the graph). Empty string on success.
    juce::String setOutputDevice (const juce::String& outputDevice);
    void close();
    bool isOpen() const noexcept { return running && ! deviceStopped.load (std::memory_order_relaxed); }
    bool deviceStoppedUnexpectedly() const noexcept { return running && deviceStopped.load (std::memory_order_relaxed); }

    // Stops the callback, re-prepares the controller for its current session, restarts. Call after the assignments change.
    void reconfigure();

    juce::String getInputDeviceName() const;
    juce::String getOutputDeviceName() const;
    int getNumInputChannels() const;
    int getNumOutputChannels() const;
    juce::StringArray getOutputChannelNames() const;   // the open channels' names, in the order the feeds address them

    // CoreAudio has gained or lost a device (DINE building its own combined output, an
    // interface plugged in). JUCE caches the device list inside each AudioIODeviceType, so a
    // device that appeared after the last scan does not exist as far as it is concerned -
    // which is why opening one straight after creating it fails with "No such device".
    void rescanDevices();
    // Wait for a named output device to turn up, asking again as it goes. A newly created
    // aggregate device is published asynchronously, so "it is not there" immediately after
    // making it is a timing answer rather than a real one.
    bool waitForOutputDevice (const juce::String& name, int timeoutMs = 4000);
    double getSampleRate() const;
    int getBufferSize() const;
    int getXRunCount() const { return deviceManager.getXRunCount(); }
    juce::String getLastError() const { return lastError; }

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) noexcept LIVEMIX_NONBLOCKING override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceListChanged() override;      // juce::AudioIODeviceType::Listener
    void handleAsyncUpdate() override;           // ... both, back on the message thread

    // What open() was last asked for, so the device that comes back is opened the way it was.
    struct OpenRequest
    {
        bool valid = false, outputOnly = false;
        juce::String input, output;
        double sampleRate = 48000.0;
        int bufferSize = 64;
        juce::BigInteger outputChannels;
        juce::String inputUid, outputUid;       // which physical unit was opened (MonitorDevice), when known
    };
    OpenRequest lastRequest;
    bool impostorAnnounced = false;             // a different unit under the same name has been reported
    bool reopenOnReturn = true;
    bool lostAnnounced = false;                  // onDeviceLost has been told about this one
    // The device that was open went away and has not been opened again. It outlives the
    // failed attempts to reopen it - which is the whole point: without it, one attempt that
    // came a moment too early would leave the session looking as if nobody had chosen a
    // device, and no later plug-in event would put it back.
    bool lost = false;
    std::atomic<bool> listChanged { false };
    // The device stopped, without deciding what that means. close() is the app saying it is
    // done with this device; open() is on its way to a new one.
    void stopDevice();

    MixController& controller;
    DawEngine& daw;
    juce::AudioDeviceManager deviceManager;
    bool running = false;
    bool closing = false;
    std::atomic<bool> deviceStopped { false };   // the device stopped without close(): unplugged, or taken by the system
    juce::String lastError;
    // The inputs were asked for and did not come. Kept so state() can say so, and so the UI
    // does not have to guess from a channel count of zero (a legitimate output-only session
    // has that too).
    bool inputRefused = false;
    InputAccess inputHeld = InputAccess::Listen;
    juce::String inputRefusedWhy;
    juce::String wantedInput;                    // what was asked for, for the sentence
};

} // namespace livemix
