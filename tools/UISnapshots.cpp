// Headless UI snapshots: builds the real DrumsProcessor + DrumsEditor without a
// native window, feeds synthetic drum audio, walks every view/state and writes
// PNGs. Usage: livemix_ui_snapshots <output-dir> [scenario...]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DrumsProcessor.h"
#include "DrumsEditor.h"
#include "State/ParameterIDs.h"
#include <cstdio>
#include <cmath>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 512;

    struct Rig
    {
        DrumsProcessor proc;
        std::unique_ptr<DrumsEditor> editor;
        juce::AudioBuffer<float> buffer { 2, kBlock };
        juce::MidiBuffer midi;
        juce::Random rng { 7 };
        long long samplePos = 0;
        float levelDb = -12.0f;   // target peak of the synthetic hits
        float floorDb = -66.0f;   // between-hit floor (bleed)
        float boxyGain = 0.0f;    // extra 420 Hz component: low-mid mud for Tune to find
        bool silent = false;

        Rig()
        {
            proc.prepareToPlay (kSr, kBlock);
            editor = std::make_unique<DrumsEditor> (proc);
            editor->setSize (900, 650);
            editor->setVisible (true);
        }

        void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

        // Feeds `seconds` of audio while pumping the message loop so timers/UI update.
        void feed (double seconds)
        {
            const int blocks = int (seconds * kSr / kBlock);
            const float amp = std::pow (10.0f, levelDb / 20.0f);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const long long pos = samplePos + i;
                    const int since = int (pos % int (kSr / 2));           // a hit every 500 ms
                    const bool ghost = ((pos / int (kSr / 2)) % 4) == 3;    // every 4th hit is a ghost note
                    const float env = std::exp (-float (since) / float (0.06 * kSr));
                    const float tone = 0.7f * std::sin (2.0f * juce::MathConstants<float>::pi * 180.0f * float (since) / float (kSr))
                                     + boxyGain * std::sin (2.0f * juce::MathConstants<float>::pi * 420.0f * float (since) / float (kSr))
                                     + 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                    const float floorAmp = std::pow (10.0f, floorDb / 20.0f);
                    const float v = silent ? 0.0f : amp * (ghost ? 0.25f : 1.0f) * env * tone + (silent ? 0.0f : floorAmp * (rng.nextFloat() * 2.0f - 1.0f));
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
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots").getFullPathName());
    dir.createDirectory();

    Rig rig;
    auto& p = rig.proc;
    auto& ed = *rig.editor;

    p.setRoleFromUI (ChannelRole::SnareTop);
    p.setProfileFromUI (StyleProfileId::ModernGospel);
    rig.feed (1.5);
    rig.snap (dir, "01-simple-snare");

    ed.setView (SubBar::View::Advanced);
    for (auto m : { ChainModule::EQ, ChainModule::Gate, ChainModule::Comp, ChainModule::Input, ChainModule::Transient, ChainModule::Output })
    {
        ed.getAdvancedPanel().setModule (m);
        rig.feed (0.6);
        rig.snap (dir, juce::String ("02-advanced-") + juce::String (chainModuleName (m)).toLowerCase());
    }

    // Kit: two more instances in the same group (the registry is process-wide)
    DrumsProcessor kick, ohl;
    kick.prepareToPlay (kSr, kBlock);  kick.setRoleFromUI (ChannelRole::KickIn);
    ohl.prepareToPlay (kSr, kBlock);   ohl.setRoleFromUI (ChannelRole::OverheadLeft);
    juce::AudioBuffer<float> kb (2, kBlock), ob (2, kBlock);
    juce::MidiBuffer km;
    auto feedOthers = [&] (int blocks, float kickDb, float ohDb)
    {
        juce::Random r2 (11);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const long long pos = rig.samplePos + i;
                const int since = int (pos % int (kSr / 2));
                const float env = std::exp (-float (since) / float (0.09 * kSr));
                kb.setSample (0, i, std::pow (10.0f, kickDb / 20.0f) * env * std::sin (2.0f * juce::MathConstants<float>::pi * 60.0f * float (since) / float (kSr)));
                kb.setSample (1, i, kb.getSample (0, i));
                ob.setSample (0, i, std::pow (10.0f, ohDb / 20.0f) * (0.4f + 0.6f * env) * (r2.nextFloat() * 2.0f - 1.0f));
                ob.setSample (1, i, ob.getSample (0, i) * 0.8f);
            }
            kick.processBlock (kb, km);
            ohl.processBlock (ob, km);
        }
    };
    ed.setView (SubBar::View::Kit);
    rig.feed (0.6);
    rig.snap (dir, "03-kit");
    p.startKitAnalyze();
    for (int s = 0; s < 14; ++s) { feedOthers (int (kSr / kBlock), -30.0f, -4.0f); rig.feed (1.0); }
    rig.pump (500);
    rig.feed (0.5);
    rig.snap (dir, "03b-kit-results");

    // Tune workflow: waiting for signal -> listening -> tuned (BEFORE/AFTER, KEEP, REVIEW, REVERT)
    // The kit tune above already tuned this channel; go back to the template and make the snare
    // boxy with real bleed so Tune has decisions to make.
    p.reloadPreset();
    rig.boxyGain = 0.9f;
    rig.floorDb = -34.0f;
    ed.setView (SubBar::View::Simple);
    rig.silent = true;
    rig.feed (6.0);
    ed.requestAnalyze();
    rig.feed (0.4);
    rig.snap (dir, "04-tune-waiting");
    rig.silent = false;
    rig.feed (5.0);
    rig.snap (dir, "05-tune-listening");
    rig.feed (8.0);
    rig.pump (400);
    rig.snap (dir, "06-tune-result");
    ed.compareTune (false);
    rig.feed (0.3);
    rig.snap (dir, "06a-tune-before");
    ed.compareTune (true);
    ed.keepTune();
    rig.feed (0.3);
    rig.snap (dir, "06b-kept-toast");
    ed.setView (SubBar::View::Simple);
    rig.feed (0.2);
    ed.showLastAnalysis();
    rig.feed (0.2);
    rig.snap (dir, "06c-reopened-result");
    ed.dismissAnalyzeOverlay();

    // REVIEW -> Advanced with every Tune decision listed (UNDO / DISMISS)
    ed.reviewAnalysisDetails();
    rig.feed (0.4);
    rig.snap (dir, "07-advanced-suggestion");
    ed.getAdvancedPanel().setModule (ChainModule::EQ);
    rig.feed (0.4);
    rig.snap (dir, "07b-advanced-eq-flag");

    // Input low / clipping / live safe
    ed.setView (SubBar::View::Simple);
    rig.levelDb = -32.0f;
    rig.feed (3.5);
    rig.snap (dir, "08-input-low");
    rig.levelDb = 0.0f;
    rig.feed (2.5);
    rig.snap (dir, "09-clipping");
    rig.levelDb = -12.0f;
    p.getBridge().setParameterValue (ParamID::liveSafe, 1.0f);
    rig.feed (2.5);
    rig.snap (dir, "10-live-safe");
    p.getBridge().setParameterValue (ParamID::liveSafe, 0.0f);
    rig.feed (0.5);

    // Larger window: the shell must flex without breaking the layout
    ed.setView (SubBar::View::Simple);
    ed.setSize (1280, 860);
    rig.feed (0.6);
    rig.snap (dir, "11-simple-1280x860");
    ed.setView (SubBar::View::Advanced);
    ed.getAdvancedPanel().setModule (ChainModule::EQ);
    rig.feed (0.6);
    rig.snap (dir, "12-advanced-1280x860");
    ed.setSize (900, 650);

    rig.editor.reset();
    kick.releaseResources();
    ohl.releaseResources();
    p.releaseResources();
    return 0;
}
