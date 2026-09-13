#include "TestFramework.h"
#include "TestSignals.h"
#include "Mix/MixEngine.h"
#include "Mix/OfflineCapture.h"
#include "Mix/MixPlanner.h"
#include "Profiles/MixProfileData.h"
#include "FX/FxProfiles.h"
#include <cstdio>
#include <random>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;

    // A small church band on 15 device inputs.
    MixSession band()
    {
        MixSession s;
        s.profile = StyleProfileId::ModernGospel;
        s.purpose = MixPurpose::ChurchBroadcast;
        s.inputs = {
            { "Kick",   ChannelRole::KickIn,        0, -1 },
            { "Snare",  ChannelRole::SnareTop,      1, -1 },
            { "Tom L",  ChannelRole::RackTom,       2, -1 },
            { "Tom R",  ChannelRole::FloorTom,      3, -1 },
            { "OH",     ChannelRole::Overhead,      4,  5 },
            { "Room",   ChannelRole::Room,          6, -1 },
            { "Bass",   ChannelRole::BassDI,        7, -1 },
            { "Keys",   ChannelRole::Piano,         8,  9 },
            { "Lead",   ChannelRole::LeadVocal,    10, -1 },
            { "Vox 1",  ChannelRole::BackingVocal, 11, -1 },
            { "Vox 2",  ChannelRole::BackingVocal, 12, -1 },
            { "Vox 3",  ChannelRole::BackingVocal, 13, -1 },
            { "Pastor", ChannelRole::Speech,       14, -1 },
        };
        return s;
    }

    void sine (std::vector<float>& c, float hz, float amp, float from = 0.0f, float to = 1.0e9f)
    {
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float t = float (i) / float (kSr);
            if (t >= from && t < to) c[i] += amp * std::sin (2.0f * float (M_PI) * hz * t);
        }
    }
    void bursts (std::vector<float>& c, float hz, float amp, float periodS, float lengthS, float phaseS, bool noise, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float t = float (i) / float (kSr);
            const float inPeriod = std::fmod (t + periodS - phaseS, periodS);
            if (inPeriod < lengthS)
            {
                const float env = 1.0f - inPeriod / lengthS;
                c[i] += amp * env * (noise ? dist (rng) : std::sin (2.0f * float (M_PI) * hz * t));
            }
        }
    }
    void noise (std::vector<float>& c, float amp, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (auto& x : c) x += amp * dist (rng);
    }

    // Eight seconds of "band". The pastor stays silent.
    testsig::Buffer bandAudio()
    {
        testsig::Buffer in (15, int (kSr * 8));
        bursts (in.data[0], 100.0f, 0.7f, 0.5f, 0.12f, 0.0f, false, 1);    // kick: 100 Hz, sits in the Low band
        bursts (in.data[1], 0.0f, 0.5f, 0.5f, 0.05f, 0.25f, true, 2);     // snare: noise crack
        bursts (in.data[2], 120.0f, 0.5f, 2.0f, 0.3f, 0.7f, false, 3);    // rack tom, rare
        bursts (in.data[3], 90.0f, 0.5f, 2.0f, 0.3f, 1.4f, false, 4);     // floor tom, rare
        noise (in.data[2], 0.03f, 5); noise (in.data[3], 0.03f, 6);        // bleed on the tom mics
        noise (in.data[4], 0.05f, 7); noise (in.data[5], 0.05f, 8);        // overheads (stereo pair)
        noise (in.data[6], 0.04f, 9);                                      // room
        sine (in.data[7], 40.0f, 0.8f);                                    // bass: loud and sub-heavy
        sine (in.data[8], 262.0f, 0.15f); sine (in.data[8], 2600.0f, 0.2f); // keys: bright upper mids
        sine (in.data[9], 330.0f, 0.15f); sine (in.data[9], 2800.0f, 0.2f);
        sine (in.data[10], 220.0f, 0.3f); sine (in.data[10], 440.0f, 0.1f); // lead: warm voice-like tone
        sine (in.data[11], 330.0f, 0.2f); sine (in.data[12], 392.0f, 0.2f); sine (in.data[13], 494.0f, 0.2f);
        noise (in.data[14], 0.02f, 10);                                    // pastor mic: only band spill during the song
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

    const StripPlan& stripNamed (const MixPlan& p, const char* name)
    {
        for (const auto& s : p.strips) if (s.name == name) return s;
        throw std::runtime_error ("no strip named " + std::string (name));
    }
    int stripIndex (const MixPlan& p, const char* name) { return stripNamed (p, name).strip; }
    bool hasRelationship (const MixPlan& p, const char* text)
    {
        for (const auto& r : p.relationships) if (r.what.find (text) != std::string::npos) return true;
        return false;
    }
}

TEST_CASE ("MixPlanner: the listen finds the tempo, and the delays and reverb tails are fitted to it")
{
    // The synthetic band plays its kick and snare on a 0.5 s pulse: 120 BPM, known exactly. The engine
    // starts from a deliberately wrong tempo so the planner has to measure it rather than agree by default.
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    auto ctx = rig.context (cap);
    ctx.current.tempoBpm = 150.0f;
    ctx.atCapture.tempoBpm = 150.0f;
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);

    // A live console has no host play head, so without this every tempo-synced delay runs at whatever the
    // engine was left at and sits out of time with the band.
    CHECK_NEAR (plan.proposed.tempoBpm, 120.0f, 6.0f);
    CHECK (hasRelationship (plan, "Delays timed to the song"));

    // Reverb tails are fitted to the song: shortened toward the profile's beat count, never past the
    // effect's own character, and never gutted.
    const float secondsPerBeat = 60.0f / plan.proposed.tempoBpm;
    bool fittedOne = false;
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        if (! ctx.graph.fxUsed[size_t (f)]) continue;
        const auto& fx = plan.proposed.fx[size_t (f)];
        const float character = FxProfiles::baseline (StyleProfileId::ModernGospel, ctx.graph.fxType[size_t (f)]).reverbDecayS;
        const float beats = MixProfile::reverbBeats (StyleProfileId::ModernGospel, FxSlot (f));
        if (beats <= 0.0f || ! fx.fx.reverbEnabled) continue;
        CHECK (fx.fx.reverbDecayS <= character + 0.01f);            // the profile's decay stays the ceiling
        CHECK (fx.fx.reverbDecayS >= 0.5f * character - 0.01f);     // ... and it is never gutted
        CHECK_NEAR (fx.fx.reverbDecayS, std::max (std::min (beats * secondsPerBeat, character), 0.5f * character), 0.05f);
        fittedOne = true;
    }
    CHECK (fittedOne);

    // A held note or an open room microphone has no rhythm to read: only sources that play one vote.
    for (const auto& sp : plan.strips)
        if (sp.heard && roleFamily (sp.role) == RoleFamily::Speech)
            CHECK (cap.strips[size_t (sp.strip)].transientsPerSecond < 0.5f);   // the pastor's spill is not a voter

    // Planning again on the same listen lands on the same tempo and the same tails.
    MixPlanContext again = ctx;
    again.current = plan.proposed;
    const auto second = MixPlanner::plan (again);
    REQUIRE (second.valid);
    CHECK_NEAR (second.proposed.tempoBpm, plan.proposed.tempoBpm, 0.01f);
    for (int f = 0; f < int (FxSlot::Count); ++f)
        CHECK_NEAR (second.proposed.fx[size_t (f)].fx.reverbDecayS, plan.proposed.fx[size_t (f)].fx.reverbDecayS, 0.01f);
}

TEST_CASE ("MixPlanner: one listen tunes every source, balances the faders and reasons about the mix as a whole")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    REQUIRE (cap.valid);
    const auto ctx = rig.context (cap);
    const auto plan = MixPlanner::plan (ctx);
    REQUIRE (plan.valid);
    CHECK (plan.headline == "MIX TUNED");
    CHECK (plan.strips.size() == 13);
    CHECK (plan.stripsHeard == 13);                    // the pastor's mic hears the band
    CHECK (stripNamed (plan, "Pastor").heard);
    CHECK (stripNamed (plan, "Pastor").bleedOnly);      // ... so it is left alone
    CHECK (stripNamed (plan, "Pastor").faderDb == 0.0f);
    CHECK (stripNamed (plan, "Pastor").inputGainDb == 0.0f);
    CHECK (plan.parametersChanged > 0);
    CHECK (plan.fadersChanged > 0);

    // Every heard source got its own Tune report.
    for (const auto& s : plan.strips)
        if (s.heard) CHECK (s.tune.valid);

    // Balance: every heard source's fader is fitted from how loud it is while it plays to the profile's
    // mix level, held under the peak ceiling, and inside bounds.
    const auto& R = MixProfile::relationships (StyleProfileId::ModernGospel);
    for (const auto& s : plan.strips)
    {
        CHECK (std::fabs (s.faderDb) <= R.maxFaderMoveDb + 0.01f);
        if (! s.balanced || roleFamily (s.role) == RoleFamily::BackingVocal) continue;
        const RoleFamily f = roleFamily (s.role);
        const auto& proposed = plan.proposed.strips[size_t (s.strip)];
        const float target = MixProfile::mixLevelTargetDb (StyleProfileId::ModernGospel, f);
        const float level = MixPlanner::predictedProcessedActiveRmsDb (ctx, s.strip, proposed);
        const float peak = MixPlanner::predictedProcessedPeakDb (ctx, s.strip, proposed);
        float expected = std::round ((target - level) * 2.0f) * 0.5f;
        expected = std::min (expected, std::round ((MixProfile::stripPeakCeilingDb (StyleProfileId::ModernGospel) - peak) * 2.0f) * 0.5f);
        // A drum close microphone hears the rest of the kit, so the balance only lifts one so far.
        const bool closeMic = f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom || f == RoleFamily::HiHat;
        if (closeMic)
        {
            const float gainRaise = std::max (s.inputGainDb - ctx.atCapture.strips[size_t (s.strip)].inputGainDb, 0.0f);
            expected = std::min (expected, std::max (std::round ((R.maxCloseMicRaiseDb - gainRaise) * 2.0f) * 0.5f, 0.0f));
        }
        expected = std::max (-R.maxFaderMoveDb, std::min (R.maxFaderMoveDb, expected));
        CHECK_NEAR (s.faderDb, expected, 0.01f);
    }
    CHECK (stripNamed (plan, "Bass").balanced);
    CHECK (stripNamed (plan, "Lead").balanced);

    // Kick <-> bass: the bass carries the sub, so its high-pass is raised (never above 0.8 x its note).
    const auto& bass = plan.proposed.strips[size_t (stripIndex (plan, "Bass"))].channel;
    CHECK (hasRelationship (plan, "Bass high-pass"));
    CHECK (bass.hpfEnabled);
    CHECK (bass.hpfHz >= std::min (R.bassHpfMinHz, 0.8f * 40.0f) - 0.5f);
    CHECK (bass.hpfHz <= 0.8f * 40.0f + 0.5f);   // the note wins: never above 0.8 x the measured fundamental

    // Lead <-> keys: the keys make room around the vocal pocket.
    const auto& keys = plan.proposed.strips[size_t (stripIndex (plan, "Keys"))].channel;
    CHECK (hasRelationship (plan, "Made room for the lead vocal in KEYS"));
    CHECK (keys.toneBands[1].enabled);
    CHECK_NEAR (keys.toneBands[1].freqHz, R.vocalPocketHz, 0.5f);
    CHECK (keys.toneBands[1].gainDb <= -1.0f);
    CHECK (keys.toneBands[1].gainDb >= -R.vocalPocketMaxCutDb - 0.01f);

    // Toms <-> overheads: if a tom gate exists it never closes fully.
    for (const char* tom : { "Tom L", "Tom R" })
    {
        const auto& p = plan.proposed.strips[size_t (stripIndex (plan, tom))].channel;
        if (p.gateEnabled) CHECK (p.gateRangeDb <= R.tomGateMaxRangeWithOverheadsDb + 0.01f);
    }

    // Room mics present: the drum room return steps back on the toms' sends.
    CHECK (hasRelationship (plan, "Drum room return stepped back"));
    CHECK_NEAR (plan.proposed.strips[size_t (stripIndex (plan, "Tom L"))].sendDb[size_t (FxSlot::DrumRoom)],
                MixProfile::defaultSendDb (StyleProfileId::ModernGospel, RoleFamily::Tom, FxSlot::DrumRoom) - R.drumRoomSendCutWithRoomMicsDb, 0.01f);

    // Lead <-> backing vocals: several voices add up, and however they were fitted the group ends up
    // behind the lead. Whether that needed a group move (the "held" relationship) or the per-voice level
    // already put them there depends on how many are singing, so the invariant is what is checked.
    {
        const float leadLevel = MixProfile::mixLevelTargetDb (StyleProfileId::ModernGospel, RoleFamily::LeadVocal);
        int voices = 0;
        for (const auto& s : plan.strips)
            if (s.balanced && roleFamily (s.role) == RoleFamily::BackingVocal) ++voices;
        REQUIRE (voices >= 2);
        const float group = MixProfile::mixLevelTargetDb (StyleProfileId::ModernGospel, RoleFamily::BackingVocal)
                          + 10.0f * std::log10 (float (voices));
        CHECK (group <= leadLevel - R.backingGroupBelowLeadDb + 0.01f);
        CHECK (hasRelationship (plan, "Backing vocals held") || hasRelationship (plan, "Lead vocal stays in front"));
    }
    CHECK (stripNamed (plan, "Vox 1").faderDb < stripNamed (plan, "Lead").faderDb);

    // Input gain: a hot kick (-3 dBFS) is brought down, quiet overheads (-26 dBFS) are brought up, both bounded.
    CHECK (stripNamed (plan, "Kick").inputGainDb < 0.0f);
    CHECK (stripNamed (plan, "OH").inputGainDb > 0.0f);
    for (const auto& s : plan.strips) CHECK (std::fabs (s.inputGainDb) <= R.maxInputGainDb + 0.01f);
    CHECK (plan.gainsChanged >= 2);
    CHECK (plan.proposed.strips[size_t (stripIndex (plan, "OH"))].inputGainDb == stripNamed (plan, "OH").inputGainDb);

    // Buses and master were tuned from what they received.
    CHECK (plan.buses[size_t (MixBus::Drums)].tune.valid);
    CHECK (plan.buses[size_t (MixBus::Vocals)].tune.valid);
    CHECK (plan.buses[size_t (MixBus::Master)].tune.valid);
    CHECK (plan.proposed.master().channel.limiterEnabled);
    CHECK (! plan.notes.empty());

    std::printf ("    %s | ", plan.headline.c_str());
    for (const auto& n : plan.notes) std::printf ("%s ", n.c_str());
    std::printf ("\n");
    for (const auto& r : plan.relationships) std::printf ("      - %s\n", r.what.c_str());
}

namespace
{
    void dumpDifferences (const MixPlan& a, const MixPlan& b)
    {
        for (size_t i = 0; i < a.strips.size(); ++i)
        {
            for (const auto& c : diffParameters (a.proposed.strips[i].channel, b.proposed.strips[i].channel))
                std::printf ("      %s: %s -> %.3g\n", a.strips[i].name.c_str(), c.paramId.c_str(), double (c.value));
            if (std::fabs (a.proposed.strips[i].faderDb - b.proposed.strips[i].faderDb) > 0.01f)
                std::printf ("      %s: fader %.1f -> %.1f\n", a.strips[i].name.c_str(), double (a.proposed.strips[i].faderDb), double (b.proposed.strips[i].faderDb));
        }
        for (int bus = 0; bus < int (MixBus::Count); ++bus)
            for (const auto& c : diffParameters (a.proposed.buses[size_t (bus)].channel, b.proposed.buses[size_t (bus)].channel))
                std::printf ("      %s bus: %s -> %.3g\n", mixBusName (MixBus (bus)), c.paramId.c_str(), double (c.value));
    }
}

TEST_CASE ("MixPlanner: planning again on the same listen changes nothing (idempotent)")
{
    Rig rig (band());
    auto in = bandAudio();
    const auto cap = rig.listen (in);
    auto ctx = rig.context (cap);
    const auto first = MixPlanner::plan (ctx);
    REQUIRE (first.valid);
    REQUIRE (! first.noChangeRequired);

    ctx.current = first.proposed;      // the user kept the plan; the listen is the same
    const auto second = MixPlanner::plan (ctx);
    REQUIRE (second.valid);
    if (! second.noChangeRequired) dumpDifferences (first, second);
    CHECK (second.noChangeRequired);
    CHECK (second.headline == "MIX: NO CHANGE REQUIRED");
    CHECK (second.parametersChanged == 0);
    CHECK (second.fadersChanged == 0);
    CHECK (second.sendsChanged == 0);
    CHECK (MixPlanner::countParameterChanges (first.proposed, second.proposed) == 0);
}

TEST_CASE ("MixPlanner: a close drum microphone is not lifted again by the next Tune Mix")
{
    // The budget for a close microphone - it hears the rest of the kit, so what lifts the drum lifts the
    // bleed - was measured against the gain that ran at the listen. After a mix is kept, the next listen
    // runs with that gain, so the raise it was meant to count reads as zero and the whole budget is handed
    // out again: a tom climbs another 6 dB a pass and brings the kit up inside its own microphone.
    Rig rig (band());
    auto in = bandAudio();
    // Under-gained tom microphones, the way a desk with the preamps left low sends them: quiet enough that
    // the whole close-mic budget is spent on digital gain before the fader is even asked for.
    for (int c : { 2, 3 }) for (auto& x : in.data[size_t (c)]) x *= 0.08f;
    const auto first = MixPlanner::plan (rig.context (rig.listen (in)));
    REQUIRE (first.valid);
    // The situation the rule is for has to actually arise, or this test guards nothing.
    REQUIRE (stripNamed (first, "Tom L").inputGainDb >= MixProfile::relationships (StyleProfileId::ModernGospel).maxCloseMicRaiseDb);

    rig.engine.setParameters (first.proposed);        // the user kept it; now DLIVE listens again through it
    auto ctx = rig.context (rig.listen (in));
    ctx.current = ctx.atCapture = first.proposed;
    const auto second = MixPlanner::plan (ctx);
    REQUIRE (second.valid);

    for (const auto& sp : first.strips)
    {
        const RoleFamily f = roleFamily (sp.role);
        if (! (f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom || f == RoleFamily::HiHat)) continue;
        if (! sp.balanced) continue;
        const auto& again = stripNamed (second, sp.name.c_str());
        if (! again.balanced) continue;
        // Its total lift is gain plus fader; a second pass may trim it, but it must never add another budget.
        const float firstLift = std::max (sp.inputGainDb, 0.0f) + sp.faderDb;
        const float secondLift = std::max (again.inputGainDb, 0.0f) + again.faderDb;
        CHECK (secondLift <= firstLift + 0.75f);
    }
}

TEST_CASE ("MixPlanner: silence everywhere is reported as no signal, not as a mix")
{
    Rig rig (band());
    testsig::Buffer in (15, int (kSr * 3));
    const auto cap = rig.listen (in);
    const auto plan = MixPlanner::plan (rig.context (cap));
    CHECK (plan.headline == "MIX: NO SIGNAL");
    CHECK (plan.stripsHeard == 0);
    CHECK (MixPlanner::countParameterChanges (plan.before, plan.proposed) == 0);
}

TEST_CASE ("MixPlanner: a session without a lead vocal or bass makes no vocal or low-end relationship decisions")
{
    MixSession s;
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Keys", ChannelRole::Piano, 1, 2 } };
    Rig rig (s);
    testsig::Buffer in (3, int (kSr * 4));
    bursts (in.data[0], 80.0f, 0.7f, 0.5f, 0.12f, 0.0f, false, 1);
    sine (in.data[1], 262.0f, 0.2f); sine (in.data[2], 330.0f, 0.2f);
    const auto cap = rig.listen (in);
    const auto plan = MixPlanner::plan (rig.context (cap));
    REQUIRE (plan.valid);
    CHECK (! hasRelationship (plan, "Bass high-pass"));
    CHECK (! hasRelationship (plan, "Made room for the lead vocal"));
    CHECK (! hasRelationship (plan, "Backing vocals"));
    CHECK (plan.buses[size_t (MixBus::Master)].tune.valid);
}
