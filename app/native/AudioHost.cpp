#include "AudioHost.h"

namespace livemix
{

AudioHost::AudioHost (MixController& c) : controller (c)
{
    // Register the device types without opening anything yet.
    deviceManager.initialise (0, 0, nullptr, false);
}

AudioHost::~AudioHost()
{
    close();
}

juce::Array<AudioHost::DeviceInfo> AudioHost::listInputDevices()
{
    juce::Array<DeviceInfo> out;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        for (const auto& name : type->getDeviceNames (true))
        {
            DeviceInfo info;
            info.name = name;
            if (auto device = std::unique_ptr<juce::AudioIODevice> (type->createDevice ({}, name)))
                info.inputChannels = device->getInputChannelNames().size();
            out.add (info);
        }
    }
    return out;
}

juce::Array<AudioHost::DeviceInfo> AudioHost::listOutputDevices()
{
    juce::Array<DeviceInfo> out;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        for (const auto& name : type->getDeviceNames (false))
        {
            DeviceInfo info;
            info.name = name;
            if (auto device = std::unique_ptr<juce::AudioIODevice> (type->createDevice (name, {})))
                info.outputChannels = device->getOutputChannelNames().size();
            out.add (info);
        }
    }
    return out;
}

juce::String AudioHost::open (const juce::String& inputDevice, const juce::String& outputDevice, double preferredSampleRate, int preferredBufferSize)
{
    close();
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = inputDevice;
    setup.outputDeviceName = outputDevice;
    setup.sampleRate = preferredSampleRate;
    setup.bufferSize = preferredBufferSize;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.setRange (0, kMaxInputs, true);     // every input the device has, up to the engine's capacity
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setRange (0, 2, true);
    lastError = deviceManager.setAudioDeviceSetup (setup, true);
    if (lastError.isNotEmpty()) return lastError;
    if (deviceManager.getCurrentAudioDevice() == nullptr) { lastError = "The audio device could not be opened."; return lastError; }
    deviceManager.addAudioCallback (this);
    running = true;
    return {};
}

void AudioHost::close()
{
    if (running)
    {
        deviceManager.removeAudioCallback (this);   // returns only when the callback is no longer running
        deviceManager.closeAudioDevice();
        running = false;
    }
    if (playback != nullptr) { playback->release(); playback = nullptr; }
}

juce::String AudioHost::openPlayback (MultitrackSource& source, const juce::String& outputDevice, int preferredBufferSize)
{
    close();
    if (! source.isLoaded()) { lastError = "No recording is loaded."; return lastError; }
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = {};
    setup.outputDeviceName = outputDevice;
    setup.sampleRate = source.getFileSampleRate();
    setup.bufferSize = preferredBufferSize;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setRange (0, 2, true);
    playback = &source;
    lastError = deviceManager.setAudioDeviceSetup (setup, true);
    if (lastError.isNotEmpty()) { playback = nullptr; return lastError; }
    if (deviceManager.getCurrentAudioDevice() == nullptr) { playback = nullptr; lastError = "The output device could not be opened."; return lastError; }
    deviceManager.addAudioCallback (this);
    running = true;
    return {};
}

void AudioHost::reconfigure()
{
    if (! running)
    {
        controller.prepare (controller.getSampleRate(), controller.getBlockSize());
        return;
    }
    deviceManager.removeAudioCallback (this);
    if (auto* device = deviceManager.getCurrentAudioDevice())
        controller.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    deviceManager.addAudioCallback (this);
}

juce::String AudioHost::getInputDeviceName() const  { return playback != nullptr ? "Recording: " + playback->getFolder().getFileName() : deviceManager.getAudioDeviceSetup().inputDeviceName; }
juce::String AudioHost::getOutputDeviceName() const { return deviceManager.getAudioDeviceSetup().outputDeviceName; }

int AudioHost::getNumInputChannels() const
{
    if (playback != nullptr) return playback->getTotalChannels();
    if (auto* device = deviceManager.getCurrentAudioDevice()) return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

double AudioHost::getSampleRate() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice()) return device->getCurrentSampleRate();
    return 0.0;
}

int AudioHost::getBufferSize() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice()) return device->getCurrentBufferSizeSamples();
    return 0;
}

void AudioHost::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                  float* const* outputChannelData, int numOutputChannels,
                                                  int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    if (! controller.isPrepared())
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr) juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        return;
    }
    if (playback != nullptr)
    {
        // The recording is the console: fill the input layout from the files, then mix exactly as live.
        const int channels = playbackBuffer.getNumChannels();
        if (numSamples > playbackBuffer.getNumSamples() || channels == 0)
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (outputChannelData[ch] != nullptr) juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
            return;
        }
        playback->fillNext (playbackBuffer.getArrayOfWritePointers(), channels, numSamples);
        for (int ch = 0; ch < channels; ++ch) playbackPtrs[size_t (ch)] = playbackBuffer.getReadPointer (ch);
        controller.process (playbackPtrs.data(), channels, outputChannelData, numOutputChannels, numSamples);
        return;
    }
    controller.process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called before the first callback, off the audio thread: the one place the graph is (re)built for the device.
    controller.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    if (playback != nullptr)
    {
        playback->prepare (device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
        playbackBuffer.setSize (playback->getTotalChannels(), device->getCurrentBufferSizeSamples() * 4, false, true, true);
        playbackPtrs.assign (size_t (playback->getTotalChannels()), nullptr);
    }
}

void AudioHost::audioDeviceStopped() {}

} // namespace livemix
