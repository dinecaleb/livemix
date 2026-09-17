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
    // to discover their solo came out of the laptop.
    auto looksBuiltIn = [] (const Device& d)
    {
        const auto n = d.name.toLowerCase();
        return n.contains ("built-in") || n.contains ("macbook") || n.contains ("imac")
            || n.contains ("mac mini") || n.contains ("display") || n.contains ("airpods");
    };
    const Device* headphones = nullptr;
    for (const auto& d : devices)
    {
        if (d.isDliveBuilt || d.isAggregate || d.uid == broadcast->uid) continue;
        if (looksBuiltIn (d)) continue;
        headphones = &d;
        break;
    }
    if (headphones == nullptr)
        for (const auto& d : devices)
        {
            if (d.isDliveBuilt || d.isAggregate || d.uid == broadcast->uid) continue;
            headphones = &d;
            break;
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

} // namespace MonitorDevice
} // namespace livemix
