// DINE application-layer tests: the controller's state machine (setup -> listen -> plan ->
// preview -> keep / revert, compare, macros, Advanced edits) and the session document round trip.
// No device, no UI: the engine is fed synthetic audio through MixController::process().
#include "TestFramework.h"
#include "native/MixController.h"
#include "native/SampleLibrary.h"
#include "native/DevicePlan.h"
#include "native/DeviceState.h"
#include "native/SessionStore.h"
#include "Profiles/MixProfileData.h"
#include "Mix/MixPlanner.h"
#include "MixAI/MixReasoningProvider.h"
#include "native/OpenAiMixProvider.h"
#include "native/ReferenceAudio.h"
#include <juce_audio_formats/juce_audio_formats.h>
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
        float leadGain = 1.0f;          // the singer stepping in to, or back from, the microphone
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
                    in[4][size_t (i)] = leadGain * 0.3f * std::sin (2.0f * float (M_PI) * 220.0f * t);
                    // the backing voice sings in phrases, and its microphone hears the stage between them
                    in[5][size_t (i)] = (std::fmod (t, 0.5f) < 0.38f ? 0.2f * std::sin (2.0f * float (M_PI) * 330.0f * t) : 0.0f)
                                      + 0.004f * std::sin (2.0f * float (M_PI) * 1234.0f * t);
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
    CHECK (c.getTuneLive().getProvider()->getName() == "DINE built-in (offline)");
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
    // audible while DINE is still working out what else this band needs.
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

// A TUNE THAT IS NOT A LIVE RUN IS NOT THE LAST LIVE RUN. The coordinator stays Ready once
// TUNE LIVE MIX has finished, and the result card reads that state to decide what it is a
// card about - so a TUNE DRUMS started afterwards used to title itself TUNE LIVE MIX and list
// the reasoning layer's sentences about the voices under a scope that said DRUMS.
TEST_CASE ("MixController: an ordinary tune started after a live run does not inherit its review")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.5);

    MixController::LiveTuneSettings settings;
    settings.initial = { 7.0f, -200.0f, 0.0f };
    settings.verify = { 6.5f, -200.0f, 0.0f };
    c.startTuneLiveMix (settings);
    REQUIRE (f.waitForLiveTune());
    REQUIRE (c.getTuneLive().getState() == TuneLiveCoordinator::State::Ready);
    REQUIRE (! c.getTuneLive().getReviewLines().empty());
    c.keepPlan();

    // Now one group on its own, the ordinary way.
    c.startTuneBus (MixBus::Drums, { 7.0f, -200.0f, 0.0f });
    CHECK (! c.isTuningLive());
    CHECK (c.getTuneLive().getState() == TuneLiveCoordinator::State::Idle);
    CHECK (c.getTuneLive().getReviewLines().empty());
    REQUIRE (f.waitFor (MixController::Stage::Preview, 20000));
    CHECK (c.getLastTuneScope() == std::string (mixBusName (MixBus::Drums)));
    CHECK (c.getTuneLive().getState() == TuneLiveCoordinator::State::Idle);
    CHECK (c.getTuneLive().getReviewLines().empty());
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

TEST_CASE ("MixController: the master's voicing rides on top of the kept mix; the loudness lift raises the master under the limiter")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    const MixParameters kept = c.getKept();

    // Neutral is exactly the kept mix; a voicing touches the master and nothing else, and is
    // never written into the kept mix.
    CHECK (c.getVoicing() == MasterVoicing::Neutral);
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), kept) == 0);
    c.setVoicing (MasterVoicing::Warm);
    CHECK (MixPlanner::countParameterChanges (c.getKept(), kept) == 0);
    const auto& warm = c.getRunning().master().channel;
    CHECK (warm.toneEqEnabled);
    CHECK (warm.toneBands[0].gainDb > kept.master().channel.toneBands[0].gainDb);
    CHECK (warm.toneBands[3].gainDb < kept.master().channel.toneBands[3].gainDb);
    MixParameters onlyMaster = kept;
    onlyMaster.master() = c.getRunning().master();
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), onlyMaster) == 0);
    c.setVoicing (MasterVoicing::PhoneSpeakers);
    CHECK (c.getRunning().master().channel.toneBands[0].gainDb < kept.master().channel.toneBands[0].gainDb);
    c.setVoicing (MasterVoicing::Neutral);
    CHECK (MixPlanner::countParameterChanges (c.getRunning(), kept) == 0);

    // Nothing has played: the lift says so and does nothing.
    CHECK (! c.previewLoudnessMove().possible);
    const float trimBefore = c.getKept().master().channel.outputTrimDb;
    c.raiseLoudnessToTarget();
    CHECK (c.getKept().master().channel.outputTrimDb == trimBefore);

    // The band plays under the YouTube target: one press raises the master toward it, the
    // limiter is on at the delivery ceiling, and UNDO takes it back.
    c.setDelivery (DeliveryLoudness::StreamingLoud);
    f.play (3.0);
    const auto move = c.previewLoudnessMove();
    REQUIRE (move.possible);
    CHECK (move.moveDb > 0.0f);
    CHECK_NEAR (move.targetLufs, -14.0f, 0.01f);
    c.raiseLoudnessToTarget();
    CHECK (c.getKept().master().channel.outputTrimDb > trimBefore);
    CHECK (c.getKept().master().channel.limiterEnabled);
    CHECK (c.getKept().master().channel.limiterCeilingDb <= -1.0f);
    CHECK (c.getRunning().master().channel.outputTrimDb == c.getKept().master().channel.outputTrimDb);
    f.play (1.0);
    CHECK (f.outputPeak() <= 1.0f);                   // the limiter's promise
    REQUIRE (c.canUndoMix());
    c.undoMix();
    CHECK (c.getKept().master().channel.outputTrimDb == trimBefore);

    // LIVE SAFE limits the step the way it limits the master fader, and says so.
    c.setLiveSafe (true);
    std::string last;
    c.onMessage = [&last] (const std::string& m) { last = m; };
    c.raiseLoudnessToTarget();
    CHECK (c.getKept().master().channel.outputTrimDb - trimBefore <= c.getLiveSafePolicy().maxMasterStepDb + 0.01f);
    CHECK (! last.empty());
    c.setLiveSafe (false);
}

TEST_CASE ("MixController: under LIVE SAFE a macro is fenced to the plan's neighbourhood, and the fence is what the pads read")
{
    MixController c;
    // Off: the whole travel, and the range says so.
    c.setMacro (MixMacro::Space, 92.0f);
    CHECK (c.getMacros().get (MixMacro::Space) == 92.0f);
    CHECK (c.macroRange().lo == 0.0f);
    CHECK (c.macroRange().hi == 100.0f);
    CHECK (liveSafe::macroLimitReason (c.getLiveSafePolicy()).empty());

    // On: a value past the fence lands on it rather than being refused, on both sides,
    // and the range the pads draw is the same one the controller enforces.
    c.setLiveSafe (true);
    const auto range = c.macroRange();
    const float e = c.getLiveSafePolicy().maxMacroExcursion;
    CHECK_NEAR (range.lo, 50.0f - e, 0.001f);
    CHECK_NEAR (range.hi, 50.0f + e, 0.001f);
    c.setMacro (MixMacro::Drums, 100.0f);
    CHECK_NEAR (c.getMacros().get (MixMacro::Drums), range.hi, 0.001f);
    c.setMacro (MixMacro::Bass, 0.0f);
    CHECK_NEAR (c.getMacros().get (MixMacro::Bass), range.lo, 0.001f);
    c.setMacro (MixMacro::Vocals, 60.0f);
    CHECK (c.getMacros().get (MixMacro::Vocals) == 60.0f);        // inside the fence: untouched
    CHECK (! liveSafe::macroLimitReason (c.getLiveSafePolicy()).empty());
    c.setLiveSafe (false);
    c.setMacro (MixMacro::Drums, 100.0f);
    CHECK (c.getMacros().get (MixMacro::Drums) == 100.0f);
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

TEST_CASE ("MixController: TUNE <these channels> moves those channels and nothing else")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    // A whole mix first, so there is a real mix for the narrowed tune to leave alone.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    const MixParameters afterMix = c.getKept();

    // Lead and Vox: two voices, picked by hand rather than as a group.
    const std::vector<int> picked { 3, 4 };
    c.startTuneStrips (picked, { 2.0f, -200.0f, 0.0f });
    CHECK (c.isListening());
    CHECK (c.isTuningStrips());
    CHECK (c.isTuningPart());
    CHECK (! c.isTuningChannel());
    CHECK (! c.isTuningBus());
    CHECK (c.getTuningStrips() == picked);
    CHECK (c.getTuningName() == std::string ("2 channels"));
    // The card has to be able to say what it is a card about, in names.
    CHECK (c.getLastTuneScope() == std::string ("Lead, Vox"));

    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    REQUIRE (c.hasPlan());
    const auto* plan = c.getPlan();
    for (int i = 0; i < plan->before.numStrips; ++i)
    {
        if (i == 3 || i == 4) continue;
        CHECK (diffParameters (plan->before.strips[size_t (i)].channel, plan->proposed.strips[size_t (i)].channel).empty());
        CHECK (plan->before.strips[size_t (i)].faderDb == plan->proposed.strips[size_t (i)].faderDb);
        CHECK (plan->before.strips[size_t (i)].inputGainDb == plan->proposed.strips[size_t (i)].inputGainDb);
    }
    // No group and no master moves: a handful of channels is not a master decision.
    for (int b = 0; b < int (MixBus::Count); ++b)
        CHECK (diffParameters (plan->before.buses[size_t (b)].channel, plan->proposed.buses[size_t (b)].channel).empty());

    c.keepPlan();
    for (int i = 0; i < c.getKept().numStrips; ++i)
        if (i != 3 && i != 4)
            CHECK (diffParameters (afterMix.strips[size_t (i)].channel, c.getKept().strips[size_t (i)].channel).empty());

    // ONE PICKED CHANNEL IS TUNE CHANNEL. The picker never has to say so, and the shorter
    // listen a single source needs comes with it.
    c.startTuneStrips ({ 2 });
    CHECK (c.isTuningChannel());
    CHECK (! c.isTuningStrips());
    CHECK (c.getTuningName() == std::string ("Keys"));
    c.abortTuneMix();

    // Nothing picked is refused with a sentence rather than listened to.
    std::string said;
    c.onMessage = [&said] (const std::string& m) { said = m; };
    c.startTuneStrips ({});
    CHECK (! c.isListening());
    CHECK (said.find ("Pick the channels") != std::string::npos);

    // Out of range is dropped, duplicates collapse, and the order is the console's.
    said.clear();
    c.startTuneStrips ({ 4, 99, 3, 4, -1 }, { 2.0f, -200.0f, 0.0f });
    CHECK (c.isListening());
    const std::vector<int> tidied { 3, 4 };
    CHECK (c.getTuningStrips() == tidied);
    c.abortTuneMix();
}

TEST_CASE ("MixController: TUNE <GROUP> tunes one group, and KEEP SOME keeps only what is switched on")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    // A whole mix first, so the group tune and the partial keep have a real mix to leave alone.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    const MixParameters afterMix = c.getKept();

    // ---- TUNE VOCALS: the voices and the vocal group move, the band and the master do not.
    c.startTuneBus (MixBus::Vocals, { 2.0f, -200.0f, 0.0f });
    CHECK (c.isListening());
    CHECK (c.isTuningBus());
    CHECK (c.isTuningPart());
    CHECK (! c.isTuningChannel());
    CHECK (c.getTuningBus() == MixBus::Vocals);
    CHECK (c.getTuningName() == std::string ("BGV"));      // VOCALS is the backing voices now
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    REQUIRE (c.hasPlan());
    const auto* plan = c.getPlan();
    CHECK (plan->headline.find ("BGV") == 0);
    const auto& graph = c.getGraph();
    for (int i = 0; i < plan->before.numStrips; ++i)
    {
        if (graph.strips[size_t (i)].bus == MixBus::Vocals) continue;
        CHECK (diffParameters (plan->before.strips[size_t (i)].channel, plan->proposed.strips[size_t (i)].channel).empty());
        CHECK (plan->before.strips[size_t (i)].faderDb == plan->proposed.strips[size_t (i)].faderDb);
        CHECK (plan->before.strips[size_t (i)].inputGainDb == plan->proposed.strips[size_t (i)].inputGainDb);
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        if (MixBus (b) == MixBus::Vocals) continue;
        CHECK (diffParameters (plan->before.buses[size_t (b)].channel, plan->proposed.buses[size_t (b)].channel).empty());
    }
    c.keepPlan();
    CHECK (c.getStage() == MixController::Stage::Mixed);
    for (int i = 0; i < c.getKept().numStrips; ++i)
        if (graph.strips[size_t (i)].bus != MixBus::Vocals)
            CHECK (diffParameters (afterMix.strips[size_t (i)].channel, c.getKept().strips[size_t (i)].channel).empty());
    const MixParameters afterVocals = c.getKept();

    // ---- KEEP SOME on a whole-mix proposal: AFTER plays the selection, KEEP applies it.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    REQUIRE (c.hasPlan());
    const MixParameters whole = c.getPlan()->proposed;
    CHECK (! c.hasPlanSelection());

    auto drumsOnly = MixPlanner::PlanSelection::group (c.getGraph(), MixBus::Drums);
    c.setPlanSelection (drumsOnly);
    REQUIRE (c.hasPlanSelection());
    // What is audible is the selection, not the whole proposal: the drum strips are the
    // proposal's, everything else is the mix as it was before the listen.
    for (int i = 0; i < c.getBase().numStrips; ++i)
    {
        const bool onDrums = c.getGraph().strips[size_t (i)].bus == MixBus::Drums;
        const auto& want = onDrums ? whole.strips[size_t (i)] : afterVocals.strips[size_t (i)];
        CHECK (diffParameters (c.getBase().strips[size_t (i)].channel, want.channel).empty());
        CHECK (c.getBase().strips[size_t (i)].faderDb == want.faderDb);
    }
    CHECK (diffParameters (c.getBase().master().channel, afterVocals.master().channel).empty());

    // BEFORE is still the whole mix as it was, whatever is selected.
    c.setCompare (MixController::Compare::Before);
    CHECK (MixPlanner::countParameterChanges (c.getBase(), afterVocals) == 0);
    c.setCompare (MixController::Compare::After);

    // Clearing it puts the whole proposal back.
    c.clearPlanSelection();
    CHECK (! c.hasPlanSelection());
    CHECK (MixPlanner::countParameterChanges (c.getBase(), whole) == 0);

    // KEEP with a selection applies exactly what AFTER was playing, and nothing else.
    c.keepPlanSelection (drumsOnly);
    CHECK (c.getStage() == MixController::Stage::Mixed);
    for (int i = 0; i < c.getKept().numStrips; ++i)
    {
        const bool onDrums = c.getGraph().strips[size_t (i)].bus == MixBus::Drums;
        const auto& want = onDrums ? whole.strips[size_t (i)] : afterVocals.strips[size_t (i)];
        CHECK (diffParameters (c.getKept().strips[size_t (i)].channel, want.channel).empty());
    }
    CHECK (diffParameters (c.getKept().master().channel, afterVocals.master().channel).empty());
    // A selection never outlives the proposal it narrowed.
    CHECK (! c.hasPlanSelection());

    // And it is one mix change, so UNDO takes the whole of it back.
    c.undoMix();
    CHECK (MixPlanner::countParameterChanges (c.getKept(), afterVocals) == 0);
}

TEST_CASE ("MixController: pinning the focal source settles what the mix is built around, and survives a save")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    CHECK (c.getFocusInput() == -1);              // nobody has said: DINE decides from the listen

    const MixParameters before = c.getKept();
    c.setFocusInput (3);                          // "Lead"
    CHECK (c.getFocusInput() == 3);
    CHECK (c.getSession().inputs[3].focus);
    // Nothing you can hear moves: it is what the *next* tune is built around.
    CHECK (MixPlanner::countParameterChanges (c.getKept(), before) == 0);

    c.setFocusInput (4);                          // pinning another unpins the first
    CHECK (c.getFocusInput() == 4);
    int pins = 0;
    for (const auto& in : c.getSession().inputs) if (in.focus) ++pins;
    CHECK (pins == 1);

    c.setFocusInput (4);                          // pinning the pinned one clears it
    CHECK (c.getFocusInput() == -1);
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

TEST_CASE ("MixController: editing the session keeps the sound; preparing again rebuilds the graph")
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

    // The document has moved ahead of the graph - and that is ALL it has done. `prepared` is
    // what the audio callback checks before it does anything, so if this went false the output
    // would be silenced by the act of assigning one more input, and would stay silent until
    // something happened to rebuild the graph. That was a real fault, found in a real service.
    CHECK (c.isPrepared());
    CHECK (c.needsReconfigure());
    // The mix underneath is still audible and still the user's; only the preview, which
    // described the graph being replaced, is gone.
    CHECK (! c.hasPlan());
    CHECK (c.getStage() == MixController::Stage::Mixed);
    CHECK (c.getEngine().getNumStrips() == 5);      // still the graph it was prepared with

    c.prepare (kSr, kBlock);
    CHECK (c.getEngine().getNumStrips() == 6);
    CHECK (! c.needsReconfigure());                 // the graph is the document again
    CHECK (! c.hasPlan());
    // And the mix is still the one the engineer tuned. Preparing the graph for one more input
    // used to throw it away - the tune count went back to 0, the stage back to Ready - and
    // app/Main.cpp pushed a held copy back afterwards to undo that. The mix belongs to the
    // session now, so there is nothing to undo and nothing to hold.
    CHECK (c.getTuneCount() == 1);
    CHECK (c.getStage() == MixController::Stage::Mixed);
    CHECK (c.hasKeptMix());
}

TEST_CASE ("MixController: a band imported over an empty session can hear its effects")
{
    // What DINE holds before a folder is imported: no inputs, so no return in use. The import
    // used to carry that "off" across, and every effect ran with its return held at silence.
    MixController c;
    MixSession empty;
    empty.name = "Untitled";
    c.setSession (empty);
    c.prepare (kSr, kBlock);
    c.setSession (band());
    c.prepare (kSr, kBlock);
    const auto& graph = c.getGraph();
    REQUIRE (graph.fxUsed[size_t (FxSlot::VocalPlate)]);
    CHECK (c.getBase().fx[size_t (FxSlot::VocalPlate)].enabled);

    // Heard: with only the lead playing, a plate send makes a tail after the voice stops.
    for (int i = 0; i < c.getBase().numStrips; ++i) c.setStripMute (i, i != 3);
    c.setStripSend (3, FxSlot::VocalPlate, 0.0f);
    Feeder f (c);
    f.play (1.0, false);
    f.leadGain = 0.0f;
    f.play (0.05, false);                     // the voice's own chain empties
    float tail = 0.0f;
    for (int k = 0; k < 10; ++k) { f.play (double (kBlock) / kSr, false); tail = std::max (tail, f.outputPeak()); }
    CHECK (tail > 1.0e-4f);
}

TEST_CASE ("MixController: rebuilding the graph does not reach into the engineer's headphones")
{
    // The monitor belongs to the device and the person at the desk, not to the mix. Rebuilding
    // the graph - which is what changing the assignments does - used to put the level, the tap
    // point and the solo mode back to factory, so a routing change silently undid whatever the
    // engineer had set up to hear with, and solo appeared to stop working for good.
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);

    OutputFeeds feeds;
    feeds.count = 2;
    feeds.feeds[0] = { 0, 1, MixBus::Master, 0.0f, false, false, false };
    feeds.feeds[1] = { 2, 3, MixBus::Master, 0.0f, false, false, true };
    c.setOutputFeeds (feeds);
    c.setSoloPoint (SoloPoint::PFL);
    c.setMonitorGain (-7.5f);
    c.setMonitorDim (true);
    REQUIRE (c.hasMonitorOutput());

    MixSession s = band();
    s.inputs.push_back ({ "Pastor", ChannelRole::Speech, 6, -1 });
    c.setSession (s);
    c.prepare (kSr, kBlock);

    // The routing survives, as it always did...
    CHECK (c.hasMonitorOutput());
    CHECK (c.getOutputFeeds().feeds[1].monitor);
    // ...and so does what the engineer set up to listen with.
    CHECK (c.getMonitor().point == SoloPoint::PFL);
    CHECK_NEAR (c.getMonitor().gainDb, -7.5f, 0.01);
    CHECK (c.getMonitor().dim);
    CHECK (c.getMonitor().mode == SoloMode::Monitor);

    // Solo in place is a deliberate, dangerous choice, so it survives a rebuild too rather
    // than silently reverting to something the engineer did not pick.
    c.setSoloMode (SoloMode::InPlace);
    c.prepare (kSr, kBlock);
    CHECK (c.getMonitor().mode == SoloMode::InPlace);
}

TEST_CASE ("MixController: solo cannot claim to be set up when no monitor feed is routed")
{
    // The fault this guards is a second copy of a truth. Whether solo goes anywhere is a
    // property of the routing; turning the monitor feed into an ordinary bus feed from the
    // rows underneath has to be reflected, not contradicted by something remembered earlier.
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);

    OutputFeeds feeds;
    feeds.count = 2;
    feeds.feeds[0] = { 0, 1, MixBus::Master, 0.0f, false, false, false };
    feeds.feeds[1] = { 2, 3, MixBus::Master, 0.0f, false, false, true };
    c.setOutputFeeds (feeds);
    CHECK (c.hasMonitorOutput());

    // The engineer re-points that feed at a group. Solo now has nowhere to go, and the app
    // has to say so rather than keep claiming it is set up.
    feeds.feeds[1].monitor = false;
    feeds.feeds[1].source = MixBus::Vocals;
    c.setOutputFeeds (feeds);
    CHECK (! c.hasMonitorOutput());

    // Un-routing it entirely is the same answer.
    feeds.feeds[1].monitor = true;
    feeds.feeds[1].left = feeds.feeds[1].right = -1;
    c.setOutputFeeds (feeds);
    CHECK (! c.hasMonitorOutput());
}

TEST_CASE ("MixController: assigning inputs one at a time never silences what is already playing")
{
    // The shape of the fault this guards: a volunteer adds a channel on the INPUTS page while
    // the band is being monitored. Every edit calls setSession, and every edit used to take
    // the output away until the graph happened to be rebuilt.
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    REQUIRE (c.isPrepared());

    MixSession s = band();
    for (int i = 0; i < 4; ++i)
    {
        s.inputs.push_back ({ "Extra " + std::to_string (i), ChannelRole::BackingVocal, 6 + i, -1 });
        c.setSession (s);
        CHECK (c.isPrepared());          // the sound carries on through every one of them
    }
    // Removing inputs is the same promise.
    while (s.inputs.size() > 2) { s.inputs.pop_back(); c.setSession (s); CHECK (c.isPrepared()); }
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
    d.devices.consoleInput = "Dante Virtual Soundcard";
    d.devices.broadcastOutput = "Dante Virtual Soundcard";
    d.session.voicing = MasterVoicing::Car;
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
    CHECK (back.session.voicing == MasterVoicing::Car);      // who the mix is for comes back with it
    REQUIRE (back.session.inputs.size() == 5);
    CHECK (back.session.inputs[2].name == "Keys");
    CHECK (back.session.inputs[2].role == ChannelRole::Piano);
    CHECK (back.session.inputs[2].inputA == 2);
    CHECK (back.session.inputs[2].inputB == 3);
    CHECK (back.devices.consoleInput == "Dante Virtual Soundcard");
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

    // Not a DINE file: refused, nothing changed.
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
    d.devices.consoleInput = "In";
    d.devices.broadcastOutput = "Out";
    d.hasMix = false;
    // Write into the real sessions folder via a unique name, or fall back to a temp file for the round trip.
    const auto file = juce::File ("/Users/calebwork/Documents/GitHub/Calive/.tmp-session-test.dine.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (d, file));
    REQUIRE (file.existsAsFile());

    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));
    CHECK (back.session.name == "ListTest Sunday");
    CHECK (back.devices.broadcastOutput == "Out");
    file.deleteFile();

    (void) SessionStore::listSessions();
}


// ---------------------------------------------------------------------------
// REFERENCE MIX: "make it sound like this."
// ---------------------------------------------------------------------------
namespace
{
    // A minute of pink-ish stereo music written to disk, so the file half of the feature is
    // tested through a real decoder rather than around it.
    juce::File writeReferenceWav (const juce::File& file, float seconds, int channels, float tilt)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream == nullptr) return {};
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), kSr, unsigned (channels), 24, {}, 0));
        if (writer == nullptr) return {};
        const int n = int (kSr * double (seconds));
        juce::AudioBuffer<float> buffer (channels, n);
        juce::Random rng (7);
        float lp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = float (i) / float (kSr);
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            lp = 0.96f * lp + 0.04f * white;                       // a low-heavy bed
            const float body = 0.35f * lp + 0.10f * std::sin (2.0f * float (M_PI) * 110.0f * t);
            const float top = tilt * 0.25f * white;                 // ... with the top end dialled by `tilt`
            const float beat = std::fmod (t, 0.5f) < 0.06f ? 0.35f : 0.0f;
            for (int c = 0; c < channels; ++c)
                buffer.setSample (c, i, body + top + beat * std::sin (2.0f * float (M_PI) * 60.0f * t) + (c == 1 ? 0.08f * white : 0.0f));
        }
        writer->writeFromAudioSampleBuffer (buffer, 0, n);
        writer.reset();
        return file;
    }

    juce::File scratch (const juce::String& name)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    }
}

TEST_CASE ("ReferenceAudio: a finished recording is measured; a four-second clip is refused with a reason")
{
    const auto song = writeReferenceWav (scratch ("dine-reference-song.wav"), 12.0f, 2, 1.0f);
    REQUIRE (song.existsAsFile());
    const auto measured = ReferenceAudio::measure (song, StyleProfileId::ModernGospel);
    CHECK (measured.error.isEmpty());
    REQUIRE (measured.adequacy.usable);
    REQUIRE (measured.profile.valid);
    CHECK (measured.profile.name == "dine-reference-song");
    CHECK (measured.profile.channels == 2);
    CHECK_NEAR (measured.profile.seconds, 12.0f, 0.5f);
    CHECK (measured.profile.loudnessLufs > -45.0f);

    const auto clip = writeReferenceWav (scratch ("dine-reference-clip.wav"), 4.0f, 2, 1.0f);
    const auto tooShort = ReferenceAudio::measure (clip, StyleProfileId::ModernGospel);
    CHECK (! tooShort.adequacy.usable);
    CHECK (! tooShort.adequacy.reason.empty());
    CHECK (! tooShort.profile.valid);

    // A file that is not audio at all is an error, not a refusal: there is nothing to judge.
    const auto text = scratch ("dine-reference-not-audio.wav");
    text.replaceWithText ("this is not a song");
    const auto broken = ReferenceAudio::measure (text, StyleProfileId::ModernGospel);
    CHECK (broken.error.isNotEmpty());
    CHECK (! broken.profile.valid);

    song.deleteFile();
    clip.deleteFile();
    text.deleteFile();
}

TEST_CASE ("MixController: a reference aims the mix from the listen it already has, and only the master moves")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    CHECK (! c.hasReference());
    CHECK (! c.hasListened());

    c.startTuneMix ({ 3.0f, -45.0f, 2.0f });
    f.play (0.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    CHECK (c.hasListened());
    const MixParameters tuned = c.getKept();

    // What the mix proposes with no reference at all: the profile's own aim, from this listen.
    c.startReferenceMatch();
    REQUIRE (c.getStage() == MixController::Stage::Preview);
    REQUIRE (c.hasPlan());
    CHECK (! c.getPlan()->reference.used);
    const MixParameters profileAimed = c.getPlan()->proposed;
    c.revertPlan();

    // A reference measured from this mix's own master, but leaner at the bottom and brighter
    // on top: a record this band is not currently making.
    auto measurement = c.getLastListen().buses[size_t (MixBus::Master)];
    REQUIRE (measurement.valid);
    measurement.durationSeconds = 200.0f;
    measurement.loudnessLufs = -9.0f;
    measurement.silencePercent = 0.0f;
    measurement.bandEnergyDb[size_t (Band::Low)] -= 12.0f;
    measurement.bandEnergyDb[size_t (Band::Brilliance)] += 8.0f;
    measurement.bandEnergyDb[size_t (Band::Air)] += 8.0f;
    c.setReference (Reference::profileFrom (measurement, "Sunday Record", "/tmp/sunday.wav"));
    REQUIRE (c.hasReference());
    CHECK (c.getReference().name == "Sunday Record");

    // Setting one changes nothing that can be heard: it is a target, not a move.
    CHECK (MixPlanner::countParameterChanges (tuned, c.getKept()) == 0);
    CHECK (c.getStage() == MixController::Stage::Mixed);

    // Matching works from the listen DINE already has - the band is not asked to play again.
    c.startReferenceMatch();
    REQUIRE (c.getStage() == MixController::Stage::Preview);
    REQUIRE (c.hasPlan());
    REQUIRE (c.getPlan()->reference.used);
    CHECK (c.getPlan()->reference.name == "Sunday Record");
    CHECK (! c.getPlan()->reference.aims.empty());
    CHECK (! c.getPlan()->reference.limits.empty());

    // The master is aimed somewhere else than the profile alone would have aimed it...
    const MixParameters referenceAimed = c.getPlan()->proposed;
    CHECK (! diffParameters (profileAimed.master().channel, referenceAimed.master().channel).empty());

    // ... and nothing else in the mix moved because of it.
    for (int i = 0; i < referenceAimed.numStrips; ++i)
    {
        CHECK (diffParameters (profileAimed.strips[size_t (i)].channel, referenceAimed.strips[size_t (i)].channel).empty());
        CHECK_NEAR (profileAimed.strips[size_t (i)].faderDb, referenceAimed.strips[size_t (i)].faderDb, 0.001f);
    }
    for (int b = 0; b < int (MixBus::Master); ++b)
        CHECK (diffParameters (profileAimed.buses[size_t (b)].channel, referenceAimed.buses[size_t (b)].channel).empty());

    // BEFORE / AFTER is the same preview every other verb produces.
    c.setCompare (MixController::Compare::Before);
    CHECK (MixPlanner::countParameterChanges (c.getBase(), c.getPlan()->before) == 0);
    c.setCompare (MixController::Compare::After);
    c.keepPlan();
    CHECK (MixPlanner::countParameterChanges (c.getKept(), referenceAimed) == 0);

    // Removing it stops the mix being aimed at it: the next plan is the profile's own again.
    // What the reference already set is not rolled back here - that is what REVERT is for -
    // because the master keeps whatever is on it until a decision moves it.
    c.clearReference();
    CHECK (! c.hasReference());
    c.startReferenceMatch();
    REQUIRE (c.getStage() == MixController::Stage::Preview);
    CHECK (! c.getPlan()->reference.used);
    CHECK (c.getPlan()->notes.size() > 0);
}

TEST_CASE ("SessionStore: the reference a session is aimed at survives the round trip, and an unknown schema is ignored")
{
    AnalysisResult measured;
    measured.valid = true;
    measured.numChannels = 2;
    measured.durationSeconds = 214.0f;
    measured.loudnessLufs = -8.5f;
    measured.crestFactorDb = 9.25f;
    measured.stereoCorrelation = 0.42f;
    measured.tempoBpm = 96.0f;
    measured.tempoConfidence = 0.8f;
    for (int i = 0; i < int (Band::Count); ++i) measured.bandEnergyDb[size_t (i)] = -8.0f - float (i);
    for (int i = 0; i < kNumThirdOctaveBands; ++i) measured.thirdOctaveDb[size_t (i)] = -20.0f - 0.5f * float (i);

    SessionStore::Document d;
    d.session = band();
    d.reference = Reference::profileFrom (measured, "Take Me To The King", "/Users/x/Music/king.wav");
    REQUIRE (d.reference.valid);

    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), back));
    REQUIRE (back.reference.valid);
    CHECK (back.reference.name == "Take Me To The King");
    CHECK (back.reference.path == "/Users/x/Music/king.wav");
    CHECK_NEAR (back.reference.seconds, 214.0f, 0.01f);
    CHECK (back.reference.channels == 2);
    CHECK_NEAR (back.reference.loudnessLufs, -8.5f, 0.01f);
    CHECK_NEAR (back.reference.crestFactorDb, 9.25f, 0.01f);
    CHECK_NEAR (back.reference.stereoCorrelation, 0.42f, 0.001f);
    CHECK_NEAR (back.reference.tempoBpm, 96.0f, 0.01f);
    for (int i = 0; i < int (Band::Count); ++i)
        CHECK_NEAR (back.reference.bandEnergyDb[size_t (i)], measured.bandEnergyDb[size_t (i)], 0.01f);
    CHECK_NEAR (back.reference.thirdOctaveDb[3], measured.thirdOctaveDb[3], 0.01f);

    // A session that was never aimed at anything comes back with nothing to aim at.
    SessionStore::Document plain;
    plain.session = band();
    SessionStore::Document plainBack;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (plain))), plainBack));
    CHECK (! plainBack.reference.valid);

    // A reference written by a schema this build does not know is not guessed at: the mix
    // goes back to the profile's own target rather than being aimed at half a document.
    auto doc = SessionStore::toVar (d);
    if (auto* obj = doc.getDynamicObject())
        if (auto* ref = obj->getProperty ("reference").getDynamicObject())
            ref->setProperty ("schema", kReferenceSchemaVersion + 1);
    SessionStore::Document future;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (doc)), future));
    CHECK (! future.reference.valid);
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
    // cannot ask for a kind of change DINE has no way to express.
    CHECK (body.contains ("\"strict\": true"));
    CHECK (body.contains ("dine_mix_intent"));
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

// ---------------------------------------------------------------------------
// Track history: what changed on one channel, and any earlier setting put back
// ---------------------------------------------------------------------------
TEST_CASE ("Track history: every tune and hand edit on a channel is remembered, any of them can be put back, and the records survive a save and a rearrangement")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };
    for (int i = 0; i < 5; ++i) CHECK (c.getStripHistory (i).empty());
    CHECK (c.getStripHistory (-1).empty());

    // A TUNE MIX kept: every channel the plan moved has one record, naming what did it and
    // holding the strip as it was and as it became. A channel it left alone has none.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    const MixPlan plan = *c.getPlan();
    c.keepPlan();
    int strip = -1;
    for (int i = 0; i < 5; ++i)
    {
        const bool moved = stripTuneDiffers (plan.before.strips[size_t (i)], plan.proposed.strips[size_t (i)]);
        REQUIRE (c.getStripHistory (i).size() == (moved ? 1u : 0u));
        if (! moved) continue;
        const auto& r = c.getStripHistory (i)[0];
        CHECK (r.what == "TUNE MIX");
        CHECK (r.strip == i);
        CHECK (r.tune == c.getTuneCount());
        CHECK (r.whenMs > 0);
        CHECK (! stripTuneDiffers (r.before, plan.before.strips[size_t (i)]));
        CHECK (! stripTuneDiffers (r.after, c.getKept().strips[size_t (i)]));
        if (strip < 0) strip = i;
    }
    REQUIRE (strip >= 0);
    const std::string name = band().inputs[size_t (strip)].name;
    const StripParameters tuned = c.getKept().strips[size_t (strip)];

    // A hand edit of the chain is a record of its own. A fader move is not: it is not tuning,
    // and a drag would write thirty of them a second. Keys are never part of a setting.
    c.setStripMute (strip, true);
    ChannelParameters edited = tuned.channel;
    edited.hpfEnabled = true;
    edited.hpfHz = tuned.channel.hpfHz + 30.0f;
    c.setStripChannel (strip, edited);
    REQUIRE (c.getStripHistory (strip).size() == 2);
    CHECK (c.getStripHistory (strip)[1].what == "Inspector edit");
    CHECK (! c.getStripHistory (strip)[1].after.mute);
    c.setStripMute (strip, false);
    c.setStripFader (strip, tuned.faderDb - 3.0f);
    CHECK (c.getStripHistory (strip).size() == 2);
    ChannelParameters same = c.getKept().strips[size_t (strip)].channel;
    c.setStripChannel (strip, same);                     // nothing changed: nothing to remember
    CHECK (c.getStripHistory (strip).size() == 2);

    // PUT BACK: the tune's own setting returns - chain, level, gain, pan and sends - on this
    // channel alone. It is a record of its own, and an undo step with the channel's name on it.
    const int other = (strip + 1) % 5;
    const StripParameters otherWas = c.getKept().strips[size_t (other)];
    const int undoBefore = int (c.canUndoMix());
    REQUIRE (c.restoreStripTune (strip, 0));
    CHECK (! stripTuneDiffers (c.getKept().strips[size_t (strip)], tuned));
    CHECK_NEAR (c.getKept().strips[size_t (strip)].faderDb, tuned.faderDb, 0.01);
    CHECK (! stripTuneDiffers (c.getKept().strips[size_t (other)], otherWas));
    REQUIRE (c.getStripHistory (strip).size() == 3);
    CHECK (c.getStripHistory (strip)[2].what == "Put back: TUNE MIX");
    CHECK (c.undoMixLabel() == "putting " + name + " back");
    REQUIRE (! messages.empty());
    CHECK (messages.back().find (name + " is back to what TUNE MIX set") != std::string::npos);
    (void) undoBefore;
    c.undoMix();
    CHECK_NEAR (c.getKept().strips[size_t (strip)].channel.hpfHz, edited.hpfHz, 0.01);
    CHECK_NEAR (c.getKept().strips[size_t (strip)].faderDb, tuned.faderDb - 3.0f, 0.01);
    c.redoMix();
    CHECK (! stripTuneDiffers (c.getKept().strips[size_t (strip)], tuned));

    // Out of range is refused quietly.
    CHECK (! c.restoreStripTune (strip, 99));
    CHECK (! c.restoreStripTune (-1, 0));
    CHECK (! c.restoreStripTune (strip, -1));

    // Under LIVE SAFE the chain is let through and the level moves by one step, as any
    // Inspector edit would - and the sentence says so.
    c.setStripFader (strip, tuned.faderDb - 20.0f);
    c.setLiveSafe (true);
    messages.clear();
    REQUIRE (c.restoreStripTune (strip, 0));
    CHECK_NEAR (c.getKept().strips[size_t (strip)].faderDb, tuned.faderDb - 20.0f + c.getLiveSafePolicy().maxFaderStepDb, 0.01);
    CHECK (diffParameters (c.getKept().strips[size_t (strip)].channel, tuned.channel).empty());
    REQUIRE (! messages.empty());
    CHECK (messages.back().find ("LIVE SAFE") != std::string::npos);
    c.setLiveSafe (false);

    // The session carries it: what is saved is what comes back.
    const auto records = c.getAllStripHistory();
    REQUIRE (! records.empty());
    SessionStore::Document d;
    d.session = c.getSession();
    d.hasMix = true;
    d.mix = c.getKept();
    d.history = records;
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), back));
    REQUIRE (back.history.size() == records.size());
    for (size_t k = 0; k < records.size(); ++k)
    {
        CHECK (back.history[k].strip == records[k].strip);
        CHECK (back.history[k].what == records[k].what);
        CHECK (back.history[k].tune == records[k].tune);
        CHECK (back.history[k].whenMs == records[k].whenMs);
        CHECK (! stripTuneDiffers (back.history[k].before, records[k].before));
        CHECK (! stripTuneDiffers (back.history[k].after, records[k].after));
    }
    // A document from before the history existed simply has none.
    SessionStore::Document older;
    d.history.clear();
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), older));
    CHECK (older.history.empty());

    // A rearrangement: each record follows its input, and one whose input became a different
    // source is dropped with the chain it described.
    const size_t onStrip = c.getStripHistory (strip).size();
    MixSession moved = band();
    auto in = moved.inputs[size_t (strip)];
    moved.inputs.erase (moved.inputs.begin() + strip);
    moved.inputs.insert (moved.inputs.begin(), in);
    MixController c2;
    c2.setSession (moved);
    c2.prepare (kSr, kBlock);
    c2.carryStripHistory (records, band());
    CHECK (c2.getStripHistory (0).size() == onStrip);
    CHECK (c2.getAllStripHistory().size() == records.size());
    for (const auto& r : c2.getStripHistory (0)) CHECK (r.strip == 0);
    moved.inputs[0].role = ChannelRole::Piano;
    c2.setSession (moved);
    c2.prepare (kSr, kBlock);
    // A rebuild carries the records with their inputs rather than clearing them for the host to
    // push back: every channel that is still the same source keeps its history, and the one
    // that became a piano lost it along with the chain it described.
    CHECK (c2.getStripHistory (0).empty());
    CHECK (c2.getAllStripHistory().size() == records.size() - onStrip);
    c2.carryStripHistory (records, band());
    CHECK (c2.getStripHistory (0).empty());              // ... and the records of a source that changed stay gone
    CHECK (c2.getAllStripHistory().size() == records.size() - onStrip);
}

// ---------------------------------------------------------------------------
// Sample replacement: the built-in bank, and the sounds reaching a drum strip
// ---------------------------------------------------------------------------
TEST_CASE ("SampleLibrary: the built-in bank loads from the project, decodes to mono at its own rate, and reaches the kick strip through the controller")
{
    SampleLibrary library;
    library.load();
    REQUIRE (library.table() != nullptr);
    // What ships: three kicks, four snares, four toms, named as the files are, decoded at 44.1 kHz.
    CHECK (library.numSounds (RoleFamily::Kick) == 3);
    CHECK (library.numSounds (RoleFamily::Snare) == 4);
    CHECK (library.numSounds (RoleFamily::Tom) == 4);
    const auto kicks = library.soundNames (RoleFamily::Kick);
    CHECK (kicks.contains ("Punch kick"));
    CHECK (kicks.contains ("Perfect kick"));
    CHECK (kicks.contains ("Soft kick"));
    CHECK (library.soundNames (RoleFamily::Tom).contains ("16 inch floor tom"));
    for (auto family : { RoleFamily::Kick, RoleFamily::Snare, RoleFamily::Tom })
        for (int i = 0; i < library.numSounds (family); ++i)
        {
            const auto* b = library.table()->bank (family, i);
            REQUIRE (b != nullptr);
            CHECK_NEAR (b->sampleRate, 44100.0, 1.0);
            REQUIRE (b->layers.size() == 1u);
            REQUIRE (b->layers[0].hits.size() == 1u);
            const auto& hit = b->layers[0].hits[0];
            CHECK (hit.size() > 4410u);                     // more than a tenth of a second of drum
            float peak = 0.0f;
            for (float v : hit) peak = std::max (peak, std::fabs (v));
            CHECK_NEAR (peak, 1.0f, 1.0e-4f);               // peak-normalised
            float early = 0.0f;
            for (size_t k = 0; k < 96 && k < hit.size(); ++k) early = std::max (early, std::fabs (hit[k]));
            CHECK (early > 0.005f);                         // trimmed to its onset
        }
    // A family with nothing on disk is never empty: the placeholders stand in.
    CHECK (library.numSounds (RoleFamily::LeadVocal) == 0);
    CHECK (library.whereLoadedFrom().size() >= 1);

    // Through the controller: the kick strip plays sound 0 until told otherwise, the bass strip
    // has no stage at all, and changing the kick's sound changes what its stage holds.
    MixController c;
    c.setSampleBanks (library.table());
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.1);
    CHECK (c.getEngine().getStrip (0).getOptions().sampleReplacement);          // Kick
    CHECK (! c.getEngine().getStrip (1).getOptions().sampleReplacement);        // Bass
    CHECK (c.getEngine().getStrip (0).getSampler().getBank() == library.table()->bank (RoleFamily::Kick, 0));
    auto kick = c.getKept().strips[0].channel;
    kick.replaceSound = 2;
    kick.replaceEnabled = true;
    c.setStripChannel (0, kick);
    f.play (0.1);
    CHECK (c.getEngine().getStrip (0).getSampler().getBank() == library.table()->bank (RoleFamily::Kick, 2));
    CHECK (c.getEngine().getStrip (0).getSampler().getParams().enabled);
    CHECK (c.getEngine().getLatencySamples() == c.getEngine().getBus (MixBus::Master).getLatencySamples());   // the stage adds none
    // The stage is saved with the mix like any other setting.
    SessionStore::Document d;
    d.session = c.getSession();
    d.hasMix = true;
    d.mix = c.getKept();
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), back));
    CHECK (back.mix.strips[0].channel.replaceEnabled);
    CHECK (back.mix.strips[0].channel.replaceSound == 2);
}

TEST_CASE ("HEAR IT: an audition plays the strip's sound into the engineer's listen and never into the broadcast")
{
    SampleLibrary library;
    library.load();
    MixController c;
    c.setSampleBanks (library.table());
    c.setSession (band());
    c.prepare (kSr, kBlock);
    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };

    // Silent inputs, four output channels: the broadcast on 1-2, the listen on 3-4.
    std::vector<std::vector<float>> in (6, std::vector<float> (size_t (kBlock), 0.0f));
    std::vector<const float*> ip;
    for (auto& v : in) ip.push_back (v.data());
    std::vector<std::vector<float>> out (4, std::vector<float> (size_t (kBlock), 0.0f));
    float* op[4] = { out[0].data(), out[1].data(), out[2].data(), out[3].data() };
    auto run = [&] (int blocks)
    {
        float broadcast = 0.0f, listen = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            c.process (ip.data(), 6, op, 4, kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                broadcast = std::max ({ broadcast, std::fabs (out[0][size_t (i)]), std::fabs (out[1][size_t (i)]) });
                listen = std::max ({ listen, std::fabs (out[2][size_t (i)]), std::fabs (out[3][size_t (i)]) });
            }
        }
        return std::make_pair (broadcast, listen);
    };

    // No solo output yet: refused, and the sentence says where to set it up.
    CHECK (! c.auditionSample (0));
    REQUIRE (! messages.empty());
    CHECK (messages.back().find ("nowhere to go") != std::string::npos);
    auto quiet = run (20);
    CHECK (quiet.first == 0.0f);
    CHECK (quiet.second == 0.0f);

    // The listen on channels 3-4: the audition lands there, at the stage's level, and the broadcast stays silent.
    OutputFeeds feeds = OutputFeeds::mainOnly();
    feeds.count = 2;
    feeds.feeds[1].monitor = true;
    feeds.feeds[1].left = 2;
    feeds.feeds[1].right = 3;
    feeds.feeds[1].mono = false;
    feeds.feeds[1].mute = false;
    c.setOutputFeeds (feeds);
    run (4);
    REQUIRE (c.auditionSample (0));
    auto heard = run (int (kSr / kBlock));           // a second: the whole hit
    CHECK (heard.first == 0.0f);
    CHECK (heard.second > 0.1f);
    CHECK (heard.second <= 1.0f);
    // A vocal strip has no sound to hear.
    messages.clear();
    CHECK (! c.auditionSample (3));
    REQUIRE (! messages.empty());
    CHECK (messages.back().find ("no sound") != std::string::npos);

    // The same session opened on a device with only two outputs (a laptop's speakers): the
    // listen on 3-4 goes nowhere, so HEAR IT and solo say so instead of playing into nothing.
    c.process (ip.data(), 6, op, 2, kBlock);
    CHECK (! c.hasMonitorOutput());
    messages.clear();
    CHECK (! c.auditionSample (0));
    REQUIRE (! messages.empty());
    CHECK (messages.back().find ("outputs 3-4") != std::string::npos);
    CHECK (messages.back().find ("only 2") != std::string::npos);
    CHECK (messages.back().find ("everyone hears solo") != std::string::npos);   // ... and what to do about it
    // With solo in place (everyone hears solo: a rehearsal) the sound plays on the main output
    // instead, because that is where solo is heard.
    c.setSoloMode (SoloMode::InPlace);
    for (auto& o : out) std::fill (o.begin(), o.end(), 0.0f);
    REQUIRE (c.auditionSample (0));
    float main = 0.0f;
    for (int b = 0; b < int (kSr / kBlock); ++b)
    {
        c.process (ip.data(), 6, op, 2, kBlock);
        for (int i = 0; i < kBlock; ++i) main = std::max ({ main, std::fabs (out[0][size_t (i)]), std::fabs (out[1][size_t (i)]) });
    }
    CHECK (main > 0.05f);
    CHECK (main <= 1.0f);
    // ... never under LIVE SAFE, which does not allow solo in place.
    c.setSoloMode (SoloMode::Monitor);
    // ... and back on the four-output device it plays in the private listen again.
    c.process (ip.data(), 6, op, 4, kBlock);
    CHECK (c.hasMonitorOutput());
    CHECK (c.auditionSample (0));
}

// ---------------------------------------------------------------------------
// Opening a session whose console is not plugged in
// ---------------------------------------------------------------------------
TEST_CASE ("DevicePlan: a session opens on its own devices when they are here, on what is open otherwise, on an output alone for playback, and never refuses")
{
    const juce::StringArray ins { "Dante Virtual Soundcard", "MacBook Pro Microphone" };
    const juce::StringArray outs { "Dante Virtual Soundcard", "MacBook Pro Speakers" };

    // Its own devices are here: open them, no note.
    auto p = planDevicesForSession ("Dante Virtual Soundcard", "Dante Virtual Soundcard", true, ins, outs, "", "", false);
    CHECK (p.action == DevicePlan::Action::OpenBoth);
    CHECK (p.input == "Dante Virtual Soundcard");
    CHECK (p.output == "Dante Virtual Soundcard");
    CHECK (p.note.isEmpty());

    // The input is here but the output it used is not: the mix goes out of what there is, and it says so.
    p = planDevicesForSession ("Dante Virtual Soundcard", "Behringer X32", true, ins, outs, "", "", false);
    CHECK (p.action == DevicePlan::Action::OpenBoth);
    CHECK (p.output == "Dante Virtual Soundcard");
    CHECK (p.note.contains ("Behringer X32"));

    // The console is gone and something is open: keep it, and name the missing console.
    p = planDevicesForSession ("Behringer X32", "Behringer X32", true, ins, outs, "MacBook Pro Microphone", "MacBook Pro Speakers", true);
    CHECK (p.action == DevicePlan::Action::KeepOpen);
    CHECK (p.note.contains ("Behringer X32"));
    CHECK (p.note.contains ("MacBook Pro Microphone"));
    CHECK (p.note.contains ("Audio device"));

    // The console is gone and nothing is open: an output alone, so the recording plays.
    p = planDevicesForSession ("Behringer X32", "Behringer X32", true, ins, outs, "", "", false);
    CHECK (p.action == DevicePlan::Action::OpenOutputOnly);
    CHECK (p.output == "Dante Virtual Soundcard");
    CHECK (p.note.contains ("Behringer X32"));
    CHECK (p.note.contains ("Playback is on Dante Virtual Soundcard"));

    // A session built from imported stems (no console) with audio: its output if here, else the first.
    p = planDevicesForSession ("", "MacBook Pro Speakers", true, ins, outs, "", "", false);
    CHECK (p.action == DevicePlan::Action::OpenOutputOnly);
    CHECK (p.output == "MacBook Pro Speakers");
    CHECK (! p.note.contains ("recorded on"));

    // No devices at all: nothing opens, the session still opens, the note points at the page.
    p = planDevicesForSession ("Behringer X32", "Behringer X32", true, {}, {}, "", "", false);
    CHECK (p.action == DevicePlan::Action::None);
    CHECK (p.note.contains ("Behringer X32"));
    CHECK (p.note.contains ("Audio device"));

    // An empty session (no console, no audio) with nothing open: nothing to open, and it says so plainly.
    p = planDevicesForSession ("", "", false, ins, outs, "", "", false);
    CHECK (p.action == DevicePlan::Action::None);
    CHECK (p.note.contains ("No audio device is open"));

    // A missing output is never replaced by whatever happens to be first in the list - that can
    // be the PA's interface. The Mac's own speakers are where an unchosen mix goes.
    const juce::StringArray stage { "Behringer X32", "PA Processor", "MacBook Pro Speakers" };
    p = planDevicesForSession ("Dante Virtual Soundcard", "Broadcast Encoder", true, ins, stage, "", "", false, "MacBook Pro Speakers");
    CHECK (p.output == "MacBook Pro Speakers");
    p = planDevicesForSession ("Behringer X32", "Broadcast Encoder", true, {}, stage, "", "", false, "MacBook Pro Speakers");
    CHECK (p.action == DevicePlan::Action::OpenOutputOnly);
    CHECK (p.output == "MacBook Pro Speakers");
}

TEST_CASE ("Devices: a device that comes back is checked, not trusted")
{
    // A Dante card back in 16-channel mode on a 24-input session.
    const auto s = deviceBackSentence ("Dante Virtual Soundcard", 16, 16, 24, 2);
    CHECK (! s.contains ("on the channel it was on"));
    CHECK (s.contains ("inputs 17-24 are silent"));
    CHECK (deviceBackSentence ("X32", 32, 32, 24, 2).contains ("on the channel it was on"));
    CHECK (deviceBackSentence ("X32", 32, 1, 24, 2).contains ("outputs above 1"));
    // Another unit of the same model is not the same device; an unknown UID is not a mismatch.
    CHECK (! sameUnit ("AppleUSBAudioEngine:X32:1", "AppleUSBAudioEngine:X32:2"));
    CHECK (sameUnit ("AppleUSBAudioEngine:X32:1", "AppleUSBAudioEngine:X32:1"));
    CHECK (sameUnit ({}, "AppleUSBAudioEngine:X32:2"));
    // ... and the session remembers which unit it was.
    SessionState st;
    st.devices = { "X32", "X32", {}, "uid-in", "uid-out" };
    SessionState back;
    REQUIRE (SessionStore::fromVar (SessionStore::toVar (st), back));
    CHECK (back.devices.consoleInputUid == "uid-in");
    CHECK (back.devices.broadcastOutputUid == "uid-out");
}

// ---------------------------------------------------------------------------
// The emergency keys, and scenes
// ---------------------------------------------------------------------------
TEST_CASE ("DIM and MUTE: the broadcast drops or goes silent on every feed but the engineer's listen, and nothing is kept, saved or undone")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    OutputFeeds feeds = OutputFeeds::mainOnly();
    feeds.count = 2;
    feeds.feeds[1].monitor = true; feeds.feeds[1].left = 2; feeds.feeds[1].right = 3; feeds.feeds[1].mono = false; feeds.feeds[1].mute = false;
    c.setOutputFeeds (feeds);
    c.setStripSolo (0, true);                         // the listen carries the kick

    std::vector<std::vector<float>> in (6, std::vector<float> (size_t (kBlock), 0.0f));
    std::vector<const float*> ip;
    for (auto& v : in) ip.push_back (v.data());
    std::vector<std::vector<float>> out (4, std::vector<float> (size_t (kBlock), 0.0f));
    float* op[4] = { out[0].data(), out[1].data(), out[2].data(), out[3].data() };
    auto run = [&] (int blocks)
    {
        float broadcast = 0.0f, listen = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i) in[0][size_t (i)] = 0.5f * std::sin (2.0f * float (M_PI) * 100.0f * float (b * kBlock + i) / float (kSr));
            c.process (ip.data(), 6, op, 4, kBlock);
            if (b < blocks * 3 / 4) continue;         // let the smoothers land: the last quarter is measured
            for (int i = 0; i < kBlock; ++i)
            {
                broadcast = std::max ({ broadcast, std::fabs (out[0][size_t (i)]), std::fabs (out[1][size_t (i)]) });
                listen = std::max ({ listen, std::fabs (out[2][size_t (i)]), std::fabs (out[3][size_t (i)]) });
            }
        }
        return std::make_pair (broadcast, listen);
    };
    const auto plain = run (240);
    REQUIRE (plain.first > 0.01f);
    REQUIRE (plain.second > 0.01f);

    c.setBroadcastDim (true);
    CHECK (c.isBroadcastDimmed());
    const auto dimmed = run (240);
    CHECK_NEAR (20.0f * std::log10 (dimmed.first / plain.first), -20.0f, 0.5f);
    CHECK_NEAR (dimmed.second, plain.second, 0.01f);   // the listen is untouched
    CHECK (! c.canUndoMix());                          // not a mix change
    CHECK (MixPlanner::countParameterChanges (c.getKept(), c.getKept()) == 0);

    c.setBroadcastMute (true);
    const auto muted = run (240);
    CHECK (muted.first < 1.0e-4f);
    CHECK_NEAR (muted.second, plain.second, 0.01f);
    // BYPASS is a comparison of the mix, and MUTE is not part of the mix: pressing it after
    // MUTE must not put what was muted back on the air.
    c.setBypass (true);
    CHECK (run (240).first < 1.0e-4f);
    c.setBypass (false);
    // LIVE SAFE never locks them.
    c.setLiveSafe (true);
    c.setBroadcastMute (false);
    c.setBroadcastDim (false);
    CHECK (! c.isBroadcastMuted());
    CHECK (! c.isBroadcastDimmed());
    const auto back = run (240);
    CHECK_NEAR (back.first, plain.first, 0.01f);
    // Never saved: a session must not open muted.
    c.setBroadcastMute (true);
    SessionStore::Document d;
    d.session = c.getSession(); d.hasMix = true; d.mix = c.getKept();
    SessionStore::Document backDoc;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), backDoc));
    CHECK (! backDoc.mix.broadcastMute);
}

TEST_CASE ("Scenes: KEEP holds the whole mix under a name, RECALL brings it back as one undoable change, and a scene kept on other inputs is refused")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    std::vector<std::string> messages;
    c.onMessage = [&] (const std::string& m) { messages.push_back (m); };
    REQUIRE (c.numScenes() == 4);
    CHECK (c.getScene (0).name == "Band");
    CHECK (c.getScene (1).name == "Speech");
    CHECK (! c.getScene (0).kept);

    // Nothing kept yet: refused, with the sentence.
    CHECK (! c.recallScene (0));
    CHECK (messages.back().find ("nothing kept") != std::string::npos);

    // The band mix: kick up, keys down, a macro; kept as Band.
    c.setStripFader (0, 3.0f);
    c.setStripFader (2, -6.0f);
    c.setMacro (MixMacro::Drums, 70.0f);
    c.keepScene (0);
    CHECK (c.getScene (0).kept);
    // The speech mix: everything but the lead down; kept as Speech.
    c.setStripFader (0, -20.0f);
    c.setStripFader (2, -20.0f);
    c.setStripFader (3, 2.0f);
    c.setMacro (MixMacro::Drums, 30.0f);
    c.keepScene (1);

    // Recall Band: faders and macros as kept, one undo step, the strip history says so.
    c.setStripSolo (1, true);                         // solo is the engineer's and survives a recall
    REQUIRE (c.recallScene (0));
    CHECK_NEAR (c.getKept().strips[0].faderDb, 3.0f, 0.01f);
    CHECK_NEAR (c.getKept().strips[2].faderDb, -6.0f, 0.01f);
    CHECK_NEAR (c.getMacros().get (MixMacro::Drums), 70.0f, 0.01f);
    CHECK (c.getKept().strips[1].solo);
    CHECK (c.undoMixLabel() == "recalling Band");
    REQUIRE (! c.getStripHistory (0).empty());
    CHECK (c.getStripHistory (0).back().what == "Scene: Band");
    c.undoMix();
    CHECK_NEAR (c.getKept().strips[0].faderDb, -20.0f, 0.01f);
    // Under LIVE SAFE a recall goes through.
    c.setLiveSafe (true);
    REQUIRE (c.recallScene (1));
    CHECK_NEAR (c.getKept().strips[3].faderDb, 2.0f, 0.01f);
    c.setLiveSafe (false);

    // Saved with the session, by name, and refused on a different set of inputs.
    c.renameScene (2, "Choir");
    SessionStore::Document d;
    d.session = c.getSession(); d.hasMix = true; d.mix = c.getKept(); d.scenes = c.getScenes();
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), back));
    REQUIRE (back.scenes.size() == 4u);
    CHECK (back.scenes[0].kept);
    CHECK (back.scenes[2].name == "Choir");
    CHECK (! back.scenes[2].kept);
    CHECK_NEAR (back.scenes[0].mix.strips[0].faderDb, 3.0f, 0.01f);
    CHECK (back.scenes[0].inputs.size() == 5u);

    MixSession other = band();
    other.inputs[1].name = "Bass DI";
    MixController c2;
    c2.setSession (other);
    c2.prepare (kSr, kBlock);
    c2.onMessage = [&] (const std::string& m) { messages.push_back (m); };
    c2.restoreScenes (back.scenes);
    CHECK (c2.getScene (0).kept);
    CHECK (! c2.recallScene (0));
    CHECK (messages.back().find ("different set of inputs") != std::string::npos);
}

TEST_CASE ("MixController: RESET TO RAW takes back everything DINE decided and nothing else")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    const MixParameters raw = c.getKept();      // the session's own baseline, before anything was heard

    // A real mix: a tune kept, a hand edit on top, a macro leaned, a scene, a reference and a
    // per-channel record - every kind of thing a reset has to decide about.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    CHECK (c.getTuneCount() == 1);
    CHECK (c.hasKeptMix());
    c.setStripFader (1, -7.5f);
    c.setStripMute (2, true);
    c.setStripSolo (3, true);
    c.setMacro (MixMacro::Drums, 72.0f);
    c.keepScene (1);
    ReferenceProfile ref;
    ref.valid = true;
    ref.name = "Take Me To The King";
    c.setReference (ref);
    const auto recordsBefore = c.getStripHistory (1).size();
    const auto scenesBefore = c.getScene (1).kept;
    const auto checkpointsBefore = c.getCheckpoints().size();
    CHECK (MixPlanner::countParameterChanges (c.getKept(), raw) > 0);

    REQUIRE (c.resetMixToRaw());

    // Everything DINE decided is gone: every chain, every level, every send, the macros, and
    // the count of tunes that produced them.
    for (int i = 0; i < c.getKept().numStrips; ++i)
    {
        CHECK (diffParameters (c.getKept().strips[size_t (i)].channel, raw.strips[size_t (i)].channel).empty());
        CHECK_NEAR (c.getKept().strips[size_t (i)].faderDb, raw.strips[size_t (i)].faderDb, 0.001f);
        CHECK_NEAR (c.getKept().strips[size_t (i)].inputGainDb, raw.strips[size_t (i)].inputGainDb, 0.001f);
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        CHECK (diffParameters (c.getKept().buses[size_t (b)].channel, raw.buses[size_t (b)].channel).empty());
        CHECK_NEAR (c.getKept().buses[size_t (b)].faderDb, raw.buses[size_t (b)].faderDb, 0.001f);
    }
    CHECK (c.getTuneCount() == 0);
    CHECK (! c.hasKeptMix());
    CHECK (! c.hasPlan());
    CHECK_NEAR (c.getMacros().get (MixMacro::Drums), 50.0f, 0.001f);

    // The engineer's listening state is not a mix decision and survives, as it does through
    // BYPASS. So does everything that is not the mix at all.
    CHECK (c.getKept().strips[2].mute);
    CHECK (c.getKept().strips[3].solo);
    CHECK (c.getScene (1).kept == scenesBefore);
    CHECK (c.hasReference());
    CHECK (c.getReference().name == std::string ("Take Me To The King"));
    CHECK (c.getStripHistory (1).size() == recordsBefore);
    CHECK (c.getSession().inputs.size() == band().inputs.size());

    // IT IS NEVER A ONE-WAY DOOR. A checkpoint was taken first, and UNDO takes it back.
    CHECK (c.getCheckpoints().size() > checkpointsBefore);
    bool named = false;
    for (const auto& cp : c.getCheckpoints()) if (cp.what.find ("Before reset") != std::string::npos) named = true;
    CHECK (named);
    REQUIRE (c.canUndoMix());
    c.undoMix();
    CHECK_NEAR (c.getKept().strips[1].faderDb, -7.5f, 0.01f);
    CHECK (MixPlanner::countParameterChanges (c.getKept(), raw) > 0);

    // ...and BYPASS is a different thing entirely: it is a way of listening, and a reset does
    // not touch it.
    c.setBypass (true);
    CHECK (c.isBypassed());
    REQUIRE (c.resetMixToRaw());
    CHECK (c.isBypassed());
    c.setBypass (false);

    // LIVE SAFE refuses it with a sentence: putting the whole mix back to where it started is
    // exactly what must not happen in the middle of a service.
    c.setLiveSafe (true);
    c.setStripFader (1, -3.0f);
    std::string said;
    c.onMessage = [&said] (const std::string& m) { said = m; };
    CHECK (! c.resetMixToRaw());
    CHECK (said.find ("LIVE SAFE") != std::string::npos);
    CHECK_NEAR (c.getKept().strips[1].faderDb, -3.0f, 0.01f);
    c.setLiveSafe (false);
}

TEST_CASE ("MixController: the effects come off one microphone and back on, and nothing else about it moves")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);

    // band() is Kick, Bass, Keys, Lead, Vox. The lead is strip 3, the kick strip 0.
    const int lead = 3, kick = 0;
    CHECK (c.stripCanHaveEffects (lead));
    CHECK (! c.stripCanHaveEffects (kick));       // a kick drum is not asked about the plate
    CHECK (! c.stripCanHaveEffects (99));

    // A lead vocal arrives wet, because that is what the profile gives one.
    REQUIRE (c.stripEffectsOn (lead));
    const auto wet = c.getKept().strips[size_t (lead)].sendDb;
    CHECK (wet[size_t (FxSlot::VocalPlate)] > kSilenceDb);

    // OFF: nothing reaches a return, and THE LEVELS ARE LEFT WHERE THEY WERE - that is what
    // makes the press back exact rather than approximate.
    const auto wasChain = c.getKept().strips[size_t (lead)].channel;
    const float wasFader = c.getKept().strips[size_t (lead)].faderDb;
    c.setStripEffects (lead, false);
    CHECK (! c.stripEffectsOn (lead));
    CHECK (c.getKept().strips[size_t (lead)].effectsOff);
    CHECK (c.getKept().strips[size_t (lead)].sendDb == wet);
    CHECK (diffParameters (wasChain, c.getKept().strips[size_t (lead)].channel).empty());
    CHECK_NEAR (wasFader, c.getKept().strips[size_t (lead)].faderDb, 0.001f);
    // ...and the routing is untouched: this is not setInputRole.
    CHECK (c.getGraph().strips[size_t (lead)].bus == MixBus::Lead);
    CHECK (c.getSession().inputs[size_t (lead)].role == ChannelRole::LeadVocal);

    // ON again: exactly the mix it had.
    c.setStripEffects (lead, true);
    CHECK (c.stripEffectsOn (lead));
    CHECK (! c.getKept().strips[size_t (lead)].effectsOff);
    CHECK (c.getKept().strips[size_t (lead)].sendDb == wet);

    // Asking for what it already is does nothing, and a kick drum cannot be asked at all.
    c.setStripEffects (lead, true);
    CHECK (c.getKept().strips[size_t (lead)].sendDb == wet);
    c.setStripEffects (kick, true);
    for (float db : c.getKept().strips[size_t (kick)].sendDb) CHECK (db <= kSilenceDb);

    // IT SURVIVES A TUNE. TUNE MIX plans the send levels; whether this microphone is in the
    // plate right now is the engineer's, and the planner never takes it back.
    c.setStripEffects (lead, false);
    {
        Feeder f (c);
        c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
        f.play (2.6);
        REQUIRE (f.waitFor (MixController::Stage::Preview));
        c.keepPlan();
    }
    CHECK (c.getKept().strips[size_t (lead)].effectsOff);
    CHECK (! c.stripEffectsOn (lead));
    c.setStripEffects (lead, true);
    CHECK (c.stripEffectsOn (lead));

    // LIVE SAFE ALLOWS IT. A role change is refused because it rebuilds the graph and stops the
    // audio; this changes one gate, so it is exactly the move a service needs.
    c.setLiveSafe (true);
    c.setStripEffects (lead, false);
    CHECK (! c.stripEffectsOn (lead));
    c.setStripEffects (lead, true);
    CHECK (c.stripEffectsOn (lead));
    CHECK (c.getKept().strips[size_t (lead)].sendDb == wet);
    c.setLiveSafe (false);
}

TEST_CASE ("MixController: a speaking microphone starts dry and one press puts it in the plate")
{
    // The pastor's handheld, set up for preaching: no send, because that is what speech gets.
    MixSession s;
    s.name = "Sermon";
    s.inputs = { { "Pastor", ChannelRole::Speech, 0, -1 }, { "Lead", ChannelRole::LeadVocal, 1, -1 } };
    MixController c;
    c.setSession (s);
    c.prepare (kSr, kBlock);

    const int pastor = 0;
    REQUIRE (c.stripCanHaveEffects (pastor));
    CHECK (! c.stripEffectsOn (pastor));
    for (float db : c.getKept().strips[size_t (pastor)].sendDb) CHECK (db <= kSilenceDb);

    // HE HAS STARTED SINGING. There is nothing to un-gate, so the sends are seeded from what
    // this profile gives a lead vocal - the same table TUNE plans from.
    c.setStripEffects (pastor, true);
    CHECK (c.stripEffectsOn (pastor));
    const auto& sends = c.getKept().strips[size_t (pastor)].sendDb;
    CHECK_NEAR (sends[size_t (FxSlot::VocalPlate)],
                MixProfile::defaultSendDb (c.getSession().profile, RoleFamily::LeadVocal, FxSlot::VocalPlate), 0.01f);
    // ...and he is still a speaking microphone on the SPEECH group. Only the effects changed.
    CHECK (c.getSession().inputs[size_t (pastor)].role == ChannelRole::Speech);
    CHECK (c.getGraph().strips[size_t (pastor)].bus == MixBus::Speech);

    // BACK TO PREACHING, and back to singing again: the seeded levels are now his own and the
    // switch only ever gates them.
    const auto seeded = sends;
    c.setStripEffects (pastor, false);
    CHECK (c.getKept().strips[size_t (pastor)].sendDb == seeded);
    c.setStripEffects (pastor, true);
    CHECK (c.getKept().strips[size_t (pastor)].sendDb == seeded);

    // An engineer's own level survives the switch untouched.
    c.setStripEffects (pastor, false);
    c.setStripSend (pastor, FxSlot::VocalPlate, -4.0f);
    c.setStripEffects (pastor, true);
    CHECK_NEAR (c.getKept().strips[size_t (pastor)].sendDb[size_t (FxSlot::VocalPlate)], -4.0f, 0.01f);
}

TEST_CASE ("MixController: a voice microphone is given a job, and the next tune plans it as that")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    // band() is Kick, Bass, Keys, Lead, Vox. The lead is strip 3.
    const int lead = 3, kick = 0;
    CHECK (c.isVoiceChannel (lead));
    CHECK (c.isVoiceChannel (4));
    CHECK (! c.isVoiceChannel (kick));            // a kick drum is not offered a job
    CHECK (! c.isVoiceChannel (2));               // nor a keyboard
    CHECK (c.voiceJobs().size() == 4);
    for (const auto& job : c.voiceJobs())
    {
        CHECK (juce::String (job.name).isNotEmpty());
        CHECK (juce::String (job.what).length() > 40);      // every one of them says what it does
    }

    // A handheld that was a lead vocal becomes the preaching microphone: it moves to the
    // SPEECH group and takes the profile's own starting point for a speech channel.
    CHECK (c.getGraph().strips[size_t (lead)].bus == MixBus::Lead);
    const auto wasKick = c.getKept().strips[size_t (kick)];
    const auto wasLead = c.getKept().strips[size_t (lead)].channel;
    REQUIRE (c.setInputRole (lead, ChannelRole::Speech));
    c.prepare (kSr, kBlock);
    CHECK (c.getSession().inputs[size_t (lead)].role == ChannelRole::Speech);
    CHECK (c.getGraph().strips[size_t (lead)].bus == MixBus::Speech);
    CHECK (! diffParameters (wasLead, c.getKept().strips[size_t (lead)].channel).empty());

    // It is the profile's starting point for what it now is, not a frozen preset.
    {
        const auto raw = startingPoint (c.getSession(), c.getGraph());
        CHECK (diffParameters (c.getKept().strips[size_t (lead)].channel, raw.strips[size_t (lead)].channel).empty());
    }
    // ...and nothing else moved.
    CHECK (diffParameters (wasKick.channel, c.getKept().strips[size_t (kick)].channel).empty());
    CHECK_NEAR (wasKick.faderDb, c.getKept().strips[size_t (kick)].faderDb, 0.001f);

    // THE NEXT TUNE PLANS IT AS WHAT IT NOW IS, and the choice survives the tune.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    CHECK (c.getSession().inputs[size_t (lead)].role == ChannelRole::Speech);
    CHECK (c.getGraph().strips[size_t (lead)].bus == MixBus::Speech);
    // ...and the mix it landed on is a speech channel's, not a lead vocal's: TUNE planned it
    // from the speech family because that is what the input is now.
    {
        auto asLead = c.getSession();
        asLead.inputs[size_t (lead)].role = ChannelRole::LeadVocal;
        const auto leadRaw = startingPoint (asLead, RoutingGraph::build (asLead));
        CHECK (! diffParameters (c.getKept().strips[size_t (lead)].channel, leadRaw.strips[size_t (lead)].channel).empty());
    }

    // The way back is the mix history, because a graph change is a different mix and clears UNDO.
    bool named = false;
    for (const auto& cp : c.getCheckpoints()) if (cp.what.find ("Before Lead became") != std::string::npos) named = true;
    CHECK (named);

    // Asking for what it already is changes nothing; so does a strip that is not there.
    CHECK (! c.setInputRole (lead, ChannelRole::Speech));
    CHECK (! c.setInputRole (99, ChannelRole::Speech));
    CHECK (! c.setInputRole (-1, ChannelRole::Speech));

    // LIVE SAFE refuses it: the graph is rebuilt, and that stops the audio for a moment.
    c.setLiveSafe (true);
    std::string said;
    c.onMessage = [&said] (const std::string& m) { said = m; };
    CHECK (! c.setInputRole (lead, ChannelRole::LeadVocal));
    CHECK (said.find ("LIVE SAFE") != std::string::npos);
    CHECK (c.getSession().inputs[size_t (lead)].role == ChannelRole::Speech);
    c.setLiveSafe (false);

    // ...and back again: a handheld is a lead vocal in the last song.
    REQUIRE (c.setInputRole (lead, ChannelRole::LeadVocal));
    c.prepare (kSr, kBlock);
    CHECK (c.getGraph().strips[size_t (lead)].bus == MixBus::Lead);
}

TEST_CASE ("SampleLibrary: a sound imported from a strip is copied into the session and travels with it")
{
    // A session folder, and one WAV that is not in it. This is what an engineer does when the
    // kick the church actually uses is a file on their desktop.
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("dine-sample-import").getChildFile ("Sunday");
    folder.deleteRecursively();
    folder.createDirectory();
    auto elsewhere = folder.getParentDirectory().getChildFile ("Their kick.wav");
    elsewhere.deleteFile();
    {
        // A short, real hit: a decaying sine, so decoding it has something to find.
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out (elsewhere.createOutputStream());
        REQUIRE (out != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.release(), 48000.0, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> hit (1, 12000);
        for (int i = 0; i < hit.getNumSamples(); ++i)
            hit.setSample (0, i, 0.9f * std::sin (2.0f * float (M_PI) * 60.0f * float (i) / 48000.0f)
                                      * std::exp (-3.0f * float (i) / 48000.0f));
        writer->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples());
    }

    SampleLibrary library;
    library.setSessionFolder (folder);
    library.load();
    const int builtInKicks = library.numSounds (RoleFamily::Kick);
    REQUIRE (builtInKicks > 0);

    juce::String problem;
    const auto name = library.importSound (RoleFamily::Kick, elsewhere, problem);
    CHECK (problem.isEmpty());
    CHECK (name == "Their kick");

    // IT WAS COPIED, NOT REFERENCED: the file is inside the session folder, and deleting the
    // original changes nothing about what the session can play.
    const auto copied = folder.getChildFile ("Samples").getChildFile ("kick").getChildFile ("Their kick.wav");
    CHECK (copied.existsAsFile());
    elsewhere.deleteFile();
    library.load();
    CHECK (library.numSounds (RoleFamily::Kick) == builtInKicks + 1);
    CHECK (library.soundNames (RoleFamily::Kick).contains ("Their kick"));

    // ...and it is findable by the identity a session stores, and marked as the session's own.
    const int slot = library.slotFor (RoleFamily::Kick, "Their kick", true, "kick/Their kick.wav");
    CHECK (slot >= 0);
    REQUIRE (slot < int (library.sounds (RoleFamily::Kick).size()));
    CHECK (library.sounds (RoleFamily::Kick)[size_t (slot)].inSession);
    CHECK (library.table()->bank (RoleFamily::Kick, slot) != nullptr);

    // A file DINE cannot play never reaches the session folder: it is decoded before it is
    // copied, so a Sunday is not where you find out.
    auto rubbish = folder.getParentDirectory().getChildFile ("notes.txt");
    rubbish.replaceWithText ("this is not a drum");
    CHECK (library.importSound (RoleFamily::Kick, rubbish, problem).isEmpty());
    CHECK (problem.isNotEmpty());
    CHECK (! folder.getChildFile ("Samples").getChildFile ("kick").getChildFile ("notes.txt").existsAsFile());

    // Only the three drums that have a Sample stage are offered one.
    CHECK (library.importSound (RoleFamily::LeadVocal, copied, problem).isEmpty());
    CHECK (problem.contains ("kick"));

    // The cap is a real number and it is no longer eight: a church with a folder of kicks
    // reaches all of them.
    CHECK (SampleBankTable::kSounds >= 24);
    CHECK (! library.familyFull (RoleFamily::Kick));

    folder.getParentDirectory().deleteRecursively();
}

TEST_CASE ("MixController: a favourite mix is a scene that was also measured, and TUNE can aim at it")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);

    // A favourite marked before anything has been heard keeps the mix and says so: there is
    // nothing measured to aim at, and DINE does not pretend there is.
    std::string said;
    c.onMessage = [&said] (const std::string& m) { said = m; };
    REQUIRE (c.markFavourite ("Too early"));
    CHECK (c.numFavourites() == 1);
    CHECK (! c.getFavourite (0).sound.valid);
    CHECK (said.find ("not measured") != std::string::npos);
    CHECK (! c.useFavouriteAsReference (0));
    CHECK (! c.hasReference());
    c.removeFavourite (0);
    CHECK (c.numFavourites() == 0);

    // A real one: listen, tune, keep, then mark.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    c.keepPlan();
    c.setStripFader (3, -2.5f);
    REQUIRE (c.markFavourite ("Sunday 09:30"));
    REQUIRE (c.numFavourites() == 1);

    const auto& fav = c.getFavourite (0);
    CHECK (fav.name == std::string ("Sunday 09:30"));
    CHECK (fav.favourite);
    CHECK (fav.kept);
    CHECK (fav.whenMs > 0);
    CHECK_NEAR (fav.mix.strips[3].faderDb, -2.5f, 0.01f);

    // THE FINGERPRINT IS MEASURED, not read off a fader: the master's own numbers, where each
    // group landed against it, and the relationships under their own stable names.
    REQUIRE (fav.sound.valid);
    CHECK (fav.sound.masterLufs > -100.0f);
    CHECK (! fav.sound.metrics.empty());
    bool anyBus = false;
    for (int b = 0; b < int (MixBus::Master); ++b) anyBus = anyBus || fav.sound.busMeasured[size_t (b)];
    CHECK (anyBus);

    // THE FOUR SERVICE SLOTS ARE UNTOUCHED BY IT. A favourite is a scene past them, so the
    // pads on LIVE still mean what they meant.
    for (int i = 0; i < kMixScenes; ++i) CHECK (! c.getScene (i).favourite);
    c.keepScene (0);
    CHECK (c.getScene (0).kept);
    CHECK (! c.getScene (0).favourite);
    CHECK (c.numFavourites() == 1);

    // IT IS AIMABLE AT, through the same Reference a record goes through - no second target.
    REQUIRE (c.useFavouriteAsReference (0));
    REQUIRE (c.hasReference());
    CHECK (c.getReference().name == std::string ("Sunday 09:30"));
    CHECK_NEAR (c.getReference().loudnessLufs, fav.sound.masterLufs, 0.01f);
    CHECK (c.getReference().bandEnergyDb == fav.sound.masterBandDb);

    // ...and recalled like a scene, refused onto a different console like a scene.
    c.setStripFader (3, -9.0f);
    REQUIRE (c.recallFavourite (0));
    CHECK_NEAR (c.getKept().strips[3].faderDb, -2.5f, 0.01f);

    c.renameFavourite (0, "The one from the 9:30");
    CHECK (c.getFavourite (0).name == std::string ("The one from the 9:30"));

    // A LIST, NOT ONE SLOT.
    REQUIRE (c.markFavourite ("Later that morning"));
    CHECK (c.numFavourites() == 2);
    CHECK (c.getFavourite (1).name == std::string ("Later that morning"));
    CHECK (c.getScenes().size() == size_t (kMixScenes) + 2);

    // Out of range does nothing at all, in either direction.
    CHECK (! c.recallFavourite (7));
    CHECK (! c.useFavouriteAsReference (-1));
    c.removeFavourite (99);
    CHECK (c.numFavourites() == 2);
}

TEST_CASE ("MixController: Autopilot holds the mix it was given, and hands a fader back the moment you touch it")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    // Every poll decides, so the stretches below are long enough to learn AND to act: at the
    // default interval, and shorter than the 8 s it learns for, these checks could not fail.
    c.setAutopilotIntervalMs (0);
    Feeder f (c);

    std::string said;
    c.onMessage = [&said] (const std::string& m) { said = m; };

    // Nothing playing: there is no mix to hold, and DINE says so rather than engaging on
    // silence and calling it a target.
    CHECK (! c.setAutopilot (true));
    CHECK (! c.isAutopilotOn());
    CHECK (said.find ("Nothing is playing") != std::string::npos);

    f.play (1.0);
    REQUIRE (c.setAutopilot (true));
    CHECK (c.isAutopilotOn());
    CHECK (c.getAutopilot().on);
    // A place to come back to is kept before it starts.
    bool named = false;
    for (const auto& cp : c.getCheckpoints()) if (cp.what.find ("Before Autopilot") != std::string::npos) named = true;
    CHECK (named);

    // WITHIN TOLERANCE IT DOES NOTHING. The same band, playing the same way, for a while.
    const MixParameters before = c.getKept();
    f.play (14.0);
    for (int b = 0; b < int (MixBus::Count); ++b)
        CHECK_MESSAGE (std::fabs (c.getKept().buses[size_t (b)].faderDb - before.buses[size_t (b)].faderDb) < 0.001f,
                       std::string ("Autopilot moved ") + mixBusName (MixBus (b)) + " with nothing wrong");
    CHECK (c.getAutopilot().groupsCorrected == 0);

    // IT NEVER TOUCHES ANYTHING BUT A GROUP FADER, whatever it sees.
    for (int i = 0; i < c.getKept().numStrips; ++i)
    {
        CHECK (diffParameters (before.strips[size_t (i)].channel, c.getKept().strips[size_t (i)].channel).empty());
        CHECK_NEAR (before.strips[size_t (i)].faderDb, c.getKept().strips[size_t (i)].faderDb, 0.001f);
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
        CHECK (diffParameters (before.buses[size_t (b)].channel, c.getKept().buses[size_t (b)].channel).empty());
    CHECK_NEAR (before.master().faderDb, c.getKept().master().faderDb, 0.001f);

    // THE ENGINEER OUTRANKS IT. A move on a group it had been correcting hands that group
    // back for this engagement.
    // back for this engagement - whether or not it had moved it yet: a first move by the
    // engineer (the piano up for a solo) must not be pulled back either.
    CHECK (! c.getAutopilot().released[size_t (MixBus::Drums)]);
    said.clear();
    c.setBusFader (MixBus::Drums, -2.0f);
    CHECK (c.getAutopilot().released[size_t (MixBus::Drums)]);
    CHECK (said.find ("is yours again") != std::string::npos);
    {
        MixController::AutopilotState& state = const_cast<MixController::AutopilotState&> (c.getAutopilot());
        state.movedDb[size_t (MixBus::Music)] = 0.5f;               // as though it had corrected MUSIC
    }
    said.clear();
    c.setBusFader (MixBus::Music, -1.0f);
    CHECK (c.getAutopilot().released[size_t (MixBus::Music)]);
    CHECK (said.find ("is yours again") != std::string::npos);

    // One press off, and nothing goes back: the faders are where they are.
    const float drumsNow = c.getKept().buses[size_t (MixBus::Drums)].faderDb;
    c.setAutopilot (false);
    CHECK (! c.isAutopilotOn());
    CHECK_NEAR (c.getKept().buses[size_t (MixBus::Drums)].faderDb, drumsNow, 0.001f);
    CHECK (c.getAutopilot().groupsCorrected == 0);

    // It never works against something the engineer is in the middle of.
    f.play (0.5);
    REQUIRE (c.setAutopilot (true));
    c.setBypass (true);
    const MixParameters underBypass = c.getKept();
    f.play (12.0);
    for (int b = 0; b < int (MixBus::Count); ++b)
        CHECK_NEAR (underBypass.buses[size_t (b)].faderDb, c.getKept().buses[size_t (b)].faderDb, 0.001f);
    c.setBypass (false);
    c.setAutopilot (false);
}

TEST_CASE ("MixController: Autopilot, closed loop through the engine - it converges, it is bounded, and the master is not a drift")
{
    // The pure decision is tested in tests/Mix/AutopilotTests.cpp. This is the loop: what it
    // measures has to move when it moves a fader, or it walks every group to the end of its
    // travel - which is what it did while it measured each group before its own fader.
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setAutopilotIntervalMs (0);
    // The lead with nothing holding its level, so a step back at the microphone is a step back
    // in the mix and the loop has something to close.
    {
        auto lead = c.getKept().strips[3].channel;
        lead.compEnabled = false;
        lead.gateEnabled = false;
        lead.deEssEnabled = false;
        c.setStripChannel (3, lead);
        auto group = c.getKept().buses[size_t (MixBus::Lead)].channel;     // ... and the group's own compressor
        group.compEnabled = false;
        c.setBusChannel (MixBus::Lead, group);
    }
    Feeder f (c);
    f.play (1.0);
    REQUIRE (c.setAutopilot (true));
    f.play (10.0);                                          // learnt, over seconds of the band
    const MixParameters learnt = c.getKept();
    for (int b = 0; b < int (MixBus::Count); ++b)
        CHECK_NEAR (c.getKept().buses[size_t (b)].faderDb, learnt.buses[size_t (b)].faderDb, 0.001f);

    // THE MASTER IS NOT A DRIFT. Pulling the master down 3 dB used to read as every group 3 dB
    // too loud, and every one of them was walked down.
    c.setBusFader (MixBus::Master, learnt.master().faderDb - 3.0f);
    f.play (12.0);
    for (int b = 0; b < int (MixBus::Master); ++b)
        CHECK_MESSAGE (std::fabs (c.getKept().buses[size_t (b)].faderDb - learnt.buses[size_t (b)].faderDb) < 0.001f,
                       std::string ("Autopilot moved ") + mixBusName (MixBus (b)) + " because the master moved");

    // The singer steps back 6 dB. LEAD comes up - by less than the whole 6 (it is bounded) and
    // it stops: it does not keep stepping once the lead is back where it was.
    f.leadGain = 0.5f;                                      // a step back: 6 dB at the microphone
    f.play (16.0);
    const float leadMove = c.getKept().buses[size_t (MixBus::Lead)].faderDb - learnt.buses[size_t (MixBus::Lead)].faderDb;
    // It came up - and it stopped once the lead was back inside the tolerance, short of both
    // the whole 4 dB and its bound. A loop that could not see its own moves ran to the bound.
    CHECK (leadMove > 0.9f);
    CHECK (leadMove < c.getAutopilotLimits().maxTotalDb - 0.4f);
    const float settled = c.getKept().buses[size_t (MixBus::Lead)].faderDb;
    f.play (6.0);
    CHECK_NEAR (c.getKept().buses[size_t (MixBus::Lead)].faderDb, settled, 0.51f);
    // ...and nothing else was pulled around to make room for it.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        if (MixBus (b) == MixBus::Lead) continue;
        CHECK_MESSAGE (std::fabs (c.getKept().buses[size_t (b)].faderDb - learnt.buses[size_t (b)].faderDb) <= 1.01f,
                       std::string ("Autopilot pulled ") + mixBusName (MixBus (b)) + " around for the lead");
    }

    // Everything it did is in the history, including what had not added up to an entry yet.
    c.setAutopilot (false);
    bool logged = false;
    for (const auto& cp : c.getCheckpoints()) logged = logged || cp.what.find ("Autopilot: LEAD") == 0;
    CHECK (logged);
}

TEST_CASE ("MixController: Autopilot follows the engineer's own changes, and a new session turns it off")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setAutopilotIntervalMs (0);
    Feeder f (c);
    f.play (1.0);
    REQUIRE (c.setAutopilot (true));
    f.play (10.0);
    const float music = c.getKept().buses[size_t (MixBus::Music)].faderDb;

    // The keys' gain pulled 8 dB by hand: MUSIC is quieter because the engineer made it so.
    // Autopilot learns that mix instead of pushing MUSIC back up against them.
    c.setStripInputGain (2, c.getKept().strips[2].inputGainDb - 8.0f);
    f.play (20.0);
    CHECK_NEAR (c.getKept().buses[size_t (MixBus::Music)].faderDb, music, 0.001f);

    // Another session is not the mix it was engaged on.
    c.resetDocument();
    CHECK (! c.isAutopilotOn());
}

TEST_CASE ("MixController: a listen the audio device restarts under is ended, not left waiting")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.5);
    c.startTuneMix ({ 4.0f, -200.0f, 0.0f });
    REQUIRE (c.getStage() == MixController::Stage::Listening);
    c.prepare (kSr, kBlock);                         // the device re-opened mid-listen
    CHECK (c.getStage() != MixController::Stage::Listening);
}

TEST_CASE ("MixController: the master's ceiling stays closed - from a file, under LIVE SAFE, and on a feed")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    REQUIRE (c.getKept().master().channel.limiterEnabled);

    // From a file: a master saved open comes back with its ceiling.
    SessionStore::Document d;
    d.session = c.getSession();
    d.hasMix = true;
    d.mix = c.getKept();
    d.mix.master().channel.limiterEnabled = false;
    d.mix.master().channel.bypassAll = true;
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (SessionStore::toVar (d), back));
    CHECK (back.mix.master().channel.limiterEnabled);
    CHECK (! back.mix.master().channel.bypassAll);

    // Under LIVE SAFE it cannot be switched off.
    c.setLiveSafe (true);
    auto open = c.getKept().master().channel;
    open.limiterEnabled = false;
    c.setBusChannel (MixBus::Master, open);
    CHECK (c.getKept().master().channel.limiterEnabled);
    c.setLiveSafe (false);

    // A feed of the finished mix is never lifted past the ceiling.
    OutputFeeds feeds;
    feeds.count = 2;
    feeds.feeds[0] = { 0, 1 };
    feeds.feeds[1] = { 2, 3 };
    feeds.feeds[1].gainDb = 9.0f;
    c.setOutputFeeds (feeds);
    CHECK (c.getOutputFeeds().feeds[1].gainDb <= 0.0f);
}

TEST_CASE ("MixController: a mix is never handed to an engine playing a different set of inputs")
{
    // Parameters go to the engine by strip position. Between a change of inputs and the
    // device being reconfigured, the engine still plays the old strips - so every strip it
    // plays takes the settings of the strip listening to the same device channels, never
    // the settings of whatever now sits at its position.
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.1);
    const float bgvBefore = c.getEngine().getAppliedParameters().strips[4].faderDb;

    auto moved = band();
    moved.inputs.insert (moved.inputs.begin() + 1, { "Pastor", ChannelRole::Speech, 5, -1 });   // an input added in the middle
    c.setSession (moved);
    c.setStripFader (4, -20.0f);          // the lead, in the new layout; strip 4 is the backing vocal in the engine's
    f.play (0.1);
    CHECK_NEAR (c.getEngine().getAppliedParameters().strips[3].faderDb, -20.0f, 0.001f);   // the lead's own input
    CHECK_NEAR (c.getEngine().getAppliedParameters().strips[4].faderDb, bgvBefore, 0.001f); // not its neighbour

    // Once the engine is prepared with the new inputs, the new layout is what it plays.
    c.prepare (kSr, kBlock);
    f.play (0.1);
    CHECK_NEAR (c.getEngine().getAppliedParameters().strips[4].faderDb, -20.0f, 0.001f);
}

TEST_CASE ("MixController: MUTE, DIM and every fader still reach the audio while the device catches up")
{
    // SPEAKING / SINGING on the TUNE page change a voice's job without re-opening the device.
    // The console must not freeze in between: a howl is muted with the device as it is.
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.1);

    REQUIRE (c.setInputRole (3, ChannelRole::Speech));    // the lead becomes the pastor's microphone
    REQUIRE (c.needsReconfigure());                        // ...and nobody has re-opened the device yet
    c.setBroadcastMute (true);
    c.setStripMute (2, true);
    c.setStripFader (1, -12.0f);
    f.play (0.1);
    const auto& heard = c.getEngine().getAppliedParameters();
    CHECK (heard.broadcastMute);
    CHECK (heard.strips[2].mute);
    CHECK_NEAR (heard.strips[1].faderDb, -12.0f, 0.001f);
    CHECK (heard.numStrips == band().numStrips());
}

TEST_CASE ("MixController: nothing is kept by pressing TUNE again, and a move made on BEFORE stays made")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.5);
    const auto before = c.getKept();

    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    const auto proposed = c.getBase();

    // Re-tune while the proposal waits: refused, the proposal is still waiting, nothing kept.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    CHECK (c.getStage() == MixController::Stage::Preview);
    CHECK (MixPlanner::countParameterChanges (proposed, c.getBase()) == 0);
    c.startTuneChannel (1);
    CHECK (c.getStage() == MixController::Stage::Preview);

    // On BEFORE a howling microphone is muted: heard at once, and REVERT does not un-mute it.
    c.setCompare (MixController::Compare::Before);
    c.setStripMute (3, true);
    c.setBusFader (MixBus::Drums, -9.0f);
    f.play (0.1);
    CHECK (c.getEngine().getAppliedParameters().strips[3].mute);
    CHECK_NEAR (c.getEngine().getAppliedParameters().buses[size_t (MixBus::Drums)].faderDb, -9.0f, 0.001f);
    c.revertPlan();
    f.play (0.1);
    CHECK (c.getKept().strips[3].mute);
    CHECK (c.getEngine().getAppliedParameters().strips[3].mute);
    CHECK_NEAR (c.getKept().buses[size_t (MixBus::Drums)].faderDb, -9.0f, 0.001f);
    // ...and the rest of the mix is the one from before the run.
    CHECK_NEAR (c.getKept().strips[1].faderDb, before.strips[1].faderDb, 0.001f);
}

TEST_CASE ("MixController: solo in place is refused under LIVE SAFE, and a session never opens in it")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setLiveSafe (true);
    c.setSoloMode (SoloMode::InPlace);
    CHECK (c.getMonitor().mode == SoloMode::Monitor);
    c.setLiveSafe (false);
    c.setSoloMode (SoloMode::InPlace);
    REQUIRE (c.getMonitor().mode == SoloMode::InPlace);

    SessionStore::Document d;
    d.session = c.getSession();
    d.hasMix = true;
    d.mix = c.getKept();
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (SessionStore::toVar (d), back));
    CHECK (back.mix.monitor.mode == SoloMode::Monitor);
}

TEST_CASE ("MixController: the engineer's listen never takes the room's outputs")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    OutputFeeds feeds;
    feeds.count = 3;
    feeds.feeds[0] = { 0, 1 };                                 // the stream
    feeds.feeds[1] = { 2, 3 }; feeds.feeds[1].monitor = true;  // the engineer's listen
    feeds.feeds[2] = { 4, 5 };                                 // the room
    c.setOutputFeeds (feeds);
    REQUIRE (c.getOutputFeeds().feeds[2].left == 4);

    // Solo moved onto the room's pair - with LIVE SAFE on, where only the listen may move.
    c.setLiveSafe (true);
    feeds.feeds[1].left = 4; feeds.feeds[1].right = 5;
    c.setOutputFeeds (feeds);
    const auto& now = c.getOutputFeeds();
    CHECK (now.feeds[2].left == 4);
    CHECK (now.feeds[2].right == 5);
    CHECK (! now.feeds[1].routed());
    CHECK (now.count == 3);
}

TEST_CASE ("MixController: renaming a channel keeps every scene and history entry")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setStripFader (1, -7.0f);
    c.keepScene (0);
    c.checkpoint ("Before the sermon", false);
    c.setStripFader (1, 0.0f);

    c.setInputName (4, "Grace");                 // this week's singer on the backing mic
    CHECK (c.getGraph().strips[4].name == "Grace");
    REQUIRE (c.recallScene (0));
    CHECK_NEAR (c.getKept().strips[1].faderDb, -7.0f, 0.001f);
    c.setStripFader (1, 0.0f);
    const auto list = c.getCheckpoints();
    REQUIRE (! list.empty());
    int index = -1;
    for (int i = 0; i < int (list.size()); ++i) if (list[size_t (i)].what == "Before the sermon") index = i;
    REQUIRE (index >= 0);
    CHECK (c.restoreCheckpoint (index));
    CHECK_NEAR (c.getKept().strips[1].faderDb, -7.0f, 0.001f);
}

TEST_CASE ("Mix Buddy: a question never changes the mix, and nothing is kept for you")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    f.play (0.5);

    // Asking changes nothing: not a parameter, not the history, not the undo stack.
    const auto kept = c.getKept();
    const bool couldUndo = c.canUndoMix();
    const auto marks = c.getCheckpoints().size();
    c.askBuddy ("why can't I hear the lead?");
    c.askBuddy ("turn the lead up");
    c.askBuddy ("how do I save this mix?");
    CHECK (MixPlanner::countParameterChanges (kept, c.getKept()) == 0);
    CHECK (c.canUndoMix() == couldUndo);
    CHECK (c.getCheckpoints().size() == marks);
    REQUIRE (c.getChat().size() == 6);
    CHECK (! c.getChat()[1].text.empty());

    // A proposal waiting on BEFORE / AFTER is the engineer's to decide. A change asked for from
    // Mix Buddy is refused rather than keeping it, and so is a TUNE LIVE MIX run.
    c.startTuneMix ({ 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    const auto proposed = c.getBase();
    CHECK (! c.askForChange ("lead brighter"));
    CHECK (c.getStage() == MixController::Stage::Preview);
    c.startTuneLiveMix();
    CHECK (c.getStage() == MixController::Stage::Preview);            // not kept, not replaced
    CHECK (MixPlanner::countParameterChanges (proposed, c.getBase()) == 0);
    c.revertPlan();

    // LIVE SAFE refuses a proposed change outright, and says why in the conversation.
    c.setLiveSafe (true);
    CHECK (! c.askForChange ("lead brighter"));
    CHECK (c.getChat().back().failed);
    CHECK (c.getChat().back().text.find ("LIVE SAFE") != std::string::npos);
    c.setLiveSafe (false);
}

TEST_CASE ("MixController: a tune says what it was about in fixed words, and never with a channel's name")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    Feeder f (c);
    std::vector<MixController::UsageEvent> seen;
    c.onUsage = [&seen] (const MixController::UsageEvent& e) { seen.push_back (e); };
    const auto word = [] (const MixController::UsageEvent& e, const std::string& k)
    {
        for (const auto& [key, v] : e.words) if (key == k) return v;
        return std::string();
    };

    c.startTuneChannel (1, { 2.0f, -200.0f, 0.0f });       // the bass
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    REQUIRE (! seen.empty());
    const auto result = seen.back();
    CHECK (result.name == "tune_result");
    CHECK (word (result, "scope") == "channel");
    CHECK (word (result, "family") == "bass");
    CHECK (word (result, "kind") == "bass");
    CHECK (word (result, "outcome") == "proposal");
    c.keepPlan();
    CHECK (seen.back().name == "tune_decision");
    CHECK (word (seen.back(), "decision") == "kept");

    c.startTuneBus (MixBus::Drums, { 2.0f, -200.0f, 0.0f });
    f.play (2.6);
    REQUIRE (f.waitFor (MixController::Stage::Preview));
    CHECK (word (seen.back(), "scope") == "group");
    CHECK (word (seen.back(), "group") == "DRUMS");
    c.revertPlan();
    CHECK (word (seen.back(), "decision") == "reverted");

    c.keepScene (0);
    CHECK (seen.back().name == "preset_saved");
    CHECK (word (seen.back(), "kind") == "scene");

    // Nothing the user named ever rides along.
    for (const auto& e : seen)
        for (const auto& [k, v] : e.words)
            for (const auto& input : band().inputs) CHECK (v != input.name);
}

TEST_CASE ("MixController: an effect's sound and the song's tempo are set by hand, kept, saved and undone")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    REQUIRE (c.getGraph().fxUsed[size_t (FxSlot::VocalPlate)]);
    REQUIRE (c.getGraph().fxUsed[size_t (FxSlot::VocalDelay)]);
    const auto plateWas = c.getKept().fx[size_t (FxSlot::VocalPlate)].fx;

    // The plate: a longer tail that starts later. It reaches what the engine plays at once.
    auto plate = plateWas;
    plate.reverbDecayS = 3.2f;
    plate.reverbPreDelayMs = 60.0f;
    plate.reverbSize = 99.0f;                 // not one of the hand's fields: ignored
    c.setFxSlotCharacter (FxSlot::VocalPlate, plate);
    CHECK_NEAR (c.getKept().fx[size_t (FxSlot::VocalPlate)].fx.reverbDecayS, 3.2f, 1.0e-4f);
    CHECK_NEAR (c.getKept().fx[size_t (FxSlot::VocalPlate)].fx.reverbPreDelayMs, 60.0f, 1.0e-4f);
    CHECK_NEAR (c.getKept().fx[size_t (FxSlot::VocalPlate)].fx.reverbSize, plateWas.reverbSize, 1.0e-4f);
    CHECK_NEAR (c.getRunning().fx[size_t (FxSlot::VocalPlate)].fx.reverbDecayS, 3.2f, 1.0e-4f);

    // Out of range is held to the range, never passed to the audio.
    plate.reverbDecayS = 500.0f;
    plate.reverbPreDelayMs = -20.0f;
    c.setFxSlotCharacter (FxSlot::VocalPlate, plate);
    CHECK (c.getKept().fx[size_t (FxSlot::VocalPlate)].fx.reverbDecayS <= 10.0f);
    CHECK (c.getKept().fx[size_t (FxSlot::VocalPlate)].fx.reverbPreDelayMs >= 0.0f);

    // The delay on a note of the song, and the song's tempo.
    auto delay = c.getKept().fx[size_t (FxSlot::VocalDelay)].fx;
    delay.delaySync = true;
    delay.delayDivision = int (NoteDivision::Quarter);
    delay.delayFeedback = 45.0f;
    c.setFxSlotCharacter (FxSlot::VocalDelay, delay);
    c.setTempo (72.0f);
    CHECK_NEAR (c.getTempo(), 72.0f, 1.0e-4f);
    CHECK_NEAR (c.getRunning().tempoBpm, 72.0f, 1.0e-4f);
    CHECK (c.getRunning().fx[size_t (FxSlot::VocalDelay)].fx.delayDivision == int (NoteDivision::Quarter));
    c.setTempo (1000.0f);
    CHECK (c.getTempo() <= 240.0f);
    c.setTempo (72.0f);

    // Kept with the session: it comes back from the document.
    SessionStore::Document d;
    d.session = c.getSession();
    d.hasMix = true;
    d.mix = c.getKept();
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), back));
    CHECK_NEAR (back.mix.fx[size_t (FxSlot::VocalPlate)].fx.reverbPreDelayMs, c.getKept().fx[size_t (FxSlot::VocalPlate)].fx.reverbPreDelayMs, 0.01f);
    CHECK (back.mix.fx[size_t (FxSlot::VocalDelay)].fx.delayDivision == int (NoteDivision::Quarter));
    CHECK_NEAR (back.mix.fx[size_t (FxSlot::VocalDelay)].fx.delayFeedback, 45.0f, 0.01f);
    CHECK_NEAR (back.mix.tempoBpm, 72.0f, 0.01f);

    // And undone like any edit: the tempo first, the delay, then the plate.
    c.undoMix();
    CHECK (std::fabs (c.getTempo() - 72.0f) > 0.01f);
    c.undoMix(); c.undoMix(); c.undoMix(); c.undoMix();
    CHECK_NEAR (c.getKept().fx[size_t (FxSlot::VocalPlate)].fx.reverbDecayS, plateWas.reverbDecayS, 1.0e-4f);
}

TEST_CASE ("MixController: a channel's alignment delay is set by hand, held to its range, saved, and left out of BYPASS")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setStripDelay (1, 12.5f);
    CHECK_NEAR (c.getKept().strips[1].delayMs, 12.5f, 1.0e-4f);
    CHECK_NEAR (c.getRunning().strips[1].delayMs, 12.5f, 1.0e-4f);
    c.setStripDelay (1, 5000.0f);
    CHECK (c.getKept().strips[1].delayMs <= kMaxStripDelayMs);
    c.setStripDelay (1, -3.0f);
    CHECK (c.getKept().strips[1].delayMs >= 0.0f);
    c.setStripDelay (1, 21.0f);

    c.setBypass (true);                       // the console feed as it arrives
    CHECK (c.getRunning().strips[1].delayMs == 0.0f);
    c.setBypass (false);
    CHECK_NEAR (c.getRunning().strips[1].delayMs, 21.0f, 1.0e-4f);

    SessionStore::Document d;
    d.session = c.getSession();
    d.hasMix = true;
    d.mix = c.getKept();
    SessionStore::Document back;
    REQUIRE (SessionStore::fromVar (juce::JSON::parse (juce::JSON::toString (SessionStore::toVar (d))), back));
    CHECK_NEAR (back.mix.strips[1].delayMs, 21.0f, 0.01f);
    CHECK (back.mix.strips[0].delayMs == 0.0f);
}

TEST_CASE ("MixController: one effect return has its own fader and mute, under LIVE SAFE and through BYPASS")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    REQUIRE (c.getGraph().fxUsed[size_t (FxSlot::VocalPlate)]);
    REQUIRE (c.getGraph().fxUsed[size_t (FxSlot::VocalDelay)]);

    // Its own fader moves it and nothing else: not the other returns, not the FX group fader.
    const float delayWas = c.getKept().fx[size_t (FxSlot::VocalDelay)].returnDb;
    c.setFxSlotReturn (FxSlot::VocalPlate, -5.0f);
    CHECK (std::fabs (c.getKept().fx[size_t (FxSlot::VocalPlate)].returnDb + 5.0f) < 0.001f);
    CHECK (std::fabs (c.getKept().fx[size_t (FxSlot::VocalDelay)].returnDb - delayWas) < 0.001f);
    CHECK (std::fabs (c.getKept().fxReturnDb) < 0.001f);
    CHECK (std::fabs (c.getRunning().fx[size_t (FxSlot::VocalPlate)].returnDb + 5.0f) < 0.001f);

    // Under LIVE SAFE it is a fader like any other: one slip cannot throw it across the console.
    c.setLiveSafe (true);
    c.setFxSlotReturn (FxSlot::VocalPlate, -60.0f);
    CHECK (c.getKept().fx[size_t (FxSlot::VocalPlate)].returnDb >= -5.0f - LiveSafePolicy().maxFaderStepDb - 0.01f);
    c.setLiveSafe (false);

    // Its mute is the engineer's, so BYPASS keeps it - comparing must not put the delay back on.
    c.setFxSlotMute (FxSlot::VocalDelay, true);
    CHECK (c.getKept().fx[size_t (FxSlot::VocalDelay)].mute);
    CHECK (! c.getKept().fx[size_t (FxSlot::VocalPlate)].mute);
    c.setBypass (true);
    CHECK (c.getRunning().fx[size_t (FxSlot::VocalDelay)].mute);
    c.setBypass (false);
    CHECK (c.getRunning().fx[size_t (FxSlot::VocalDelay)].mute);
    c.setFxSlotMute (FxSlot::VocalDelay, false);
    CHECK (! c.getRunning().fx[size_t (FxSlot::VocalDelay)].mute);
}
