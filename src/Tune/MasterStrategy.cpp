// Master strategy: the final mix for one delivery (livestream, broadcast,
// recording or the room). Broad tonal moves only, glue rather than squash, a
// mono-safe image, and the output level fitted to the delivery's loudness
// target with the limiter holding the true-peak ceiling.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace
{
    using namespace tune;

    class MasterStrategyImpl final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Master"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float sub = bandExcess (ctx, t, Band::Sub);
            if (sub > 0.0f)
                placeHighPass (ctx, t, d, templateHighPassHz (ctx, t) * (1.0f + 0.1f * std::min (sub, 6.0f)),
                               ("Sub energy is " + fmtDb (sub, 0) + " above the profile tolerance: rumble that eats headroom without being heard. The subsonic filter is raised a little.").c_str());
            shapeBody (ctx, t, d, 0.0f);
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setLoudness (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };
}

const SourceStrategy& masterStrategy()
{
    static const MasterStrategyImpl strategy;
    return strategy;
}

} // namespace livemix
