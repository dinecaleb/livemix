#include "AudioHost.h"
#include "MicPermission.h"
#include "MonitorDevice.h"

namespace livemix
{

AudioHost::AudioHost (MixController& c, DawEngine& d) : controller (c), daw (d)
{
    // Register the device types without opening anything yet.
    deviceManager.initialise (0, 0, nullptr, false);
    // ... and ask each of them to say when something is plugged in or pulled out.
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type != nullptr) type->addListener (this);
}

AudioHost::~AudioHost()
{
    cancelPendingUpdate();
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type != nullptr) type->removeListener (this);
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

juce::String AudioHost::open (const juce::String& inputDevice, const juce::String& outputDevice, double preferredSampleRate, int preferredBufferSize,
                              const juce::BigInteger& outputChannels, bool listen)
{
    stopDevice();
    inputRefused = false;
    inputHeld = InputAccess::Listen;
    inputRefusedWhy.clear();
    wantedInput = inputDevice;
    lostAnnounced = false;
    // Remembered before the attempt, not after: a device that comes back has to be opened the
    // way it was asked for, which is not always the way it ended up (the input half may have
    // been refused and the output opened alone).
    lastRequest = { true, false, inputDevice, outputDevice, preferredSampleRate, preferredBufferSize, outputChannels,
                    MonitorDevice::findDevice (inputDevice).uid, MonitorDevice::findDevice (outputDevice).uid };
    impostorAnnounced = false;

    // MACOS FIRST. The input is only opened once macOS has said yes; otherwise the output opens
    // alone and the reason is the true one (DeviceState.h, inputAccessFor).
    if (inputDevice.isNotEmpty())
    {
        const auto access = inputAccessFor (MicPermission::check(), listen);
        if (access != InputAccess::Listen)
        {
            const auto why = access == InputAccess::AskFirst   ? inputsNotAskedSentence (inputDevice, outputDevice)
                           : access == InputAccess::Restricted ? inputRestrictedSentence (outputDevice)
                                                               : inputRefusedSentence (inputDevice, outputDevice, true, {});
            if (outputDevice.isEmpty()) { lastError = why; return lastError; }
            const auto fallback = openOutputOnly (outputDevice, preferredSampleRate, preferredBufferSize);
            if (fallback.isNotEmpty()) { lastError = fallback; return lastError; }
            inputRefused = true;
            inputHeld = access;
            inputRefusedWhy = why;
            return {};
        }
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = inputDevice;
    setup.outputDeviceName = outputDevice;
    setup.sampleRate = preferredSampleRate;
    setup.bufferSize = preferredBufferSize;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.setRange (0, kMaxInputs, true);     // every input the device has, up to the engine's capacity
    setup.useDefaultOutputChannels = false;
    if (outputChannels.isZero())
        setup.outputChannels.setRange (0, kMaxOutputs, true);   // every pair the device has: the feeds decide what lands where
    else
        setup.outputChannels = outputChannels;                  // exactly the pairs the feeds need (a solo pair past channel 16)
    lastError = deviceManager.setAudioDeviceSetup (setup, true);

    const bool opened = lastError.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr;
    // Input and output are asked for in one call, and CoreAudio answers all or nothing. If the
    // input half is what failed, refusing the whole thing would mean a session with a perfectly
    // good output cannot be played, mixed or even looked at - which is how "no audio devices"
    // came to be DINE's answer to a microphone switch. So the output opens on its own, and
    // state() carries the sentence that says why the meters are still.
    if (! opened && inputDevice.isNotEmpty() && outputDevice.isNotEmpty())
    {
        const auto inputError = lastError.isNotEmpty() ? lastError : juce::String ("The audio device could not be opened.");
        const auto fallback = openOutputOnly (outputDevice, preferredSampleRate, preferredBufferSize);
        if (fallback.isEmpty())
        {
            // macOS said yes (or the input would not have been tried), so this is the device's
            // own refusal - busy, a rate it will not share, Dante not running - and its own words.
            inputRefused = true;
            inputRefusedWhy = inputRefusedSentence (inputDevice, outputDevice, false, inputError);
            lastError.clear();
            return {};
        }
        lastError = inputError;      // neither half would open: that is a real failure
        return lastError;
    }

    if (! opened)
    {
        if (lastError.isEmpty()) lastError = "The audio device could not be opened.";
        return lastError;
    }
    deviceStopped.store (false);
    deviceManager.addAudioCallback (this);
    running = true;
    lost = false;
    return {};
}

DeviceState AudioHost::state() const
{
    DeviceState d;
    d.input = wantedInput.isNotEmpty() ? wantedInput : getInputDeviceName();
    d.output = getOutputDeviceName();
    d.inputChannels = getNumInputChannels();
    d.outputChannels = getNumOutputChannels();
    if (deviceStoppedUnexpectedly()) { d.stage = DeviceStage::Disconnected; return d; }
    // Lost and not open again yet - including the moment just after an attempt to reopen it
    // failed, when nothing is running and the device manager has forgotten the names. The
    // session's own request is what says which device is being waited for.
    if (lost && ! running)
    {
        d.stage = DeviceStage::Disconnected;
        if (lastRequest.valid)
        {
            d.input = lastRequest.input;
            d.output = lastRequest.output;
        }
        d.inputChannels = 0;
        d.outputChannels = 0;
        return d;
    }
    if (inputRefused)
    {
        d.stage = DeviceStage::InputRefused;
        d.why = inputRefusedWhy;
        return d;
    }
    if (! running) { d.stage = d.output.isNotEmpty() ? DeviceStage::Selected : DeviceStage::Absent; return d; }
    d.stage = d.inputChannels > 0 ? DeviceStage::Open : DeviceStage::OutputOpen;
    return d;
}

juce::String AudioHost::openOutputOnly (const juce::String& outputDevice, double preferredSampleRate, int preferredBufferSize)
{
    // The fallback inside open() calls this; that call must not overwrite what open() was
    // asked for, or a console that comes back would be reopened as an output on its own.
    const bool fromOpen = lastRequest.valid && ! lastRequest.outputOnly && lastRequest.output == outputDevice;
    stopDevice();
    inputRefused = false;
    inputHeld = InputAccess::Listen;
    inputRefusedWhy.clear();
    if (! fromOpen)
    {
        lostAnnounced = false;
        lastRequest = { true, true, {}, outputDevice, preferredSampleRate, preferredBufferSize, {},
                        {}, MonitorDevice::findDevice (outputDevice).uid };
        impostorAnnounced = false;
    }
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
    lost = false;
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

void AudioHost::stopDevice()
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

void AudioHost::close()
{
    // The application is done with this device, rather than on its way to another one. So
    // nothing is waiting for it to come back: a device DINE was told to let go of is not one
    // it should open again by itself half an hour later.
    stopDevice();
    lost = false;
    lostAnnounced = false;
    lastRequest.valid = false;
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
    // In the order the callback hands them over, which is the order the feeds address them: the
    // open channels, ascending. With the first sixteen open that is the device's own list.
    juce::StringArray names;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto all = device->getOutputChannelNames();
        const auto active = device->getActiveOutputChannels();
        for (int c = 0; c < all.size(); ++c)
            if (active[c]) names.add (all[c]);
    }
    return names;
}

int AudioHost::slotForOutputChannel (int deviceChannel) const
{
    if (deviceChannel < 0) return -1;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto active = device->getActiveOutputChannels();
        if (! active[deviceChannel]) return -1;
        int slot = 0;
        for (int c = 0; c < deviceChannel; ++c) if (active[c]) ++slot;
        return slot;
    }
    return -1;
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
                                                  int numSamples, const juce::AudioIODeviceCallbackContext&) noexcept LIVEMIX_NONBLOCKING
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
    // What it is really running at, so a device that goes away and comes back is opened the way
    // it was last running - a buffer or a rate chosen on the Audio device page since it was
    // opened included - rather than the way it was first asked for.
    if (lastRequest.valid)
    {
        lastRequest.sampleRate = device->getCurrentSampleRate();
        lastRequest.bufferSize = device->getCurrentBufferSizeSamples();
    }
}

void AudioHost::audioDeviceStopped()
{
    // Our own close() and reconfigure() stop the device too; anything else is the device going away.
    if (running && ! closing)
    {
        deviceStopped.store (true);
        lost = true;
        triggerAsyncUpdate();     // this is the device's thread: the sentence happens elsewhere
    }
}

// ---------------------------------------------------------------------- hot-plug
void AudioHost::audioDeviceListChanged()
{
    listChanged.store (true);
    triggerAsyncUpdate();
}

void AudioHost::handleAsyncUpdate()
{
    // The message thread, always. Everything below may open and close devices.
    if (listChanged.exchange (false))
    {
        rescanDevices();                 // JUCE caches the list per device type
        if (onDeviceListChanged) onDeviceListChanged();
    }

    if (deviceStoppedUnexpectedly() && ! lostAnnounced)
    {
        lostAnnounced = true;
        if (onDeviceLost) onDeviceLost (state());
    }

    checkForReturnedDevice();
}

bool AudioHost::retryHeldInput (bool recording)
{
    if (recording || ! inputRefused || inputHeld == InputAccess::Listen) return false;
    if (! lastRequest.valid || lastRequest.outputOnly || lastRequest.input.isEmpty()) return false;
    if (MicPermission::check() != MicPermission::State::Granted) return false;
    const auto want = lastRequest;
    return open (want.input, want.output, want.sampleRate, want.bufferSize, want.outputChannels).isEmpty()
        && ! inputRefused;
}

bool AudioHost::checkForReturnedDevice()
{
    if (! reopenOnReturn || ! lastRequest.valid) return false;

    const auto before = state();
    juce::StringArray inputs, outputs;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type == nullptr) continue;
        inputs.addArray (type->getDeviceNames (true));
        outputs.addArray (type->getDeviceNames (false));
    }
    const auto answer = deviceReturned (before, inputs, outputs);
    if (! answer.reopen) return false;

    // The same name is not the same unit. Two interfaces of one model share a name; the one
    // that has turned up may be the other one, with other things plugged into it.
    const auto& want = lastRequest;
    const bool inputSame = want.input.isEmpty() || sameUnit (want.inputUid, MonitorDevice::findDevice (want.input).uid);
    const bool outputSame = want.output.isEmpty() || sameUnit (want.outputUid, MonitorDevice::findDevice (want.output).uid);
    if (! inputSame || ! outputSame)
    {
        if (! impostorAnnounced && onDifferentUnitReturned)
            onDifferentUnitReturned (! inputSame ? want.input : want.output);
        impostorAnnounced = true;
        return false;
    }

    // Opened exactly as it was: same devices, same rate, same buffer, same output channels.
    // The session, the assignments and the kept mix are not this function's business - it
    // opens a device, and prepare() builds the graph for whatever the document already says.
    const auto request = lastRequest;
    const auto error = request.outputOnly
        ? openOutputOnly (request.output, request.sampleRate, request.bufferSize)
        : open (request.input, request.output, request.sampleRate, request.bufferSize, request.outputChannels);
    if (error.isNotEmpty()) { lastRequest = request; return false; }   // still gone, or busy: try again next time
    lastRequest = request;
    lostAnnounced = false;
    if (onDeviceReturned) onDeviceReturned (state());
    return true;
}

} // namespace livemix
