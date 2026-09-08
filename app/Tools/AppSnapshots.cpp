// Headless DINELIVE UI snapshots: builds the real controller + MainView without a device,
// feeds a synthetic 16-input band through the engine, walks every page and state and writes
// PNGs. Usage: dinelive_ui_snapshots <output-dir>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_events/juce_events.h>
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
        explicit FakeServices (MixController& c) : controller (c) {}
        juce::Array<Device> inputDevices() override { juce::Array<Device> a; a.add ({ "Dante Virtual Soundcard", 32 }); a.add ({ "SQ-6 USB", 32 }); a.add ({ "MacBook Pro Microphone", 1 }); return a; }
        juce::Array<Device> outputDevices() override { juce::Array<Device> a; a.add ({ "Dante Virtual Soundcard", 32 }); a.add ({ "MacBook Pro Speakers", 2 }); return a; }
        juce::String openDevices (const juce::String& in, const juce::String& out) override { input = in; output = out; running = true; return {}; }
        juce::String changeOutput (const juce::String& out) override { output = out; return {}; }
        bool isAudioRunning() override { return running; }
        int numInputChannels() override { return running ? kInputs : 0; }
        double sampleRate() override { return kSr; }
        int bufferSize() override { return kBlock; }
        int xrunCount() override { return 0; }
        void reconfigure() override { controller.prepare (kSr, kBlock); }
        void saveSession() override {}
        juce::String saveSessionAs (const juce::String& name) override { sessionName = name; return {}; }
        juce::String loadSession (const juce::File&) override { return {}; }
        juce::Array<SessionStore::Listing> listSessions() override { return {}; }
        juce::String currentInputDevice() override { return input; }
        juce::String currentOutputDevice() override { return output; }
        juce::String currentSessionName() override { return sessionName; }
        juce::String openRecording (const juce::File&, const juce::String&) override { return "Recordings are not available in the snapshot tool."; }
        bool isPlayingRecording() override { return false; }
        MixSession recordingSuggestion (const MixSession& base) override { return base; }
        juce::String exportMix (const MixSession&, const MixParameters&, const juce::File&, const juce::File&, ExportFormat) override
        {
            return "Export is not available in the snapshot tool.";
        }
    private:
        MixController& controller;
        bool running = false;
        juce::String input, output, sessionName { "Sunday" };
    };

    struct Rig
    {
        MixController controller;
        FakeServices services { controller };
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
            view->setSize (1400, 920);
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
                if (controller.isPrepared()) controller.process (ip.data(), kInputs, op, 2, kBlock);
                pos += kBlock;
                if ((b % 4) == 0) pump (1);
                else std::this_thread::sleep_for (std::chrono::microseconds (300));
            }
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
    rig.snap (dir, "05-mix-ready");

    // TUNE MIX: a short listen for the tool.
    rig.controller.startTuneMix ({ 4.0f, -45.0f, 5.0f });
    rig.feed (1.2);
    rig.snap (dir, "06-mix-listening");
    rig.feed (4.5);
    for (int i = 0; i < 100 && rig.controller.getStage() != MixController::Stage::Preview; ++i) { rig.controller.poll(); rig.pump (10); }
    rig.feed (0.5);
    rig.snap (dir, "07-mix-preview");
    rig.controller.setCompare (MixController::Compare::Before);
    rig.feed (0.3);
    rig.snap (dir, "08-mix-preview-before");
    rig.controller.setCompare (MixController::Compare::After);

    view.showPage (MainView::Page::Advanced);
    view.getAdvancedPage().select (0); // Kick — a channel, not a bus
    rig.feed (0.3);
    rig.snap (dir, "09-advanced-strip");
    view.getAdvancedPage().select (1); // Snare — often has a snare-plate send after Tune
    rig.feed (0.3);
    rig.snap (dir, "09a-advanced-send");
    view.getAdvancedPage().selectBus (MixBus::Drums);
    rig.feed (0.3);
    rig.snap (dir, "09b-advanced-bus");
    view.getAdvancedPage().selectBus (MixBus::Master);
    rig.feed (0.3);
    rig.snap (dir, "10-advanced-master");

    view.showPage (MainView::Page::Mixer);
    rig.feed (0.3);
    rig.snap (dir, "10a-mixer");

    view.showPage (MainView::Page::Mix);
    rig.controller.keepPlan();
    view.getMixPage().setMacroValue (MixMacro::Space, 72.0f);
    view.getMixPage().setMacroValue (MixMacro::Drums, 30.0f);
    rig.feed (0.5);
    rig.snap (dir, "11-mix-ready-macros");
    std::printf ("stage %d, health %d%%\n", int (rig.controller.getStage()), rig.controller.getMixHealthPercent());
    rig.view.reset();
    return 0;
}
