// Guitar strategies: acoustic (DI or mic), electric (amp mic or modeler) and
// the guitar bus. Guitars share the mid range with the keys and the voice, so
// the rules lean on the high-pass, the boom / mud cut, quack and fizz control,
// a fizz roll-off for driven amps, gentle compression and a clean floor
// between phrases (amp hum, stage noise), bounded by the profile targets.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include "Profiles/Profile.h"
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
                           ("Low energy is " + fmtDb (low, 0) + " above the profile tolerance: " + std::string (what) + ". The high-pass is raised so the bass and the kick own the bottom.").c_str());
        else if (low < -2.0f && d.proposed.hpfEnabled && templateHighPassHz (ctx, t) > t.hpfMinHz * 1.2f)
            placeHighPass (ctx, t, d, templateHighPassHz (ctx, t) * 0.85f, "The guitar is thinner than the profile target; the high-pass is lowered a little to let its body back in.");
    }

    // Driven amps and cabs put a fizzy layer above 6 kHz. When brilliance and air sit well above the
    // profile the low-pass is fitted from the profile template (never from the current value, so
    // re-tuning the same capture lands on the same frequency).
    void rollOffFizz (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
    {
        const float brilliance = bandExcess (ctx, t, Band::Brilliance);
        const float air = bandExcess (ctx, t, Band::Air);
        const float excess = 0.6f * brilliance + 0.4f * air;
        const auto& tpl = Profiles::baseline (ctx.profile, ctx.role);
        const float templateHz = tpl.lpfEnabled ? tpl.lpfHz : 12000.0f;
        const auto& cur = d.proposed;

        if (excess > 3.0f)
        {
            const float hz = std::round (clamp (templateHz * std::pow (2.0f, -0.08f * std::min (excess, 9.0f)), 6000.0f, 12000.0f) / 250.0f) * 250.0f;
            if (cur.lpfEnabled && std::fabs (cur.lpfHz - hz) <= 0.1f * hz) return;
            d.move (Recommendation::Kind::Filter, TuneSection::Tone, "Rolled off the fizz: low-pass at " + fmtHz (hz),
                    "Brilliance and air are " + fmtDb (excess, 0) + " above the profile tolerance; a driven amp's fizz lives up here and adds nothing but edge, so it is filtered rather than EQ'd.",
                    excess > 6.0f ? Confidence::High : Confidence::Medium,
                    [=] (ChannelParameters& p) { p.lpfEnabled = true; p.lpfHz = hz; });
        }
        else if (excess < -2.0f && cur.lpfEnabled && cur.lpfHz < 11000.0f)
        {
            const float hz = std::round (clamp (std::max (templateHz, cur.lpfHz) * 1.25f, 6000.0f, 12000.0f) / 250.0f) * 250.0f;
            d.move (Recommendation::Kind::Filter, TuneSection::Tone, "Opened the low-pass to " + fmtHz (hz),
                    "The top end is already " + fmtDb (-excess, 0) + " under the profile target; the fizz roll-off was taking detail the profile wants.",
                    Confidence::Medium, [=] (ChannelParameters& p) { p.lpfHz = hz; });
        }
    }

    // Acoustic: boom and body resonance out, quack controlled, pick definition kept, strums held even.
    class AcousticGuitarStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Acoustic Guitar"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "body boom from the pickup or a close microphone");
            shapeBody (ctx, t, d, 0.0f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 80.0f, 300.0f, "the body's boom");
            shapeAttack (ctx, t, d);       // pick definition
            controlHarshness (ctx, t, d);  // pickup quack
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);     // gentle expander only when there is real noise between phrases
        }
    };

    // Electric: amp tone kept. Mud and a boxy cab resonance out, fizz filtered, level held, floor cleaned.
    class ElectricGuitarStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Electric Guitar"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "amp low end that crowds the bass and the kick");
            shapeBody (ctx, t, d, 0.0f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 200.0f, 700.0f, "a boxy cab resonance");
            shapeAttack (ctx, t, d);
            controlHarshness (ctx, t, d);  // fizz / ice-pick region
            rollOffFizz (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);     // amp hum and noise between phrases
        }
    };

    // Guitar bus: glue only. Broad tonal moves, gentle compression, no gate.
    class GuitarBusStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Guitar Bus"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            clearTheLowEnd (ctx, t, d, "several guitars stacking up under the bass");
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setWidth (ctx, t, d);
            setGate (ctx, t, d, 0.0f);     // bypasses a gate if one was left on
        }
    };
}

const SourceStrategy& guitarStrategyFor (RoleFamily family)
{
    static const AcousticGuitarStrategy acoustic;
    static const ElectricGuitarStrategy electric;
    static const GuitarBusStrategy bus;
    switch (family)
    {
        case RoleFamily::ElectricGuitar: return electric;
        case RoleFamily::GuitarBus:      return bus;
        case RoleFamily::AcousticGuitar:
        default:                         return acoustic;
    }
}

} // namespace livemix
