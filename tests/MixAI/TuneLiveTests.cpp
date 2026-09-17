#include "TestFramework.h"
#include "TestSignals.h"
#include "Core/Json.h"
#include "Mix/MixEngine.h"
#include "Mix/MixPlanner.h"
#include "Mix/OfflineCapture.h"
#include "MixAI/CapabilityResolver.h"
#include "MixAI/DspCapabilityRegistry.h"
#include "MixAI/MixContext.h"
#include "MixAI/MixIntent.h"
#include "MixAI/MixReasoningProvider.h"
#include "MixAI/MixRequestParser.h"
#include "MixAI/MixSafetyValidator.h"
#include "MixAI/RelationshipEngine.h"
#include "Profiles/MixProfileData.h"
#include "MixAI/TuneLiveCoordinator.h"
#include <chrono>
#include <random>
#include <thread>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    MixSession band()
    {
        MixSession s;
        s.profile = StyleProfileId::ModernGospel;
        s.purpose = MixPurpose::ChurchBroadcast;
        s.inputs = {
            { "Kick",   ChannelRole::KickIn,        0, -1 },
            { "Snare",  ChannelRole::SnareTop,      1, -1 },
            { "OH",     ChannelRole::Overhead,      2,  3 },
            { "Bass",   ChannelRole::BassDI,        4, -1 },
            { "Keys",   ChannelRole::Piano,         5,  6 },
            { "Organ",  ChannelRole::Organ,         7, -1 },
            { "Lead",   ChannelRole::LeadVocal,     8, -1 },
            { "Vox 1",  ChannelRole::BackingVocal,  9, -1 },
            { "Vox 2",  ChannelRole::BackingVocal, 10, -1 },
            { "Pastor", ChannelRole::Speech,       11, -1 },
        };
        return s;
    }

    void sine (std::vector<float>& c, float hz, float amp)
    {
        for (size_t i = 0; i < c.size(); ++i)
            c[i] += amp * std::sin (2.0f * float (M_PI) * hz * float (i) / float (kSr));
    }
    void bursts (std::vector<float>& c, float hz, float amp, float periodS, float lengthS, float phaseS, bool isNoise, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float t = float (i) / float (kSr);
            const float inPeriod = std::fmod (t + periodS - phaseS, periodS);
            if (inPeriod >= lengthS) continue;
            const float env = 1.0f - inPeriod / lengthS;
            c[i] += amp * env * (isNoise ? dist (rng) : std::sin (2.0f * float (M_PI) * hz * t));
        }
    }
    void noiseInto (std::vector<float>& c, float amp, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (auto& x : c) x += amp * dist (rng);
    }

    // Eight seconds of a band whose keys and organ are deliberately loud in the presence band,
    // so the lead vocal is covered by them: the relationship the whole feature exists to solve.
    testsig::Buffer bandAudio()
    {
        testsig::Buffer in (12, int (kSr * 8));
        bursts (in.data[0], 100.0f, 0.7f, 0.5f, 0.12f, 0.0f, false, 1);
        bursts (in.data[1], 0.0f, 0.5f, 0.5f, 0.05f, 0.25f, true, 2);
        noiseInto (in.data[2], 0.05f, 7); noiseInto (in.data[3], 0.05f, 8);
        sine (in.data[4], 45.0f, 0.8f);                                       // bass, sub-heavy
        sine (in.data[5], 262.0f, 0.12f); sine (in.data[5], 3000.0f, 0.42f);  // keys: hard in the vocal's band
        sine (in.data[6], 330.0f, 0.12f); sine (in.data[6], 3200.0f, 0.42f);
        sine (in.data[7], 196.0f, 0.15f); sine (in.data[7], 2900.0f, 0.35f);  // organ: the same
        sine (in.data[8], 220.0f, 0.30f); sine (in.data[8], 440.0f, 0.10f);   // lead: warm, little presence
        sine (in.data[9], 330.0f, 0.22f); sine (in.data[10], 392.0f, 0.22f);
        noiseInto (in.data[11], 0.02f, 10);                                   // pastor mic: spill only
        return in;
    }

    struct Rig
    {
        MixEngine engine;
        OfflineCapture capture;
        MixSession session;
        explicit Rig (const MixSession& s) : session (s)
        {
            engine.prepare (kSr, 128, session);
            capture.prepare (kSr, engine.getGraph());
            engine.setTap (&capture);
        }
        MixCapture::Result listen (testsig::Buffer& in)
        {
            std::vector<const float*> ip (in.ptrs.size());
            std::vector<float> l (128), r (128);
            float* op[2] = { l.data(), r.data() };
            capture.start();
            for (int i = 0; i + 128 <= in.numSamples(); i += 128)
            {
                for (size_t c = 0; c < ip.size(); ++c) ip[c] = in.ptrs[c] + i;
                engine.process (ip.data(), int (ip.size()), op, 2, 128);
            }
            return capture.finish();
        }
        MixPlanContext context (const MixCapture::Result& cap)
        {
            MixPlanContext ctx;
            ctx.session = session;
            ctx.graph = engine.getGraph();
            ctx.current = engine.getAppliedParameters();
            ctx.atCapture = ctx.current;
            ctx.capture = cap;
            return ctx;
        }
    };

    int stripNamed (const MixPlanContext& ctx, const char* name)
    {
        for (int i = 0; i < ctx.graph.numStrips(); ++i) if (ctx.graph.strips[size_t (i)].name == name) return i;
        return -1;
    }

    bool relationSeen (const std::vector<MixRelationship>& rs, const char* metric)
    {
        for (const auto& r : rs) if (r.metric == metric) return true;
        return false;
    }

    // Drives the coordinator to Ready without a device: the same listen is offered as both the
    // initial and the verify capture, which is exactly what a re-listen on an unchanged band gives.
    void pump (TuneLiveCoordinator& c)
    {
        for (int i = 0; i < 2000 && c.isWaitingOnProvider(); ++i)
        {
            c.poll();
            if (c.isWaitingOnProvider()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        c.poll();
    }
}

// ---------------------------------------------------------------------------
// JSON: the serialisation every schema depends on
// ---------------------------------------------------------------------------
TEST_CASE ("Json: writes, reads back and keeps key order")
{
    auto o = json::Value::object();
    o.set ("schemaVersion", 1);
    o.set ("name", std::string ("Lead \"Vocal\"\n"));
    o.set ("db", -2.5f);
    auto arr = json::Value::array();
    arr.add (1).add (2).add (json::Value::object().set ("x", true));
    o.set ("items", arr);

    const auto text = o.write();
    CHECK (text.find ("\"schemaVersion\":1") != std::string::npos);
    CHECK (text.find ("\\\"Vocal\\\"") != std::string::npos);

    std::string error;
    const auto back = json::parse (text, &error);
    CHECK (error.empty());
    CHECK (back["schemaVersion"].asInt() == 1);
    CHECK_NEAR (back["db"].asFloat(), -2.5f, 1.0e-6f);
    CHECK (back["items"].size() == 3);
    CHECK (back["items"][2]["x"].asBool());
    CHECK (back.keyAt (0) == "schemaVersion");      // stable bytes for the same document

    // A reply wrapped in prose or a fence is still usable; the numbers still come from the object.
    std::string inner;
    CHECK (json::extractObject ("Here you go:\n```json\n{\"a\":{\"b\":1}}\n```\nhope that helps", inner));
    CHECK (json::parse (inner)["a"]["b"].asInt() == 1);

    // Malformed input is an ordinary outcome, not a crash and not a guess.
    CHECK (json::parse ("{\"a\":", &error).isNull());
    CHECK (! error.empty());
}

// ---------------------------------------------------------------------------
// The capability registry is the engine's own truth
// ---------------------------------------------------------------------------
TEST_CASE ("DspCapabilityRegistry: reports what MixEngine really configures, and says why not")
{
    Rig rig (band());
    const auto registry = DspCapabilityRegistry::build (rig.session, rig.engine.getGraph());

    const MixTargetRef lead { MixTargetKind::Strip, 6 };
    const MixTargetRef keys { MixTargetKind::Strip, 4 };
    const MixTargetRef master { MixTargetKind::Bus, int (MixBus::Master) };
    const MixTargetRef vocals { MixTargetKind::Bus, int (MixBus::Vocals) };

    REQUIRE (registry.find (lead) != nullptr);
    CHECK (registry.find (lead)->name == "Lead");

    // MixEngine turns the limiter stage on for the master and nowhere else, so that is what the
    // registry says - and it says it with a reason rather than leaving the processor missing.
    CHECK (registry.supports (master, DspProcessor::Limiter));
    CHECK (! registry.supports (lead, DspProcessor::Limiter));
    REQUIRE (registry.find (lead, DspProcessor::Limiter) != nullptr);
    CHECK (! registry.find (lead, DspProcessor::Limiter)->unavailableBecause.empty());

    // A mono source has no stereo image to widen, and a vocal bus is not a kind of source
    // DLIVE fits a width stage to at all. Both are reported with a reason rather than left
    // out of the table, so nothing keeps asking for a processor that is not coming.
    CHECK (! registry.supports (lead, DspProcessor::Width));
    CHECK (registry.supports (keys, DspProcessor::Width));
    CHECK (! registry.supports (vocals, DspProcessor::Width));
    REQUIRE (registry.find (vocals, DspProcessor::Width) != nullptr);
    CHECK (! registry.find (vocals, DspProcessor::Width)->unavailableBecause.empty());
    CHECK (registry.find (lead, DspProcessor::Width)->unavailableBecause.find ("mono") != std::string::npos);

    // Ranges are the engine's own, not a second copy: the compressor's threshold range here is
    // the same table the plug-in and the safety validator read.
    const auto* threshold = registry.findParameter (lead, DspProcessor::Compressor, "compThreshold");
    REQUIRE (threshold != nullptr);
    const ParameterSpec* spec = findParameterSpec ("compThreshold");
    REQUIRE (spec != nullptr);
    CHECK_NEAR (threshold->minValue, spec->minValue, 1.0e-6f);
    CHECK_NEAR (threshold->maxValue, spec->maxValue, 1.0e-6f);

    // Nothing invented: a processor DLIVE does not have is simply not in the table.
    CHECK (registry.find (lead, DspProcessor::Count) == nullptr);
    CHECK (dspProcessorFromId ("springReverb") == DspProcessor::Count);
}

// ---------------------------------------------------------------------------
// Relationships are measured, not decided
// ---------------------------------------------------------------------------
TEST_CASE ("RelationshipEngine: finds what is covering the lead vocal and who owns the low end")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);

    const auto relations = RelationshipEngine::measure (ctx);
    CHECK (relationSeen (relations, "sub_overlap_db"));
    CHECK (relationSeen (relations, "presence_masking_db"));
    CHECK (relationSeen (relations, "master_true_peak_db"));

    // The keys and the organ were written to sit hard in the vocal's band, so both must read
    // as a concern against the lead - this is the measurement the whole feature turns on.
    const int keys = stripNamed (ctx, "Keys");
    const int organ = stripNamed (ctx, "Organ");
    const int lead = stripNamed (ctx, "Lead");
    int concerns = 0;
    for (const auto& r : relations)
        if (r.metric == "presence_masking_db" && r.concern && (r.stripA == keys || r.stripA == organ))
        {
            CHECK (r.stripB == lead);
            CHECK (r.value > r.tolerance);
            CHECK (! r.headline.empty());
            ++concerns;
        }
    CHECK (concerns == 2);

    // Measuring twice on the same listen gives the same numbers: nothing here reads a clock,
    // a random number or the current parameters.
    const auto again = RelationshipEngine::measure (ctx);
    REQUIRE (again.size() == relations.size());
    for (size_t i = 0; i < again.size(); ++i)
    {
        CHECK (again[i].metric == relations[i].metric);
        CHECK_NEAR (again[i].value, relations[i].value, 1.0e-6f);
    }
}

// ---------------------------------------------------------------------------
// MixContext
// ---------------------------------------------------------------------------
TEST_CASE ("MixContext: versioned, deterministic and honest about a thin listen")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);

    const auto context = buildMixContext (ctx, plan);
    CHECK (context.schemaVersion == kMixContextSchemaVersion);
    CHECK (context.profile == "Modern Gospel");
    CHECK (context.purpose == "Church Broadcast");
    CHECK (int (context.tracks.size()) == ctx.graph.numStrips());
    CHECK (context.adequacy.sufficient);
    CHECK (context.adequacy.tracksActive >= 2);

    // The same listen always serialises to the same bytes: without that there is no golden
    // test, no cache and no way to tell a changed mix from a changed document.
    CHECK (buildMixContext (ctx, plan).write() == context.write());

    const auto root = json::parse (context.write());
    CHECK (root["schemaVersion"].asInt() == kMixContextSchemaVersion);
    CHECK (root["tracks"].size() == int (context.tracks.size()));
    CHECK (root["tracks"][0]["id"].asString() == "strip:0");
    CHECK (root["relationships"].size() > 0);
    // Capture gain and mix gain stay separate all the way into the document.
    CHECK (root["tracks"][0]["signalHealth"].has ("consolePreampMoveDb"));

    // A listen where only one source played is reported as not enough to mix from, with the
    // reason and what to do about it - never as a confident mix.
    MixPlan thin = plan;
    thin.stripsHeard = 1;
    for (auto& s : thin.strips) s.heard = false;
    thin.strips[size_t (stripNamed (ctx, "Lead"))].heard = true;
    const auto thinContext = buildMixContext (ctx, thin);
    CHECK (! thinContext.adequacy.sufficient);
    CHECK (thinContext.adequacy.reason.find ("Lead") != std::string::npos);
    CHECK (! thinContext.adequacy.guidance.empty());
}

// ---------------------------------------------------------------------------
// MixIntent
// ---------------------------------------------------------------------------
TEST_CASE ("MixIntent: reads a structured reply and refuses to guess at anything else")
{
    Rig rig (band());
    const auto registry = DspCapabilityRegistry::build (rig.session, rig.engine.getGraph());

    std::vector<std::string> problems;
    const auto intent = parseMixIntentText (R"({
        "schemaVersion": 1,
        "summary": "Made room for the lead.",
        "targets": [
          { "target": "strip:4", "confidence": "HIGH", "reason": "Keys cover the words.",
            "objectives": [ { "type": "separation", "against": "strip:6", "strength": 9.0, "preserveArticulation": true },
                            { "type": "teleport", "strength": 1.0 } ] },
          { "target": "strip:99", "objectives": [ { "type": "presence", "strength": 0.5 } ] }
        ]})", &problems);

    REQUIRE (intent.valid);
    REQUIRE (intent.targets.size() == 2);
    CHECK (intent.targets[0].confidence == Confidence::High);
    REQUIRE (intent.targets[0].objectives.size() == 1);      // "teleport" is not an objective
    CHECK (intent.targets[0].objectives[0].type == MixObjectiveType::Separation);
    CHECK_NEAR (intent.targets[0].objectives[0].strength, 1.0f, 1.0e-6f);   // clamped, never taken as read
    CHECK (intent.targets[0].objectives[0].preserveArticulation);
    CHECK (problems.size() == 1);                            // the unknown objective

    // Parsing does not know the session; validating against it does, and strip:99 goes there.
    std::vector<std::string> checkedProblems;
    const auto usable = validateMixIntent (intent, registry, &checkedProblems);
    REQUIRE (usable.targets.size() == 1);
    CHECK (usable.targets[0].targetName == "Keys");
    CHECK (checkedProblems.size() == 1);

    // Nothing usable is a clean failure, not an empty mix that claims to have worked.
    CHECK (! parseMixIntentText ("not json at all").valid);
    CHECK (! parseMixIntentText (R"({"schemaVersion": 99, "targets": []})").valid);

    // A separation that does not say what it is making room for is dropped.
    problems.clear();
    auto loose = parseMixIntentText (R"({"targets":[{"target":"strip:4","objectives":[{"type":"separation","strength":0.5}]}]})");
    const auto checked = validateMixIntent (loose, registry, &problems);
    CHECK (! checked.valid);
    CHECK (problems.size() == 1);
}

// ---------------------------------------------------------------------------
// The resolver: creative within what DLIVE really has, and honest when it is not enough
// ---------------------------------------------------------------------------
TEST_CASE ("CapabilityResolver: builds what it can, approximates what it must, refuses the rest")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);
    REQUIRE (baseline.valid);
    const auto registry = DspCapabilityRegistry::build (ctx.session, ctx.graph);
    const auto context = buildMixContext (ctx, baseline);

    CapabilityResolver::Context rc;
    rc.registry = &registry;
    rc.mix = &context;
    rc.baseline = &baseline.proposed;
    rc.graph = &ctx.graph;
    rc.profile = ctx.session.profile;

    const int keys = stripNamed (ctx, "Keys");
    const int lead = stripNamed (ctx, "Lead");

    MixIntent intent;
    intent.valid = true;
    intent.summary = "Test intent.";
    {
        MixTargetIntent t;
        t.target = { MixTargetKind::Strip, keys };
        t.objectives.push_back ({ MixObjectiveType::Separation, 0.8f, { MixTargetKind::Strip, lead }, {}, true, false });
        intent.targets.push_back (t);
    }
    {
        MixTargetIntent t;                                   // a mono voice asked to be wide
        t.target = { MixTargetKind::Strip, lead };
        t.objectives.push_back ({ MixObjectiveType::Width, 1.0f, {}, {}, false, false });
        t.objectives.push_back ({ MixObjectiveType::Presence, 0.5f, {}, {}, false, false });
        intent.targets.push_back (t);
    }
    {
        MixTargetIntent t;                                   // a spring reverb DLIVE does not have
        t.target = { MixTargetKind::FxSlot, int (FxSlot::VocalPlate) };
        t.objectives.push_back ({ MixObjectiveType::Character, 0.9f, {}, "warm vintage spring", false, false });
        intent.targets.push_back (t);
    }
    {
        MixTargetIntent t;                                   // ... and one it cannot build at all
        t.target = { MixTargetKind::FxSlot, int (FxSlot::BgvHall) };
        t.objectives.push_back ({ MixObjectiveType::Character, 0.9f, {}, "gated reverb", false, false });
        intent.targets.push_back (t);
    }

    const auto plan = CapabilityResolver::resolve (intent, rc);
    REQUIRE (plan.valid);

    auto find = [&] (MixTargetKind kind, int index, const std::string& param) -> const MixAction*
    {
        for (const auto& a : plan.actions)
            if (a.target.kind == kind && a.target.index == index && a.paramId == param) return &a;
        return nullptr;
    };

    // Separation is a real cut in the lead's pocket - and DLIVE has no dynamic EQ, so it says
    // the cut is an approximation rather than claiming it only works while the voice is there.
    bool foundCut = false;
    for (const auto& a : plan.actions)
    {
        if (a.target.index != keys || a.processor != DspProcessor::CorrectiveEq) continue;
        if (a.paramId.find ("Gain") == std::string::npos) continue;
        CHECK (a.value < 0.0f);
        CHECK (a.resolution == ResolutionStatus::Approximated);
        CHECK (a.note.find ("dynamic EQ") != std::string::npos);
        foundCut = true;
    }
    CHECK (foundCut);

    // A mono source cannot be widened, and DLIVE says so instead of moving a control that
    // would do nothing. The presence request on the same target still lands.
    CHECK (find (MixTargetKind::Strip, lead, "widthAmount") == nullptr);
    CHECK (find (MixTargetKind::Strip, lead, "toneEq3Gain") != nullptr);
    bool saidMono = false;
    for (const auto& u : plan.unsupported) if (u.find ("mono") != std::string::npos) saidMono = true;
    CHECK (saidMono);

    // A spring reverb is built out of the plate - short, band-limited and modulated - and the
    // plan carries the word APPROXIMATED and the sentence that explains what it will not have.
    const auto* mod = find (MixTargetKind::FxSlot, int (FxSlot::VocalPlate), "rvModDepth");
    REQUIRE (mod != nullptr);
    CHECK (mod->resolution == ResolutionStatus::Approximated);
    CHECK (mod->note.find ("spring") != std::string::npos);
    CHECK (mod->value > mod->previousValue);

    // A gated reverb needs a processor DLIVE does not have at all: nothing is applied and the
    // reason survives to the user.
    CHECK (find (MixTargetKind::FxSlot, int (FxSlot::BgvHall), "rvDecay") == nullptr);
    bool saidGated = false;
    for (const auto& u : plan.unsupported) if (u.find ("gated reverb") != std::string::npos) saidGated = true;
    CHECK (saidGated);

    // Deterministic: the same intent against the same session resolves identically.
    CHECK (CapabilityResolver::resolve (intent, rc).write() == plan.write());
}

// ---------------------------------------------------------------------------
// The safety validator is the authority at execution level
// ---------------------------------------------------------------------------
TEST_CASE ("MixSafetyValidator: clamps what it can justify and refuses what it cannot")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);
    const auto registry = DspCapabilityRegistry::build (ctx.session, ctx.graph);
    const auto context = buildMixContext (ctx, baseline);

    MixSafetyValidator::Context vc;
    vc.registry = &registry;
    vc.baseline = &baseline.proposed;
    vc.graph = &ctx.graph;
    vc.mix = &context;
    vc.profile = ctx.session.profile;

    const int lead = stripNamed (ctx, "Lead");
    const int overheads = stripNamed (ctx, "OH");

    ProcessingPlan plan;
    plan.valid = true;
    auto action = [] (MixTargetRef t, DspProcessor p, const char* id, float value, float previous)
    {
        MixAction a;
        a.target = t; a.processor = p; a.paramId = id; a.value = value; a.previousValue = previous;
        a.reason = "test";
        return a;
    };
    const MixTargetRef leadRef { MixTargetKind::Strip, lead };
    const MixTargetRef overheadRef { MixTargetKind::Strip, overheads };
    const MixTargetRef masterRef { MixTargetKind::Bus, int (MixBus::Master) };

    plan.actions.push_back (action (leadRef, DspProcessor::ToneEq, "toneEq3Gain", 18.0f, 0.0f));      // absurd boost
    plan.actions.push_back (action (leadRef, DspProcessor::ToneEq, "toneEq3On", 1.0f, 0.0f));
    plan.actions.push_back (action (leadRef, DspProcessor::Fader, "faderDb", 40.0f, 0.0f));           // absurd fader
    plan.actions.push_back (action (leadRef, DspProcessor::InputGain, "inputGainDb", 12.0f, 0.0f));   // never allowed
    plan.actions.push_back (action (overheadRef, DspProcessor::Gate, "gateOn", 1.0f, 0.0f));          // would chop the cymbals
    plan.actions.push_back (action (leadRef, DspProcessor::Limiter, "limiterCeiling", -1.0f, -1.0f)); // no limiter here
    plan.actions.push_back (action (leadRef, DspProcessor::ToneEq, "notAControl", 1.0f, 0.0f));
    plan.actions.push_back (action (masterRef, DspProcessor::Fader, "faderDb", 24.0f, 0.0f));         // eats the headroom
    plan.actions.push_back (action ({ MixTargetKind::Strip, 999 }, DspProcessor::Fader, "faderDb", 1.0f, 0.0f));
    plan.actions.push_back (action (leadRef, DspProcessor::Compressor, "compThreshold",
                                    std::numeric_limits<float>::quiet_NaN(), -20.0f));

    MixSafetyValidator::Report report;
    const auto checked = MixSafetyValidator::validate (plan, vc, &report);

    auto status = [&] (const std::string& id) -> MixActionStatus
    {
        for (const auto& a : checked.actions) if (a.paramId == id) return a.status;
        return MixActionStatus::Proposed;
    };
    auto valueOf = [&] (const std::string& id) -> float
    {
        for (const auto& a : checked.actions) if (a.paramId == id) return a.value;
        return 0.0f;
    };

    const auto& B = MixProfile::aiBounds();
    CHECK (status ("toneEq3Gain") == MixActionStatus::Clamped);
    CHECK (valueOf ("toneEq3Gain") <= B.maxEqGainDb + 1.0e-4f);
    CHECK (std::fabs (valueOf ("faderDb")) <= B.maxFaderMoveDb + 1.0e-4f);

    // Capture gain is the console's. A reasoning pass that moves the digital preamp is hiding
    // a bad capture behind noise, so it is refused outright rather than clamped.
    CHECK (status ("inputGainDb") == MixActionStatus::Rejected);
    CHECK (status ("gateOn") == MixActionStatus::Rejected);
    CHECK (status ("limiterCeiling") == MixActionStatus::Rejected);
    CHECK (status ("notAControl") == MixActionStatus::Rejected);
    CHECK (status ("compThreshold") == MixActionStatus::Rejected);
    CHECK (report.rejected >= 6);
    CHECK (! report.notes.empty());

    // Every refusal keeps its reason, so REVIEW CHANGES can show what DLIVE declined to do.
    for (const auto& a : checked.actions)
        if (a.status == MixActionStatus::Rejected) CHECK (! a.note.empty());

    // The master keeps room under its ceiling whatever was asked for. A refused action leaves
    // the control exactly where the deterministic plan put it, which is what actually runs.
    const float masterMove = [&]
    {
        for (const auto& a : checked.actions)
            if (a.target.isMaster() && a.paramId == "faderDb") return a.applies() ? a.value - a.previousValue : 0.0f;
        return 0.0f;
    }();
    const float ceiling = baseline.proposed.master().channel.limiterEnabled
                              ? baseline.proposed.master().channel.limiterCeilingDb : 0.0f;
    // The deterministic mix already runs the master hard against its ceiling, which is the
    // limiter doing its job. The rule is that a reasoning pass may never take the remaining
    // room away: it gets whatever is left under the ceiling and not one dB more, which here
    // is nothing at all.
    const float roomLeft = std::max (0.0f, ceiling - B.minMasterHeadroomDb - context.master.truePeakDb);
    CHECK (masterMove <= roomLeft + 1.0e-3f);
}

// ---------------------------------------------------------------------------
// The whole slice, offline
// ---------------------------------------------------------------------------
TEST_CASE ("TUNE LIVE MIX: listen, reason, resolve, validate, apply, verify, refine, ready")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);
    REQUIRE (baseline.valid);

    TuneLiveCoordinator tune;
    CHECK (tune.getState() == TuneLiveCoordinator::State::Idle);
    CHECK (tune.getProvider()->getName() == "DLIVE built-in (offline)");
    CHECK (! tune.getProvider()->sendsDataExternally());

    tune.beginListening ("test-session");
    CHECK (tune.getState() == TuneLiveCoordinator::State::CapturingInitial);

    tune.onListenComplete (ctx, baseline);
    pump (tune);
    REQUIRE (tune.getState() == TuneLiveCoordinator::State::Applying);
    CHECK (tune.wantsApply());
    CHECK (tune.hasProposal());

    // The reasoning layer worked on the relationships, so the keys and the organ covering the
    // lead is what it acted on.
    const auto& intent = tune.getIntent();
    CHECK (intent.valid);
    CHECK (intent.objectiveCount() > 0);
    bool actedOnMasking = false;
    for (const auto& t : intent.targets)
        for (const auto& o : t.objectives)
            if (o.type == MixObjectiveType::Separation) actedOnMasking = true;
    CHECK (actedOnMasking);

    // Everything applied went through the bounds, and the proposal is the deterministic plan
    // plus those moves - never a rebuild of the mix from nothing.
    const auto& plan = tune.getPlan();
    CHECK (plan.countApplied() > 0);
    const auto& proposed = tune.getProposed();
    CHECK (proposed.numStrips == baseline.proposed.numStrips);
    const auto& B = MixProfile::aiBounds();
    for (int i = 0; i < proposed.numStrips; ++i)
        CHECK (std::fabs (proposed.strips[size_t (i)].faderDb - baseline.proposed.strips[size_t (i)].faderDb) <= B.maxFaderMoveDb + 1.0e-3f);
    // Capture gain is untouched by the whole pass, on every strip.
    for (int i = 0; i < proposed.numStrips; ++i)
        CHECK_NEAR (proposed.strips[size_t (i)].inputGainDb, baseline.proposed.strips[size_t (i)].inputGainDb, 1.0e-4f);

    const auto afterFirstPass = proposed;

    tune.onApplied();
    REQUIRE (tune.getState() == TuneLiveCoordinator::State::CapturingVerify);
    CHECK (tune.wantsVerifyListen());

    tune.onVerifyComplete (ctx, baseline);
    pump (tune);
    REQUIRE (tune.getState() == TuneLiveCoordinator::State::ApplyingRefinement);

    // A refinement is a correction, not a second mix: whatever it does is smaller than the
    // first pass, and it is allowed to do nothing at all.
    const auto& refined = tune.getProposed();
    for (int i = 0; i < refined.numStrips; ++i)
        CHECK (std::fabs (refined.strips[size_t (i)].faderDb - afterFirstPass.strips[size_t (i)].faderDb) <= B.maxFaderMoveDb + 1.0e-3f);

    tune.onApplied();
    CHECK (tune.getState() == TuneLiveCoordinator::State::Ready);
    CHECK (tune.getStatusText() == "LIVE MIX READY");

    // REVIEW CHANGES reads as sentences, and what was refused is in there too.
    const auto lines = tune.getReviewLines();
    CHECK (! lines.empty());
    const auto diag = tune.getDiagnostics();
    CHECK (diag.provider == "DLIVE built-in (offline)");
    CHECK (! diag.sentDataExternally);
    CHECK (diag.actionsProposed > 0);
    CHECK (diag.refinementRan);

    // Stored with the session and read back without any provider at all.
    const auto stored = json::parse (tune.toJson().write());
    CHECK (stored["state"].asString() == "READY");
    CHECK (stored["plan"]["schemaVersion"].asInt() == kProcessingPlanSchemaVersion);
}

TEST_CASE ("TUNE LIVE MIX: a listen that is not enough to mix from fails without touching the mix")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    auto baseline = MixPlanner::plan (ctx);
    REQUIRE (baseline.valid);

    // Only the lead played: a confident mix from this would be worse than no mix.
    baseline.stripsHeard = 1;
    for (auto& s : baseline.strips) s.heard = false;
    baseline.strips[size_t (stripNamed (ctx, "Lead"))].heard = true;

    TuneLiveCoordinator tune;
    tune.beginListening ("thin");
    tune.onListenComplete (ctx, baseline);
    CHECK (tune.getState() == TuneLiveCoordinator::State::Failed);
    CHECK (tune.getFailure().find ("Lead") != std::string::npos);
    CHECK (! tune.hasProposal());
}

TEST_CASE ("TUNE LIVE MIX: cancelling and a broken provider both leave the mix exactly as it was")
{
    struct BrokenProvider final : MixReasoningProvider
    {
        std::string getName() const override { return "Broken"; }
        bool isAvailable() const override { return true; }
        bool sendsDataExternally() const override { return true; }
        MixReasoningResponse reason (const MixReasoningRequest&, const std::atomic<bool>&) override
        {
            throw std::runtime_error ("the network went away");
        }
    };

    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);

    TuneLiveCoordinator tune;
    tune.setProvider (std::make_shared<BrokenProvider>());
    tune.beginListening ("broken");
    tune.onListenComplete (ctx, baseline);
    pump (tune);
    CHECK (tune.getState() == TuneLiveCoordinator::State::Failed);
    CHECK (tune.getFailure().find ("has not been changed") != std::string::npos);
    // The deterministic mix from the same listen is still there to keep - that is the point of
    // building it first - but the reasoning layer added nothing to it.
    CHECK (tune.hasProposal());
    CHECK (tune.getPlan().countApplied() == 0);
    CHECK (MixPlanner::countParameterChanges (tune.getProposed(), baseline.proposed) == 0);

    // Nothing is ever left half applied: a plan reaches the mix as one whole snapshot or not at all.
    TuneLiveCoordinator cancelled;
    cancelled.beginListening ("cancelled");
    cancelled.cancel();
    CHECK (cancelled.getState() == TuneLiveCoordinator::State::Cancelled);
    CHECK (! cancelled.hasProposal());
}

// ---------------------------------------------------------------------------
// REPEATABILITY
//
// The same band, the same listen and the same settings have to produce the same mix. This
// is not a nicety: an engineer cannot learn what DLIVE does from a system that answers
// differently every time it is asked, and cannot trust one in front of a congregation.
// ---------------------------------------------------------------------------

namespace
{
    // A provider that answers differently every single time - the worst case the cache and
    // the seed exist to defend against. It also counts how often it was actually asked.
    class DriftingProvider final : public MixReasoningProvider
    {
    public:
        std::string getName() const override { return "drifting (test)"; }
        bool isAvailable() const override { return true; }
        bool sendsDataExternally() const override { return false; }
        MixReasoningResponse reason (const MixReasoningRequest& r, const std::atomic<bool>&) override
        {
            ++calls;
            seenSeeds.push_back (r.seed);
            seenVariations.push_back (r.variation);
            MixReasoningResponse out;
            out.providerName = getName();
            out.valid = true;
            out.intent.valid = true;
            out.intent.schemaVersion = kMixIntentSchemaVersion;
            out.intent.summary = "drift " + std::to_string (calls);
            MixTargetIntent t;
            t.target = MixTargetRef { MixTargetKind::Bus, int (MixBus::Vocals) };
            t.targetName = "VOCALS";
            t.reason = "drifting on purpose";
            t.confidence = Confidence::Medium;
            MixObjective o;
            o.type = MixObjectiveType::Presence;
            // A different strength every call: if anything downstream is pinned, it is pinned
            // because DLIVE pinned it, not because the provider was well behaved.
            o.strength = 0.1f * float (calls);
            t.objectives.push_back (o);
            out.intent.targets.push_back (t);
            return out;
        }
        int calls = 0;
        std::vector<std::uint64_t> seenSeeds;
        std::vector<int> seenVariations;
    };

    // One whole reasoning pass, returning the mix it proposes.
    MixParameters runOnePass (TuneLiveCoordinator& tune, const MixPlanContext& ctx, const MixPlan& baseline)
    {
        tune.reset();
        tune.beginListening ("repeatability");
        tune.onListenComplete (ctx, baseline);
        pump (tune);
        return tune.getProposed();
    }

    bool sameMix (const MixParameters& a, const MixParameters& b)
    {
        if (a.numStrips != b.numStrips) return false;
        return MixPlanner::countParameterChanges (a, b) == 0;
    }
}

TEST_CASE ("Repeatability: the same listen is the same document, and the same fingerprint")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);

    const auto a = buildMixContext (ctx, baseline);
    const auto b = buildMixContext (ctx, baseline);
    CHECK (a.write() == b.write());
    CHECK (a.fingerprint() == b.fingerprint());
    CHECK (a.fingerprint() != 0);

    // A different band is a different question, and says so.
    auto quiet = ctx;
    quiet.capture.strips[0].peakDb -= 12.0f;
    const auto c = buildMixContext (quiet, baseline);
    CHECK (c.fingerprint() != a.fingerprint());
}

TEST_CASE ("Repeatability: the deterministic engineer reaches the same mix every time")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);
    REQUIRE (baseline.valid);

    TuneLiveCoordinator tune;
    TuneLiveCoordinator::Settings s;
    s.refinementPass = false;
    tune.setSettings (s);

    const auto first = runOnePass (tune, ctx, baseline);
    const auto second = runOnePass (tune, ctx, baseline);
    const auto third = runOnePass (tune, ctx, baseline);
    CHECK (sameMix (first, second));
    CHECK (sameMix (second, third));
}

TEST_CASE ("Repeatability: a provider that drifts is asked once, and the answer is pinned")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);

    auto drifting = std::make_shared<DriftingProvider>();
    TuneLiveCoordinator tune;
    tune.setProvider (drifting);
    TuneLiveCoordinator::Settings s;
    s.refinementPass = false;
    tune.setSettings (s);

    const auto first = runOnePass (tune, ctx, baseline);
    CHECK (drifting->calls == 1);
    const auto second = runOnePass (tune, ctx, baseline);
    const auto third = runOnePass (tune, ctx, baseline);

    // The provider was asked once. Everything after that is the answer it already gave.
    CHECK (drifting->calls == 1);
    CHECK (sameMix (first, second));
    CHECK (sameMix (second, third));
    CHECK (tune.getDiagnostics().answerFromCache);

    // The seed it was given is derived from this exact listen, not from a clock or a counter:
    // a different band gets a different seed, and the same band always gets the same one.
    REQUIRE (! drifting->seenSeeds.empty());
    const auto seedHere = drifting->seenSeeds.front();
    CHECK (seedHere != 0);

    auto quieter = ctx;
    for (auto& strip : quieter.capture.strips) strip.peakDb -= 9.0f;
    auto other = std::make_shared<DriftingProvider>();
    TuneLiveCoordinator onQuieter;
    onQuieter.setProvider (other);
    onQuieter.setSettings (s);
    runOnePass (onQuieter, quieter, MixPlanner::plan (quieter));
    REQUIRE (! other->seenSeeds.empty());
    CHECK (other->seenSeeds.front() != seedHere);
}

TEST_CASE ("Repeatability: TRY ANOTHER MIX is a different question, asked on purpose")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    const auto baseline = MixPlanner::plan (ctx);

    auto drifting = std::make_shared<DriftingProvider>();
    TuneLiveCoordinator tune;
    tune.setProvider (drifting);

    TuneLiveCoordinator::Settings s;
    s.refinementPass = false;
    s.variation = 0;
    tune.setSettings (s);
    const auto primary = runOnePass (tune, ctx, baseline);

    s.variation = 1;
    tune.setSettings (s);
    const auto alternative = runOnePass (tune, ctx, baseline);

    // A new variation is a new question: the provider is asked again, with a different seed.
    CHECK (drifting->calls == 2);
    CHECK (drifting->seenSeeds[0] != drifting->seenSeeds[1]);
    CHECK (drifting->seenVariations[1] == 1);

    // And that variation is itself repeatable.
    const auto alternativeAgain = runOnePass (tune, ctx, baseline);
    CHECK (drifting->calls == 2);
    CHECK (sameMix (alternative, alternativeAgain));
    (void) primary;

    // Going back to the primary mix gives the primary mix, not the newest answer.
    s.variation = 0;
    tune.setSettings (s);
    CHECK (sameMix (runOnePass (tune, ctx, baseline), primary));
    CHECK (drifting->calls == 2);
}

// ---------------------------------------------------------------------------
// AI MIX CHAT
//
// The whole point of the chat is that a sentence becomes a real, bounded, reviewable change
// rather than written advice. These tests are about the reading: does DLIVE hear what was
// asked, on the right channel, in the right direction - and does it say so plainly when it
// did not follow, instead of confidently changing something nobody asked about.
// ---------------------------------------------------------------------------
namespace
{
    struct ChatRig
    {
        Rig rig;
        MixCapture::Result cap;
        MixPlanContext ctx;
        MixPlan baseline;
        MixContext context;
        DspCapabilityRegistry registry;

        explicit ChatRig (const MixSession& s) : rig (s)
        {
            auto in = bandAudio();
            cap = rig.listen (in);
            ctx = rig.context (cap);
            baseline = MixPlanner::plan (ctx);
            context = buildMixContext (ctx, baseline);
            registry = DspCapabilityRegistry::build (rig.session, rig.engine.getGraph());
        }
        MixRequestReading read (const char* text) const { return readMixRequest (text, context, registry); }
    };

    const MixTargetIntent* intentFor (const MixIntent& i, const std::string& name)
    {
        for (const auto& t : i.targets) if (t.targetName == name) return &t;
        return nullptr;
    }
    const MixObjective* objectiveOfType (const MixTargetIntent& t, MixObjectiveType type)
    {
        for (const auto& o : t.objectives) if (o.type == type) return &o;
        return nullptr;
    }
}

TEST_CASE ("AI MIX CHAT: plain words become a bounded intent on the right channel")
{
    ChatRig chat { band() };

    {
        const auto r = chat.read ("Bring the lead vocal forward");
        REQUIRE (r.understood);
        const auto* lead = intentFor (r.intent, "Lead");
        REQUIRE (lead != nullptr);
        const auto* o = objectiveOfType (*lead, MixObjectiveType::Presence);
        REQUIRE (o != nullptr);
        CHECK (o->strength > 0.0f);           // forward, not back
    }
    {
        // A complaint asks for the opposite of the thing complained about.
        const auto r = chat.read ("The vocals sound harsh");
        REQUIRE (r.understood);
        REQUIRE (! r.intent.targets.empty());
        const auto* o = objectiveOfType (r.intent.targets.front(), MixObjectiveType::Brightness);
        REQUIRE (o != nullptr);
        CHECK (o->strength < 0.0f);
    }
    {
        const auto r = chat.read ("Make the drums punchier");
        REQUIRE (r.understood);
        const auto* drums = intentFor (r.intent, "DRUMS");
        REQUIRE (drums != nullptr);           // a whole family means the group, which is one move
        const auto* o = objectiveOfType (*drums, MixObjectiveType::Punch);
        REQUIRE (o != nullptr);
        CHECK (o->strength > 0.0f);
        CHECK (o->preserveTransients);        // control must not cost the attack
    }
    {
        const auto r = chat.read ("The mix sounds muddy");
        REQUIRE (r.understood);
        const auto* master = intentFor (r.intent, "MASTER");
        REQUIRE (master != nullptr);          // a statement about the whole mix is about the master
        CHECK (objectiveOfType (*master, MixObjectiveType::Clarity) != nullptr);
    }
    {
        // "a little" is a smaller move than the same request without it.
        const auto plain = chat.read ("Give the lead more warmth");
        const auto gentle = chat.read ("Give the lead a little more warmth");
        REQUIRE (plain.understood && gentle.understood);
        const auto* a = objectiveOfType (*intentFor (plain.intent, "Lead"), MixObjectiveType::Warmth);
        const auto* b = objectiveOfType (*intentFor (gentle.intent, "Lead"), MixObjectiveType::Warmth);
        REQUIRE (a != nullptr && b != nullptr);
        CHECK (b->strength < a->strength);
    }
    {
        // Two sources and a "make room" word is about the relationship, not about a channel.
        const auto r = chat.read ("The keys are covering the lead");
        REQUIRE (r.understood);
        bool madeRoom = false;
        for (const auto& t : r.intent.targets)
            for (const auto& o : t.objectives)
                if (o.type == MixObjectiveType::Separation && o.hasAgainst()) madeRoom = true;
        CHECK (madeRoom);
    }
}

TEST_CASE ("AI MIX CHAT: what it did not follow, and what is not a mix decision, are said plainly")
{
    ChatRig chat { band() };

    // Nothing recognisable: no change is proposed, and the answer says what to try.
    {
        const auto r = chat.read ("do the thing with the wotsit");
        CHECK (! r.understood);
        CHECK (! r.failure.empty());
        CHECK (r.intent.targets.empty());
    }
    // A channel but no wish.
    {
        const auto r = chat.read ("the lead vocal");
        CHECK (! r.understood);
        CHECK (r.failure.find ("what you want") != std::string::npos);
    }
    // A wish but no channel.
    {
        const auto r = chat.read ("make it punchier please");
        CHECK (! r.understood);
        CHECK (r.failure.find ("which channel") != std::string::npos);
    }
    // A source naming something that is not on this console is not invented.
    {
        const auto r = chat.read ("more air on the saxophone");
        CHECK (! r.understood);               // there is no sax in this band
    }
    // A capture problem is answered honestly rather than hidden with processing.
    {
        const auto r = chat.read ("the lead vocal is feedback-y and the singer is off mic");
        bool saidSo = false;
        for (const auto& n : r.notMixDecisions) if (n.find ("at the source") != std::string::npos) saidSo = true;
        CHECK (saidSo);
    }
    // "Louder without clipping" is a request and a refusal at once, and says both.
    {
        const auto r = chat.read ("make the master louder without clipping");
        REQUIRE (r.understood);
        bool keptHeadroom = false;
        for (const auto& n : r.notMixDecisions) if (n.find ("headroom") != std::string::npos) keptHeadroom = true;
        CHECK (keptHeadroom);
    }
}

TEST_CASE ("AI MIX CHAT: the same sentence always reads the same way")
{
    ChatRig chat { band() };
    const auto a = chat.read ("the backing vocals need less reverb and a bit more clarity");
    const auto b = chat.read ("the backing vocals need less reverb and a bit more clarity");
    REQUIRE (a.understood && b.understood);
    CHECK (a.intent.write() == b.intent.write());
}

TEST_CASE ("AI MIX CHAT: a request goes through the same bounds as a Tune, offline")
{
    ChatRig chat { band() };

    MixReasoningRequest request;
    request.context = chat.context;
    request.registry = chat.registry;
    request.userRequest = "bring the lead vocal forward and give the drums more punch";

    LocalMixReasoningProvider provider;
    std::atomic<bool> cancel { false };
    const auto reply = provider.reason (request, cancel);
    REQUIRE (reply.valid);
    CHECK (reply.intent.objectiveCount() > 0);

    // Resolved and validated exactly as a Tune's intent is: nothing the chat asks for can
    // reach a parameter by a path the reasoning layer could not.
    const auto intent = validateMixIntent (reply.intent, chat.registry);

    CapabilityResolver::Context rc;
    rc.registry = &chat.registry;
    rc.mix = &chat.context;
    rc.baseline = &chat.baseline.proposed;
    rc.graph = &chat.ctx.graph;
    rc.profile = chat.ctx.session.profile;
    const auto resolved = CapabilityResolver::resolve (intent, rc);

    MixSafetyValidator::Context vc;
    vc.registry = &chat.registry;
    vc.baseline = &chat.baseline.proposed;
    vc.graph = &chat.ctx.graph;
    vc.mix = &chat.context;
    vc.profile = chat.ctx.session.profile;
    MixSafetyValidator::Report report;
    const auto checked = MixSafetyValidator::validate (resolved, vc, &report);
    const auto proposed = applyProcessingPlan (chat.baseline.proposed, checked, chat.ctx.graph);

    const auto& B = MixProfile::aiBounds();
    for (int i = 0; i < proposed.numStrips; ++i)
    {
        CHECK (std::fabs (proposed.strips[size_t (i)].faderDb - chat.baseline.proposed.strips[size_t (i)].faderDb)
                   <= B.maxFaderMoveDb + 1.0e-3f);
        // Capture gain is the console's, whatever anybody types into a chat window.
        CHECK_NEAR (proposed.strips[size_t (i)].inputGainDb, chat.baseline.proposed.strips[size_t (i)].inputGainDb, 1.0e-4f);
    }
}
