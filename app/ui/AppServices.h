#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "native/MixController.h"

namespace livemix
{

// What the UI needs from the outside world besides the controller: devices and
// persistence. The application implements it with AudioHost + SessionStore; the
// headless snapshot tool implements it with fakes, so every screen renders without
// a device.
class AppServices
{
public:
    virtual ~AppServices() = default;

    struct Device { juce::String name; int inputChannels = 0; };
    virtual juce::Array<Device> inputDevices() = 0;
    virtual juce::Array<Device> outputDevices() = 0;
    virtual juce::String openDevices (const juce::String& input, const juce::String& output) = 0;   // "" on success
    virtual bool isAudioRunning() = 0;
    virtual int numInputChannels() = 0;
    virtual double sampleRate() = 0;
    virtual int bufferSize() = 0;
    virtual int xrunCount() = 0;
    virtual void reconfigure() = 0;          // assignments changed: rebuild the graph with audio stopped
    virtual void saveSession() = 0;
    virtual juce::String currentInputDevice() = 0;
    virtual juce::String currentOutputDevice() = 0;

    // A folder of recorded stems played as the inputs (bands testing with a multitrack). "" on success.
    virtual juce::String openRecording (const juce::File& folder, const juce::String& outputDevice) = 0;
    virtual bool isPlayingRecording() = 0;
    virtual MixSession recordingSuggestion (const MixSession& base) = 0;   // names and sources guessed from the file names
};

// Shared page look: a titled card area on the design's ground.
namespace AppStyle
{
    inline constexpr int kTopBar = 52;
    inline constexpr int kMargin = 28;
    inline constexpr int kMaxContentWidth = 980;

    inline juce::Rectangle<int> contentArea (juce::Rectangle<int> bounds)
    {
        auto r = bounds.reduced (kMargin);
        if (r.getWidth() > kMaxContentWidth) r = r.withSizeKeepingCentre (kMaxContentWidth, r.getHeight());
        return r;
    }
}

} // namespace livemix
