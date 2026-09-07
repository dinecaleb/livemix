#include "TransientProcessor.h"
#include "Core/FastMath.h"
#include <cmath>
#include <initializer_list>

namespace livemix
{

namespace
{
    constexpr float kMaxAttackBoostDb = 12.0f;
    constexpr float kMaxSustainDb = 12.0f;
}

void TransientProcessor::prepare (double sampleRate, int, int)
{
    sr = sampleRate;
    for (auto* f : { &attackFast, &attackSlow, &sustainFast, &sustainSlow })
        f->prepare (sr);

    attackFast.setAttackMs (0.1f);   attackFast.setReleaseMs (40.0f);
    attackSlow.setAttackMs (12.0f);  attackSlow.setReleaseMs (40.0f);
    sustainFast.setAttackMs (1.0f);  sustainFast.setReleaseMs (50.0f);
    sustainSlow.setAttackMs (1.0f);  sustainSlow.setReleaseMs (700.0f);

    gainSmoother.prepare (sr, 0.3f);
    gainSmoother.snapTo (1.0f);
    reset();
}

void TransientProcessor::reset() noexcept
{
    attackFast.reset(); attackSlow.reset(); sustainFast.reset(); sustainSlow.reset();
    gainSmoother.snapTo (1.0f);
    lastGainDb = 0.0f;
}

void TransientProcessor::setParams (const Params& p) noexcept
{
    params = p;
}

void TransientProcessor::process (AudioBlockView& block) noexcept
{
    if (! params.enabled || (std::fabs (params.attack) < 1.0e-3f && std::fabs (params.sustain) < 1.0e-3f))
    {
        lastGainDb = 0.0f;
        return;
    }

    const int n = block.numSamples;
    const int numCh = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;

    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float a = std::fabs (block.channels[ch][i]);
            if (a > peak) peak = a;
        }

        const float af = attackFast.process (peak);
        const float as = attackSlow.process (peak);
        const float sf = sustainFast.process (peak);
        const float ss = sustainSlow.process (peak);

        float attackDiff = fastmath::gainToDb (af) - fastmath::gainToDb (as);
        if (attackDiff < 0.0f) attackDiff = 0.0f;
        if (attackDiff > kMaxAttackBoostDb) attackDiff = kMaxAttackBoostDb;

        float sustainDiff = fastmath::gainToDb (ss) - fastmath::gainToDb (sf);
        if (sustainDiff < 0.0f) sustainDiff = 0.0f;
        if (sustainDiff > kMaxSustainDb) sustainDiff = kMaxSustainDb;

        float gainDb = params.attack * attackDiff + params.sustain * sustainDiff;
        if (gainDb > 18.0f) gainDb = 18.0f;
        if (gainDb < -24.0f) gainDb = -24.0f;
        lastGainDb = gainDb;

        gainSmoother.setTarget (fastmath::dbToGain (gainDb));
        const float g = gainSmoother.next();

        for (int ch = 0; ch < numCh; ++ch)
            block.channels[ch][i] *= g;
    }
}

} // namespace livemix
