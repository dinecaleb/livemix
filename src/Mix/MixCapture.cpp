#include "MixCapture.h"
#include "Core/DbUtils.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kFifoFrames = 16384;     // ~340 ms at 48 kHz per stream between worker rounds
    constexpr int kPopFrames = 4096;
    constexpr int kStallMs = 5000;         // no audio for this long: finish with what we have

    float blockPeak (const float* interleaved, int frames, int channels) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < frames * channels; ++i) p = std::max (p, std::fabs (interleaved[i]));
        return p;
    }
}

MixCapture::MixCapture()
{
    for (auto& p : postPeak) p.store (0.0f);
    for (auto& s : postSumSquares) s.store (0.0);
    for (auto& c : postSamples) c.store (0);
    worker = std::thread ([this] { workerLoop(); });
}

MixCapture::~MixCapture()
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        shouldExit.store (true);
        abortRequested.store (true);
        active.store (false);
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
}

void MixCapture::prepare (double sampleRate, const RoutingGraph& graph)
{
    abort();
    std::lock_guard<std::mutex> lock (mutex);
    sr = sampleRate;
    numStrips = graph.numStrips();

    strips.clear();
    for (int i = 0; i < numStrips; ++i)
    {
        auto s = std::make_unique<Stream>();
        s->channels = graph.strips[size_t (i)].numChannels();
        s->used = true;
        s->fifo.prepare (kFifoFrames, s->channels);
        s->accumulator.prepare (sr, s->channels);
        strips.push_back (std::move (s));
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        auto& s = buses[size_t (b)];
        s.channels = 2;
        s.used = graph.busUsed[size_t (b)];
        if (s.used)
        {
            s.fifo.prepare (kFifoFrames, 2);
            s.accumulator.prepare (sr, 2);
        }
    }
    masterOut.channels = 2;
    masterOut.used = true;
    masterOut.fifo.prepare (kFifoFrames, 2);
    masterOut.accumulator.prepare (sr, 2);
    popBuffer.assign (size_t (kPopFrames * kMaxChannels), 0.0f);
    const int frameSize = std::max (1, int (sr / 100.0));
    staging.assign (size_t (numStrips + int (MixBus::Count) + 1), std::vector<float> (size_t (frameSize * kMaxChannels), 0.0f));
    stagedFrames.assign (staging.size(), 0);
}

void MixCapture::start (const Settings& s)
{
    const auto st = getState();
    if (st == State::Waiting || st == State::Listening || st == State::Processing) return;
    {
        std::lock_guard<std::mutex> lock (mutex);
        settings = s;
        targetFrames = int (s.seconds * sr);
        abortRequested.store (false);
        startRequested.store (true);
    }
    cv.notify_all();
}

void MixCapture::abort()
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        abortRequested.store (true);
        startRequested.store (false);
        active.store (false);
    }
    cv.notify_all();
    for (int i = 0; i < 500 && (getState() == State::Waiting || getState() == State::Listening || getState() == State::Processing); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
}

float MixCapture::getProgress() const noexcept
{
    if (targetFrames <= 0) return 0.0f;
    return std::min (1.0f, float (progressFrames.load (std::memory_order_relaxed)) / float (targetFrames));
}

bool MixCapture::stripHeard (int strip) const noexcept
{
    return strip >= 0 && strip < numStrips && strips[size_t (strip)]->heard.load (std::memory_order_relaxed);
}

MixCapture::Result MixCapture::getResult() const
{
    std::lock_guard<std::mutex> lock (resultMutex);
    return result;
}

// ---- audio thread ----

void MixCapture::pushStripInput (int strip, const AudioBlockView& raw) noexcept
{
    if (! active.load (std::memory_order_relaxed) || strip < 0 || strip >= numStrips) return;
    strips[size_t (strip)]->fifo.push (raw);
}

void MixCapture::pushStripProcessed (int strip, const AudioBlockView& processed) noexcept
{
    if (! active.load (std::memory_order_relaxed) || strip < 0 || strip >= numStrips) return;
    float peak = postPeak[size_t (strip)].load (std::memory_order_relaxed);
    double sq = 0.0;
    for (int ch = 0; ch < processed.numChannels; ++ch)
    {
        const float* d = processed.channels[ch];
        for (int i = 0; i < processed.numSamples; ++i)
        {
            const float a = std::fabs (d[i]);
            if (a > peak) peak = a;
            sq += double (d[i]) * d[i];
        }
    }
    postPeak[size_t (strip)].store (peak, std::memory_order_relaxed);
    postSumSquares[size_t (strip)].store (postSumSquares[size_t (strip)].load (std::memory_order_relaxed) + sq, std::memory_order_relaxed);
    postSamples[size_t (strip)].fetch_add (static_cast<long long> (processed.numSamples) * processed.numChannels, std::memory_order_relaxed);
}

void MixCapture::pushBus (MixBus bus, const AudioBlockView& input) noexcept
{
    if (! active.load (std::memory_order_relaxed)) return;
    auto& s = buses[size_t (bus)];
    if (s.used) s.fifo.push (input);
}

void MixCapture::pushMasterOutput (const AudioBlockView& output) noexcept
{
    if (! active.load (std::memory_order_relaxed)) return;
    masterOut.fifo.push (output);
}

// ---- worker ----

// One round over every stream. While waiting (consume == false) each stream's next
// 10 ms block is parked in staging and compared with the trigger; the caller feeds the
// staged blocks to the accumulators once something triggered, so the onset is kept.
// While listening (consume == true) everything available goes straight in.
int MixCapture::popAll (int maxFrames, bool consume, float triggerLin, bool& triggered)
{
    const float heardLin = dbToGain (settings.heardDb);
    int total = 0;
    auto handle = [&] (Stream& s, size_t index, bool isStrip)
    {
        if (! s.used) return;
        if (consume)
        {
            int n = s.fifo.pop (popBuffer.data(), std::min (maxFrames, int (popBuffer.size()) / s.channels));
            if (n <= 0) return;
            total += n;
            if (isStrip && blockPeak (popBuffer.data(), n, s.channels) >= heardLin) s.heard.store (true, std::memory_order_relaxed);
            if (s.captured + n > targetFrames) n = targetFrames - s.captured;
            if (n > 0) { s.accumulator.consume (popBuffer.data(), n); s.captured += n; }
        }
        else
        {
            auto& stage = staging[index];
            const int n = s.fifo.pop (stage.data(), std::min (maxFrames, int (stage.size()) / s.channels));
            stagedFrames[index] = n;
            if (n <= 0) return;
            total += n;
            if (isStrip)
            {
                const float peak = blockPeak (stage.data(), n, s.channels);
                if (peak >= triggerLin) triggered = true;
                if (peak >= heardLin) s.heard.store (true, std::memory_order_relaxed);
            }
        }
    };
    for (int i = 0; i < numStrips; ++i) handle (*strips[size_t (i)], size_t (i), true);
    for (int b = 0; b < int (MixBus::Count); ++b) handle (buses[size_t (b)], size_t (numStrips + b), false);
    handle (masterOut, size_t (numStrips + int (MixBus::Count)), false);
    return total;
}

void MixCapture::workerLoop()
{
    while (! shouldExit.load())
    {
        {
            std::unique_lock<std::mutex> lock (mutex);
            cv.wait (lock, [this] { return startRequested.load() || shouldExit.load(); });
            if (shouldExit.load()) return;
            startRequested.store (false);
            for (auto& s : strips) { s->accumulator.reset(); s->captured = 0; s->heard.store (false); s->fifo.clear(); s->fifo.resetDropped(); }
            for (auto& b : buses) if (b.used) { b.accumulator.reset(); b.captured = 0; b.fifo.clear(); b.fifo.resetDropped(); }
            if (masterOut.used) { masterOut.accumulator.reset(); masterOut.captured = 0; masterOut.fifo.clear(); masterOut.fifo.resetDropped(); }
            for (int i = 0; i < kMaxStrips; ++i) { postPeak[size_t (i)].store (0.0f); postSumSquares[size_t (i)].store (0.0); postSamples[size_t (i)].store (0); }
            progressFrames.store (0);
        }
        if (numStrips == 0) { state.store (int (State::Failed), std::memory_order_release); continue; }

        const int frameSize = std::max (1, int (sr / 100.0));
        const float triggerLin = dbToGain (settings.triggerDb);
        const int maxWaitFrames = int (settings.maxWaitSeconds * sr);

        // ---- Waiting for the band (a trigger below -150 dB means: start now) ----
        state.store (int (State::Waiting), std::memory_order_release);
        active.store (true, std::memory_order_release);
        if (settings.triggerDb > -150.0f)
        {
            int waited = 0;
            bool triggered = false;
            while (! triggered && ! abortRequested.load() && ! shouldExit.load())
            {
                const int got = popAll (frameSize, false, triggerLin, triggered);
                if (got <= 0) { std::this_thread::sleep_for (std::chrono::milliseconds (2)); continue; }
                if (! triggered)
                {
                    waited += stagedFrames[0];
                    if (maxWaitFrames > 0 && waited >= maxWaitFrames) triggered = true; // start anyway
                }
            }
            if (triggered)
            {
                // The onset blocks belong to the capture.
                for (int i = 0; i < numStrips; ++i)
                {
                    auto& s = *strips[size_t (i)];
                    const int n = stagedFrames[size_t (i)];
                    if (n > 0) { s.accumulator.consume (staging[size_t (i)].data(), n); s.captured += n; }
                }
                for (int b = 0; b < int (MixBus::Count); ++b)
                {
                    auto& s = buses[size_t (b)];
                    const int n = stagedFrames[size_t (numStrips + b)];
                    if (s.used && n > 0) { s.accumulator.consume (staging[size_t (numStrips + b)].data(), n); s.captured += n; }
                }
                {
                    const size_t idx = size_t (numStrips + int (MixBus::Count));
                    const int n = stagedFrames[idx];
                    if (n > 0) { masterOut.accumulator.consume (staging[idx].data(), n); masterOut.captured += n; }
                }
            }
        }

        // ---- Listening ----
        state.store (int (State::Listening), std::memory_order_release);
        {
            auto lastAudio = std::chrono::steady_clock::now();
            while (! abortRequested.load() && ! shouldExit.load())
            {
                bool done = true;
                int maxCaptured = 0;
                for (auto& s : strips) { done = done && s->captured >= targetFrames; maxCaptured = std::max (maxCaptured, s->captured); }
                progressFrames.store (maxCaptured, std::memory_order_relaxed);
                if (done) break;
                bool unused = false;
                const int got = popAll (kPopFrames, true, triggerLin, unused);
                const auto now = std::chrono::steady_clock::now();
                if (got > 0) lastAudio = now;
                else
                {
                    if (std::chrono::duration_cast<std::chrono::milliseconds> (now - lastAudio).count() > kStallMs) break;
                    std::this_thread::sleep_for (std::chrono::milliseconds (2));
                }
            }
        }
        active.store (false, std::memory_order_release);

        if (abortRequested.load() || shouldExit.load())
        {
            state.store (int (State::Idle), std::memory_order_release);
            continue;
        }

        // ---- Measure ----
        state.store (int (State::Processing), std::memory_order_release);
        Result r;
        r.strips.resize (size_t (numStrips));
        r.processed.resize (size_t (numStrips));
        int maxCaptured = 0;
        for (int i = 0; i < numStrips; ++i)
        {
            auto& s = *strips[size_t (i)];
            r.strips[size_t (i)] = s.accumulator.finalise (s.fifo.getDroppedFrames());
            r.droppedFrames += s.fifo.getDroppedFrames();
            maxCaptured = std::max (maxCaptured, s.captured);
            const long long count = postSamples[size_t (i)].load();
            auto& o = r.processed[size_t (i)];
            o.valid = count > 0;
            o.peakDb = gainToDb (postPeak[size_t (i)].load());
            o.rmsDb = count > 0 ? gainToDb (float (std::sqrt (postSumSquares[size_t (i)].load() / double (count)))) : -120.0f;
        }
        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            auto& s = buses[size_t (b)];
            if (s.used) r.buses[size_t (b)] = s.accumulator.finalise (s.fifo.getDroppedFrames());
        }
        r.masterOutput = masterOut.accumulator.finalise (masterOut.fifo.getDroppedFrames());
        r.seconds = float (maxCaptured / sr);
        r.valid = true;
        for (const auto& a : r.strips) if (! a.valid) r.valid = false;
        {
            std::lock_guard<std::mutex> lock (resultMutex);
            result = r;
        }
        state.store (int (r.valid ? State::Complete : State::Failed), std::memory_order_release);
    }
}

} // namespace livemix
