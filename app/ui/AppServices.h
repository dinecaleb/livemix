#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "native/MixController.h"
#include "native/SessionStore.h"

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

    struct Device { juce::String name; int inputChannels = 0; int outputChannels = 0; };
    virtual juce::Array<Device> inputDevices() = 0;
    virtual juce::Array<Device> outputDevices() = 0;
    virtual juce::String openDevices (const juce::String& input, const juce::String& output) = 0;   // "" on success
    // Change stereo output without re-running setup. Keeps the current mix. "" on success.
    virtual juce::String changeOutput (const juce::String& output) = 0;
    virtual bool isAudioRunning() = 0;
    virtual int numInputChannels() = 0;
    virtual double sampleRate() = 0;
    virtual int bufferSize() = 0;
    virtual int xrunCount() = 0;
    virtual bool deviceStopped() { return false; }   // the device went away without the app closing it
    virtual void reconfigure() = 0;          // assignments changed: rebuild the graph with audio stopped
    virtual void saveSession() = 0;
    // Save under a new name (Save As). Updates the live session name. "" on success.
    virtual juce::String saveSessionAs (const juce::String& name) = 0;
    // Load a previously saved mix; restores assignments, macros, kept mix, and reopens devices when possible.
    // Returns "" on success. Caller should show Mix (or Assign) afterward.
    virtual juce::String loadSession (const juce::File& file) = 0;
    virtual juce::Array<SessionStore::Listing> listSessions() = 0;
    virtual juce::String currentInputDevice() = 0;
    virtual juce::String currentOutputDevice() = 0;
    virtual juce::String currentSessionName() = 0;

    // A folder of recorded stems played as the inputs (bands testing with a multitrack). "" on success.
    virtual juce::String openRecording (const juce::File& folder, const juce::String& outputDevice) = 0;
    virtual bool isPlayingRecording() = 0;
    virtual MixSession recordingSuggestion (const MixSession& base) = 0;   // names and sources guessed from the file names
};

// Shared page look: a titled card area on the design's ground.
namespace AppStyle
{
    inline constexpr int kTopBar = 56;
    inline constexpr int kMargin = 32;
    inline constexpr int kMaxContentWidth = 1080;

    inline juce::Rectangle<int> contentArea (juce::Rectangle<int> bounds)
    {
        auto r = bounds.reduced (kMargin);
        if (r.getWidth() > kMaxContentWidth) r = r.withSizeKeepingCentre (kMaxContentWidth, r.getHeight());
        return r;
    }
}

} // namespace livemix
