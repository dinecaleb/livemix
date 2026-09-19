// Plugin-level integration tests shared by every Dine channel product. Compiled once per
// product with LIVEMIX_PRODUCT=<Drums|Vocals|Keys|Master|Guitar|Bass>: state save/restore, latency,
// bypass, sample-rate changes, malformed / foreign state, allocation-free processing,
// Tune alongside processing, presets, hidden stages and knob defaults.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "ChannelPluginProcessor.h"
#include "State/ParameterIDs.h"
#include "State/ParameterSpecs.h"
#include "Profiles/StyleProfile.h"
#include "AllocationTracker.h"
#include <memory>
#include <vector>
#include <cstdio>
#include <cmath>
#include "DSP/Limiter.h"
#include <algorithm>

using namespace livemix;

#ifndef LIVEMIX_PRODUCT
#define LIVEMIX_PRODUCT Drums
#endif
static constexpr Product kProduct = Product::LIVEMIX_PRODUCT;

static int failures = 0;
#define CHECK(expr) do { if (! (expr)) { ++failures; std::printf ("    %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); } } while (0)

namespace
{
    // A signal that suits the product: drum hits, a sung/spoken voice, stereo keys chords or a full mix.
    void fillSignal (juce::AudioBuffer<float>& b, double sr, long long startSample = 0)
    {
        juce::Random rng (3);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const long long pos = startSample + i;
            const float t = float (pos) / float (sr);
            float l = 0.0f, r = 0.0f;
            const int since = int (pos % int (sr / 4));
            const float hitEnv = std::exp (-float (since) / float (0.08 * sr));
            const float hit = 0.5f * hitEnv * (0.6f * std::sin (2.0f * juce::MathConstants<float>::pi * 120.0f * float (since) / float (sr)) + 0.4f * (rng.nextFloat() * 2.0f - 1.0f));
            switch (kProduct)
            {
                case Product::Vocals:
                {
                    float v = 0.0f;
                    for (int h = 1; h <= 6; ++h) v += (0.3f / float (h)) * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * float (h) * t);
                    v *= 0.5f + 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 0.7f * t);
                    if (std::fmod (t, 0.6f) < 0.06f) v += 0.4f * (rng.nextFloat() * 2.0f - 1.0f);
                    l = r = v;
                    break;
                }
                case Product::Keys:
                {
                    for (float f : { 261.6f, 329.6f, 392.0f, 523.3f })
                        l += 0.12f * std::sin (2.0f * juce::MathConstants<float>::pi * f * t);
                    r = 0.8f * l + 0.05f * std::sin (2.0f * juce::MathConstants<float>::pi * 659.3f * t);
                    const float env = 0.4f + 0.6f * std::fabs (std::sin (2.0f * juce::MathConstants<float>::pi * 0.5f * t));
                    l *= env; r *= env;
                    break;
                }
                case Product::Master:
                {
                    float v = hit;
                    for (float f : { 110.0f, 220.0f, 440.0f }) v += 0.08f * std::sin (2.0f * juce::MathConstants<float>::pi * f * t);
                    l = v + 0.05f * (rng.nextFloat() * 2.0f - 1.0f); r = v + 0.05f * (rng.nextFloat() * 2.0f - 1.0f);
                    break;
                }
                case Product::Guitar:
                {
                    // Strummed chord every half second with a pick click, decaying into the next strum.
                    const int strum = int (pos % int (sr / 2));
                    const float env = std::exp (-float (strum) / float (0.35 * sr));
                    float v = 0.0f;
                    for (float f : { 82.4f, 123.5f, 164.8f, 207.7f, 246.9f, 329.6f })
                        for (int h = 1; h <= 4; ++h) v += (0.03f / float (h)) * std::sin (2.0f * juce::MathConstants<float>::pi * f * float (h) * t);
                    v *= env;
                    if (strum < int (0.004 * sr)) v += 0.15f * (rng.nextFloat() * 2.0f - 1.0f);
                    l = r = v;
                    break;
                }
                case Product::Bass:
                {
                    // A walking line: one plucked note every half second with a finger click, decaying into the next.
                    const int pluck = int (pos % int (sr / 2));
                    const float env = std::exp (-float (pluck) / float (0.3 * sr));
                    const float notes[] = { 41.2f, 55.0f, 61.7f, 73.4f };
                    const float f = notes[(pos / int (sr / 2)) % 4];
                    float v = 0.0f;
                    for (int h = 1; h <= 6; ++h) v += (0.22f / float (h)) * std::sin (2.0f * juce::MathConstants<float>::pi * f * float (h) * t);
                    v *= env;
                    if (pluck < int (0.003 * sr)) v += 0.1f * (rng.nextFloat() * 2.0f - 1.0f);
                    l = r = v;
                    break;
                }
                case Product::Drums:
                case Product::Count:
                default:
                    l = r = hit;
                    break;
            }
            if (b.getNumChannels() > 0) b.setSample (0, i, l);
            if (b.getNumChannels() > 1) b.setSample (1, i, r);
        }
    }

    // Host parameters round-trip through a normalised (often skewed) float: compare with a small tolerance.
    int mismatches (const ChannelParameters& a, const ChannelParameters& b, bool report = true)
    {
        int n = 0;
        for (const auto& c : diffParameters (a, b))
        {
            float va = 0.0f;
            ChannelParameters copy = a;
            forEachDspParameter (copy, [&] (const std::string& id, auto& v) { if (id == c.paramId) va = float (v); });
            if (std::abs (va - c.value) > 1e-3f * std::max (1.0f, std::abs (va))) { ++n; if (report) std::printf ("      mismatch %s: %g vs %g\n", c.paramId.c_str(), double (va), double (c.value)); }
        }
        return n;
    }

    bool sameAudio (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int delay = 0, float tol = 0.0f)
    {
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = delay; i < a.getNumSamples(); ++i)
                if (std::abs (a.getSample (ch, i) - b.getSample (ch, i - delay)) > tol) return false;
        return true;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& def = productDefinition (kProduct);
    std::printf ("%s plugin tests\n", def.name);
    const ChannelRole secondRole = def.roles.size() > 1 ? def.roles[1] : def.roles[0];
    const char* macroId = def.macros[1].id;

    // ---- Product identity and parameter table ----
    {
        ChannelPluginProcessor p (kProduct);
        CHECK (p.getName() == juce::String (def.name));
        CHECK (p.getBridge().readRole() == def.defaultRole);
        for (const auto& s : channelParameterSpecs (kProduct)) CHECK (p.getState().getParameter (s.id) != nullptr);
        // Hidden stages have no host parameter at all.
        ChannelParameters d;
        forEachDspParameter (d, [&] (const std::string& id, auto&)
        {
            const bool exists = p.getState().getParameter (id) != nullptr;
            CHECK (exists == productUsesParameter (kProduct, id));
        });
        // The fresh instance sits exactly on the baseline (every knob at its default).
        const auto base = StyleProfile::baseline (def.defaultRole, p.getBridge().readStyle());
        CHECK (mismatches (base, p.getBridge().read()) == 0);
        for (const auto& m : def.macros) CHECK (std::abs (p.getBridge().getParameterValue (m.id) - m.defaultValue) < 1e-4f);
    }

    // ---- State round trip ----
    {
        ChannelPluginProcessor a (kProduct);
        a.prepareToPlay (48000.0, 128);
        a.setRoleFromUI (secondRole);
        a.setProfileFromUI (StyleProfileId::ModernWorship);
        a.getBridge().setParameterValue (ParamID::compThreshold, -27.0f);
        a.getBridge().setParameterValue (macroId, 80.0f);
        a.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        a.getBridge().setParameterValue (eqBandId ("toneEq", 2, "Gain"), 5.5f);
        a.setGroupName ("Group B");
        a.setAdvancedViewOpen (true);

        juce::MemoryBlock state;
        a.getStateInformation (state);
        CHECK (state.getSize() > 0);

        ChannelPluginProcessor b (kProduct);
        b.prepareToPlay (48000.0, 128);
        b.setStateInformation (state.getData(), int (state.getSize()));
        CHECK (b.getBridge().readRole() == secondRole);
        CHECK (b.getBridge().readStyle() == StyleProfileId::ModernWorship);
        CHECK (std::abs (b.getBridge().getParameterValue (ParamID::compThreshold) - (-27.0f)) < 1e-3f);
        CHECK (std::abs (b.getBridge().getParameterValue (macroId) - 80.0f) < 1e-3f);
        CHECK (std::abs (b.getBridge().getParameterValue (eqBandId ("toneEq", 2, "Gain")) - 5.5f) < 1e-3f);
        CHECK (b.isLiveSafe());
        CHECK (b.getGroupName() == "Group B");
        CHECK (b.isAdvancedViewOpen());
        CHECK (mismatches (a.getBridge().read(), b.getBridge().read()) == 0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        CHECK (std::abs (b.getBridge().getParameterValue (ParamID::compThreshold) - (-27.0f)) < 1e-3f);

        // Another product's state (or garbage) is ignored without changing anything.
        ChannelPluginProcessor c (kProduct);
        c.prepareToPlay (44100.0, 64);
        const float before = c.getBridge().getParameterValue (ParamID::compThreshold);
        const char garbage[] = "this is not a valid state blob at all";
        c.setStateInformation (garbage, int (sizeof (garbage)));
        c.setStateInformation (nullptr, 0);
        juce::ValueTree wrong ("LiveMixSomethingElseState");
        juce::MemoryBlock mb; { juce::MemoryOutputStream os (mb, false); wrong.writeToStream (os); }
        c.setStateInformation (mb.getData(), int (mb.getSize()));
        CHECK (std::abs (c.getBridge().getParameterValue (ParamID::compThreshold) - before) < 1e-6f);
    }

    // ---- Latency, bypass, processing, sample-rate changes ----
    {
        ChannelPluginProcessor p (kProduct);
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int block : { 32, 64, 128, 256 })
            {
                p.prepareToPlay (sr, block);
                // What the host is told is exactly the limiter's lookahead on the product that has the stage, 0 elsewhere.
                const int expected = def.hasLoudness ? std::max (1, int (std::lround (Limiter::kLookaheadMs * 0.001 * sr))) : 0;
                CHECK (p.getLatencySamples() == expected);
                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                for (int n = 0; n < 50; ++n)
                {
                    fillSignal (buf, sr, n * block);
                    p.processBlock (buf, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < block; ++i)
                            if (! std::isfinite (buf.getSample (ch, i))) { ++failures; std::printf ("non-finite output at sr %f block %d\n", sr, block); n = 50; break; }
                }
            }
        }

        // A/B ORIGINAL: output equals input (delayed by the limiter lookahead on Dine Master); the latency never changes.
        p.prepareToPlay (48000.0, 128);
        const int L = p.getLatencySamples();
        p.getBridge().setParameterValue (ParamID::bypass, 1.0f);
        p.getBridge().setParameterValue (ParamID::abMatch, 0.0f);
        juce::AudioBuffer<float> buf (2, 128), ref (2, 128);
        juce::MidiBuffer midi;
        fillSignal (buf, 48000.0); p.processBlock (buf, midi); // fill the delay line
        fillSignal (buf, 48000.0, 128);
        ref.makeCopyOf (buf);
        p.processBlock (buf, midi);
        if (L == 0) CHECK (sameAudio (buf, ref));
        else
        {
            juce::AudioBuffer<float> prev (2, 128); fillSignal (prev, 48000.0, 0);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = L; i < 128; ++i)
                    if (std::abs (buf.getSample (ch, i) - ref.getSample (ch, i - L)) > 1e-6f) { ++failures; std::printf ("bypass delay mismatch at %d\n", i); break; }
        }
        CHECK (p.getLatencySamples() == L);
        p.getBridge().setParameterValue (ParamID::bypass, 0.0f);

        // Host bypass path passes audio untouched.
        fillSignal (buf, 48000.0); ref.makeCopyOf (buf);
        p.processBlockBypassed (buf, midi);
        CHECK (sameAudio (buf, ref));
    }

    // ---- Steady-state processBlock never allocates ----
    {
        ChannelPluginProcessor p (kProduct);
        p.prepareToPlay (48000.0, 64);
        p.setRoleFromUI (secondRole);
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        fillSignal (buf, 48000.0);
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

    // ---- Tune runs alongside processing, proposes only this product's parameters; Live Safe blocks it ----
    {
        ChannelPluginProcessor p (kProduct);
        p.prepareToPlay (48000.0, 64);
        p.startAnalyze();
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const int blocks = int (48000.0 * 13.0 / 64.0);
        int spikes = 0; // isolated scheduler hiccups on a busy machine are not a real-time violation; a pattern is
        for (int n = 0; n < blocks; ++n)
        {
            fillSignal (buf, 48000.0, n * 64);
            const auto s = juce::Time::getHighResolutionTicks();
            p.processBlock (buf, midi);
            const double us = 1.0e6 * double (juce::Time::getHighResolutionTicks() - s) / double (juce::Time::getHighResolutionTicksPerSecond());
            if (us > 1000.0 && ++spikes > 3) { ++failures; std::printf ("processBlock took %.0f us while analyzing (%d spikes)\n", us, spikes); break; }
            if (n % 200 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        if (spikes > 0) std::printf ("    note: %d processBlock spike(s) over 1 ms while analyzing (tolerated up to 3)\n", spikes);
        for (int i = 0; i < 400 && p.getAnalysis().getState() != AnalysisEngine::State::Complete; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        CHECK (p.getAnalysis().getState() == AnalysisEngine::State::Complete);
        const auto rec = p.getRecommendations();
        CHECK (rec.valid);
        for (const auto& item : rec.items)
            for (const auto& c : item.changes)
                if (! productUsesParameter (kProduct, c.paramId)) { ++failures; std::printf ("Tune proposed a hidden parameter: %s\n", c.paramId.c_str()); }
        const auto tune = p.getTuneResult();
        std::printf ("    tune wall time %.1f s, input health: %s, %d items, %d parameters changed: %s\n",
                     (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, rec.inputHealth.c_str(), int (rec.items.size()), tune.parametersChanged, tune.headline.c_str());
        // BEFORE / AFTER / KEEP / REVERT are consistent.
        if (p.isTunePreviewActive())
        {
            p.setTuneCompare (false);
            CHECK (mismatches (p.getBridge().read(), tune.before) == 0);
            p.setTuneCompare (true);
            CHECK (mismatches (p.getBridge().read(), tune.proposed) == 0);
            p.revertTune();
            CHECK (mismatches (p.getBridge().read(), tune.before) == 0);
        }

        p.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        CHECK (! p.isAnalyzeAvailable());
        p.startAnalyze();
        CHECK (! p.getAnalysis().isCapturing());
    }

    // ---- Presets: factory load, user save/load/delete round trip ----
    {
        ChannelPluginProcessor p (kProduct);
        p.prepareToPlay (48000.0, 128);
        PresetManager::Info factory;
        for (auto& f : PresetManager::getFactoryPresets (def))
            if (f.role == secondRole && f.style == StyleProfileId::ModernWorship) factory = f;
        CHECK (p.loadPreset (factory));
        CHECK (p.getBridge().readRole() == secondRole);
        CHECK (p.getBridge().readStyle() == StyleProfileId::ModernWorship);
        CHECK (mismatches (StyleProfile::baseline (secondRole, StyleProfileId::ModernWorship), p.getBridge().read()) == 0);

        p.getBridge().setParameterValue (ParamID::compRatio, 7.5f);
        p.getBridge().setParameterValue (macroId, 70.0f);
        const juce::String name = juce::String (def.presetFolder) + "Test_" + juce::String (juce::Time::currentTimeMillis());
        PresetManager::Info saved;
        CHECK (PresetManager::saveUserPreset (def, name, p.createPresetTree (name), &saved));
        CHECK (saved.file.existsAsFile());

        ChannelPluginProcessor q (kProduct);
        q.prepareToPlay (48000.0, 128);
        CHECK (q.loadPreset (saved));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120); // the knob timer must not re-derive parameters
        CHECK (std::abs (q.getBridge().getParameterValue (ParamID::compRatio) - 7.5f) < 1e-3f);
        CHECK (std::abs (q.getBridge().getParameterValue (macroId) - 70.0f) < 1e-3f);
        CHECK (q.getBridge().readRole() == secondRole);
        CHECK (q.getCurrentPresetName() == name);
        bool listed = false;
        for (auto& u : PresetManager::getUserPresets (def)) if (u.name == name) listed = true;
        CHECK (listed);
        CHECK (PresetManager::deleteUserPreset (saved));
        CHECK (! saved.file.existsAsFile());

        q.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        CHECK (! q.loadPreset (factory));
    }

    // ---- Knobs: moving one and back restores the baseline (idempotent at 50) ----
    {
        ChannelPluginProcessor p (kProduct);
        p.prepareToPlay (48000.0, 128);
        const auto base = p.getBridge().read();
        p.getBridge().setParameterValue (macroId, 90.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        CHECK (mismatches (base, p.getBridge().read(), false) > 0);
        p.getBridge().setParameterValue (macroId, def.macros[1].defaultValue);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        CHECK (mismatches (base, p.getBridge().read()) == 0);
    }

    // ---- Editor lifecycle ----
    {
        ChannelPluginProcessor p (kProduct);
        p.prepareToPlay (48000.0, 128);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);
        editor->setSize (900, 650);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        editor.reset();
    }

    // ---- 32 instances: steady-state cost at 64 samples ----
    {
        std::vector<std::unique_ptr<ChannelPluginProcessor>> many;
        for (int i = 0; i < 32; ++i)
        {
            auto p = std::make_unique<ChannelPluginProcessor> (kProduct);
            p->prepareToPlay (48000.0, 64);
            p->setRoleFromUI (def.roles[size_t (i) % def.roles.size()]);
            many.push_back (std::move (p));
        }
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        fillSignal (buf, 48000.0);
        const int blocks = int (48000.0 * 3.0 / 64.0);
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int n = 0; n < blocks; ++n)
            for (auto& p : many) p->processBlock (buf, midi);
        const double usPerBlockAll = 1.0e6 * double (juce::Time::getHighResolutionTicks() - t0) / double (juce::Time::getHighResolutionTicksPerSecond()) / blocks;
        int overruns = 0;
        for (auto& p : many) overruns += p->getDiagnostics().overBudgetBlocks;
        std::printf ("    32 instances @ 64 smp: %.0f us per block for all (%.1f%% of budget), %d over-budget blocks\n",
                     usPerBlockAll, 100.0 * usPerBlockAll / 1333.3, overruns);
        CHECK (usPerBlockAll < 1333.0 * 0.6);
        CHECK (overruns == 0);
    }

    std::printf ("%s: %d failures\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
