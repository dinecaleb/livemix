#include "MonitorDevice.h"

#if JUCE_MAC
 #include <CoreAudio/AudioHardware.h>
 #include <CoreFoundation/CoreFoundation.h>
#endif

namespace livemix
{
namespace MonitorDevice
{

namespace
{
    // The UID DLIVE gives the device it builds. Stable, so a second run finds and replaces the
    // one it made last Sunday instead of leaving a pile of them in the Mac's device list.
    constexpr const char* kDliveUid = "com.dine.dlive.monitoring";
    constexpr const char* kDliveName = "DLIVE Monitoring";

#if JUCE_MAC
    // ---- small CoreFoundation helpers -------------------------------------
    struct CFHold
    {
        CFTypeRef ref = nullptr;
        explicit CFHold (CFTypeRef r) : ref (r) {}
        ~CFHold() { if (ref != nullptr) CFRelease (ref); }
        CFHold (const CFHold&) = delete;
        CFHold& operator= (const CFHold&) = delete;
    };

    juce::String fromCF (CFStringRef s)
    {
        if (s == nullptr) return {};
        return juce::String::fromCFString (s);
    }

    CFStringRef toCF (const juce::String& s) { return s.toCFString(); }   // caller releases

    AudioObjectPropertyAddress address (AudioObjectPropertySelector selector,
                                        AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
    {
        return { selector, scope, kAudioObjectPropertyElementMain };
    }

    juce::String deviceStringProperty (AudioObjectID device, AudioObjectPropertySelector selector)
    {
        auto addr = address (selector);
        CFStringRef value = nullptr;
        UInt32 size = sizeof (value);
        if (AudioObjectGetPropertyData (device, &addr, 0, nullptr, &size, &value) != noErr || value == nullptr) return {};
        CFHold hold (value);
        return fromCF (value);
    }

    int outputChannelCount (AudioObjectID device)
    {
        auto addr = address (kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeOutput);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (device, &addr, 0, nullptr, &size) != noErr || size == 0) return 0;
        juce::HeapBlock<char> raw;
        raw.calloc (size);
        auto* list = reinterpret_cast<AudioBufferList*> (raw.get());
        if (AudioObjectGetPropertyData (device, &addr, 0, nullptr, &size, list) != noErr) return 0;
        int channels = 0;
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i) channels += int (list->mBuffers[i].mNumberChannels);
        return channels;
    }

    bool isAggregateDevice (AudioObjectID device)
    {
        auto addr = address (kAudioDevicePropertyTransportType);
        UInt32 transport = 0;
        UInt32 size = sizeof (transport);
        if (AudioObjectGetPropertyData (device, &addr, 0, nullptr, &size, &transport) != noErr) return false;
        return transport == kAudioDeviceTransportTypeAggregate || transport == kAudioDeviceTransportTypeVirtual;
    }

    juce::Array<AudioObjectID> allDeviceIds()
    {
        juce::Array<AudioObjectID> ids;
        auto addr = address (kAudioHardwarePropertyDevices);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr) return ids;
        const int count = int (size / sizeof (AudioObjectID));
        if (count <= 0) return ids;
        juce::HeapBlock<AudioObjectID> raw { size_t (count) };
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &addr, 0, nullptr, &size, raw.get()) != noErr) return ids;
        for (int i = 0; i < count; ++i) ids.add (raw[i]);
        return ids;
    }

    // The CoreAudio plug-in that owns aggregate devices. Everything below is addressed to it.
    AudioObjectID coreAudioPlugIn()
    {
        AudioObjectID plugIn = kAudioObjectUnknown;
        auto addr = address (kAudioHardwarePropertyPlugInForBundleID);
        CFStringRef bundle = CFSTR ("com.apple.audio.CoreAudio");
        AudioValueTranslation translation { &bundle, sizeof (CFStringRef), &plugIn, sizeof (AudioObjectID) };
        UInt32 size = sizeof (translation);
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &addr, 0, nullptr, &size, &translation) != noErr)
            return kAudioObjectUnknown;
        return plugIn;
    }

    AudioObjectID findDeviceByUid (const juce::String& uid)
    {
        for (auto id : allDeviceIds())
            if (deviceStringProperty (id, kAudioDevicePropertyDeviceUID) == uid) return id;
        return kAudioObjectUnknown;
    }

    // One entry of the sub-device list. `drift` is the difference between an aggregate device
    // that works and one that ticks every few seconds.
    CFMutableDictionaryRef subDevice (const juce::String& uid, bool drift)
    {
        auto* dict = CFDictionaryCreateMutable (kCFAllocatorDefault, 0,
                                                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFStringRef uidRef = toCF (uid);
        CFDictionarySetValue (dict, CFSTR (kAudioSubDeviceUIDKey), uidRef);
        CFRelease (uidRef);
        const int on = drift ? 1 : 0;
        CFNumberRef driftRef = CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &on);
        CFDictionarySetValue (dict, CFSTR (kAudioSubDeviceDriftCompensationKey), driftRef);
        CFRelease (driftRef);
        return dict;
    }
#endif
}

bool available()
{
#if JUCE_MAC
    return coreAudioPlugIn() != kAudioObjectUnknown;
#else
    return false;
#endif
}

juce::Array<Device> outputDevices()
{
    juce::Array<Device> out;
#if JUCE_MAC
    for (auto id : allDeviceIds())
    {
        Device d;
        d.outputChannels = outputChannelCount (id);
        if (d.outputChannels <= 0) continue;           // an input-only device is not a destination
        d.name = deviceStringProperty (id, kAudioObjectPropertyName);
        d.uid = deviceStringProperty (id, kAudioDevicePropertyDeviceUID);
        d.isAggregate = isAggregateDevice (id);
        d.isDliveBuilt = d.uid == kDliveUid;
        if (d.name.isEmpty() || d.uid.isEmpty()) continue;
        out.add (d);
    }
#endif
    return out;
}

bool dliveDeviceExists()
{
#if JUCE_MAC
    return findDeviceByUid (kDliveUid) != kAudioObjectUnknown;
#else
    return false;
#endif
}

Suggestion suggest (const juce::String& currentOutputDeviceName)
{
    if (! available())
    {
        Suggestion s;
        s.problem = "This Mac will not let DLIVE build a combined output device. "
                    "You can still make one yourself in Audio MIDI Setup.";
        return s;
    }
    return suggestFrom (outputDevices(), currentOutputDeviceName);
}

Result combine (const Device& broadcast, const Device& headphones)
{
    Result r;
#if JUCE_MAC
    const AudioObjectID plugIn = coreAudioPlugIn();
    if (plugIn == kAudioObjectUnknown) { r.error = "This Mac will not let DLIVE build a combined output device."; return r; }
    if (broadcast.uid.isEmpty() || headphones.uid.isEmpty()) { r.error = "Those devices could not be identified."; return r; }
    if (broadcast.uid == headphones.uid) { r.error = "The broadcast and the headphones have to be two different devices."; return r; }

    // Replace the one from last time rather than adding another. A device the *user* built is
    // never touched - only ours carries our UID.
    removeDliveDevice();

    auto* description = CFDictionaryCreateMutable (kCFAllocatorDefault, 0,
                                                   &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFHold holdDescription (description);

    CFStringRef nameRef = toCF (kDliveName);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceNameKey), nameRef);
    CFRelease (nameRef);
    CFStringRef uidRef = toCF (kDliveUid);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceUIDKey), uidRef);
    CFRelease (uidRef);

    // The broadcast device is the clock master: it is the one feeding the stream and the room,
    // and it is the one that must never be resampled to suit the headphones.
    CFStringRef masterUid = toCF (broadcast.uid);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceMainSubDeviceKey), masterUid);
    CFRelease (masterUid);

    // Public, so it appears in the Mac's device list and the user can see what DLIVE made and
    // remove it themselves if they ever want to.
    const int isPrivate = 0;
    CFNumberRef privateRef = CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &isPrivate);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceIsPrivateKey), privateRef);
    CFRelease (privateRef);

    // NOT stacked. A stacked aggregate is a Multi-Output Device: the same audio to every
    // device in it, one bus, mirrored - which is precisely why a solo could never be private.
    // Unstacked concatenates the channels, giving the broadcast and the headphones a real pair
    // each. This one line is the whole feature.
    const int stacked = 0;
    CFNumberRef stackedRef = CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &stacked);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceIsStackedKey), stackedRef);
    CFRelease (stackedRef);

    auto* subDevices = CFArrayCreateMutable (kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
    CFHold holdSubs (subDevices);
    // The master runs on its own clock; the other one is drift-corrected onto it. Dante and a
    // USB interface do not share a clock, and without this the monitor ticks every few seconds.
    auto* first = subDevice (broadcast.uid, false);
    CFArrayAppendValue (subDevices, first);
    CFRelease (first);
    auto* second = subDevice (headphones.uid, true);
    CFArrayAppendValue (subDevices, second);
    CFRelease (second);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceSubDeviceListKey), subDevices);

    AudioObjectID created = kAudioObjectUnknown;
    UInt32 size = sizeof (created);
    auto createAddress = address (kAudioPlugInCreateAggregateDevice);
    const OSStatus status = AudioObjectGetPropertyData (plugIn, &createAddress,
                                                        sizeof (description), &description, &size, &created);
    if (status != noErr || created == kAudioObjectUnknown)
    {
        r.error = "The combined output device could not be created (CoreAudio error "
                + juce::String (int (status)) + "). You can still make one yourself in Audio MIDI Setup.";
        return r;
    }

    // CoreAudio decides the channel order from the sub-device list, so the layout is read back
    // from the device that now exists rather than assumed: the broadcast is at the start and
    // the headphones follow it.
    r.ok = true;
    r.deviceName = deviceStringProperty (created, kAudioObjectPropertyName);
    if (r.deviceName.isEmpty()) r.deviceName = kDliveName;
    r.broadcastChannel = 0;
    r.headphoneChannel = juce::jmax (2, broadcast.outputChannels);
    // A device that came back smaller than expected means the broadcast device did not offer
    // everything it advertised; the headphone pair is clamped so it can never point past the end.
    const int total = outputChannelCount (created);
    if (total > 0 && r.headphoneChannel + 1 >= total) r.headphoneChannel = juce::jmax (0, total - 2);

    r.summary = "Your headphones are on " + headphones.name + ". Solo goes there; the stream never changes.";
#else
    juce::ignoreUnused (broadcast, headphones);
    r.error = "Combined output devices are a macOS feature.";
#endif
    return r;
}

bool removeDliveDevice()
{
#if JUCE_MAC
    const AudioObjectID plugIn = coreAudioPlugIn();
    const AudioObjectID existing = findDeviceByUid (kDliveUid);
    if (plugIn == kAudioObjectUnknown || existing == kAudioObjectUnknown) return false;
    auto destroyAddress = address (kAudioPlugInDestroyAggregateDevice);
    UInt32 size = sizeof (existing);
    AudioObjectID target = existing;
    return AudioObjectGetPropertyData (plugIn, &destroyAddress, 0, nullptr, &size, &target) == noErr;
#else
    return false;
#endif
}

void openAudioMidiSetup()
{
#if JUCE_MAC
    juce::File ("/System/Applications/Utilities/Audio MIDI Setup.app").startAsProcess();
#endif
}

juce::StringArray manualSteps()
{
    return { "Open Audio MIDI Setup (DLIVE can open it for you).",
             "Press + at the bottom left and choose Create Aggregate Device.",
             "Tick your console's device first, then the interface your headphones are in.",
             "Tick Drift Correction on the interface - not on the console.",
             "Come back to DLIVE and choose the new device here." };
}

} // namespace MonitorDevice
} // namespace livemix
