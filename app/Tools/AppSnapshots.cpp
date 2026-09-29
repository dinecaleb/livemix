// Headless DLIVE UI snapshots: builds the real controller, DAW engine and MainView
// without a device, feeds a synthetic 16-input band through the engine, writes a short
// multitrack to disk so the timeline has real waveforms, then walks every workspace and
// state and writes PNGs. Usage: dlive_ui_snapshots <output-dir>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_events/juce_events.h>
#include "native/DawEngine.h"
#include "native/SampleLibrary.h"
#include "native/MixController.h"
#include "ui/MainView.h"
#include "native/ThemeStore.h"
#include "native/StemNames.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <cstdio>
#include <random>
#include <chrono>
#include <thread>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;
    constexpr int kSyntheticInputs = 16;

    // A REAL SERVICE, when one is given (--stems <folder>).
    //
    // The synthetic band further down is sine tones and noise bursts. It is enough to prove a
    // meter moves, and not enough to look at: a spectrum of a sine is a spike, a waveform of a
    // noise burst is a block, and a mix of them tells you nothing about whether the console
    // reads right. So every screen that is going to be compared against the design is rendered
    // from a real multitrack instead - the same recordings TUNE MIX is checked against
    // (docs/BUILD-AND-VERIFY.md). The tones stay as the fallback, so CI and a machine with no
    // recordings on it still render every screen.
    //
    // Nothing is copied: the timeline's clips point at the files where they are, and the engine
    // is fed a window held in memory.
    struct StemSet
    {
        struct Stem
        {
            juce::String name;                  // the display name, take number stripped
            ChannelRole role = ChannelRole::Count;
            juce::File file;
            juce::AudioBuffer<float> audio;     // the window fed to the engine
            juce::int64 lengthInFile = 0;       // the whole file, for the timeline clip
            int firstInput = 0;
            bool stereo = false;
            float peakDb = -120.0f;
        };

        std::vector<Stem> stems;
        std::vector<std::pair<int, int>> channels;   // input number -> (stem, channel in it)
        juce::String source;

        bool loaded() const { return ! stems.empty(); }
        int numInputs() const { return int (channels.size()); }

        // Reads `seconds` from `offset` out of every file in the folder whose name names a
        // source DLIVE knows. Returns a sentence on failure, "" on success.
        juce::String load (const juce::File& folder, double seconds, double offset)
        {
            if (! folder.isDirectory()) return "There is no folder at " + folder.getFullPathName();

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            juce::Array<juce::File> files;
            folder.findChildFiles (files, juce::File::findFiles, false, "*.wav;*.aif;*.aiff");
            files.sort();

            for (const auto& file : files)
            {
                ChannelRole role {};
                if (! StemNames::guessRole (file.getFileNameWithoutExtension(), role)) continue;

                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
                if (reader == nullptr) continue;

                Stem stem;
                stem.name = StemNames::cleanName (file.getFileNameWithoutExtension());
                stem.role = role;
                stem.file = file;
                stem.lengthInFile = reader->lengthInSamples;
                stem.stereo = reader->numChannels > 1;

                const int channelCount = stem.stereo ? 2 : 1;
                const auto start = juce::int64 (offset * reader->sampleRate);
                const int wanted = int (seconds * reader->sampleRate);
                const int available = int (juce::jmax (juce::int64 (0), reader->lengthInSamples - start));
                const int frames = juce::jmin (wanted, available);
                if (frames <= kBlock) continue;                      // nothing there to feed

                stem.audio.setSize (channelCount, frames);
                reader->read (&stem.audio, 0, frames, start, true, channelCount > 1);
                stem.peakDb = juce::Decibels::gainToDecibels (stem.audio.getMagnitude (0, frames));
                stems.push_back (std::move (stem));
            }

            if (stems.empty()) return "No file in " + folder.getFullPathName() + " names a source DLIVE knows.";

            // The desk order the design draws: the kit, then the band, then the voices, then the
            // room - which is the order the engine already puts a group in, so sorting by group
            // and then by name gives the console its familiar shape.
            std::sort (stems.begin(), stems.end(), [] (const Stem& a, const Stem& b)
            {
                const auto ga = int (mixBusForRole (a.role)), gb = int (mixBusForRole (b.role));
                if (ga != gb) return ga < gb;
                return a.name.compareNatural (b.name) < 0;
            });

            for (auto& stem : stems)
            {
                stem.firstInput = int (channels.size());
                channels.push_back ({ int (&stem - stems.data()), 0 });
                if (stem.stereo) channels.push_back ({ int (&stem - stems.data()), 1 });
            }
            source = folder.getFileName();
            return {};
        }

        // The stem an input number belongs to, or nullptr.
        const Stem* forInput (int input) const
        {
            if (input < 0 || input >= int (channels.size())) return nullptr;
            return &stems[size_t (channels[size_t (input)].first)];
        }

        float sample (int input, long long p) const
        {
            if (input < 0 || input >= int (channels.size())) return 0.0f;
            const auto& [which, channel] = channels[size_t (input)];
            const auto& audio = stems[size_t (which)].audio;
            if (audio.getNumSamples() == 0) return 0.0f;
            // The window loops, so a long render never runs off the end of it.
            return audio.getSample (channel, int (p % audio.getNumSamples()));
        }
    };

    StemSet gStems;
    // How many inputs the fake device offers: the real multitrack's channel count when one was
    // given, else the synthetic sixteen.
    int gInputs = kSyntheticInputs;

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
        int numInputChannels() override { return running ? gInputs : 0; }
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
        // The session writes itself down without being asked, and the toolbar says so. There is
        // no document here to write, so the fake states a write that has just landed - which is
        // the state every screen in the design is drawn in.
        juce::Time lastAutosave() override { return autosaved; }
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
        juce::Time autosaved { juce::Time::getCurrentTime() };
        juce::String input, output, sessionName { "Sunday" };
        juce::Array<SessionStore::Listing> sessions;
    };

    struct Rig
    {
        SampleLibrary samples;                  // before the controller: destroyed after it
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
            // Nothing may open by itself here: every state in this tool is a state somebody
            // asked for, and a first-run coach over the console would be in all of them.
            MainView::setAutoTutorial (false);
            MainView::setStoredThemeUsed (false);   // every render starts from the design, whatever this Mac chose
            samples.load();
            controller.setSampleBanks (samples.table());
            view = std::make_unique<MainView> (controller, services);
            view->setSize (1520, 960);
            view->setVisible (true);
            in.assign (size_t (gInputs), std::vector<float> (kBlock, 0.0f));
            ip.resize (size_t (gInputs));
            outL.resize (kBlock); outR.resize (kBlock);
        }

        void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

        // A church band on 16 inputs, matching the assignments made below - or the real
        // multitrack, when one was given (--stems).
        float sample (int input, long long p)
        {
            if (gStems.loaded()) return gStems.sample (input, p);

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
                for (int c = 0; c < gInputs; ++c)
                {
                    for (int i = 0; i < kBlock; ++i) in[size_t (c)][size_t (i)] = sample (c, pos + i);
                    ip[size_t (c)] = in[size_t (c)].data();
                }
                float* op[2] = { outL.data(), outR.data() };
                if (controller.isPrepared()) dawEngine.processBlock (ip.data(), gInputs, op, 2, kBlock);
                pos += kBlock;
                if ((b % 4) == 0) pump (1);
                else std::this_thread::sleep_for (std::chrono::microseconds (300));
            }
        }

        // Writes one short WAV per assigned track and puts it on the timeline, so the Tracks
        // workspace shows the waveforms it will show in the real application. The audio is
        // whatever `sample` is serving - the real service when one was given, else the tones.
        //
        // The window is written out rather than the clip pointing back at the original file:
        // a 150 MB service take is still being scanned for its thumbnail long after the render
        // has finished, and the clip photographs as a flat block.
        void recordSyntheticTake (const juce::File& folder, double seconds)
        {
            if (gStems.loaded()) seconds = juce::jmax (seconds, 30.0);   // a minute of service, not six seconds of tone
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

        // A clip draws as a flat block until its thumbnail has been scanned, and a real service
        // take is a hundred times the audio the synthetic one was. Give the scan its time
        // before anything is photographed, rather than photograph an empty timeline.
        void waitForWaveforms (int ms = 6000)
        {
            view->getTracksPage().primeThumbnails();
            const auto until = juce::Time::getMillisecondCounter() + juce::uint32 (ms);
            while (juce::Time::getMillisecondCounter() < until)
            {
                pump (50);
                if (view->getTracksPage().waveformsReady()) break;
            }
            view->getTracksPage().repaint();
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
//   dlive_ui_snapshots --paint  [channels=48] [frames=120]   the same, plus the paint tree
// ---------------------------------------------------------------------------
// The band on the desk. Given a real multitrack (--stems) every input is assigned from the
// file that recorded it, by the name the console wrote; otherwise the synthetic sixteen are
// assigned the way they always were. One place, so the snapshot walk and the size renders can
// never photograph two different consoles.
static void assignBand (MainView& view)
{
    auto& assign = view.getAssignPage();

    if (gStems.loaded())
    {
        for (const auto& stem : gStems.stems)
            assign.assign (stem.firstInput, stem.role, stem.name, stem.stereo);
        return;
    }

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
}

// The first three backing vocals on this console, by input number: 11, 12 and 13 on the
// synthetic band, and wherever the real multitrack put them.
static std::vector<int> backingVocalInputs()
{
    std::vector<int> picked;
    if (gStems.loaded())
    {
        for (const auto& stem : gStems.stems)
        {
            if (stem.role != ChannelRole::BackingVocal) continue;
            picked.push_back (stem.firstInput);
            if (picked.size() == 3) break;
        }
        return picked;
    }
    return { 11, 12, 13 };
}

// Where a page's paint actually goes, component by component. A workspace is a tree and a
// wall-clock total for the whole window says nothing about which branch of it to fix - the two
// real regressions in this work were both found by asking this question rather than by reading
// layout code. `--paint` prints the tree with the cost of each subtree, warm, so what is left
// after every cache has done its job is what shows up.
static void paintHotspots (juce::Component& c, juce::Image& canvas, int reps, int depth,
                           juce::Point<int> origin, juce::Rectangle<int> clip,
                           double inclusiveMs, double threshold)
{
    struct Child { juce::Component* c; double ms; juce::Point<int> origin; juce::Rectangle<int> clip; };
    std::vector<Child> children;

    for (auto* child : c.getChildren())
    {
        if (! child->isVisible() || child->getBounds().isEmpty()) continue;
        const auto within = clip.getIntersection (child->getBounds() + origin);
        if (within.isEmpty()) continue;
        children.push_back ({ child, 1.0e9, origin + child->getPosition(), within });
    }

    // Every sibling timed inside ONE pass over the whole row of them, repeated, keeping each
    // one's fastest pass. Timing a component on its own instead gets the answer wrong, and
    // wrong in the flattering direction: painted by itself it has JUCE's glyph caches to
    // itself, and the first thing this walk was used to find was text being laid out again
    // because a sibling had evicted it. A component costs what it costs with the rest of the
    // window drawing around it.
    for (int r = 0; r < reps; ++r)
    {
        juce::Graphics g (canvas);
        for (auto& child : children)
        {
            const auto t0 = std::chrono::steady_clock::now();
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (child.clip);
                g.setOrigin (child.origin);
                child.c->paintEntireComponent (g, true);
            }
            child.ms = std::min (child.ms, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
        }
    }

    double childTotal = 0.0;
    for (const auto& child : children) childTotal += child.ms;

    const auto name = [] (juce::Component& x)
    {
        auto n = x.getComponentID();
        if (n.isEmpty()) n = x.getName();
        if (n.isEmpty()) n = juce::String (typeid (x).name());
        return n;
    };

    // What this component draws itself, as opposed to what its children draw.
    const auto self = inclusiveMs - childTotal;
    if (self >= threshold)
        std::printf ("  %*s%-*s %6.2f ms drawn by %s itself\n", depth * 2, "",
                     juce::jmax (4, 34 - depth * 2), "(itself)", juce::jmax (0.0, self),
                     name (c).toRawUTF8());

    std::sort (children.begin(), children.end(), [] (const Child& a, const Child& b) { return a.ms > b.ms; });

    for (const auto& child : children)
    {
        if (child.ms < threshold) continue;
        std::printf ("  %*s%-*s %6.2f ms\n", depth * 2, "",
                     juce::jmax (4, 34 - depth * 2),
                     name (*child.c).substring (0, juce::jmax (4, 32 - depth * 2)).toRawUTF8(), child.ms);
        paintHotspots (*child.c, canvas, reps, depth + 1, child.origin, child.clip, child.ms, threshold);
    }
}

static int measureFrames (int channels, int frames, bool detail)
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

    struct Result { const char* name; double tickMs; double coldMs; double warmMs; long long layouts; };
    std::vector<Result> results;

    // Deliberately no window. Timing a real one on macOS measures the window server's vsync,
    // not DLIVE: the same run varies by an order of magnitude. Rendering into an image
    // measures only this application's own drawing, which is the thing a performance pass can
    // actually change and the thing a regression would show up in.

    const MainView::Page pages[] = { MainView::Page::Tracks, MainView::Page::Mixer,
                                     MainView::Page::Tune, MainView::Page::Live, MainView::Page::Inspector };
    const char* names[] = { "TRACKS", "MIXER", "TUNE", "LIVE", "INSPECTOR" };

    // Everything a page has cached, thrown away: a page switch or a resize arrives with every
    // buffered strip's image dirty, and that is the moment worth timing.
    const std::function<void (juce::Component&)> invalidate = [&invalidate] (juce::Component& c)
    {
        c.repaint();
        for (auto* child : c.getChildren())
            invalidate (*child);
    };

    const auto paintWholeWindow = [&view] (juce::Image& canvas)
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
    };

    // Every number below is the MINIMUM of `reps` rounds, not the mean. On a laptop the mean
    // of anything drifts about 15 % with the fan, uniformly across all five workspaces, so a
    // before/after taken minutes apart says nothing; the fastest round the machine managed is
    // the one measurement the thermal state cannot inflate. It also means two binaries no
    // longer have to be run alternately to be comparable.
    const int reps = 7;

    for (size_t p = 0; p < sizeof (pages) / sizeof (pages[0]); ++p)
    {
        view.showPage (pages[p]);
        rig.pump (120);

        // NativeImageType, not the default software one, and that is the whole difference
        // between measuring DLIVE and measuring a renderer DLIVE never uses. On macOS a window
        // paints through CoreGraphics, where a run of text goes to CTFontDrawGlyphs; a plain
        // juce::Image paints through JUCE's own software rasteriser, which builds an outline
        // per glyph behind a 128-entry cache and re-builds it the moment a page has more
        // distinct glyphs than that. A native image also makes every setBufferedToImage strip
        // cache itself the way it does on screen, because JUCE asks the context which image
        // type to use. Still no window, so still none of the window server's vsync.
        juce::Image canvas (juce::Image::ARGB, view.getWidth(), view.getHeight(), true,
                            juce::NativeImageType());

        double tick = 1.0e9, cold = 1.0e9, warm = 1.0e9;

        // What one timer tick costs: every refresh() on this page, with real audio underneath
        // so the meters really move and the pages really have new numbers. Measured in rounds
        // of its own, before any full repaint: a page arrives at its tick with its caches warm,
        // and timing the two interleaved measures the tick against a state it never meets.
        for (int r = 0; r < reps; ++r)
        {
            double tickTotal = 0.0;
            for (int f = 0; f < frames; ++f)
            {
                rig.feed (0.03);
                const auto t0 = std::chrono::steady_clock::now();
                rig.pump (1);
                tickTotal += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            }
            tick = std::min (tick, tickTotal / frames);
        }

        for (int r = 0; r < reps; ++r)
        {
            // COLD: the price of arriving on this page. Every cached strip image is dirty and
            // every string on it has to be laid out for the first time.
            invalidate (view);
            const auto c0 = std::chrono::steady_clock::now();
            paintWholeWindow (canvas);
            cold = std::min (cold, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - c0).count());

            // WARM: the same window again with nothing invalidated. What is left is the
            // drawing the caches cannot save - and the gap between the two is what a text or
            // image cache can actually win.
            const auto w0 = std::chrono::steady_clock::now();
            paintWholeWindow (canvas);
            warm = std::min (warm, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - w0).count());
        }

        // How many strings this workspace has to lay out to draw itself once from nothing.
        // A count, not a clock: it is the same on every machine and in every thermal state, so
        // it is the figure a frame-budget regression is actually caught by. A page that starts
        // building strings in paint() shows up here before it shows up in the milliseconds.
        Dine::clearTextCache();
        Dine::resetTextCacheStats();
        invalidate (view);
        paintWholeWindow (canvas);
        const auto layouts = Dine::textCacheStats().misses;

        results.push_back ({ names[p], tick, cold, warm, layouts });

        if (detail)
        {
            std::printf ("\n%s - where the warm paint goes (anything over 0.30 ms)\n", names[p]);
            paintHotspots (view, canvas, 3, 0, {}, view.getLocalBounds(), warm, 0.30);
        }
    }

    std::printf ("\nFRAME COST  (%d channels, %d frames, %d reps, %d x %d)\n",
                 channels, frames, reps, view.getWidth(), view.getHeight());
    std::printf ("  %-11s %10s %14s %10s %10s\n", "workspace", "tick ms", "cold repaint", "warm", "layouts");
    for (const auto& r : results)
        std::printf ("  %-11s %9.2f %13.2f %9.2f %10lld%s\n", r.name, r.tickMs, r.coldMs, r.warmMs, r.layouts,
                     (r.tickMs + r.coldMs) > 33.0 ? "   a page that repaints itself whole would miss the frame" : "");
    std::printf ("  every time is the fastest of %d rounds, so the fan cannot flatter it; layouts is\n"
                 "  a count and does not move at all.\n", reps);
    std::printf ("  tick = one refresh() of this page. cold = arriving on it, every cache dirty.\n"
                 "  warm = the same window again. a page must not call repaint() on itself per\n"
                 "  tick: at these sizes that alone spends the whole 33.3 ms a 30 Hz frame has.\n");
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
    assignBand (view);
    view.showPage (MainView::Page::Purpose);
    view.getPurposePage().onContinue();
    rig.feed (1.0);
    rig.recordSyntheticTake (dir.getChildFile ("take"), 6.0);
    view.getTracksPage().rebuild();
    rig.waitForWaveforms();
    view.getTracksPage().zoomToFit();
    rig.controller.startTuneMix ({ 4.0f, -45.0f, 5.0f });
    rig.feed (5.5);
    for (int i = 0; i < 100 && rig.controller.getStage() != MixController::Stage::Preview; ++i) { rig.controller.poll(); rig.pump (10); }
    rig.controller.keepPlan();
    rig.feed (0.5);

    // The three desk sizes the design is drawn at, and the smallest window the application
    // allows (app/Main.cpp's setResizeLimits): a layout that survives 1180 x 760 survives a
    // laptop in a booth with a stream window beside it.
    for (const auto& size : { std::pair<int, int> { 1180, 760 }, { 1280, 800 }, { 1440, 900 }, { 1920, 1080 } })
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

// ---------------------------------------------------------------------------
// TEXT SIZE
//
// View > Appearance > Text size scales every word and every number and no metric, so the
// thing to look at is a full console: does a 32-channel MIXER still show 32 channels at
// Larger, and does every name that no longer fits end in an ellipsis rather than running
// into its neighbour? Rendered at the smallest window the application allows, because that
// is where a bigger word runs out of room first.
//
//   dlive_ui_snapshots --text-sizes <dir>
// ---------------------------------------------------------------------------
static int renderTextSizes (const juce::File& dir)
{
    dir.createDirectory();
    Rig rig;
    auto& view = *rig.view;
    rig.services.openDevices ("Dante Virtual Soundcard", "Dante Virtual Soundcard");

    // Thirty-two sources with the long names a real console writes: the case the words have
    // to be ellipsised in.
    MixSession session;
    session.name = "Text size";
    const ChannelRole roles[] = {
        ChannelRole::KickIn, ChannelRole::SnareTop, ChannelRole::HiHat, ChannelRole::RackTom,
        ChannelRole::FloorTom, ChannelRole::Overhead, ChannelRole::Room, ChannelRole::BassDI,
        ChannelRole::Piano, ChannelRole::Organ, ChannelRole::SynthPad, ChannelRole::AcousticGuitar,
        ChannelRole::ElectricGuitarClean, ChannelRole::SaxTenor, ChannelRole::LeadVocal,
        ChannelRole::BackingVocal, ChannelRole::Choir, ChannelRole::Speech, ChannelRole::CrowdMic
    };
    for (int i = 0; i < 32; ++i)
    {
        InputAssignment a;
        a.role = roles[size_t (i) % (sizeof (roles) / sizeof (roles[0]))];
        a.name = (juce::String (channelRoleName (a.role)) + " - stage right " + juce::String (i + 1)).toStdString();
        a.inputA = i;
        session.inputs.push_back (a);
    }
    rig.controller.setSession (session);
    rig.services.reconfigure();
    rig.pump (120);

    view.setSize (1180, 760);            // app/Main.cpp's smallest allowed window
    for (const auto& size : ThemeStore::textSizes())
    {
        Dine::setTextScale (size.scale);
        Dine::refreshWindow (view);
        Dine::relayoutTree (view);
        const juce::String tag = juce::String (size.name).toLowerCase();
        for (const auto& page : { std::pair<MainView::Page, const char*> { MainView::Page::Tracks, "tracks" },
                                  { MainView::Page::Mixer,     "mixer" },
                                  { MainView::Page::Tune,      "tune" },
                                  { MainView::Page::Live,      "live" },
                                  { MainView::Page::Inspector, "inspector" } })
        {
            view.showPage (page.first);
            rig.pump (60);
            rig.snap (dir, tag + "-" + page.second);
        }
    }
    Dine::setTextScale (1.0f);
    rig.view.reset();
    return 0;
}

int main (int argc, char** argv)
{
    // The theme table and the token table must agree, and the design in AppTheme.h must be
    // exactly the built-in default - otherwise a fresh build and "Studio Teal" would differ.
    {
        int problems = 0;
        for (const auto& t : ThemeStore::tokens())
        {
            bool bound = false;
            for (const auto& b : Dine::themeBindings()) if (juce::String (b.key) == t.key) bound = true;
            if (! bound) { std::cerr << "theme key without a Dine:: binding: " << t.key << "\n"; ++problems; }
        }
        for (const auto& b : Dine::themeBindings())
            if (! ThemeStore::isToken (b.key)) { std::cerr << "Dine:: binding without a theme key: " << b.key << "\n"; ++problems; }
        const auto design = Dine::currentColours();
        for (const auto& kv : ThemeStore::resolve (ThemeStore::builtIn().front()))
            if (design.count (kv.first) == 0 || design.at (kv.first) != kv.second)
            {
                std::cerr << "AppTheme.h and the Studio Teal preset disagree on " << kv.first << ": "
                          << ThemeStore::hex (design.count (kv.first) ? design.at (kv.first) : 0) << " vs " << ThemeStore::hex (kv.second) << "\n";
                ++problems;
            }
        if (problems > 0) return 2;
    }

    // --theme <name> renders the whole set under one of the built-in (or saved) themes.
    // --stems <folder> renders every screen from a real service multitrack instead of the
    // synthetic band - which is what a render being compared against the design must use.
    juce::String themeName;
    juce::File stemsFolder;
    std::vector<char*> args (argv, argv + argc);
    for (size_t a = 1; a + 1 < args.size(); ++a)
        if (juce::String (args[a]) == "--theme") { themeName = args[a + 1]; args.erase (args.begin() + long (a), args.begin() + long (a) + 2); break; }
    for (size_t a = 1; a + 1 < args.size(); ++a)
        if (juce::String (args[a]) == "--stems") { stemsFolder = juce::File (juce::String (args[a + 1])); args.erase (args.begin() + long (a), args.begin() + long (a) + 2); break; }
    if (stemsFolder == juce::File() )
    {
        // So --sizes and --frames pick the same recording up without repeating it.
        const auto fromEnv = juce::SystemStats::getEnvironmentVariable ("DLIVE_STEMS", {});
        if (fromEnv.isNotEmpty()) stemsFolder = juce::File (fromEnv);
    }
    argc = int (args.size());
    argv = args.data();

    juce::ScopedJuceInitialiser_GUI juceInit;

    if (stemsFolder != juce::File())
    {
        // A minute in, so the band is playing rather than walking on. Thirty seconds is longer
        // than the longest listen a screen needs, and the window loops after that.
        if (const auto problem = gStems.load (stemsFolder, 30.0, 60.0); problem.isNotEmpty())
        {
            std::cerr << "--stems: " << problem << "\n";
            return 2;
        }
        gInputs = gStems.numInputs();
        std::printf ("stems: %d sources on %d inputs from %s\n", int (gStems.stems.size()), gInputs, gStems.source.toRawUTF8());
        for (const auto& stem : gStems.stems)
            std::printf ("  in %2d%-4s %-16s %-18s peak %6.1f dBFS%s\n",
                         stem.firstInput + 1, stem.stereo ? "/+1" : "", stem.name.toRawUTF8(),
                         channelRoleName (stem.role), stem.peakDb,
                         stem.peakDb < -60.0f ? "   (silent: this source is not in the recording)" : "");
    }
    if (argc > 1 && juce::String (argv[1]) == "--text-sizes")
        return renderTextSizes (juce::File (argc > 2 ? juce::String (argv[2])
                                                     : juce::File::getCurrentWorkingDirectory().getChildFile ("app-text-sizes").getFullPathName()));
    if (argc > 1 && juce::String (argv[1]) == "--sizes")
        return renderSizes (juce::File (argc > 2 ? juce::String (argv[2])
                                                 : juce::File::getCurrentWorkingDirectory().getChildFile ("app-sizes").getFullPathName()));
    // --frames  the five workspaces' frame cost.  --paint  the same, plus where it goes.
    if (argc > 1 && (juce::String (argv[1]) == "--frames" || juce::String (argv[1]) == "--paint"))
        return measureFrames (argc > 2 ? juce::String (argv[2]).getIntValue() : 48,
                              argc > 3 ? juce::String (argv[3]).getIntValue() : 120,
                              juce::String (argv[1]) == "--paint");

    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("app-snapshots").getFullPathName());
    dir.createDirectory();

    Rig rig;
    auto& view = *rig.view;
    if (themeName.isNotEmpty())
    {
        Dine::applyTheme (ThemeStore::find (themeName));
        Dine::refreshWindow (view);
        std::cout << "theme: " << Dine::currentThemeName() << "\n";
    }

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
            d.devices.consoleInput = "Dante Virtual Soundcard";
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
    // ROUTING's other two sections: where the sound leaves, and the patches this church has
    // saved. The first three sections are the pages above, photographed on their own already.
    view.showPage (MainView::Page::Outputs);
    rig.snap (dir, "01b-routing-outputs");
    view.showPage (MainView::Page::Maps);
    rig.snap (dir, "01c-routing-patches");
    // ...and what ROUTING looks like in the middle of a service: covered, with the one press
    // that uncovers it for this visit and nothing else.
    rig.dawEngine.setLiveSafe (true);
    view.showPage (MainView::Page::Device);
    rig.pump (60);
    rig.snap (dir, "01d-routing-live-safe");
    rig.dawEngine.setLiveSafe (false);
    view.showPage (MainView::Page::Device);

    rig.services.openDevices ("Dante Virtual Soundcard", "Dante Virtual Soundcard");
    view.showPage (MainView::Page::Assign);
    rig.snap (dir, "02-assign-empty");
    assignBand (view);
    rig.snap (dir, "03-assign");

    // Inputs picked out: the toolbar becomes the bulk one - set what they are, fill a kit
    // down them in order, name them from their role, link them as pairs, or drop them. Three
    // backing vocals are the case it was made for, so it is three backing vocals whichever
    // console this is.
    auto& assign = view.getAssignPage();
    assign.selectInputs (backingVocalInputs());
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
    rig.waitForWaveforms();
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
    rig.controller.linkStrips ({ 9, 10, 11 });
    rig.feed (0.3);
    rig.snap (dir, "06f-tracks-linked");
    rig.controller.unlinkStrip (9); rig.controller.unlinkStrip (10);

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

    // The focal source, pinned on the lead, so the rail shows what a mix built around one
    // source looks like: FOCUS beside it, and nothing beside anything else.
    rig.controller.setFocusInput (8);
    view.showPage (MainView::Page::Tune);
    rig.feed (0.5);
    rig.snap (dir, "07-tune-ready");

    // WHAT SHOULD DLIVE TUNE: the scope picker the verb opens with. All three, because the
    // whole point of it is that the two that were invisible are now the same size as the one
    // that was not.
    {
        auto& mix = view.getMixPage();
        mix.setScopeForSnapshot (1, -1);
        mix.pressTune();
        rig.feed (0.2);
        rig.snap (dir, "07f-tune-scope-group");
        mix.setScopeForSnapshot (2, -1);
        rig.feed (0.2);
        rig.snap (dir, "07g-tune-scope-channels");
        mix.setScopeForSnapshot (0, -1);
        rig.feed (0.2);
        rig.snap (dir, "07e-tune-scope-mix");
        mix.closeScopeSheet();
        mix.setScopeForSnapshot (0, -1);
        rig.controller.abortTuneMix();
        rig.feed (0.2);
    }

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
    {
        // HISTORY: TUNE MIX's record on the kick, then a hand edit of its high-pass, newest
        // first, each with PUT BACK. The column is scrolled to the section.
        auto edited = rig.controller.getKept().strips[0].channel;
        edited.hpfEnabled = true;
        edited.hpfHz = 80.0f;
        rig.controller.setStripChannel (0, edited);
        rig.feed (0.3);
        view.getAdvancedPage().revealHistory();
        rig.feed (0.2);
        rig.snap (dir, "11d-inspector-history");
        view.getAdvancedPage().select (0);       // back to the top of the column for the shots that follow
    }
    view.getAdvancedPage().selectStage (4);  // corrective EQ: the curve, its nodes and the band cards (the kick's chain has a sample stage before it)
    rig.feed (0.3);
    rig.snap (dir, "11b-inspector-eq");
    view.getAdvancedPage().selectStage (6);  // the compressor: its in/out line and the live reduction
    rig.feed (0.3);
    rig.snap (dir, "11c-inspector-comp");
    {
        // SAMPLE: the kick's stage switched on at the profile's blend, with the built-in sounds
        // in its list; the path's chip and the strip along the foot say so too.
        auto withSample = rig.controller.getKept().strips[0].channel;
        withSample.replaceEnabled = true;
        rig.controller.setStripChannel (0, withSample);
        view.getAdvancedPage().selectStage (3);
        rig.feed (0.5);
        rig.snap (dir, "11e-inspector-sample");
        withSample.replaceEnabled = false;
        rig.controller.setStripChannel (0, withSample);
        rig.feed (0.2);
    }
    view.getAdvancedPage().selectStage (0);  // back to the input, so the next channel opens where it left off
    view.getAdvancedPage().select (1); // Snare — often has a snare-plate send after Tune
    rig.feed (0.3);
    rig.snap (dir, "12-inspector-send");
    view.getAdvancedPage().selectStage (10); // the sends close the path where the session uses FX
    rig.feed (0.3);
    rig.snap (dir, "12b-inspector-sends");
    {
        // EFFECTS ON THIS MICROPHONE: the pastor's handheld, dry for preaching and then in the
        // plate for the song he has just started, without anything being re-routed. Found by
        // role rather than by index, because a real multitrack patches its own way.
        int pastor = -1;
        const auto& g = rig.controller.getGraph();
        for (int i = 0; i < g.numStrips(); ++i)
            if (g.strips[size_t (i)].role == ChannelRole::Speech) { pastor = i; break; }
        if (pastor >= 0)
        {
            view.getAdvancedPage().selectStage (0);
            view.getAdvancedPage().select (pastor);
            rig.feed (0.3);
            rig.snap (dir, "12c-inspector-effects-off");
            rig.controller.setStripEffects (pastor, true);
            rig.feed (0.3);
            rig.snap (dir, "12d-inspector-effects-on");
            rig.controller.setStripEffects (pastor, false);
            rig.feed (0.2);
        }
        view.getAdvancedPage().select (1);
    }
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
    view.getAdvancedPage().setTrailShown (false);
    rig.feed (0.3);
    rig.snap (dir, "14b-inspector-panels-folded");
    view.setSidebarShown (true);
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

    // THE SOLO BAND. Whatever is soloed - a channel, a group, a return - says so under the
    // toolbar on every workspace, so an S left down on MIXER is not invisible from TRACKS.
    // Shot on a workspace that is not the one the S was pressed on, which is the whole point.
    rig.controller.setBusSolo (MixBus::Drums, true);
    rig.controller.setFxSolo (FxSlot::VocalPlate, true);
    view.updateChromeForSnapshot();
    rig.feed (0.4);
    rig.snap (dir, "15j-mixer-solo-bar");
    view.showPage (MainView::Page::Tracks);
    rig.feed (0.4);
    rig.snap (dir, "15k-tracks-solo-bar");
    view.showPage (MainView::Page::Mixer);
    rig.controller.clearSolos();
    view.updateChromeForSnapshot();
    rig.feed (0.3);

    rig.controller.setStripMute (2, false);
    rig.controller.setStripSolo (10, false);
    // Linked faders: the three backing vocals move together, and the console says so beside their names.
    rig.controller.linkStrips ({ 9, 10, 11 });
    rig.controller.setStripFader (9, -4.0f);
    view.getMixerPage().setView (MixerPage::View::List);
    rig.feed (0.3);
    rig.snap (dir, "15h-mixer-linked");
    view.getMixerPage().setView (MixerPage::View::Strips);
    rig.feed (0.3);
    // ... and in the column layout, at its narrowest, after the strips have repainted a few
    // times over: the mark sits at the right end of the name row and the name is still there.
    // (A paint that trimmed the strip's own layout took 18 px off the name on every frame.)
    view.getMixerPage().setStripSize (MixerPage::Size::Narrow);
    for (int i = 0; i < 4; ++i) { rig.controller.setStripMute (9, i % 2 == 0); rig.feed (0.1); }
    rig.snap (dir, "15i-mixer-linked-narrow");
    view.getMixerPage().setStripSize (MixerPage::Size::Normal);
    rig.feed (0.2);
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

    // TUNE with the shared channel list folded away: the mix, its meters and its macros
    // with the whole window.
    view.showPage (MainView::Page::Tune);
    view.setSidebarShown (false);
    rig.feed (0.3);
    rig.snap (dir, "16b-tune-rail-folded");
    view.setSidebarShown (true);
    rig.controller.keepPlan();
    view.getMixPage().setMacroValue (MixMacro::Space, 72.0f);
    view.getMixPage().setMacroValue (MixMacro::Drums, 30.0f);
    rig.feed (0.5);
    rig.snap (dir, "16-tune-macros");
    // The same pads under LIVE SAFE: each macro fenced to the plan's neighbourhood, the fence hatched.
    rig.dawEngine.setLiveSafe (true);
    view.getMixPage().setMacroValue (MixMacro::Bass, 62.0f);
    rig.feed (0.5);
    rig.snap (dir, "16c-tune-macros-live-safe");
    rig.dawEngine.setLiveSafe (false);
    rig.feed (0.3);

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

    // SCENES: the band's mix kept, then the pastor's; two pads lit, two empty. And the
    // emergency keys: DIM lit on the toolbar while the broadcast is 20 dB down.
    rig.controller.keepScene (0);
    rig.controller.setStripFader (0, -20.0f);
    rig.controller.setStripFader (12, 3.0f);
    rig.controller.keepScene (1);
    rig.controller.recallScene (0);
    rig.controller.setBroadcastDim (true);
    view.updateChromeForSnapshot();
    rig.feed (0.5);
    rig.snap (dir, "17c-live-scenes-dimmed");
    rig.controller.setBroadcastDim (false);
    view.updateChromeForSnapshot();
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
    view.closeSheetsForSnapshot();
    // CHECK INPUTS: every assigned input with its level and one word, at the moment the band is playing.
    view.showCheck();
    rig.feed (3.5);                         // three seconds decide "silent"
    rig.snap (dir, "18b-check-inputs");
    view.closeSheetsForSnapshot();
    rig.feed (0.2);


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

    // AUTOPILOT, holding the mix: the band under the toolbar that says so on every workspace,
    // what it has had to move, and the one press that stops it.
    rig.controller.setAutopilot (true);
    view.updateChromeForSnapshot();
    rig.feed (0.5);
    rig.snap (dir, "27d-autopilot");
    view.showPage (MainView::Page::Tracks);
    rig.feed (0.4);
    rig.snap (dir, "27e-autopilot-tracks");
    rig.controller.setAutopilot (false);
    view.showPage (MainView::Page::Mixer);
    view.updateChromeForSnapshot();
    rig.feed (0.3);

    // MIX HISTORY, with the favourites at the top of it: the mixes somebody said worked, what
    // each one sounded like, and the one press that aims the next tune at one of them.
    rig.controller.markFavourite ("Sunday 09:30 - the one");
    rig.controller.setStripFader (10, -2.0f);
    rig.controller.markFavourite ("After the choir came in");
    view.showHistory();
    rig.feed (0.4);
    rig.snap (dir, "27c-mix-history-favourites");
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

    // ---- FIRST SUNDAY: what a volunteer meets the first time they open DLIVE.
    view.closeSheets();
    rig.view->setSize (1520, 960);
    view.showTutorial();
    rig.feed (0.3);
    rig.snap (dir, "28-getting-started");
    view.closeTutorial();

    // ---- APPEARANCE: every built-in theme on the console, and the sheet itself. A theme
    // that breaks a page shows here before it reaches a booth.
    if (themeName.isEmpty())
    {
        view.closeTutorial();
        view.closeSheets();
        view.showPage (MainView::Page::Mixer);
        for (const auto& t : ThemeStore::builtIn())
        {
            Dine::applyTheme (t);
            Dine::refreshWindow (view);
            rig.pump (60);
            rig.snap (dir, "30-theme-" + juce::File::createLegalFileName (t.name.toLowerCase().replaceCharacter (' ', '-')));
        }
        Dine::applyTheme (ThemeStore::builtIn().front());
        Dine::refreshWindow (view);
        view.showThemes();
        rig.pump (60);
        rig.snap (dir, "31-appearance");
        view.closeSheets();
    }

    std::printf ("stage %d, health %d%%\n", int (rig.controller.getStage()), rig.controller.getMixHealthPercent());
    rig.view.reset();
    return 0;
}
