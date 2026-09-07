#include "AnalysisEngine.h"
#include "Core/DbUtils.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace livemix
{

AnalysisEngine::AnalysisEngine()
{
    worker = std::thread ([this] { workerLoop(); });
}

AnalysisEngine::~AnalysisEngine()
{
    {
        // The flags must change under the mutex: otherwise the worker can test the
        // wait predicate, lose the CPU, and block after this notify has already fired.
        std::lock_guard<std::mutex> lock (mutex);
        shouldExit.store (true);
        abortRequested.store (true);
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
}

void AnalysisEngine::prepare (double sampleRate, int numChannels)
{
    abort();
    std::lock_guard<std::mutex> lock (mutex);
    sr = sampleRate;
    channels = numChannels < kMaxChannels ? numChannels : kMaxChannels;
    if (channels < 1) channels = 1;

    // ~350 ms of slack at 96 kHz between worker polls.
    fifo.prepare (32768, channels);
    popBuffer.assign (size_t (4096 * channels), 0.0f);
    accumulator.prepare (sr, channels);
}

void AnalysisEngine::startCapture (float seconds, float triggerDb, float maxWaitSeconds)
{
    const auto s = getState();
    if (s == State::Capturing || s == State::Processing || s == State::Waiting) return;
    {
        std::lock_guard<std::mutex> lock (mutex);
        targetFrames = int (seconds * sr);
        triggerLevelDb = triggerDb;
        maxWaitFrames = int (maxWaitSeconds * sr);
        abortRequested.store (false);
        startRequested.store (true);
    }
    cv.notify_all();
}

void AnalysisEngine::abort()
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        abortRequested.store (true);
        startRequested.store (false); // a queued-but-not-started capture is cancelled too
        fifoActive.store (false);
    }
    cv.notify_all();
    // Wait until the worker acknowledges (it clears the flag when idle).
    for (int i = 0; i < 200 && (getState() == State::Capturing || getState() == State::Processing || getState() == State::Waiting); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
}

float AnalysisEngine::getProgress() const noexcept
{
    if (targetFrames <= 0) return 0.0f;
    return std::min (1.0f, float (progressFrames.load (std::memory_order_relaxed)) / float (targetFrames));
}

AnalysisResult AnalysisEngine::getResult() const
{
    std::lock_guard<std::mutex> lock (resultMutex);
    return result;
}

void AnalysisEngine::setResult (const AnalysisResult& r)
{
    std::lock_guard<std::mutex> lock (resultMutex);
    result = r;
    if (r.valid) state.store (int (State::Complete), std::memory_order_release);
}

void AnalysisEngine::workerLoop()
{
    while (! shouldExit.load())
    {
        {
            std::unique_lock<std::mutex> lock (mutex);
            cv.wait (lock, [this] { return startRequested.load() || shouldExit.load(); });
            if (shouldExit.load()) return;
            startRequested.store (false);
            accumulator.reset();
            capturedFrames = 0;
            progressFrames.store (0);
            fifo.clear();
            fifo.resetDropped();
        }

        const int frameSize = accumulator.getFrameSize();
        const int maxPop = int (popBuffer.size()) / channels;
        const int triggerPop = std::max (1, std::min (maxPop, frameSize)); // 10 ms decisions
        const float triggerLin = triggerLevelDb > -150.0f ? dbToGain (triggerLevelDb) : -1.0f;

        // Waiting: discard audio until a 10 ms frame exceeds the trigger (or the wait times out).
        if (triggerLin >= 0.0f)
        {
            state.store (int (State::Waiting), std::memory_order_release);
            fifoActive.store (true, std::memory_order_release);
            int waited = 0;
            bool triggered = false;
            while (! triggered && ! abortRequested.load() && ! shouldExit.load())
            {
                const int n = fifo.pop (popBuffer.data(), triggerPop);
                if (n <= 0) { std::this_thread::sleep_for (std::chrono::milliseconds (2)); continue; }
                float peak = 0.0f;
                for (int i = 0; i < n * channels; ++i) peak = std::max (peak, std::fabs (popBuffer[size_t (i)]));
                if (peak >= triggerLin)
                {
                    triggered = true;
                    accumulator.consume (popBuffer.data(), n); // the onset block belongs to the capture
                    capturedFrames += n;
                }
                else
                {
                    waited += n;
                    if (maxWaitFrames > 0 && waited >= maxWaitFrames) triggered = true; // start anyway
                }
            }
        }
        else
        {
            fifoActive.store (true, std::memory_order_release);
        }

        state.store (int (State::Capturing), std::memory_order_release);
        progressFrames.store (capturedFrames, std::memory_order_relaxed);

        while (capturedFrames < targetFrames && ! abortRequested.load() && ! shouldExit.load())
        {
            int n = fifo.pop (popBuffer.data(), maxPop);
            if (n > 0)
            {
                if (capturedFrames + n > targetFrames) n = targetFrames - capturedFrames;
                accumulator.consume (popBuffer.data(), n);
                capturedFrames += n;
                progressFrames.store (capturedFrames, std::memory_order_relaxed);
            }
            else
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (3));
            }
        }

        fifoActive.store (false, std::memory_order_release);

        if (abortRequested.load() || shouldExit.load())
        {
            state.store (int (State::Idle), std::memory_order_release);
            continue;
        }

        state.store (int (State::Processing), std::memory_order_release);
        AnalysisResult r = accumulator.finalise (fifo.getDroppedFrames());
        {
            std::lock_guard<std::mutex> lock (resultMutex);
            result = r;
        }
        state.store (int (r.valid ? State::Complete : State::Failed), std::memory_order_release);
    }
}

} // namespace livemix
