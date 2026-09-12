// DLIVE application-layer tests: the controller's state machine (setup -> listen -> plan ->
// preview -> keep / revert, compare, macros, Advanced edits) and the session document round trip.
// No device, no UI: the engine is fed synthetic audio through MixController::process().
#include "TestFramework.h"
#include "native/MixController.h"
#include "native/SessionStore.h"
#include "Mix/MixPlanner.h"
#include "MixAI/MixReasoningProvider.h"
#include "native/OpenAiMixProvider.h"
#include <juce_core/juce_core.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;

    MixSession band()
    {
        MixSession s;
        s.name = "Test Sunday";
        s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Bass", ChannelRole::BassDI, 1, -1 }, { "Keys", ChannelRole::Piano, 2, 3 },
                     { "Lead", ChannelRole::LeadVocal, 4, -1 }, { "Vox", ChannelRole::BackingVocal, 5, -1 } };
        return s;
    }

    struct Feeder
    {
        MixController& c;
        std::vector<std::vector<float>> in = std::vector<std::vector<float>> (6, std::vector<float> (static_cast<size_t> (kBlock), 0.0f));
        std::vector<const float*> ip = std::vector<const float*> (6, nullptr);
        std::vector<float> l = std::vector<float> (static_cast<size_t> (kBlock), 0.0f);
        std::vector<float> r = std::vector<float> (static_cast<size_t> (kBlock), 0.0f);
        long long pos = 0;
        explicit Feeder (MixController& controller) : c (controller) {}

        // Plays the band for `seconds`, paced like a real callback so the listen worker keeps up.
        void play (double seconds, bool poll = true)
        {
            const int blocks = int (seconds * kSr / kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const float t = float (pos + i) / float (kSr);
                    in[0][size_t (i)] = std::fmod (t, 0.5f) < 0.1f ? 0.6f * std::sin (2.0f * float (M_PI) * 100.0f * t) : 0.0f;
                    in[1][size_t (i)] = 0.4f * std::sin (2.0f * float (M_PI) * 41.0f * t);
                    in[2][size_t (i)] = 0.2f * std::sin (2.0f * float (M_PI) * 262.0f * t) + 0.1f * std::sin (2.0f * float (M_PI) * 2600.0f * t);
                    in[3][size_t (i)] = 0.2f * std::sin (2.0f * float (M_PI) * 330.0f * t) + 0.1f * std::sin (2.0f * float (M_PI) * 2800.0f * t);
                    in[4][size_t (i)] = 0.3f * std::sin (2.0f * float (M_PI) * 220.0f * t);
                    in[5][size_t (i)] = 0.2f * std::sin (2.0f * float (M_PI) * 330.0f * t);
                }
                for (size_t ch = 0; ch < ip.size(); ++ch) ip[ch] = in[ch].data();
                float* op[2] = { l.data(), r.data() };
                c.process (ip.data(), 6, op, 2, kBlock);
                pos += kBlock;
                if (poll && (b % 8) == 0) c.poll();
                if ((b % 4) == 0) std::this_thread::sleep_for (std::chrono::microseconds (400));
            }
        }
        float outputPeak() const { float p = 0.0f; for (float x : l) p = std::max (p, std::fabs (x)); return p; }

        bool waitFor (MixController::Stage stage, int ms = 4000)
        {
            for (int i = 0; i < ms / 10; ++i)
            {
                c.poll();
                if (c.getStage() == stage) return true;
                play (0.05, false);
            }
            return false;
        }

        // A live run spends most of its time off a listen, so it is waited on by its own
        // state machine rather than by the controller's stage. The band keeps playing
        // throughout, exactly as it would at a soundcheck.
        bool waitForLiveTune (int ms = 30000)
        {
            for (int i = 0; i < ms / 10; ++i)
            {
                c.poll();
                if (! c.isTuningLive()) return true;
                play (0.05, false);
            }
            return false;
        }
    };
}

TEST_CASE ("MixController: TUNE LIVE MIX listens, builds, verifies and leaves a mix you can compare and revert")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.5);

    const MixParameters beforeAnything = c.getKept();
    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };

    // No provider was configured, so this runs on the offline engineer: no network, no key,
    // nothing leaves the machine. That is the default and it has to work on its own.
    CHECK (c.getTuneLive().getProvider()->getName() == "DLIVE built-in (offline)");
    CHECK (! c.getTuneLive().getProvider()->sendsDataExternally());

    // Long enough to be worth mixing from: a listen that measured nothing is refused on
    // purpose, so a test cannot ask for a confident mix off two seconds either.
    MixController::LiveTuneSettings settings;
    settings.initial = { 7.0f, -200.0f, 0.0f };
    settings.verify = { 6.5f, -200.0f, 0.0f };
    c.startTuneLiveMix (settings);
    CHECK (c.isTuningLive());
    CHECK (c.isListening());

    // The professional mix arrives before the reasoning does: the deterministic plan is
    // audible while DLIVE is still working out what else this band needs.
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    CHECK (c.isTuningLive());
    REQUIRE (c.hasPlan());

    REQUIRE (f.waitForLiveTune());
    CHECK (c.getTuneLive().getState() == TuneLiveCoordinator::State::Ready);
    CHECK (c.getStage() == MixController::Stage::Preview);
    REQUIRE (c.hasPlan());
    CHECK (c.getPlan()->headline == "LIVE MIX READY");
    CHECK (! messages.empty());
    CHECK (f.outputPeak() > 0.0001f);              // the audio never stopped for any of it

    // BEFORE is the complete pre-Tune snapshot and AFTER is the mix that was built from it,
    // so the ordinary comparison, KEEP and REVERT all work on a live run unchanged.
    CHECK (MixPlanner::countParameterChanges (c.getPlan()->before, beforeAnything) == 0);
    CHECK (MixPlanner::countParameterChanges (c.getPlan()->before, c.getPlan()->proposed) > 0);
    c.setCompare (MixController::Compare::Before);
    CHECK (c.getBase().strips[0].faderDb == c.getPlan()->before.strips[0].faderDb);
    c.setCompare (MixController::Compare::After);

    // What happened is readable, and the run is stored with the session without a provider.
    CHECK (! c.getTuneLive().getReviewLines().empty());
    const auto diag = c.getTuneLive().getDiagnostics();
    CHECK (! diag.sentDataExternally);
    CHECK (diag.refinementRan);

    // REVERT puts the console back exactly where it started.
    c.revertPlan();
    CHECK (MixPlanner::countParameterChanges (c.getKept(), beforeAnything) == 0);
    CHECK (! c.isTuningLive());
}

TEST_CASE ("MixController: when the reasoning provider fails, the deterministic mix is what you are left with")
{
    struct DeadProvider final : MixReasoningProvider
    {
        std::string getName() const override { return "Unreachable"; }
        bool isAvailable() const override { return false; }
        bool sendsDataExternally() const override { return true; }
        MixReasoningResponse reason (const MixReasoningRequest&, const std::atomic<bool>&) override
        {
            MixReasoningResponse r;
            r.error = "Internet connection unavailable.";
            return r;
        }
    };

    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    c.setReasoningProvider (std::make_shared<DeadProvider>());

    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };

    MixController::LiveTuneSettings settings;
    settings.initial = { 7.0f, -200.0f, 0.0f };
    c.startTuneLiveMix (settings);
    REQUIRE (f.waitForLiveTune (20000));

    // The mix is still there, still professional, still comparable - and the app said what
    // happened instead of pretending it had done something.
    CHECK (c.getStage() == MixController::Stage::Preview);
    REQUIRE (c.hasPlan());
    CHECK (f.outputPeak() > 0.0001f);
    bool saidSo = false;
    for (const auto& m : messages) if (m.find ("Internet connection unavailable") != std::string::npos) saidSo = true;
    CHECK (saidSo);
    c.keepPlan();
    CHECK (c.getStage() == MixController::Stage::Mixed);
}

TEST_CASE ("MixController: setup -> ready -> listening -> preview -> keep, with BEFORE / AFTER and macros on top")
{
    MixController c;
    CHECK (c.getStage() == MixController::Stage::Setup);
    c.setSession (band());
    c.prepare (kSr, kBlock);
    REQUIRE (c.isPrepared());
    CHECK (c.getStage() == MixController::Stage::Ready);
    CHECK (c.getEngine().getNumStrips() == 5);
    CHECK (c.getMixHealthPercent() == 0);

    Feeder f (c);
    f.play (0.5);
    CHECK (f.outputPeak() > 0.01f);   // the baselines pass audio before any Tune

    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    CHECK (c.isListening());
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    REQUIRE (c.hasPlan());
    CHECK (c.getPlan()->headline == "MIX TUNED");
    CHECK (c.getTuneCount() == 1);
    CHECK (! messages.empty());
    CHECK (c.getMixHealthPercent() > 50);

    // AFTER is what the plan proposed; BEFORE is what ran during the listen.
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), c.getPlan()->proposed) == 0);
    c.setCompare (MixController::Compare::Before);
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), c.getPlan()->before) == 0);
    CHECK (c.getBase().strips[0].faderDb == c.getPlan()->before.strips[0].faderDb);
    c.setCompare (MixController::Compare::After);

    c.keepPlan();
    CHECK (c.getStage() == MixController::Stage::Mixed);
    CHECK (MixPlanner::countParameterChanges (c.getKept(), c.getPlan()->proposed) == 0);
    CHECK (! c.getStatusText().empty());                 // the health notes in plain words, or READY
    CHECK (c.getMixHealthPercent() > 50);
    CHECK (! c.getMixHealthNotes().empty());

    // Macros sit on top of the kept plan and never touch it.
    const MixParameters keptBefore = c.getKept();
    c.setMacro (MixMacro::Space, 90.0f);
    CHECK (MixPlanner::countParameterChanges (c.getKept(), keptBefore) == 0);
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), keptBefore) > 0);
    CHECK (c.getRunning().strips[3].sendDb[size_t (FxSlot::VocalPlate)] > keptBefore.strips[3].sendDb[size_t (FxSlot::VocalPlate)]);
    c.resetMacros();
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), keptBefore) == 0);

    // Advanced edits go into the kept mix.
    c.setStripFader (0, -3.0f);
    c.setStripMute (1, true);
    c.setStripSolo (0, true);
    c.setStripInputGain (4, 6.0f);
    CHECK (c.getKept().strips[0].faderDb == -3.0f);
    CHECK (c.getKept().strips[1].mute);
    CHECK (c.getKept().strips[0].solo);
    CHECK (c.getKept().strips[4].inputGainDb == 6.0f);
    CHECK (c.getRunning().strips[0].faderDb == -3.0f);
    CHECK (c.getRunning().strips[0].solo);
    c.clearSolos();
    CHECK (! c.getKept().strips[0].solo);
    f.play (0.3);
    CHECK (f.outputPeak() > 0.001f);
}

TEST_CASE ("MixController: the Inspector's chain edits live on the kept mix, reach the engine and survive a macro")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    // A hand edit on one strip: the whole chain arrives at once, the way the engine takes it.
    auto channel = c.getKept().strips[0].channel;
    channel.compEnabled = true;
    channel.compThresholdDb = -18.5f;
    channel.toneBands[1] = { true, FilterType::Peak, 900.0f, -3.5f, 1.6f };
    c.setStripChannel (0, channel);
    CHECK (c.getKept().strips[0].channel.compEnabled);
    CHECK (c.getKept().strips[0].channel.compThresholdDb == -18.5f);
    CHECK (c.getRunning().strips[0].channel.toneBands[1].freqHz == 900.0f);

    // And on a bus, where only the master owns a limiter.
    auto master = c.getKept().buses[size_t (MixBus::Master)].channel;
    master.limiterCeilingDb = -2.5f;
    c.setBusChannel (MixBus::Master, master);
    CHECK (c.getKept().buses[size_t (MixBus::Master)].channel.limiterCeilingDb == -2.5f);
    CHECK (c.getRunning().buses[size_t (MixBus::Master)].channel.limiterCeilingDb == -2.5f);

    // A macro moves on top of the edit; the edit itself is untouched and comes back.
    const MixParameters keptWithEdit = c.getKept();
    c.setMacro (MixMacro::Energy, 80.0f);
    CHECK (MixPlanner::countParameterChanges (c.getKept(), keptWithEdit) == 0);
    CHECK (c.getRunning().strips[0].channel.compThresholdDb == -18.5f);
    c.resetMacros();
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), keptWithEdit) == 0);

    // BYPASS hears the inputs with none of it, and switching it off puts the mix back.
    c.setBypass (true);
    CHECK (c.getRunning().bypassProcessing);
    CHECK (c.getRunning().strips[0].channel.compThresholdDb != -18.5f);
    CHECK (MixPlanner::countParameterChanges (c.getKept(), keptWithEdit) == 0);
    c.setBypass (false);
    CHECK (c.getRunning().strips[0].channel.compThresholdDb == -18.5f);

    f.play (0.3);
    CHECK (f.outputPeak() > 0.001f);
}

TEST_CASE ("MixController: TUNE CHANNEL tunes one source and leaves the rest of the mix exactly where it is")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    // A whole mix first, so the channel tune has a real mix to leave alone.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    const MixParameters afterMix = c.getKept();

    // Bass on its own.
    const int strip = 1;
    c.startTuneChannel (strip, { 2.0f, -200.0f, 0.0f });
    CHECK (c.isListening());
    CHECK (c.isTuningChannel());
    CHECK (c.getTuningStrip() == strip);
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    REQUIRE (c.hasPlan());
    const auto* plan = c.getPlan();
    CHECK (plan->headline.find ("BASS") == 0);

    // Everything else is untouched: same chain, same fader, same gain, same sends, same buses.
    for (int i = 0; i < plan->before.numStrips; ++i)
    {
        if (i == strip) continue;
        CHECK (diffParameters (plan->before.strips[size_t (i)].channel, plan->proposed.strips[size_t (i)].channel).empty());
        CHECK (plan->before.strips[size_t (i)].faderDb == plan->proposed.strips[size_t (i)].faderDb);
        CHECK (plan->before.strips[size_t (i)].inputGainDb == plan->proposed.strips[size_t (i)].inputGainDb);
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        CHECK (diffParameters (plan->before.buses[size_t (b)].channel, plan->proposed.buses[size_t (b)].channel).empty());
        CHECK (plan->before.buses[size_t (b)].faderDb == plan->proposed.buses[size_t (b)].faderDb);
    }
    // ... and the listen still measured every input, so the gain-staging advice is fresh for all of them.
    for (int i = 0; i < c.getEngine().getNumStrips(); ++i) CHECK (c.getInputAdvice (i).known);
    CHECK (c.getMixHealthPercent() > 50);

    // BEFORE is the mix as it was kept; KEEP applies the channel and nothing else.
    c.setCompare (MixController::Compare::Before);
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), afterMix) == 0);
    c.setCompare (MixController::Compare::After);
    c.keepPlan();
    CHECK (c.getStage() == MixController::Stage::Mixed);
    CHECK (MixPlanner::countParameterChanges (c.getKept(), c.getPlan()->proposed) == 0);
    for (int i = 0; i < c.getKept().numStrips; ++i)
        if (i != strip)
            CHECK (diffParameters (afterMix.strips[size_t (i)].channel, c.getKept().strips[size_t (i)].channel).empty());

    // REVERT after a channel tune puts that channel back and touches nothing else.
    const MixParameters afterChannel = c.getKept();
    c.startTuneChannel (strip, { 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.revertPlan();
    CHECK (! c.hasPlan());
    CHECK (! c.isTuningChannel());
    CHECK (MixPlanner::countParameterChanges (c.getKept(), afterChannel) == 0);
}

TEST_CASE ("MixController: a channel that never played is told so, and nothing is proposed for it")
{
    MixController c;
    MixSession s = band();
    s.inputs.push_back ({ "Pastor", ChannelRole::Speech, 6, -1 });     // channel 6 is never fed
    c.setSession (s);
    c.prepare (kSr, kBlock);

    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };

    Feeder f (c);
    f.in.resize (7, std::vector<float> (size_t (kBlock), 0.0f));
    f.ip.resize (7, nullptr);
    c.startTuneChannel (5, { 1.0f, -200.0f, 0.0f });
    f.play (1.6);
    for (int i = 0; i < 300 && c.getStage() == MixController::Stage::Listening; ++i)
    {
        c.poll();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    CHECK (c.getStage() != MixController::Stage::Preview);
    bool saidNotHeard = false;
    for (const auto& m : messages) if (m.find ("NOT HEARD") != std::string::npos) saidNotHeard = true;
    CHECK (saidNotHeard);
    // The listen still heard the band, so what it knows about the other inputs is kept.
    CHECK (c.getInputAdvice (0).known);
}

TEST_CASE ("MixController: revert restores the mix that ran before the listen; abort returns to ready; a silent listen makes no plan")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    const MixParameters original = c.getKept();

    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.revertPlan();
    CHECK (c.getStage() == MixController::Stage::Ready);
    CHECK (! c.hasPlan());
    CHECK (MixPlanner::countParameterChanges (c.getKept(), original) == 0);
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), original) == 0);

    c.startTuneMix ({ 5.0f, -45.0f, 30.0f });
    CHECK (c.isListening());
    for (int i = 0; i < 50 && ! c.isWaitingForBand(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (2));
    CHECK (c.isWaitingForBand());
    c.abortTuneMix();
    CHECK (c.getStage() == MixController::Stage::Ready);

    // Silence: the listen starts straight away, hears nothing, and says so instead of inventing a mix.
    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };
    c.startTuneMix ({ 1.0f, -200.0f, 0.0f });
    for (auto& ch : f.in) std::fill (ch.begin(), ch.end(), 0.0f);
    {
        std::vector<const float*> ip (6);
        for (size_t ch = 0; ch < 6; ++ch) ip[ch] = f.in[ch].data();
        float* op[2] = { f.l.data(), f.r.data() };
        for (int b = 0; b < int (1.5 * kSr / kBlock); ++b)
        {
            c.process (ip.data(), 6, op, 2, kBlock);
            if ((b % 4) == 0) std::this_thread::sleep_for (std::chrono::microseconds (400));
            if ((b % 8) == 0) c.poll();
        }
    }
    for (int i = 0; i < 300 && c.getStage() == MixController::Stage::Listening; ++i) { c.poll(); std::this_thread::sleep_for (std::chrono::milliseconds (5)); }
    CHECK (c.getStage() == MixController::Stage::Ready);
    CHECK (! c.hasPlan());
    bool saidNoSignal = false;
    for (const auto& m : messages) if (m.find ("NO SIGNAL") != std::string::npos) saidNoSignal = true;
    CHECK (saidNoSignal);
}

TEST_CASE ("MixController: changing the session and preparing again rebuilds the graph and clears the plan")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    c.startTuneMix ({ 1.5f, -200.0f, 0.0f });
    f.play (2.0);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    MixSession s = band();
    s.inputs.push_back ({ "Pastor", ChannelRole::Speech, 6, -1 });
    c.setSession (s);
    CHECK (c.getStage() == MixController::Stage::Setup);
    c.prepare (kSr, kBlock);
    CHECK (c.getEngine().getNumStrips() == 6);
    CHECK (! c.hasPlan());
    CHECK (c.getTuneCount() == 0);
    CHECK (c.getStage() == MixController::Stage::Ready);
}

TEST_CASE ("SessionStore: a session document survives the JSON round trip")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    MixParameters kept = c.getKept();
    kept.strips[0].faderDb = -4.5f;
    kept.strips[2].inputGainDb = 3.0f;
    kept.strips[3].sendDb[size_t (FxSlot::VocalPlate)] = -7.0f;
    kept.strips[1].mute = true;
    kept.strips[4].channel.compThresholdDb = -27.5f;
    kept.strips[4].channel.toneBands[2] = { true, FilterType::Peak, 3200.0f, 2.5f, 1.1f };
    kept.buses[size_t (MixBus::Drums)].channel.compRatio = 3.3f;
    kept.master().channel.limiterCeilingDb = -1.5f;
    kept.fx[size_t (FxSlot::VocalPlate)].fx.reverbDecayS = 2.4f;
    kept.fx[size_t (FxSlot::VocalPlate)].returnDb = -2.0f;
    kept.tempoBpm = 96.0f;   // the tempo the synced delays are in time with

    SessionStore::Document d;
    d.session = c.getSession();
    d.session.purpose = MixPurpose::Livestream;
    d.session.profile = StyleProfileId::ModernWorship;
    d.inputDevice = "Dante Virtual Soundcard";
    d.outputDevice = "Dante Virtual Soundcard";
    d.macros.set (MixMacro::Space, 70.0f);
    d.macros.set (MixMacro::Drums, 35.0f);
    d.hasMix = true;
    d.mix = kept;
    d.tuneCount = 2;

    const juce::var v = SessionStore::toVar (d);
    const juce::String text = juce::JSON::toString (v);
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (text), back));
    CHECK (back.session.name == "Test Sunday");
    CHECK_NEAR (back.mix.tempoBpm, 96.0f, 0.01f);   // ... and it comes back, or the delays fall out of time
    CHECK (back.session.purpose == MixPurpose::Livestream);
    CHECK (back.session.profile == StyleProfileId::ModernWorship);
    REQUIRE (back.session.inputs.size() == 5);
    CHECK (back.session.inputs[2].name == "Keys");
    CHECK (back.session.inputs[2].role == ChannelRole::Piano);
    CHECK (back.session.inputs[2].inputA == 2);
    CHECK (back.session.inputs[2].inputB == 3);
    CHECK (back.inputDevice == "Dante Virtual Soundcard");
    CHECK (back.macros.get (MixMacro::Space) == 70.0f);
    CHECK (back.macros.get (MixMacro::Drums) == 35.0f);
    CHECK (back.macros.get (MixMacro::Energy) == 50.0f);
    CHECK (back.tuneCount == 2);
    REQUIRE (back.hasMix);
    CHECK (back.mix.numStrips == 5);
    CHECK (MixPlanner::countParameterChanges (back.mix, kept) == 0);
    CHECK (back.mix.strips[1].mute);
    CHECK_NEAR (back.mix.strips[4].channel.toneBands[2].freqHz, 3200.0f, 0.01f);
    CHECK_NEAR (back.mix.fx[size_t (FxSlot::VocalPlate)].fx.reverbDecayS, 2.4f, 1e-4);
    CHECK_NEAR (back.mix.fx[size_t (FxSlot::VocalPlate)].returnDb, -2.0f, 1e-4);
    CHECK_NEAR (back.mix.master().channel.limiterCeilingDb, -1.5f, 1e-4);

    // Not a DLIVE file: refused, nothing changed.
    SessionStore::Document untouched;
    CHECK (! SessionStore::fromVar (juce::JSON::parse ("{\"app\":\"other\"}"), untouched));
    CHECK (! SessionStore::fromVar (juce::var(), untouched));

    // The kept mix restored into a controller runs.
    MixController c2;
    c2.setSession (back.session);
    c2.prepare (kSr, kBlock);
    c2.setKept (back.mix);
    CHECK (c2.getKept().strips[0].faderDb == -4.5f);
    CHECK (c2.getStage() == MixController::Stage::Mixed);
}

TEST_CASE ("SessionStore: save, list, and load a named mix file")
{
    SessionStore::Document d;
    d.session = band();
    d.session.name = "ListTest Sunday";
    d.inputDevice = "In";
    d.outputDevice = "Out";
    d.hasMix = false;
    // Write into the real sessions folder via a unique name, or fall back to a temp file for the round trip.
    const auto file = juce::File ("/Users/calebwork/Documents/GitHub/Calive/.tmp-session-test.dlive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (d, file));
    REQUIRE (file.existsAsFile());

    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));
    CHECK (back.session.name == "ListTest Sunday");
    CHECK (back.outputDevice == "Out");
    file.deleteFile();

    (void) SessionStore::listSessions();
}


TEST_CASE ("OpenAiMixProvider: asks for intent under a strict schema, sends no audio, and refuses prose")
{
    MixReasoningRequest request;
    request.context.sessionName = "Test Sunday";
    request.context.profile = "Modern Gospel";
    request.context.purpose = "Church Broadcast";
    request.instructions = mixEngineerInstructions (StyleProfileId::ModernGospel, MixPurpose::ChurchBroadcast);
    request.capabilities = json::Value::object().set ("targets", json::Value::array());

    AISettings settings;
    settings.apiKey = "sk-not-a-real-key";
    settings.model = "gpt-5";
    const auto body = OpenAiMixProvider::buildRequestBody (request, settings);

    // A strict schema, and an enum of the objectives the resolver can actually build: the model
    // cannot ask for a kind of change DLIVE has no way to express.
    CHECK (body.contains ("\"strict\": true"));
    CHECK (body.contains ("dlive_mix_intent"));
    CHECK (body.contains ("spatial_depth"));
    CHECK (body.contains ("Test Sunday"));
    // Nothing that is not a measurement or a capability leaves the machine - no audio, ever,
    // and never the key in anything but the Authorization header.
    CHECK (! body.contains ("samples"));
    CHECK (! body.contains ("sk-not-a-real-key"));

    // A refusal, a truncation and an HTTP error each come back as a sentence, not as a mix.
    CHECK (! OpenAiMixProvider::parseResponse ("{\"error\":{\"message\":\"no quota\"}}", 429, "gpt-5").valid);
    CHECK (OpenAiMixProvider::parseResponse ("{\"error\":{\"message\":\"no quota\"}}", 429, "gpt-5").error.find ("no quota") != std::string::npos);
    CHECK (! OpenAiMixProvider::parseResponse (R"({"choices":[{"message":{"content":"I think the keys are loud."}}]})", 200, "gpt-5").valid);

    // A well-formed answer becomes an intent, and nothing else in it is taken as read.
    const auto good = OpenAiMixProvider::parseResponse (
        R"({"choices":[{"finish_reason":"stop","message":{"content":"{\"schemaVersion\":1,\"summary\":\"ok\",)"
        R"(\"noChangeRequired\":false,\"unsupportedRequests\":[],\"targets\":[{\"target\":\"strip:2\",\"name\":\"Keys\",)"
        R"(\"reason\":\"Covering the voice.\",\"confidence\":\"HIGH\",\"objectives\":[{\"type\":\"separation\",)"
        R"(\"strength\":0.6,\"against\":\"strip:4\",\"character\":\"\",\"preserveArticulation\":true,)"
        R"(\"preserveTransients\":false}]}]}"}}]})", 200, "gpt-5");
    REQUIRE (good.valid);
    REQUIRE (good.intent.targets.size() == 1);
    CHECK (good.intent.targets[0].target.index == 2);
    CHECK (good.intent.targets[0].objectives[0].type == MixObjectiveType::Separation);
    CHECK (good.intent.targets[0].objectives[0].against.index == 4);
}
