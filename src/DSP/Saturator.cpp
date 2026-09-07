#include "Saturator.h"
#include <cmath>

namespace livemix
{

namespace
{
    // Rational tanh approximation, exact at 0 and unity at |x| = 3.
    inline float softClip (float x) noexcept
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
}

void Saturator::prepare (double sampleRate, int, int)
{
    driveSmoother.prepare (sampleRate, 20.0f);
    mixSmoother.prepare (sampleRate, 20.0f);
    setParams (params);
    driveSmoother.snapToTarget();
    mixSmoother.snapToTarget();
}

void Saturator::reset() noexcept {}

void Saturator::setParams (const Params& p) noexcept
{
    params = p;
    driveSmoother.setTarget (p.drive);
    mixSmoother.setTarget (p.mix);
}

void Saturator::process (AudioBlockView& block) noexcept
{
    if (! params.enabled) return;

    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;

    for (int i = 0; i < n; ++i)
    {
        const float drive = driveSmoother.next();
        const float mix = mixSmoother.next();
        const float k = 1.0f + drive * 5.0f; // pre-gain 1..6
        const float invK = 1.0f / k;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = block.channels[ch][i];
            const float wet = softClip (dry * k) * invK * (1.0f + drive * 0.5f);
            block.channels[ch][i] = dry + mix * (wet - dry);
        }
    }
}

} // namespace livemix
