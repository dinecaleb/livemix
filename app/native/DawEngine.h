#pragma once
#include <atomic>
#include <vector>
#include "Core/TripleBuffer.h"
#include "MixController.h"
#include "Project.h"
#include "Recorder.h"
#include "TimelinePlayer.h"
#include "Transport.h"

namespace livemix
{

// The DAW around the mix: the playhead, the multitrack recorder, the timeline player
// and the rule that decides, per track and per block, whether the mix hears the live
// input or the recording.
//
//   device inputs ─┬─ Recorder (raw WAV per armed track, worker thread)
//                  └─ input matrix ─ MixController::process ─ stereo out
//   timeline ────── TimelinePlayer ──┘
//
// Everything the audio thread needs arrives as one published route table; nothing here
// allocates, locks or touches a file on the audio thread.
class DawEngine
{
public:
    explicit DawEngine (MixController&);
    ~DawEngine();

    // ---- message thread ----
    Project& getProject() noexcept { return project; }
    const Project& getProject() const noexcept { return project; }
    void setProject (const Project& p);

    Transport& getTransport() noexcept { return transport; }
    const Transport& getTransport() const noexcept { return transport; }
    Recorder& getRecorder() noexcept { return recorder; }
    const TimelinePlayer& getPlayer() const noexcept { return player; }

    // Called with the audio callback stopped (AudioHost::audioDeviceAboutToStart).
    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // The session decides which device channels a track owns; call after assignments change.
    void setSession (const MixSession& s) { session = s; project.syncTracks (s); refresh(); }
    const MixSession& getSession() const noexcept { return session; }

    // Clips, arming, monitoring or the loop changed: republish what the audio thread reads.
    void refresh();

    void play();
    void stop();
    void locate (juce::int64 sample);
    void returnToStart() { locate (0); }
    void setLoop (bool on, juce::int64 start, juce::int64 end);

    // Starts a take on every armed track. "" on success; the transport begins rolling.
    juce::String startRecording();
    // Ends the take and turns what was captured into clips on their tracks. Returns how many.
    int stopRecording();
    bool isRecording() const noexcept { return recorder.isRecording(); }

    // ---- audio thread ----
    void processBlock (const float* const* deviceInputs, int numInputChannels,
                       float* const* outputs, int numOutputs, int numSamples) noexcept;

private:
    struct Route
    {
        int inputA = -1, inputB = -1, channels = 1;
        bool armed = false, hasClips = false;
        MonitorMode monitor = MonitorMode::Auto;
    };
    struct RouteTable
    {
        int count = 0;
        int matrixChannels = 0;
        std::array<Route, kMaxStrips> tracks {};
    };

    // Rebuilding the player (a take landed, clips were edited) frees the buffers the audio
    // thread reads. `rebuilding` stops the next block using them and `inBlock` waits for the
    // one already in flight, so the swap costs the message thread a block, never a crash.
    void rebuildPlayer();

    MixController& controller;
    MixSession session;
    Project project;
    Transport transport;
    Recorder recorder;
    TimelinePlayer player;

    TripleBuffer<RouteTable> routeMailbox;
    RouteTable routes;                       // audio thread's copy
    std::vector<const float*> matrix;
    std::vector<float> silence;
    std::atomic<bool> rebuilding { false }, inBlock { false };
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool prepared = false;
    bool clipsDirty = true;
};

} // namespace livemix
