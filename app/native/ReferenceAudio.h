#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Mix/ReferenceMix.h"

namespace livemix
{

// Listening to a reference recording: the file half of REFERENCE MIX.
//
// The measurement itself is the engine's (one AnalysisAccumulator, the same one every
// listen uses), so what DLIVE knows about a finished record and what it knows about the
// band are the same kind of knowledge. All this adds is the decoder and the patience to
// walk a five-minute song.
namespace ReferenceAudio
{
    // How much of a song is worth measuring. A tonal balance settles inside a minute; the
    // rest is spent to no purpose, and a volunteer should not wait on an album track.
    inline constexpr float kMaxSeconds = 240.0f;

    struct Result
    {
        ReferenceProfile profile;       // valid only when the file could be read and is worth aiming at
        ReferenceAdequacy adequacy;     // why not, when it is not
        juce::String error;             // empty unless the file could not be read at all
    };

    // Message thread or a worker: this reads the whole file, so a long track takes a moment.
    Result measure (const juce::File&, StyleProfileId profile);
}

} // namespace livemix
