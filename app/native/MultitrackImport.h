#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Project.h"

namespace livemix
{

// Turns a folder of recorded stems into a DINELIVE session: one track per file, each
// file a clip at the start of the timeline, names and source roles guessed from the file
// names. This is how a band, or anyone with a multitrack, walks through the whole app —
// assign, TUNE MIX, BEFORE / AFTER, faders, export — without a console in the room.
namespace MultitrackImport
{
    struct Result
    {
        MixSession session;
        Project project;
        double sampleRate = 0.0;
        int files = 0;
        juce::String error;             // empty on success
    };

    // `base` supplies the name, purpose and profile to keep. Nothing is copied or moved:
    // the clips point at the files where they are.
    Result fromFolder (const juce::File& folder, const MixSession& base);
}

} // namespace livemix
