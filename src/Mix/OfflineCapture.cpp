#include "OfflineCapture.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

void OfflineCapture::prepare (double sampleRate, const RoutingGraph& graph)
{
    sr = sampleRate;
    numStrips = graph.numStrips();
    active = false;
    strips.clear();
    for (int i = 0; i < numStrips; ++i)
    {
        auto s = std::make_unique<Stream>();
        s->used = true;
        s->accumulator.prepare (sr, graph.strips[size_t (i)].numChannels());
        strips.push_back (std::move (s));
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        buses[size_t (b)].used = graph.busUsed[size_t (b)];
        if (buses[size_t (b)].used) buses[size_t (b)].accumulator.prepare (sr, 2);
    }
    masterOut.used = true;
    masterOut.accumulator.prepare (sr, 2);
    postPeak.assign (size_t (numStrips), 0.0f);
    postSumSquares.assign (size_t (numStrips), 0.0);
    postSamples.assign (size_t (numStrips), 0);
}

void OfflineCapture::start()
{
    for (auto& s : strips) s->accumulator.reset();
    for (auto& b : buses) if (b.used) b.accumulator.reset();
    masterOut.accumulator.reset();
    std::fill (postPeak.begin(), postPeak.end(), 0.0f);
    std::fill (postSumSquares.begin(), postSumSquares.end(), 0.0);
    std::fill (postSamples.begin(), postSamples.end(), 0);
    active = true;
}

MixCapture::Result OfflineCapture::finish()
{
    active = false;
    MixCapture::Result r;
    r.strips.resize (size_t (numStrips));
    r.processed.resize (size_t (numStrips));
    int maxCaptured = 0;
    for (int i = 0; i < numStrips; ++i)
    {
        r.strips[size_t (i)] = strips[size_t (i)]->accumulator.finalise (0);
        maxCaptured = std::max (maxCaptured, strips[size_t (i)]->accumulator.getCapturedFrames());
        auto& o = r.processed[size_t (i)];
        o.valid = postSamples[size_t (i)] > 0;
        o.peakDb = gainToDb (postPeak[size_t (i)]);
        o.rmsDb = o.valid ? gainToDb (float (std::sqrt (postSumSquares[size_t (i)] / double (postSamples[size_t (i)])))) : -120.0f;
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
        if (buses[size_t (b)].used) r.buses[size_t (b)] = buses[size_t (b)].accumulator.finalise (0);
    r.masterOutput = masterOut.accumulator.finalise (0);
    r.seconds = float (maxCaptured / sr);
    r.valid = numStrips > 0;
    for (const auto& a : r.strips) if (! a.valid) r.valid = false;
    return r;
}

void OfflineCapture::pushStripInput (int strip, const AudioBlockView& raw) noexcept
{
    if (! active || strip < 0 || strip >= numStrips) return;
    strips[size_t (strip)]->accumulator.consume (raw);
}

void OfflineCapture::pushStripProcessed (int strip, const AudioBlockView& processed) noexcept
{
    if (! active || strip < 0 || strip >= numStrips) return;
    float peak = postPeak[size_t (strip)];
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
    postPeak[size_t (strip)] = peak;
    postSumSquares[size_t (strip)] += sq;
    postSamples[size_t (strip)] += static_cast<long long> (processed.numSamples) * processed.numChannels;
}

void OfflineCapture::pushBus (MixBus bus, const AudioBlockView& input) noexcept
{
    if (! active) return;
    auto& s = buses[size_t (bus)];
    if (s.used) s.accumulator.consume (input);
}

void OfflineCapture::pushMasterOutput (const AudioBlockView& output) noexcept
{
    if (active) masterOut.accumulator.consume (output);
}

} // namespace livemix
