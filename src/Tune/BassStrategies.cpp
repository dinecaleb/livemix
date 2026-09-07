// Bass strategies: electric bass (DI or amp), synth bass and the bass bus.
// The bass owns the bottom of a gospel mix, so the rules protect the
// fundamental (the high-pass never goes above 0.8 x the lowest measured note),
// clear the mud above it, control fret clank, hold every note at the same
// level and add a little grit so the bass reads on small speakers.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace
{
    using namespace tune;

    // Rumble under the lowest note is filtered, never EQ'd; the fundamental caps the high-pass.
    void protectTheFundamental (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float f)
    {
        const float sub = std::max (0.0f, bandExcess (ctx, t, Band::Sub));
        const float low = bandExcess (ctx, t, Band::Low);
        const float cap = f > 0.0f ? f * 0.8f : t.hpfMaxHz;
        if (sub > 0.0f && cap > templateHighPassHz (ctx, t) * 1.05f)
        {
            const float hz = std::min (templateHighPassHz (ctx, t) * (1.0f + 0.1f * std::min (sub, 6.0f)), cap);
            placeHighPass (ctx, t, d, hz, ("Energy below the lowest note is " + fmtDb (sub, 0) + " above the profile tolerance: rumble, stage noise or a boomy cabinet. The high-pass is raised"
                                           + std::string (f > 0.0f ? ", staying under the measured fundamental (" + fmtHz (f) + ")." : ".")).c_str());
        }
        else if (f > 0.0f && d.proposed.hpfEnabled && d.proposed.hpfHz > cap)
            placeHighPass (ctx, t, d, cap, ("The high-pass sat above the lowest note (" + fmtHz (f) + "); it is lowered so the bass keeps its weight.").c_str());
        else if (low < -2.0f && d.proposed.hpfEnabled && templateHighPassHz (ctx, t) > t.hpfMinHz * 1.2f)
            placeHighPass (ctx, t, d, templateHighPassHz (ctx, t) * 0.85f, "The low end is thinner than the profile target; the high-pass is lowered a little.");
    }

    // Gospel bass wants a touch of drive: it rounds spiky notes and puts harmonics where phones can hear them.
    void addGrit (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
    {
        if (! t.saturationAppropriate) return;
        if (ctx.analysis.crestFactorDb <= t.crestFactorMaxDb + 2.0f) return;
        const float drive = clamp (std::max (d.proposed.satEnabled ? d.proposed.satDrive : 0.0f, 0.15f), 0.0f, t.satMaxDrive);
        if (d.proposed.satEnabled && drive <= d.proposed.satDrive + 0.02f) return;
        d.move (Recommendation::Kind::Info, TuneSection::Dynamics, "Added a touch of grit",
                "Notes are spiky against the average level (crest factor " + fmtDb (ctx.analysis.crestFactorDb, 0) + "); gentle drive rounds them before the compressor and adds the harmonics small speakers need to hear the bass.",
                Confidence::Low, [=] (ChannelParameters& p) { p.satEnabled = true; p.satDrive = drive; });
    }

    // A bass rings between notes: when the level between notes sits close under the notes, or notes decay slowly,
    // the "floor" is the instrument itself and an expander would chop its tails. Only a real quiet floor (DI hum,
    // amp noise well under the playing) gets the shared expander rule.
    void cleanTheFloor (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float f)
    {
        const auto& a = ctx.analysis;
        const Levels L = levels (ctx);
        const float gap = L.hitDb - L.floorDb;
        const bool rings = t.gateAppropriate && (gap < 24.0f || a.meanDecayMs > 600.0f);
        if (! rings) { setGate (ctx, t, d, f); return; }
        if (d.proposed.gateEnabled)
            d.move (Recommendation::Kind::Gate, TuneSection::Bleed, "Clean-up bypassed: the bass rings between notes",
                    "Between notes the channel sits only " + std::to_string (int (gap + 0.5f)) + " dB under them"
                    + std::string (a.meanDecayMs > 600.0f ? " and notes ring for about " + std::to_string (int (a.meanDecayMs)) + " ms" : "")
                    + ": that is the instrument sustaining, not noise, so an expander would cut into the notes themselves.",
                    Confidence::High, [] (ChannelParameters& p) { p.gateEnabled = false; });
        else
            d.note (Recommendation::Kind::Gate, TuneSection::Bleed, "No clean-up needed",
                    "The bass sustains between notes (the level between notes is " + std::to_string (int (gap + 0.5f)) + " dB under them); there is no quiet floor to clean up.", Confidence::Medium);
    }

    class ElectricBassStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Electric Bass"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float f = fundamental (ctx, t);
            protectTheFundamental (ctx, t, d, f);
            shapeBody (ctx, t, d, f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 200.0f, 600.0f, "a boxy note");
            shapeAttack (ctx, t, d);       // string definition
            controlHarshness (ctx, t, d);  // fret clank
            addGrit (ctx, t, d);
            setCompression (ctx, t, d);
            cleanTheFloor (ctx, t, d, f);  // hum / noise between notes; the detector sits under the fundamental
        }
    };

    class SynthBassStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Synth Bass"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float f = fundamental (ctx, t);
            protectTheFundamental (ctx, t, d, f);
            shapeBody (ctx, t, d, f);
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, f);        // bypasses a gate if one was left on (not appropriate)
        }
    };

    class BassBusStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Bass Bus"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            protectTheFundamental (ctx, t, d, fundamental (ctx, t));
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };
}

const SourceStrategy& bassStrategyFor (RoleFamily family)
{
    static const ElectricBassStrategy electric;
    static const SynthBassStrategy synth;
    static const BassBusStrategy bus;
    switch (family)
    {
        case RoleFamily::SynthBass: return synth;
        case RoleFamily::BassBus:   return bus;
        case RoleFamily::ElectricBass:
        default:                    return electric;
    }
}

} // namespace livemix
