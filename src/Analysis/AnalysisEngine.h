#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include "AnalysisFifo.h"
#include "AnalysisResult.h"
#include "AnalysisAccumulator.h"

namespace livemix
{

// Owns the capture FIFO and a worker thread. The audio thread only ever calls
// pushAudio(), which is wait-free. Everything else runs on the worker or the
// caller's (message) thread. Results never touch DSP parameters directly.
// The measurements themselves live in AnalysisAccumulator; this class adds the
// FIFO, the trigger ("wait for signal") and the thread.
class AnalysisEngine
{
public:
    enum class State : int { Idle = 0, Capturing, Processing, Complete, Failed, Waiting };

    AnalysisEngine();
    ~AnalysisEngine();

    // Message thread. May allocate. Safe to call again on sample-rate change.
    void prepare (double sampleRate, int numChannels);

    // Message thread: begin a capture of the given length. With a trigger level the
    // engine first waits (State::Waiting) until a 10 ms frame exceeds triggerDb, so the
    // capture window is not wasted on silence; after maxWaitSeconds it starts anyway.
    void startCapture (float seconds, float triggerDb = -200.0f, float maxWaitSeconds = 0.0f);
    void abort();

    // Audio thread: wait-free.
    void pushAudio (const AudioBlockView& block) noexcept
    {
        if (fifoActive.load (std::memory_order_relaxed))
            fifo.push (block);
    }

    State getState() const noexcept { return State (state.load (std::memory_order_acquire)); }
    bool isCapturing() const noexcept { return getState() == State::Capturing; }
    bool isWaitingForSignal() const noexcept { return getState() == State::Waiting; }
    bool isActive() const noexcept { return fifoActive.load (std::memory_order_relaxed); } // waiting or capturing
    float getProgress() const noexcept; // 0..1 during capture

    // Message thread: copy of the most recent completed result.
    AnalysisResult getResult() const;
    void setResult (const AnalysisResult& r); // for state restore

private:
    void workerLoop();

    AnalysisFifo fifo;
    std::vector<float> popBuffer;
    AnalysisAccumulator accumulator;

    double sr = 48000.0;
    int channels = 1;
    int targetFrames = 0;
    int capturedFrames = 0;
    float triggerLevelDb = -200.0f;
    int maxWaitFrames = 0;

    // Threading
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> state { int (State::Idle) };
    std::atomic<bool> fifoActive { false };
    std::atomic<bool> abortRequested { false };
    std::atomic<bool> shouldExit { false };
    std::atomic<bool> startRequested { false };
    std::atomic<int> progressFrames { 0 };

    mutable std::mutex resultMutex;
    AnalysisResult result;
};

} // namespace livemix
