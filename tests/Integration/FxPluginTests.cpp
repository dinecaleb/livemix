// Plugin-level integration tests: run the real FxProcessor headlessly.
// State save/restore, malformed state, latency/tail, bypass, sample rates,
// allocation-free processing, host tempo sync, presets, Live Safe, editor lifecycle.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "FxProcessor.h"
#include "FxEditor.h"
#include "FxPresets.h"
#include "State/ParameterIDs.h"
#include "AllocationTracker.h"
#include <cstdio>
#include <cmath>

using namespace livemix;

static int failures = 0;
#define CHECK(expr) do { if (! (expr)) { ++failures; std::printf ("    %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); } } while (0)

namespace
{
    struct TestPlayHead : public juce::AudioPlayHead
    {
        double bpm = 120.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setIsPlaying (true);
            return p;
        }
    };

    void fillTone (juce::AudioBuffer<float>& b, double sr, long long& pos)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * float (pos + i) / float (sr));
            for (int ch = 0; ch < b.getNumChannels(); ++ch) b.setSample (ch, i, v);
        }
        pos += b.getNumSamples();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- State round trip ----
    {
        FxProcessor a;
        a.prepareToPlay (48000.0, 128);
        a.setTypeFromUI (FxType::PingPongDelay);
        a.setProfileFromUI (StyleProfileId::ModernWorship);
        a.getBridge().setParameterValue (FxParamID::rvDecay, 3.3f);
        a.getBridge().setParameterValue (FxParamID::length, 80.0f);
        a.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        a.setAdvancedViewOpen (true);

        juce::MemoryBlock state;
        a.getStateInformation (state);
        CHECK (state.getSize() > 0);

        FxProcessor b;
        b.prepareToPlay (48000.0, 128);
        b.setStateInformation (state.getData(), int (state.getSize()));
        CHECK (b.getBridge().readType() == FxType::PingPongDelay);
        CHECK (b.getBridge().readStyle() == StyleProfileId::ModernWorship);
        CHECK (std::abs (b.getBridge().getParameterValue (FxParamID::rvDecay) - 3.3f) < 1e-3f);
        CHECK (std::abs (b.getBridge().getParameterValue (FxParamID::length) - 80.0f) < 1e-3f);
        CHECK (b.isLiveSafe());
        CHECK (b.isAdvancedViewOpen());

        FxParameters pa = a.getBridge().read(), pb = b.getBridge().read();
        int mismatches = 0;
        std::vector<float> va, vb;
        forEachFxParameter (pa, [&] (const std::string&, auto& v) { va.push_back (float (v)); });
        forEachFxParameter (pb, [&] (const std::string&, auto& v) { vb.push_back (float (v)); });
        for (size_t i = 0; i < va.size(); ++i) if (std::abs (va[i] - vb[i]) > 1e-4f) ++mismatches;
        CHECK (mismatches == 0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150); // no macro re-application after restore
        CHECK (std::abs (b.getBridge().getParameterValue (FxParamID::rvDecay) - 3.3f) < 1e-3f);
    }

    // ---- Malformed state ----
    {
        FxProcessor p;
        p.prepareToPlay (44100.0, 64);
        const float before = p.getBridge().getParameterValue (FxParamID::rvDecay);
        const char garbage[] = "this is not a valid state blob at all";
        p.setStateInformation (garbage, int (sizeof (garbage)));
        p.setStateInformation (nullptr, 0);
        juce::ValueTree wrong ("SomethingElse");
        juce::MemoryBlock mb; { juce::MemoryOutputStream os (mb, false); wrong.writeToStream (os); }
        p.setStateInformation (mb.getData(), int (mb.getSize()));
        CHECK (std::abs (p.getBridge().getParameterValue (FxParamID::rvDecay) - before) < 1e-6f);
    }

    // ---- Latency, tail, processing across sample rates / block sizes, no allocation ----
    {
        FxProcessor p;
        p.setTypeFromUI (FxType::VocalThrow);
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 256 })
            {
                p.prepareToPlay (sr, block);
                CHECK (p.getLatencySamples() == 0);
                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                long long pos = 0;
                for (int n = 0; n < 40; ++n)
                {
                    fillTone (buf, sr, pos);
                    p.processBlock (buf, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < block; ++i)
                            if (! std::isfinite (buf.getSample (ch, i))) { ++failures; std::printf ("non-finite output at sr %f block %d\n", sr, block); n = 40; break; }
                }
                CHECK (p.getTailLengthSeconds() > 1.0);
            }
        p.prepareToPlay (48000.0, 64);
        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        long long pos = 0;
        for (int n = 0; n < 200; ++n) { fillTone (buf, 48000.0, pos); p.processBlock (buf, midi); }
        {
            alloctrack::Scope scope;
            for (int n = 0; n < 2000; ++n) { fillTone (buf, 48000.0, pos); p.processBlock (buf, midi); }
            CHECK (alloctrack::getCount() == 0);
        }
        // Host bypass keeps audio untouched
        juce::AudioBuffer<float> raw (2, 64);
        long long p2 = 0; fillTone (raw, 48000.0, p2);
        juce::AudioBuffer<float> copy (raw);
        p.processBlockBypassed (copy, midi);
        bool same = true;
        for (int i = 0; i < 64; ++i) if (std::abs (copy.getSample (0, i) - raw.getSample (0, i)) > 1e-7f) same = false;
        CHECK (same);
    }

    // ---- Host tempo drives synced delays ----
    {
        FxProcessor p;
        TestPlayHead head;
        p.setPlayHead (&head);
        p.setTypeFromUI (FxType::QuarterDelay);
        p.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> buf (2, 128);
        juce::MidiBuffer midi;
        long long pos = 0;
        head.bpm = 100.0;
        for (int n = 0; n < 20; ++n) { fillTone (buf, 48000.0, pos); p.processBlock (buf, midi); }
        CHECK (std::abs (p.getTempo() - 100.0) < 1e-6);
        CHECK (p.hasHostTempo());
        CHECK (std::abs (p.getChain().getDelay().getTimeMs (0) - 600.0f) < 0.01f);
        head.bpm = 140.0;
        for (int n = 0; n < 5; ++n) { fillTone (buf, 48000.0, pos); p.processBlock (buf, midi); }
        CHECK (std::abs (p.getChain().getDelay().getTimeMs (0) - 60000.0f / 140.0f) < 0.01f);
        p.setPlayHead (nullptr);
    }

    // ---- Type / profile / macros / presets / Live Safe ----
    {
        FxProcessor p;
        p.prepareToPlay (48000.0, 128);
        CHECK (p.getBridge().readType() == FxType::VocalPlate);
        CHECK (p.getBridge().read().reverbEnabled && ! p.getBridge().read().delayEnabled);
        p.setTypeFromUI (FxType::DottedEighthDelay);
        CHECK (p.getBridge().read().delayEnabled && ! p.getBridge().read().reverbEnabled);
        CHECK (p.getBridge().read().delayDivision == int (NoteDivision::DottedEighth));
        CHECK (p.getCurrentPresetName() == "Modern Gospel / Dotted 1/8 Delay");

        // Macro moves rewrite only their parameters (applied on the timer)
        const float userRate = 2.5f;
        p.getBridge().setParameterValue (FxParamID::rvModRate, userRate);          // not macro-affected
        const float fbBefore = p.getBridge().read().delayFeedback;
        p.getBridge().setParameterValue (FxParamID::length, 90.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        CHECK (p.getBridge().read().delayFeedback > fbBefore);
        CHECK (std::abs (p.getBridge().read().reverbModRateHz - userRate) < 1e-4f);

        // Use preset
        const auto use = FxPresets::getUsePresets();
        CHECK (use.size() == 6);
        CHECK (p.loadPreset (use[3]));
        CHECK (p.getBridge().readType() == FxType::LargeAmbient);
        CHECK (p.getBridge().readStyle() == StyleProfileId::ModernWorship);
        CHECK (p.getCurrentPresetName() == "Large Worship Ambient");
        CHECK (std::abs (p.getBridge().getParameterValue (FxParamID::space) - 70.0f) < 1e-3f);

        // Factory presets
        const auto factory = FxPresets::getFactoryPresets();
        CHECK (factory.size() == size_t (int (FxType::Count) * int (StyleProfileId::Count)));
        CHECK (p.loadPreset (factory[0]));
        CHECK (p.getBridge().readType() == FxType::VocalPlate && p.getBridge().readStyle() == StyleProfileId::ModernGospel);

        // User preset round trip
        p.getBridge().setParameterValue (FxParamID::rvDecay, 4.4f);
        FxPresets::Info saved;
        const juce::String name = "LiveMix FX Test Preset " + juce::String (juce::Random::getSystemRandom().nextInt (100000));
        CHECK (FxPresets::saveUserPreset (name, p.createPresetTree (name), &saved));
        p.setTypeFromUI (FxType::Room);
        CHECK (p.loadPreset (saved));
        CHECK (p.getBridge().readType() == FxType::VocalPlate);
        CHECK (std::abs (p.getBridge().getParameterValue (FxParamID::rvDecay) - 4.4f) < 1e-3f);
        CHECK (FxPresets::deleteUserPreset (saved));

        // Live Safe locks type, profile and presets
        p.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
        p.setTypeFromUI (FxType::SlapDelay);
        CHECK (p.getBridge().readType() == FxType::VocalPlate);
        p.setProfileFromUI (StyleProfileId::ModernWorship);
        CHECK (p.getBridge().readStyle() == StyleProfileId::ModernGospel);
        CHECK (! p.loadPreset (use[0]));
        p.getBridge().setParameterValue (ParamID::liveSafe, 0.0f);
    }

    // ---- Editor lifecycle ----
    {
        FxProcessor p;
        p.prepareToPlay (48000.0, 128);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr);
        ed->setSize (900, 600);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        if (auto* fxEd = dynamic_cast<FxEditor*> (ed.get()))
        {
            fxEd->setView (SubBar::View::Advanced);
            for (int m = 0; m < int (FxModule::Count); ++m) { fxEd->getAdvancedPanel().setModule (FxModule (m)); juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
            CHECK (p.isAdvancedViewOpen());
        }
        ed.reset();
    }

    std::printf ("%s (%d failures)\n", failures == 0 ? "FX PLUGIN TESTS PASSED" : "FX PLUGIN TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
