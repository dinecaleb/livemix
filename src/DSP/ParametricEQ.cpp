#include "ParametricEQ.h"
#include <cmath>

namespace livemix
{

void ParametricEQ::prepare (double sampleRate, int, int)
{
    sr = sampleRate;
    for (auto& b : bands)
    {
        b.freq.prepare (sr, 20.0f); b.freq.snapTo (b.params.freqHz);
        b.gain.prepare (sr, 20.0f); b.gain.snapTo (b.params.gainDb);
        b.q.prepare (sr, 20.0f);    b.q.snapTo (b.params.q);
        b.needsUpdate = true;
        updateBand (b);
    }
    reset();
}

void ParametricEQ::reset() noexcept
{
    for (auto& b : bands) b.filter.reset();
}

void ParametricEQ::setBand (int index, const EQBandParams& p) noexcept
{
    if (index < 0 || index >= kMaxBands) return;
    auto& b = bands[size_t (index)];
    if (p.type != b.params.type) b.needsUpdate = true;
    b.params = p;
    b.freq.setTarget (p.freqHz);
    b.gain.setTarget (p.gainDb);
    b.q.setTarget (p.q);
}

void ParametricEQ::updateBand (Band& b) noexcept
{
    b.filter.setCoefficients (BiquadCoefficients::make (b.params.type, sr,
                                                        b.freq.getCurrent(),
                                                        b.q.getCurrent(),
                                                        b.gain.getCurrent()));
    b.needsUpdate = false;
}

void ParametricEQ::process (AudioBlockView& block) noexcept
{
    if (! enabled) return;
    const int n = block.numSamples;

    for (int bi = 0; bi < numBands; ++bi)
    {
        auto& b = bands[size_t (bi)];
        const bool isPassFilter = b.params.type == FilterType::LowPass
                               || b.params.type == FilterType::HighPass
                               || b.params.type == FilterType::Notch;
        const bool shouldBeActive = b.params.enabled && (isPassFilter || std::fabs (b.gain.getTarget()) > 0.01f || b.gain.isSmoothing());

        if (shouldBeActive && ! b.active)
        {
            b.filter.reset();
            b.needsUpdate = true;
        }
        b.active = shouldBeActive;
        if (! b.active) continue;

        if (b.freq.isSmoothing() || b.gain.isSmoothing() || b.q.isSmoothing())
        {
            for (int i = 0; i < n; ++i) { b.freq.next(); b.gain.next(); b.q.next(); }
            b.needsUpdate = true;
        }
        if (b.needsUpdate) updateBand (b);

        for (int ch = 0; ch < block.numChannels && ch < kMaxChannels; ++ch)
            b.filter.processBlock (ch, block.channel (ch), n);
    }
}

} // namespace livemix
