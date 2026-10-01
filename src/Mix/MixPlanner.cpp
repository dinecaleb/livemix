#include "MixPlanner.h"
#include "DSP/SampleBank.h"
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
    // Where a band of a source will sit once the balance has placed it: the level the fader is
    // actually going to land it at, plus that band's energy relative to the source.
    //
    // This used to read the profile's *target* instead, which is the level the mix would like
    // the source at rather than the one it will get. A voice that could not reach its number -
    // too far off the microphone, held back because the microphone mostly hears the stage,
    // stopped at the top of the scale - was still treated as if it had, so the keys had a
    // pocket cut into them to make room for a voice that was not there, and the bass kept its
    // sub because a kick that never arrived was supposed to own it. What the ear hears is what
    // the faders do; so is this.
    float bandAtMixDb (float fittedLevelDb, const AnalysisResult& a, Band b)
    {
        return fittedLevelDb + a.bandEnergyDb[size_t (b)];
    }

    // The close microphones on a drum kit: each one hears every other drum in the room, so what a fader
    // does to one of them it also does to the bleed. Overheads and room microphones are meant to hear the
    // whole kit, and everything else is not sharing a stand with another instrument.
    bool isDrumCloseMic (RoleFamily f)
    {
        return f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom || f == RoleFamily::HiHat;
    }

    // A microphone that has quiet between the sounds it is there for - a voice between phrases, a drum
    // between hits, a horn between lines - and hears the stage in that quiet. An overhead or a room
    // microphone is meant to hear the room and is exempt; so is anything played in held chords, where
    // the floor of the capture *is* the chord and the rule would read the instrument as its own spill
    // (an organ, a pad, an electric piano, a DI of any kind).
    //
    // A saxophone, an acoustic guitar, a guitar cabinet and a piano lid all belong here: they play in
    // phrases with real gaps, they sit on a stage next to a drum kit, and lifting one lifts the kit
    // with it exactly as it does on a vocal microphone. A DI on the same role costs nothing to include
    // - its floor is far under everything, so the rule never reaches it.
    bool isSpillProneMic (RoleFamily f)
    {
        return isDrumCloseMic (f) || f == RoleFamily::LeadVocal || f == RoleFamily::BackingVocal
            || f == RoleFamily::Choir || f == RoleFamily::Speech
            || f == RoleFamily::Saxophone || f == RoleFamily::AcousticGuitar
            || f == RoleFamily::ElectricGuitar || f == RoleFamily::Piano;
    }

    // A voice: a source whose loudest moments are consonants rather than impacts, and whose
    // level in the mix is the sustained part underneath them.
    bool isSustainedVoice (RoleFamily f)
    {
        return f == RoleFamily::Speech || f == RoleFamily::LeadVocal || f == RoleFamily::BackingVocal || f == RoleFamily::Choir;
    }

    // A microphone that is there for the building rather than for a source in front of it.
    // What it hears when nobody is playing is the room, which is the point of it.
    bool hearsTheBuilding (RoleFamily f)
    {
        return f == RoleFamily::Room || f == RoleFamily::Overhead || f == RoleFamily::Ambience;
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
        if (a.loudnessGatedLufs > -100.0f) a.loudnessGatedLufs += db;
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

// What the sample stage does to a peak, dB: the crossfade `mic * (1 - blend) + sample * blend`,
// with the sample's own peak where TUNE put it. `tune::sampledHitDb` is the same arithmetic the
// tuner fits the compressor with - one model, so the fader and the threshold cannot disagree.
float sampleStageShiftDb (const ChannelParameters& after, const ChannelParameters& atCapture, float micPeakDb)
{
    if (! after.replaceEnabled && ! atCapture.replaceEnabled) return 0.0f;
    return tune::sampledHitDb (after, micPeakDb) - tune::sampledHitDb (atCapture, micPeakDb);
}

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
    // Switching the sample stage on changes what the rest of the chain is handed - at a full
    // blend it replaces the microphone outright - so the prediction starts from the processed
    // peak the listen measured plus that change, not from the microphone alone.
    const float sampleShift = sampleStageShiftDb (strip.channel, atCapture.channel, a.peakDb + gainDelta);
    // The raw tap sits after the input gain, so the measured raw peak is the chain input at the listen. A compressor
    // with attack `a` has reached 1 - exp(-rise / a) of its static reduction when the peak arrives.
    const float shareAfter = 1.0f - std::exp (-rise / std::max (strip.channel.compAttackMs, 0.1f));
    const float shareBefore = 1.0f - std::exp (-rise / std::max (atCapture.channel.compAttackMs, 0.1f));
    return o.peakDb + gainDelta + sampleShift
         + compressorEffectDb (strip.channel, a.peakDb + gainDelta + sampleShift, shareAfter)
         - compressorEffectDb (atCapture.channel, a.peakDb, shareBefore);
}

float predictedProcessedRmsDb (const MixPlanContext& ctx, int i, const StripParameters& strip)
{
    const auto& o = ctx.capture.processed[size_t (i)];
    const auto& a = ctx.capture.strips[size_t (i)];
    const auto& atCapture = ctx.atCapture.strips[size_t (i)];
    const float gainDelta = strip.inputGainDb - atCapture.inputGainDb;
    const float sampleShift = sampleStageShiftDb (strip.channel, atCapture.channel, a.peakDb + gainDelta);
    return o.rmsDb + gainDelta + sampleShift
         + compressorAverageDeltaDb (strip.channel, atCapture.channel, a.rmsDb, a.peakDb, gainDelta + sampleShift,
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
    // A sampled kit: any kick, snare or tom that will be carried by a sample once this tune has
    // run - already on, or about to be. Read once for every strip, so the hi-hat and the
    // overheads are tuned for the kit they are actually going to be in.
    //
    // `tune::willSample`, not `replaceEnabled`: TUNE switches the stage on itself now, so asking
    // what is on *before* the tune would tune the hat for one kit and leave it in another - and
    // the second pass over the same listen would then disagree with the first, which is the one
    // thing a tune may never do. A sampled hi-hat does not make a sampled kit; the rules this
    // feeds are about the kick and the snare being carried, which is what changes what every
    // other drum microphone hears.
    bool kitSampled = false;
    for (int k = 0; k < ctx.graph.numStrips() && k < ctx.current.numStrips && k < int (ctx.capture.strips.size()); ++k)
    {
        const auto kRole = ctx.graph.strips[size_t (k)].role;
        const RoleFamily kf = roleFamily (kRole);
        if (kf == RoleFamily::HiHat) continue;
        if (tune::willSample (kRole, ctx.capture.strips[size_t (k)], Profiles::targets (profile, kRole),
                              ctx.current.strips[size_t (k)].channel)) kitSampled = true;
    }
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

        // A signal with no performance in it: never quiet, and its loudest moments barely above
        // its own average. A held chord or a pad measures like that too, so on its own this is
        // not enough to say anything about one input - but when it is true of *everything* the
        // listen heard, what DLIVE was played is a tone, a ring or a fault, not a band. The
        // refusal is below, once every input has been looked at.
        if (sp.heard)
        {
            const auto& sa = ctx.capture.strips[size_t (i)];
            sp.stuck = sa.crestFactorDb < R.stuckSourceCrestDb && sa.silencePercent < R.stuckSourceSilencePercent;
            if (sp.stuck) ++plan.stripsStuck;
        }
        if (sp.heard) ++plan.stripsHeard;

        TuneContext tc;
        tc.analysis = ctx.capture.strips[size_t (i)];
        tc.role = route.role;
        tc.profile = profile;
        tc.current = ctx.current.strips[size_t (i)].channel;
        tc.hasOutput = false;   // the mix balances with faders below, not with the strip's output trim
        tc.hasSampleStage = sampleReplacementAppropriate (roleFamily (route.role));
        tc.sampled = tc.hasSampleStage && tc.current.replaceEnabled;
        tc.kitSampled = kitSampled;
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
        plan.proposed = plan.before;          // a refused listen changes nothing, not even a chain
        plan.noChangeRequired = true;
        plan.headline = "MIX: NO SIGNAL";
        plan.notes.push_back ("No input carried a usable signal during the listen. Have the band play and Tune Mix again.");
        plan.valid = true;
        return plan;
    }

    // Was that a performance at all? Two ways of asking, and a mix - above all a master -
    // fitted to the answer "no" is a mix fitted to a fault, so it is refused with the reason
    // rather than applied and explained afterwards.
    //
    // (1) Everything the listen heard was steady. One held chord measures like that, which is
    //     why a single input never decides this; every input doing it at once does not happen
    //     on a stage. QUEENSVIEW, take 002 at 450 s: a kick channel stuck at -2.5 dBFS with a
    //     2.5 dB crest was the only thing "playing", and the master went 7.5 dB up to meet it.
    // (2) What arrived at the mix never moved. A band and a voice both have peaks well clear
    //     of their own average; a tone, a feedback ring and a converter fault do not.
    const bool everythingSteady = plan.stripsStuck > 0 && plan.stripsStuck == plan.stripsHeard;
    const auto& mo = ctx.capture.masterOutput;
    const bool mixNeverMoved = mo.valid && mo.rmsDb > -80.0f && mo.crestFactorDb < R.minMasterCrestDb;
    if (everythingSteady || mixNeverMoved)
    {
        plan.proposed = plan.before;          // ... including the chains the strips were given above
        plan.noChangeRequired = true;
        plan.headline = "MIX: THAT WAS NOT A PERFORMANCE";
        plan.notes.push_back (everythingSteady
            ? "Every input DLIVE could hear carried a steady signal rather than somebody playing: never quiet, and never far above "
              "its own average. That is a tone, a feedback ring or a fault, not a band. Nothing was changed."
            : "What reached the mix during the listen never moved - its loudest moments sat only "
              + num ("%.0f dB", double (mo.crestFactorDb)) + " above its own average. Music and speech are never that steady, so "
              "something is feeding DLIVE a tone, a feedback ring or a fault. Nothing was changed.");
        for (const auto& sp : plan.strips)
            if (sp.stuck) plan.notes.push_back (upper (sp.name) + ": steady all the way through the listen; check what is patched to it.");
        plan.notes.push_back ("Find it, then Tune Mix again while the band plays.");
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

    // A backing-vocal microphone nobody sang into is the same case from the other side: what it heard
    // is the stage, and lifting it to the backing-vocal level lifts the stage. A voice has gaps that a
    // stage does not, so the test is how far its loudest moments stood above its floor - read from the
    // raw capture, middle third of three, so it is the same answer however often the listen is planned.
    {
        for (int i = 0; i < n; ++i)
        {
            auto& sp = plan.strips[size_t (i)];
            const auto& a = ctx.capture.strips[size_t (i)];
            if (! sp.heard || sp.bleedOnly || roleFamily (sp.role) != RoleFamily::BackingVocal) continue;
            if (a.noiseFloorDb <= -119.0f || a.dynamicRangeDb >= R.minSungRangeDb) continue;
            sp.bleedOnly = true;
            plan.proposed.strips[size_t (i)].inputGainDb = ctx.current.strips[size_t (i)].inputGainDb;
            sp.inputGainDb = sp.inputGainBeforeDb;
            {
                TuneContext tc;
                tc.analysis = a;
                shiftLevels (tc.analysis, ctx.current.strips[size_t (i)].inputGainDb - ctx.atCapture.strips[size_t (i)].inputGainDb);
                tc.role = sp.role; tc.profile = profile; tc.current = ctx.current.strips[size_t (i)].channel; tc.hasOutput = false;
                sp.tune = TuneEngine::tune (tc);
                if (sp.tune.valid) plan.proposed.strips[size_t (i)].channel = sp.tune.proposed;
            }
            sp.mixItems.clear();
            sp.mixItems.push_back (info (Recommendation::Kind::Info, upper (sp.name) + ": nobody sang into it",
                                         "Its loudest moments stood only " + num ("%.0f dB", double (a.dynamicRangeDb))
                                         + " above what it heard the rest of the time - that is the stage, not a voice, which has gaps "
                                         "between its phrases. Lifting it would bring the stage up through it, so its level and gain were "
                                         "left alone. Tune Mix again while somebody sings into it.", Confidence::High));
        }
    }

    // ... and the other half of the same question. A listen where the only source playing is
    // the spoken word is a sermon, and the microphones that are left open across a stage
    // during one - the drum room, the overheads - are not the band playing. They are the PA
    // and the building coming back, and balancing them as sources puts the preacher's own
    // voice into the mix a second time, late. A congregation microphone is the exception on
    // purpose: under a sermon the room *is* what a stream expects to hear.
    bool sermonListen = false;
    {
        bool speechPlayed = false, bandPlayed = false;
        for (int i = 0; i < n; ++i)
        {
            const auto& sp = plan.strips[size_t (i)];
            if (! sp.heard || sp.bleedOnly) continue;
            const RoleFamily f = roleFamily (sp.role);
            if (f == RoleFamily::Speech) speechPlayed = true;
            else if (! hearsTheBuilding (f)) bandPlayed = true;
        }
        sermonListen = speechPlayed && ! bandPlayed;
    }
    if (sermonListen)
        for (int i = 0; i < n; ++i)
        {
            auto& sp = plan.strips[size_t (i)];
            const RoleFamily f = roleFamily (sp.role);
            if (! sp.heard || sp.bleedOnly || ! (f == RoleFamily::Room || f == RoleFamily::Overhead)) continue;
            sp.bleedOnly = true;
            plan.proposed.strips[size_t (i)].inputGainDb = ctx.current.strips[size_t (i)].inputGainDb;
            sp.inputGainDb = sp.inputGainBeforeDb;
            sp.mixItems.clear();
            sp.mixItems.push_back (info (Recommendation::Kind::Info, upper (sp.name) + ": heard as spill while the pastor spoke",
                                         "There is nothing on the kit during a sermon, so what this microphone picked up is the room and the PA - "
                                         "the preacher's own voice arriving late. Its level and gain were left alone. Tune Mix again while the band "
                                         "plays to set it.", Confidence::High));
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

    // Where one source's fader lands: the whole of the balance decision for a strip, in one
    // place, because it has to be answered twice. The relationships below need to know what
    // level each source is going to end up at before they decide who is masking whom - a cut
    // made for a voice that never reaches the mix is a cut nobody asked for - and the balance
    // proper then answers it again over the chains those relationships changed. `explain` is
    // off for the first answer: it is arithmetic, not a decision anybody should read about.
    auto balanceable = [&] (int i) -> bool
    {
        const auto& sp = plan.strips[size_t (i)];
        const auto& o = i < int (ctx.capture.processed.size()) ? ctx.capture.processed[size_t (i)] : OutputStats {};
        const auto& a = ctx.capture.strips[size_t (i)];
        return ! sp.faint && sp.heard && ! sp.bleedOnly && o.valid && o.peakDb > -60.0f && a.silencePercent <= 60.0f;
    };
    auto fitFader = [&] (int i, bool explain) -> float
    {
        auto& sp = plan.strips[size_t (i)];
        const auto& a = ctx.capture.strips[size_t (i)];
    const RoleFamily f = roleFamily (sp.role);
    const float target = MixProfile::mixLevelTargetDb (profile, f);
    // How loud this source will be while it plays once its new gain and chain run: predicted from the
    // listen, so the first Tune Mix lands the balance instead of needing a second listen to hear the
    // processing it just chose. Loudness, not peak - see mixLevelTargetDb.
    const float effectiveLevel = predictedProcessedActiveRmsDb (ctx, i, plan.proposed.strips[size_t (i)]);
    float fader = roundHalf (target - effectiveLevel);
    // Headroom guard: the balance is a loudness decision, but a strip still must not arrive at its bus
    // hot enough to leave a transient nowhere to go. What "loudest" means is the source's own
    // (MixProfile::stripPeakCeilingDb): a stick hit is the sound and needs room, a consonant is not.
    float effectivePeak = predictedProcessedPeakDb (ctx, i, plan.proposed.strips[size_t (i)]);
    // On a voice the guard reads the musical peak rather than the loudest sample: a desk
    // export routinely carries one click 10 dB above anything the speaker said, and a
    // voice held down by a click is a voice nobody can hear.
    if (isSustainedVoice (f) && a.musicalPeakDb > -119.0f && a.peakDb > a.musicalPeakDb)
        effectivePeak -= a.peakDb - a.musicalPeakDb;
    fader = std::min (fader, roundHalf (MixProfile::stripPeakCeilingDb (profile, f) - effectivePeak));
    // A close microphone that hears the rest of the kit is only lifted so far: past that the bleed comes
    // up with the instrument and the console preamp is the thing that is actually wrong.
    if (isDrumCloseMic (f))
    {
        // How far DLIVE is lifting this microphone digitally in total - the gain and the fader together, as
        // an absolute amount, not what this pass added on top of the last one. Measuring the raise against
        // the gain that ran at the listen handed the whole budget out again on every Tune Mix, because by
        // then the gain it was meant to count had become the listen's own: a close mic climbed another
        // maxCloseMicRaiseDb each pass and brought the rest of the kit up with it. The gain counts whichever
        // way it went: a hot hi-hat pulled down 12 dB for its processing and lifted 17 dB on the fader is
        // lifted 5 dB, and the bleed with it - not 17.
        const float allowed = R.maxCloseMicRaiseDb - sp.inputGainDb;
        if (fader > allowed)
        {
            fader = roundHalf (std::max (allowed, 0.0f));
            if (explain) sp.mixItems.push_back (info (Recommendation::Kind::CaptureGain, upper (sp.name) + ": turn this microphone up at the console",
                                         "To sit where the mix wants it this close microphone needs more level than DLIVE will add to it. "
                                         "It also hears the rest of the kit, so raising it here would bring that bleed up with the instrument. "
                                         "Turn its preamp up at the console and Tune Mix again.", Confidence::High));
        }
    }
    // Any microphone on a stage hears the stage between the sounds it is there for. A voice or a close drum
    // microphone is lifted only until what it hears between phrases would land R.spillBelowTargetDb under the
    // level the mix wants it at; past that the lift is the rest of the band arriving through the wrong
    // microphone - which is how a barely-used vocal microphone, lifted 30 dB to reach the vocal level, became
    // the loudest cymbals in the mix. Measured from the listen: the floor was heard at the listen's gain, the
    // lift is what the plan adds on top, so planning again on the same listen lands in the same place. Only
    // ever a limit on a lift: a microphone already at its level, or being brought down, is left to the rules above.
    if (isSpillProneMic (f) && a.noiseFloorDb > -119.0f)
    {
        const float gainDelta = sp.inputGainDb - ctx.atCapture.strips[size_t (i)].inputGainDb;
        const float lift = gainDelta + fader;
        // A speech microphone has its own, looser number: between a preacher's phrases it hears
        // the room, not the band, and the room under a sermon is what a stream expects to hear.
        // Holding it to the singer's rule is what left the pastor a few dB under the mix.
        const float spillBelow = f == RoleFamily::Speech ? R.speechSpillBelowTargetDb : R.spillBelowTargetDb;
        const float allowedLift = (target - spillBelow) - a.noiseFloorDb;
        if (lift > 0.0f && lift > allowedLift)
        {
            const float kept = std::max (allowedLift, 0.0f);
            fader = roundHalf (kept - gainDelta);
            if (explain) sp.spillLimited = true;
            if (explain) sp.mixItems.push_back (info (Recommendation::Kind::CaptureGain, upper (sp.name) + ": mostly hears the stage",
                                         "Between the sounds it is there for, this microphone picks up the rest of the stage at "
                                         + num ("%.0f dBFS", double (a.noiseFloorDb - ctx.atCapture.strips[size_t (i)].inputGainDb))
                                         + " at the device - only " + num ("%.0f dB", double (std::max (a.activeRmsDb - a.noiseFloorDb, 0.0f)))
                                         + " under what it hears while the source plays. Lifting it to the level the mix wants would bring the "
                                         "stage up with it - the cymbals through a vocal microphone are the usual result - so it is lifted "
                                         + num ("%.0f dB", double (kept)) + " and left there. Get the source closer to the microphone (a preamp "
                                         "raises the stage with it), or keep it muted while nobody is using it, and Tune Mix again.",
                                         Confidence::High));
        }
    }
        return clamp (fader, -R.maxFaderMoveDb, R.maxFaderMoveDb);
    };
    // The level every source is predicted to sit at once the balance has placed it, which is
    // what the relationships are measured in.
    std::vector<float> fittedLevelDb (size_t (n), -120.0f);
    for (int i = 0; i < n; ++i)
        if (balanceable (i))
            fittedLevelDb[size_t (i)] = predictedProcessedActiveRmsDb (ctx, i, plan.proposed.strips[size_t (i)]) + fitFader (i, false);

    // ---- The focal source ----
    // The one thing the mix is built around: the lead singer during a song, the pastor during
    // a sermon, and whoever the engineer has pinned whenever they disagree with that. Every
    // rule below that says "the lead" means this one source - the pocket the music makes, the
    // hierarchy the backing voices sit under, the reference the whole balance follows down -
    // so there is one answer to the question and not one per rule.
    //
    // Unpinned, it is the lead microphone somebody is really singing into: the one whose own
    // level stands furthest above what it hears between phrases. The loudest lead is the wrong
    // answer when a spare microphone is lying open on a monitor wedge, which is how a mix ends
    // up built around a stand.
    const int focal = [&] () -> int
    {
        const int pinned = ctx.session.focusInput();
        if (pinned >= 0 && pinned < n && plan.strips[size_t (pinned)].heard && ! plan.strips[size_t (pinned)].bleedOnly)
            return pinned;
        int best = -1;
        float bestSeparation = -1.0e9f;
        for (int i = 0; i < n; ++i)
        {
            const auto& sp = plan.strips[size_t (i)];
            if (! sp.heard || sp.bleedOnly || roleFamily (sp.role) != RoleFamily::LeadVocal) continue;
            const auto& a = ctx.capture.strips[size_t (i)];
            const float separation = a.activeRmsDb - a.noiseFloorDb;
            if (separation > bestSeparation) { bestSeparation = separation; best = i; }
        }
        return best;
    }();
    if (focal >= 0 && ctx.session.focusInput() == focal && ctx.session.focusInput() >= 0)
        plan.relationships.push_back (info (Recommendation::Kind::Info, upper (plan.strips[size_t (focal)].name) + " is what this mix is built around",
                                             "You pinned it, so it is the reference: every level is set against it, the music makes room for it rather than "
                                             "the other way round, and nothing is held back to make room for anything else.", Confidence::High));

    // ---- 2. Relationships ----
    // Kick <-> bass: who owns the sub.
    {
        const int kick = findHeard ([] (RoleFamily f) { return f == RoleFamily::Kick; });
        const int bass = findHeard ([] (RoleFamily f) { return f == RoleFamily::ElectricBass || f == RoleFamily::SynthBass; });
        if (kick >= 0 && bass >= 0)
        {
            const auto& ka = ctx.capture.strips[size_t (kick)];
            const auto& ba = ctx.capture.strips[size_t (bass)];
            const float overlap = bandAtMixDb (fittedLevelDb[size_t (bass)], ba, Band::Sub) - bandAtMixDb (fittedLevelDb[size_t (kick)], ka, Band::Sub);
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
        const int lead = focal;
        if (lead >= 0)
        {
            const auto& la = ctx.capture.strips[size_t (lead)];
            const float leadPresence = bandAtMixDb (fittedLevelDb[size_t (lead)], la, Band::UpperMid);
            for (int i = 0; i < n; ++i)
            {
                auto& sp = plan.strips[size_t (i)];
                const RoleFamily f = roleFamily (sp.role);
                if (! sp.heard || ! isMusicFamily (f)) continue;
                const float masking = bandAtMixDb (fittedLevelDb[size_t (i)], ctx.capture.strips[size_t (i)], Band::UpperMid) - leadPresence;
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
                // A boost and a cut never land inside the same octave in one pass: a lift this
                // source carries near the pocket (a guitar's definition at 2.5 kHz, a synth's
                // presence at 3 kHz) comes out, the way the tuner takes its own lift back out
                // over a measured cut. The pocket is the measured decision; it stands.
                bool liftTakenOut = false;
                {
                    const auto& cur = plan.proposed.strips[size_t (i)].channel;
                    for (int k = 0; k < int (cur.toneBands.size()); ++k)
                    {
                        const auto& b = cur.toneBands[size_t (k)];
                        if (k != 1 && b.enabled && b.gainDb > 0.5f && b.freqHz < hz * 2.0f && hz < b.freqHz * 2.0f) liftTakenOut = true;
                    }
                }
                d.move (Recommendation::Kind::EQ, TuneSection::Tone,
                        "Made room for the lead vocal in " + upper (sp.name) + ": " + fmtDb (-cut, 1) + " at " + fmtHz (hz),
                        // Past ~24 dB the figure has stopped meaning "this is masking the voice" and
                        // started meaning "the voice has almost nothing in this band" - a number there
                        // is precision the measurement does not have, so it is described instead.
                        "Around " + fmtHz (hz) + " this source carries "
                        + (masking > 24.0f ? std::string ("far more energy than")
                                           : masking >= 0.0f ? fmtDb (masking, 0) + " more energy than"
                                                             : std::string ("almost as much energy as"))
                        + " the lead vocal at mix level. Rather than pushing the voice brighter, the music steps aside where the words live."
                        + (liftTakenOut ? std::string (" The lift this source carried in the same octave was taken out: a cut and a boost in one octave only argue.")
                                        : std::string()),
                        Confidence::Medium, [=] (ChannelParameters& p)
                        {
                            p.toneEqEnabled = true;
                            p.toneBands[1] = { true, FilterType::Peak, hz, -cut, q };
                            for (int k = 0; k < int (p.toneBands.size()); ++k)
                            {
                                auto& b = p.toneBands[size_t (k)];
                                if (k != 1 && b.enabled && b.gainDb > 0.5f && b.freqHz < hz * 2.0f && hz < b.freqHz * 2.0f)
                                    { b.enabled = false; b.gainDb = 0.0f; }
                            }
                        });
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
        if (sp.faint) continue;
        if (! sp.heard)
        {
            sp.mixItems.push_back (info (Recommendation::Kind::Info, upper (sp.name) + ": not heard during the listen",
                                         "This input stayed quiet, so its level was left where it is. Tune Mix again while it is playing.", Confidence::High));
            continue;
        }
        if (sp.bleedOnly) continue;
        if (! balanceable (i)) continue;                 // too sparse to place with confidence
        sp.faderDb = fitFader (i, true);
        sp.balanced = true;
    }

    // The lead vocal is the reference. If its fader could not reach the profile level (a quiet capture hits
    // the bound), everything else follows it down by the same amount so the hierarchy survives; the master's
    // loudness rule makes up the overall level afterwards.
    {
        // The reference is the focal source, and only when it is a microphone somebody is really
        // singing into: one held back because it mostly hears the stage is not a quiet capture
        // the rest of the band should follow down.
        const int lead = focal >= 0 && plan.strips[size_t (focal)].balanced && ! plan.strips[size_t (focal)].spillLimited ? focal : -1;
        if (lead >= 0)
        {
            const float effectiveLevel = predictedProcessedActiveRmsDb (ctx, lead, plan.proposed.strips[size_t (lead)]);
            // Measured against the focal's OWN target: a pinned pastor or sax was fitted to its
            // family's level, and reading it against the lead vocal's number moved every other
            // fader by the difference between two profile entries (+2 dB for speech, -6 for a sax)
            // and called it a fader at its limit.
            const float needed = roundHalf (MixProfile::mixLevelTargetDb (profile, roleFamily (ctx.graph.strips[size_t (lead)].role))
                                            - effectiveLevel);
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

    // Several lead microphones at once. Two voices each fitted to the lead's number are 3 dB of
    // lead vocal, not one, and the band that was balanced under one of them is now buried under
    // both. The group is held so their sum is the level a single lead would have been - the
    // focal one included, because a duet is two people at the same level, not one in front.
    //
    // Only voices that are actually singing together count: two singers taking a verse each are
    // not a duet, and holding each of them down 3 dB for a sum that never happens would leave
    // both under the band. A microphone quiet for more than half the listen is taking turns.
    {
        std::vector<int> together;
        for (int i = 0; i < n; ++i)
        {
            const auto& sp = plan.strips[size_t (i)];
            if (! sp.balanced || roleFamily (sp.role) != RoleFamily::LeadVocal || sp.spillLimited) continue;
            if (ctx.capture.strips[size_t (i)].silencePercent > 50.0f) continue;
            together.push_back (i);
        }
        if (together.size() >= 2)
        {
            const float drop = roundHalf (std::min (10.0f * std::log10 (float (together.size())), 4.0f));
            if (drop >= 0.5f)
            {
                for (int i : together)
                {
                    auto& sp = plan.strips[size_t (i)];
                    sp.faderDb = clamp (sp.faderDb - drop, -R.maxFaderMoveDb, R.maxFaderMoveDb);
                }
                plan.relationships.push_back (info (Recommendation::Kind::MixGain,
                                                     std::to_string (together.size()) + " lead microphones held " + fmtDb (-drop, 1) + " as one voice",
                                                     std::to_string (together.size()) + " lead microphones were sung into together through this listen. Each one set to the lead's own level "
                                                     "would put " + fmtDb (10.0f * std::log10 (float (together.size())), 0) + " more lead vocal in the mix than the balance was built for, and the band "
                                                     "underneath it would be the thing that disappeared. They are held together so the voices sum to one lead - "
                                                     "which is what a duet sounds like.", Confidence::Medium));
            }
        }
    }

    // Lead <-> backing vocals: several voices add up, the group stays behind the lead.
    {
        const int lead = focal;
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

    // ---- Many microphones, one instrument ----
    // The profile's numbers are for an instrument, and a church patches instruments as several
    // channels: the inside and the outside of the kick, the top and the bottom of the snare,
    // a stereo keyboard as two mono stems, five playback stems. Fitting each channel to the
    // whole instrument's level is how a kit came out all wash and no kick, and the playback
    // came up 25 dB (the Praise stems). Three rules, all absolute from the capture:
    //   1. Channels linked on the console that are one kind of source are one source: the
    //      strongest one's chain on all of them, one gain, and one fader for their sum.
    //   2. A blend microphone (Kick Out, Snare Bottom) sits under its drum's main one.
    //   3. Several sources of one kind playing at once share that kind's level.
    {
        auto groupOf = [&] (int i) { return plan.proposed.strips[size_t (i)].linkGroup; };
        auto familyOf = [&] (int i) { return roleFamily (plan.strips[size_t (i)].role); };
        auto powerSum = [] (const std::vector<float>& dbs)
        {
            double e = 0.0;
            for (float d : dbs) e += std::pow (10.0, double (d) / 10.0);
            return e > 0.0 ? float (10.0 * std::log10 (e)) : -120.0f;
        };
        std::vector<bool> inLinkedSource (size_t (n), false);

        // 1. Linked channels of one kind.
        std::vector<int> seen;
        for (int i = 0; i < n; ++i)
        {
            const int g = groupOf (i);
            if (g == 0 || ! plan.strips[size_t (i)].balanced || std::find (seen.begin(), seen.end(), g) != seen.end()) continue;
            seen.push_back (g);
            std::vector<int> members;
            for (int k = 0; k < n; ++k)
                if (groupOf (k) == g && plan.strips[size_t (k)].balanced && familyOf (k) == familyOf (i)) members.push_back (k);
            if (members.size() < 2) continue;
            // The strongest member leads: its chain and its gain go on every member, so the pair
            // is processed as one source and its image does not move when one side gets louder.
            int leader = members.front();
            for (int k : members)
                if (ctx.capture.strips[size_t (k)].activeRmsDb > ctx.capture.strips[size_t (leader)].activeRmsDb) leader = k;
            for (int k : members)
            {
                if (k == leader) continue;
                plan.proposed.strips[size_t (k)].channel = plan.proposed.strips[size_t (leader)].channel;
                plan.proposed.strips[size_t (k)].inputGainDb = plan.proposed.strips[size_t (leader)].inputGainDb;
                plan.strips[size_t (k)].inputGainDb = plan.strips[size_t (leader)].inputGainDb;
            }
            // One fader for the sum, never above what any member's own limits allowed.
            std::vector<float> levels;
            float limit = R.maxFaderMoveDb;
            for (int k : members)
            {
                levels.push_back (predictedProcessedActiveRmsDb (ctx, k, plan.proposed.strips[size_t (k)]));
                limit = std::min (limit, fitFader (k, false));
            }
            const float target = MixProfile::mixLevelTargetDb (profile, familyOf (leader));
            const float fader = clamp (std::min (roundHalf (target - powerSum (levels)), limit), -R.maxFaderMoveDb, R.maxFaderMoveDb);
            std::string names;
            for (size_t m = 0; m < members.size(); ++m)
            {
                plan.strips[size_t (members[m])].faderDb = fader;
                inLinkedSource[size_t (members[m])] = true;
                names += (m == 0 ? "" : " and ") + upper (plan.strips[size_t (members[m])].name);
            }
            plan.relationships.push_back (info (Recommendation::Kind::MixGain, names + " tuned as one source",
                                                 "They are linked and they are one kind of source, so they are one instrument: "
                                                 + upper (plan.strips[size_t (leader)].name) + "'s chain and gain are on all of them, and one fader "
                                                 "sets their sum to where a " + std::string (styleProfileName (profile)) + " mix puts it - not each of "
                                                 "them to it, which would put the instrument " + fmtDb (10.0f * std::log10 (float (members.size())), 0)
                                                 + " too loud.", Confidence::High));
        }

        // 2. Blend microphones.
        for (int i = 0; i < n; ++i)
        {
            auto& sp = plan.strips[size_t (i)];
            const float below = MixProfile::blendBelowPrimaryDb (sp.role);
            if (! sp.balanced || below <= 0.0f || inLinkedSource[size_t (i)]) continue;
            bool primary = false;
            for (int k = 0; k < n; ++k)
                if (k != i && plan.strips[size_t (k)].balanced && familyOf (k) == familyOf (i)
                    && MixProfile::blendBelowPrimaryDb (plan.strips[size_t (k)].role) <= 0.0f) primary = true;
            if (! primary) continue;
            sp.faderDb = clamp (sp.faderDb - below, -R.maxFaderMoveDb, R.maxFaderMoveDb);
            sp.mixItems.push_back (info (Recommendation::Kind::MixGain, upper (sp.name) + " blended " + fmtDb (-below, 0) + " under the main microphone",
                                         "This is the drum's second microphone. The main one is the drum; this one adds the "
                                         + std::string (sp.role == ChannelRole::SnareBottom ? "wires" : "air and the low thump")
                                         + ", and level with it the drum turns to wash.", Confidence::Medium));
        }

        // 3. Several of one kind at once. Drums, voices and the bass have rules of their own
        //    (a kick is one source, the lead and the backing voices are held above).
        auto shares = [] (RoleFamily f)
        {
            return f == RoleFamily::Overhead || f == RoleFamily::Room || f == RoleFamily::Piano || f == RoleFamily::ElectricPiano
                || f == RoleFamily::Organ || f == RoleFamily::Synth || f == RoleFamily::AcousticGuitar
                || f == RoleFamily::ElectricGuitar || f == RoleFamily::Ambience || f == RoleFamily::DrumPad;
        };
        std::vector<RoleFamily> done;
        for (int i = 0; i < n; ++i)
        {
            const RoleFamily f = familyOf (i);
            if (! shares (f) || std::find (done.begin(), done.end(), f) != done.end()) continue;
            done.push_back (f);
            std::vector<int> members;
            std::vector<int> units;            // a linked source counts once
            for (int k = 0; k < n; ++k)
            {
                const auto& sp = plan.strips[size_t (k)];
                if (familyOf (k) != f || ! sp.balanced || ctx.capture.strips[size_t (k)].silencePercent > R.familyShareQuietPercent) continue;
                members.push_back (k);
                const int unit = inLinkedSource[size_t (k)] ? -groupOf (k) : k + 1;
                if (std::find (units.begin(), units.end(), unit) == units.end()) units.push_back (unit);
            }
            if (units.size() < 2) continue;
            const float drop = roundHalf (std::min (10.0f * std::log10 (float (units.size())), R.familyShareMaxDb));
            if (drop < 0.5f) continue;
            std::string names;
            for (size_t m = 0; m < members.size(); ++m)
            {
                auto& sp = plan.strips[size_t (members[m])];
                sp.faderDb = clamp (sp.faderDb - drop, -R.maxFaderMoveDb, R.maxFaderMoveDb);
                names += (m == 0 ? "" : ", ") + upper (sp.name);
            }
            plan.relationships.push_back (info (Recommendation::Kind::MixGain, names + " share one level " + fmtDb (-drop, 1),
                                                 std::to_string (units.size()) + " of the same kind of source play together through this listen. Each set to "
                                                 "that kind's own level would put " + fmtDb (10.0f * std::log10 (float (units.size())), 0)
                                                 + " more of it in the mix than the balance was built for, and the voices would be what disappeared.",
                                                 Confidence::Medium));
        }
    }

    // ---- The top end of the whole mix: the cymbals ----
    // Every source is placed and shaped on its own, and nothing on its own is too bright: the
    // overheads sit at their level, the hi-hat at its, each voice gets the air its own capture
    // asks for. But a kit's cymbals arrive through every microphone on the stage, and it is
    // the sum of all of that above 6 kHz that a listener hears as "the cymbals are a lot". On
    // the QUEENSVIEW service the overheads and the hi-hat were 60 % of the mix between 6 and
    // 12 kHz and nearly all of it above 12, and the top end landed anywhere from 5 to 8 dB
    // under the 1-3 kHz band depending on which thirty seconds the listen caught.
    //
    // So the mix's top end is measured where every source will land - its level, the band's
    // share of it, and the high shelf its chain now carries - against the band the words live
    // in, and held R.topEndBelowUpperMidDb under it. Two moves, in order: a microphone that
    // mostly hears the stage gives back any top it was given (a brighter one is brighter
    // cymbals, not a clearer voice), then the cymbal microphones' own top end comes down, and
    // only what that cannot reach comes off the overhead and hi-hat faders. Everything is
    // computed from the capture and the profile, so the same listen always lands here.
    if (! sermonListen)
    {
        auto isCymbalMic = [] (RoleFamily f) { return f == RoleFamily::Overhead || f == RoleFamily::HiHat || f == RoleFamily::Room; };
        auto isCymbalFader = [] (RoleFamily f) { return f == RoleFamily::Overhead || f == RoleFamily::HiHat; };
        // How much of a shelf's gain reaches 6-20 kHz: nearly all of it from 8 kHz, less the
        // higher the corner sits above the band's bottom edge.
        auto reach = [] (float hz) { return hz <= 8000.0f ? 1.0f : hz <= 10000.0f ? 0.8f : 0.5f; };
        auto powerSum = [] (float accDb, float db) { return 10.0f * std::log10 (std::pow (10.0f, accDb / 10.0f) + std::pow (10.0f, db / 10.0f)); };
        // The shelf each strip's own Tune aims at, from the capture and the profile: never the
        // shelf it runs now, which may be this rule's own earlier cut.
        std::vector<float> aim (size_t (n), 0.0f), aimHz (size_t (n), 10000.0f), tplShelf (size_t (n), 0.0f);
        for (int i = 0; i < n; ++i)
        {
            if (! plan.strips[size_t (i)].balanced) continue;
            TuneContext tc;
            tc.analysis = ctx.capture.strips[size_t (i)];
            tc.role = plan.strips[size_t (i)].role;
            tc.profile = profile;
            tc.current = plan.proposed.strips[size_t (i)].channel;
            const auto t = Profiles::targets (profile, tc.role);
            aim[size_t (i)] = tune::airShelfAimDb (tc, t);
            aimHz[size_t (i)] = t.airHz;
            const auto tpl = Profiles::baseline (profile, tc.role).toneBands[3];
            tplShelf[size_t (i)] = tpl.enabled ? tpl.gainDb : 0.0f;
        }
        const auto stageMic = [&] (int i) { return plan.strips[size_t (i)].spillLimited && ! isCymbalMic (roleFamily (plan.strips[size_t (i)].role)); };
        // The mix's top end against its words with the three moves below taken to the given depth.
        auto excessWith = [&] (bool stageMicsFlat, float shelfCut, float faderDrop) -> float
        {
            float top = -200.0f, words = -200.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto& sp = plan.strips[size_t (i)];
                if (! sp.balanced) continue;
                const auto& a = ctx.capture.strips[size_t (i)];
                const RoleFamily f = roleFamily (sp.role);
                float shelf = aim[size_t (i)];
                if (stageMicsFlat && stageMic (i)) shelf = std::min (shelf, 0.0f);
                if (isCymbalMic (f) && shelfCut > 0.0f) shelf = std::min (shelf, tplShelf[size_t (i)] - shelfCut);
                const float level = predictedProcessedRmsDb (ctx, i, plan.proposed.strips[size_t (i)]) + sp.faderDb
                                  - (isCymbalFader (f) ? faderDrop : 0.0f);
                const float band = powerSum (a.bandEnergyDb[size_t (Band::Brilliance)], a.bandEnergyDb[size_t (Band::Air)]);
                // ... and the shelf its group adds on the way to the master (the drum bus carries
                // one in most profiles): the cymbals reach the listener through both.
                const auto busShelf = Profiles::baseline (profile, busRole (ctx.graph.strips[size_t (i)].bus, ctx.session.purpose)).toneBands[3];
                const float busLift = busShelf.enabled && busShelf.type == FilterType::HighShelf ? busShelf.gainDb * reach (busShelf.freqHz) : 0.0f;
                top = powerSum (top, level + band + shelf * reach (aimHz[size_t (i)]) + busLift);
                words = powerSum (words, level + a.bandEnergyDb[size_t (Band::UpperMid)]);
            }
            return words > -150.0f ? (top - words) + R.topEndBelowUpperMidDb : -100.0f;
        };
        const float excess = excessWith (false, 0.0f, 0.0f);
        if (excess > 0.5f)
        {
            std::vector<std::string> shaped;
            // 1. A microphone that mostly hears the stage keeps no lift above 6 kHz.
            for (int i = 0; i < n; ++i)
            {
                auto& sp = plan.strips[size_t (i)];
                auto& ch = plan.proposed.strips[size_t (i)].channel;
                const auto& b = ch.toneBands[3];
                if (! sp.balanced || ! stageMic (i) || ! (ch.toneEqEnabled && b.enabled && b.gainDb > 0.0f)) continue;
                TuneDecisions d (ch);
                d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Took the top-end lift back out of " + upper (sp.name),
                        "This microphone mostly hears the stage, and above 6 kHz what the stage sends it is the cymbals. A brighter top end "
                        "here would not make the voice clearer; it would make the kit louder through the wrong microphone.",
                        Confidence::Medium, [] (ChannelParameters& q) { q.toneBands[3].enabled = false; q.toneBands[3].gainDb = 0.0f; });
                commit (d, ch, sp.mixItems, plan.relationships);
                shaped.push_back (upper (sp.name));
            }
            // 2. The cymbal microphones' own top end: a cut below the profile's own shelf, as
            //    deep as the excess asks for and never deeper than the profile allows.
            float shelfCut = 0.0f;
            while (shelfCut < R.cymbalShelfMaxCutDb && excessWith (true, shelfCut, 0.0f) > 0.5f) shelfCut += 0.5f;
            if (shelfCut >= 0.5f)
            {
                for (int i = 0; i < n; ++i)
                {
                    auto& sp = plan.strips[size_t (i)];
                    auto& ch = plan.proposed.strips[size_t (i)].channel;
                    if (! sp.balanced || ! isCymbalMic (roleFamily (sp.role))) continue;
                    const auto tpl = Profiles::baseline (profile, sp.role).toneBands[3];
                    const float freq = tpl.type == FilterType::HighShelf && tpl.freqHz > 0.0f ? tpl.freqHz : 8000.0f;
                    const float want = roundHalf (tplShelf[size_t (i)] - shelfCut);
                    const auto& cur = ch.toneBands[3];
                    const bool curShelf = ch.toneEqEnabled && cur.enabled && cur.type == FilterType::HighShelf;
                    const float gain = curShelf ? std::min (cur.gainDb, want) : want;
                    if (curShelf && cur.gainDb - gain < 0.5f) continue;
                    TuneDecisions d (ch);
                    d.move (Recommendation::Kind::EQ, TuneSection::Tone,
                            "Softened the cymbals in " + upper (sp.name) + ": high shelf down to " + fmtDb (gain) + " at " + fmtHz (freq),
                            "Across every microphone on the stage the mix carries more above 6 kHz than a " + std::string (styleProfileName (profile))
                            + " mix does, and most of it is the kit's cymbals. Their own microphones give up the top end first, so the voices keep theirs.",
                            Confidence::Medium, [=] (ChannelParameters& q)
                            {
                                q.toneEqEnabled = true;
                                q.toneBands[3] = { true, FilterType::HighShelf, freq, gain, 0.7f };
                            });
                    commit (d, ch, sp.mixItems, plan.relationships);
                    shaped.push_back (upper (sp.name));
                }
            }
            // 3. What the shelves could not reach comes off the overhead and hi-hat faders.
            float drop = 0.0f;
            while (drop < R.cymbalFaderMaxCutDb && excessWith (true, shelfCut, drop) > 0.5f) drop += 0.5f;
            if (drop >= 0.5f)
            {
                for (int i = 0; i < n; ++i)
                {
                    auto& sp = plan.strips[size_t (i)];
                    if (! sp.balanced || ! isCymbalFader (roleFamily (sp.role))) continue;
                    sp.faderDb = clamp (sp.faderDb - drop, -R.maxFaderMoveDb, R.maxFaderMoveDb);
                    shaped.push_back (upper (sp.name) + " " + fmtDb (-drop, 1));
                }
            }
            if (! shaped.empty())
            {
                std::string names;
                for (size_t k = 0; k < shaped.size(); ++k) names += (k == 0 ? "" : ", ") + shaped[k];
                plan.relationships.push_back (info (Recommendation::Kind::MixGain, "Cymbals held back: " + names,
                                                     "Where every source lands, the mix carried " + fmtDb (excess, 1) + " more above 6 kHz than a "
                                                     + std::string (styleProfileName (profile)) + " mix does against the band the words live in. A kit's cymbals reach "
                                                     "every microphone on the stage, so no single channel looked too bright - the sum was. The top came off the "
                                                     "microphones that are there for the cymbals, and off the ones that only hear them.", Confidence::Medium));
            }
        }
    }

    // ---- A sermon listen: the spoken word, and no band behind it ----
    // A service is not one performance, it is a sequence of them, and the listen only ever
    // hears the one that is happening. When the only thing playing is the speech group, the
    // profile's own -16 dBFS is the wrong question to ask: that number is a *balance* - where
    // a voice sits against a band - and there is no band to sit against. What matters instead
    // is how loud the words leave the building, and that is the master's own delivery target
    // read through the master the band already set. So the speech faders are moved together
    // until the mix DLIVE can hear measures what the stream is asked for, and the master
    // itself is left exactly as the song left it (below). Come back to the song and the band
    // is still where it was, at the level it was, through a master that never moved.
    if (sermonListen && ctx.capture.masterOutput.valid)
    {
        const auto& mo = ctx.capture.masterOutput;
        const float delivered = mo.loudnessGatedLufs > -100.0f ? mo.loudnessGatedLufs : mo.loudnessLufs;
        const SourceTargets masterT = Profiles::targets (profile, busRole (MixBus::Master, ctx.session.purpose));
        const float wantedLufs = ctx.session.deliveryTargetLufs() < 0.0f ? ctx.session.deliveryTargetLufs() : masterT.targetLufs;
        if (delivered > -100.0f && masterT.loudnessTargetAppropriate)
        {
            // What the speech faders have already done to the master's input, and what the
            // master's own compressor will take back off it when it arrives that much louder.
            const float stripShift = faderShiftDb (ctx, ctx.atCapture, plan.proposed, MixBus::Speech, false);
            const auto& masterChain = ctx.atCapture.master().channel;
            const auto& ma = ctx.capture.buses[size_t (MixBus::Master)];
            const float absorbed = ma.valid ? compressorAverageDeltaDb (masterChain, masterChain, ma.rmsDb, ma.peakDb, stripShift, R.compDetectorCrestShareMaster) : 0.0f;
            const float correction = roundHalf (wantedLufs - (delivered + stripShift + absorbed));
            if (std::fabs (correction) >= 0.5f)
            {
                int moved = 0, shortOf = 0;
                for (int i = 0; i < n; ++i)
                {
                    auto& sp = plan.strips[size_t (i)];
                    if (! sp.balanced || ctx.graph.strips[size_t (i)].bus != MixBus::Speech) continue;
                    const float wanted = sp.faderDb + correction;
                    sp.faderDb = clamp (wanted, -R.maxFaderMoveDb, R.maxFaderMoveDb);
                    if (std::fabs (wanted - sp.faderDb) >= 0.5f)
                    {
                        ++shortOf;
                        sp.mixItems.push_back (info (Recommendation::Kind::CaptureGain, upper (sp.name) + ": turn this microphone up at the console",
                                                     "To carry the sermon at the level this delivery asks for, this microphone needs "
                                                     + num ("%.0f dB", double (std::fabs (wanted - sp.faderDb))) + " more than DLIVE will add to a fader. "
                                                     "Turn its preamp up at the console - or get the speaker closer to it - and Tune Mix again.", Confidence::High));
                    }
                    ++moved;
                }
                if (shortOf > 0) ++plan.stripsWantPreamp;
                if (moved > 0)
                    plan.relationships.push_back (info (Recommendation::Kind::MixGain,
                                                        "The spoken word set by what leaves the mix: " + fmtDb (correction, 1),
                                                        "Nothing but the speech group played during this listen, so there is no band for the voice to be balanced against - what "
                                                        "decides its level is how loud the words leave DLIVE. Through the master the band already set, the mix measured "
                                                        + num ("%.1f LUFS", double (delivered)) + " and this delivery asks for " + num ("%.0f LUFS", double (wantedLufs))
                                                        + ", so the speech group is moved " + fmtDb (correction, 1) + " and the master is left exactly where the song left it. "
                                                        "That is what keeps a sermon and a song at the same level for the listener.", Confidence::High));
            }
        }
    }

    // Faders are written once, after every rule that touches them, so a fader that ends where it started is not a change.
    int heldBack = 0;
    for (int i = 0; i < n; ++i)
    {
        auto& sp = plan.strips[size_t (i)];
        if (! sp.balanced) continue;
        // A correction to a mix somebody is already listening to moves one fader only so far.
        // Measured from the fader that ran during the listen, so re-planning the same listen
        // lands in the same place however many times it is asked.
        if (ctx.retune)
        {
            const float ran = ctx.atCapture.strips[size_t (i)].faderDb;
            const float capped = clamp (sp.faderDb, ran - R.maxRetuneFaderStepDb, ran + R.maxRetuneFaderStepDb);
            if (std::fabs (capped - sp.faderDb) >= 0.5f)
            {
                ++heldBack;
                sp.mixItems.push_back (info (Recommendation::Kind::MixGain, upper (sp.name) + ": level moved as far as one tune will move it",
                                             "This listen asked for " + fmtDb (sp.faderDb - ran, 1) + " on " + upper (sp.name) + ", which is more than a "
                                             "single re-tune will change a mix you are already listening to. It moved " + fmtDb (capped - ran, 1)
                                             + ". If the next listen still asks for the rest, RE-TUNE will take it there.", Confidence::Medium));
            }
            sp.faderDb = capped;
        }
        if (std::fabs (sp.faderDb - sp.faderBeforeDb) < R.faderDeadbandDb) { sp.faderDb = sp.faderBeforeDb; continue; }
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
    // ... except during a sermon. The master's density, its tone and above all its output level
    // were fitted to the sum of a band; a speech microphone on its own is a different signal
    // entirely, and re-fitting the master to it means the band comes back through a master
    // built for one voice - and then the next TUNE MIX moves it back. That swing between the
    // song and the sermon is exactly what a listener hears as the level "jumping about". The
    // speech group was just set by what leaves the mix instead, so there is nothing left for
    // the master to do here.
    if (sermonListen)
    {
        busPlayed[size_t (MixBus::Master)] = false;
        plan.notes.push_back ("Only the spoken word played, so the master was left exactly as the band set it and the speech group was "
                              "set by what leaves the mix. Tune Mix again with the band playing to fit the master itself.");
    }

    auto planBus = [&] (int b)
    {
        auto& bp = plan.buses[size_t (b)];
        bp.bus = MixBus (b);
        bp.used = ctx.graph.busUsed[size_t (b)];
        const auto& a = ctx.capture.buses[size_t (b)];
        if (! bp.used || ! a.valid) return;
        if (! busPlayed[size_t (b)])
        {
            if (! (sermonListen && MixBus (b) == MixBus::Master))
                plan.notes.push_back (std::string (mixBusName (MixBus (b)))
                                      + ": nothing on this group played during the listen, so its chain was left alone."
                                        " Tune Mix again while it does.");
            return;
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
        SourceTargets masterTargets;
        if (isMaster)
        {
            masterTargets = Profiles::targets (profile, role);
            bool overridden = false;
            if (ctx.reference.valid)
            {
                masterTargets = Reference::targets (masterTargets, ctx.reference, a, profile, plan.reference);
                overridden = true;
            }
            // HOW LOUD THE FINISHED MIX SHOULD BE. A session can aim somewhere other than its
            // delivery role's own standard, and when it does, that is the number the whole
            // gain structure is fitted against - not a gain added at the end. A reference sets
            // the *tone*; it is explicitly not allowed to set the delivery loudness, so this
            // is applied after it and wins.
            const float wanted = ctx.session.deliveryTargetLufs();
            if (wanted < 0.0f && masterTargets.loudnessTargetAppropriate)
            {
                masterTargets.targetLufs = wanted;
                // A louder target needs a lower ceiling to stay true-peak safe on a codec:
                // -14 LUFS through a lossy encoder wants -1 dBTP, not -0.3.
                masterTargets.truePeakCeilingDb = std::min (masterTargets.truePeakCeilingDb, wanted >= -15.0f ? -1.0f : -1.5f);
                overridden = true;
            }
            if (overridden) tc.targetsOverride = &masterTargets;
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
                if (! ctx.graph.busUsed[size_t (o)] || ! oa.valid || oa.rmsDb <= -100.0f) continue;   // the next group, not the end of the master
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
        const float masterOutLufs = ctx.capture.masterOutput.loudnessGatedLufs > -100.0f
                                      ? ctx.capture.masterOutput.loudnessGatedLufs : ctx.capture.masterOutput.loudnessLufs;
        if (isMaster && ctx.capture.masterOutput.valid && masterOutLufs > -100.0f)
        {
            // The loudness rule predicts the output as input loudness + output trim. What actually left the master
            // during the listen is known, so the input loudness is set to make that prediction exact: the
            // compression and limiting on the way are then accounted for, and the number is still computed from
            // the listen and the trim at the listen, never from the current value. The master compressor the plan
            // chooses works harder than the one that ran (more level, often a lower threshold); that extra reduction
            // is not in the measurement, so the master is tuned twice: once to learn its compressor, then again with
            // the loudness that compressor will leave. On the same listen the compressor is unchanged and the second
            // pass equals the first.
            const float measuredIn = masterOutLufs - ctx.atCapture.master().channel.outputTrimDb + shift;
            tc.analysis.loudnessLufs = measuredIn;
            tc.analysis.loudnessGatedLufs = measuredIn;
            tc.analysis.truePeakDb = ctx.capture.masterOutput.truePeakDb + shift;
            const TuneResult first = TuneEngine::tune (tc);
            const ChannelParameters& chosen = first.valid ? first.proposed : ctx.current.master().channel;
            const float compDelta = compressorAverageDeltaDb (chosen, ctx.atCapture.master().channel, a.rmsDb, a.peakDb, shift, R.compDetectorCrestShareMaster);
            tc.analysis.loudnessLufs = measuredIn + compDelta;
            tc.analysis.loudnessGatedLufs = tc.analysis.loudnessLufs;
            tc.analysis.truePeakDb += compDelta;
        }
        bp.tune = TuneEngine::tune (tc);
        if (bp.tune.valid) plan.proposed.buses[size_t (b)].channel = bp.tune.proposed;
        if (! isMaster)
            bp.predictedOutShiftDb = busOutShiftDb[size_t (b)] = shift
                                      + compressorAverageDeltaDb (plan.proposed.buses[size_t (b)].channel, ctx.atCapture.buses[size_t (b)].channel, a.rmsDb, a.peakDb, shift, R.compDetectorCrestShareBus)
                                      + (plan.proposed.buses[size_t (b)].faderDb - ctx.atCapture.buses[size_t (b)].faderDb);
    };

    // The groups first, then their balance against each other, then the master from the sum of
    // what they will all put out.
    for (int b = 0; b < int (MixBus::Master); ++b) planBus (b);

    // ---- The group balance ----
    // Every source has been placed against the profile's number for its own kind. What that
    // does not settle is how the groups sit against each other, because that depends on how
    // many microphones are in each one: six backing voices at the backing-voice level are a
    // different group from two, eight drum microphones are a different kit from four, and a
    // church with one keyboard and a church with three do not get the same MUSIC bus out of
    // the same per-source numbers. So the groups are finally set against the voices - the
    // relationship the profile has always described and nothing has ever read (DRUMS -1,
    // BASS -3, MUSIC -5, SPEECH level, AMBIENCE -12 in Modern Gospel) - using what each bus
    // is predicted to actually put out under the plan.
    {
        // THE LEAD IS WHAT THE MIX IS BUILT AROUND, so it is what the groups are set against.
        // Backing voices are a texture under it and cannot be the reference; with no lead
        // singing the backing group is the nearest thing to one, and a sermon has only speech.
        // A reference group has to have somebody really singing (or speaking) into it: an open
        // spare microphone on a wedge, held back because it mostly hears the stage, would set the
        // whole band against stage spill. The focal rule excludes those microphones; so does this.
        std::array<bool, int (MixBus::Count)> busSung {};
        for (int i = 0; i < n; ++i)
        {
            const auto& sp = plan.strips[size_t (i)];
            if (sp.heard && ! sp.bleedOnly && ! sp.spillLimited) busSung[size_t (ctx.graph.strips[size_t (i)].bus)] = true;
        }
        const auto playing = [&] (MixBus b) { return busSung[size_t (b)] && busPlayed[size_t (b)] && ctx.graph.busUsed[size_t (b)]; };
        const MixBus ref = playing (MixBus::Lead)   ? MixBus::Lead
                         : playing (MixBus::Vocals) ? MixBus::Vocals
                         : playing (MixBus::Speech) ? MixBus::Speech
                         : MixBus::Count;
        // What a group puts out with its fader at zero, under this plan's chains and strip
        // levels: a function of the listen alone, so a fader set here is the same number
        // however many times the same listen is planned.
        auto baseOut = [&] (MixBus bus) -> float
        {
            // The whole listen's level, not the level while it plays: on the QUEENSVIEW service the
            // active level made a fresh listen through the tuned mix move the groups further, not less
            // (34 dB against 31.5 over four windows), so the average that the prediction was built on stays.
            const auto& a = ctx.capture.buses[size_t (bus)];
            if (! a.valid || a.rmsDb <= -100.0f) return -120.0f;
            return a.rmsDb + busOutShiftDb[size_t (bus)]
                 - plan.proposed.buses[size_t (bus)].faderDb + ctx.atCapture.buses[size_t (bus)].faderDb;
        };
        const float refBase = ref != MixBus::Count ? baseOut (ref) : -120.0f;
        // The reference group keeps whatever fader the listen ran through: this rule sets the
        // others against it, it never moves it.
        const float refOut = refBase > -100.0f ? refBase + ctx.atCapture.buses[size_t (ref)].faderDb : -120.0f;
        if (refOut > -100.0f)
        {
            std::vector<std::string> moved;
            for (int b = 0; b < int (MixBus::Master); ++b)
            {
                const MixBus bus = MixBus (b);
                if (bus == ref || ! ctx.graph.busUsed[size_t (b)] || ! busPlayed[size_t (b)]) continue;
                const float base = baseOut (bus);
                if (base <= -100.0f) continue;
                const float want = refOut + R.busBelowVocalsDb[size_t (b)];
                const float ran = ctx.atCapture.buses[size_t (b)].faderDb;
                float fader = roundHalf (want - base);
                fader = clamp (fader, ran - R.maxBusFaderMoveDb, ran + R.maxBusFaderMoveDb);
                if (ctx.retune) fader = clamp (fader, ran - R.maxRetuneFaderStepDb, ran + R.maxRetuneFaderStepDb);
                if (std::fabs (fader - plan.before.buses[size_t (b)].faderDb) < R.faderDeadbandDb) continue;
                const float was = plan.proposed.buses[size_t (b)].faderDb;
                plan.proposed.buses[size_t (b)].faderDb = fader;
                busOutShiftDb[size_t (b)] += fader - was;
                plan.buses[size_t (b)].predictedOutShiftDb = busOutShiftDb[size_t (b)];
                moved.push_back (std::string (mixBusName (bus)) + " " + fmtDb (fader - plan.before.buses[size_t (b)].faderDb, 1));
            }
            if (! moved.empty())
            {
                std::string names;
                for (size_t k = 0; k < moved.size(); ++k) names += (k == 0 ? "" : ", ") + moved[k];
                plan.relationships.push_back (info (Recommendation::Kind::MixGain, "Groups set against the " + std::string (mixBusName (ref)) + ": " + names,
                                                     "Each source is at the level its own kind sits at, but how loud a group ends up also depends on how many "
                                                     "microphones are in it - six backing voices are not two, and eight drum microphones are not four. The groups "
                                                     "are set against the lead so a " + std::string (styleProfileName (profile)) + " mix sounds like one whatever "
                                                     "this church happens to have on the stage.", Confidence::Medium));
            }
        }
    }

    planBus (int (MixBus::Master));

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
    // What this proposal does to the mix that is running, source by source, biggest first. A
    // count of levels changed says how much happened; this says what, which is the thing an
    // engineer has to agree with before pressing KEEP.
    {
        std::vector<std::pair<float, std::string>> moves;
        for (const auto& sp : plan.strips)
        {
            const float d = plan.proposed.strips[size_t (sp.strip)].faderDb - plan.before.strips[size_t (sp.strip)].faderDb
                          + plan.proposed.strips[size_t (sp.strip)].inputGainDb - plan.before.strips[size_t (sp.strip)].inputGainDb;
            if (std::fabs (d) >= 1.0f) moves.emplace_back (d, upper (sp.name));
        }
        std::sort (moves.begin(), moves.end(), [] (const auto& a, const auto& b) { return std::fabs (a.first) > std::fabs (b.first); });
        if (! moves.empty())
        {
            std::string line;
            for (size_t k = 0; k < moves.size() && k < 5; ++k) line += (k == 0 ? "" : ", ") + moves[k].second + " " + fmtDb (moves[k].first, 1);
            if (moves.size() > 5) line += " and " + std::to_string (moves.size() - 5) + " more";
            plan.notes.push_back ("What moves, against the mix you have now: " + line + ".");
        }
    }
    if (heldBack > 0)
        plan.notes.push_back (heldBack == 1 ? "1 level wanted to move further than one re-tune will move it; RE-TUNE again if the next listen still asks."
                                            : std::to_string (heldBack) + " levels wanted to move further than one re-tune will move them; RE-TUNE again if the next listen still asks.");
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

    // A channel linked to others of its own kind (keys1L and keys1r) is one source, and the
    // plan already decided it as one: tuning one side tunes them all, or the pair would come
    // back with one side processed and the other not.
    std::vector<int> members { strip };
    if (const int g = full.before.strips[i].linkGroup; g != 0)
        for (int k = 0; k < int (full.strips.size()) && k < full.before.numStrips; ++k)
            if (k != strip && full.before.strips[size_t (k)].linkGroup == g
                && roleFamily (full.strips[size_t (k)].role) == roleFamily (full.strips[i].role))
                members.push_back (k);
    auto isMember = [&members] (int k) { return std::find (members.begin(), members.end(), k) != members.end(); };

    for (auto& sp : out.strips)
    {
        if (isMember (sp.strip)) continue;
        sp.faderDb = sp.faderBeforeDb;
        sp.inputGainDb = sp.inputGainBeforeDb;
        sp.balanced = false;
    }

    const auto& sp = out.strips[i];
    std::string NAME = upper (sp.name);
    if (members.size() == 2) NAME += " AND " + upper (out.strips[size_t (members[1])].name);
    else if (members.size() > 2) NAME += " + " + std::to_string (members.size() - 1) + " LINKED";
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

    out.relationships = sp.mixItems;
    for (int k : members)
    {
        const size_t m = size_t (k);
        out.proposed.strips[m] = full.proposed.strips[m];
        out.parametersChanged += int (diffParameters (out.before.strips[m].channel, out.proposed.strips[m].channel).size());
        if (std::fabs (out.before.strips[m].faderDb - out.proposed.strips[m].faderDb) >= 0.01f) ++out.fadersChanged;
        if (std::fabs (out.before.strips[m].inputGainDb - out.proposed.strips[m].inputGainDb) >= 0.01f) ++out.gainsChanged;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (std::fabs (out.before.strips[m].sendDb[size_t (f)] - out.proposed.strips[m].sendDb[size_t (f)]) >= 0.01f) ++out.sendsChanged;
        if (k != strip)
            for (const auto& r : out.strips[m].mixItems) out.relationships.push_back (r);
    }
    // The "tuned as one source" sentence belongs to the pair, so it is said once.
    for (const auto& r : full.relationships)
        if (members.size() > 1 && r.what.find ("tuned as one source") != std::string::npos
            && r.what.find (upper (sp.name)) != std::string::npos)
            out.relationships.push_back (r);

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
        out.notes.push_back (NAME + " heard only spill during the listen, so its level was left alone. "
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


// ---- KEEP SOME / TUNE <GROUP> ----
// The plan above with part of it taken back out. `proposed` goes back to `before` for every
// strip and group that is not selected, and every count is recomputed from what is left, so
// the narrowed plan's `proposed` is exactly what the mix becomes when it is kept - the same
// promise channelOnly makes for one strip. Nothing is re-decided: what the listen measured
// and what the planner concluded stand; only what is applied is smaller.
namespace
{
    void narrow (MixPlan& out, const MixPlan& full, const MixPlanner::PlanSelection& sel)
    {
        out.proposed = full.before;
        const int n = std::min (full.before.numStrips, full.proposed.numStrips);
        for (int i = 0; i < n; ++i)
            if (sel.strip (i)) out.proposed.strips[size_t (i)] = full.proposed.strips[size_t (i)];
        for (int b = 0; b < int (MixBus::Count); ++b)
            if (sel.bus (MixBus (b))) out.proposed.buses[size_t (b)] = full.proposed.buses[size_t (b)];
        // The returns and the tempo belong to no single input; they ride with the master.
        if (sel.bus (MixBus::Master))
        {
            out.proposed.fx = full.proposed.fx;
            out.proposed.tempoBpm = full.proposed.tempoBpm;
        }
        for (auto& sp : out.strips)
        {
            if (sel.strip (sp.strip)) continue;
            sp.faderDb = sp.faderBeforeDb;
            sp.inputGainDb = sp.inputGainBeforeDb;
            sp.balanced = false;
        }
        if (! sel.bus (MixBus::Master)) out.reference = ReferenceMatch {};
    }

    void recount (MixPlan& out)
    {
        out.parametersChanged = out.fadersChanged = out.sendsChanged = out.gainsChanged = 0;
        const int n = std::min (out.before.numStrips, out.proposed.numStrips);
        for (int i = 0; i < n; ++i)
        {
            const auto& a = out.before.strips[size_t (i)];
            const auto& b = out.proposed.strips[size_t (i)];
            out.parametersChanged += int (diffParameters (a.channel, b.channel).size());
            if (std::fabs (a.faderDb - b.faderDb) >= 0.01f) ++out.fadersChanged;
            if (std::fabs (a.inputGainDb - b.inputGainDb) >= 0.01f) ++out.gainsChanged;
            for (int f = 0; f < int (FxSlot::Count); ++f)
                if (std::fabs (a.sendDb[size_t (f)] - b.sendDb[size_t (f)]) >= 0.01f) ++out.sendsChanged;
        }
        for (int b = 0; b < int (MixBus::Count); ++b)
            out.parametersChanged += int (diffParameters (out.before.buses[size_t (b)].channel, out.proposed.buses[size_t (b)].channel).size());
        out.noChangeRequired = out.parametersChanged == 0 && out.fadersChanged == 0 && out.sendsChanged == 0 && out.gainsChanged == 0;
    }
}

MixPlanner::PlanSelection MixPlanner::PlanSelection::all (int numStrips)
{
    PlanSelection s;
    for (int i = 0; i < numStrips && i < kMaxStrips; ++i) s.strips[size_t (i)] = true;
    s.buses.fill (true);
    return s;
}

MixPlanner::PlanSelection MixPlanner::PlanSelection::group (const RoutingGraph& graph, MixBus bus)
{
    PlanSelection s;
    for (int i = 0; i < graph.numStrips() && i < kMaxStrips; ++i)
        if (graph.strips[size_t (i)].bus == bus) s.strips[size_t (i)] = true;
    if (int (bus) >= 0 && int (bus) < int (MixBus::Count)) s.buses[size_t (bus)] = true;
    return s;
}

bool MixPlanner::PlanSelection::any() const noexcept
{
    for (bool b : strips) if (b) return true;
    for (bool b : buses) if (b) return true;
    return false;
}

bool MixPlanner::PlanSelection::everything (int numStrips) const noexcept
{
    for (int i = 0; i < numStrips && i < kMaxStrips; ++i) if (! strips[size_t (i)]) return false;
    for (bool b : buses) if (! b) return false;
    return true;
}

MixPlan restrictTo (const MixPlan& full, const MixPlanner::PlanSelection& sel, const RoutingGraph& graph, StyleProfileId)
{
    MixPlan out = full;
    if (! full.valid) return out;
    narrow (out, full, sel);
    recount (out);
    out.notes.clear();
    out.stripsWantPreamp = 0;
    if (out.noChangeRequired)
    {
        out.headline = "MIX: NOTHING SELECTED TO KEEP";
        out.notes.push_back ("None of what was selected changes anything, so keeping it changes nothing.");
        return out;
    }
    // What was kept, by group, in the words the sheet uses.
    std::vector<std::string> groups;
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        bool touched = sel.bus (MixBus (b));
        for (int i = 0; i < graph.numStrips() && ! touched; ++i)
            if (graph.strips[size_t (i)].bus == MixBus (b) && sel.strip (i)) touched = true;
        if (touched) groups.push_back (mixBusName (MixBus (b)));
    }
    std::string names;
    for (size_t k = 0; k < groups.size(); ++k) names += (k == 0 ? "" : k + 1 == groups.size() ? " and " : ", ") + groups[k];
    out.headline = sel.everything (full.before.numStrips) ? full.headline : "MIX: " + names + " KEPT";
    out.notes.push_back (std::to_string (out.parametersChanged) + (out.parametersChanged == 1 ? " setting" : " settings") + ", "
                         + std::to_string (out.fadersChanged) + (out.fadersChanged == 1 ? " level" : " levels") + " and "
                         + std::to_string (out.gainsChanged) + (out.gainsChanged == 1 ? " input gain" : " input gains") + " kept on " + names
                         + ". Everything else is exactly as it was.");
    return out;
}

MixPlan busOnly (const MixPlan& full, MixBus bus, const RoutingGraph& graph, StyleProfileId profile)
{
    MixPlan out = full;
    out.relationships.clear();
    out.notes.clear();
    out.reference = ReferenceMatch {};
    out.stripsWantPreamp = 0;
    out.stripsHeard = 0;
    out.stripsFaint = 0;
    const std::string NAME = mixBusName (bus);
    if (! full.valid || int (bus) < 0 || int (bus) >= int (MixBus::Master))
    {
        out.proposed = full.before;
        recount (out);
        out.headline = NAME + ": NOT IN THIS MIX";
        out.notes.push_back ("That group is not part of this mix. Check the assignments, then tune it again.");
        return out;
    }
    const auto sel = MixPlanner::PlanSelection::group (graph, bus);
    narrow (out, full, sel);

    // Who on the group played. A strip heard only as spill (the pastor's microphone during
    // the song) is not a source that played, and the listen says so below.
    int played = 0, faint = 0, notHeard = 0, heardAny = 0;
    std::vector<std::string> left;
    for (const auto& sp : out.strips)
    {
        if (! sel.strip (sp.strip)) continue;
        if (sp.faint) { ++faint; ++out.stripsFaint; left.push_back (upper (sp.name) + ": barely reached DLIVE, check it"); continue; }
        if (! sp.heard) { ++notHeard; left.push_back (upper (sp.name) + ": not heard, left as it was"); continue; }
        ++heardAny;
        if (sp.bleedOnly) { left.push_back (upper (sp.name) + ": heard only as spill, its level left alone"); continue; }
        ++played;
        for (const auto& r : sp.mixItems) out.relationships.push_back (r);
        if (sp.tune.valid && ! sp.bleedOnly)
        {
            const auto& R = MixProfile::relationships (profile);
            const bool health = sp.tune.report.inputHealth != "Healthy" && ! sp.tune.report.inputHealth.empty();
            if (health || std::fabs (sp.inputGainDb) >= R.digitalGainAdviceDb) ++out.stripsWantPreamp;
        }
    }
    // `stripsHeard` on a group plan is how many of its sources actually played: a microphone
    // that only picked up the rest of the stage is not a source the group can be tuned from,
    // and the caller decides whether there is anything to preview from this one number.
    out.stripsHeard = played;
    if (played == 0)
    {
        // Nothing on the group really played: nothing is proposed for it, and `proposed` is
        // `before` everywhere so keeping this plan changes nothing.
        out.proposed = full.before;
        for (auto& sp : out.strips) { sp.faderDb = sp.faderBeforeDb; sp.inputGainDb = sp.inputGainBeforeDb; sp.balanced = false; }
        recount (out);
        out.headline = faint > 0 && notHeard == 0 ? NAME + ": CHECK THESE INPUTS" : NAME + " WAS NOT HEARD";
        out.notes.push_back (heardAny > 0 ? "The " + NAME + " microphones only picked up the rest of the stage during the listen, so nothing was decided about them. Tune the group again while it plays."
                                          : "Nothing on " + NAME + " played during the listen, so nothing was decided about it. Tune the group again while it plays.");
        for (const auto& l : left) out.notes.push_back (l);
        return out;
    }

    recount (out);
    out.headline = out.noChangeRequired ? NAME + ": NO CHANGE REQUIRED" : NAME + " TUNED";
    if (out.stripsWantPreamp > 0)
        out.notes.push_back (std::string ("Gain staging first: ") + (out.stripsWantPreamp == 1 ? "one " + NAME + " input still wants its preamp moved at the console." : std::to_string (out.stripsWantPreamp) + " " + NAME + " inputs still want their preamps moved at the console."));
    if (! out.noChangeRequired)
        out.notes.push_back (std::to_string (played) + (played == 1 ? " source on " : " sources on ") + NAME + " shaped from what it played: "
                             + std::to_string (out.parametersChanged) + (out.parametersChanged == 1 ? " setting" : " settings")
                             + (out.fadersChanged > 0 ? ", " + std::to_string (out.fadersChanged) + (out.fadersChanged == 1 ? " level" : " levels") : "")
                             + (out.gainsChanged > 0 ? ", " + std::to_string (out.gainsChanged) + (out.gainsChanged == 1 ? " input gain" : " input gains") : "")
                             + ". The other groups and the master stay exactly where they are.");
    if (! diffParameters (out.before.buses[size_t (bus)].channel, out.proposed.buses[size_t (bus)].channel).empty())
        out.notes.push_back ("The " + NAME + " group's own chain was re-fitted to what the group carried.");
    for (const auto& l : left) out.notes.push_back (l);
    if (out.noChangeRequired)
        out.notes.push_back (NAME + " is already where the profile wants it. Nothing was changed.");
    return out;
}

} // namespace MixPlanner
} // namespace livemix
