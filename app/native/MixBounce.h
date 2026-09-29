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
    enum class Format { Wav = 0, Aiff, Mp3 };

    // WHAT TO WRITE (design: `27 - Export`, 88:23516).
    //
    // StereoMix     one file: the kept mix, exactly as the room and the stream hear it.
    // GroupStems    one stereo file per group bus that is used, each at its fader, so the
    //               set of them sums to the mix as it stood before the master's own chain.
    //               That is what a broadcast editor re-balances from.
    // RawMultitrack one file per assigned input, straight off the disk with nothing in the
    //               way: no trim, no chain, no fader. What the microphones heard, for
    //               somebody who wants to mix it themselves.
    enum class What { StereoMix = 0, GroupStems, RawMultitrack };

    // WHERE IT LANDS. The mix is built to a target once, in Purpose and sound, and carries
    // it - "as mixed" is the honest answer and the default. The other two are for handing the
    // same service to a platform that wants its own number: the render is measured, and one
    // gain is applied to all of it. Nothing is compressed or limited on the way out; a mix
    // that is already louder than the target is turned down, never squashed into it.
    //
    // Only a stereo mix can be normalised. Stems and a raw multitrack are parts of something,
    // and moving their levels apart from each other would stop them being parts of it.
    enum class Loudness { AsMixed = 0, Stream14, Podcast16 };

    inline float targetLufs (Loudness l) noexcept
    {
        return l == Loudness::Stream14 ? -14.0f : l == Loudness::Podcast16 ? -16.0f : 0.0f;
    }

    struct Options
    {
        juce::int64 from = 0;
        juce::int64 to = 0;                        // 0 = the end of the recorded material
        double sampleRate = 0.0;                   // 0 = the project's rate
        What what = What::StereoMix;
        Loudness loudness = Loudness::AsMixed;
        std::function<bool (float)> onProgress;    // 0..1; return false to cancel
    };

    // Renders `project`'s clips through `session` + `params` into `dest`. "" on success.
    //
    // For a stereo mix `dest` is the file. For stems and a raw multitrack it is the folder:
    // its name without an extension becomes a folder beside it, with one file per group or
    // per input inside. `written` is given what was actually made, in the order it was made.
    //
    // MP3 uses ffmpeg or lame when present on the machine; otherwise it says why not.
    juce::String renderProject (const MixSession& session,
                                const MixParameters& params,
                                const Project& project,
                                const juce::File& dest,
                                Format format,
                                Options options = {},
                                juce::StringArray* written = nullptr);
}

} // namespace livemix
