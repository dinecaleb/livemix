#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>
#include "Mix/MixSession.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// The DAW side of a DLIVE session: what was recorded, where it sits on the
// timeline, and how each track behaves while recording.
//
// The *mix* side (input gain, fader, pan, mute, solo, sends, processing) lives in
// MixParameters and is shared by Tracks, Mixer, Tune and Live. Nothing here
// duplicates it: one session, four views.
// ---------------------------------------------------------------------------

// One recorded or imported region on a track.
struct AudioClip
{
    juce::String name;
    juce::String file;              // file name inside the project's "Audio Files", or an absolute path
    juce::int64 start = 0;          // where it begins on the timeline, in project samples
    juce::int64 offset = 0;         // first sample used from the file
    juce::int64 length = 0;         // how many samples are used
    double fileSampleRate = 0.0;    // 0 = same as the project

    juce::int64 end() const noexcept { return start + length; }
    bool covers (juce::int64 pos) const noexcept { return pos >= start && pos < end(); }
};

// How a track listens to its live input. DLIVE is a live console before it is a
// tape machine, so the input is what you hear unless something says otherwise:
//   Off   - the live input is never heard through this track (recording still captures it)
//   Input - the live input is always heard
//   Auto  - the live input is heard, except while the timeline is playing this track's
//           recording back; an armed track being recorded always stays on its input
// This is the whole rule, and the Tracks page prints it.
enum class MonitorMode : int { Off = 0, Input, Auto, Count };

inline constexpr const char* monitorModeName (MonitorMode m) noexcept
{
    switch (m)
    {
        case MonitorMode::Off:   return "Off";
        case MonitorMode::Input: return "Input";
        case MonitorMode::Auto:
        default:                 return "Auto";
    }
}

// The per-track DAW state, one entry per assigned input in the MixSession.
struct TrackState
{
    bool armed = false;
    MonitorMode monitor = MonitorMode::Auto;
    int height = 64;                    // timeline row height in pixels
    std::vector<AudioClip> clips;

    juce::int64 lengthSamples() const noexcept
    {
        juce::int64 n = 0;
        for (const auto& c : clips) n = juce::jmax (n, c.end());
        return n;
    }
};

// The one place the monitoring rule lives, so the audio thread, the UI and the tests agree.
inline bool monitorUsesLiveInput (MonitorMode mode, bool armed, bool hasClips, bool playing, bool recording) noexcept
{
    switch (mode)
    {
        case MonitorMode::Off:   return false;
        case MonitorMode::Input: return true;
        case MonitorMode::Auto:
        default: break;
    }
    if (recording && armed) return true;
    return ! (playing && hasClips);
}

// A named position on the timeline.
struct Marker { juce::String name; juce::int64 position = 0; };

// The recording/timeline document. `tracks` runs parallel to MixSession::inputs.
struct Project
{
    juce::File folder;                  // the project folder; audio lands in folder/"Audio Files"
    double sampleRate = 48000.0;
    double tempo = 120.0;
    std::vector<TrackState> tracks;
    std::vector<Marker> markers;
    // LIVE SAFE: during a service, nothing may re-tune, re-route or edit the timeline by
    // accident. Mutes, faders and the transport stay available - an operator must always be
    // able to act in an emergency.
    bool liveSafe = false;
    bool loopEnabled = false;
    juce::int64 loopStart = 0, loopEnd = 0;

    juce::File audioFolder() const { return folder.getChildFile ("Audio Files"); }

    // The tracks run parallel to the session's inputs, so when the assignments are rebuilt -
    // an input dropped on the ASSIGN page, a pair linked, a source added - every track has to
    // follow *its own* input instead of staying at its index. Otherwise a track keeps its
    // audio and takes the next input's name, which is how a timeline ends up showing "Keys"
    // over a clip of the guitar. A track is matched to its input by the device channel it came
    // in on, then by name; an input that is new to the session starts with an empty track, and
    // a track whose input is gone goes with it.
    void syncTracks (const MixSession& previous, const MixSession& next)
    {
        // Which input each new input used to be is decided in one place (matchInputs), which
        // is also where the kept mix reads it: the clips and the console can never end up
        // disagreeing about which input is which.
        const auto match = matchInputs (previous, next);
        std::vector<TrackState> moved (next.inputs.size());
        for (size_t n = 0; n < match.size(); ++n)
            if (match[n] >= 0 && match[n] < int (tracks.size()))
                moved[n] = std::move (tracks[size_t (match[n])]);
        tracks = std::move (moved);
    }

    // The session did not change shape (a load, a rebuild from a document): only the count matters.
    void syncTracks (const MixSession& session)
    {
        tracks.resize (session.inputs.size());
    }

    juce::int64 lengthSamples() const noexcept
    {
        juce::int64 n = 0;
        for (const auto& t : tracks) n = juce::jmax (n, t.lengthSamples());
        return n;
    }

    bool hasAudio() const noexcept
    {
        for (const auto& t : tracks) if (! t.clips.empty()) return true;
        return false;
    }

    int numArmed() const noexcept
    {
        int n = 0;
        for (const auto& t : tracks) if (t.armed) ++n;
        return n;
    }

    // Resolves a clip's file: a bare name lives in "Audio Files", anything else is a path.
    juce::File fileFor (const AudioClip& clip) const
    {
        if (clip.file.isEmpty()) return {};
        if (juce::File::isAbsolutePath (clip.file)) return juce::File (clip.file);
        return audioFolder().getChildFile (clip.file);
    }
};

} // namespace livemix
