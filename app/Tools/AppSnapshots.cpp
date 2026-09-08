// Headless DINELIVE UI snapshots: builds the real controller, DAW engine and MainView
// without a device, feeds a synthetic 16-input band through the engine, writes a short
// multitrack to disk so the timeline has real waveforms, then walks every workspace and
// state and writes PNGs. Usage: dinelive_ui_snapshots <output-dir>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_events/juce_events.h>
#include "native/DawEngine.h"
#include "native/MixController.h"
#include "ui/MainView.h"
#include <cstdio>
#include <random>
#include <thread>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;
    constexpr int kInputs = 16;

    class FakeServices : public AppServices
    {
    public:
        FakeServices (MixController& c, DawEngine& d) : controller (c), dawEngine (d) {}

        DawEngine& daw() override { return dawEngine; }
        juce::Array<Device> inputDevices() override { juce::Array<Device> a; a.add ({ "Dante Virtual Soundcard", 32 }); a.add ({ "SQ-6 USB", 32 }); a.add ({ "MacBook Pro Microphone", 1 }); return a; }
        juce::Array<Device> outputDevices() override { juce::Array<Device> a; a.add ({ "Dante Virtual Soundcard", 32 }); a.add ({ "MacBook Pro Speakers", 2 }); return a; }
        juce::String openDevices (const juce::String& in, const juce::String& out) override { input = in; output = out; running = true; return {}; }
        juce::String openOutputOnly (const juce::String& out) override { output = out; running = true; return {}; }
        juce::String changeOutput (const juce::String& out) override { output = out; return {}; }
        bool isAudioRunning() override { return running; }
        int numInputChannels() override { return running ? kInputs : 0; }
        double sampleRate() override { return kSr; }
        int bufferSize() override { return kBlock; }
        int xrunCount() override { return 0; }
        void reconfigure() override { controller.prepare (kSr, kBlock); dawEngine.setSession (controller.getSession()); dawEngine.prepare (kSr, kBlock); }
        void saveSession() override {}
        void newSession() override {}
        juce::String saveSessionAs (const juce::String& name) override { sessionName = name; return {}; }
        juce::String loadSession (const juce::File&) override { return {}; }
        juce::Array<SessionStore::Listing> listSessions() override { return {}; }
        juce::String currentInputDevice() override { return input; }
        juce::String currentOutputDevice() override { return output; }
        juce::String currentSessionName() override { return sessionName; }
        juce::File sessionFolder() override { return dawEngine.getProject().folder; }
        juce::String importMultitrack (const juce::File&) override { return "Import is not available in the snapshot tool."; }
        juce::String exportMix (const juce::File&, ExportFormat, std::function<bool (float)>) override
        {
            return "Export is not available in the snapshot tool.";
        }
    private:
        MixController& controller;
        DawEngine& dawEngine;
        bool running = false;
        juce::String input, output, sessionName { "Sunday" };
    };

    struct Rig
    {
        MixController controller;
        DawEngine dawEngine { controller };
        FakeServices services { controller, dawEngine };
        std::unique_ptr<MainView> view;
        std::vector<std::vector<float>> in;
        std::vector<const float*> ip;
        std::vector<float> outL, outR;
        long long pos = 0;
        std::mt19937 rng { 7 };
        std::uniform_real_distribution<float> dist { -1.0f, 1.0f };

        Rig()
        {
            view = std::make_unique<MainView> (controller, services);
            view->setSize (1520, 960);
            view->setVisible (true);
            in.assign (kInputs, std::vector<float> (kBlock, 0.0f));
            ip.resize (kInputs);
            outL.resize (kBlock); outR.resize (kBlock);
        }

        void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

        // A church band on 16 inputs, matching the assignments made below.
        float sample (int input, long long p)
        {
            const float t = float (p) / float (kSr);
            const float beat = std::fmod (t, 0.5f);
            switch (input)
            {
                case 0:  return beat < 0.1f ? 0.7f * (1.0f - beat / 0.1f) * std::sin (2.0f * float (M_PI) * 60.0f * t) : 0.0f;         // kick
                case 1:  return std::fmod (t + 0.25f, 0.5f) < 0.04f ? 0.5f * dist (rng) : 0.0f;                                        // snare
                case 2:  return std::fmod (t, 2.0f) < 0.3f ? 0.4f * std::sin (2.0f * float (M_PI) * 120.0f * t) : 0.02f * dist (rng);   // tom
                case 3:  return std::fmod (t + 1.0f, 2.0f) < 0.3f ? 0.4f * std::sin (2.0f * float (M_PI) * 90.0f * t) : 0.02f * dist (rng);
                case 4: case 5: return 0.05f * dist (rng);                                                                              // OH pair
                case 6:  return 0.04f * dist (rng);                                                                                     // room
                case 7:  return 0.4f * std::sin (2.0f * float (M_PI) * 55.0f * t);                                                       // bass
                case 8:  return 0.15f * std::sin (2.0f * float (M_PI) * 262.0f * t) + 0.1f * std::sin (2.0f * float (M_PI) * 2600.0f * t); // keys L
                case 9:  return 0.15f * std::sin (2.0f * float (M_PI) * 330.0f * t) + 0.1f * std::sin (2.0f * float (M_PI) * 2800.0f * t); // keys R
                case 10: return 0.3f * std::sin (2.0f * float (M_PI) * 220.0f * t) * (0.6f + 0.4f * std::sin (2.0f * float (M_PI) * 0.7f * t)); // lead
                case 11: return 0.2f * std::sin (2.0f * float (M_PI) * 330.0f * t);
                case 12: return 0.2f * std::sin (2.0f * float (M_PI) * 392.0f * t);
                case 13: return 0.2f * std::sin (2.0f * float (M_PI) * 494.0f * t);
                default: return 0.0f;   // pastor and spare stay silent
            }
        }

        // Feeds `seconds` of band while pumping the message loop, paced so the listen worker keeps up.
        void feed (double seconds)
        {
            const int blocks = int (seconds * kSr / kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                for (int c = 0; c < kInputs; ++c)
                {
                    for (int i = 0; i < kBlock; ++i) in[size_t (c)][size_t (i)] = sample (c, pos + i);
                    ip[size_t (c)] = in[size_t (c)].data();
                }
                float* op[2] = { outL.data(), outR.data() };
                if (controller.isPrepared()) dawEngine.processBlock (ip.data(), kInputs, op, 2, kBlock);
                pos += kBlock;
                if ((b % 4) == 0) pump (1);
                else std::this_thread::sleep_for (std::chrono::microseconds (300));
            }
        }

        // Writes one short WAV per assigned track and puts it on the timeline, so the Tracks
        // workspace shows the waveforms it will show in the real application.
        void recordSyntheticTake (const juce::File& folder, double seconds)
        {
            folder.createDirectory();
            auto project = dawEngine.getProject();
            project.folder = folder;
            project.sampleRate = kSr;
            project.syncTracks (controller.getSession());

            juce::WavAudioFormat wav;
            const auto& inputs = controller.getSession().inputs;
            for (size_t t = 0; t < inputs.size(); ++t)
            {
                const int channels = inputs[t].isStereo() ? 2 : 1;
                const int frames = int (seconds * kSr);
                juce::AudioBuffer<float> buffer (channels, frames);
                for (int ch = 0; ch < channels; ++ch)
                {
                    const int source = ch == 0 ? inputs[t].inputA : inputs[t].inputB;
                    for (int i = 0; i < frames; ++i) buffer.setSample (ch, i, sample (source, i));
                }
                const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String (inputs[t].name)) + "_001.wav");
                if (auto* stream = file.createOutputStream().release())
                {
                    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, kSr, (unsigned) channels, 24, {}, 0));
                    if (writer != nullptr) writer->writeFromAudioSampleBuffer (buffer, 0, frames);
                    else delete stream;
                }

                AudioClip clip;
                clip.name = juce::String (inputs[t].name);
                clip.file = file.getFullPathName();
                clip.start = juce::int64 (kSr * 0.5);
                clip.length = frames;
                clip.fileSampleRate = kSr;
                project.tracks[t].clips.push_back (clip);
                project.tracks[t].armed = (t % 5) == 0;
            }
            dawEngine.setProject (project);
        }

        void snap (const juce::File& dir, const juce::String& name)
        {
            pump (80);
            auto img = view->createComponentSnapshot (view->getLocalBounds(), false, 2.0f);
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
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("app-snapshots").getFullPathName());
    dir.createDirectory();

    Rig rig;
    auto& view = *rig.view;
    rig.snap (dir, "01-device");

    rig.services.openDevices ("Dante Virtual Soundcard", "Dante Virtual Soundcard");
    view.showPage (MainView::Page::Assign);
    rig.snap (dir, "02-assign-empty");
    auto& assign = view.getAssignPage();
    assign.assign (0, ChannelRole::KickIn, "Kick");
    assign.assign (1, ChannelRole::SnareTop, "Snare");
    assign.assign (2, ChannelRole::RackTom, "Rack Tom");
    assign.assign (3, ChannelRole::FloorTom, "Floor Tom");
    assign.assign (4, ChannelRole::Overhead, "OH", true);
    assign.assign (6, ChannelRole::Room, "Room");
    assign.assign (7, ChannelRole::BassDI, "Bass");
    assign.assign (8, ChannelRole::Piano, "Keys", true);
    assign.assign (10, ChannelRole::LeadVocal, "Lead");
    assign.assign (11, ChannelRole::BackingVocal, "BGV 1");
    assign.assign (12, ChannelRole::BackingVocal, "BGV 2");
    assign.assign (13, ChannelRole::BackingVocal, "BGV 3");
    assign.assign (14, ChannelRole::Speech, "Pastor");
    rig.snap (dir, "03-assign");

    view.showPage (MainView::Page::Purpose);
    rig.snap (dir, "04-purpose");

    view.getPurposePage().onContinue();
    rig.feed (1.0);
    rig.snap (dir, "05-tracks-empty");

    // A take on the timeline, so the waveforms and the clip editing are really exercised.
    const auto take = dir.getChildFile ("take");
    rig.recordSyntheticTake (take, 6.0);
    view.getTracksPage().rebuild();
    view.getTracksPage().zoomToFit();

    // Markers, so the ruler's marker lane and its lines are really exercised.
    rig.dawEngine.locate (juce::int64 (1.2 * kSr));
    view.getTracksPage().addMarkerAtPlayhead();
    rig.dawEngine.locate (juce::int64 (3.6 * kSr));
    view.getTracksPage().addMarkerAtPlayhead();
    rig.dawEngine.setLoop (true, juce::int64 (1.2 * kSr), juce::int64 (3.6 * kSr));
    rig.dawEngine.locate (juce::int64 (2.4 * kSr));
    rig.feed (1.5);
    rig.snap (dir, "06-tracks");

    view.getTracksPage().setRowHeight (TracksPage::RowHeight::Small);
    rig.feed (0.3);
    rig.snap (dir, "06b-tracks-short-rows");
    view.getTracksPage().setRowHeight (TracksPage::RowHeight::Large);
    rig.feed (0.3);
    rig.snap (dir, "06c-tracks-tall-rows");
    view.getTracksPage().setRowHeight (TracksPage::RowHeight::Medium);
    rig.feed (0.3);

    view.showPage (MainView::Page::Tune);
    rig.feed (0.5);
    rig.snap (dir, "07-tune-ready");

    // TUNE MIX: a short listen for the tool.
    rig.controller.startTuneMix ({ 4.0f, -45.0f, 5.0f });
    rig.feed (1.2);
    rig.snap (dir, "08-tune-listening");
    rig.feed (4.5);
    for (int i = 0; i < 100 && rig.controller.getStage() != MixController::Stage::Preview; ++i) { rig.controller.poll(); rig.pump (10); }
    rig.feed (0.5);
    rig.snap (dir, "09-tune-preview");
    rig.controller.setCompare (MixController::Compare::Before);
    rig.feed (0.3);
    rig.snap (dir, "10-tune-preview-before");
    rig.controller.setCompare (MixController::Compare::After);

    view.showPage (MainView::Page::Inspector);
    view.getAdvancedPage().select (0); // Kick — a channel, not a bus
    rig.feed (0.3);
    rig.snap (dir, "11-inspector-strip");
    view.getAdvancedPage().select (1); // Snare — often has a snare-plate send after Tune
    rig.feed (0.3);
    rig.snap (dir, "12-inspector-send");
    view.getAdvancedPage().selectBus (MixBus::Drums);
    rig.feed (0.3);
    rig.snap (dir, "13-inspector-bus");
    view.getAdvancedPage().selectBus (MixBus::Master);
    rig.feed (0.3);
    rig.snap (dir, "14-inspector-master");

    view.showPage (MainView::Page::Mixer);
    rig.feed (0.3);
    rig.snap (dir, "15-mixer");

    view.getMixerPage().setStripSize (MixerPage::Size::Narrow);
    rig.feed (0.3);
    rig.snap (dir, "15b-mixer-narrow");
    view.getMixerPage().setStripSize (MixerPage::Size::Wide);
    rig.feed (0.3);
    rig.snap (dir, "15c-mixer-wide");
    view.getMixerPage().setStripSize (MixerPage::Size::Normal);
    view.getMixerPage().setView (MixerPage::View::List);
    rig.feed (0.3);
    rig.snap (dir, "15d-mixer-list");
    view.getMixerPage().setShow (MixerPage::Show::Groups);
    rig.feed (0.3);
    rig.snap (dir, "15e-mixer-groups");
    view.getMixerPage().setShow (MixerPage::Show::All);
    view.getMixerPage().setView (MixerPage::View::Strips);
    view.setBypass (true);
    rig.feed (0.3);
    rig.snap (dir, "15f-mixer-bypass");
    view.setBypass (false);

    view.showPage (MainView::Page::Tune);
    rig.controller.keepPlan();
    view.getMixPage().setMacroValue (MixMacro::Space, 72.0f);
    view.getMixPage().setMacroValue (MixMacro::Drums, 30.0f);
    rig.feed (0.5);
    rig.snap (dir, "16-tune-macros");

    view.showPage (MainView::Page::Live);
    rig.feed (0.5);
    rig.snap (dir, "17-live");

    std::printf ("stage %d, health %d%%\n", int (rig.controller.getStage()), rig.controller.getMixHealthPercent());
    rig.view.reset();
    return 0;
}
