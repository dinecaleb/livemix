// Headless UI snapshots for one Dine channel product (compiled per product with
// LIVEMIX_PRODUCT=<Drums|Vocals|Keys|Master|Guitar|Bass>): builds the real processor + editor
// without a native window, feeds a product-appropriate synthetic signal, walks every
// view / stage / Tune state and writes PNGs. Usage: <tool> <output-dir>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChannelPluginProcessor.h"
#include "ChannelPluginEditor.h"
#include "State/ParameterIDs.h"
#include <cstdio>
#include <cmath>

using namespace livemix;

#ifndef LIVEMIX_PRODUCT
#define LIVEMIX_PRODUCT Vocals
#endif
static constexpr Product kProduct = Product::LIVEMIX_PRODUCT;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 512;

    struct Rig
    {
        ChannelPluginProcessor proc { kProduct };
        std::unique_ptr<ChannelPluginEditor> editor;
        juce::AudioBuffer<float> buffer { 2, kBlock };
        juce::MidiBuffer midi;
        juce::Random rng { 7 };
        long long samplePos = 0;
        float levelDb = -12.0f;   // target peak
        float floorDb = -66.0f;   // between-phrase floor (bleed / room)
        float sharpness = 0.0f;   // extra S / harshness content for Tune to find
        bool silent = false;

        Rig()
        {
            proc.prepareToPlay (kSr, kBlock);
            editor = std::make_unique<ChannelPluginEditor> (proc);
            editor->setSize (900, 650);
            editor->setVisible (true);
        }

        void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

        float sample (long long pos, int channel)
        {
            const float t = float (pos) / float (kSr);
            const float amp = std::pow (10.0f, levelDb / 20.0f);
            const float floorAmp = std::pow (10.0f, floorDb / 20.0f);
            float v = 0.0f;
            switch (kProduct)
            {
                case Product::Vocals:
                {
                    for (int h = 1; h <= 6; ++h) v += (0.35f / float (h)) * std::sin (2.0f * juce::MathConstants<float>::pi * 196.0f * float (h) * t);
                    v *= 0.55f + 0.45f * std::sin (2.0f * juce::MathConstants<float>::pi * 0.6f * t);   // phrasing
                    if (std::fmod (t, 0.7f) < 0.07f) v += (0.25f + 0.6f * sharpness) * (rng.nextFloat() * 2.0f - 1.0f); // S
                    break;
                }
                case Product::Keys:
                {
                    const float chord[] = { 261.6f, 329.6f, 392.0f, 523.3f };
                    for (float f : chord) v += 0.2f * std::sin (2.0f * juce::MathConstants<float>::pi * f * t + (channel == 1 ? 0.6f : 0.0f));
                    v += sharpness * 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 300.0f * t);
                    v *= 0.35f + 0.65f * std::fabs (std::sin (2.0f * juce::MathConstants<float>::pi * 0.4f * t));
                    if (channel == 1) v *= 0.85f;
                    break;
                }
                case Product::Master:
                {
                    const int since = int (pos % int (kSr / 2));
                    const float env = std::exp (-float (since) / float (0.06 * kSr));
                    v = 0.6f * env * (0.7f * std::sin (2.0f * juce::MathConstants<float>::pi * 80.0f * float (since) / float (kSr)) + 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
                    for (float f : { 110.0f, 220.0f, 330.0f, 440.0f }) v += 0.1f * std::sin (2.0f * juce::MathConstants<float>::pi * f * t + (channel == 1 ? 0.3f : 0.0f));
                    v += sharpness * 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 3200.0f * t);
                    break;
                }
                case Product::Guitar:
                {
                    const int strum = int (pos % int (kSr / 2));
                    const float env = std::exp (-float (strum) / float (0.35 * kSr));
                    for (float f : { 82.4f, 123.5f, 164.8f, 207.7f, 246.9f, 329.6f })
                        for (int h = 1; h <= 4; ++h) v += (0.08f / float (h)) * std::sin (2.0f * juce::MathConstants<float>::pi * f * float (h) * t + (channel == 1 ? 0.2f : 0.0f));
                    v += sharpness * 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 3100.0f * t);
                    v *= env;
                    if (strum < int (0.004 * kSr)) v += 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                    break;
                }
                case Product::Bass:
                {
                    const int pluck = int (pos % int (kSr / 2));
                    const float env = std::exp (-float (pluck) / float (0.3 * kSr));
                    const float notes[] = { 41.2f, 55.0f, 61.7f, 73.4f };
                    const float f = notes[(pos / int (kSr / 2)) % 4];
                    for (int h = 1; h <= 6; ++h) v += (0.3f / float (h)) * std::sin (2.0f * juce::MathConstants<float>::pi * f * float (h) * t);
                    v += sharpness * 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 2600.0f * t);
                    v *= env;
                    if (pluck < int (0.003 * kSr)) v += 0.1f * (rng.nextFloat() * 2.0f - 1.0f);
                    break;
                }
                case Product::Drums:
                case Product::Count:
                default:
                {
                    const int since = int (pos % int (kSr / 2));
                    const float env = std::exp (-float (since) / float (0.06 * kSr));
                    v = env * (0.7f * std::sin (2.0f * juce::MathConstants<float>::pi * 180.0f * float (since) / float (kSr)) + 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
                    break;
                }
            }
            return silent ? 0.0f : amp * v + floorAmp * (rng.nextFloat() * 2.0f - 1.0f);
        }

        // Feeds `seconds` of audio while pumping the message loop so timers/UI update.
        void feed (double seconds)
        {
            const int blocks = int (seconds * kSr / kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    buffer.setSample (0, i, sample (samplePos + i, 0));
                    buffer.setSample (1, i, sample (samplePos + i, 1));
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
    const auto& def = productDefinition (kProduct);
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots").getFullPathName());
    dir.createDirectory();

    Rig rig;
    auto& p = rig.proc;
    auto& ed = *rig.editor;

    p.setProfileFromUI (StyleProfileId::ModernGospel);
    rig.feed (1.5);
    rig.snap (dir, "01-simple");

    // Every source in the SOURCE menu, in Simple view.
    for (size_t i = 0; i < def.roles.size(); ++i)
    {
        p.setRoleFromUI (def.roles[i]);
        rig.feed (0.5);
        rig.snap (dir, "01-simple-" + juce::String (int (i) + 1) + "-" + juce::String (channelRoleName (def.roles[i])).toLowerCase().replaceCharacter (' ', '-'));
    }
    p.setRoleFromUI (def.defaultRole);

    ed.setView (SubBar::View::Advanced);
    for (auto stage : def.stages)
    {
        ed.getAdvancedPanel().setModule (stage);
        rig.feed (0.6);
        rig.snap (dir, juce::String ("02-advanced-") + juce::String (chainModuleName (stage)).toLowerCase().replaceCharacter ('-', '_'));
    }

    // Tune workflow: waiting for signal -> listening -> tuned (BEFORE/AFTER, KEEP, REVIEW, REVERT)
    rig.sharpness = 1.0f;
    rig.floorDb = -40.0f;
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
    ed.showLastAnalysis();
    rig.feed (0.2);
    rig.snap (dir, "06c-reopened-result");
    ed.dismissAnalyzeOverlay();

    ed.reviewAnalysisDetails();
    rig.feed (0.4);
    rig.snap (dir, "07-advanced-suggestion");

    // Input low / clipping / live safe
    ed.setView (SubBar::View::Simple);
    rig.sharpness = 0.0f; rig.floorDb = -66.0f;
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

    ed.setSize (1280, 860);
    rig.feed (0.6);
    rig.snap (dir, "11-simple-1280x860");
    ed.setView (SubBar::View::Advanced);
    ed.getAdvancedPanel().setModule (ChainModule::EQ);
    rig.feed (0.6);
    rig.snap (dir, "12-advanced-1280x860");
    ed.setSize (900, 650);

    rig.editor.reset();
    p.releaseResources();
    return 0;
}
