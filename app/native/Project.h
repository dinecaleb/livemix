#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>
#include "Mix/MixSession.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// The DAW side of a DINE session: what was recorded, where it sits on the
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
    // Last, so the positional initialisers stay as they were. Two mono stems joined as one stereo input ("OH L" + "OH R"): `file` is the left, this is
    // the right, and they run sample for sample. Empty = `file` carries both sides.
    juce::String fileRight;
    // A desk's multichannel recording (a 32-channel card file) is one file and many tracks: the
    // channel of `file` this clip starts at (a stereo track takes it and the next), and the
    // channel of `fileRight` its right side is. 0 for every ordinary file.
    int fileChannel = 0;
    int fileRightChannel = 0;

    juce::int64 end() const noexcept { return start + length; }
    bool covers (juce::int64 pos) const noexcept { return pos >= start && pos < end(); }
};

// How a track listens to its live input. DINE is a live console before it is a
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
        std::vector<bool> used (tracks.size(), false);
        for (size_t n = 0; n < match.size(); ++n)
            if (match[n] >= 0 && match[n] < int (tracks.size()))
            {
                moved[n] = tracks[size_t (match[n])];
                used[size_t (match[n])] = true;
            }

        // TWO MONO STEMS JOINED AS A PAIR. "OH L" and "OH R" imported as two inputs and linked
        // on ASSIGN become one stereo input on the left one's channel - and the right one's
        // track used to go with its input, so the pair played its left side down the middle
        // and the right side was never heard. Its clips now ride on the left track's as the
        // right-hand file.
        auto previousOn = [&] (int channel) -> int
        {
            for (size_t p = 0; p < previous.inputs.size(); ++p)
                if (previous.inputs[p].inputA == channel && ! previous.inputs[p].isStereo()) return int (p);
            return -1;
        };
        for (size_t n = 0; n < next.inputs.size(); ++n)
        {
            const auto& in = next.inputs[n];
            if (! in.isStereo() || match[n] < 0 || previous.inputs[size_t (match[n])].isStereo()) continue;
            const int right = previousOn (in.inputB);
            if (right < 0 || right >= int (tracks.size()) || used[size_t (right)]) continue;
            for (auto& clip : moved[n].clips)
                for (const auto& r : tracks[size_t (right)].clips)
                    if (r.start == clip.start && r.length == clip.length && clip.fileRight.isEmpty())
                        { clip.fileRight = r.file; clip.fileRightChannel = r.fileChannel; break; }
            used[size_t (right)] = true;
        }
        // ... and taken apart again: the right-hand files go back to a track of their own on
        // the right side's channel, and the left track stops carrying them.
        for (size_t n = 0; n < next.inputs.size(); ++n)
        {
            const auto& in = next.inputs[n];
            if (in.isStereo()) continue;
            if (match[n] >= 0)
            {
                if (previous.inputs[size_t (match[n])].isStereo())
                    for (auto& clip : moved[n].clips) { clip.fileRight = {}; clip.fileRightChannel = 0; }
                continue;
            }
            for (size_t p = 0; p < previous.inputs.size() && p < tracks.size(); ++p)
            {
                const auto& was = previous.inputs[p];
                if (! was.isStereo() || was.inputB != in.inputA) continue;
                TrackState split = tracks[p];
                split.armed = false;
                split.clips.clear();
                for (auto clip : tracks[p].clips)
                    if (clip.fileRight.isNotEmpty())
                    {
                        clip.file = clip.fileRight;
                        clip.fileChannel = clip.fileRightChannel;
                        clip.fileRight = {};
                        clip.fileRightChannel = 0;
                        split.clips.push_back (clip);
                    }
                moved[n] = split;
                break;
            }
        }
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
    juce::File fileFor (const AudioClip& clip) const { return fileForPath (clip.file); }
    juce::File rightFileFor (const AudioClip& clip) const { return fileForPath (clip.fileRight); }
    juce::File fileForPath (const juce::String& path) const
    {
        if (path.isEmpty()) return {};
        if (juce::File::isAbsolutePath (path)) return juce::File (path);
        return audioFolder().getChildFile (path);
    }
};

} // namespace livemix
