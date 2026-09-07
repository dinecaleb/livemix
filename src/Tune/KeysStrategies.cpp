// Keys strategies: piano, electric piano, organ, synths and the keys bus.
// Keys are usually stereo and usually fight the bass and the vocals for space,
// so the rules lean on the high-pass, the low-mid cut, harshness control,
// gentle compression and a mono-compatible stereo image.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace
{
    using namespace tune;

    void clearTheLowEnd (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, const char* what)
    {
        const float low = bandExcess (ctx, t, Band::Low) + std::max (0.0f, bandExcess (ctx, t, Band::Sub));
        if (low > 0.0f)
            placeHighPass (ctx, t, d, templateHighPassHz (ctx, t) * (1.0f + 0.12f * std::min (low, 6.0f)),
                           ("Low energy is " + fmtDb (low, 0) + " above the profile tolerance: " + std::string (what) + ". The high-pass is raised so the bass owns the bottom.").c_str());
        else if (low < -2.0f && d.proposed.hpfEnabled && templateHighPassHz (ctx, t) > t.hpfMinHz * 1.2f)
            placeHighPass (ctx, t, d, templateHighPassHz (ctx, t) * 0.85f, "The low end is thinner than the profile target; the high-pass is lowered a little.");
    }

    class PianoStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Piano"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "the piano's low strings sit on top of the bass");
            shapeBody (ctx, t, d, 0.0f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 150.0f, 800.0f, "a ringing string");
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };

    class ElectricPianoStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Electric Piano"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "the tines' low end crowds the bass");
            shapeBody (ctx, t, d, 0.0f);
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d); // the bark
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            if (t.saturationAppropriate && ctx.analysis.crestFactorDb > t.crestFactorMaxDb + 2.0f)
            {
                const float drive = clamp (std::max (d.proposed.satEnabled ? d.proposed.satDrive : 0.0f, 0.12f), 0.0f, t.satMaxDrive);
                if (! d.proposed.satEnabled || drive > d.proposed.satDrive + 0.02f)
                    d.move (Recommendation::Kind::Info, TuneSection::Dynamics, "Added a touch of warmth",
                            "Notes are spiky against the average level; gentle saturation rounds them before the compressor has to work.",
                            Confidence::Low, [=] (ChannelParameters& p) { p.satEnabled = true; p.satDrive = drive; });
            }
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };

    class OrganStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Organ"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "the pedals and the low drawbars share the bass player's space");
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 200.0f, 900.0f, "a honking drawbar");
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };

    class SynthStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Synth"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "sub content that belongs to the bass");
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };

    class KeysBusStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Keys Bus"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "several keyboards stacking up under the bass");
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };
}

const SourceStrategy& keysStrategyFor (RoleFamily family)
{
    static const PianoStrategy piano;
    static const ElectricPianoStrategy ep;
    static const OrganStrategy organ;
    static const SynthStrategy synth;
    static const KeysBusStrategy bus;
    switch (family)
    {
        case RoleFamily::ElectricPiano: return ep;
        case RoleFamily::Organ:         return organ;
        case RoleFamily::Synth:         return synth;
        case RoleFamily::KeysBus:       return bus;
        case RoleFamily::Piano:
        default:                        return piano;
    }
}

} // namespace livemix
