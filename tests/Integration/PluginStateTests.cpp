// Plugin-level integration tests: run the real DrumsProcessor headlessly.
// Covers state save/restore, latency reporting, bypass, sample-rate changes,
// malformed state, and that Analyze never blocks processing.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "DrumsProcessor.h"
#include "State/ParameterIDs.h"
#include "AllocationTracker.h"
#include "State/PresetManager.h"
#include "Intelligence/OpenAIProvider.h"
#include "Intelligence/SafetyValidator.h"
#include <memory>
#include <vector>
#include <cstdio>
#include <cmath>

using namespace livemix;

static int failures = 0;
#define CHECK(expr) do { if (! (expr)) { ++failures; std::printf ("    %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void fillHits (juce::AudioBuffer<float>& b, double sr)
{
    juce::Random rng (3);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int since = i % int (sr / 4);
            const float env = std::exp (-float (since) / float (0.08 * sr));
            b.setSample (ch, i, 0.5f * env * (0.6f * std::sin (2.0f * juce::MathConstants<float>::pi * 120.0f * float (since) / float (sr)) + 0.4f * rng.nextFloat() * 2.0f - 0.4f));
        }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- State round trip ----
    {
        DrumsProcessor a;
        a.prepareToPlay (48000.0, 128);
        a.setRoleFromUI (ChannelRole::FloorTom);
        a.setProfileFromUI (StyleProfileId::ModernWorship);
        a.getBridge().setParameterValue (ParamID::gateThreshold, -27.0f);
        a.getBridge().setParameterValue (ParamID::punch, 80.0f);
        a.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        a.getBridge().setParameterValue (eqBandId ("toneEq", 2, "Gain"), 5.5f);
        a.setGroupName ("Kit B");
        a.setAIAssistEnabled (true);
        a.setAdvancedViewOpen (true);

        juce::MemoryBlock state;
        a.getStateInformation (state);
        CHECK (state.getSize() > 0);

        DrumsProcessor b;
        b.prepareToPlay (48000.0, 128);
        b.setStateInformation (state.getData(), int (state.getSize()));
        CHECK (b.getBridge().readRole() == ChannelRole::FloorTom);
        CHECK (b.getBridge().readStyle() == StyleProfileId::ModernWorship);
        CHECK (std::abs (b.getBridge().getParameterValue (ParamID::gateThreshold) - (-27.0f)) < 1e-3f);
        CHECK (std::abs (b.getBridge().getParameterValue (ParamID::punch) - 80.0f) < 1e-3f);
        CHECK (std::abs (b.getBridge().getParameterValue (eqBandId ("toneEq", 2, "Gain")) - 5.5f) < 1e-3f);
        CHECK (b.isLiveSafe());
        CHECK (b.getGroupName() == "Kit B");
        CHECK (! b.isAIAssistEnabled()); // AI assistance is switched off on purpose (AIFeature.h): a saved 'on' never comes back on
        CHECK (b.isAdvancedViewOpen());

        // Every parameter must match exactly.
        const auto pa = a.getBridge().read();
        const auto pb = b.getBridge().read();
        int mismatches = 0;
        ChannelParameters copyA = pa, copyB = pb;
        std::vector<float> va, vb;
        forEachDspParameter (copyA, [&] (const std::string&, auto& v) { va.push_back (float (v)); });
        forEachDspParameter (copyB, [&] (const std::string&, auto& v) { vb.push_back (float (v)); });
        for (size_t i = 0; i < va.size(); ++i) if (std::abs (va[i] - vb[i]) > 1e-4f) ++mismatches;
        CHECK (mismatches == 0);

        // Restoring must not re-trigger macro/preset writes: run the message loop briefly.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        CHECK (std::abs (b.getBridge().getParameterValue (ParamID::gateThreshold) - (-27.0f)) < 1e-3f);
    }

    // ---- Malformed state must not crash or change anything ----
    {
        DrumsProcessor p;
        p.prepareToPlay (44100.0, 64);
        const float before = p.getBridge().getParameterValue (ParamID::compThreshold);
        const char garbage[] = "this is not a valid state blob at all";
        p.setStateInformation (garbage, int (sizeof (garbage)));
        p.setStateInformation (nullptr, 0);
        juce::ValueTree wrong ("SomethingElse");
        juce::MemoryBlock mb; { juce::MemoryOutputStream os (mb, false); wrong.writeToStream (os); }
        p.setStateInformation (mb.getData(), int (mb.getSize()));
        CHECK (std::abs (p.getBridge().getParameterValue (ParamID::compThreshold) - before) < 1e-6f);
    }

    // ---- Latency, bypass, processing, sample-rate changes ----
    {
        DrumsProcessor p;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int block : { 32, 64, 128, 256 })
            {
                p.prepareToPlay (sr, block);
                CHECK (p.getLatencySamples() == 0);
                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                for (int n = 0; n < 50; ++n)
                {
                    fillHits (buf, sr);
                    p.processBlock (buf, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < block; ++i)
                            if (! std::isfinite (buf.getSample (ch, i))) { ++failures; std::printf ("non-finite output at sr %f block %d\n", sr, block); n = 50; break; }
                }
            }
        }

        // A/B bypass parameter: output equals input.
        p.prepareToPlay (48000.0, 128);
        p.getBridge().setParameterValue (ParamID::bypass, 1.0f);
        juce::AudioBuffer<float> buf (2, 128), ref (2, 128);
        fillHits (buf, 48000.0);
        ref.makeCopyOf (buf);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        bool same = true;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) if (std::abs (buf.getSample (ch, i) - ref.getSample (ch, i)) > 0.0f) same = false;
        CHECK (same);
        p.getBridge().setParameterValue (ParamID::bypass, 0.0f);

        // Host bypass path
        fillHits (buf, 48000.0); ref.makeCopyOf (buf);
        p.processBlockBypassed (buf, midi);
        same = true;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) if (std::abs (buf.getSample (ch, i) - ref.getSample (ch, i)) > 0.0f) same = false;
        CHECK (same);
    }

    // ---- Steady-state processBlock (full chain, parameters changing) never allocates ----
    {
        DrumsProcessor p;
        p.prepareToPlay (48000.0, 64);
        p.setRoleFromUI (ChannelRole::SnareTop);
        p.getBridge().setParameterValue (ParamID::satOn, 1.0f);
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        fillHits (buf, 48000.0);
        p.processBlock (buf, midi);
        {
            alloctrack::Scope scope;
            double totalUs = 0.0;
            for (int n = 0; n < 2000; ++n)
            {
                if (n % 50 == 0) p.getBridge().setParameterValue (ParamID::compThreshold, -20.0f - float (n % 7));
                const auto s = juce::Time::getHighResolutionTicks();
                p.processBlock (buf, midi);
                totalUs += 1.0e6 * double (juce::Time::getHighResolutionTicks() - s) / double (juce::Time::getHighResolutionTicksPerSecond());
            }
            CHECK (alloctrack::getCount() == 0);
            std::printf ("    processBlock allocations during 2000 blocks: %zu, mean %.2f us per 64-sample stereo block (budget 1333 us)\n",
                         alloctrack::getCount(), totalUs / 2000.0);
        }
    }

    // ---- Analyze runs alongside processing; Live Safe blocks it ----
    {
        DrumsProcessor p;
        p.prepareToPlay (48000.0, 64);
        p.setRoleFromUI (ChannelRole::SnareTop);
        p.startAnalyze();
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        // ~13 s of audio pushed as fast as possible while pumping the message loop.
        const int blocks = int (48000.0 * 13.0 / 64.0);
        for (int n = 0; n < blocks; ++n)
        {
            fillHits (buf, 48000.0);
            const auto s = juce::Time::getHighResolutionTicks();
            p.processBlock (buf, midi);
            const double us = 1.0e6 * double (juce::Time::getHighResolutionTicks() - s) / double (juce::Time::getHighResolutionTicksPerSecond());
            if (us > 1000.0) { ++failures; std::printf ("processBlock took %.0f us while analyzing\n", us); break; }
            if (n % 200 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        for (int i = 0; i < 400 && p.getAnalysis().getState() != AnalysisEngine::State::Complete; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        CHECK (p.getAnalysis().getState() == AnalysisEngine::State::Complete);
        CHECK (p.getRecommendations().valid);
        std::printf ("    analyze wall time %.1f s, input health: %s, %d items\n",
                     (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, p.getRecommendations().inputHealth.c_str(), int (p.getRecommendations().items.size()));

        p.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        CHECK (! p.isAnalyzeAvailable());
        p.startAnalyze();
        CHECK (! p.getAnalysis().isCapturing());
    }

    // ---- Presets: factory load, user save/load/delete round trip ----
    {
        DrumsProcessor p;
        p.prepareToPlay (48000.0, 128);
        PresetManager::Info snareAgg;
        for (auto& f : PresetManager::getFactoryPresets())
            if (f.role == ChannelRole::SnareTop && f.style == StyleProfileId::ModernWorship) snareAgg = f;
        CHECK (p.loadPreset (snareAgg));
        CHECK (p.getBridge().readRole() == ChannelRole::SnareTop);
        CHECK (p.getBridge().readStyle() == StyleProfileId::ModernWorship);
        CHECK (p.getBridge().read().satEnabled); // Aggressive baseline enables saturation

        p.getBridge().setParameterValue (ParamID::compRatio, 7.5f);
        p.getBridge().setParameterValue (ParamID::punch, 70.0f);
        const juce::String name = "LiveMixTest_" + juce::String (juce::Time::currentTimeMillis());
        PresetManager::Info saved;
        CHECK (PresetManager::saveUserPreset (name, p.createPresetTree (name), &saved));
        CHECK (saved.file.existsAsFile());

        DrumsProcessor q;
        q.prepareToPlay (48000.0, 128);
        CHECK (q.loadPreset (saved));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120); // macro timer must not re-derive parameters
        CHECK (std::abs (q.getBridge().getParameterValue (ParamID::compRatio) - 7.5f) < 1e-3f);
        CHECK (std::abs (q.getBridge().getParameterValue (ParamID::punch) - 70.0f) < 1e-3f);
        CHECK (q.getBridge().readRole() == ChannelRole::SnareTop);
        CHECK (q.getCurrentPresetName() == name);
        bool listed = false;
        for (auto& u : PresetManager::getUserPresets()) if (u.name == name) listed = true;
        CHECK (listed);
        CHECK (PresetManager::deleteUserPreset (saved));
        CHECK (! saved.file.existsAsFile());

        // Live Safe blocks preset loads.
        q.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        CHECK (! q.loadPreset (snareAgg));
    }

    // ---- Kit intelligence: 4 instances in one group, ANALYZE KIT from one of them ----
    {
        const juce::String group = "TestKit_" + juce::String (juce::Time::currentTimeMillis());
        std::vector<std::unique_ptr<DrumsProcessor>> kit;
        const ChannelRole roles[] = { ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::RackTom, ChannelRole::OverheadLeft };
        const float levels[] = { 0.5f, 0.45f, 0.06f, 0.2f }; // tom captured far too low
        for (int i = 0; i < 4; ++i)
        {
            auto p = std::make_unique<DrumsProcessor>();
            p->prepareToPlay (48000.0, 64);
            p->setRoleFromUI (roles[i]);
            p->setGroupName (group);
            kit.push_back (std::move (p));
        }
        CHECK (kit[0]->getKit().getGroupMembers (group.toStdString()).size() == 4);

        kit[0]->startKitAnalyze();
        CHECK (kit[0]->getKit().getState() == KitController::State::Analyzing);
        for (auto& p : kit) CHECK (p->getAnalysis().isCapturing() || p->kitIsAnalyzing());

        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        const int blocks = int (48000.0 * 13.0 / 64.0);
        for (int n = 0; n < blocks; ++n)
        {
            for (int i = 0; i < 4; ++i)
            {
                fillHits (buf, 48000.0);
                buf.applyGain (levels[i] / 0.5f);
                kit[size_t (i)]->processBlock (buf, midi);
            }
            if (n % 200 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        for (int i = 0; i < 600 && kit[0]->getKit().getState() == KitController::State::Analyzing; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        CHECK (kit[0]->getKit().getState() == KitController::State::Complete);
        const auto& r = kit[0]->getKit().getResult();
        CHECK (r.valid);
        CHECK (r.membersAnalyzed == 4);
        CHECK (r.capture.size() == 4);
        bool tomLow = false;
        for (auto& c : r.capture) if (c.name == "Rack Tom") tomLow = c.health == "Low" && c.captureGainDb > 0.0f;
        CHECK (tomLow);
        std::printf ("    kit: %d members, %zu processing notes, %zu balance items, %zu notes\n",
                     r.membersAnalyzed, r.processing.size(), r.balance.size(), r.notes.size());

        // Apply balance changes output trims only on the listed members.
        const float tomTrimBefore = kit[2]->getBridge().getParameterValue (ParamID::outputTrim);
        kit[0]->applyKitBalance();
        bool tomInBalance = false;
        for (auto& b : r.balance) if (b.instanceId == kit[2]->getInstanceId()) tomInBalance = true;
        if (tomInBalance)
            CHECK (kit[2]->getBridge().getParameterValue (ParamID::outputTrim) > tomTrimBefore);

        // Removing an instance mid-life must not break the group.
        kit.pop_back();
        CHECK (kit[0]->getKit().getGroupMembers (group.toStdString()).size() == 3);
        kit[0]->applyKitSafeChanges();
    }

    // ---- 48 instances: steady-state cost and zero overruns at 64 samples ----
    {
        std::vector<std::unique_ptr<DrumsProcessor>> many;
        for (int i = 0; i < 48; ++i)
        {
            auto p = std::make_unique<DrumsProcessor>();
            p->prepareToPlay (48000.0, 64);
            p->setRoleFromUI (channelRoleFromIndex (i % int (ChannelRole::Count)));
            p->getBridge().setParameterValue (ParamID::satOn, 1.0f);
            many.push_back (std::move (p));
        }
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        fillHits (buf, 48000.0);
        const int blocks = int (48000.0 * 3.0 / 64.0);
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int n = 0; n < blocks; ++n)
            for (auto& p : many) p->processBlock (buf, midi);
        const double totalUs = 1.0e6 * double (juce::Time::getHighResolutionTicks() - t0) / double (juce::Time::getHighResolutionTicksPerSecond());
        const double usPerBlockAll = totalUs / blocks;
        int overruns = 0;
        for (auto& p : many) overruns += p->getDiagnostics().overBudgetBlocks;
        std::printf ("    48 instances @ 64 smp: %.0f us per block for all (%.1f%% of budget), %d over-budget blocks\n",
                     usPerBlockAll, 100.0 * usPerBlockAll / 1333.3, overruns);
        CHECK (usPerBlockAll < 1333.0 * 0.6);
        CHECK (overruns == 0);
    }

    // ---- OpenAI provider: request body and response parsing (offline, no network) ----
    {
        IntelligenceRequest req;
        req.role = ChannelRole::SnareTop;
        req.style = StyleProfileId::ModernWorship;
        req.analysis.valid = true; req.analysis.peakDb = -14.0f; req.analysis.rmsDb = -28.0f;
        req.groupName = "Drum Kit 1";
        KitMember kick; kick.name = "Kick In"; kick.role = ChannelRole::KickIn; kick.analysis.valid = true; kick.analysis.peakDb = -10.0f;
        req.kitContext.push_back (kick);
        AISettings s; s.apiKey = "test"; s.model = "gpt-5"; s.effort = "medium";
        const auto body = OpenAIProvider::buildRequestBody (req, s);
        auto json = juce::JSON::parse (body);
        CHECK (json.getProperty ("model", "").toString() == "gpt-5");
        CHECK (json.getProperty ("reasoning_effort", "").toString() == "medium");
        CHECK (json.getProperty ("response_format", juce::var()).getProperty ("type", "").toString() == "json_schema");
        CHECK (bool (json.getProperty ("response_format", juce::var()).getProperty ("json_schema", juce::var()).getProperty ("strict", false)));
        CHECK (json.getProperty ("messages", juce::var()).getArray()->size() == 2);
        CHECK (body.contains ("allowed_parameters") && body.contains ("otherKitChannels") && body.contains ("Kick In"));
        CHECK (! body.contains ("inputTrim\"") || ! body.contains ("\"id\": \"inputTrim\"")); // capture gain is never an allowed AI parameter
        s.model = "gpt-4.1";
        CHECK (! OpenAIProvider::buildRequestBody (req, s).contains ("reasoning_effort")); // non-reasoning models get no effort field

        const juce::String canned = R"({"id":"x","model":"gpt-5","choices":[{"finish_reason":"stop","message":{"role":"assistant","refusal":null,
            "content":"{\"interpretation\":\"Bright snare with ring.\",\"sourceAssessment\":\"Healthy capture.\",\"recommendations\":[
              {\"kind\":\"EQ\",\"what\":\"-3 dB at 450 Hz\",\"why\":\"boxy\",\"confidence\":\"HIGH\",\"changes\":[{\"paramId\":\"corrEq1On\",\"value\":1},{\"paramId\":\"corrEq1Gain\",\"value\":-30},{\"paramId\":\"role\",\"value\":2}]},
              {\"kind\":\"CaptureGain\",\"what\":\"+4 dB preamp\",\"why\":\"low\",\"confidence\":\"MEDIUM\",\"changes\":[{\"paramId\":\"inputTrim\",\"value\":4}]}]}"}}]})";
        auto parsed = OpenAIProvider::parseResponse (canned, 200, "gpt-5");
        CHECK (parsed.valid);
        CHECK (parsed.interpretation.find ("Bright snare") != std::string::npos);
        CHECK (parsed.recommendations.size() == 2);
        CHECK (parsed.recommendations[1].changes.empty()); // capture gain carries no plugin changes
        RecommendationResult aiOnly; aiOnly.valid = true; aiOnly.items = parsed.recommendations;
        auto validated = SafetyValidator::validate (aiOnly, true);
        CHECK (validated.items.size() == 2);
        CHECK (validated.items[0].changes.size() == 2);                       // "role" dropped
        CHECK (std::abs (validated.items[0].changes[1].value - (-6.0f)) < 1e-4f); // -30 dB clamped to the AI limit
        CHECK (! validated.items[0].safeToAutoApply);

        CHECK (! OpenAIProvider::parseResponse (R"({"error":{"message":"Incorrect API key"}})", 401, "gpt-5").valid);
        CHECK (OpenAIProvider::parseResponse (R"({"error":{"message":"Incorrect API key"}})", 401, "gpt-5").error.find ("401") != std::string::npos);
        CHECK (! OpenAIProvider::parseResponse (R"({"choices":[{"finish_reason":"length","message":{"content":"{"}}]})", 200, "gpt-5").valid);
        CHECK (! OpenAIProvider::parseResponse (R"({"choices":[{"finish_reason":"stop","message":{"refusal":"no","content":null}}]})", 200, "gpt-5").valid);
        CHECK (! OpenAIProvider::parseResponse ("garbage", 200, "gpt-5").valid);
    }

    std::printf ("%s: %d failures\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
