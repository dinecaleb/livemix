#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Project.h"

namespace livemix
{

// Multitrack recording of the RAW device inputs. The audio thread only pushes into a
// lock-free FIFO per track (juce::AudioFormatWriter::ThreadedWriter); a background
// thread does every file operation. Nothing here allocates or blocks on the audio
// thread, and the processed mix is never the only copy of a performance.
//
// One WAV per armed track per take, named "<Track>_001.wav" in the project's
// "Audio Files" folder. A take that cannot be written (disk full, no permission) stops
// recording and says so instead of failing quietly.
class Recorder
{
public:
    Recorder();
    ~Recorder();

    // One armed track: which device channels it captures and what to call the file.
    struct Spec
    {
        int trackIndex = 0;
        juce::String name = "Track";
        int inputA = -1, inputB = -1;   // device channel indices; inputB < 0 = mono
    };

    // What one track produced.
    struct Take
    {
        int trackIndex = 0;
        juce::String name;
        juce::String fileName;
        juce::int64 length = 0;
    };

    // Message thread. Creates the files and starts the writer thread. "" on success.
    juce::String start (const juce::File& audioFolder,
                        const std::vector<Spec>& specs,
                        double sampleRate,
                        juce::int64 timelineStart);

    // Message thread. Finishes the files and returns one take per track that captured audio.
    std::vector<Take> stop();

    bool isRecording() const noexcept { return active.load (std::memory_order_acquire); }
    juce::int64 getTimelineStart() const noexcept { return startSample; }
    juce::int64 getFramesWritten() const noexcept { return frames.load (std::memory_order_relaxed); }
    // Non-empty once a write failed (a full or too-slow disk). Recording should be stopped and the user told.
    juce::String getError() const;

    // Audio thread. Captures the device inputs exactly as they arrived.
    void write (const float* const* deviceInputs, int numInputChannels, int numSamples) noexcept;

private:
    struct Writer
    {
        int trackIndex = 0;
        juce::String name;
        juce::File file;
        int channels = 1;
        int inputA = -1, inputB = -1;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        std::array<const float*, 2> ptrs {};
    };

    static juce::File uniqueTakeFile (const juce::File& folder, const juce::String& trackName);

    juce::TimeSliceThread thread { "DINELIVE recorder" };
    std::vector<Writer> writers;
    std::vector<float> silence;                 // a missing device channel records as silence, not as garbage
    std::atomic<bool> active { false };
    std::atomic<bool> inCallback { false };
    std::atomic<juce::int64> frames { 0 };
    std::atomic<bool> failed { false };
    juce::int64 startSample = 0;
    double rate = 48000.0;
};

} // namespace livemix
