#pragma once
#include <array>
#include <vector>
#include "AnalysisResult.h"
#include "Core/Constants.h"
#include "Core/AudioBlockView.h"
#include "Core/FFT.h"
#include "DSP/Biquad.h"

namespace livemix
{

// The measurement half of the analysis: feed it interleaved frames, ask for the
// result. Single-threaded, no FIFO, no thread of its own: AnalysisEngine drives
// one from its worker, MixCapture drives one per input from a shared worker,
// offline tools call it directly. Allocates only in prepare().
class AnalysisAccumulator
{
public:
    void prepare (double sampleRate, int numChannels);   // may allocate
    void reset() noexcept;                                // clear every accumulator, keep buffers
    void consume (const float* interleaved, int numFrames) noexcept;
    void consume (const AudioBlockView& block) noexcept;   // interleaves through an internal scratch buffer
    AnalysisResult finalise (int droppedFrames = 0);      // measurements over everything consumed since reset()

    int getCapturedFrames() const noexcept { return capturedFrames; }
    int getNumChannels() const noexcept { return channels; }
    int getFrameSize() const noexcept { return analysisFrameSize; }   // 10 ms
    double getSampleRate() const noexcept { return sr; }

private:
    void processFftFrame() noexcept;
    void recordEvent (float peakFrameDb) noexcept;   // bins the peak level a detected hit reached

    double sr = 48000.0;
    int channels = 1;
    int capturedFrames = 0;

    double sumSquares = 0.0, sumSamples = 0.0;
    std::array<double, kMaxChannels> channelSumSquares {};
    float peakAbs = 0.0f;
    int clipCount = 0;

    int analysisFrameSize = 480;
    int analysisFramePos = 0;
    double frameSumSquares = 0.0;
    std::vector<int> levelHistogram;
    int totalAnalysisFrames = 0, silentFrames = 0;
    float prevFrameDb = -120.0f;
    float prevFrameRms = 0.0f;
    std::vector<float> recentFrameDb;
    int recentPos = 0;
    int transientCount = 0;
    double transientRiseSum = 0.0;
    bool decayTracking = false;
    std::vector<int> eventHistogram;   // peak frame level of each detected hit
    std::vector<float> onsetEnvelope;  // per-frame onset strength, for the tempo estimate
    int onsetCount = 0;
    int eventFrames = 0;
    float decayPeakDb = -120.0f;
    int decayFrames = 0;
    double decaySumMs = 0.0;
    int decayCount = 0;

    Biquad sibilanceHpf;
    double frameHighSumSquares = 0.0;
    std::vector<int> sibilanceHistogram;
    int sibilanceFrames = 0, sibilantFrames = 0;

    double sumLR = 0.0;
    Biquad kShelf, kHighpass;
    std::array<double, kMaxChannels> kSumSquares {};
    float interpPeak = 0.0f;
    std::array<float, kMaxChannels> lastSample {};

    RealFFT fft;
    std::vector<float> fftInput, window, windowed, powerAccum, powerScratch, interleaveScratch;
    int fftPos = 0;
    int fftFrames = 0;
};

} // namespace livemix
