#include "MixPlanner.h"
#include "Tune/TuneEngine.h"
#include "Tune/SourceStrategy.h"
#include "Intelligence/SafetyValidator.h"
#include "Profiles/MixProfileData.h"
#include "Profiles/Profile.h"
#include "FX/FxProfiles.h"
#include "Core/DbUtils.h"
#include "DSP/Compressor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace MixPlanner { float predictedProcessedPeakDb (const MixPlanContext&, int, const StripParameters&); float predictedProcessedRmsDb (const MixPlanContext&, int, const StripParameters&);
                       float predictedProcessedActiveRmsDb (const MixPlanContext&, int, const StripParameters&); }

namespace
{
    using tune::fmtDb;
    using tune::fmtHz;

    std::string num (const char* fmt, double v) { char b[64]; std::snprintf (b, sizeof (b), fmt, v); return b; }
    float roundHalf (float v) { return std::round (v * 2.0f) * 0.5f; }
    std::string upper (std::string s) { for (auto& c : s) c = char (std::toupper (static_cast<unsigned char> (c))); return s; }

    // The same test TuneEngine uses for "no usable signal".
    bool heard (const AnalysisResult& a) { return a.valid && ! (a.silencePercent > 95.0f || a.peakDb < -70.0f); }

    // Where a band of this source will sit once its fader has put it at the profile's mix level:
    // the band's energy relative to the source (<= 0) plus the mix level the source is aimed at.
    float bandAtMixDb (StyleProfileId profile, RoleFamily family, const AnalysisResult& a, Band b)
    {
        return MixProfile::mixLevelTargetDb (profile, family) + a.bandEnergyDb[size_t (b)];
    }

    // The close microphones on a drum kit: each one hears every other drum in the room, so what a fader
    // does to one of them it also does to the bleed. Overheads and room microphones are meant to hear the
    // whole kit, and everything else is not sharing a stand with another instrument.
    bool isDrumCloseMic (RoleFamily f)
    {
        return f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom || f == RoleFamily::HiHat;
    }

    bool isMusicFamily (RoleFamily f)
    {
        return f == RoleFamily::Piano || f == RoleFamily::ElectricPiano || f == RoleFamily::Organ || f == RoleFamily::Synth
            || f == RoleFamily::AcousticGuitar || f == RoleFamily::ElectricGuitar;
    }

    // The tempo the band is actually playing, agreed across the sources. One channel is easily fooled -
    // a shuffle reads as 4/3 of the beat, a close tom mostly hears the snare - and the confident answer
    // is not always the right one, but a shuffle does not fool the whole band at once. Each heard source
    // votes with its own confidence for every candidate within kTempoAgreementPercent of it, and the
    // candidate carrying the most agreement wins. 0 when nothing agrees.
    constexpr float kTempoAgreementPercent = 4.0f;
    // Only a source that actually plays a rhythm gets a vote. A held organ note or an open room
    // microphone has no onsets to be periodic, so its autocorrelation is reading noise - and there are
    // usually more of those on a stage than there are drums, so letting them vote buries the kick.
    constexpr float kTempoVoterOnsetsPerSecond = 0.5f;
    constexpr int kTempoVoterMinEvents = 8;

    bool tempoVoter (const AnalysisResult& a, const StripPlan& sp)
    {
        return sp.heard && a.tempoBpm > 0.0f
            && a.transientsPerSecond >= kTempoVoterOnsetsPerSecond
            && a.eventCount >= kTempoVoterMinEvents;
    }

    float consensusTempoBpm (const MixPlanContext& ctx, const std::vector<StripPlan>& strips, int n, float& agreementOut)
    {
        agreementOut = 0.0f;
        double total = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto& a = ctx.capture.strips[size_t (i)];
            if (! tempoVoter (a, strips[size_t (i)])) continue;
            total += double (a.tempoConfidence);
        }
        if (total <= 0.0) return 0.0f;

        float bestBpm = 0.0f;
        double bestWeight = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto& candidate = ctx.capture.strips[size_t (i)];
            if (! tempoVoter (candidate, strips[size_t (i)])) continue;
            double weight = 0.0;
            double weighted = 0.0;
            for (int j = 0; j < n; ++j)
            {
                const auto& voter = ctx.capture.strips[size_t (j)];
                if (! tempoVoter (voter, strips[size_t (j)])) continue;
                if (std::fabs (voter.tempoBpm - candidate.tempoBpm) > candidate.tempoBpm * kTempoAgreementPercent * 0.01f) continue;
                weight += double (voter.tempoConfidence);
                weighted += double (voter.tempoConfidence) * double (voter.tempoBpm);
            }
            if (weight > bestWeight) { bestWeight = weight; bestBpm = weight > 0.0 ? float (weighted / weight) : candidate.tempoBpm; }
        }
        agreementOut = float (bestWeight / total);
        return bestBpm;
    }

    // Applies a strip's relationship decisions through the same validator every Tune path uses.
    void commit (TuneDecisions& d, ChannelParameters& target, std::vector<Recommendation>& stripItems, std::vector<Recommendation>& mixItems)
    {
        if (d.items.empty()) return;
        RecommendationResult rep;
        rep.valid = true;
        rep.items = d.items;
        rep = SafetyValidator::validate (rep, false);
        std::vector<ParameterChange> all;
        for (const auto& item : rep.items) all.insert (all.end(), item.changes.begin(), item.changes.end());
        target = applyChanges (target, all);
        for (const auto& item : rep.items) { stripItems.push_back (item); mixItems.push_back (item); }
    }

    Recommendation info (Recommendation::Kind kind, std::string what, std::string why, Confidence c)
    {
        Recommendation r;
        r.kind = kind;
        r.section = Recommendation::sectionFor (kind);
        r.what = std::move (what);
        r.why = std::move (why);
        r.confidence = c;
        return r;
    }

    // Moves every absolute level in a measurement by `db` (a gain change ahead of the analysis point).
    void shiftLevels (AnalysisResult& a, float db)
    {
        a.peakDb += db; a.rmsDb += db; a.hitLevelDb += db; a.noiseFloorDb += db; a.truePeakDb += db;
        if (a.activeRmsDb > -119.0f) a.activeRmsDb += db;
        if (a.musicalPeakDb > -119.0f) a.musicalPeakDb += db;
        if (a.eventLevelDb > -119.0f) a.eventLevelDb += db;
        if (a.loudnessLufs > -100.0f) a.loudnessLufs += db;
        for (auto& c : a.channelRmsDb) if (c > -100.0f) c += db;
    }

    // What the compressor stage does to a level of `inDb` (dB, output minus input): the static curve scaled by `share`
    // of the reduction, plus the makeup, blended by the dry/wet mix.
    float compressorEffectDb (const ChannelParameters& p, float inDb, float share)
    {
        if (! p.compEnabled) return 0.0f;
        const float reduction = (Compressor::computeGain (inDb, p.compThresholdDb, p.compRatio, p.compKneeDb) - inDb) * share;
        const float wet = reduction + p.compMakeupDb;
        if (p.compMix >= 0.999f) return wet;
        return gainToDb (p.compMix * dbToGain (wet) + (1.0f - p.compMix));
    }

    // Where a strip's processed RMS lands under a parameter set, after its fader. RMS, not peak, because what the buses
    // and the master loudness rule need is how much energy arrives; a snare's peaks would otherwise outweigh every voice.
    float stripLevelDb (const MixPlanContext& ctx, const MixParameters& p, int i)
    {
        return MixPlanner::predictedProcessedRmsDb (ctx, i, p.strips[size_t (i)]) + p.strips[size_t (i)].faderDb;
    }

    // The change (dB) a chain's compressor makes to a stream's average level when that stream arrives `shiftDb` louder
    // than at the listen, where it measured `rmsDb` / `peakDb` at the compressor: the average reduction follows a
    // level between the RMS and the peaks (the release holds it between syllables and hits).
    float compressorAverageDeltaDb (const ChannelParameters& after, const ChannelParameters& atCapture, float rmsDb, float peakDb, float shiftDb, float crestShare)
    {
        const float level = rmsDb + crestShare * (peakDb - rmsDb);
        return compressorEffectDb (after, level + shiftDb, 1.0f) - compressorEffectDb (atCapture, level, 1.0f);
    }

    // Level shift (dB) of a group of strips between two parameter sets, power-summed from their processed levels.
    float faderShiftDb (const MixPlanContext& ctx, const MixParameters& from, const MixParameters& to, MixBus bus, bool allBuses)
    {
        double before = 0.0, after = 0.0;
        for (int i = 0; i < ctx.graph.numStrips() && i < int (ctx.capture.processed.size()); ++i)
        {
            if (! allBuses && ctx.graph.strips[size_t (i)].bus != bus) continue;
            const auto& o = ctx.capture.processed[size_t (i)];
            if (! o.valid || o.peakDb <= -60.0f) continue;
            before += std::pow (10.0, double (stripLevelDb (ctx, from, i)) / 10.0);
            after  += std::pow (10.0, double (stripLevelDb (ctx, to, i)) / 10.0);
        }
        if (before <= 0.0 || after <= 0.0) return 0.0f;
        return clamp (float (10.0 * std::log10 (after / before)), -30.0f, 30.0f);
    }
}

namespace MixPlanner
{

// Where a strip's processed (pre-fader) peak lands under `strip`, predicted from what was measured under the chain that ran
// at the listen: the input-gain change moves the chain input, and the compressor's curve says how much of that survives.
// With the same gain and chain as at the listen this is the measurement itself, so planning again changes nothing.
float predictedProcessedPeakDb (const MixPlanContext& ctx, int i, const StripParameters& strip)
{
    const auto& o = ctx.capture.processed[size_t (i)];
    const auto& a = ctx.capture.strips[size_t (i)];
    const auto& atCapture = ctx.atCapture.strips[size_t (i)];
    const float rise = MixProfile::compPeakRiseMs (ctx.session.profile, ctx.graph.strips[size_t (i)].role);
    const float gainDelta = strip.inputGainDb - atCapture.inputGainDb;
    // The raw tap sits after the input gain, so the measured raw peak is the chain input at the listen. A compressor
    // with attack `a` has reached 1 - exp(-rise / a) of its static reduction when the peak arrives.
    const float shareAfter = 1.0f - std::exp (-rise / std::max (strip.channel.compAttackMs, 0.1f));
    const float shareBefore = 1.0f - std::exp (-rise / std::max (atCapture.channel.compAttackMs, 0.1f));
    return o.peakDb + gainDelta
         + compressorEffectDb (strip.channel, a.peakDb + gainDelta, shareAfter)
         - compressorEffectDb (atCapture.channel, a.peakDb, shareBefore);
}

float predictedProcessedRmsDb (const MixPlanContext& ctx, int i, const StripParameters& strip)
{
    const auto& o = ctx.capture.processed[size_t (i)];
    const auto& a = ctx.capture.strips[size_t (i)];
    const auto& atCapture = ctx.atCapture.strips[size_t (i)];
    const float gainDelta = strip.inputGainDb - atCapture.inputGainDb;
    return o.rmsDb + gainDelta
         + compressorAverageDeltaDb (strip.channel, atCapture.channel, a.rmsDb, a.peakDb, gainDelta,
                                     MixProfile::relationships (ctx.session.profile).compDetectorCrestShareStrip);
}

// Where a strip's processed loudness-while-playing lands under a parameter set. The processed tap
// measures peak and RMS over the whole listen; how much of that listen the source was actually playing
// is a property of the source, which the chain does not change, so the raw measurement's own
// active-to-overall offset carries across. This is what the balance is fitted to.
float predictedProcessedActiveRmsDb (const MixPlanContext& ctx, int i, const StripParameters& strip)
{
    const auto& a = ctx.capture.strips[size_t (i)];
    const float dutyCycleDb = (a.activeRmsDb > -119.0f && a.rmsDb > -119.0f) ? std::max (a.activeRmsDb - a.rmsDb, 0.0f) : 0.0f;
    return predictedProcessedRmsDb (ctx, i, strip) + dutyCycleDb;
}

int countParameterChanges (const MixParameters& from, const MixParameters& to)
{
    int n = 0;
    const int strips = std::min (from.numStrips, to.numStrips);
    for (int i = 0; i < strips; ++i)
    {
        n += int (diffParameters (from.strips[size_t (i)].channel, to.strips[size_t (i)].channel).size());
        if (std::fabs (from.strips[size_t (i)].faderDb - to.strips[size_t (i)].faderDb) > 0.01f) ++n;
        if (std::fabs (from.strips[size_t (i)].inputGainDb - to.strips[size_t (i)].inputGainDb) > 0.01f) ++n;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (std::fabs (from.strips[size_t (i)].sendDb[size_t (f)] - to.strips[size_t (i)].sendDb[size_t (f)]) > 0.01f) ++n;
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        n += int (diffParameters (from.buses[size_t (b)].channel, to.buses[size_t (b)].channel).size());
        if (std::fabs (from.buses[size_t (b)].faderDb - to.buses[size_t (b)].faderDb) > 0.01f) ++n;
    }
    return n;
}

MixPlan plan (const MixPlanContext& ctx)
{
    MixPlan plan;
    plan.before = ctx.current;
    plan.proposed = ctx.current;

    const StyleProfileId profile = ctx.session.profile;
    const auto& R = MixProfile::relationships (profile);
    const int n = std::min ({ ctx.graph.numStrips(), ctx.current.numStrips, int (ctx.capture.strips.size()) });
    if (n <= 0)
    {
        plan.headline = "MIX: NO SIGNAL";
        plan.notes.push_back ("Nothing was captured. Check the input device and that inputs are assigned, then Tune Mix again.");
        return plan;
    }

    // ---- 1. Every source on its own: the existing Tune, unchanged ----
    plan.strips.resize (size_t (n));
    for (int i = 0; i < n; ++i)
    {
        const auto& route = ctx.graph.strips[size_t (i)];
        auto& sp = plan.strips[size_t (i)];
        sp.strip = i;
        sp.name = route.name;
        sp.role = route.role;
        sp.faderBeforeDb = sp.faderDb = ctx.current.strips[size_t (i)].faderDb;
        const float gainAtCapture = ctx.atCapture.strips[size_t (i)].inputGainDb;
        sp.inputGainBeforeDb = sp.inputGainDb = ctx.current.strips[size_t (i)].inputGainDb;
        sp.capturePeakDb = ctx.capture.strips[size_t (i)].peakDb - gainAtCapture;
        sp.heard = heard (ctx.capture.strips[size_t (i)]);

        // A signal that never got above the faint level at the device is not a source playing: the mic is off, the cable
        // or preamp is wrong, or the player sat out. Tuning it would fit a chain to noise and the gain would pull up bleed.
        if (sp.heard && ctx.capture.strips[size_t (i)].peakDb - gainAtCapture < R.faintInputDb)
        {
            sp.heard = false;
            sp.faint = true;
            ++plan.stripsFaint;
            sp.mixItems.push_back (info (Recommendation::Kind::Info, upper (sp.name) + ": barely reached DLIVE, check this input",
                                         "Its loudest moment during the listen was " + num ("%.0f dBFS", double (ctx.capture.strips[size_t (i)].peakDb - gainAtCapture))
                                         + " at the device, too quiet to be a source that is really playing. Check the microphone, the cable and the preamp, "
                                         "then Tune Mix again. Nothing about it was changed.", Confidence::High));
            continue;
        }
        if (sp.heard) ++plan.stripsHeard;

        TuneContext tc;
        tc.analysis = ctx.capture.strips[size_t (i)];
        tc.role = route.role;
        tc.profile = profile;
        tc.current = ctx.current.strips[size_t (i)].channel;
        tc.hasOutput = false;   // the mix balances with faders below, not with the strip's output trim
        sp.tune = TuneEngine::tune (tc);

        // Input gain: the console move Tune recommends, done digitally where DLIVE owns the input stage, in one
        // go (a person turns a preamp one step at a time; a number does not have to). Computed from the listen and
        // the gain at the listen (never the current value); the chain input is never pushed above the profile's
        // ceiling, and the strip is re-tuned as it will now be heard.
        const float toHealthy = sp.heard ? tune::captureGainToHealthyDb (tc.analysis, Profiles::targets (profile, route.role)) : 0.0f;
        if (sp.heard && sp.tune.valid && std::fabs (toHealthy) >= 1.0f)
        {
            float gain = gainAtCapture + toHealthy;
            const float musicalPeak = tc.analysis.musicalPeakDb > -119.0f ? tc.analysis.musicalPeakDb : tc.analysis.peakDb;
            gain = std::min (gain, gainAtCapture + (R.inputPeakCeilingDb - musicalPeak));
            gain = clamp (roundHalf (gain), -R.maxInputGainDb, R.maxInputGainDb);
            const float delta = gain - gainAtCapture;
            if (std::fabs (delta) >= 0.5f)
            {
                shiftLevels (tc.analysis, delta);
                sp.tune = TuneEngine::tune (tc);
            }
            sp.inputGainDb = gain;
        }
        if (sp.tune.valid) plan.proposed.strips[size_t (i)].channel = sp.tune.proposed;
        plan.proposed.strips[size_t (i)].inputGainDb = sp.inputGainDb;
        if (std::fabs (sp.inputGainDb - sp.inputGainBeforeDb) >= 0.5f)
        {
            const auto& a = ctx.capture.strips[size_t (i)];
            const bool up = sp.inputGainDb > sp.inputGainBeforeDb;
            sp.mixItems.push_back (info (Recommendation::Kind::CaptureGain, upper (sp.name) + " input gain " + fmtDb (sp.inputGainDb, 1),
                                         "This input reaches DLIVE at " + num ("%.0f dBFS", double (a.peakDb - (gainAtCapture))) + " peak from the device"
                                         + (up ? ", under the healthy range, so DLIVE raises it digitally and the processing works at the right level. Raising the console preamp instead keeps the noise floor down."
                                               : ", hotter than the healthy range, so DLIVE lowers it before the processing. Clipping at the converter itself cannot be undone here; lower the preamp when you can."),
                                         Confidence::High));
        }
    }
    if (plan.stripsHeard == 0)
    {
        plan.headline = "MIX: NO SIGNAL";
        plan.notes.push_back ("No input carried a usable signal during the listen. Have the band play and Tune Mix again.");
        plan.valid = true;
        return plan;
    }

    // A speech microphone that is "heard" while a lead vocal sings is picking up the band, not a sermon.
    // Its level is left where it is; Tune Mix again while the pastor speaks and the band is quiet.
    {
        bool leadHeard = false;
        for (int i = 0; i < n; ++i) if (plan.strips[size_t (i)].heard && roleFamily (plan.strips[size_t (i)].role) == RoleFamily::LeadVocal) leadHeard = true;
        if (leadHeard)
            for (int i = 0; i < n; ++i)
            {
                auto& sp = plan.strips[size_t (i)];
                if (! sp.heard || roleFamily (sp.role) != RoleFamily::Speech) continue;
                sp.bleedOnly = true;
                plan.proposed.strips[size_t (i)].inputGainDb = ctx.current.strips[size_t (i)].inputGainDb;
                sp.inputGainDb = sp.inputGainBeforeDb;
                {
                    TuneContext tc;
                    tc.analysis = ctx.capture.strips[size_t (i)];
                    shiftLevels (tc.analysis, ctx.current.strips[size_t (i)].inputGainDb - ctx.atCapture.strips[size_t (i)].inputGainDb);
                    tc.role = sp.role; tc.profile = profile; tc.current = ctx.current.strips[size_t (i)].channel; tc.hasOutput = false;
                    sp.tune = TuneEngine::tune (tc);
                    if (sp.tune.valid) plan.proposed.strips[size_t (i)].channel = sp.tune.proposed;
                }
                sp.mixItems.clear();
                sp.mixItems.push_back (info (Recommendation::Kind::Info, upper (sp.name) + ": heard as spill while the lead sang",
                                             "A speech microphone open during the song picks up the band, so its level and gain were left alone. "
                                             "Tune Mix again while the pastor speaks and the band is quiet to set it.", Confidence::High));
            }
    }

    // ---- Tempo: what the delays have to be in time with ----
    {
        float agreement = 0.0f;
        const float bpm = consensusTempoBpm (ctx, plan.strips, n, agreement);
        // Only a tempo most of the band agrees on is worth retiming the delays to; below that the
        // engine default stands and the sends stay where the profile put them.
        if (bpm > 0.0f && agreement >= 0.5f)
        {
            plan.proposed.tempoBpm = bpm;
            // With the tempo known the reverb tails can be fitted to the song as well as the delays: a tail
            // that outlasts the phrase is the difference between a mix with depth and a mix that washes.
            // Computed from the tempo and the FX profile, never from the current value, so a re-plan lands here again.
            const float secondsPerBeat = 60.0f / bpm;
            for (int f = 0; f < int (FxSlot::Count); ++f)
            {
                if (! ctx.graph.fxUsed[size_t (f)]) continue;
                const float beats = MixProfile::reverbBeats (profile, FxSlot (f));
                if (beats <= 0.0f) continue;
                auto& fx = plan.proposed.fx[size_t (f)];
                if (! fx.fx.reverbEnabled) continue;
                const float character = FxProfiles::baseline (profile, ctx.graph.fxType[size_t (f)]).reverbDecayS;
                const float fitted = clamp (beats * secondsPerBeat, 0.5f * character, character);
                if (std::fabs (fitted - fx.fx.reverbDecayS) < 0.05f) continue;
                fx.fx.reverbDecayS = fitted;
                plan.relationships.push_back (info (Recommendation::Kind::Info,
                                                     std::string (fxSlotName (FxSlot (f))) + " tail shortened to " + num ("%.1f s", double (fitted)),
                                                     "At " + num ("%.0f BPM", double (bpm)) + " that is about " + num ("%.0f", double (beats))
                                                     + " beats: the tail clears before the next phrase instead of sounding under it. The effect keeps its character - "
                                                     + std::string (FxProfiles::intent (ctx.graph.fxType[size_t (f)])) + ".", Confidence::Medium));
            }
            if (std::fabs (plan.before.tempoBpm - bpm) >= 1.0f)
                plan.relationships.push_back (info (Recommendation::Kind::Info, "Delays timed to the song: " + num ("%.0f BPM", double (bpm)),
                                                     "A delay that is not in time with the band smears the voice instead of supporting it, and a live console has no "
                                                     "play head to read the tempo from. DLIVE measured it from the listen ("
                                                     + num ("%.0f%%", double (agreement * 100.0)) + " of the sources agree) and every tempo-synced return now follows it.",
                                                     agreement > 0.7f ? Confidence::High : Confidence::Medium));
        }
    }

    auto findHeard = [&] (auto predicate) -> int
    {
        int best = -1;
        for (int i = 0; i < n; ++i)
            if (plan.strips[size_t (i)].heard && predicate (roleFamily (plan.strips[size_t (i)].role)))
                if (best < 0 || ctx.capture.strips[size_t (i)].hitLevelDb > ctx.capture.strips[size_t (best)].hitLevelDb) best = i;
        return best;
    };
    auto anyHeard = [&] (RoleFamily f) { return findHeard ([f] (RoleFamily g) { return g == f; }) >= 0; };

    // ---- 2. Relationships ----
    // Kick <-> bass: who owns the sub.
    {
        const int kick = findHeard ([] (RoleFamily f) { return f == RoleFamily::Kick; });
        const int bass = findHeard ([] (RoleFamily f) { return f == RoleFamily::ElectricBass || f == RoleFamily::SynthBass; });
        if (kick >= 0 && bass >= 0)
        {
            const auto& ka = ctx.capture.strips[size_t (kick)];
            const auto& ba = ctx.capture.strips[size_t (bass)];
            const RoleFamily bf = roleFamily (plan.strips[size_t (bass)].role);
            const float overlap = bandAtMixDb (profile, bf, ba, Band::Sub) - bandAtMixDb (profile, RoleFamily::Kick, ka, Band::Sub);
            if (overlap > R.subOverlapToleranceDb)
            {
                const float excess = overlap - R.subOverlapToleranceDb;
                float hpf = clamp (R.bassHpfMinHz + 2.0f * excess, R.bassHpfMinHz, R.bassHpfMaxHz);
                TuneContext bc;
                bc.analysis = ba; bc.role = plan.strips[size_t (bass)].role; bc.profile = profile; bc.current = plan.proposed.strips[size_t (bass)].channel;
                const float fund = tune::fundamental (bc, Profiles::targets (profile, bc.role));
                if (fund > 0.0f) hpf = std::min (hpf, 0.8f * fund);
                hpf = std::round (hpf);
                TuneDecisions d (plan.proposed.strips[size_t (bass)].channel);
                d.move (Recommendation::Kind::Filter, TuneSection::Tone,
                        "Bass high-pass at " + fmtHz (hpf) + " so the kick owns the sub",
                        "Below 60 Hz the bass carries " + fmtDb (overlap, 0) + " more energy than the kick at mix level. In " + std::string (styleProfileName (profile))
                        + " the kick owns the very bottom and the bass sits just above it; nothing you hear in the bass lives under " + fmtHz (hpf) + ".",
                        Confidence::Medium, [=] (ChannelParameters& p) { p.hpfEnabled = true; p.hpfHz = std::max (p.hpfHz, hpf); });
                commit (d, plan.proposed.strips[size_t (bass)].channel, plan.strips[size_t (bass)].mixItems, plan.relationships);
            }
            else
                plan.relationships.push_back (info (Recommendation::Kind::Info, "Kick and bass share the low end cleanly",
                                                     "At mix level the bass carries " + fmtDb (overlap, 0) + " relative to the kick below 60 Hz, inside the profile's tolerance.", Confidence::Medium));
        }
    }

    // Lead vocal <-> music: the music makes room for the words instead of the voice getting brighter.
    {
        const int lead = findHeard ([] (RoleFamily f) { return f == RoleFamily::LeadVocal; });
        if (lead >= 0)
        {
            const auto& la = ctx.capture.strips[size_t (lead)];
            const float leadPresence = bandAtMixDb (profile, RoleFamily::LeadVocal, la, Band::UpperMid);
            for (int i = 0; i < n; ++i)
            {
                auto& sp = plan.strips[size_t (i)];
                const RoleFamily f = roleFamily (sp.role);
                if (! sp.heard || ! isMusicFamily (f)) continue;
                const float masking = bandAtMixDb (profile, f, ctx.capture.strips[size_t (i)], Band::UpperMid) - leadPresence;
                if (masking <= -R.maskingToleranceDb) continue;
                const float cut = clamp (roundHalf ((masking + R.maskingToleranceDb) * 0.5f), 1.0f, R.vocalPocketMaxCutDb);
                auto& band = plan.proposed.strips[size_t (i)].channel.toneBands[1];
                if (band.enabled && std::fabs (band.freqHz - R.vocalPocketHz) > 1.0f)
                {
                    plan.relationships.push_back (info (Recommendation::Kind::Info, upper (sp.name) + ": vocal pocket skipped",
                                                         "The tone band the pocket would use is already shaping this source; nothing was changed.", Confidence::Low));
                    continue;
                }
                TuneDecisions d (plan.proposed.strips[size_t (i)].channel);
                const float hz = R.vocalPocketHz, q = R.vocalPocketQ;
                d.move (Recommendation::Kind::EQ, TuneSection::Tone,
                        "Made room for the lead vocal in " + upper (sp.name) + ": " + fmtDb (-cut, 1) + " at " + fmtHz (hz),
                        // Past ~24 dB the figure has stopped meaning "this is masking the voice" and
                        // started meaning "the voice has almost nothing in this band" - a number there
                        // is precision the measurement does not have, so it is described instead.
                        "Around " + fmtHz (hz) + " this source carries "
                        + (masking > 24.0f ? std::string ("far more energy than")
                                           : masking >= 0.0f ? fmtDb (masking, 0) + " more energy than"
                                                             : std::string ("almost as much energy as"))
                        + " the lead vocal at mix level. Rather than pushing the voice brighter, the music steps aside where the words live.",
                        Confidence::Medium, [=] (ChannelParameters& p) { p.toneEqEnabled = true; p.toneBands[1] = { true, FilterType::Peak, hz, -cut, q }; });
                commit (d, plan.proposed.strips[size_t (i)].channel, sp.mixItems, plan.relationships);
            }
        }
    }

    // Toms <-> overheads: the overheads already carry the ring, so close-mic gates stay gentle.
    if (anyHeard (RoleFamily::Overhead))
    {
        for (int i = 0; i < n; ++i)
        {
            auto& sp = plan.strips[size_t (i)];
            if (roleFamily (sp.role) != RoleFamily::Tom) continue;
            const auto& p = plan.proposed.strips[size_t (i)].channel;
            if (! p.gateEnabled || p.gateRangeDb <= R.tomGateMaxRangeWithOverheadsDb) continue;
            TuneDecisions d (p);
            const float range = R.tomGateMaxRangeWithOverheadsDb;
            d.move (Recommendation::Kind::Gate, TuneSection::Bleed,
                    upper (sp.name) + " gate kept gentle (" + fmtDb (-range, 0) + " instead of closing)",
                    "The overheads already carry the toms' ring and the cymbals between hits. A gate that shuts completely would make the kit sound chopped; "
                    "this one only turns the close mic down by " + num ("%.0f dB", double (range)) + " between hits.",
                    Confidence::Medium, [=] (ChannelParameters& q) { q.gateRangeDb = std::min (q.gateRangeDb, range); });
            commit (d, plan.proposed.strips[size_t (i)].channel, sp.mixItems, plan.relationships);
        }
    }

    // Drum room return <-> real room microphones.
    if (anyHeard (RoleFamily::Room) && ctx.graph.fxUsed[size_t (FxSlot::DrumRoom)])
    {
        bool moved = false;
        for (int i = 0; i < n; ++i)
        {
            auto& sp = plan.strips[size_t (i)];
            const RoleFamily f = roleFamily (sp.role);
            const float base = MixProfile::defaultSendDb (profile, f, FxSlot::DrumRoom);
            if (base <= kSilenceDb) continue;
            const float target = base - R.drumRoomSendCutWithRoomMicsDb;
            auto& send = plan.proposed.strips[size_t (i)].sendDb[size_t (FxSlot::DrumRoom)];
            if (std::fabs (send - target) > 0.01f) { send = target; moved = true; }
        }
        if (moved)
            plan.relationships.push_back (info (Recommendation::Kind::Info, "Drum room return stepped back " + fmtDb (-R.drumRoomSendCutWithRoomMicsDb, 0),
                                                 "Real room microphones are in the mix, so the artificial drum room only adds a little depth instead of doubling the space.", Confidence::Medium));
    }

    // ---- 3. Balance: faders fitted to the profile's mix levels from the measured processed peaks ----
    for (int i = 0; i < n; ++i)
    {
        auto& sp = plan.strips[size_t (i)];
        const auto& o = i < int (ctx.capture.processed.size()) ? ctx.capture.processed[size_t (i)] : OutputStats {};
        const auto& a = ctx.capture.strips[size_t (i)];
        if (sp.faint) continue;
        if (! sp.heard)
        {
            sp.mixItems.push_back (info (Recommendation::Kind::Info, upper (sp.name) + ": not heard during the listen",
                                         "This input stayed quiet, so its level was left where it is. Tune Mix again while it is playing.", Confidence::High));
            continue;
        }
        if (sp.bleedOnly) continue;
        if (! o.valid || o.peakDb <= -60.0f || a.silencePercent > 60.0f) continue;   // too sparse to place with confidence
        const RoleFamily f = roleFamily (sp.role);
        const float target = MixProfile::mixLevelTargetDb (profile, f);
        // How loud this source will be while it plays once its new gain and chain run: predicted from the
        // listen, so the first Tune Mix lands the balance instead of needing a second listen to hear the
        // processing it just chose. Loudness, not peak - see mixLevelTargetDb.
        const float effectiveLevel = predictedProcessedActiveRmsDb (ctx, i, plan.proposed.strips[size_t (i)]);
        float fader = roundHalf (target - effectiveLevel);
        // Headroom guard: the balance is a loudness decision, but a strip still must not arrive at its bus
        // hot enough to leave a transient nowhere to go.
        const float effectivePeak = predictedProcessedPeakDb (ctx, i, plan.proposed.strips[size_t (i)]);
        fader = std::min (fader, roundHalf (MixProfile::stripPeakCeilingDb (profile) - effectivePeak));
        // A close microphone that hears the rest of the kit is only lifted so far: past that the bleed comes
        // up with the instrument and the console preamp is the thing that is actually wrong.
        if (isDrumCloseMic (f))
        {
            // How far DLIVE is lifting this microphone digitally in total - an absolute amount, not what this
            // pass added on top of the last one. Measuring the raise against the gain that ran at the listen
            // handed the whole budget out again on every Tune Mix, because by then the gain it was meant to
            // count had become the listen's own: a close mic climbed another maxCloseMicRaiseDb each pass and
            // brought the rest of the kit up with it.
            const float digitalRaise = std::max (sp.inputGainDb, 0.0f);
            const float allowed = R.maxCloseMicRaiseDb - digitalRaise;
            if (fader > allowed)
            {
                fader = roundHalf (std::max (allowed, 0.0f));
                sp.mixItems.push_back (info (Recommendation::Kind::CaptureGain, upper (sp.name) + ": turn this microphone up at the console",
                                             "To sit where the mix wants it this close microphone needs more level than DLIVE will add to it. "
                                             "It also hears the rest of the kit, so raising it here would bring that bleed up with the instrument. "
                                             "Turn its preamp up at the console and Tune Mix again.", Confidence::High));
            }
        }
        sp.faderDb = clamp (fader, -R.maxFaderMoveDb, R.maxFaderMoveDb);
        sp.balanced = true;
    }

    // The lead vocal is the reference. If its fader could not reach the profile level (a quiet capture hits
    // the bound), everything else follows it down by the same amount so the hierarchy survives; the master's
    // loudness rule makes up the overall level afterwards.
    {
        const int lead = findHeard ([] (RoleFamily f) { return f == RoleFamily::LeadVocal; });
        if (lead >= 0 && plan.strips[size_t (lead)].balanced)
        {
            const float effectiveLevel = predictedProcessedActiveRmsDb (ctx, lead, plan.proposed.strips[size_t (lead)]);
            const float needed = roundHalf (MixProfile::mixLevelTargetDb (profile, RoleFamily::LeadVocal) - effectiveLevel);
            const float shortfall = needed - plan.strips[size_t (lead)].faderDb;   // > 0: the lead is quieter than planned
            if (std::fabs (shortfall) >= 0.5f)
            {
                for (int i = 0; i < n; ++i)
                {
                    auto& sp = plan.strips[size_t (i)];
                    if (! sp.balanced || i == lead) continue;
                    sp.faderDb = clamp (sp.faderDb - shortfall, -R.maxFaderMoveDb, R.maxFaderMoveDb);
                }
                plan.relationships.push_back (info (Recommendation::Kind::MixGain, "Whole mix follows the lead vocal " + fmtDb (-shortfall, 1),
                                                     "The lead vocal reaches the console " + num ("%.0f dB", double (shortfall)) + " short of where the mix wants it, and its fader is at its limit. "
                                                     "Everything else is lowered by the same amount so the voice stays in front; the master makes up the level. Raising the lead's preamp is the real fix.",
                                                     Confidence::High));
            }
        }
    }

    // Lead <-> backing vocals: several voices add up, the group stays behind the lead.
    {
        const int lead = findHeard ([] (RoleFamily f) { return f == RoleFamily::LeadVocal; });
        std::vector<int> backing;
        for (int i = 0; i < n; ++i)
            if (plan.strips[size_t (i)].balanced && roleFamily (plan.strips[size_t (i)].role) == RoleFamily::BackingVocal)
                backing.push_back (i);   // only voices whose fader was fitted: the group move stays absolute
        if (lead >= 0 && backing.size() >= 2)
        {
            const float leadLevel = MixProfile::mixLevelTargetDb (profile, RoleFamily::LeadVocal);
            const float group = MixProfile::mixLevelTargetDb (profile, RoleFamily::BackingVocal) + 10.0f * std::log10 (float (backing.size()));
            const float allowed = leadLevel - R.backingGroupBelowLeadDb;
            if (group > allowed)
            {
                const float drop = roundHalf (group - allowed);
                for (int i : backing)
                {
                    auto& sp = plan.strips[size_t (i)];
                    sp.faderDb = clamp (sp.faderDb - drop, -R.maxFaderMoveDb, R.maxFaderMoveDb);
                }
                plan.relationships.push_back (info (Recommendation::Kind::MixGain, "Backing vocals held " + fmtDb (-drop, 1) + " further back as a group",
                                                     std::to_string (backing.size()) + " backing voices add up to about " + fmtDb (10.0f * std::log10 (float (backing.size())), 0)
                                                     + " more than one; together they would sit level with the lead. Each is lowered so the group stays "
                                                     + num ("%.0f dB", double (R.backingGroupBelowLeadDb)) + " behind the lead vocal.", Confidence::Medium));
            }
            else
                plan.relationships.push_back (info (Recommendation::Kind::Info, "Lead vocal stays in front of the backing vocals",
                                                     "The backing group sits " + fmtDb (leadLevel - group, 0) + " under the lead at these levels; the hierarchy holds.", Confidence::Medium));
        }
    }

    // Faders are written once, after every rule that touches them, so a fader that ends where it started is not a change.
    for (int i = 0; i < n; ++i)
    {
        auto& sp = plan.strips[size_t (i)];
        if (! sp.balanced) continue;
        if (std::fabs (sp.faderDb - sp.faderBeforeDb) < 0.5f) { sp.faderDb = sp.faderBeforeDb; continue; }
        plan.proposed.strips[size_t (i)].faderDb = sp.faderDb;
        const RoleFamily f = roleFamily (sp.role);
        const float effectiveLevel = predictedProcessedActiveRmsDb (ctx, i, plan.proposed.strips[size_t (i)]);
        sp.mixItems.push_back (info (Recommendation::Kind::MixGain, upper (sp.name) + " fader " + fmtDb (sp.faderDb, 1),
                                     "Processed, this source sits around " + num ("%.0f dBFS", double (effectiveLevel)) + " while it plays; in a " + std::string (styleProfileName (profile))
                                     + " mix it sits around " + num ("%.0f dBFS", double (MixProfile::mixLevelTargetDb (profile, f)))
                                     + (f == RoleFamily::LeadVocal ? " as the reference the rest is balanced against." : " relative to the lead vocal.")
                                     + (f == RoleFamily::BackingVocal ? " The backing group is held behind the lead." : ""),
                                     Confidence::Medium));
    }

    // ---- 4. Buses and master ----
    // The buses come first (Master is last in the enum): what each bus will put out once the faders have moved and its
    // own chain has been re-fitted is predicted, and the master is planned from the sum of those predictions.
    std::array<float, int (MixBus::Count)> busOutShiftDb {};   // change of each bus's output level, listen -> plan

    // A bus is only fitted to what it actually carried. If nothing feeding it was heard playing -
    // the speech group during a song, a horn section that sat out - the listen measured an empty
    // bus, and a compressor fitted to silence sits its threshold down near the noise floor and
    // crushes the group the moment it arrives. That chain is left exactly where it is, and the
    // plan says to listen again while that group plays. This is the bus-level form of the rule
    // that already leaves a speech microphone alone when it was only picking up the band.
    std::array<bool, int (MixBus::Count)> busPlayed {};
    for (int i = 0; i < n; ++i)
    {
        const auto& sp = plan.strips[size_t (i)];
        if (sp.heard && ! sp.bleedOnly) busPlayed[size_t (ctx.graph.strips[size_t (i)].bus)] = true;
    }
    busPlayed[size_t (MixBus::Master)] = true;      // the master carries whatever the buses carried

    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        auto& bp = plan.buses[size_t (b)];
        bp.bus = MixBus (b);
        bp.used = ctx.graph.busUsed[size_t (b)];
        const auto& a = ctx.capture.buses[size_t (b)];
        if (! bp.used || ! a.valid) continue;
        if (! busPlayed[size_t (b)])
        {
            plan.notes.push_back (std::string (mixBusName (MixBus (b)))
                                  + ": nothing on this group played during the listen, so its chain was left alone."
                                    " Tune Mix again while it does.");
            continue;
        }
        const bool isMaster = MixBus (b) == MixBus::Master;
        const ChannelRole role = busRole (MixBus (b), ctx.session.purpose);
        TuneContext tc;
        tc.analysis = a;
        // REFERENCE MIX. A reference is a finished stereo record, so the only thing in this
        // plan it can honestly be compared with is the master: it says what the whole mix
        // should sound like, not what one group or one microphone should. The aim moves, the
        // strategy does not - every bound, sentence and the idempotency rule come along
        // unchanged, and the match is computed from the reference and the profile, never from
        // where the master happens to sit, so re-tuning the same listen lands in the same place.
        SourceTargets refTargets;
        if (isMaster && ctx.reference.valid)
        {
            refTargets = Reference::targets (Profiles::targets (profile, role), ctx.reference, a, profile, plan.reference);
            tc.targetsOverride = &refTargets;
        }
        // What the bus will receive once the faders have moved (the master: once the buses have moved), from the
        // processed levels. A bus chain that then works harder (its compressor sees more level) puts out less than
        // the shift alone says; that part is predicted from the compressor curve, like the strips.
        float shift = 0.0f;
        if (! isMaster)
            shift = faderShiftDb (ctx, ctx.atCapture, plan.proposed, MixBus (b), false);
        else
        {
            double before = 0.0, after = 0.0;
            for (int o = 0; o < int (MixBus::Master); ++o)
            {
                const auto& oa = ctx.capture.buses[size_t (o)];
                if (! ctx.graph.busUsed[size_t (o)] || ! oa.valid || oa.rmsDb <= -100.0f) continue;
                before += std::pow (10.0, double (oa.rmsDb) / 10.0);
                after  += std::pow (10.0, double (oa.rmsDb + busOutShiftDb[size_t (o)]) / 10.0);
            }
            if (before > 0.0 && after > 0.0) shift = clamp (float (10.0 * std::log10 (after / before)), -30.0f, 30.0f);
        }
        shiftLevels (tc.analysis, shift);
        bp.predictedInShiftDb = shift;
        tc.role = role;
        tc.profile = profile;
        tc.current = ctx.current.buses[size_t (b)].channel;
        tc.hasOutput = false;
        if (isMaster && ctx.capture.masterOutput.valid && ctx.capture.masterOutput.loudnessLufs > -100.0f)
        {
            // The loudness rule predicts the output as input loudness + output trim. What actually left the master
            // during the listen is known, so the input loudness is set to make that prediction exact: the
            // compression and limiting on the way are then accounted for, and the number is still computed from
            // the listen and the trim at the listen, never from the current value. The master compressor the plan
            // chooses works harder than the one that ran (more level, often a lower threshold); that extra reduction
            // is not in the measurement, so the master is tuned twice: once to learn its compressor, then again with
            // the loudness that compressor will leave. On the same listen the compressor is unchanged and the second
            // pass equals the first.
            const float measuredIn = ctx.capture.masterOutput.loudnessLufs - ctx.atCapture.master().channel.outputTrimDb + shift;
            tc.analysis.loudnessLufs = measuredIn;
            tc.analysis.truePeakDb = ctx.capture.masterOutput.truePeakDb + shift;
            const TuneResult first = TuneEngine::tune (tc);
            const ChannelParameters& chosen = first.valid ? first.proposed : ctx.current.master().channel;
            const float compDelta = compressorAverageDeltaDb (chosen, ctx.atCapture.master().channel, a.rmsDb, a.peakDb, shift, R.compDetectorCrestShareBus);
            tc.analysis.loudnessLufs = measuredIn + compDelta;
            tc.analysis.truePeakDb += compDelta;
        }
        bp.tune = TuneEngine::tune (tc);
        if (bp.tune.valid) plan.proposed.buses[size_t (b)].channel = bp.tune.proposed;
        if (! isMaster)
            bp.predictedOutShiftDb = busOutShiftDb[size_t (b)] = shift
                                      + compressorAverageDeltaDb (plan.proposed.buses[size_t (b)].channel, ctx.atCapture.buses[size_t (b)].channel, a.rmsDb, a.peakDb, shift, R.compDetectorCrestShareBus)
                                      + (plan.proposed.buses[size_t (b)].faderDb - ctx.atCapture.buses[size_t (b)].faderDb);
    }

    // ---- 5. Sum up ----
    for (int i = 0; i < n; ++i)
        plan.parametersChanged += int (diffParameters (plan.before.strips[size_t (i)].channel, plan.proposed.strips[size_t (i)].channel).size());
    for (int b = 0; b < int (MixBus::Count); ++b)
        plan.parametersChanged += int (diffParameters (plan.before.buses[size_t (b)].channel, plan.proposed.buses[size_t (b)].channel).size());
    plan.fadersChanged = 0;
    plan.sendsChanged = 0;
    plan.gainsChanged = 0;
    for (int i = 0; i < n; ++i)
    {
        if (std::fabs (plan.before.strips[size_t (i)].faderDb - plan.proposed.strips[size_t (i)].faderDb) >= 0.01f) ++plan.fadersChanged;
        if (std::fabs (plan.before.strips[size_t (i)].inputGainDb - plan.proposed.strips[size_t (i)].inputGainDb) >= 0.01f) ++plan.gainsChanged;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (std::fabs (plan.before.strips[size_t (i)].sendDb[size_t (f)] - plan.proposed.strips[size_t (i)].sendDb[size_t (f)]) >= 0.01f) ++plan.sendsChanged;
    }

    plan.valid = true;
    plan.noChangeRequired = plan.parametersChanged == 0 && plan.fadersChanged == 0 && plan.sendsChanged == 0 && plan.gainsChanged == 0;
    plan.headline = plan.noChangeRequired ? "MIX: NO CHANGE REQUIRED" : "MIX TUNED";

    // Gain staging is the first move in any mix, so it is the first thing the plan says.
    // DLIVE set the digital input gain itself; what is left is the console preamp, and
    // the note names the inputs so nobody has to hunt for them.
    {
        std::vector<std::string> wantPreamp;
        for (const auto& sp : plan.strips)
        {
            if (! sp.heard || sp.bleedOnly || ! sp.tune.valid) continue;
            const bool health = sp.tune.report.inputHealth != "Healthy" && ! sp.tune.report.inputHealth.empty();
            // DLIVE makes a quiet input work digitally, but a big digital raise means the preamp
            // itself is low - and a digital raise lifts the preamp's noise with the source.
            const bool digital = std::fabs (sp.inputGainDb) >= R.digitalGainAdviceDb;
            if (! health && ! digital) continue;
            ++plan.stripsWantPreamp;
            if (wantPreamp.size() < 4) wantPreamp.push_back (upper (sp.name));
        }
        if (plan.stripsWantPreamp > 0)
        {
            std::string names;
            for (size_t k = 0; k < wantPreamp.size(); ++k) names += (k == 0 ? "" : ", ") + wantPreamp[k];
            if (plan.stripsWantPreamp > int (wantPreamp.size())) names += " and " + std::to_string (plan.stripsWantPreamp - int (wantPreamp.size())) + " more";
            plan.notes.push_back (std::string ("Gain staging first: turn the preamp for ") + names
                                  + (plan.stripsWantPreamp == 1 ? " at the console, then RE-TUNE." : " at the console, then RE-TUNE."));
        }
    }

    plan.notes.push_back (std::to_string (plan.stripsHeard) + " of " + std::to_string (n) + " sources heard.");
    if (plan.stripsFaint > 0)
        plan.notes.push_back (plan.stripsFaint == 1 ? "1 input barely reached DLIVE: check it." : std::to_string (plan.stripsFaint) + " inputs barely reached DLIVE: check them.");
    int tuned = 0;
    for (const auto& sp : plan.strips) if (sp.tune.valid && ! sp.tune.noChangeRequired) ++tuned;
    if (tuned > 0) plan.notes.push_back (std::to_string (tuned) + " sources shaped individually (" + std::to_string (plan.parametersChanged) + " settings).");
    if (plan.gainsChanged > 0) plan.notes.push_back (std::to_string (plan.gainsChanged) + " input gains set so the processing works at the right level.");
    if (plan.fadersChanged > 0) plan.notes.push_back (std::to_string (plan.fadersChanged) + " levels balanced against the lead vocal.");
    int relationshipMoves = 0;
    for (const auto& r : plan.relationships) if (! r.changes.empty() || r.kind == Recommendation::Kind::MixGain) ++relationshipMoves;
    if (relationshipMoves > 0) plan.notes.push_back (relationshipMoves == 1 ? "1 decision made in mix context (sources working together)."
                                                                            : std::to_string (relationshipMoves) + " decisions made in mix context (sources working together).");
    const auto& master = plan.buses[size_t (MixBus::Master)];
    if (master.tune.valid)
    {
        for (const auto& item : master.tune.report.items)
            if (item.kind == Recommendation::Kind::MixGain) { plan.notes.push_back ("Master: " + item.what + "."); break; }
    }
    if (plan.reference.used)
    {
        plan.notes.push_back ("Aimed at " + plan.reference.name + ": the master's tone, image and density follow it. "
                              "Who is loud in the mix, and how loud the mix is delivered, do not.");
        for (const auto& aim : plan.reference.aims) plan.notes.push_back (aim);
    }
    return plan;
}

// ---- TUNE CHANNEL ----
// The plan above, narrowed to one source. The listen and the decisions are the same ones
// TUNE MIX makes - a channel is never tuned by rules of its own - but only this strip is
// applied, so `proposed` is `before` everywhere else. That also makes `proposed` exactly
// what the mix will be once it is kept, which is what the Inspector reads as "what DINE
// set". The buses and the master are left alone: where one source sits is not a master
// decision. Every other strip keeps what the listen measured about it (the gain-staging
// advice and the mix health are about the listen, not about this channel), with the moves
// this plan does not make taken back out.
MixPlan channelOnly (const MixPlan& full, int strip, StyleProfileId profile)
{
    MixPlan out = full;
    out.proposed = full.before;
    out.relationships.clear();
    out.notes.clear();
    // A channel tune leaves the master where it is, so whatever a reference aimed the master
    // at is not part of what this plan proposes. Saying otherwise would put a claim in REVIEW
    // CHANGES that the applied mix does not contain.
    out.reference = ReferenceMatch {};
    out.parametersChanged = out.fadersChanged = out.sendsChanged = out.gainsChanged = 0;
    out.stripsWantPreamp = 0;
    out.noChangeRequired = true;

    const size_t i = size_t (strip);
    if (! full.valid || strip < 0 || strip >= int (full.strips.size()) || strip >= full.before.numStrips)
    {
        out.headline = "CHANNEL: NOT IN THIS MIX";
        out.notes.push_back ("That channel is not part of this mix any more. Check the assignments, then tune it again.");
        return out;
    }

    for (auto& sp : out.strips)
    {
        if (sp.strip == strip) continue;
        sp.faderDb = sp.faderBeforeDb;
        sp.inputGainDb = sp.inputGainBeforeDb;
        sp.balanced = false;
    }

    const auto& sp = out.strips[i];
    const std::string NAME = upper (sp.name);
    if (sp.faint)
    {
        out.headline = NAME + ": CHECK THIS INPUT";
        for (const auto& r : sp.mixItems) if (r.kind == Recommendation::Kind::Info) { out.notes.push_back (r.why); break; }
        if (out.notes.empty())
            out.notes.push_back ("Its loudest moment during the listen was too quiet to be a source that is really playing. "
                                 "Check the microphone, the cable and the preamp, then tune it again.");
        return out;
    }
    if (! sp.heard)
    {
        out.headline = NAME + " WAS NOT HEARD";
        out.notes.push_back ("Nothing played on this input during the listen, so nothing was decided about it. "
                             "Tune it again while it plays.");
        return out;
    }

    out.proposed.strips[i] = full.proposed.strips[i];
    out.relationships = sp.mixItems;
    out.parametersChanged = int (diffParameters (out.before.strips[i].channel, out.proposed.strips[i].channel).size());
    if (std::fabs (out.before.strips[i].faderDb - out.proposed.strips[i].faderDb) >= 0.01f) out.fadersChanged = 1;
    if (std::fabs (out.before.strips[i].inputGainDb - out.proposed.strips[i].inputGainDb) >= 0.01f) out.gainsChanged = 1;
    for (int f = 0; f < int (FxSlot::Count); ++f)
        if (std::fabs (out.before.strips[i].sendDb[size_t (f)] - out.proposed.strips[i].sendDb[size_t (f)]) >= 0.01f) ++out.sendsChanged;

    out.noChangeRequired = out.parametersChanged == 0 && out.fadersChanged == 0 && out.sendsChanged == 0 && out.gainsChanged == 0;
    out.headline = out.noChangeRequired ? NAME + ": NO CHANGE REQUIRED" : NAME + " TUNED";

    // Gain staging first, on this channel too: DLIVE set the digital gain, the preamp is the user's move.
    if (sp.tune.valid && ! sp.bleedOnly)
    {
        const auto& R = MixProfile::relationships (profile);
        const bool health = sp.tune.report.inputHealth != "Healthy" && ! sp.tune.report.inputHealth.empty();
        if (health || std::fabs (sp.inputGainDb) >= R.digitalGainAdviceDb)
        {
            ++out.stripsWantPreamp;
            out.notes.push_back ("Gain staging first: turn the preamp for " + NAME + " at the console, then tune it again.");
        }
    }
    if (sp.bleedOnly)
        out.notes.push_back (NAME + " was heard as spill while the lead sang, so its level was left alone. "
                             "Tune it again when it is the source that is playing.");
    if (out.parametersChanged > 0)
        out.notes.push_back (std::to_string (out.parametersChanged) + (out.parametersChanged == 1 ? " setting shaped from what it played." : " settings shaped from what it played."));
    // The gain and the level are only mentioned here when the decision does not already
    // carry its own sentence: the same move said twice reads as two moves.
    auto explained = [&sp] (Recommendation::Kind kind)
    {
        for (const auto& r : sp.mixItems) if (r.kind == kind) return true;
        return false;
    };
    if (out.gainsChanged > 0 && ! explained (Recommendation::Kind::CaptureGain))
        out.notes.push_back ("Input gain " + fmtDb (out.proposed.strips[i].inputGainDb, 1) + " so the processing works at the right level.");
    if (out.fadersChanged > 0 && ! explained (Recommendation::Kind::MixGain))
        out.notes.push_back ("Level " + fmtDb (out.proposed.strips[i].faderDb, 1) + " against the rest of the mix (it was " + fmtDb (out.before.strips[i].faderDb, 1) + ").");
    if (out.noChangeRequired)
        out.notes.push_back ("This channel is already where the profile wants it. Nothing was changed.");
    return out;
}

} // namespace MixPlanner
} // namespace livemix
