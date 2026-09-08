#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Mix/MixEngine.h"

namespace livemix
{

// Offline bounce of the current mix through a stems folder (the same layout PLAY A
// RECORDING uses). Message thread / worker only — never call from the audio callback.
namespace MixBounce
{
    enum class Format { Wav = 0, Mp3 };

    // Renders the session with `params` from every audio file in `stemsFolder` (device-channel
    // layout matches MultitrackSource). Writes stereo to `dest`. Returns "" on success.
    // MP3 uses ffmpeg or lame when present on the machine; otherwise returns why not.
    juce::String renderToFile (const MixSession& session,
                               const MixParameters& params,
                               const juce::File& stemsFolder,
                               const juce::File& dest,
                               Format format,
                               double maxSeconds = 0.0);   // 0 = whole recording
}

} // namespace livemix
