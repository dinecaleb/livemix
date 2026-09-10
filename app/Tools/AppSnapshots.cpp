// Headless DLIVE UI snapshots: builds the real controller, DAW engine and MainView
// without a device, feeds a synthetic 16-input band through the engine, writes a short
// multitrack to disk so the timeline has real waveforms, then walks every workspace and
// state and writes PNGs. Usage: dlive_ui_snapshots <output-dir>
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
        int numOutputChannels() override { return running ? 8 : 0; }        // a four-pair interface
        juce::StringArray outputChannelNames() override
        {
            juce::StringArray names;
            for (int i = 1; i <= 8; ++i) names.add ("Out " + juce::String (i));
            return names;
        }
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

    // A track whose name no longer describes the audio under it: the header flags it in amber
    // and its right-click menu is where a name, a source or the assignments are put right.
    {
        const std::string wasSnare = rig.controller.getSession().inputs[1].name;
        const std::string wasKeys = rig.controller.getSession().inputs[7].name;
        rig.controller.setInputName (1, "Rack Tom");
        rig.controller.setInputName (7, "Jewel");
        // ...and a track drawn as something the role would never pick: Keys running playback.
        rig.controller.setInputIcon (7, "waveform");
        view.getTracksPage().rebuild();
        rig.feed (0.3);
        rig.snap (dir, "06d-tracks-name-mismatch");
        rig.controller.setInputName (1, wasSnare);
        rig.controller.setInputName (7, wasKeys);
        rig.controller.setInputIcon (7, {});
        view.getTracksPage().rebuild();
        rig.feed (0.3);
    }

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
    view.getAdvancedPage().selectStage (3);  // corrective EQ: the curve, its nodes and the band cards
    rig.feed (0.3);
    rig.snap (dir, "11b-inspector-eq");
    view.getAdvancedPage().selectStage (5);  // the compressor: its in/out line and the live reduction
    rig.feed (0.3);
    rig.snap (dir, "11c-inspector-comp");
    view.getAdvancedPage().selectStage (0);  // back to the input, so the next channel opens where it left off
    view.getAdvancedPage().select (1); // Snare — often has a snare-plate send after Tune
    rig.feed (0.3);
    rig.snap (dir, "12-inspector-send");
    view.getAdvancedPage().selectStage (10); // the sends close the path where the session uses FX
    rig.feed (0.3);
    rig.snap (dir, "12b-inspector-sends");
    view.getAdvancedPage().selectStage (0);
    view.getAdvancedPage().selectBus (MixBus::Drums);
    rig.feed (0.3);
    rig.snap (dir, "13-inspector-bus");
    view.getAdvancedPage().selectBus (MixBus::Master);
    rig.feed (0.3);
    rig.snap (dir, "14-inspector-master");

    // Every panel at the edge folded away: the sidebar and both of the Inspector's
    // columns, so the channel and its chain have the whole window.
    view.getAdvancedPage().select (0);
    view.setSidebarShown (false);
    view.getAdvancedPage().setRailShown (false);
    view.getAdvancedPage().setTrailShown (false);
    rig.feed (0.3);
    rig.snap (dir, "14b-inspector-panels-folded");
    view.getAdvancedPage().setRailShown (true);
    view.getAdvancedPage().setTrailShown (true);

    view.showPage (MainView::Page::Mixer);
    rig.feed (0.3);
    rig.snap (dir, "15-mixer-no-sidebar");
    view.setSidebarShown (true);
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
    view.getMixerPage().selectStrip (10);            // the lead vocal: the chain strip along the foot
    rig.controller.setStripMute (2, true);           // a muted strip has to read as muted
    rig.controller.setStripSolo (10, true);
    rig.feed (0.4);
    rig.snap (dir, "15g-mixer-mute-solo");
    rig.controller.setStripMute (2, false);
    rig.controller.setStripSolo (10, false);
    rig.feed (0.3);
    view.setBypass (true);
    rig.feed (0.3);
    rig.snap (dir, "15f-mixer-bypass");
    view.setBypass (false);

    view.showPage (MainView::Page::Tune);
    view.getMixPage().setRailShown (false);
    rig.feed (0.3);
    rig.snap (dir, "16b-tune-rail-folded");
    view.getMixPage().setRailShown (true);
    rig.controller.keepPlan();
    view.getMixPage().setMacroValue (MixMacro::Space, 72.0f);
    view.getMixPage().setMacroValue (MixMacro::Drums, 30.0f);
    rig.feed (0.5);
    rig.snap (dir, "16-tune-macros");

    view.showPage (MainView::Page::Live);
    rig.feed (0.5);
    rig.snap (dir, "17-live");

    // A muted group, and a soloed one: during a service the state has to be readable at a
    // glance, so both are snapped.
    rig.controller.setBusMute (MixBus::Drums, true);
    rig.controller.setBusSolo (MixBus::Vocals, true);
    rig.feed (0.5);
    rig.snap (dir, "17b-live-muted");
    rig.controller.setBusMute (MixBus::Drums, false);
    rig.controller.setBusSolo (MixBus::Vocals, false);
    rig.feed (0.3);

    // TUNE CHANNEL: one source listened to and tuned on its own, over whatever workspace
    // it was clicked on. Here: the lead vocal, from the console.
    view.showPage (MainView::Page::Mixer);
    view.getMixerPage().selectStrip (10);
    rig.controller.setStripInputGain (10, -12.0f);   // as if the desk had been moved under it
    rig.controller.setStripFader (10, -8.0f);
    rig.feed (0.3);
    view.tuneChannel (10, { 4.0f, -45.0f, 5.0f });
    rig.feed (1.2);
    rig.snap (dir, "19-channel-listening");
    rig.feed (4.5);
    for (int i = 0; i < 100 && rig.controller.getStage() != MixController::Stage::Preview; ++i) { rig.controller.poll(); rig.pump (10); }
    rig.feed (0.5);
    rig.snap (dir, "20-channel-tuned");
    rig.controller.keepPlan();
    rig.feed (0.5);
    rig.pump (50);

    // Outputs: the main pair, plus a cue on 3-4 carrying the vocals group.
    view.showPage (MainView::Page::Mixer);
    {
        auto feeds = rig.controller.getOutputFeeds();
        feeds.count = 2;
        feeds.feeds[1].left = 2;
        feeds.feeds[1].right = 3;
        feeds.feeds[1].source = MixBus::Vocals;
        feeds.feeds[1].gainDb = -4.5f;
        rig.controller.setOutputFeeds (feeds);
    }
    view.showOutputs();
    rig.feed (0.4);
    rig.snap (dir, "18-outputs");

    std::printf ("stage %d, health %d%%\n", int (rig.controller.getStage()), rig.controller.getMixHealthPercent());
    rig.view.reset();
    return 0;
}
