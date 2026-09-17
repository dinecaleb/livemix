#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "DawEngine.h"
#include "MixController.h"

namespace livemix
{

// The only place DLIVE touches an audio device. Wraps juce::AudioDeviceManager
// (CoreAudio on macOS: Dante Virtual Soundcard, USB consoles and interfaces all appear
// here) and hands every block to DawEngine, which records the raw inputs, plays the
// timeline back and mixes. Every output channel the device has (up to kMaxOutputs) is
// opened, so the mix can leave by more than one pair at once - see OutputFeeds.
class AudioHost : private juce::AudioIODeviceCallback
{
public:
    AudioHost (MixController& controller, DawEngine& daw);
    ~AudioHost() override;

    struct DeviceInfo { juce::String name; int inputChannels = 0; int outputChannels = 0; };
    juce::Array<DeviceInfo> listInputDevices();
    juce::Array<DeviceInfo> listOutputDevices();

    // Opens the devices and starts the callback. Returns an empty string on success.
    juce::String open (const juce::String& inputDevice, const juce::String& outputDevice,
                       double preferredSampleRate = 48000.0, int preferredBufferSize = 64);
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
    juce::StringArray getOutputChannelNames() const;

    // CoreAudio has gained or lost a device (DLIVE building its own combined output, an
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
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    MixController& controller;
    DawEngine& daw;
    juce::AudioDeviceManager deviceManager;
    bool running = false;
    bool closing = false;
    std::atomic<bool> deviceStopped { false };   // the device stopped without close(): unplugged, or taken by the system
    juce::String lastError;
};

} // namespace livemix
