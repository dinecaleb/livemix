#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Project.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// BRINGING AUDIO IN.
//
// Every way a file arrives - File > Import Audio Files, a folder from the launcher, the device
// page's import, a drop on TRACKS - goes through here, so they all agree on what a folder of
// stems means. The rules, in the order they are applied:
//
//   * EVERY FILE IS HEARD. A file DINE has no name for is still a channel on the console: it plays
//     in the Music group until somebody says what it is, and the summary names it. (It used to
//     be listed and never processed, which is what "some channels are missing" was.)
//   * A FOLDER IS LOOKED INSIDE. Sub-folders are searched; hidden files (the "._" copies an SD
//     card is full of) and bounce folders are not.
//   * NUMBERS ARE COUNTED, NOT SPELLED. "2 Snare" comes before "10 Keys". A leading channel
//     number ("01-KICK") is the channel; a take number ("Kick_002", "Kick#03") and a desk's
//     time stamp ("-240927_2117") are not part of the name.
//   * A RECORDING PASS IS ONE PLACE ON THE TIMELINE. Files that ran at the same time (by their
//     broadcast-WAV time stamp, else by when the file was made) are one pass; a later pass sits
//     after it. "LEAD_001..003" in one pass are three microphones, and "LEAD_004..006" in the
//     next are the same three again - so they land on Lead 1, Lead 2 and Lead 3, not on six
//     tracks.
//   * A DESK'S MULTICHANNEL FILE IS ITS CHANNELS. A 32-channel card recording is 32 tracks, and
//     the card's numbered chunks follow each other on them.
//   * SPLIT STEREO IS JOINED. "Keys L" + "Keys R", "ohL" + "ohR", "Piano.L" + "Piano.R" of the
//     same length are one stereo track. Two toms are not a pair, however they are named.
//   * NOTHING IS THROWN AWAY. Importing into a session adds to it. A track already set up but
//     still empty takes the file with its name (or, failing that, the only file of its source),
//     which is how last Sunday's multitrack lands on this Sunday's console channels.
//
// Nothing is copied or moved: the clips point at the files where they are.
// ---------------------------------------------------------------------------
namespace MultitrackImport
{
    // One clip of a planned track, before the project's sample rate is known.
    struct PlannedClip
    {
        AudioClip clip;                 // file, fileChannel (and the right side's); length in the file's samples
        double startSeconds = 0.0;      // where it begins on the timeline
        int pass = 0;
    };

    struct PlannedTrack
    {
        juce::String name;
        ChannelRole role = ChannelRole::SynthPad;
        bool recognised = false;        // the name said what it is
        bool stereo = false;
        bool joinedPair = false;        // two mono files made into this one
        int deviceChannel = -1;         // a desk's multichannel file: the console channel it was recorded from
        std::vector<PlannedClip> clips;
    };

    struct Plan
    {
        std::vector<PlannedTrack> tracks;
        double sampleRate = 0.0;        // the first file's
        int files = 0;                  // audio files that were read
        int passes = 0;
        juce::StringArray skipped;      // "notes.mp4 (it will not open)"
    };

    // Reads what it is given - files, folders, or both - and decides the tracks. Opens every
    // file to learn its length and channels; reads no audio.
    Plan plan (const juce::Array<juce::File>& filesOrFolders);

    enum class Destination
    {
        Match,          // onto the session's empty tracks where a file's name says so, else new tracks
        NewTracks,      // every planned track a new track (dropped below the last one)
        OntoTracks,     // onto the tracks from `firstTrack` down, at `at`, then new tracks (dropped on one)
    };

    struct Applied
    {
        int added = 0;                  // new tracks (and inputs)
        int onExisting = 0;             // planned tracks that landed on a track already here
        int clips = 0;
        int pairs = 0;
        juce::StringArray unrecognised; // names of the tracks the import could not tell the source of
        juce::StringArray leftOut;      // names that did not fit in the session
        juce::String summary;           // one plain paragraph for the toast
    };

    // Puts the plan into the session and its project. The project's tracks run parallel to the
    // session's inputs before and after. The project takes the plan's sample rate when it has
    // no audio yet.
    Applied apply (const Plan&, MixSession& session, Project& project,
                   Destination = Destination::Match, int firstTrack = -1, juce::int64 at = 0);

    // A folder into a fresh session (the stems tool, the device check): `base` supplies the
    // name, purpose and profile to keep, and its inputs are not.
    struct Result
    {
        MixSession session;
        Project project;
        double sampleRate = 0.0;
        int files = 0;
        juce::String summary;
        juce::String error;             // empty on success
    };
    Result fromFolder (const juce::File& folder, const MixSession& base);

    // The pieces of the rules above, exposed for the tests.
    struct ParsedName { juce::String display; int number = -1; };
    ParsedName parseName (const juce::String& fileNameWithoutExtension);
    // -1 left, +1 right, 0 neither; `base` is the name without its side.
    int sideOf (const juce::String& display, juce::String& base);
}

} // namespace livemix
