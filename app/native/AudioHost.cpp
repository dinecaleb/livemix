#include "AudioHost.h"

namespace livemix
{

AudioHost::AudioHost (MixController& c, DawEngine& d) : controller (c), daw (d)
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

void AudioHost::rescanDevices()
{
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type != nullptr) type->scanForDevices();
}

bool AudioHost::waitForOutputDevice (const juce::String& name, int timeoutMs)
{
    if (name.isEmpty()) return false;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);
    for (;;)
    {
        rescanDevices();
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type != nullptr && type->getDeviceNames (false).contains (name)) return true;
        if (juce::Time::getMillisecondCounter() >= deadline) return false;
        juce::Thread::sleep (80);
    }
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
    setup.outputChannels.setRange (0, kMaxOutputs, true);   // every pair the device has: the feeds decide what lands where
    lastError = deviceManager.setAudioDeviceSetup (setup, true);
    if (lastError.isNotEmpty()) return lastError;
    if (deviceManager.getCurrentAudioDevice() == nullptr) { lastError = "The audio device could not be opened."; return lastError; }
    deviceStopped.store (false);
    deviceManager.addAudioCallback (this);
    running = true;
    return {};
}

juce::String AudioHost::openOutputOnly (const juce::String& outputDevice, double preferredSampleRate, int preferredBufferSize)
{
    close();
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = {};
    setup.outputDeviceName = outputDevice;
    setup.sampleRate = preferredSampleRate;
    setup.bufferSize = preferredBufferSize;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setRange (0, kMaxOutputs, true);   // every pair the device has: the feeds decide what lands where
    lastError = deviceManager.setAudioDeviceSetup (setup, true);
    if (lastError.isNotEmpty()) return lastError;
    if (deviceManager.getCurrentAudioDevice() == nullptr) { lastError = "The output device could not be opened."; return lastError; }
    deviceStopped.store (false);
    deviceManager.addAudioCallback (this);
    running = true;
    return {};
}

juce::String AudioHost::setOutputDevice (const juce::String& outputDevice)
{
    if (! running) { lastError = "No audio device is open."; return lastError; }
    if (outputDevice.isEmpty()) { lastError = "Choose an output device."; return lastError; }
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.outputDeviceName == outputDevice) return {};

    closing = true;
    deviceManager.removeAudioCallback (this);
    setup.outputDeviceName = outputDevice;
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setRange (0, kMaxOutputs, true);   // every pair the device has: the feeds decide what lands where
    lastError = deviceManager.setAudioDeviceSetup (setup, true);
    closing = false;
    if (lastError.isNotEmpty())
    {
        // Best effort: put the callback back on whatever device remains.
        if (deviceManager.getCurrentAudioDevice() != nullptr) deviceManager.addAudioCallback (this);
        return lastError;
    }
    if (deviceManager.getCurrentAudioDevice() == nullptr) { lastError = "The output device could not be opened."; return lastError; }
    deviceStopped.store (false);
    deviceManager.addAudioCallback (this);   // aboutToStart -> prepare; caller restores the mix
    return {};
}

void AudioHost::close()
{
    closing = true;
    if (running)
    {
        deviceManager.removeAudioCallback (this);   // returns only when the callback is no longer running
        deviceManager.closeAudioDevice();
        running = false;
    }
    deviceStopped.store (false);
    closing = false;
}

void AudioHost::reconfigure()
{
    if (! running)
    {
        controller.prepare (controller.getSampleRate(), controller.getBlockSize());
        daw.prepare (controller.getSampleRate(), controller.getBlockSize());
        return;
    }
    closing = true;   // the stop that follows is ours
    deviceManager.removeAudioCallback (this);
    closing = false;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        controller.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
        daw.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    }
    deviceManager.addAudioCallback (this);
}

juce::String AudioHost::getInputDeviceName() const  { return deviceManager.getAudioDeviceSetup().inputDeviceName; }
juce::String AudioHost::getOutputDeviceName() const { return deviceManager.getAudioDeviceSetup().outputDeviceName; }

int AudioHost::getNumInputChannels() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice()) return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

int AudioHost::getNumOutputChannels() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice()) return device->getActiveOutputChannels().countNumberOfSetBits();
    return 0;
}

juce::StringArray AudioHost::getOutputChannelNames() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice()) return device->getOutputChannelNames();
    return {};
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
    daw.processBlock (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceStopped.store (false);
    // Called before the first callback, off the audio thread: the one place the graph is (re)built for the device.
    controller.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    daw.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioHost::audioDeviceStopped()
{
    // Our own close() and reconfigure() stop the device too; anything else is the device going away.
    if (running && ! closing) deviceStopped.store (true);
}

} // namespace livemix
