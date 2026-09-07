#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include "AnalysisFifo.h"
#include "AnalysisResult.h"
#include "Core/FFT.h"
#include "DSP/Biquad.h"

namespace livemix
{

// Owns the capture FIFO and a worker thread. The audio thread only ever calls
// pushAudio(), which is wait-free. Everything else runs on the worker or the
// caller's (message) thread. Results never touch DSP parameters directly.
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
    void resetAccumulators();
    void consumeFrames (const float* interleaved, int numFrames);
    void processAnalysisFrame();
    void finalise();

    AnalysisFifo fifo;
    std::vector<float> popBuffer;

    // Accumulators (worker thread only)
    double sr = 48000.0;
    int channels = 1;
    int targetFrames = 0;
    int capturedFrames = 0;
    float triggerLevelDb = -200.0f;
    int maxWaitFrames = 0;

    double sumSquares = 0.0, sumSamples = 0.0;
    std::array<double, kMaxChannels> channelSumSquares {};
    float peakAbs = 0.0f;
    int clipCount = 0;

    int analysisFrameSize = 480; // 10 ms
    int analysisFramePos = 0;
    double frameSumSquares = 0.0;
    std::vector<int> levelHistogram; // 1 dB bins from -100 .. 0
    int totalAnalysisFrames = 0, silentFrames = 0;
    float prevFrameDb = -120.0f;
    std::vector<float> recentFrameDb; // small ring for floor tracking
    int recentPos = 0;
    int transientCount = 0;
    double transientRiseSum = 0.0;
    bool decayTracking = false;
    float decayPeakDb = -120.0f;
    int decayFrames = 0;
    double decaySumMs = 0.0;
    int decayCount = 0;

    // Sibilance: high band (>= 5 kHz) vs full band per 10 ms frame, histogram of the difference on loud frames.
    Biquad sibilanceHpf;
    double frameHighSumSquares = 0.0;
    std::vector<int> sibilanceHistogram; // 1 dB bins, -60 .. +10 dB
    int sibilanceFrames = 0, sibilantFrames = 0;
    // Stereo correlation and BS.1770 loudness
    double sumLR = 0.0;
    Biquad kShelf, kHighpass;
    std::array<double, kMaxChannels> kSumSquares {};
    float interpPeak = 0.0f;
    std::array<float, kMaxChannels> lastSample {};

    RealFFT fft;
    std::vector<float> fftInput, window, powerAccum, powerScratch;
    int fftPos = 0;
    int fftFrames = 0;

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
