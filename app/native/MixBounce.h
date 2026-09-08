#pragma once
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include "ClipSource.h"
#include "Mix/MixEngine.h"

namespace livemix
{

// Offline bounce: the recorded timeline rendered through the mix, block by block and
// straight to disk, so a three-hour service exports without three hours of RAM.
// Message thread / worker only — never call this from the audio callback.
namespace MixBounce
{
    enum class Format { Wav = 0, Mp3 };

    struct Options
    {
        juce::int64 from = 0;
        juce::int64 to = 0;                        // 0 = the end of the recorded material
        double sampleRate = 0.0;                   // 0 = the project's rate
        std::function<bool (float)> onProgress;    // 0..1; return false to cancel
    };

    // Renders `project`'s clips through `session` + `params` into `dest`. "" on success.
    // MP3 uses ffmpeg or lame when present on the machine; otherwise it says why not.
    juce::String renderProject (const MixSession& session,
                                const MixParameters& params,
                                const Project& project,
                                const juce::File& dest,
                                Format format,
                                Options options = {});
}

} // namespace livemix
