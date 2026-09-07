// Drum source strategies: the engineering behaviour of one professional
// engineer per source. Each consumes structured analysis + profile targets and
// composes bounded decisions from the shared toolkit plus source-specific rules.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace
{
    using namespace tune;

    std::string ms (float v) { char b[32]; std::snprintf (b, sizeof (b), "%.0f ms", double (v)); return b; }

    // Kick: deep, punchy, controlled, defined. Fundamental first: everything low is placed relative to it.
    class KickStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Kick"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float f = fundamental (ctx, t);
            if (f > 0.0f)
                placeHighPass (ctx, t, d, f * 0.5f, ("The measured fundamental is " + fmtHz (f) + "; the high-pass sits half an octave below it, removing rumble while keeping every bit of the fundamental.").c_str());
            shapeBody (ctx, t, d, f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 150.0f, 800.0f, "shell ring");
            shapeAttack (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, f);
        }
    };

    // Snare: full, cracking, powerful, controlled; never harsh. Harshness is cut, never boosted around.
    class SnareStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Snare"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float f = fundamental (ctx, t);
            if (f > 0.0f)
                placeHighPass (ctx, t, d, f * 0.5f, ("The measured fundamental is " + fmtHz (f) + "; the high-pass sits below it so the shell stays full while kick spill is reduced.").c_str());
            shapeBody (ctx, t, d, f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 300.0f, 1500.0f, "ring");
            shapeAttack (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, f);
        }
    };

    // Toms: large, clean, strong attack, controlled sustain, sensible bleed management.
    class TomStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Tom"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float f = fundamental (ctx, t);
            if (f > 0.0f)
                placeHighPass (ctx, t, d, f * 0.5f, ("The measured fundamental is " + fmtHz (f) + "; the high-pass sits half an octave below it.").c_str());
            shapeBody (ctx, t, d, f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 200.0f, 900.0f, "ring");
            shapeAttack (ctx, t, d);
            setCompression (ctx, t, d);

            // Sustain: a long ring (measured) is controlled with the transient shaper, bounded by the profile.
            const float decay = ctx.analysis.meanDecayMs;
            if (t.transientAppropriate && decay > 600.0f && ctx.analysis.decayCount >= 3)
            {
                const float cut = -clamp (0.15f + (decay - 600.0f) / 2000.0f, 0.15f, t.transientMaxSustainCut);
                if (cut < d.proposed.transientSustain - 0.05f)
                    d.move (Recommendation::Kind::Transient, TuneSection::Attack, "Controlled the sustain: transient sustain " + std::to_string (int (std::round (cut * 100.0f))) + "%",
                            "Hits take about " + ms (decay) + " to fall 20 dB; shortening the ring keeps big toms clean under the rest of the kit.",
                            Confidence::Medium, [=] (ChannelParameters& p) { p.transientEnabled = true; p.transientSustain = cut; });
            }
            setGate (ctx, t, d, f);
        }
    };

    // Overheads: detailed, smooth, controlled harshness, balanced image. No gate, no transient shaping.
    // Bleed is managed by filtering: kick and low spill raise the high-pass.
    class OverheadStrategy final : public SourceStrategy
    {
    public:
        explicit OverheadStrategy (bool isRoom) : room (isRoom) {}
        const char* name() const override { return room ? "Room" : "Overhead"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float low = bandExcess (ctx, t, Band::Low) + std::max (0.0f, bandExcess (ctx, t, Band::Sub));
            if (low > 0.0f)
                placeHighPass (ctx, t, d, d.proposed.hpfHz * (1.0f + 0.12f * std::min (low, 6.0f)),
                               ("Low energy is " + fmtDb (low, 0) + " above the profile tolerance: kick and low spill in the " + std::string (room ? "room" : "overheads") + ". The high-pass is raised so the close mics own the low end.").c_str());
            else if (low < -1.5f && d.proposed.hpfEnabled && d.proposed.hpfHz > t.hpfMinHz * 1.25f)
                placeHighPass (ctx, t, d, d.proposed.hpfHz * 0.8f,
                               "The low end is thinner than the profile target; the high-pass is lowered a little to let the kit's weight back in.");
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);

            if (room && t.transientAppropriate && ctx.analysis.meanDecayMs > 0.0f && ctx.analysis.meanDecayMs < 150.0f && ctx.analysis.decayCount >= 3)
            {
                const float sustain = clamp (0.25f, 0.0f, 0.5f);
                if (d.proposed.transientSustain < sustain - 0.05f)
                    d.move (Recommendation::Kind::Transient, TuneSection::Attack, "Added sustain: transient sustain +25%",
                            "The room decays in about " + ms (ctx.analysis.meanDecayMs) + "; a little sustain gives the profile the size it wants from a dead room.",
                            Confidence::Medium, [=] (ChannelParameters& p) { p.transientEnabled = true; p.transientSustain = sustain; });
            }
            setGate (ctx, t, d, 0.0f); // bypasses a gate if one was left on
        }
    private:
        bool room;
    };

    // Hi-hat: crisp and clear, never splashy. Filtering and gentle control only.
    class HiHatStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Hi-Hat"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float low = bandExcess (ctx, t, Band::LowMid) + std::max (0.0f, bandExcess (ctx, t, Band::Low));
            if (low > 0.0f)
                placeHighPass (ctx, t, d, d.proposed.hpfHz * (1.0f + 0.1f * std::min (low, 6.0f)),
                               ("Low and low-mid energy is " + fmtDb (low, 0) + " above the profile tolerance: snare and kick spill. The high-pass is raised.").c_str());
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };

    // Drum bus: cohesion, punch, controlled peaks. Glue, not squash; broad tonal moves only.
    class BusStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Drum Bus"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            shapeBody (ctx, t, d, 0.0f);
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            setCompression (ctx, t, d);
            if (t.saturationAppropriate && ctx.analysis.crestFactorDb > t.crestFactorMaxDb + 3.0f)
            {
                const float drive = clamp (std::max (d.proposed.satEnabled ? d.proposed.satDrive : 0.0f, 0.2f), 0.0f, t.satMaxDrive);
                if (! d.proposed.satEnabled || drive > d.proposed.satDrive + 0.02f)
                    d.move (Recommendation::Kind::Info, TuneSection::Dynamics, "Added a touch of saturation",
                            "Peaks are well above the average; gentle saturation rounds them and adds cohesion before the compressor has to work.",
                            Confidence::Low, [=] (ChannelParameters& p) { p.satEnabled = true; p.satDrive = drive; });
            }
            setGate (ctx, t, d, 0.0f);
        }
    };
}

const SourceStrategy& strategyFor (RoleFamily family)
{
    static const KickStrategy kick;
    static const SnareStrategy snare;
    static const TomStrategy tom;
    static const OverheadStrategy overhead { false };
    static const OverheadStrategy room { true };
    static const HiHatStrategy hihat;
    static const BusStrategy bus;
    switch (family)
    {
        case RoleFamily::Kick:     return kick;
        case RoleFamily::Snare:    return snare;
        case RoleFamily::Tom:      return tom;
        case RoleFamily::Overhead: return overhead;
        case RoleFamily::Room:     return room;
        case RoleFamily::HiHat:    return hihat;
        case RoleFamily::Bus:      return bus;
        case RoleFamily::LeadVocal:
        case RoleFamily::BackingVocal:
        case RoleFamily::Choir:
        case RoleFamily::Speech:
        case RoleFamily::VocalBus: return vocalStrategyFor (family);
        case RoleFamily::Piano:
        case RoleFamily::ElectricPiano:
        case RoleFamily::Organ:
        case RoleFamily::Synth:
        case RoleFamily::KeysBus:  return keysStrategyFor (family);
        case RoleFamily::Master:   return masterStrategy();
        case RoleFamily::AcousticGuitar:
        case RoleFamily::ElectricGuitar:
        case RoleFamily::GuitarBus: return guitarStrategyFor (family);
        case RoleFamily::ElectricBass:
        case RoleFamily::SynthBass:
        case RoleFamily::BassBus:  return bassStrategyFor (family);
        case RoleFamily::Count:
        default:                   return bus;
    }
}

} // namespace livemix
