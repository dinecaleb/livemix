#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <vector>
#include "Mix/MixSession.h"

namespace livemix
{

// A folder of recorded stems played as if it were the console: every file becomes one or two
// device inputs, streamed from disk on a background thread and read block by block on the
// audio thread (juce::BufferingAudioSource), looping at the longest file. Lets a band, or
// anyone with a multitrack, walk through the real app: assign, TUNE MIX, BEFORE / AFTER, the
// sliders, all while hearing the mix from the chosen output device.
class MultitrackSource
{
public:
    struct Track { juce::String name; int channels = 1; int firstInput = 0; ChannelRole guessedRole = ChannelRole::KickIn; bool roleGuessed = false; };

    MultitrackSource();
    ~MultitrackSource();

    // Message thread. Opens every audio file in the folder. Returns "" on success, else why not.
    juce::String load (const juce::File& folder);
    void unload();
    bool isLoaded() const noexcept { return ! sources.empty(); }

    const juce::File& getFolder() const noexcept { return folder; }
    const std::vector<Track>& getTracks() const noexcept { return tracks; }
    int getTotalChannels() const noexcept { return totalChannels; }
    double getFileSampleRate() const noexcept { return fileRate; }
    double getLengthSeconds() const noexcept { return fileRate > 0.0 ? double (longestFrames) / fileRate : 0.0; }
    double getPositionSeconds() const noexcept;

    // The session the file names suggest: one input per file, stereo files linked, roles guessed.
    MixSession suggestedSession (const MixSession& base) const;

    // Message thread, before the device starts (or with the callback stopped).
    void prepare (int blockSize, double deviceSampleRate);
    void release();

    // Audio thread: fills `numChannels` destination channels (device-input layout) for one block.
    void fillNext (float* const* dest, int numChannels, int numSamples) noexcept;
    void rewind() noexcept { rewindRequested.store (true); }

private:
    struct Source
    {
        std::unique_ptr<juce::AudioFormatReaderSource> reader;
        std::unique_ptr<juce::ResamplingAudioSource> resampler;
        std::unique_ptr<juce::BufferingAudioSource> buffering;
        int channels = 1;
        int firstInput = 0;
        juce::int64 lengthFrames = 0;
    };

    juce::AudioFormatManager formats;
    juce::TimeSliceThread thread { "DINELIVE multitrack" };
    juce::File folder;
    std::vector<Track> tracks;
    std::vector<Source> sources;
    juce::AudioBuffer<float> scratch;
    int totalChannels = 0;
    double fileRate = 0.0, deviceRate = 0.0;
    juce::int64 longestFrames = 0;      // in file samples
    juce::int64 loopDeviceFrames = 0;   // in device samples
    std::atomic<juce::int64> position { 0 };
    std::atomic<bool> rewindRequested { false };
    bool prepared = false;
};

} // namespace livemix
