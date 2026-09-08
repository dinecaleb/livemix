#pragma once
#include <map>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Project.h"

namespace livemix
{

// Reads the timeline. Given a set of tracks and their clips, renders any range of the
// timeline into one contiguous buffer per track channel, mixing overlapping clips and
// resampling material recorded at another rate.
//
// This is the one place clips become audio. TimelinePlayer drives it from its reader
// thread to keep the audio thread fed; the offline bounce drives it directly. It touches
// files and allocates, so it is never called from the audio callback.
class ClipSource
{
public:
    struct Track
    {
        int channels = 1;
        std::vector<AudioClip> clips;   // `file` must be an absolute path
    };

    ClipSource();

    void prepare (double projectSampleRate, int maxBlock, const std::vector<Track>& tracks);
    void release();

    int numTracks() const noexcept { return int (tracks.size()); }
    int numChannels (int track) const noexcept;

    // Renders `count` samples of the timeline from `from` into the internal buffers.
    void read (juce::int64 from, int count);
    const float* channel (int track, int ch) const noexcept;

private:
    struct Prepared
    {
        int channels = 1;
        std::vector<AudioClip> clips;                  // sorted by start
        std::array<std::vector<float>, 2> buffer;
    };

    juce::AudioFormatReader* readerFor (const juce::String& path);

    juce::AudioFormatManager formats;
    std::map<juce::String, std::unique_ptr<juce::AudioFormatReader>> readers;
    std::vector<Prepared> tracks;
    juce::AudioBuffer<float> scratch;
    double rate = 48000.0;
    int maxSamples = 512;
};

} // namespace livemix
