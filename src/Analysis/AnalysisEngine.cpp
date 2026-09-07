#include "AnalysisEngine.h"
#include "Core/DbUtils.h"
#include "DSP/LoudnessMeter.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace livemix
{

namespace
{
    constexpr int kFftSize = 4096;
    constexpr int kFftHop = 2048;
    constexpr int kHistogramBins = 101; // -100 .. 0 dB
    constexpr int kRecentFrames = 30;   // 300 ms floor tracking window
    constexpr float kSilenceThresholdDb = -60.0f;
    constexpr float kDigitalSilenceDb = -90.0f;
    constexpr int kSibilanceBins = 71;      // -60 .. +10 dB
    constexpr float kSibilanceLoudDb = -40.0f; // frames below this say nothing about S sounds
}

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
    analysisFrameSize = std::max (1, int (sr / 100.0));
    levelHistogram.assign (kHistogramBins, 0);
    recentFrameDb.assign (kRecentFrames, -120.0f);

    sibilanceHpf.setCoefficients (BiquadCoefficients::make (FilterType::HighPass, sr, 5000.0f, 0.7071f, 0.0f));
    sibilanceHistogram.assign (kSibilanceBins, 0);
    kShelf.setCoefficients (LoudnessMeter::kWeightingShelf (sr));
    kHighpass.setCoefficients (LoudnessMeter::kWeightingHighPass (sr));

    fft.prepare (kFftSize);
    fftInput.assign (kFftSize, 0.0f);
    window.resize (kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[size_t (i)] = 0.5f * (1.0f - std::cos (2.0f * float (M_PI) * float (i) / float (kFftSize - 1)));
    powerAccum.assign (kFftSize / 2 + 1, 0.0f);
    powerScratch.assign (kFftSize / 2 + 1, 0.0f);
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

void AnalysisEngine::resetAccumulators()
{
    capturedFrames = 0;
    sumSquares = sumSamples = 0.0;
    channelSumSquares.fill (0.0);
    peakAbs = 0.0f;
    clipCount = 0;
    analysisFramePos = 0;
    frameSumSquares = 0.0;
    std::fill (levelHistogram.begin(), levelHistogram.end(), 0);
    totalAnalysisFrames = silentFrames = 0;
    prevFrameDb = -120.0f;
    std::fill (recentFrameDb.begin(), recentFrameDb.end(), -120.0f);
    recentPos = 0;
    transientCount = 0;
    transientRiseSum = 0.0;
    decayTracking = false;
    decayPeakDb = -120.0f;
    decayFrames = 0;
    decaySumMs = 0.0;
    decayCount = 0;
    sibilanceHpf.reset();
    frameHighSumSquares = 0.0;
    std::fill (sibilanceHistogram.begin(), sibilanceHistogram.end(), 0);
    sibilanceFrames = sibilantFrames = 0;
    sumLR = 0.0;
    kShelf.reset(); kHighpass.reset();
    kSumSquares.fill (0.0);
    interpPeak = 0.0f;
    lastSample.fill (0.0f);
    std::fill (fftInput.begin(), fftInput.end(), 0.0f);
    std::fill (powerAccum.begin(), powerAccum.end(), 0.0f);
    fftPos = 0;
    fftFrames = 0;
    progressFrames.store (0);
    fifo.clear();
    fifo.resetDropped();
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
            resetAccumulators();
        }

        const int maxPop = int (popBuffer.size()) / channels;
        const int triggerPop = std::max (1, std::min (maxPop, analysisFrameSize)); // 10 ms decisions
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
                    consumeFrames (popBuffer.data(), n); // the onset block belongs to the capture
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
                consumeFrames (popBuffer.data(), n);
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
        finalise();
        state.store (int (result.valid ? State::Complete : State::Failed), std::memory_order_release);
    }
}

void AnalysisEngine::consumeFrames (const float* interleaved, int numFrames)
{
    for (int i = 0; i < numFrames; ++i)
    {
        const float* frame = interleaved + i * channels;
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float x = frame[ch];
            mono += x;
            channelSumSquares[size_t (ch)] += double (x) * x;
            const float a = std::fabs (x);
            if (a > peakAbs) peakAbs = a;
            if (a >= 0.9999f) ++clipCount;
            // K-weighted energy for loudness, interpolated peak for true peak.
            const float k = kHighpass.processSample (ch, kShelf.processSample (ch, x));
            kSumSquares[size_t (ch)] += double (k) * k;
            const float prev = lastSample[size_t (ch)];
            const float mid = std::fabs (prev + 0.5f * (x - prev));
            if (mid > interpPeak) interpPeak = mid;
            if (a > interpPeak) interpPeak = a;
            lastSample[size_t (ch)] = x;
        }
        if (channels > 1) sumLR += double (frame[0]) * frame[1];
        mono /= float (channels);

        sumSquares += double (mono) * mono;
        sumSamples += mono;
        frameSumSquares += double (mono) * mono;
        const float high = sibilanceHpf.processSample (0, mono);
        frameHighSumSquares += double (high) * high;

        fftInput[size_t (fftPos)] = mono;
        if (++fftPos == kFftSize)
        {
            processAnalysisFrame();
            // Slide by hop
            std::copy (fftInput.begin() + kFftHop, fftInput.end(), fftInput.begin());
            fftPos = kFftSize - kFftHop;
        }

        if (++analysisFramePos >= analysisFrameSize)
        {
            const float frameRms = float (std::sqrt (frameSumSquares / analysisFramePos));
            const float frameDb = gainToDb (frameRms);
            ++totalAnalysisFrames;
            if (frameDb < kSilenceThresholdDb) ++silentFrames;

            int bin = int (std::lround (frameDb)) + 100;
            bin = std::clamp (bin, 0, kHistogramBins - 1);
            ++levelHistogram[size_t (bin)];

            const float floorDb = *std::min_element (recentFrameDb.begin(), recentFrameDb.end());

            if (frameDb > kSibilanceLoudDb)
            {
                const float highDb = gainToDb (float (std::sqrt (frameHighSumSquares / analysisFramePos)));
                const float diff = highDb - frameDb;
                int sb = int (std::lround (diff)) + 60;
                sb = std::clamp (sb, 0, kSibilanceBins - 1);
                ++sibilanceHistogram[size_t (sb)];
                ++sibilanceFrames;
                if (diff > -6.0f) ++sibilantFrames;
            }
            frameHighSumSquares = 0.0;

            const float rise = frameDb - prevFrameDb;
            if (rise >= 8.0f && frameDb > kSilenceThresholdDb && frameDb > floorDb + 12.0f)
            {
                ++transientCount;
                transientRiseSum += rise;
                decayTracking = true;   // a new hit: measure how long it takes to fall 20 dB
                decayPeakDb = frameDb;
                decayFrames = 0;
            }
            else if (decayTracking)
            {
                if (frameDb > decayPeakDb) { decayPeakDb = frameDb; decayFrames = 0; }
                else
                {
                    ++decayFrames;
                    const bool fell = frameDb <= decayPeakDb - 20.0f;
                    const bool tooLong = decayFrames >= 200; // 2 s: sustained, count as 2 s
                    if (fell || tooLong)
                    {
                        decaySumMs += double (decayFrames) * 1000.0 * double (analysisFrameSize) / sr;
                        ++decayCount;
                        decayTracking = false;
                    }
                }
            }
            prevFrameDb = frameDb;
            recentFrameDb[size_t (recentPos)] = frameDb;
            recentPos = (recentPos + 1) % kRecentFrames;

            analysisFramePos = 0;
            frameSumSquares = 0.0;
        }
    }
}

void AnalysisEngine::processAnalysisFrame()
{
    // Windowed FFT of the current 4096-sample block into the power accumulator.
    static thread_local std::vector<float> windowed;
    if (windowed.size() != size_t (kFftSize)) windowed.assign (kFftSize, 0.0f);
    for (int i = 0; i < kFftSize; ++i)
        windowed[size_t (i)] = fftInput[size_t (i)] * window[size_t (i)];
    fft.forwardPower (windowed.data(), powerScratch.data());
    for (size_t k = 0; k < powerAccum.size(); ++k)
        powerAccum[k] += powerScratch[k];
    ++fftFrames;
}

void AnalysisEngine::finalise()
{
    AnalysisResult r;
    r.sampleRate = sr;
    r.numChannels = channels;
    r.durationSeconds = float (capturedFrames / sr);
    r.droppedFrames = fifo.getDroppedFrames();

    if (capturedFrames < analysisFrameSize * 10)
    {
        r.valid = false;
        std::lock_guard<std::mutex> lock (resultMutex);
        result = r;
        return;
    }

    const double n = double (capturedFrames);
    const float rms = float (std::sqrt (sumSquares / n));
    r.peakDb = gainToDb (peakAbs);
    r.rmsDb = gainToDb (rms);
    r.crestFactorDb = r.peakDb - r.rmsDb;
    r.dcOffset = float (sumSamples / n);
    r.clipCount = clipCount;
    r.silencePercent = totalAnalysisFrames > 0 ? 100.0f * float (silentFrames) / float (totalAnalysisFrames) : 100.0f;

    for (int ch = 0; ch < channels; ++ch)
        r.channelRmsDb[size_t (ch)] = gainToDb (float (std::sqrt (channelSumSquares[size_t (ch)] / n)));
    r.stereoBalanceDb = channels > 1 ? r.channelRmsDb[0] - r.channelRmsDb[1] : 0.0f;
    if (channels > 1)
    {
        const double denom = std::sqrt (channelSumSquares[0] * channelSumSquares[1]);
        r.stereoCorrelation = denom > 1.0e-12 ? float (sumLR / denom) : 1.0f;
    }
    {
        double k = 0.0;
        for (int ch = 0; ch < channels; ++ch) k += kSumSquares[size_t (ch)] / n;
        r.loudnessLufs = LoudnessMeter::lufsFromMeanSquare (k);
        r.truePeakDb = gainToDb (interpPeak);
    }
    if (sibilanceFrames >= 10)
    {
        const int target = std::max (1, int (std::ceil (0.95f * float (sibilanceFrames))));
        int acc = 0;
        for (int b = 0; b < kSibilanceBins; ++b)
        {
            acc += sibilanceHistogram[size_t (b)];
            if (acc >= target) { r.sibilanceDb = float (b - 60); break; }
        }
        r.sibilancePercent = 100.0f * float (sibilantFrames) / float (sibilanceFrames);
    }

    // Percentiles over frames above digital silence.
    int active = 0;
    const int firstActiveBin = int (kDigitalSilenceDb) + 100;
    for (int b = firstActiveBin; b < kHistogramBins; ++b) active += levelHistogram[size_t (b)];
    auto percentile = [&] (float pct) -> float
    {
        if (active == 0) return -120.0f;
        const int target = std::max (1, int (std::ceil (pct * float (active))));
        int acc = 0;
        for (int b = firstActiveBin; b < kHistogramBins; ++b)
        {
            acc += levelHistogram[size_t (b)];
            if (acc >= target) return float (b - 100);
        }
        return 0.0f;
    };
    r.noiseFloorDb = percentile (0.10f);
    r.hitLevelDb = percentile (0.95f);
    r.dynamicRangeDb = r.hitLevelDb - r.noiseFloorDb;

    r.transientCount = transientCount;
    r.transientsPerSecond = r.durationSeconds > 0.0f ? float (transientCount) / r.durationSeconds : 0.0f;
    r.meanTransientRiseDb = transientCount > 0 ? float (transientRiseSum / transientCount) : 0.0f;
    r.decayCount = decayCount;
    r.meanDecayMs = decayCount > 0 ? float (decaySumMs / decayCount) : 0.0f;

    // Spectrum
    if (fftFrames > 0)
    {
        const double binHz = sr / double (kFftSize);
        double total = 0.0;
        double centroidNum = 0.0;
        double above5k = 0.0, below5k = 0.0;
        for (size_t k = 1; k < powerAccum.size(); ++k)
        {
            const double f = double (k) * binHz;
            const double p = double (powerAccum[k]);
            total += p;
            centroidNum += p * f;
            if (f >= 5000.0) above5k += p; else below5k += p;
        }
        if (total <= 0.0) total = 1.0e-30;
        r.spectralCentroidHz = float (centroidNum / total);
        r.highFrequencyRatioDb = float (10.0 * std::log10 ((above5k + 1.0e-30) / (below5k + 1.0e-30)));

        // Fundamental: strongest local maximum between 30 and 500 Hz, parabolic-interpolated.
        {
            size_t best = 0;
            double bestP = 0.0;
            const size_t kLo = size_t (std::ceil (30.0 / binHz)), kHi = size_t (std::floor (500.0 / binHz));
            for (size_t k = std::max<size_t> (kLo, 1); k <= kHi && k + 1 < powerAccum.size(); ++k)
            {
                const double p = double (powerAccum[k]);
                if (p > bestP && p >= double (powerAccum[k - 1]) && p >= double (powerAccum[k + 1])) { bestP = p; best = k; }
            }
            if (best > 0 && bestP > 0.0)
            {
                const double a = std::log (double (powerAccum[best - 1]) + 1.0e-30);
                const double b = std::log (bestP + 1.0e-30);
                const double c = std::log (double (powerAccum[best + 1]) + 1.0e-30);
                const double denom = a - 2.0 * b + c;
                const double delta = std::fabs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0;
                r.fundamentalHz = float ((double (best) + std::clamp (delta, -0.5, 0.5)) * binHz);
                // Level of the peak region (±1 bin) relative to the total.
                const double region = bestP + double (powerAccum[best - 1]) + double (powerAccum[best + 1]);
                r.fundamentalLevelDb = float (10.0 * std::log10 ((region + 1.0e-30) / total));
            }
        }

        for (int b = 0; b < int (Band::Count); ++b)
        {
            const double lo = kBandEdgesHz[size_t (b)], hi = kBandEdgesHz[size_t (b + 1)];
            double e = 0.0;
            for (size_t k = 1; k < powerAccum.size(); ++k)
            {
                const double f = double (k) * binHz;
                if (f >= lo && f < hi) e += double (powerAccum[k]);
            }
            r.bandEnergyDb[size_t (b)] = float (10.0 * std::log10 ((e + 1.0e-30) / total));
        }

        for (int b = 0; b < kNumThirdOctaveBands; ++b)
        {
            const double centre = double (thirdOctaveCentreHz (b));
            const double lo = centre / std::pow (2.0, 1.0 / 6.0);
            const double hi = centre * std::pow (2.0, 1.0 / 6.0);
            double e = 0.0;
            int count = 0;
            for (size_t k = 1; k < powerAccum.size(); ++k)
            {
                const double f = double (k) * binHz;
                if (f >= lo && f < hi) { e += double (powerAccum[k]); ++count; }
            }
            // Normalise by bandwidth so the curve reflects spectral density.
            const double density = count > 0 ? e / double (count) : 0.0;
            r.thirdOctaveDb[size_t (b)] = float (10.0 * std::log10 ((density + 1.0e-30) / total));
        }

        // Resonance detection: prominence above a 5-band moving average.
        for (int b = 2; b < kNumThirdOctaveBands - 2; ++b)
        {
            const float centreHz = thirdOctaveCentreHz (b);
            if (centreHz < 60.0f || centreHz > 8000.0f) continue;
            float trend = 0.0f;
            for (int k = -2; k <= 2; ++k) if (k != 0) trend += r.thirdOctaveDb[size_t (b + k)];
            trend *= 0.25f;
            const float prominence = r.thirdOctaveDb[size_t (b)] - trend;
            const bool localMax = r.thirdOctaveDb[size_t (b)] > r.thirdOctaveDb[size_t (b - 1)]
                               && r.thirdOctaveDb[size_t (b)] >= r.thirdOctaveDb[size_t (b + 1)];
            if (prominence >= 5.0f && localMax && r.thirdOctaveDb[size_t (b)] > -40.0f)
                r.resonances.push_back ({ centreHz, prominence });
        }
        std::sort (r.resonances.begin(), r.resonances.end(),
                   [] (const ResonancePeak& a, const ResonancePeak& b) { return a.prominenceDb > b.prominenceDb; });
        if (r.resonances.size() > 3) r.resonances.resize (3);
    }

    // Bleed: floor within 10 dB of hits -> 1, 40 dB below -> 0.
    r.bleedEstimate = clamp ((r.noiseFloorDb - (r.hitLevelDb - 40.0f)) / 30.0f, 0.0f, 1.0f);
    if (r.silencePercent > 95.0f) r.bleedEstimate = 0.0f;

    r.valid = true;
    std::lock_guard<std::mutex> lock (resultMutex);
    result = r;
}

} // namespace livemix
