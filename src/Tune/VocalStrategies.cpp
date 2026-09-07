// Voice strategies: lead, backing, choir, speech and the vocal bus. Each one
// composes the shared toolkit rules (input, high-pass, low-mid, harshness,
// presence, air, S control, compression, gentle expansion) in the order a
// vocal engineer would work, bounded by the profile's targets.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace
{
    using namespace tune;

    // Lead vocal: every word clear and warm, never harsh, steady on top of the band.
    class LeadVocalStrategy final : public SourceStrategy
    {
    public:
        explicit LeadVocalStrategy (bool backing) : isBacking (backing) {}
        const char* name() const override { return isBacking ? "Backing Vocal" : "Lead Vocal"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float f = fundamental (ctx, t);
            // Rumble and handling noise: the high-pass follows the sub/low bands but always stays under the voice's own fundamental.
            const float sub = std::max (0.0f, bandExcess (ctx, t, Band::Sub));
            const float low = bandExcess (ctx, t, Band::Low);
            const float cap = f > 0.0f ? f * 0.8f : t.hpfMaxHz;
            if ((sub > 0.0f || low > 0.0f) && cap > d.proposed.hpfHz * 1.05f)
            {
                const float excess = sub + std::max (0.0f, low);
                const float hz = std::min (d.proposed.hpfHz * (1.0f + 0.1f * std::min (excess, 6.0f)), cap);
                placeHighPass (ctx, t, d, hz, ("Energy below the voice is " + fmtDb (excess, 0) + " above the profile tolerance: stage rumble, handling noise or proximity boom. The high-pass is raised"
                                               + std::string (f > 0.0f ? ", staying under the measured fundamental (" + fmtHz (f) + ")." : ".")).c_str());
            }
            else if (f > 0.0f && d.proposed.hpfEnabled && d.proposed.hpfHz > cap)
                placeHighPass (ctx, t, d, cap, ("The high-pass sat above this voice's fundamental (" + fmtHz (f) + "); it is lowered so the voice keeps its weight.").c_str());
            else if (low < -2.0f && d.proposed.hpfEnabled && d.proposed.hpfHz > t.hpfMinHz * 1.2f)
                placeHighPass (ctx, t, d, d.proposed.hpfHz * 0.85f, "The voice is thinner than the profile target; the high-pass is lowered a little to let its warmth back in.");
            shapeBody (ctx, t, d, f);
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 500.0f, 1500.0f, "nasal tone");
            shapeAttack (ctx, t, d);     // presence = the words
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            controlSibilance (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, 0.0f);  // gentle expander, bounded by the profile's small range
        }
    private:
        bool isBacking;
    };

    // Choir: distant mics on many voices. Filtering and gentle control; no expander, no drive.
    class ChoirStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Choir"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float low = bandExcess (ctx, t, Band::Low) + std::max (0.0f, bandExcess (ctx, t, Band::Sub));
            if (low > 0.0f)
                placeHighPass (ctx, t, d, d.proposed.hpfHz * (1.0f + 0.12f * std::min (low, 6.0f)),
                               ("Low energy is " + fmtDb (low, 0) + " above the profile tolerance: stage rumble and spill from the band in the choir mics. The high-pass is raised.").c_str());
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            controlSibilance (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, 0.0f); // bypasses a gate if one was left on
        }
    };

    // Speech: intelligibility first. Boom out, presence in, S sounds tamed, level held.
    class SpeechStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Speech"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            const float low = bandExcess (ctx, t, Band::Low) + std::max (0.0f, bandExcess (ctx, t, Band::Sub));
            if (low > 0.0f)
                placeHighPass (ctx, t, d, d.proposed.hpfHz * (1.0f + 0.12f * std::min (low, 6.0f)),
                               ("Low energy is " + fmtDb (low, 0) + " above the profile tolerance: boom from a close microphone or a lectern. The high-pass is raised; speech stays clear without it.").c_str());
            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 400.0f, 1200.0f, "boxiness");
            shapeAttack (ctx, t, d);
            controlHarshness (ctx, t, d);
            controlSibilance (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };

    // Vocal bus: glue only. Broad tonal moves, gentle compression, no S control (the channels did that).
    class VocalBusStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Vocal Bus"; }
        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);
            controlLowMid (ctx, t, d);
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);
            controlSibilance (ctx, t, d);
            setCompression (ctx, t, d);
            setGate (ctx, t, d, 0.0f);
        }
    };
}

const SourceStrategy& vocalStrategyFor (RoleFamily family)
{
    static const LeadVocalStrategy lead { false };
    static const LeadVocalStrategy backing { true };
    static const ChoirStrategy choir;
    static const SpeechStrategy speech;
    static const VocalBusStrategy bus;
    switch (family)
    {
        case RoleFamily::BackingVocal: return backing;
        case RoleFamily::Choir:        return choir;
        case RoleFamily::Speech:       return speech;
        case RoleFamily::VocalBus:     return bus;
        case RoleFamily::LeadVocal:
        default:                       return lead;
    }
}

} // namespace livemix
