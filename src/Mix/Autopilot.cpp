#include "Autopilot.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace
{
    std::string db (float v)
    {
        char buf[24];
        std::snprintf (buf, sizeof (buf), "%+.1f dB", double (v));
        return buf;
    }

    // The sentence for what was noticed. Plain words, about the mix rather than about the
    // number: a volunteer reading the Mix history on Monday wants to know what happened in
    // the room, and "LEAD -2.6 dB against the master" is not that.
    std::string sentenceFor (MixBus bus, float drift)
    {
        const bool quieter = drift < 0.0f;
        switch (bus)
        {
            case MixBus::Lead:
                return quieter ? "The lead fell below where you had it against the rest of the mix."
                               : "The lead had come up above where you had it against the rest of the mix.";
            case MixBus::Vocals:
                return quieter ? "The backing voices had slipped under where you had them."
                               : "The backing voices had come up over where you had them - and over the lead.";
            case MixBus::Speech:
                return quieter ? "The speaking microphone fell below where you had it, so the words were getting harder to follow."
                               : "The speaking microphone had come up above where you had it.";
            case MixBus::Drums:
                return quieter ? "The kit had dropped back from where you had it." : "The kit had taken over the mix.";
            case MixBus::Bass:
                return quieter ? "The bass had dropped back from where you had it." : "The bass had taken over the mix.";
            case MixBus::Music:
                return quieter ? "The band had dropped back from where you had it." : "The band had come up over the voices.";
            case MixBus::Ambience:
                return quieter ? "The room had dropped back from where you had it."
                               : "The room had come up over the stage - a broadcast starts to sound like a crowd.";
            case MixBus::Master:
            case MixBus::Count:
            default:
                return quieter ? "This group had dropped back from where you had it."
                               : "This group had come up over where you had it.";
        }
    }
}

float Autopilot::driftDb (const AutopilotTarget& target, const AutopilotReading& now, MixBus bus) noexcept
{
    const size_t b = size_t (bus);
    if (! target.valid || int (bus) < 0 || int (bus) >= int (MixBus::Master)) return 0.0f;
    if (! target.measured[b] || ! now.busActive[b]) return 0.0f;
    if (now.masterRmsDb <= -100.0f || now.busRmsDb[b] <= -100.0f) return 0.0f;
    // Where it sits now, against the master, minus where it sat when the mix was set.
    return (now.busRmsDb[b] - now.masterRmsDb) - target.busBelowMasterDb[b];
}

std::vector<AutopilotMove> Autopilot::decide (const AutopilotTarget& target, const AutopilotReading& now,
                                              const std::array<float, int (MixBus::Count)>& movedSoFarDb,
                                              const std::array<bool, int (MixBus::Count)>& correcting,
                                              const AutopilotLimits& limits)
{
    std::vector<AutopilotMove> out;
    if (! target.valid) return out;
    // Nothing to hold a mix against: no master, no decision. This is the sermon case and the
    // between-songs case, and in both of them doing nothing is the right answer.
    if (now.masterRmsDb <= -100.0f) return out;

    for (int i = 0; i < int (MixBus::Master); ++i)
    {
        const auto bus = MixBus (i);
        const size_t b = size_t (i);
        if (! target.measured[b] || ! now.busActive[b]) continue;
        if (now.busRmsDb[b] <= limits.quietGroupDb) continue;      // not playing: leave it alone

        const float drift = driftDb (target, now, bus);
        // WITHIN TOLERANCE IT DOES NOTHING. Once it is holding a group it keeps holding until
        // the error is well inside the tolerance again, so a group sitting on the boundary is
        // not a fader being nudged up and down for an hour.
        const float threshold = correcting[b] ? limits.toleranceDb - limits.hysteresisDb : limits.toleranceDb;
        if (std::fabs (drift) <= std::max (0.0f, threshold)) continue;

        // The smallest move that helps, never more than one step, and never past the total
        // this engagement is allowed. A group that has used up its travel stops moving and
        // says nothing more: Autopilot holds a mix, it does not rescue one.
        float want = -drift;
        want = std::max (-limits.maxStepDb, std::min (limits.maxStepDb, want));
        const float already = movedSoFarDb[b];
        const float room = want > 0.0f ? limits.maxTotalDb - already : -limits.maxTotalDb - already;
        if ((want > 0.0f && room <= 0.0f) || (want < 0.0f && room >= 0.0f)) continue;
        want = want > 0.0f ? std::min (want, room) : std::max (want, room);
        if (std::fabs (want) < 0.05f) continue;

        AutopilotMove move;
        move.bus = bus;
        move.deltaDb = want;
        move.what = std::string ("Autopilot: ") + mixBusName (bus) + " " + db (want);
        move.why = sentenceFor (bus, drift);
        out.push_back (std::move (move));
    }
    return out;
}

} // namespace livemix
