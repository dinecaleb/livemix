// Headless UI snapshots for Dine FX: builds the real FxProcessor + FxEditor
// without a native window, feeds a synthetic vocal phrase, walks every
// view/type and writes PNGs. Usage: livemix_fx_ui_snapshots <output-dir>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "FxProcessor.h"
#include "FxEditor.h"
#include "State/ParameterIDs.h"
#include <cstdio>
#include <cmath>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 512;

    struct TestPlayHead : public juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm (128.0); p.setIsPlaying (true); return p; }
    };

    struct Rig
    {
        FxProcessor proc;
        TestPlayHead head;
        std::unique_ptr<FxEditor> editor;
        juce::AudioBuffer<float> buffer { 2, kBlock };
        juce::MidiBuffer midi;
        long long samplePos = 0;
        float levelDb = -14.0f;
        bool silent = false;

        Rig()
        {
            proc.setPlayHead (&head);
            proc.prepareToPlay (kSr, kBlock);
            editor = std::make_unique<FxEditor> (proc);
            editor->setSize (900, 600);
            editor->setVisible (true);
        }
        ~Rig() { editor.reset(); proc.setPlayHead (nullptr); }

        void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

        // A sung phrase: vibrato tone in 700 ms bursts with 500 ms gaps.
        void feed (double seconds)
        {
            const int blocks = int (seconds * kSr / kBlock);
            const float amp = std::pow (10.0f, levelDb / 20.0f);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const long long pos = samplePos + i;
                    const double t = double (pos) / kSr;
                    const double phrase = std::fmod (t, 1.2);
                    const float env = phrase < 0.7 ? float (std::sin (juce::MathConstants<double>::pi * phrase / 0.7)) : 0.0f;
                    const float f = 262.0f * (1.0f + 0.006f * std::sin (2.0f * juce::MathConstants<float>::pi * 5.5f * float (t)));
                    const float v = silent ? 0.0f : amp * env * (0.7f * std::sin (2.0f * juce::MathConstants<float>::pi * f * float (t)) + 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 2.0f * f * float (t)));
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }
                proc.processBlock (buffer, midi);
                samplePos += kBlock;
                pump (8);
            }
        }

        void snap (const juce::File& dir, const juce::String& name)
        {
            pump (60);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);
            juce::PNGImageFormat png;
            juce::FileOutputStream out (dir.getChildFile (name + ".png"));
            out.setPosition (0); out.truncate();
            png.writeImageToStream (img, out);
            std::printf ("wrote %s\n", name.toRawUTF8());
        }
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("fx-ui-snapshots").getFullPathName());
    dir.createDirectory();

    Rig rig;
    auto& p = rig.proc;
    auto& ed = *rig.editor;

    rig.feed (2.0);
    rig.snap (dir, "01-simple-vocal-plate");

    ed.setView (SubBar::View::Advanced);
    for (auto m : { FxModule::Reverb, FxModule::Delay, FxModule::Input, FxModule::Output })
    {
        ed.getAdvancedPanel().setModule (m);
        rig.feed (0.6);
        rig.snap (dir, juce::String ("02-advanced-") + juce::String (fxModuleName (m)).toLowerCase());
    }

    p.setTypeFromUI (FxType::VocalThrow);
    ed.setView (SubBar::View::Simple);
    rig.feed (2.5);
    rig.snap (dir, "03-simple-vocal-throw");
    ed.setView (SubBar::View::Advanced);
    ed.getAdvancedPanel().setModule (FxModule::Delay);
    rig.feed (0.8);
    rig.snap (dir, "04-advanced-delay-throw");

    p.setTypeFromUI (FxType::WorshipHall);
    p.setProfileFromUI (StyleProfileId::ModernWorship);
    p.getBridge().setParameterValue (FxParamID::space, 75.0f);
    ed.getAdvancedPanel().setModule (FxModule::Reverb);
    rig.feed (1.0);
    rig.snap (dir, "05-advanced-reverb-worship-hall");

    ed.setView (SubBar::View::Simple);
    p.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
    rig.feed (1.0);
    rig.snap (dir, "06-live-safe");
    p.getBridge().setParameterValue (ParamID::liveSafe, 0.0f);

    p.getBridge().setParameterValue (ParamID::bypass, 1.0f);
    rig.feed (1.0);
    rig.snap (dir, "07-ab-original");
    p.getBridge().setParameterValue (ParamID::bypass, 0.0f);

    ed.setSize (1280, 820);
    rig.feed (0.6);
    rig.snap (dir, "08-simple-1280x820");
    ed.setView (SubBar::View::Advanced);
    ed.getAdvancedPanel().setModule (FxModule::Reverb);
    rig.feed (0.6);
    rig.snap (dir, "09-advanced-1280x820");
    ed.setSize (900, 600);

    rig.editor.reset();
    p.releaseResources();
    return 0;
}
