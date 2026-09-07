// DINELIVE application-layer tests: the controller's state machine (setup -> listen -> plan ->
// preview -> keep / revert, compare, macros, Advanced edits) and the session document round trip.
// No device, no UI: the engine is fed synthetic audio through MixController::process().
#include "TestFramework.h"
#include "native/MixController.h"
#include "native/SessionStore.h"
#include "Mix/MixPlanner.h"
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
    };
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
    CHECK (c.getStatusText() == "READY");

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
    c.setStripInputGain (4, 6.0f);
    CHECK (c.getKept().strips[0].faderDb == -3.0f);
    CHECK (c.getKept().strips[1].mute);
    CHECK (c.getKept().strips[4].inputGainDb == 6.0f);
    CHECK (c.getRunning().strips[0].faderDb == -3.0f);
    f.play (0.3);
    CHECK (f.outputPeak() > 0.001f);
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

    // Not a DINELIVE file: refused, nothing changed.
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
