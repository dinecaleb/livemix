#include "MeasuredMix.h"
#include "OfflineCapture.h"
#include "Intelligence/SafetyValidator.h"
#include "Tune/TuneEngine.h"
#include "Profiles/Profile.h"
#include "Profiles/MixProfileData.h"
#include "Core/DbUtils.h"
#include <algorithm>
#include <cmath>

namespace livemix::MeasuredMix
{
namespace
{
constexpr int blockSize = 256;
bool cancelled (const std::atomic<bool>* flag) { return flag && flag->load(); }
float loudness (const AnalysisResult& a) { return a.loudnessGatedLufs > -100 ? a.loudnessGatedLufs : a.loudnessLufs; }
float target (const MixSession& s)
{
    const float override = deliveryLoudnessLufs (s.delivery);
    return override < 0 ? override : Profiles::targets (s.profile, masterRoleFor (s.purpose)).targetLufs;
}
// Capture processed spectra alongside the existing bus/master analysis, using the same accumulator.
struct Tap : OfflineCapture
{
    std::vector<std::unique_ptr<AnalysisAccumulator>> post, window;
    std::vector<int> windowFrames;
    std::vector<std::vector<AnalysisResult>> windows;
    int windowSize = 24000;
    void pushStripProcessed (int strip, const AudioBlockView& audio) noexcept override
    {
        OfflineCapture::pushStripProcessed (strip, audio);
        if (! isActive()) return;
        const auto index = size_t (strip);
        post[index]->consume (audio);
        for (int pos = 0; pos < audio.numSamples;)
        {
            const int n = std::min (audio.numSamples - pos, windowSize - windowFrames[index]);
            std::array<float*, kMaxChannels> ptrs {};
            for (int c = 0; c < audio.numChannels; ++c) ptrs[size_t (c)] = audio.channels[c] + pos;
            window[index]->consume (AudioBlockView { ptrs.data(), audio.numChannels, n });
            windowFrames[index] += n; pos += n;
            if (windowFrames[index] == windowSize)
            {
                windows[index].push_back (window[index]->finalise());
                window[index]->reset(); windowFrames[index] = 0;
            }
        }
    }
};
}
Render render (const MixSession& original, const MixParameters& parameters, const MixCapture::Replay& pcm,
               const SampleBankTable* banks, const std::atomic<bool>* cancel)
{
    Render out;
    const auto originalGraph = RoutingGraph::build (original);
    if (pcm.frames < pcm.sampleRate * MixProfile::measuredTune().relationshipWindowSeconds || ! std::isfinite (pcm.sampleRate) || pcm.sampleRate < 8000 || pcm.sampleRate > 192000
        || pcm.strips.size() != originalGraph.strips.size() || pcm.channels.size() != pcm.strips.size()
        || originalGraph.numStrips() != parameters.numStrips || pcm.frames > pcm.sampleRate * 60) return out;
    MixSession session = original;
    session.inputs.clear();
    for (const auto& route : originalGraph.strips) session.inputs.push_back (original.inputs[size_t (route.input)]);
    int deviceChannels = 0;
    for (size_t i = 0; i < pcm.strips.size(); ++i)
    {
        if (pcm.channels[i] != session.inputs[i].numChannels()
            || pcm.strips[i].size() != size_t (pcm.frames) * size_t (pcm.channels[i])) return out;
        for (float sample : pcm.strips[i]) if (! std::isfinite (sample)) return out;
        session.inputs[i].inputA = deviceChannels++;
        session.inputs[i].inputB = pcm.channels[i] == 2 ? deviceChannels++ : -1;
    }
    if (deviceChannels > kMaxInputs) return out;
    for (int i = 0; i < parameters.numStrips; ++i)
        if (parameters.strips[size_t (i)].channel.replaceEnabled)
        {
            const auto* bank = banks ? banks->bank (roleFamily (session.inputs[size_t (i)].role), parameters.strips[size_t (i)].channel.replaceSound) : nullptr;
            if (! bank || bank->empty()) return out;
        }
    auto engine = std::make_unique<MixEngine>();
    engine->setSampleBanks (banks);
    engine->prepare (pcm.sampleRate, blockSize, session);
    engine->setParameters (parameters);
    // Monitoring and emergency keys are not programme processing. Measure the master tap,
    // which precedes output-feed levels/DIM/MUTE, preserving all actual mix mutes and routing.
    Tap tap;
    tap.prepare (pcm.sampleRate, engine->getGraph());
    for (int channels : pcm.channels)
    {
        auto a = std::make_unique<AnalysisAccumulator>();
        a->prepare (pcm.sampleRate, channels);
        tap.post.push_back (std::move (a));
        auto w = std::make_unique<AnalysisAccumulator>(); w->prepare (pcm.sampleRate, channels);
        tap.window.push_back (std::move (w)); tap.windowFrames.push_back (0); tap.windows.emplace_back();
    }
    std::array<AnalysisAccumulator, int (MixBus::Master)> groups;
    for (int b = 0; b < int (MixBus::Master); ++b)
        if (engine->isBusUsed (MixBus (b))) groups[size_t (b)].prepare (pcm.sampleRate, 2);
    std::array<std::vector<float>, 2> groupScratch { std::vector<float> (blockSize), std::vector<float> (blockSize) };
    float* groupPtrs[] { groupScratch[0].data(), groupScratch[1].data() };
    std::vector<std::vector<float>> input (size_t (deviceChannels), std::vector<float> (blockSize, 0.0f));
    std::vector<const float*> pointers (size_t (deviceChannels), nullptr);
    std::array<std::vector<float>, 2> output { std::vector<float> (blockSize), std::vector<float> (blockSize) };
    float* outputs[] { output[0].data(), output[1].data() };
    tap.windowSize = int (pcm.sampleRate * MixProfile::measuredTune().relationshipWindowSeconds);
    tap.start(); engine->setTap (&tap);
    for (int pos = 0; pos < pcm.frames; pos += blockSize)
    {
        if (cancelled (cancel)) return out;
        const int n = std::min (blockSize, pcm.frames - pos);
        int c = 0;
        for (size_t strip = 0; strip < pcm.strips.size(); ++strip)
            for (int channel = 0; channel < pcm.channels[strip]; ++channel, ++c)
            {
                for (int f = 0; f < n; ++f) input[size_t (c)][size_t (f)] = pcm.strips[strip][size_t ((pos + f) * pcm.channels[strip] + channel)];
                pointers[size_t (c)] = input[size_t (c)].data();
            }
        engine->process (pointers.data(), deviceChannels, outputs, 2, n);
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            const auto bus = MixBus (b);
            if (! engine->isBusUsed (bus)) continue;
            const float gain = engine->busFaderGain (bus);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < n; ++f) groupPtrs[c][f] = engine->busOutput (bus, c)[f] * gain;
            groups[size_t (b)].consume (AudioBlockView { groupPtrs, 2, n });
        }
    }
    engine->setTap (nullptr);
    out.capture = tap.finish();
    for (auto& a : tap.post) out.processed.push_back (a->finalise());
    for (int b = 0; b < int (MixBus::Master); ++b)
        if (engine->isBusUsed (MixBus (b))) out.busOutputs[size_t (b)] = groups[size_t (b)].finalise();
    out.windows = std::move (tap.windows);
    out.nonFiniteBlocks = engine->getNonFiniteBlocks();
    out.clampedBlocks = engine->getClampedOutputBlocks();
    out.valid = out.capture.valid && out.capture.masterOutput.valid;
    return out;
}
Verification verify (const MixPlanContext& ctx, const MixParameters& p, const Render& rendered)
{
    Verification v;
    if (! rendered.valid) return v;
    v.available = true;
    const auto& m = rendered.capture.masterOutput;
    auto t = Profiles::targets (ctx.session.profile, masterRoleFor (ctx.session.purpose));
    if (ctx.reference.valid)
    {
        ReferenceMatch match;
        t = Reference::targets (t, ctx.reference, rendered.capture.buses[size_t (MixBus::Master)], ctx.session.profile, match);
    }
    const auto& limits = MixProfile::measuredTune();
    v.safe = rendered.nonFiniteBlocks == 0 && rendered.clampedBlocks == 0
        && std::isfinite (m.truePeakDb) && std::isfinite (loudness (m)) && m.clipCount == 0
        && m.truePeakDb <= std::min (-0.3f, p.master().channel.limiterCeilingDb) + limits.truePeakToleranceDb
        && m.stereoCorrelation >= limits.minCorrelation;
    if (! v.safe) v.warnings.push_back ("Rendered master failed peak, finite-output or mono-safety checks.");
    v.loudnessErrorLu = std::fabs (loudness (m) - target (ctx.session));
    for (size_t b = 0; b < m.bandEnergyDb.size(); ++b)
        v.spectralErrorDb += std::max (0.0f, std::fabs (m.bandEnergyDb[b] - t.bandTargetDb[b]) - t.bandToleranceDb[b]);
    v.spectralErrorDb /= float (m.bandEnergyDb.size());
    v.relationships = RelationshipEngine::measureRendered (ctx, p, rendered.processed, &rendered.windows);
    const int focus = MixPlanner::focalStrip (ctx);
    if (focus >= 0)
    {
        const auto ref = ctx.graph.strips[size_t (focus)].bus;
        const auto& voice = rendered.busOutputs[size_t (ref)];
        const auto& relationship = MixProfile::relationships (ctx.session.profile);
        if (voice.valid && voice.activeRmsDb > -70)
            for (int b = 0; b < int (MixBus::Master); ++b)
            {
                const auto& group = rendered.busOutputs[size_t (b)];
                if (MixBus (b) == ref || ! group.valid || group.activeRmsDb <= -70) continue;
                MixRelationship r; r.kind = MixBus (b) == MixBus::Vocals ? MixRelationKind::LeadAndBacking : MixRelationKind::LeadAndMusic;
                r.metric = "rendered_bus_over_focus_db"; r.busA = b; r.busB = int (ref);
                r.nameA = mixBusName (MixBus (b)); r.nameB = mixBusName (ref);
                r.value = group.activeRmsDb - voice.activeRmsDb;
                r.tolerance = relationship.busBelowVocalsDb[size_t (b)] + relationship.busBalanceToleranceDb;
                r.concern = r.value > r.tolerance;
                r.headline = r.nameA + " against " + r.nameB + ": measured after group processing (before speech priority).";
                v.relationships.push_back (std::move (r));
            }
    }
    for (const auto& r : v.relationships) if (r.concern) v.relationshipPenaltyDb += std::max (0.0f, r.value - r.tolerance);
    const float dynamics = std::max (0.0f, t.crestFactorMinDb - m.crestFactorDb);
    v.score = v.loudnessErrorLu + v.spectralErrorDb + v.relationshipPenaltyDb + dynamics;
    if (dynamics > 0) v.warnings.push_back ("Rendered dynamics are below the profile's minimum crest factor.");
    if (v.loudnessErrorLu > t.loudnessToleranceLu) v.warnings.push_back ("Rendered loudness is outside delivery tolerance.");
    if (v.spectralErrorDb > 0) v.warnings.push_back ("Rendered spectral balance is outside profile tolerance.");
    if (v.relationshipPenaltyDb > 0) v.warnings.push_back ("Measured source masking or hierarchy still needs attention.");
    return v;
}
Result plan (const MixPlanContext& ctx, const SampleBankTable* banks, const std::atomic<bool>* cancel,
             const MixPlanner::PlanSelection* selection)
{
    auto fallbackPlan = [&]
    {
        auto seed = MixPlanner::plan (ctx);
        return selection && ! seed.refused ? MixPlanner::restrictTo (seed, *selection, ctx.graph, ctx.session.profile) : seed;
    };
    if (! ctx.capture.replay) { Result r; r.plan = fallbackPlan(); return r; }
    auto anchored = ctx;
    anchored.current = ctx.atCapture;
    auto seed = MixPlanner::plan (anchored);
    seed.before = ctx.current;
    if (seed.refused) seed.proposed = ctx.current;
    if (selection && ! seed.refused) seed = MixPlanner::restrictTo (seed, *selection, ctx.graph, ctx.session.profile);
    auto result = refine (anchored, seed, MixProfile::measuredTune().maxPasses, banks, cancel, selection);
    if (result.fallback) { result.plan = fallbackPlan(); return result; }
    result.plan.before = ctx.current;
    result.plan.noChangeRequired = MixPlanner::countParameterChanges (ctx.current, result.plan.proposed) == 0;
    if (! result.plan.refused) result.plan.headline = result.plan.noChangeRequired ? "MIX: NO CHANGE REQUIRED" : "MIX TUNED";
    MixPlanner::refreshSummary (result.plan);
    return result;
}

Result refine (const MixPlanContext& ctx, const MixPlan& baseline, int maxPasses,
               const SampleBankTable* banks, const std::atomic<bool>* cancel,
               const MixPlanner::PlanSelection* selection)
{
    Result out; out.plan = baseline;
    if (! baseline.valid || ! ctx.capture.replay || cancelled (cancel)) return out;
    const auto& pcm = *ctx.capture.replay;
    out.before = render (ctx.session, baseline.before, pcm, banks, cancel); ++out.renders;
    out.initial = render (ctx.session, baseline.proposed, pcm, banks, cancel); ++out.renders;
    if (! out.before.valid || ! out.initial.valid || cancelled (cancel)) return out;
    out.after = out.initial;
    auto best = verify (ctx, baseline.proposed, out.after);
    if (baseline.refused)
    {
        out.fallback = false; out.verification = best;
        return out; // retain the planner's refusal and exact BEFORE; no corrective passes
    }
    const auto& limits = MixProfile::measuredTune();
    const auto& bounds = MixProfile::aiBounds();
    const auto& relationships = MixProfile::relationships (ctx.session.profile);
    auto proposed = baseline.proposed;
    for (int pass = 0; pass < std::clamp (maxPasses, 0, limits.maxPasses) && ! cancelled (cancel); ++pass)
    {
        auto candidate = proposed;
        // Only cut competitors, never boost the focal source into a limiter. One move per
        // competitor per pass, bounded against the initial proposal rather than cumulatively.
        std::array<bool, kMaxStrips> moved {};
        for (const auto& r : best.relationships)
        {
            if (r.concern && r.busA >= 0 && r.busA < int (MixBus::Master))
            {
                const auto b = size_t (r.busA);
                candidate.buses[b].faderDb = std::max (baseline.proposed.buses[b].faderDb - bounds.maxFaderMoveDb,
                    candidate.buses[b].faderDb - std::min (limits.maxCompetitorCutPerPassDb, r.value - r.tolerance));
                continue;
            }
            const int i = r.stripA;
            if (! r.concern || i < 0 || moved[size_t (i)] || size_t (i) >= baseline.strips.size()
                || ! baseline.strips[size_t (i)].heard || baseline.strips[size_t (i)].bleedOnly
                || candidate.strips[size_t (i)].mute || candidate.strips[size_t (i)].linkGroup != 0) continue;
            moved[size_t (i)] = true;
            const float cut = std::min (limits.maxCompetitorCutPerPassDb, r.value - r.tolerance);
            auto& strip = candidate.strips[size_t (i)];
            strip.faderDb = std::max (baseline.proposed.strips[size_t (i)].faderDb - bounds.maxFaderMoveDb, strip.faderDb - cut);
            // Make a small spectral pocket only on music competing with a voice. Reuse the
            // profile's designated presence band; don't manufacture a second EQ chain.
            if (r.kind == MixRelationKind::LeadAndMusic && ctx.graph.strips[size_t (i)].bus == MixBus::Music)
            {
                auto& eq = strip.channel.toneBands[2];
                if ((! eq.enabled || (eq.type == FilterType::Peak && std::fabs (eq.freqHz - relationships.vocalPocketHz) < limits.presenceBandReuseHz))
                    && eq.gainDb > bounds.minEqGainDb)
                {
                    strip.channel.toneEqEnabled = true; eq.enabled = true; eq.type = FilterType::Peak;
                    eq.freqHz = relationships.vocalPocketHz; eq.q = relationships.vocalPocketQ;
                    eq.gainDb = std::max ({ bounds.minEqGainDb, baseline.proposed.strips[size_t (i)].channel.toneBands[2].gainDb - bounds.maxEqDeltaDb, eq.gainDb - cut });
                }
            }
        }
        auto& master = candidate.master().channel;
        const float error = baseline.buses[size_t (MixBus::Master)].used
                            ? target (ctx.session) - loudness (out.after.capture.masterOutput) : 0.0f;
        // Positive lift limited by measured true-peak room; a deficit that needs squashing
        // stays a warning. This is not an invitation to increase compressor or limiter GR.
        const float room = std::max (0.0f, master.limiterCeilingDb - out.after.capture.masterOutput.truePeakDb - bounds.minMasterHeadroomDb);
        const float move = std::clamp (error, -limits.maxMasterStepDb, std::min (limits.maxMasterStepDb, room));
        master.outputTrimDb = std::clamp (master.outputTrimDb + move, baseline.proposed.master().channel.outputTrimDb - limits.maxMasterTotalDb,
                                        baseline.proposed.master().channel.outputTrimDb + limits.maxMasterTotalDb);
        // Refit group dynamics from what their actual proposed inputs received. Strategies
        // and their profile bounds are shared with the first deterministic plan.
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            if (! baseline.buses[size_t (b)].used) continue;
            TuneContext tune;
            tune.analysis = out.after.capture.buses[size_t (b)];
            tune.profile = ctx.session.profile;
            tune.role = busRole (MixBus (b), ctx.session.purpose);
            tune.current = proposed.buses[size_t (b)].channel;
            candidate.buses[size_t (b)].channel = TuneEngine::tune (tune).proposed;
        }
        // Standard safety owns every DSP write. Tighter measured-pass bounds above own
        // distance from the seed, while ParameterSpecs own legal natural-unit values.
        auto safeChannel = [] (const ChannelParameters& from, const ChannelParameters& to)
        {
            RecommendationResult requested; requested.valid = true;
            Recommendation change; change.what = "Measured offline correction";
            change.changes = diffParameters (from, to); requested.items.push_back (change);
            const auto checked = SafetyValidator::validate (requested, false);
            std::vector<ParameterChange> accepted;
            for (const auto& item : checked.items) accepted.insert (accepted.end(), item.changes.begin(), item.changes.end());
            return applyChanges (from, accepted);
        };
        for (int i = 0; i < candidate.numStrips; ++i)
            candidate.strips[size_t (i)].channel = safeChannel (proposed.strips[size_t (i)].channel, candidate.strips[size_t (i)].channel);
        for (size_t b = 0; b < candidate.buses.size(); ++b)
            candidate.buses[b].channel = safeChannel (proposed.buses[b].channel, candidate.buses[b].channel);
        if (selection)
        {
            auto narrowed = baseline; narrowed.proposed = candidate;
            candidate = MixPlanner::restrictTo (narrowed, *selection, ctx.graph, ctx.session.profile).proposed;
        }
        if (MixPlanner::countParameterChanges (candidate, proposed) == 0) break;
        auto audio = render (ctx.session, candidate, pcm, banks, cancel); ++out.renders;
        auto checked = verify (ctx, candidate, audio);
        // No component may regress appreciably to buy an aggregate score improvement.
        if (! checked.available || ! checked.safe || checked.score >= best.score - limits.minScoreImprovement
            || checked.spectralErrorDb > best.spectralErrorDb + limits.maxSpectralRegressionDb
            || checked.relationshipPenaltyDb > best.relationshipPenaltyDb + limits.maxRelationshipRegressionDb
            || checked.loudnessErrorLu > best.loudnessErrorLu + limits.maxLoudnessRegressionLu
            || audio.capture.masterOutput.crestFactorDb < out.after.capture.masterOutput.crestFactorDb - limits.maxCrestLossDb) break;
        proposed = candidate; best = checked; out.after = std::move (audio); ++out.acceptedPasses;
    }
    if (cancelled (cancel)) { Result stopped; stopped.plan = baseline; return stopped; }
    out.fallback = false;
    if (! best.safe)
    {
        // A measured unsafe candidate is never presented as a verified result.
        proposed = baseline.before; out.after = out.before; best = verify (ctx, proposed, out.after);
        out.plan.notes.push_back ("Proposed render failed safety checks; the existing mix was retained.");
    }
    out.plan.proposed = proposed;
    out.verification = best;
    auto correction = [] (TuneResult& tune, const ChannelParameters& initial, const ChannelParameters& final)
    {
        auto changes = diffParameters (initial, final);
        if (changes.empty()) return;
        Recommendation item; item.what = "Corrected after measuring the proposed mix";
        item.why = "The same performance was rendered through the proposed chain; this correction improved its measured result.";
        item.changes = std::move (changes); tune.report.items.push_back (std::move (item));
    };
    for (auto& strip : out.plan.strips)
        correction (strip.tune, baseline.proposed.strips[size_t (strip.strip)].channel, proposed.strips[size_t (strip.strip)].channel);
    for (size_t b = 0; b < out.plan.buses.size(); ++b)
        correction (out.plan.buses[b].tune, baseline.proposed.buses[b].channel, proposed.buses[b].channel);
    MixPlanner::refreshSummary (out.plan);
    out.plan.headline = out.plan.noChangeRequired ? "MIX: NO CHANGE REQUIRED" : "MIX TUNED";
    out.plan.notes.push_back ("Measured the proposed chain on " + std::to_string (pcm.frames / pcm.sampleRate)
                              + " seconds of the same performance; accepted " + std::to_string (out.acceptedPasses) + " bounded corrections.");
    for (const auto& warning : best.warnings) out.plan.notes.push_back (warning);
    return out;
}
}
