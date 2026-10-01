#include "TestFramework.h"
#include "Mix/MeasuredMix.h"
#include "Profiles/MixProfileData.h"
#include <limits>

using namespace livemix;
namespace
{
struct Fixture
{
    MixPlanContext ctx;
    std::shared_ptr<MixCapture::Replay> pcm = std::make_shared<MixCapture::Replay>();
    Fixture()
    {
        ctx.session.inputs = {{ "Lead", ChannelRole::LeadVocal, 0, -1 }, { "Keys", ChannelRole::Piano, 1, -1 }};
        ctx.session.setFocus (0);
        ctx.graph = RoutingGraph::build (ctx.session);
        ctx.current = startingPoint (ctx.session, ctx.graph);
        ctx.atCapture = ctx.current;
        pcm->sampleRate = 48000; pcm->frames = 48000; pcm->channels = {1,1};
        pcm->strips.resize (2, std::vector<float> (size_t (pcm->frames)));
        for (int i = 0; i < pcm->frames; ++i)
        {
            const float t = float (i) / 48000;
            const float env = 0.2f + 0.8f * std::fabs (std::sin (t * 7));
            pcm->strips[0][size_t (i)] = 0.12f * env * (std::sin (t * 2 * 3.14159265f * 220) + 0.3f * std::sin (t * 2 * 3.14159265f * 2700));
            pcm->strips[1][size_t (i)] = 0.12f * env * std::sin (t * 2 * 3.14159265f * 2700);
        }
        auto before = MeasuredMix::render (ctx.session, ctx.current, *pcm);
        ctx.capture = before.capture; ctx.capture.replay = pcm;
    }
};
}
TEST_CASE ("MeasuredMix: fresh engine renders repeatably, gain is applied once, nonlinear decisions are measured")
{
    Fixture f;
    auto neutral = f.ctx.current;
    for (int i = 0; i < neutral.numStrips; ++i) { neutral.strips[size_t (i)].channel = ChannelParameters {}; neutral.strips[size_t (i)].sendDb.fill (kSilenceDb); }
    for (auto& b : neutral.buses) b.channel = ChannelParameters {};
    auto a = MeasuredMix::render (f.ctx.session, neutral, *f.pcm);
    auto b = MeasuredMix::render (f.ctx.session, neutral, *f.pcm);
    REQUIRE (a.valid); REQUIRE (b.valid);
    CHECK_NEAR (a.capture.masterOutput.rmsDb, b.capture.masterOutput.rmsDb, 0);
    neutral.strips[0].inputGainDb = 6;
    auto gained = MeasuredMix::render (f.ctx.session, neutral, *f.pcm);
    CHECK_NEAR (gained.processed[0].rmsDb - a.processed[0].rmsDb, 6, 0.02);
    neutral.strips[0].channel.compEnabled = true;
    neutral.strips[0].channel.compThresholdDb = -40;
    neutral.strips[0].channel.compRatio = 8;
    auto compressed = MeasuredMix::render (f.ctx.session, neutral, *f.pcm);
    CHECK (compressed.processed[0].rmsDb < gained.processed[0].rmsDb - 3);
    neutral.strips[0].channel.compEnabled = false;
    neutral.strips[0].channel.satEnabled = true; neutral.strips[0].channel.satDrive = 0.5;
    auto saturated = MeasuredMix::render (f.ctx.session, neutral, *f.pcm);
    CHECK (std::fabs (saturated.processed[0].rmsDb - gained.processed[0].rmsDb) > 0.01);
}
TEST_CASE ("MeasuredMix: missing malformed nonfinite and missing-sample audio preserve fallback")
{
    Fixture f;
    const auto seed = MixPlanner::plan (f.ctx);
    f.ctx.capture.replay.reset();
    auto missing = MeasuredMix::refine (f.ctx, seed);
    CHECK (missing.fallback); CHECK (missing.renders == 0);
    CHECK (MixPlanner::countParameterChanges (missing.plan.proposed, seed.proposed) == 0);
    f.pcm->strips[0][0] = std::numeric_limits<float>::quiet_NaN();
    CHECK (! MeasuredMix::render (f.ctx.session, seed.proposed, *f.pcm).valid);
    f.pcm->strips[0][0] = 0;
    auto sample = seed.proposed; sample.strips[0].channel.replaceEnabled = true;
    CHECK (! MeasuredMix::render (f.ctx.session, sample, *f.pcm).valid);
    f.pcm->strips[1].pop_back();
    CHECK (! MeasuredMix::render (f.ctx.session, seed.proposed, *f.pcm).valid);
}
TEST_CASE ("MeasuredMix: bounded refinement is repeatable and cancellation never changes the proposal")
{
    Fixture f;
    auto seed = MixPlanner::plan (f.ctx);
    REQUIRE (seed.valid);
    auto a = MeasuredMix::refine (f.ctx, seed, 99);
    auto b = MeasuredMix::refine (f.ctx, seed, 99);
    REQUIRE (! a.fallback);
    CHECK (a.renders <= 2 + MixProfile::measuredTune().maxPasses);
    CHECK (MixPlanner::countParameterChanges (a.plan.proposed, b.plan.proposed) == 0);
    const auto initial = MeasuredMix::verify (f.ctx, seed.proposed, a.initial);
    if (a.acceptedPasses > 0) { CHECK (a.verification.safe); CHECK (a.verification.score < initial.score); }
    std::atomic<bool> stop {true};
    auto cancelled = MeasuredMix::refine (f.ctx, seed, 3, nullptr, &stop);
    CHECK (cancelled.fallback); CHECK (cancelled.renders == 0);
    CHECK (MixPlanner::countParameterChanges (cancelled.plan.proposed, seed.proposed) == 0);
}
TEST_CASE ("MeasuredMix: relationships use processed spectra and actual bus faders, and exclude mutes")
{
    Fixture f;
    std::vector<AnalysisResult> post (2);
    for (auto& a : post) { a.valid = true; a.peakDb = -10; a.rmsDb = -20; a.activeRmsDb = -20; a.bandEnergyDb.fill (-20); }
    post[1].bandEnergyDb[size_t (Band::UpperMid)] = -1;
    auto p = f.ctx.current;
    p.strips[0].faderDb = p.strips[1].faderDb = 0;
    for (auto& bus : p.buses) bus.faderDb = 0;
    auto r = RelationshipEngine::measureRendered (f.ctx, p, post);
    REQUIRE (r.size() == 1); CHECK (r[0].concern); CHECK_NEAR (r[0].value, 19, 0.01);
    p.buses[size_t (MixBus::Music)].faderDb = -20;
    r = RelationshipEngine::measureRendered (f.ctx, p, post);
    CHECK (! r[0].concern);
    p.strips[1].mute = true;
    CHECK (RelationshipEngine::measureRendered (f.ctx, p, post).empty());
}
TEST_CASE ("MeasuredMix: unsafe true peak is refused despite finite sample output")
{
    Fixture f;
    auto audio = MeasuredMix::render (f.ctx.session, f.ctx.current, *f.pcm);
    REQUIRE (audio.valid);
    audio.capture.masterOutput.truePeakDb = 0.1f;
    auto checked = MeasuredMix::verify (f.ctx, f.ctx.current, audio);
    CHECK (checked.available); CHECK (! checked.safe);
    CHECK (! checked.warnings.empty());
}

TEST_CASE ("MeasuredMix: re-planning the same capture after KEEP is idempotent")
{
    Fixture f;
    const auto first = MeasuredMix::plan (f.ctx);
    REQUIRE (! first.fallback);
    f.ctx.current = first.plan.proposed;
    const auto kept = MeasuredMix::plan (f.ctx);
    CHECK (MixPlanner::countParameterChanges (first.plan.proposed, kept.plan.proposed) == 0);
    CHECK (kept.plan.noChangeRequired);
}
TEST_CASE ("MeasuredMix: alternating sources are not reported as simultaneously masking")
{
    Fixture f;
    std::vector<AnalysisResult> post (2);
    for (auto& a : post) { a.valid = true; a.peakDb = -10; a.rmsDb = a.activeRmsDb = -20; a.bandEnergyDb.fill (-10); }
    auto windows = std::vector<std::vector<AnalysisResult>> (2, std::vector<AnalysisResult> (2, post[0]));
    windows[0][1].rmsDb = -100; windows[1][0].rmsDb = -100;
    auto p = f.ctx.current;
    CHECK (RelationshipEngine::measureRendered (f.ctx, p, post, &windows).empty());
    f.ctx.session.inputs[1].role = ChannelRole::BackingVocal;
    f.ctx.graph = RoutingGraph::build (f.ctx.session);
    CHECK (RelationshipEngine::measureRendered (f.ctx, p, post, &windows).empty());
}
TEST_CASE ("MeasuredMix: an over-loud backing voice improves objectively within seed bounds")
{
    Fixture f;
    f.ctx.session.inputs[1].role = ChannelRole::BackingVocal;
    f.ctx.graph = RoutingGraph::build (f.ctx.session);
    f.pcm->strips[1] = f.pcm->strips[0];
    auto p = startingPoint (f.ctx.session, f.ctx.graph);
    for (int i = 0; i < p.numStrips; ++i)
    {
        p.strips[size_t (i)].channel = ChannelParameters {};
        p.strips[size_t (i)].sendDb.fill (kSilenceDb);
    }
    for (auto& bus : p.buses) bus.channel = ChannelParameters {};
    p.master().channel.limiterEnabled = true; p.master().channel.limiterCeilingDb = -1;
    p.strips[1].faderDb = 5;
    f.ctx.current = p; f.ctx.atCapture = p;
    f.ctx.capture = MeasuredMix::render (f.ctx.session, p, *f.pcm).capture;
    f.ctx.capture.replay = f.pcm;
    MixPlan seed; seed.valid = true; seed.before = p; seed.proposed = p;
    seed.strips.resize (2);
    for (int i = 0; i < 2; ++i) { seed.strips[size_t (i)].strip = i; seed.strips[size_t (i)].heard = true; }
    seed.buses[size_t (MixBus::Master)].used = true;
    auto result = MeasuredMix::refine (f.ctx, seed);
    REQUIRE (! result.fallback);
    const auto initial = MeasuredMix::verify (f.ctx, p, result.initial);
    CHECK (result.acceptedPasses > 0);
    CHECK (result.verification.safe);
    CHECK (result.verification.relationshipPenaltyDb < initial.relationshipPenaltyDb);
    CHECK (result.verification.score < initial.score);
    CHECK (result.plan.proposed.strips[1].faderDb >= p.strips[1].faderDb - MixProfile::aiBounds().maxFaderMoveDb);
    std::printf ("    backing test: score %.2f -> %.2f, relationship penalty %.2f -> %.2f, accepted %d\n",
        double (initial.score), double (result.verification.score), double (initial.relationshipPenaltyDb),
        double (result.verification.relationshipPenaltyDb), result.acceptedPasses);
}
TEST_CASE ("MeasuredMix: partial selection preserves every unselected source and master")
{
    Fixture f;
    auto selection = MixPlanner::PlanSelection::none(); selection.strips[1] = true;
    auto result = MeasuredMix::plan (f.ctx, nullptr, nullptr, &selection);
    CHECK (diffParameters (f.ctx.current.strips[0].channel, result.plan.proposed.strips[0].channel).empty());
    CHECK_NEAR (f.ctx.current.strips[0].faderDb, result.plan.proposed.strips[0].faderDb, 0);
    for (size_t b = 0; b < f.ctx.current.buses.size(); ++b)
    {
        CHECK (diffParameters (f.ctx.current.buses[b].channel, result.plan.proposed.buses[b].channel).empty());
        CHECK_NEAR (f.ctx.current.buses[b].faderDb, result.plan.proposed.buses[b].faderDb, 0);
    }
}

TEST_CASE ("MeasuredMix: disabled assignments do not corrupt stereo replay mapping")
{
    Fixture f;
    f.ctx.session.inputs.insert (f.ctx.session.inputs.begin(), {"Disabled", ChannelRole::Speech, 7, -1, false});
    const auto audio = MeasuredMix::render (f.ctx.session, f.ctx.current, *f.pcm);
    REQUIRE (audio.valid);
    CHECK (audio.processed.size() == 2);
}

TEST_CASE ("MeasuredMix: silent and steady fault listens retain the planner refusal")
{
    for (bool silent : {true, false})
    {
        Fixture f;
        for (size_t c = 0; c < f.pcm->strips.size(); ++c)
            for (int i = 0; i < f.pcm->frames; ++i)
                f.pcm->strips[c][size_t (i)] = silent ? 0.0f : 0.12f * std::sin (float (i) * 2 * 3.14159265f * 220 / 48000);
        f.ctx.capture = MeasuredMix::render (f.ctx.session, f.ctx.current, *f.pcm).capture;
        f.ctx.capture.replay = f.pcm;
        const auto seed = MixPlanner::plan (f.ctx);
        REQUIRE (seed.refused);
        const auto selection = MixPlanner::PlanSelection::all (f.ctx.graph.numStrips());
        const auto measured = MeasuredMix::plan (f.ctx, nullptr, nullptr, &selection);
        CHECK (measured.plan.refused);
        CHECK (measured.plan.headline == seed.headline);
        CHECK (measured.acceptedPasses == 0);
        CHECK (MixPlanner::countParameterChanges (f.ctx.current, measured.plan.proposed) == 0);
    }
}
