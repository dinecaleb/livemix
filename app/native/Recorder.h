#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Project.h"
#include "Core/Realtime.h"

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
//
// A take is readable even if DINE dies in the middle of it. A WAV header carries its sizes,
// and a writer that only fills them in on close leaves an unreadable file behind a crash, so
// while a take is being written the writer thread (never the audio thread) rewrites the
// header every headerFlushSeconds of audio, and keeps a sidecar beside each file -
// "<take>.wav.recording.json": sample rate, channels, track, where on the timeline, frames so
// far - updated every sidecarSeconds. The sidecar is deleted by a clean stop, so one that is
// still there on the next open *is* the detection: recoverUnfinishedTakes() rebuilds the
// header from what is actually on disk and hands the take back so it can go on its track.
class Recorder
{
public:
    // `fifoSamples` 0 = the product's size; a test passes a small one to make the disk fall behind.
    Recorder (double sidecarSeconds = 20.0, double headerFlushSeconds = 15.0, int fifoSamples = 0);
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
        int inputA = -1, inputB = -1;           // the device channels it recorded: which input it is, however the list moved
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
    // How much of the take, in seconds of one track, had to become silence because the disk fell
    // behind (every track kept its place; see write()). Zero on a healthy take. Any thread.
    double getDroppedSeconds() const noexcept
    {
        const auto n = droppedSamples.load (std::memory_order_relaxed);
        return writers.empty() || rate <= 0.0 ? 0.0 : double (n) / rate / double (writers.size());
    }
    // The same, as a fixed word for the stability events: "buffer_too_large", "disk_too_slow" or "".
    const char* getErrorCode() const noexcept;

    // How fast a take fills the disk, and how long the volume would last at that rate.
    // Message thread only (getBytesFreeOnVolume is a syscall): the UI polls it a few times
    // a minute, never per block.
    double bytesPerSecond() const noexcept;
    static double bytesPerSecondFor (const std::vector<Spec>& specs, double sampleRate) noexcept;
    static double secondsFreeOn (const juce::File& folder, double bytesPerSec) noexcept;

    // ---- Recovery ----

    // A take that was still being written when DINE last closed.
    struct Recovered
    {
        int trackIndex = -1;
        // The device channels it was recording (L, R; R < 0 = mono). -2 on a sidecar written
        // before 2026-10-05, which only knew the track's index.
        int inputA = -2, inputB = -2;
        juce::String name, fileName;
        juce::int64 length = 0;             // frames the file holds now
        juce::int64 timelineStart = 0;
        double sampleRate = 0.0;
        int channels = 0;
        bool repaired = false;              // false: `note` says why not (the sidecar is kept so the next open tries again)
        juce::String note;
    };

    // Message thread, on opening a session. Every sidecar left in the folder is a take that
    // never got its header; each is repaired from the bytes on disk (an empty one is removed)
    // and returned so the host can put it on the timeline.
    static std::vector<Recovered> recoverUnfinishedTakes (const juce::File& audioFolder);

    // Rewrites the RIFF and data sizes of a WAV from its length on disk (a trailing partial
    // frame is ignored). Returns the frames it holds, or -1 with `error` set: not a WAV, no
    // fmt/data chunk, or a file over 4 GB, which needs an RF64 header this does not write.
    static juce::int64 repairWavHeader (const juce::File& wav, juce::String& error);

    static juce::File sidecarFor (const juce::File& take);

    // Audio thread. Captures the device inputs exactly as they arrived.
    void write (const float* const* deviceInputs, int numInputChannels, int numSamples) noexcept LIVEMIX_NONBLOCKING;

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
        // Writer thread only: how big the file was, and how much audio had arrived, when it
        // last grew. A file that stops growing while audio keeps arriving is a disk that has
        // stopped taking it (see checkDisk).
        juce::int64 lastSize = 0;
        juce::int64 framesAtGrowth = 0;
        juce::int64 owedSilence = 0;            // audio thread only: samples this track could not take, still to be written as silence
    };
    // Writer thread, every sidecar interval: is the take still landing? JUCE's threaded writer
    // drops a failed write without saying so, so this is where a full disk or a drive pulled
    // out is found - by the file, not by the writer.
    void checkDisk();

    static juce::File uniqueTakeFile (const juce::File& folder, const juce::String& trackName);

    // The sidecars, written from the writer thread (a TimeSliceClient on the same thread the
    // audio is flushed by). Everything but framesWritten is fixed at start; that one is the
    // recorder's own atomic.
    struct Sidecar
    {
        juce::File file;
        int trackIndex = 0, channels = 1;
        juce::String name;
        int inputA = -1, inputB = -1;
    };
    struct SidecarWriter : public juce::TimeSliceClient
    {
        explicit SidecarWriter (Recorder& r) : owner (r) {}
        int useTimeSlice() override;
        Recorder& owner;
    };
    void writeSidecars();

    juce::TimeSliceThread thread { "DINE recorder" };
    SidecarWriter sidecarWriter { *this };
    std::vector<Sidecar> sidecars;
    int fifoSamples = 0;
    int sidecarMs = 20000;
    double headerFlushSeconds = 15.0;
    std::vector<Writer> writers;
    std::vector<float> silence;                 // a missing device channel records as silence, not as garbage
    std::atomic<bool> active { false };
    std::atomic<bool> inCallback { false };
    std::atomic<juce::int64> frames { 0 };
    std::atomic<juce::int64> droppedSamples { 0 };   // summed over tracks: audio the disk fell too far behind to take
    std::atomic<bool> failed { false };
    std::atomic<bool> oversized { false };
    std::atomic<int> diskFault { 0 };        // 1 = the disk is full, 2 = the take stopped reaching the disk
    juce::int64 startSample = 0;
    double rate = 48000.0;
};

} // namespace livemix
