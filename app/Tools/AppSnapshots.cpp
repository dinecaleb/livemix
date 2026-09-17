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
#include <chrono>
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
        juce::Array<Device> inputDevices() override { juce::Array<Device> a; a.add ({ "Dante Virtual Soundcard", 32, 32 }); a.add ({ "SQ-6 USB", 32, 12 }); a.add ({ "MacBook Pro Microphone", 1, 0 }); return a; }
        juce::Array<Device> outputDevices() override { juce::Array<Device> a; a.add ({ "Dante Virtual Soundcard", 32, 32 }); a.add ({ "MacBook Pro Speakers", 0, 2 }); return a; }
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
        void reconfigure() override
        {
            // The same three steps the application takes: the graph is rebuilt for the new
            // assignments and the mix is carried onto it, so the tool photographs what a
            // reordered console actually looks like rather than one back at its baselines.
            const auto previous = controller.getPreparedSession();
            const auto mix = controller.getKept();
            const bool hadMix = controller.hasKeptMix();
            const int tunes = controller.getTuneCount();
            controller.prepare (kSr, kBlock);
            if (hadMix) controller.carryKept (mix, previous, tunes);
            dawEngine.setSession (controller.getSession());
            dawEngine.prepare (kSr, kBlock);
        }
        void saveSession() override {}
        void newSession() override {}
        juce::String saveSessionAs (const juce::String& name) override { sessionName = name; return {}; }
        juce::String loadSession (const juce::File&) override { return {}; }
        // A small library, so the SESSIONS page renders with something in it. The files do
        // not exist, so `summarise` reports them as unreadable - which is itself a state the
        // page has to draw - except the ones written below by the snapshot tool.
        juce::Array<SessionStore::Listing> listSessions() override { return sessions; }
        void addSession (const juce::String& name, const juce::File& file, juce::Time when) { sessions.add ({ name, file, when }); }
        juce::String currentInputDevice() override { return input; }
        juce::String currentOutputDevice() override { return output; }
        juce::String currentSessionName() override { return sessionName; }
        juce::File sessionFolder() override { return dawEngine.getProject().folder; }
        juce::String importMultitrack (const juce::File&) override { return "Import is not available in the snapshot tool."; }
        std::shared_ptr<const ExportJob> snapshotExport() override { return {}; }
        juce::String exportMix (std::shared_ptr<const ExportJob>, const juce::File&, ExportFormat,
                                std::function<bool (float)>) override
        {
            return "Export is not available in the snapshot tool.";
        }
    private:
        MixController& controller;
        DawEngine& dawEngine;
        bool running = false;
        juce::String input, output, sessionName { "Sunday" };
        juce::Array<SessionStore::Listing> sessions;
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

// ---------------------------------------------------------------------------
// FRAME COST
//
// "It feels slower than a DAW should" is not something you can fix by guessing, and a small
// demo session will never show it. This mode builds a realistically large console - 48
// channels, a timeline with a clip on every one of them - and measures what one frame of
// each workspace actually costs: the timer tick (meters, state, whatever each page decides
// has changed) and the paint that follows it.
//
// Run it before and after a change. The numbers are wall-clock milliseconds per frame on
// this machine; what matters is the direction, and that no workspace is anywhere near the
// 33 ms a 30 Hz tick has to fit inside.
//
//   dlive_ui_snapshots --frames [channels=48] [frames=120]
// ---------------------------------------------------------------------------
static int measureFrames (int channels, int frames)
{
    Rig rig;
    auto& view = *rig.view;
    rig.services.openDevices ("Dante Virtual Soundcard", "Dante Virtual Soundcard");

    // A console the size of a real church desk: a full kit, a band, a choir, the room.
    MixSession session;
    session.name = "Frame cost";
    const ChannelRole roles[] = {
        ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::HiHat, ChannelRole::RackTom,
        ChannelRole::FloorTom, ChannelRole::Overhead, ChannelRole::Room, ChannelRole::BassDI,
        ChannelRole::Piano, ChannelRole::Organ, ChannelRole::SynthPad, ChannelRole::AcousticGuitar,
        ChannelRole::ElectricGuitarClean, ChannelRole::SaxTenor, ChannelRole::LeadVocal,
        ChannelRole::BackingVocal, ChannelRole::Choir, ChannelRole::Speech, ChannelRole::CrowdMic
    };
    for (int i = 0; i < channels; ++i)
    {
        InputAssignment a;
        a.role = roles[size_t (i) % (sizeof (roles) / sizeof (roles[0]))];
        // Long names on purpose: this is what the channel panel has to lay out, and it is
        // exactly the case the panel's width was made adjustable for.
        a.name = (juce::String (channelRoleName (a.role)) + " - stage right " + juce::String (i + 1)).toStdString();
        a.inputA = i;
        session.inputs.push_back (a);
    }
    rig.controller.setSession (session);
    rig.services.reconfigure();          // the same three steps the application takes
    rig.pump (120);

    struct Result { const char* name; double tickMs; double steadyMs; double fullMs; };
    std::vector<Result> results;

    // Deliberately no window. Timing a real one on macOS measures the window server's vsync,
    // not DLIVE: the same run varies by an order of magnitude. Rendering into an image
    // measures only this application's own drawing, which is the thing a performance pass can
    // actually change and the thing a regression would show up in.

    const MainView::Page pages[] = { MainView::Page::Tracks, MainView::Page::Mixer,
                                     MainView::Page::Tune, MainView::Page::Live, MainView::Page::Inspector };
    const char* names[] = { "TRACKS", "MIXER", "TUNE", "LIVE", "INSPECTOR" };

    for (size_t p = 0; p < sizeof (pages) / sizeof (pages[0]); ++p)
    {
        view.showPage (pages[p]);
        rig.pump (120);

        juce::Image canvas (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);

        // What one timer tick costs: every refresh() on this page, with real audio underneath
        // so the meters really move and the pages really have new numbers to show.
        double tickTotal = 0.0;
        for (int f = 0; f < frames; ++f)
        {
            rig.feed (0.03);
            const auto t0 = std::chrono::steady_clock::now();
            rig.pump (1);
            tickTotal += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        }

        // What painting the whole window costs. This is the price of a page switch or a
        // resize - and it is also the price a page pays *every frame* if its refresh() calls
        // repaint() on itself rather than on the few things that moved. That is the number to
        // keep an eye on: it is the ceiling every other frame is measured against.
        double fullTotal = 0.0;
        for (int f = 0; f < 5; ++f)
        {
            const auto a = std::chrono::steady_clock::now();
            {
                juce::Graphics g (canvas);
                view.paint (g);
                for (auto* child : view.getChildren())
                    if (child->isVisible() && ! child->getBounds().isEmpty())
                    {
                        juce::Graphics::ScopedSaveState save (g);
                        g.reduceClipRegion (child->getBounds());
                        g.setOrigin (child->getPosition());
                        child->paintEntireComponent (g, true);
                    }
            }
            fullTotal += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - a).count();
        }

        results.push_back ({ names[p], tickTotal / frames, 0.0, fullTotal / 5.0 });
    }

    std::printf ("\nFRAME COST  (%d channels, %d frames, %d x %d)\n",
                 channels, frames, view.getWidth(), view.getHeight());
    std::printf ("  %-11s %10s %14s\n", "workspace", "tick ms", "full repaint");
    for (const auto& r : results)
        std::printf ("  %-11s %9.2f %13.2f%s\n", r.name, r.tickMs, r.fullMs,
                     (r.tickMs + r.fullMs) > 33.0 ? "   a page that repaints itself whole would miss the frame" : "");
    std::printf ("  tick = one refresh() of this page. full repaint = the whole window.\n");
    std::printf ("  a page must not call repaint() on itself per tick: at these sizes that alone\n"
                 "  spends the whole 33.3 ms a 30 Hz frame has.\n");
    return 0;
}

// ---------------------------------------------------------------------------
// DESK SIZES
//
// A booth is whatever screen the church already owns. This renders every workspace at the
// three that actually turn up - a 13" laptop, a 15" laptop and a 1080p monitor - so a
// layout that only holds together at the developer's window is caught before a Sunday.
//
//   dlive_ui_snapshots --sizes <dir>
// ---------------------------------------------------------------------------
static int renderSizes (const juce::File& dir)
{
    dir.createDirectory();
    Rig rig;
    auto& view = *rig.view;
    rig.services.openDevices ("Dante Virtual Soundcard", "Dante Virtual Soundcard");
    view.showPage (MainView::Page::Assign);
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
    view.showPage (MainView::Page::Purpose);
    view.getPurposePage().onContinue();
    rig.feed (1.0);
    rig.recordSyntheticTake (dir.getChildFile ("take"), 6.0);
    view.getTracksPage().rebuild();
    view.getTracksPage().zoomToFit();
    rig.controller.startTuneMix ({ 4.0f, -45.0f, 5.0f });
    rig.feed (5.5);
    for (int i = 0; i < 100 && rig.controller.getStage() != MixController::Stage::Preview; ++i) { rig.controller.poll(); rig.pump (10); }
    rig.controller.keepPlan();
    rig.feed (0.5);

    for (const auto& size : { std::pair<int, int> { 1280, 800 }, { 1440, 900 }, { 1920, 1080 } })
    {
        rig.view->setSize (size.first, size.second);
        const juce::String tag = juce::String (size.first) + "x" + juce::String (size.second);
        for (const auto& page : { std::pair<MainView::Page, const char*> { MainView::Page::Sessions,  "sessions" },
                                  { MainView::Page::Device,    "device" },
                                  { MainView::Page::Assign,    "inputs" },
                                  { MainView::Page::Purpose,   "purpose" },
                                  { MainView::Page::Tracks,    "tracks" },
                                  { MainView::Page::Mixer,     "mixer" },
                                  { MainView::Page::Tune,      "tune" },
                                  { MainView::Page::Live,      "live" },
                                  { MainView::Page::Inspector, "inspector" } })
        {
            view.showPage (page.first);
            rig.feed (0.4);
            rig.snap (dir, tag + "-" + page.second);
        }
    }
    rig.view.reset();
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc > 1 && juce::String (argv[1]) == "--sizes")
        return renderSizes (juce::File (argc > 2 ? juce::String (argv[2])
                                                 : juce::File::getCurrentWorkingDirectory().getChildFile ("app-sizes").getFullPathName()));
    if (argc > 1 && juce::String (argv[1]) == "--frames")
        return measureFrames (argc > 2 ? juce::String (argv[2]).getIntValue() : 48,
                              argc > 3 ? juce::String (argv[3]).getIntValue() : 120);

    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("app-snapshots").getFullPathName());
    dir.createDirectory();

    Rig rig;
    auto& view = *rig.view;

    // ---- SESSIONS: the library. Real documents on disk, so the table shows what each
    // session sounds like, what it was for and how its inputs fall across the groups.
    {
        const auto library = dir.getChildFile ("sessions");
        library.createDirectory();
        struct Seed { const char* name; StyleProfileId profile; MixPurpose purpose; int drums, bass, music, vocals, speech; double hoursAgo; };
        const Seed seeds[] = {
            { "Sunday 09:30 - Broadcast", StyleProfileId::ModernGospel, MixPurpose::ChurchBroadcast, 9, 1, 6, 5, 2, 2.0 },
            { "Sunday 11:15 - Room",      StyleProfileId::ModernGospel, MixPurpose::WorshipSession,  9, 1, 6, 5, 2, 26.0 },
            { "Midweek rehearsal",        StyleProfileId::ModernWorship, MixPurpose::WorshipSession, 6, 1, 5, 4, 0, 72.0 },
            { "Youth night",              StyleProfileId::ModernWorship, MixPurpose::Livestream,     8, 1, 6, 4, 1, 170.0 },
            { "Carols - stems import",    StyleProfileId::ModernGospel, MixPurpose::LiveRecording,   7, 1, 8, 6, 2, 400.0 },
            { "Sermon only",              StyleProfileId::ModernWorship, MixPurpose::ChurchBroadcast, 0, 0, 0, 0, 4, 900.0 },
            { "Template - 16 in",         StyleProfileId::ModernGospel, MixPurpose::Livestream,      6, 1, 4, 4, 1, 2400.0 }
        };
        const ChannelRole byBus[5] = { ChannelRole::SnareTop, ChannelRole::BassDI, ChannelRole::Piano,
                                       ChannelRole::BackingVocal, ChannelRole::Speech };
        for (const auto& seed : seeds)
        {
            SessionStore::Document d;
            d.session.name = seed.name;
            d.session.profile = seed.profile;
            d.session.purpose = seed.purpose;
            d.inputDevice = "Dante Virtual Soundcard";
            d.tuneCount = 2;
            d.hasMix = true;
            const int counts[5] = { seed.drums, seed.bass, seed.music, seed.vocals, seed.speech };
            int channel = 0;
            for (int b = 0; b < 5; ++b)
                for (int n = 0; n < counts[b]; ++n)
                {
                    InputAssignment a;
                    a.name = juce::String (channelRoleName (byBus[b])).toStdString();
                    a.role = byBus[b];
                    a.inputA = channel++;
                    d.session.inputs.push_back (a);
                }
            const auto file = library.getChildFile (juce::File::createLegalFileName (juce::String (seed.name)) + ".dlive.json");
            SessionStore::save (d, file);
            file.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (seed.hoursAgo));
            rig.services.addSession (seed.name, file, file.getLastModificationTime());
        }
        view.showPage (MainView::Page::Sessions);
        view.getSessionsPage().refresh();
        rig.snap (dir, "00-sessions");
    }

    view.showPage (MainView::Page::Device);
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

    // Inputs picked out: the toolbar becomes the bulk one - set what they are, fill a kit
    // down them in order, name them from their role, link them as pairs, or drop them.
    assign.selectInputs ({ 11, 12, 13 });
    rig.snap (dir, "03b-assign-selection");
    assign.selectInputs ({});

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

    // Rearranging the channels: the pastor is dragged to the top of the session. The clips go
    // with the track, the mix goes with the input, and the mixer reads the same order - so the
    // shot is of the whole thing having moved, not of a list of names having been shuffled.
    {
        const int pastor = int (rig.controller.getSession().inputs.size()) - 1;
        view.getTracksPage().moveTrack (pastor, 0);
        view.getTracksPage().zoomToFit();
        rig.feed (0.6);
        rig.snap (dir, "06e-tracks-reordered");
        view.getTracksPage().moveTrack (0, pastor);      // back, so every later shot is of the session as assigned
        rig.feed (0.4);
    }

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
    rig.controller.keepPlan();
    rig.feed (0.4);

    // REFERENCE MIX: "make it sound like this." The empty sheet says what matching does and
    // what it refuses to copy; with a record chosen it draws the two balances against each
    // other. The reference here is measured from the listen the tool just made and then
    // tilted, so the shot is of real numbers rather than of a drawing of some.
    {
        view.getMixPage().openReference();
        rig.snap (dir, "07c-tune-reference-empty");

        auto measured = rig.controller.getLastListen().buses[size_t (MixBus::Master)];
        measured.durationSeconds = 254.0f;
        measured.loudnessLufs = -9.2f;
        measured.silencePercent = 0.0f;
        measured.numChannels = 2;
        measured.stereoCorrelation = 0.45f;
        measured.crestFactorDb = 10.0f;
        measured.tempoBpm = 76.0f;
        measured.tempoConfidence = 0.7f;
        measured.bandEnergyDb[size_t (Band::Low)] += 4.0f;
        measured.bandEnergyDb[size_t (Band::LowMid)] -= 3.0f;
        measured.bandEnergyDb[size_t (Band::Brilliance)] += 5.0f;
        measured.bandEnergyDb[size_t (Band::Air)] += 6.0f;
        rig.controller.setReference (Reference::profileFrom (measured, "Take Me To The King", "~/Music/king.wav"));
        rig.feed (0.3);
        rig.snap (dir, "07d-tune-reference");
        view.getMixPage().openReference();     // the button is a toggle: this puts the sheet away
        rig.feed (0.2);
    }
    rig.controller.clearReference();
    rig.feed (0.2);

    // TUNE LIVE MIX: the same listen with the mix engineer's reasoning on top. The sheet shows
    // the steps the state machine is really on, so these two shots are of actual states.
    {
        // A deliberately slow engineer, so the tool can photograph the state the app spends
        // most of a cloud run in: listening finished, nothing left to fill, still working.
        // The built-in one answers instantly, which is exactly why that state was easy to
        // ship without a visual cue.
        struct SlowProvider final : MixReasoningProvider
        {
            LocalMixReasoningProvider inner;
            std::string getName() const override { return "DLIVE built-in (offline)"; }
            bool isAvailable() const override { return true; }
            bool sendsDataExternally() const override { return false; }
            MixReasoningResponse reason (const MixReasoningRequest& r, const std::atomic<bool>& cancel) override
            {
                for (int i = 0; i < 40 && ! cancel.load(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (50));
                return inner.reason (r, cancel);
            }
        };
        rig.controller.setReasoningProvider (std::make_shared<SlowProvider>());

        MixController::LiveTuneSettings live;
        live.initial = { 7.0f, -45.0f, 5.0f };
        live.verify = { 6.5f, -45.0f, 5.0f };
        rig.controller.startTuneLiveMix (live);
        rig.feed (1.2);
        rig.snap (dir, "08b-tune-live-listening");

        // Wait for the run to leave the listen, then photograph it mid-thought.
        for (int i = 0; i < 600 && rig.controller.getTuneLive().getState() != TuneLiveCoordinator::State::WaitingForReasoning; ++i)
            { rig.controller.poll(); rig.feed (0.05); }
        rig.feed (0.6);
        rig.snap (dir, "08c-tune-live-working");

        for (int i = 0; i < 900 && rig.controller.isTuningLive(); ++i) { rig.controller.poll(); rig.feed (0.05); }
        rig.feed (0.5);
        rig.snap (dir, "09b-tune-live-result");
        rig.controller.revertPlan();
        rig.feed (0.3);
    }

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

    // ---- the console in its own window at its smallest (MixerWindow::setResizeLimits is
    // 720 x 420): no sidebar, no "Open in a window", and the sub-toolbar has to give way -
    // the hint first, then the count - rather than write the session's name under a button.
    {
        view.setSidebarShown (false);
        view.getMixerPage().setWindowButtonVisible (false);
        rig.view->setSize (720, 460);
        rig.feed (0.3);
        rig.snap (dir, "26-mixer-window-min");
        view.getMixerPage().setView (MixerPage::View::List);
        rig.feed (0.3);
        rig.snap (dir, "26b-mixer-window-min-list");
        view.getMixerPage().setView (MixerPage::View::Strips);
        view.getMixerPage().setWindowButtonVisible (true);
        rig.view->setSize (1520, 960);
        view.setSidebarShown (true);
        rig.feed (0.3);
    }

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

    // MIX CHAT: a change asked for in words. It works with no account and no network - with no
    // cloud model configured the sentence is read by DLIVE's own parser, which is deterministic
    // and offline - so these two shots are of the built-in reasoning, which is what a church
    // booth with no internet actually gets. The empty sheet says what it can be asked; the
    // answered one shows the sentence, what DLIVE decided line by line, and the same
    // BEFORE / AFTER / KEEP / REVERT the rest of the app decides a plan with.
    view.closeSheets();
    view.showPage (MainView::Page::Mixer);
    view.showChat();
    rig.feed (0.4);
    rig.snap (dir, "27-chat");
    rig.controller.sendChatRequest ("bring the lead vocal forward and take some boom out of the kick");
    for (int i = 0; i < 900 && rig.controller.isChatBusy(); ++i) { rig.controller.poll(); rig.feed (0.05); }
    rig.feed (0.6);
    rig.snap (dir, "27b-chat-answered");
    rig.controller.revertPlan();
    view.closeSheets();
    rig.feed (0.3);

    // ---- the smallest window DLIVE allows (MainWindow::setResizeLimits). A workspace that
    // only works at the developer's resolution is a workspace that breaks on a laptop at the
    // back of a church, so every one of them is rendered here too.
    view.closeSheets();
    rig.view->setSize (1180, 760);
    for (const auto& small : { std::pair<MainView::Page, const char*> { MainView::Page::Tracks,    "21-min-tracks" },
                               { MainView::Page::Mixer,     "22-min-mixer" },
                               { MainView::Page::Tune,      "23-min-tune" },
                               { MainView::Page::Live,      "24-min-live" },
                               { MainView::Page::Inspector, "25-min-inspector" } })
    {
        view.showPage (small.first);
        rig.feed (0.4);
        rig.snap (dir, small.second);
    }

    std::printf ("stage %d, health %d%%\n", int (rig.controller.getStage()), rig.controller.getMixHealthPercent());
    rig.view.reset();
    return 0;
}
