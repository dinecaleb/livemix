#pragma once
#include <array>
#include <memory>
#include <vector>
#include "MixEngine.h"
#include "MixCapture.h"
#include "Analysis/AnalysisAccumulator.h"

namespace livemix
{

// The same measurements as MixCapture, taken synchronously on the caller's thread:
// no FIFO, no worker, no trigger. For rendering recorded multitracks through the
// engine faster than real time (tests, the stems tool). Not for the audio device.
class OfflineCapture : public MixTap
{
public:
    void prepare (double sampleRate, const RoutingGraph& graph);
    void start();                       // reset and start measuring
    MixCapture::Result finish();        // stop measuring and compute the result

    bool isActive() const noexcept override { return active; }
    void pushStripInput (int strip, const AudioBlockView& raw) noexcept override;
    void pushStripProcessed (int strip, const AudioBlockView& processed) noexcept override;
    void pushBus (MixBus bus, const AudioBlockView& input) noexcept override;
    void pushMasterOutput (const AudioBlockView& output) noexcept override;

private:
    struct Stream { AnalysisAccumulator accumulator; bool used = false; };
    double sr = 48000.0;
    int numStrips = 0;
    bool active = false;
    std::vector<std::unique_ptr<Stream>> strips;
    std::array<Stream, int (MixBus::Count)> buses;
    Stream masterOut;
    std::vector<float> postPeak;
    std::vector<double> postSumSquares;
    std::vector<long long> postSamples;
};

} // namespace livemix
