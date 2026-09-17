// Crowd / ambience microphones, and the saxophone.
//
// Two families that arrived together (2026-09) because a live broadcast needs both and
// DLIVE had neither. They have nothing in common musically; what they share is that
// treating them as something else was audibly wrong:
//
//   A crowd microphone routed through the drum-room rules got gated, transient-shaped and
//   pushed forward. Gating a congregation removes the congregation - the sound of a service
//   *is* what happens between the words - and sharpening a room's transients is how a
//   broadcast starts sounding like an advertisement for a stadium.
//
//   A saxophone routed through the piano rules got its honk shelved instead of notched, far
//   too little compression for the range between a held note and a wail, and no
//   acknowledgement that it lives in the lead vocal's presence band.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace
{
    using namespace tune;

    // ------------------------------------------------------------------
    // The room and the congregation
    // ------------------------------------------------------------------
    class AmbienceStrategy final : public SourceStrategy
    {
    public:
        explicit AmbienceStrategy (bool isBus) : bus (isBus) {}
        const char* name() const override { return bus ? "Ambience Bus" : "Crowd / Ambience"; }

        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);

            // A building full of people is full of low-frequency energy that carries nothing:
            // air handling, traffic, the structure itself, and every footstep on a wooden
            // floor. It is the first thing to go, and it goes higher than on any stage source.
            const float low = bandExcess (ctx, t, Band::Low) + std::max (0.0f, bandExcess (ctx, t, Band::Sub));
            const float base = templateHighPassHz (ctx, t);
            if (low > 0.0f)
                placeHighPass (ctx, t, d, base * (1.0f + 0.15f * std::min (low, 6.0f)),
                               ("The room is carrying " + fmtDb (low, 0) + " more low energy than the profile wants - a building's "
                                "own noise, not the congregation. The high-pass is raised so none of it reaches the broadcast.").c_str());
            else
                placeHighPass (ctx, t, d, base,
                               "An ambience microphone is high-passed well above a stage source: the low end it hears is the "
                               "building rather than the people in it.");

            controlLowMid (ctx, t, d);
            notchResonance (ctx, t, d, 120.0f, 500.0f, "the room's own ring");
            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);

            // Compression here is a ceiling, not a character. What it exists for is the one
            // shout or burst of applause that would otherwise take the master's headroom.
            setCompression (ctx, t, d);

            if (! bus) setWidth (ctx, t, d);

            // The rule that defines the family, said out loud so it appears in the report
            // rather than being a silent absence.
            d.note (Recommendation::Kind::Info, TuneSection::Bleed, "No gate on the room",
                    "On a crowd or ambience microphone the quiet between the sounds is the sound. A gate would take the "
                    "room away and leave applause arriving out of silence, so this family is never gated or expanded - "
                    "whatever the noise floor measures.", Confidence::High);

            if (! bus)
                d.note (Recommendation::Kind::Info, TuneSection::Mix, "Sits under the stage, on purpose",
                        "Ambience is felt before it is heard. It is aimed well below the band so it adds the room without "
                        "ever competing with it, and it has its own group fader so it can be lifted between songs and "
                        "brought back down under the sermon.", Confidence::High);
        }

    private:
        bool bus = false;
    };

    // ------------------------------------------------------------------
    // Saxophone
    // ------------------------------------------------------------------
    class SaxophoneStrategy final : public SourceStrategy
    {
    public:
        const char* name() const override { return "Saxophone"; }

        void decide (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d) const override
        {
            removeDcOffset (ctx, d);

            // The high-pass sits under the horn's lowest note and never above 0.8x of it -
            // the rule every pitched source follows, and one a horn needs badly, because the
            // temptation on a bright reed is to keep raising it until the body is gone.
            const float f0 = fundamental (ctx, t);
            const float base = templateHighPassHz (ctx, t);
            const float low = bandExcess (ctx, t, Band::Low);
            const float want = low > 0.0f ? base * (1.0f + 0.1f * std::min (low, 5.0f)) : base;
            placeHighPass (ctx, t, d, want,
                           f0 > 0.0f
                               ? ("The lowest note measures " + fmtHz (f0) + "; the high-pass is placed under it so the "
                                  "horn keeps its body and the stage's rumble does not reach the mix.").c_str()
                               : "The high-pass is placed under the horn's range: below it there is nothing a saxophone "
                                 "makes, only the stage.");

            shapeBody (ctx, t, d, f0);
            controlLowMid (ctx, t, d);

            // The honk. It is a narrow resonance somewhere between 800 Hz and 2.5 kHz, it
            // moves with the instrument and the player, and finding it is the single change
            // that makes a saxophone sound mixed rather than merely present.
            notchResonance (ctx, t, d, t.harshnessMinHz, t.harshnessMaxHz, "the horn's honk");

            controlHarshness (ctx, t, d);
            shapeAir (ctx, t, d);

            // A horn has real range: a held note and a wailed one are not the same instrument.
            // The attack window stays slow enough to let the reed's bite through.
            setCompression (ctx, t, d);

            if (t.saturationAppropriate && ctx.analysis.crestFactorDb > t.crestFactorMaxDb + 2.0f)
            {
                const float drive = clamp (std::max (d.proposed.satEnabled ? d.proposed.satDrive : 0.0f, 0.1f), 0.0f, t.satMaxDrive);
                if (! d.proposed.satEnabled || drive > d.proposed.satDrive + 0.02f)
                    d.move (Recommendation::Kind::Info, TuneSection::Dynamics, "A little warmth on the reed",
                            "The peaks are well above the average. Gentle saturation rounds the loudest notes and thickens the "
                            "reed before the compressor has to reach for them.", Confidence::Low,
                            [=] (ChannelParameters& p) { p.satEnabled = true; p.satDrive = drive; });
            }

            // Sustained source: never an expander. Breath and key noise between phrases are
            // the player, not a fault.
            setGate (ctx, t, d, f0);

            d.note (Recommendation::Kind::Info, TuneSection::Mix, "Shares the voice's air",
                    "A saxophone sings in the same band as a lead vocal. It is aimed a little under the profile's presence "
                    "target so the two can both be heard, and the mix makes room for whichever is carrying the song.",
                    Confidence::Medium);
        }
    };
}

const SourceStrategy& ambienceStrategyFor (RoleFamily family)
{
    static const AmbienceStrategy source { false };
    static const AmbienceStrategy bus { true };
    return family == RoleFamily::AmbienceBus ? bus : source;
}

const SourceStrategy& saxophoneStrategy()
{
    static const SaxophoneStrategy sax;
    return sax;
}

} // namespace livemix
