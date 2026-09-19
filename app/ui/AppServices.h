#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "native/DawEngine.h"
#include "native/MixController.h"
#include "native/SessionStore.h"

namespace livemix
{

// What the UI needs from the outside world besides the controller and the DAW engine:
// devices, persistence, import and export. The application implements it with AudioHost +
// SessionStore; the headless snapshot tool implements it with fakes, so every screen
// renders without a device.
class AppServices
{
public:
    virtual ~AppServices() = default;

    // ---- the DAW: transport, timeline, recorder ----
    virtual DawEngine& daw() = 0;

    // ---- devices ----
    struct Device { juce::String name; int inputChannels = 0; int outputChannels = 0; };
    virtual juce::Array<Device> inputDevices() = 0;
    virtual juce::Array<Device> outputDevices() = 0;
    virtual juce::String openDevices (const juce::String& input, const juce::String& output) = 0;   // "" on success
    // Output only: playing a recorded session back with no console connected. "" on success.
    virtual juce::String openOutputOnly (const juce::String& output) = 0;
    // Change stereo output without re-running setup. Keeps the current mix. "" on success.
    virtual juce::String changeOutput (const juce::String& output) = 0;
    virtual bool isAudioRunning() = 0;
    virtual int numInputChannels() = 0;
    // How many output channels the open device has, and what it calls them. Used by the
    // Outputs sheet to offer the pairs; 2 is the safe answer for anything that cannot say.
    virtual int numOutputChannels() { return 2; }
    virtual juce::StringArray outputChannelNames() { return {}; }
    virtual double sampleRate() = 0;
    virtual int bufferSize() = 0;
    virtual int xrunCount() = 0;
    // How much of the audio thread's time the engine is using, 0..1; below 0 when nobody can say.
    virtual double cpuLoad() { return -1.0; }
    virtual bool deviceStopped() { return false; }   // the device went away without the app closing it
    virtual void reconfigure() = 0;          // assignments changed: rebuild the graph with audio stopped

    // ---- Two outputs: one for the broadcast, one for the engineer ----
    //
    // The whole feature, from the user's side, is two choices: which device the stream and the
    // room go out of, and which device the engineer listens on. Everything underneath is
    // DLIVE's problem - and it is a real one, because macOS opens exactly one audio device at
    // a time. Choosing two different devices makes DLIVE build the combined device itself
    // (native/MonitorDevice.h) and route a pair to each; choosing the same device puts solo on
    // a second pair of it. Neither case mentions an Aggregate Device to anybody.
    struct MonitorSetup { bool ok = false; juce::String message; };
    virtual bool canCombineOutputs() { return false; }
    // Which device carries the broadcast, and which carries solo ("" = solo is not set up).
    virtual juce::String broadcastOutputDevice() { return currentOutputDevice(); }
    virtual juce::String soloOutputDevice() { return {}; }
    // Choose the device solo goes to. "" turns it off and puts the Mac back as it was.
    virtual MonitorSetup setSoloOutputDevice (const juce::String&) { return { false, "That is not available here." }; }
    // What is set up right now, in plain words; empty when solo has nowhere to go.
    virtual juce::String headphonesSummary() { return {}; }
    virtual juce::String currentInputDevice() = 0;
    virtual juce::String currentOutputDevice() = 0;
    // What to *call* the output, which is not always the device that is open. While DLIVE has
    // two devices joined, the open device is one it built and the user has never heard of;
    // what they chose is the broadcast. Every piece of chrome says this rather than the name
    // of the machinery, so the toolbar and the Outputs sheet cannot appear to disagree.
    virtual juce::String outputDisplayName() { return currentOutputDevice(); }

    // ---- the session document ----
    virtual void saveSession() = 0;
    // Start over: clears the assignments, the timeline and the mix, keeping the device open.
    virtual void newSession() = 0;
    // Save under a new name (Save As). Updates the live session name and its folder. "" on success.
    virtual juce::String saveSessionAs (const juce::String& name) = 0;
    // Load a previously saved session; restores assignments, timeline, macros, kept mix, and
    // reopens devices when possible. Returns "" on success.
    virtual juce::String loadSession (const juce::File& file) = 0;
    // What the last loadSession found still recording from a crash, one sentence per take, "" if nothing. Read once.
    virtual juce::String takeRecoveryNote() { return {}; }
    virtual juce::Array<SessionStore::Listing> listSessions() = 0;
    virtual juce::String currentSessionName() = 0;
    // How wide the TRACKS channel panel was left. A layout preference stored with the session,
    // the way a track's row height already is: 0 means "never set", so the page keeps its own
    // default. Not part of the mix, so it goes through here rather than through MixController.
    virtual int trackPanelWidth() { return 0; }
    virtual void setTrackPanelWidth (int) {}
    virtual juce::File sessionFolder() = 0;   // empty until the session has been saved

    // A folder of stems becomes tracks and clips: assign, TUNE MIX, mix and export without a console.
    virtual juce::String importMultitrack (const juce::File& folder) = 0;

    // Bounce the recorded timeline through the current mix to stereo WAV or MP3. "" on success.
    //
    // An export reads the session, the running mix and every clip for as long as it takes to
    // render - minutes, for a service - while the message thread is still free to move a
    // fader, rename an input or record another take. So the render works from a *copy* taken
    // on the message thread (snapshotExport), never from the live document: `exportMix` is
    // the only thing the worker touches. `progress` returns false to cancel.
    enum class ExportFormat { Wav = 0, Mp3 };
    struct ExportJob
    {
        MixSession session;
        MixParameters params;
        Project project;
    };
    virtual std::shared_ptr<const ExportJob> snapshotExport() = 0;   // message thread
    virtual juce::String exportMix (std::shared_ptr<const ExportJob>, const juce::File& dest,
                                    ExportFormat, std::function<bool (float)> progress) = 0;   // worker thread
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
