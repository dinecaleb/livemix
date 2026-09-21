// Which two devices "use my headphones" should combine.
//
// This is a judgement, not a platform call, so it is kept away from CoreAudio and tested
// without a device: the broadcast is whatever is already carrying the mix, and the headphones
// are the best *other* real device - an interface in preference to the Mac's own speakers,
// because a sound booth has headphones plugged into an interface and nobody wants to discover
// their solo came out of the laptop in the middle of a sermon.
#include "MonitorDevice.h"

namespace livemix
{
namespace MonitorDevice
{

Suggestion suggestFrom (const juce::Array<Device>& devices, const juce::String& currentOutputDeviceName)
{
    Suggestion s;
    if (devices.isEmpty())
    {
        s.problem = "No output devices were found.";
        return s;
    }

    // The broadcast is whatever is already carrying the mix. If that is a combined device
    // DLIVE built earlier, the console's own device is inside it, so the pieces are found by
    // name instead and the old one is replaced.
    const Device* broadcast = nullptr;
    for (const auto& d : devices)
        if (! d.isDliveBuilt && d.name == currentOutputDeviceName) { broadcast = &d; break; }
    if (broadcast == nullptr)
        for (const auto& d : devices)
            if (! d.isDliveBuilt && ! d.isAggregate && d.outputChannels > 2) { broadcast = &d; break; }
    if (broadcast == nullptr)
        for (const auto& d : devices)
            if (! d.isDliveBuilt) { broadcast = &d; break; }
    if (broadcast == nullptr)
    {
        s.problem = "DLIVE could not work out which output carries the broadcast.";
        return s;
    }

    // The headphones: the best *other* real device. An interface is preferred over the Mac's
    // own speakers, because a booth has headphones plugged into an interface and nobody wants
    // to discover their solo came out of the laptop. The ranking comes from what CoreAudio says
    // the device *is* (its transport), never from its name - an interface can be called anything.
    // Bluetooth headphones beat the laptop speaker (they are at least headphones), a display's
    // speakers come after, and a virtual device last of all: there is no socket on it.
    auto rank = [] (const Device& d)
    {
        switch (d.kind)
        {
            case Device::Kind::Interface: return 0;
            case Device::Kind::Bluetooth: return 1;
            case Device::Kind::BuiltIn:   return 2;
            case Device::Kind::Display:   return 3;
            case Device::Kind::Virtual:   return 4;
        }
        return 5;
    };
    const Device* headphones = nullptr;
    for (const auto& d : devices)
    {
        if (d.isDliveBuilt || d.isAggregate || d.uid == broadcast->uid) continue;
        if (headphones == nullptr || rank (d) < rank (*headphones)) headphones = &d;
    }

    if (headphones == nullptr)
    {
        s.problem = "There is only one output device on this Mac, so there is nowhere separate "
                    "for your headphones to go. Plug in an audio interface (or headphones with "
                    "their own adaptor) and try again.";
        return s;
    }

    s.valid = true;
    s.broadcast = *broadcast;
    s.headphones = *headphones;
    s.why = "The stream and the room keep going out of " + broadcast->name
          + ", and your headphones go to " + headphones->name
          + ". Soloing a channel will only be heard in the headphones.";
    return s;
}

Layout layoutFor (const Device& broadcast, const Device& headphones, const Device* input)
{
    Layout l;
    if (broadcast.uid.isEmpty() || headphones.uid.isEmpty()) { l.problem = "Those devices could not be identified."; return l; }
    if (broadcast.uid == headphones.uid) { l.problem = "The broadcast and the headphones have to be two different devices."; return l; }
    if (broadcast.isAggregate || headphones.isAggregate || (input != nullptr && input->isAggregate))
    {
        // An aggregate device cannot hold another one. A combined device the user built is opened as
        // it is; solo then needs the user's own device to carry a spare pair. A *virtual* device is
        // not one of these: the Dante Virtual Soundcard goes inside the built device like any interface.
        l.problem = "One of those is already a combined device, and a combined device cannot be put inside another. "
                    "Choose the plain devices instead.";
        return l;
    }

    // The console's inputs first, so channel 1 stays channel 1; a device already in the list is not added twice.
    auto add = [&l] (const Device& d)
    {
        for (const auto& p : l.pieces) if (p.uid == d.uid) return;
        l.pieces.add (d);
    };
    if (input != nullptr && input->uid.isNotEmpty() && input->inputChannels > 0)
    {
        add (*input);
        l.carriesInput = true;
    }
    add (broadcast);
    add (headphones);

    int channel = 0;
    for (const auto& p : l.pieces)
    {
        if (p.uid == broadcast.uid) l.broadcastChannel = channel;
        if (p.uid == headphones.uid) l.headphoneChannel = channel;
        channel += p.outputChannels;
    }
    return l;
}

juce::BigInteger outputChannelsToOpen (const Layout& layout, int broadcastOutputs, int capacity)
{
    juce::BigInteger bits;
    bits.setRange (juce::jmax (0, layout.headphoneChannel), 2, true);
    const int forBroadcast = juce::jlimit (2, juce::jmax (2, capacity - 2), broadcastOutputs);
    for (int c = 0; c < forBroadcast; ++c)
        bits.setBit (juce::jmax (0, layout.broadcastChannel) + c, true);
    return bits;
}

} // namespace MonitorDevice
} // namespace livemix
