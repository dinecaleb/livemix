#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "MixEngine.h"
#include "Analysis/AnalysisFifo.h"
#include "Analysis/AnalysisAccumulator.h"
#include "Analysis/AnalysisResult.h"

namespace livemix
{

// TUNE MIX listening: every strip's raw input, every strip's processed level and
// every bus output are captured at the same time. One wait-free FIFO per stream,
// one worker thread for all of them, one shared trigger ("somebody started
// playing"). The audio thread only pushes; the worker measures; the message
// thread reads the result when the state says Complete.
class MixCapture : public MixTap
{
public:
    enum class State : int { Idle = 0, Waiting, Listening, Processing, Complete, Failed };

    struct Settings
    {
        float seconds = 30.0f;          // listening window once the band is playing
        float triggerDb = -45.0f;       // any strip's 10 ms frame above this starts the window
        float maxWaitSeconds = 30.0f;   // then start anyway (strips that stayed quiet say so)
        float heardDb = -50.0f;         // a strip counts as heard above this
    };

    struct Result
    {
        bool valid = false;
        float seconds = 0.0f;
        std::vector<AnalysisResult> strips;                          // raw input, per strip
        std::vector<OutputStats> processed;                          // after the chain, before the fader, per strip
        std::array<AnalysisResult, int (MixBus::Count)> buses {};    // what each bus chain received (valid only for used buses)
        AnalysisResult masterOutput;                                 // what left the master: the loudness the listener heard
        int droppedFrames = 0;
    };

    MixCapture();
    ~MixCapture() override;

    // Message thread, with the tap not active. Sizes one stream per strip and per used bus.
    void prepare (double sampleRate, const RoutingGraph& graph);

    void start (const Settings& settings);
    void abort();

    State getState() const noexcept { return State (state.load (std::memory_order_acquire)); }
    float getProgress() const noexcept;                 // 0..1 while listening
    bool stripHeard (int strip) const noexcept;         // "Drums ✓" while listening
    int getNumStrips() const noexcept { return numStrips; }

    Result getResult() const;                            // copy; valid after Complete

    // ---- MixTap (audio thread) ----
    bool isActive() const noexcept override { return active.load (std::memory_order_relaxed); }
    void pushStripInput (int strip, const AudioBlockView& raw) noexcept override;
    void pushStripProcessed (int strip, const AudioBlockView& processed) noexcept override;
    void pushBus (MixBus bus, const AudioBlockView& input) noexcept override;
    void pushMasterOutput (const AudioBlockView& output) noexcept override;

private:
    struct Stream
    {
        AnalysisFifo fifo;
        AnalysisAccumulator accumulator;
        int channels = 1;
        bool used = false;
        int captured = 0;
        std::atomic<bool> heard { false };
    };

    void workerLoop();
    int popAll (int maxFrames, bool consume, float triggerLin, bool& triggered);

    double sr = 48000.0;
    int numStrips = 0;
    std::vector<std::unique_ptr<Stream>> strips;
    std::array<Stream, int (MixBus::Count)> buses;
    Stream masterOut;
    std::vector<float> popBuffer;
    std::vector<std::vector<float>> staging;             // one block per stream while waiting for the trigger
    std::vector<int> stagedFrames;

    std::array<std::atomic<float>, kMaxStrips> postPeak {};
    std::array<std::atomic<double>, kMaxStrips> postSumSquares {};
    std::array<std::atomic<long long>, kMaxStrips> postSamples {};

    Settings settings;
    int targetFrames = 0;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> state { int (State::Idle) };
    std::atomic<bool> active { false };
    std::atomic<bool> abortRequested { false };
    std::atomic<bool> shouldExit { false };
    std::atomic<bool> startRequested { false };
    std::atomic<int> progressFrames { 0 };

    mutable std::mutex resultMutex;
    Result result;
};

} // namespace livemix
