#include "RelationshipEngine.h"
#include "Profiles/MixProfileData.h"
#include "Profiles/Profile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace livemix
{

const char* mixRelationKindName (MixRelationKind k) noexcept
{
    switch (k)
    {
        case MixRelationKind::KickAndBass:       return "kick_and_bass";
        case MixRelationKind::LeadAndMusic:      return "lead_and_music";
        case MixRelationKind::LeadAndBacking:    return "lead_and_backing";
        case MixRelationKind::CloseAndOverheads: return "close_and_overheads";
        case MixRelationKind::RoomAndReturns:    return "room_and_returns";
        case MixRelationKind::SpeechAndBand:     return "speech_and_band";
        case MixRelationKind::ChannelAndBus:     return "channel_and_bus";
        case MixRelationKind::VocalAndFx:        return "vocal_and_fx";
        case MixRelationKind::MixAndMaster:      return "mix_and_master";
        case MixRelationKind::Count:
        default:                                 return "unknown";
    }
}

namespace
{
    std::string num (const char* fmt, double v) { char b[96]; std::snprintf (b, sizeof (b), fmt, v); return b; }
    std::string db (float v, int decimals = 1) { return num (decimals == 0 ? "%.0f dB" : "%.1f dB", double (v)); }

    bool usable (const AnalysisResult& a)
    {
        return a.valid && ! (a.silencePercent > 95.0f || a.peakDb < -70.0f);
    }

    // Where a band of this source sits once its fader has put it at the profile's mix level.
    // The same convention MixPlanner uses, so a measurement here and a decision there compare.
    float bandAtMixDb (StyleProfileId profile, RoleFamily family, const AnalysisResult& a, Band b)
    {
        return MixProfile::mixLevelTargetDb (profile, family) + a.bandEnergyDb[size_t (b)];
    }

    bool isMusicFamily (RoleFamily f)
    {
        return f == RoleFamily::Piano || f == RoleFamily::ElectricPiano || f == RoleFamily::Organ || f == RoleFamily::Synth
            || f == RoleFamily::AcousticGuitar || f == RoleFamily::ElectricGuitar;
    }

    bool isDrumCloseMic (RoleFamily f)
    {
        return f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom || f == RoleFamily::HiHat;
    }

    // How loud a source actually is in the mix as it stands: its loudness while it plays, at its fader.
    float levelInMixDb (const MixPlanContext& ctx, int i)
    {
        const auto& a = ctx.capture.strips[size_t (i)];
        const float active = a.activeRmsDb > -119.0f ? a.activeRmsDb : a.rmsDb;
        return active + ctx.current.strips[size_t (i)].faderDb;
    }

    int countActiveStages (const ChannelParameters& p)
    {
        int n = 0;
        if (p.hpfEnabled) ++n;
        if (p.lpfEnabled) ++n;
        if (p.gateEnabled) ++n;
        if (p.correctiveEqEnabled) { for (const auto& b : p.correctiveBands) if (b.enabled) { ++n; break; } }
        if (p.deEssEnabled) ++n;
        if (p.compEnabled) ++n;
        if (p.transientEnabled) ++n;
        if (p.toneEqEnabled) { for (const auto& b : p.toneBands) if (b.enabled) { ++n; break; } }
        if (p.satEnabled) ++n;
        if (p.widthEnabled) ++n;
        if (p.limiterEnabled) ++n;
        return n;
    }
}

namespace RelationshipEngine
{

std::vector<MixRelationship> measure (const MixPlanContext& ctx, const std::vector<bool>* heardStrips)
{
    std::vector<MixRelationship> out;
    const StyleProfileId profile = ctx.session.profile;
    const auto& R = MixProfile::relationships (profile);
    const int n = std::min ({ ctx.graph.numStrips(), ctx.current.numStrips, int (ctx.capture.strips.size()) });
    if (n <= 0) return out;

    auto heard = [&] (int i) -> bool
    {
        if (heardStrips != nullptr && i < int (heardStrips->size())) return bool ((*heardStrips)[size_t (i)]);
        return usable (ctx.capture.strips[size_t (i)]);
    };
    auto family = [&] (int i) { return roleFamily (ctx.graph.strips[size_t (i)].role); };
    auto name = [&] (int i) { return ctx.graph.strips[size_t (i)].name; };

    // The loudest heard source of a kind: the one the relationship is really about.
    auto loudest = [&] (auto predicate) -> int
    {
        int best = -1;
        for (int i = 0; i < n; ++i)
        {
            if (! heard (i) || ! predicate (family (i))) continue;
            if (best < 0 || ctx.capture.strips[size_t (i)].hitLevelDb > ctx.capture.strips[size_t (best)].hitLevelDb) best = i;
        }
        return best;
    };

    auto add = [&] (MixRelationKind kind, const char* metric, int a, int b, float value, float tolerance, bool concern, std::string headline)
    {
        MixRelationship r;
        r.kind = kind;
        r.metric = metric;
        r.stripA = a;
        r.stripB = b;
        r.nameA = a >= 0 ? name (a) : std::string();
        r.nameB = b >= 0 ? name (b) : std::string();
        r.value = value;
        r.tolerance = tolerance;
        r.concern = concern;
        r.headline = std::move (headline);
        out.push_back (std::move (r));
    };

    // ---- Kick <-> bass: who owns the low end ----
    {
        const int kick = loudest ([] (RoleFamily f) { return f == RoleFamily::Kick; });
        const int bass = loudest ([] (RoleFamily f) { return f == RoleFamily::ElectricBass || f == RoleFamily::SynthBass; });
        if (kick >= 0 && bass >= 0)
        {
            const auto& ka = ctx.capture.strips[size_t (kick)];
            const auto& ba = ctx.capture.strips[size_t (bass)];
            const float overlap = bandAtMixDb (profile, family (bass), ba, Band::Sub) - bandAtMixDb (profile, RoleFamily::Kick, ka, Band::Sub);
            add (MixRelationKind::KickAndBass, "sub_overlap_db", bass, kick, overlap, R.subOverlapToleranceDb,
                 overlap > R.subOverlapToleranceDb,
                 overlap > R.subOverlapToleranceDb
                     ? name (bass) + " carries " + db (overlap) + " more sub than " + name (kick) + ": the two are competing below 60 Hz."
                     : name (kick) + " and " + name (bass) + " share the low end cleanly (" + db (overlap) + " apart in the sub).");

            if (ka.fundamentalHz > 0.0f && ba.fundamentalHz > 0.0f)
                add (MixRelationKind::KickAndBass, "fundamental_separation_hz", bass, kick,
                     ba.fundamentalHz - ka.fundamentalHz, 0.0f, false,
                     name (kick) + " sits at " + num ("%.0f Hz", double (ka.fundamentalHz)) + ", "
                     + name (bass) + " at " + num ("%.0f Hz", double (ba.fundamentalHz)) + ".");

            // Attack: a kick that arrives after the bass note loses its definition against it.
            add (MixRelationKind::KickAndBass, "transient_strength_difference_db", kick, bass,
                 ka.meanTransientRiseDb - ba.meanTransientRiseDb, 0.0f, false,
                 name (kick) + " attacks " + db (ka.meanTransientRiseDb - ba.meanTransientRiseDb) + " harder than " + name (bass) + ".");
        }
    }

    // ---- Lead vocal <-> the music: presence competition ----
    {
        const int lead = loudest ([] (RoleFamily f) { return f == RoleFamily::LeadVocal; });
        if (lead >= 0)
        {
            const auto& la = ctx.capture.strips[size_t (lead)];
            const float leadPresence = bandAtMixDb (profile, RoleFamily::LeadVocal, la, Band::Presence);
            for (int i = 0; i < n; ++i)
            {
                if (i == lead || ! heard (i) || ! isMusicFamily (family (i))) continue;
                const float presence = bandAtMixDb (profile, family (i), ctx.capture.strips[size_t (i)], Band::Presence);
                const float over = presence - leadPresence;
                add (MixRelationKind::LeadAndMusic, "presence_masking_db", i, lead, over, R.maskingToleranceDb,
                     over > R.maskingToleranceDb,
                     over > R.maskingToleranceDb
                         ? name (i) + " sits " + db (over) + " over " + name (lead) + " around "
                           + num ("%.1f kHz", double (R.vocalPocketHz) / 1000.0) + ", where the words live."
                         : name (i) + " leaves " + name (lead) + " room in the presence band (" + db (over) + ").");
            }
        }
    }

    // ---- Lead <-> backing vocals: hierarchy ----
    {
        const int lead = loudest ([] (RoleFamily f) { return f == RoleFamily::LeadVocal; });
        if (lead >= 0)
        {
            const float leadLevel = levelInMixDb (ctx, lead);
            int voices = 0;
            double sum = 0.0;
            float loudestBacking = -120.0f;
            int loudestIndex = -1;
            for (int i = 0; i < n; ++i)
            {
                if (! heard (i)) continue;
                const RoleFamily f = family (i);
                if (f != RoleFamily::BackingVocal && f != RoleFamily::Choir) continue;
                const float lvl = levelInMixDb (ctx, i);
                ++voices;
                sum += std::pow (10.0, double (lvl) * 0.1);
                if (lvl > loudestBacking) { loudestBacking = lvl; loudestIndex = i; }
            }
            if (voices > 0)
            {
                const float groupLevel = float (10.0 * std::log10 (std::max (sum, 1.0e-12)));
                const float behind = leadLevel - groupLevel;
                add (MixRelationKind::LeadAndBacking, "lead_above_backing_group_db", lead, loudestIndex,
                     behind, R.backingGroupBelowLeadDb, behind < R.backingGroupBelowLeadDb,
                     behind < R.backingGroupBelowLeadDb
                         ? std::to_string (voices) + (voices == 1 ? " backing voice adds up to " : " backing voices add up to ")
                           + db (-behind) + " relative to " + name (lead) + ": the lead is not clearly in front."
                         : name (lead) + " is " + db (behind) + " in front of the backing group.");
            }
        }
    }

    // ---- Close drum microphones <-> overheads ----
    {
        const int oh = loudest ([] (RoleFamily f) { return f == RoleFamily::Overhead; });
        if (oh >= 0)
        {
            const auto& oa = ctx.capture.strips[size_t (oh)];
            const float ohLevel = levelInMixDb (ctx, oh);
            for (int i = 0; i < n; ++i)
            {
                if (! heard (i) || ! isDrumCloseMic (family (i))) continue;
                const float diff = levelInMixDb (ctx, i) - ohLevel;
                // A close microphone under the overheads is the kit heard as a room recording
                // rather than as a kit: the tolerance is the profile's own bus balance window.
                add (MixRelationKind::CloseAndOverheads, "close_above_overhead_db", i, oh, diff, R.busBalanceToleranceDb,
                     diff < -R.busBalanceToleranceDb,
                     diff < -R.busBalanceToleranceDb
                         ? name (oh) + " is louder than " + name (i) + " by " + db (-diff) + ": the kit is arriving mostly through the overheads."
                         : name (i) + " sits " + db (diff) + " against " + name (oh) + ".");
            }
            if (oa.numChannels == 2)
                add (MixRelationKind::CloseAndOverheads, "overhead_correlation", oh, -1, oa.stereoCorrelation, 0.2f,
                     oa.stereoCorrelation < 0.0f,
                     oa.stereoCorrelation < 0.0f
                         ? name (oh) + " reads " + num ("%.2f", double (oa.stereoCorrelation)) + " correlation: the pair may be out of phase."
                         : name (oh) + " reads " + num ("%.2f", double (oa.stereoCorrelation)) + " correlation.");
        }
    }

    // ---- Real room microphones <-> the artificial room return ----
    {
        const int room = loudest ([] (RoleFamily f) { return f == RoleFamily::Room; });
        if (room >= 0 && ctx.graph.fxUsed[size_t (FxSlot::DrumRoom)])
            add (MixRelationKind::RoomAndReturns, "room_mic_present_with_return", room, -1,
                 levelInMixDb (ctx, room), 0.0f, true,
                 name (room) + " already puts the real room in the mix while the Drum Room return is running.");
    }

    // ---- A speech microphone open while the band plays ----
    {
        const int lead = loudest ([] (RoleFamily f) { return f == RoleFamily::LeadVocal; });
        for (int i = 0; i < n; ++i)
        {
            if (! heard (i) || family (i) != RoleFamily::Speech) continue;
            const float bleed = lead >= 0 ? levelInMixDb (ctx, i) - levelInMixDb (ctx, lead) : 0.0f;
            add (MixRelationKind::SpeechAndBand, "speech_below_lead_db", i, lead, bleed, 0.0f, lead >= 0,
                 lead >= 0 ? name (i) + " is open " + db (bleed) + " under " + name (lead)
                             + ": what it hears during the song is the band, not a sermon."
                           : name (i) + " is the only voice in the listen.");
        }
    }

    // ---- Channel and bus processing stacking on the same source ----
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        if (! ctx.graph.busUsed[size_t (b)]) continue;
        const int busStages = countActiveStages (ctx.current.buses[size_t (b)].channel);
        if (busStages == 0) continue;
        int worst = -1, worstStages = 0;
        for (int i = 0; i < n; ++i)
        {
            if (! heard (i) || ctx.graph.strips[size_t (i)].bus != MixBus (b)) continue;
            const int stages = countActiveStages (ctx.current.strips[size_t (i)].channel);
            if (stages > worstStages) { worstStages = stages; worst = i; }
        }
        if (worst < 0) continue;
        const int total = worstStages + busStages;
        // Six stages on one path is where a source stops sounding like itself: it is a flag for
        // the reasoning layer, not a rule - a lead vocal legitimately carries more than a tom.
        add (MixRelationKind::ChannelAndBus, "stacked_stages", worst, -1, float (total), 6.0f, total > 6,
             name (worst) + " passes through " + std::to_string (worstStages) + " stage"
             + (worstStages == 1 ? "" : "s") + " and " + std::string (mixBusName (MixBus (b))) + " adds "
             + std::to_string (busStages) + ".");
    }

    // ---- The vocals against the effects: depth bought with intelligibility ----
    {
        const int lead = loudest ([] (RoleFamily f) { return f == RoleFamily::LeadVocal; });
        if (lead >= 0)
        {
            float loudestSend = kSilenceDb;
            for (int f = 0; f < int (FxSlot::Count); ++f)
                if (ctx.graph.fxUsed[size_t (f)])
                    loudestSend = std::max (loudestSend, ctx.current.strips[size_t (lead)].sendDb[size_t (f)]);
            if (loudestSend > kSilenceDb + 1.0f)
            {
                const auto& la = ctx.capture.strips[size_t (lead)];
                // A voice with little of its own articulation (a dull capture, or a heavy room)
                // loses the words first when a return is opened up.
                const bool articulate = la.bandEnergyDb[size_t (Band::Presence)] > la.bandEnergyDb[size_t (Band::Mid)] - 12.0f;
                add (MixRelationKind::VocalAndFx, "lead_send_db", lead, -1, loudestSend, -6.0f, ! articulate,
                     articulate ? name (lead) + " carries its own articulation, so its return at " + db (loudestSend, 0) + " buys depth without costing words."
                                : name (lead) + " is short of presence of its own; a return at " + db (loudestSend, 0) + " will cost intelligibility before it adds depth.");
            }
        }
    }

    // ---- What arrives at the master ----
    {
        const auto& m = ctx.capture.masterOutput;
        if (m.valid)
        {
            const float headroom = -m.truePeakDb;
            add (MixRelationKind::MixAndMaster, "master_true_peak_db", -1, -1, m.truePeakDb, -1.0f, m.truePeakDb > -1.0f,
                 "The master reached " + db (m.truePeakDb) + " true peak (" + db (headroom) + " of headroom).");
            if (m.loudnessLufs > -119.0f)
                add (MixRelationKind::MixAndMaster, "master_loudness_lufs", -1, -1, m.loudnessLufs, -23.0f, false,
                     "The master measured " + num ("%.1f LUFS", double (m.loudnessLufs)) + " over the listen.");
            add (MixRelationKind::MixAndMaster, "master_crest_db", -1, -1, m.crestFactorDb, 8.0f, m.crestFactorDb < 8.0f,
                 m.crestFactorDb < 8.0f
                     ? "The master's crest factor is only " + db (m.crestFactorDb) + ": the mix arrives already flattened."
                     : "The master keeps " + db (m.crestFactorDb) + " of crest factor.");
        }
    }

    return out;
}

} // namespace RelationshipEngine
} // namespace livemix
