#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "MixController.h"
#include "MultitrackSource.h"

namespace livemix
{

// The only place DINELIVE touches an audio device. Wraps juce::AudioDeviceManager
// (CoreAudio on macOS: Dante Virtual Soundcard, USB consoles and interfaces all
// appear here) and forwards the callback to MixController::process(). Every input
// channel of the chosen device is enabled; the mix goes to the first output pair.
class AudioHost : private juce::AudioIODeviceCallback
{
public:
    explicit AudioHost (MixController& controller);
    ~AudioHost() override;

    struct DeviceInfo { juce::String name; int inputChannels = 0; int outputChannels = 0; };
    juce::Array<DeviceInfo> listInputDevices();
    juce::Array<DeviceInfo> listOutputDevices();

    // Opens the devices and starts the callback. Returns an empty string on success.
    juce::String open (const juce::String& inputDevice, const juce::String& outputDevice, double preferredSampleRate = 48000.0, int preferredBufferSize = 64);
    void close();
    bool isOpen() const noexcept { return running; }

    // Plays a folder of recorded stems as the inputs (the source must stay alive while open): output device only.
    juce::String openPlayback (MultitrackSource& source, const juce::String& outputDevice, int preferredBufferSize = 128);
    bool isPlayback() const noexcept { return playback != nullptr; }
    MultitrackSource* getPlayback() const noexcept { return playback; }

    // Stops the callback, re-prepares the controller for its current session, restarts. Call after the assignments change.
    void reconfigure();

    juce::String getInputDeviceName() const;
    juce::String getOutputDeviceName() const;
    int getNumInputChannels() const;
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
    juce::AudioDeviceManager deviceManager;
    bool running = false;
    juce::String lastError;
    MultitrackSource* playback = nullptr;
    juce::AudioBuffer<float> playbackBuffer;
    std::vector<const float*> playbackPtrs;
};

} // namespace livemix
