// THE 2026-10-05 RELIABILITY PASS: five things found in real use, each pinned down here.
//
//   Stereo     a pair linked on the Inputs page recorded as a mono take of its left side
//   Undo       Cmd+Z put back a stale timeline; a fader move was never an undo step at all
//   Export     the progress MixBounce reports was thrown away
//   Permission a device error could be dressed as a microphone refusal, and a refusal opened silent
//   Drum kits  one choice for the kick, the snare and the toms
//
// The window-level halves (Cmd+Z per workspace) are in ReachabilityTests.cpp.
#include "TestFramework.h"
#include "native/DawEngine.h"
#include "native/DeviceState.h"
#include "native/DrumKits.h"
#include "native/ExportProgress.h"
#include "native/InputMapStore.h"
#include "native/MixBounce.h"
#include "native/Recorder.h"
#include "native/SampleLibrary.h"
#include "native/SessionState.h"
#include "native/SessionStore.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;
    constexpr int kChannels = 12;
    constexpr float kLeft = 0.25f, kRight = -0.5f;   // two values that cannot be mistaken for each other

    juce::File scratch (const char* name)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dine-tests").getChildFile (name);
        f.deleteRecursively();
        f.createDirectory();
        return f;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (const juce::File& f)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (f));
    }

    // Twelve device channels: 10 carries kLeft, 11 carries kRight, the rest silence.
    struct Desk
    {
        std::vector<std::vector<float>> in;
        std::vector<const float*> ip;
        std::vector<float> l, r;
        float* op[2];
        Desk() : in (size_t (kChannels), std::vector<float> (size_t (kBlock), 0.0f)), l (size_t (kBlock)), r (size_t (kBlock))
        {
            std::fill (in[10].begin(), in[10].end(), kLeft);
            std::fill (in[11].begin(), in[11].end(), kRight);
            for (auto& c : in) ip.push_back (c.data());
            op[0] = l.data();
            op[1] = r.data();
        }
        void run (DawEngine& daw, int blocks) { for (int b = 0; b < blocks; ++b) daw.processBlock (ip.data(), kChannels, op, 2, kBlock); }
    };

    MixSession twoMonoKeys()
    {
        MixSession s;
        s.name = "Stereo";
        s.inputs = { { "Keys L", ChannelRole::Piano, 10, -1 }, { "Keys R", ChannelRole::Piano, 11, -1 } };
        return s;
    }

    MixSession stereoKeys()
    {
        MixSession s;
        s.name = "Stereo";
        s.inputs = { { "Keys", ChannelRole::Piano, 10, 11 } };
        return s;
    }

    juce::File writeTone (const juce::File& folder, const juce::String& name, double seconds)
    {
        const auto file = folder.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), kSr, 1, 24, {}, 0));
        stream.release();
        juce::AudioBuffer<float> b (1, int (seconds * kSr));
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.4f * std::sin (2.0f * float (M_PI) * 220.0f * float (i) / float (kSr)));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }
}

// =========================================================================== STEREO
// A STEREO SOURCE IS TWO CHANNELS, LINKED (2026-10-07). Each side is its own strip on the
// console and its own track on the timeline, recorded to its own mono file; the two are linked
// (fader and solo together) and spread hard left and right. Nothing shows a pair as one channel.
TEST_CASE ("Stereo: a pair linked on the Inputs page is two channels, linked, and records both sides to their own files")
{
    const auto folder = scratch ("stereo-link");
    MixController controller;
    DawEngine daw (controller);
    controller.setSession (twoMonoKeys());
    daw.setSession (twoMonoKeys());
    controller.prepare (kSr, kBlock);
    daw.prepare (kSr, kBlock);
    auto project = daw.getProject();
    project.folder = folder;
    daw.setProject (project);

    // Linked on the Inputs page, before the graph is rebuilt: the console splits it ...
    controller.setSession (stereoKeys());
    const auto& inputs = controller.getSession().inputs;
    REQUIRE (inputs.size() == 2);
    CHECK (inputs[0].name == "Keys L");
    CHECK (inputs[1].name == "Keys R");
    CHECK (inputs[0].inputA == 10);
    CHECK (inputs[1].inputA == 11);
    CHECK (! inputs[0].isStereo());
    CHECK (! inputs[1].isStereo());
    CHECK (inputs[0].stereoSide == -1);
    CHECK (inputs[1].stereoSide == 1);
    CHECK (controller.getGraph().numStrips() == 2);
    CHECK (controller.getStripLink (0) != 0);
    CHECK (controller.getStripLink (0) == controller.getStripLink (1));
    CHECK (controller.getKept().strips[0].pan == -1.0f);
    CHECK (controller.getKept().strips[1].pan == 1.0f);

    // ... and so does the timeline, whichever copy of the session it is handed.
    daw.setSession (stereoKeys());
    REQUIRE (daw.getProject().tracks.size() == 2);
    daw.toggleArmed (0);                              // one press arms the pair
    CHECK (daw.getProject().tracks[0].armed);
    CHECK (daw.getProject().tracks[1].armed);

    REQUIRE (daw.startRecording().isEmpty());
    Desk desk;
    desk.run (daw, 60);
    REQUIRE (daw.stopRecording() == 2);
    const auto& after = daw.getProject();
    for (int side = 0; side < 2; ++side)
    {
        REQUIRE (after.tracks[size_t (side)].clips.size() == 1);
        auto take = reader (after.fileFor (after.tracks[size_t (side)].clips[0]));
        REQUIRE (take != nullptr);
        CHECK (take->numChannels == 1);               // each side its own mono file
        juce::AudioBuffer<float> audio (1, int (take->lengthInSamples));
        take->read (&audio, 0, audio.getNumSamples(), 0, true, false);
        CHECK_NEAR (audio.getSample (0, 1000), side == 0 ? kLeft : kRight, 1.0e-4);
    }
    CHECK (after.tracks[0].clips[0].file != after.tracks[1].clips[0].file);
    folder.deleteRecursively();
}

TEST_CASE ("Stereo: each side has its own fader and meter, the faders move together, and the left stays left")
{
    MixController controller;
    DawEngine daw (controller);
    controller.setSession (stereoKeys());
    daw.setSession (controller.getSession());
    controller.prepare (kSr, kBlock);
    daw.prepare (kSr, kBlock);
    REQUIRE (controller.getGraph().numStrips() == 2);

    controller.setStripFader (0, -8.0f);
    CHECK_NEAR (controller.getKept().strips[1].faderDb, -8.0f, 1.0e-3);   // linked
    controller.setStripMute (1, true);
    CHECK (! controller.getKept().strips[0].mute);                         // mute stays each side's own

    controller.setStripMute (1, false);
    Desk desk;
    std::fill (desk.in[11].begin(), desk.in[11].end(), 0.0f);
    double el = 0.0, er = 0.0;
    long long t = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < kBlock; ++i, ++t)
            desk.in[10][size_t (i)] = 0.3f * std::sin (2.0f * float (M_PI) * 2000.0f * float (t) / float (kSr));
        daw.processBlock (desk.ip.data(), kChannels, desk.op, 2, kBlock);
        if (b < 200) continue;
        for (int i = 0; i < kBlock; ++i) { el += std::fabs (desk.l[size_t (i)]); er += std::fabs (desk.r[size_t (i)]); }
    }
    CHECK (el > 1.0e-3);
    CHECK (el > er * 3.0);                            // the left side is panned left, not centre
}

TEST_CASE ("Stereo: a session saved with a stereo strip opens as the linked pair, its timeline and its mix split with it")
{
    // What a version 10 file holds: one input on 10 and 11, one track with a two-channel take.
    SessionState old;
    old.session = stereoKeys();
    old.project.syncTracks (old.session);
    AudioClip clip;
    clip.name = "Keys";
    clip.file = "Keys_001.wav";
    clip.length = 4800;
    old.project.tracks[0].clips.push_back (clip);
    old.hasMix = true;
    old.mix.numStrips = 1;
    old.mix.strips[0].faderDb = -4.5f;
    old.mix.strips[0].channel.compThresholdDb = -17.0f;

    MixController controller;
    DawEngine daw (controller);
    applySession (old, controller, daw);
    const auto& s = controller.getSession();
    REQUIRE (s.inputs.size() == 2);
    CHECK (s.inputs[0].stereoSide == -1);
    CHECK (s.inputs[1].stereoSide == 1);
    for (int side = 0; side < 2; ++side)
    {
        CHECK_NEAR (controller.getKept().strips[size_t (side)].faderDb, -4.5f, 1.0e-3);   // the pair's level...
        CHECK_NEAR (controller.getKept().strips[size_t (side)].channel.compThresholdDb, -17.0f, 1.0e-3);   // ...and chain
    }
    CHECK (controller.getStripLink (0) != 0);
    CHECK (controller.getStripLink (0) == controller.getStripLink (1));
    const auto& tracks = daw.getProject().tracks;
    REQUIRE (tracks.size() == 2);
    REQUIRE (tracks[0].clips.size() == 1);
    REQUIRE (tracks[1].clips.size() == 1);
    CHECK (tracks[0].clips[0].file == "Keys_001.wav");
    CHECK (tracks[0].clips[0].fileChannel == 0);      // the take's left channel ...
    CHECK (tracks[1].clips[0].file == "Keys_001.wav");
    CHECK (tracks[1].clips[0].fileChannel == 1);      // ... and its right

    // Saved again and reopened, it is the same pair, link and all.
    const auto state = captureSession (controller, daw, DeviceChoice {}, 0);
    SessionState back;
    REQUIRE (SessionStore::fromVar (SessionStore::toVar (state), back));
    REQUIRE (back.session.inputs.size() == 2);
    CHECK (back.session.inputs[0].stereoSide == -1);
    CHECK (back.session.inputs[1].stereoSide == 1);
    CHECK (back.mix.strips[0].linkGroup != 0);
    CHECK (back.mix.strips[0].linkGroup == back.mix.strips[1].linkGroup);

    // A saved input map keeps the pair.
    InputMap map = InputMapStore::fromSession (s, "Sunday", "Dante Virtual Soundcard", 64, false);
    InputMap read;
    REQUIRE (InputMapStore::fromVar (InputMapStore::toVar (map), read));
    REQUIRE (read.inputs.size() == 2);
    CHECK (read.inputs[0].stereoSide == -1);
    CHECK (read.inputs[1].stereoSide == 1);
}

TEST_CASE ("Stereo: a two-channel take cut off by a crash goes back on the pair it became, wherever that pair is now")
{
    const auto folder = scratch ("stereo-crash");
    const auto audio = folder.getChildFile ("Audio Files");
    Recorder recorder (0.02, 0.02);               // sidecar and header kept current every 20 ms
    REQUIRE (recorder.start (audio, { { 0, "Keys", 10, 11 } }, kSr, 0).isEmpty());
    Desk desk;
    for (int b = 0; b < 200; ++b) recorder.write (desk.ip.data(), kChannels, kBlock);
    juce::Thread::sleep (300);

    const auto crashed = scratch ("stereo-crash-copy");
    const auto crashedAudio = crashed.getChildFile ("Audio Files");
    crashedAudio.createDirectory();
    for (const auto& f : audio.findChildFiles (juce::File::findFiles, false))
        f.copyFileTo (crashedAudio.getChildFile (f.getFileName()));
    recorder.stop();

    // Reopened with the keys second, and split into their two sides.
    MixSession s;
    s.name = "Stereo";
    s.inputs = { { "Lead", ChannelRole::LeadVocal, 4, -1 }, { "Keys", ChannelRole::Piano, 10, 11 } };
    MixController controller;
    DawEngine daw (controller);
    controller.setSession (s);
    daw.setSession (s);
    auto project = daw.getProject();
    project.folder = crashed;
    daw.setProject (project);
    REQUIRE (daw.getProject().tracks.size() == 3);
    const auto found = daw.recoverUnfinishedTakes();
    REQUIRE (found.size() == 1);
    CHECK (found[0].repaired);
    const auto& tracks = daw.getProject().tracks;
    CHECK (tracks[0].clips.empty());                          // not under the lead's name
    REQUIRE (tracks[1].clips.size() == 1);
    REQUIRE (tracks[2].clips.size() == 1);
    CHECK (tracks[1].clips[0].fileChannel == 0);
    CHECK (tracks[2].clips[0].fileChannel == 1);
    folder.deleteRecursively();
    crashed.deleteRecursively();
}

// =========================================================================== UNDO
TEST_CASE ("Undo: one fader drag is one step, and Cmd+Z takes back that move and nothing else")
{
    MixSession s;
    s.name = "Undo";
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Lead", ChannelRole::LeadVocal, 1, -1 } };
    MixController c;
    c.setSession (s);
    c.prepare (kSr, kBlock);
    double now = 1000.0;
    c.setClockForTests ([&now] { return now; });

    const auto start = c.getKept();
    // A drag: two hundred values, ten milliseconds apart, on one fader.
    for (int i = 0; i < 200; ++i) { c.setStripFader (1, -10.0f + 0.05f * float (i)); now += 10.0; }
    REQUIRE (c.canUndoMix());
    CHECK (c.undoMixLabel() == "Lead fader");

    // Then an EQ move on another channel, after a pause.
    now += 3000.0;
    auto chain = c.getKept().strips[0].channel;
    chain.hpfHz += 25.0f;
    for (int i = 0; i < 30; ++i) { c.setStripChannel (0, chain); now += 15.0; }   // a knob turned slowly
    const float leadAfterDrag = c.getKept().strips[1].faderDb;

    c.undoMix();                                                    // the EQ, all of it, and only it
    CHECK_NEAR (c.getKept().strips[0].channel.hpfHz, start.strips[0].channel.hpfHz, 1.0e-3);
    CHECK_NEAR (c.getKept().strips[1].faderDb, leadAfterDrag, 1.0e-4);
    c.undoMix();                                                    // the whole drag, in one step
    CHECK_NEAR (c.getKept().strips[1].faderDb, start.strips[1].faderDb, 1.0e-4);
    CHECK (MixPlanner::countParameterChanges (start, c.getKept()) == 0);
    CHECK (! c.canUndoMix());                                       // and never past where it began

    // The same fader again after a pause is a new step, not more of the old one.
    c.redoMix();
    now += 5000.0;
    c.setStripFader (1, 0.0f);
    c.undoMix();
    CHECK_NEAR (c.getKept().strips[1].faderDb, leadAfterDrag, 1.0e-4);
}

TEST_CASE ("Undo: nothing a mix undo does reaches the inputs, the timeline, the solo or the emergency keys")
{
    MixSession s;
    s.name = "Undo";
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Keys", ChannelRole::Piano, 2, 3 } };
    MixController c;
    DawEngine daw (c);
    c.setSession (s);
    daw.setSession (s);
    c.prepare (kSr, kBlock);
    daw.prepare (kSr, kBlock);
    auto project = daw.getProject();
    project.markers.push_back ({ "Sermon", 48000 });
    daw.setProject (project);

    c.setStripFader (0, -12.0f);
    c.setBroadcastDim (true);
    c.setStripSolo (1, true);
    c.undoMix();
    CHECK (c.getSession().inputs.size() == 3);       // Kick, Keys L, Keys R
    CHECK (c.getSession().inputs[2].inputA == 3);
    CHECK (daw.getProject().markers.size() == 1);
    CHECK (c.isBroadcastDimmed());                   // never kept, never undone
    c.setBroadcastDim (false);
}

TEST_CASE ("Undo: a timeline edit only comes back in the timeline it was made in")
{
    // The epoch is what TracksPage's undo is built on: it moves for everything that is not an
    // edit, so an old entry can never put back clips from before a take, another layout or
    // another session.
    MixController c;
    DawEngine daw (c);
    daw.setSession (twoMonoKeys());
    const auto e0 = daw.getTimelineEpoch();
    daw.setSession (twoMonoKeys());                 // the same channels: not a new timeline
    CHECK (daw.getTimelineEpoch() == e0);
    daw.setSession (MixSession {});                 // re-laid out: it is
    const auto e1 = daw.getTimelineEpoch();
    {
        MixSession one;
        one.inputs = { { "Keys", ChannelRole::Piano, 10, -1 } };
        daw.setSession (one);
    }
    CHECK (e1 != e0);
    daw.setProject (daw.getProject());              // a session opened, new or imported
    CHECK (daw.getTimelineEpoch() != e1);

    // Putting edits back touches the clips and markers only - never the folder, the rate,
    // arming, monitoring or LIVE SAFE - and refuses a shape that no longer matches.
    auto project = daw.getProject();
    project.folder = scratch ("epoch");
    project.tracks[0].armed = true;
    daw.setProject (project);
    const auto e2 = daw.getTimelineEpoch();
    std::vector<std::vector<AudioClip>> clips (1);
    clips[0].push_back ({});
    clips[0][0].file = "a.wav";
    clips[0][0].length = 100;
    CHECK (daw.restoreEdits (clips, { { "M", 10 } }));
    CHECK (daw.getTimelineEpoch() == e2);            // an undo is an edit, not a new timeline
    CHECK (daw.getProject().folder == project.folder);
    CHECK (daw.getProject().tracks[0].armed);
    CHECK (daw.getProject().tracks[0].clips.size() == 1);
    CHECK (! daw.restoreEdits ({}, {}));              // the wrong number of tracks: nothing happens
    CHECK (daw.getProject().tracks[0].clips.size() == 1);
    project.folder.deleteRecursively();
}

// =========================================================================== EXPORT
TEST_CASE ("Export: progress is reported, stays under 100 % until the file is closed, and a stop leaves nothing")
{
    const auto folder = scratch ("export-progress");
    MixSession s;
    s.name = "Export";
    s.inputs = { { "Lead", ChannelRole::LeadVocal, 0, -1 } };
    MixController controller;
    controller.setSession (s);
    controller.prepare (kSr, kBlock);
    Project project;
    project.sampleRate = kSr;
    project.syncTracks (s);
    AudioClip clip;
    clip.file = writeTone (folder, "Lead.wav", 2.0).getFullPathName();
    clip.length = juce::int64 (2.0 * kSr);
    clip.fileSampleRate = kSr;
    project.tracks[0].clips.push_back (clip);

    ExportProgress p;
    p.state.store (int (ExportProgress::State::Running));
    std::vector<MixBounce::Stage> stages;
    float lowest = 2.0f, highest = -1.0f;
    MixBounce::Options o;
    o.loudness = MixBounce::Loudness::Stream14;     // a second, measured pass
    o.onStage = [&] (MixBounce::Stage st, bool measurable) { stages.push_back (st); p.beginStage (st, measurable); };
    o.onProgress = [&] (float f) { if (f >= 0.0f) { lowest = std::min (lowest, f); highest = std::max (highest, f); } return p.report (f); };
    const auto dest = folder.getChildFile ("Mix.wav");
    REQUIRE (MixBounce::renderProject (s, controller.getKept(), project, dest, MixBounce::Format::Wav, o).isEmpty());
    CHECK (dest.existsAsFile());
    CHECK (lowest >= 0.0f);
    CHECK (highest <= 1.0f);
    REQUIRE (stages.size() >= 2);
    CHECK (stages.front() == MixBounce::Stage::Mixing);
    CHECK (std::find (stages.begin(), stages.end(), MixBounce::Stage::Loudness) != stages.end());
    // The words the status foot shows. 100 % is never said while the worker is still inside.
    p.beginStage (MixBounce::Stage::Mixing, true);
    p.report (1.0f);
    CHECK (exportStatusText (p, MixBounce::What::StereoMix) == "mixing 99%");
    p.report (0.42f);
    CHECK (exportStatusText (p, MixBounce::What::StereoMix) == "mixing 42%");
    CHECK (exportStatusText (p, MixBounce::What::GroupStems) == "writing the stems 42%");
    p.beginStage (MixBounce::Stage::Encoding, false);
    CHECK (exportStatusText (p, MixBounce::What::StereoMix) == "making the MP3");   // no number to give
    p.state.store (int (ExportProgress::State::Done));
    CHECK (exportStatusText (p, MixBounce::What::StereoMix) == "done");
    p.state.store (int (ExportProgress::State::Failed));
    CHECK (exportStatusText (p, MixBounce::What::StereoMix) == "failed");
    p.state.store (int (ExportProgress::State::Idle));
    CHECK (exportStatusText (p, MixBounce::What::StereoMix).isEmpty());

    // Stopped part-way: the answer says so, and no half-made file is left looking finished.
    ExportProgress stop;
    int calls = 0;
    MixBounce::Options o2;
    o2.onProgress = [&] (float f) { if (++calls == 5) stop.cancel.store (true); return stop.report (f); };
    const auto dest2 = folder.getChildFile ("Stopped.wav");
    CHECK (MixBounce::renderProject (s, controller.getKept(), project, dest2, MixBounce::Format::Wav, o2) == "Export cancelled.");
    CHECK (! dest2.existsAsFile());
    o2.what = MixBounce::What::RawMultitrack;
    calls = 0;
    stop.cancel.store (false);
    CHECK (MixBounce::renderProject (s, controller.getKept(), project, dest2, MixBounce::Format::Wav, o2) == "Export cancelled.");
    CHECK (folder.getChildFile ("Stopped multitrack").getNumberOfChildFiles (juce::File::findFiles) == 0);
    folder.deleteRecursively();
}

// =========================================================================== PERMISSION
TEST_CASE ("Microphone: the input is opened only when macOS has said yes, and every other answer has its own sentence")
{
    CHECK (inputAccessFor (MicPermission::State::Granted) == InputAccess::Listen);
    CHECK (inputAccessFor (MicPermission::State::Undetermined) == InputAccess::AskFirst);   // explained, then asked once
    CHECK (inputAccessFor (MicPermission::State::Denied) == InputAccess::Refused);          // never re-prompted
    CHECK (inputAccessFor (MicPermission::State::Restricted) == InputAccess::Restricted);
    CHECK (inputAccessFor (MicPermission::State::Granted, false) == InputAccess::AskFirst); // "Not now" asks nothing

    // A device that would not open is the device's problem, in its own words - never the
    // microphone switch, which would send somebody to the wrong screen at 9:55 on a Sunday.
    const auto busy = inputRefusedSentence ("Dante Virtual Soundcard", "MacBook Pro Speakers", false,
                                            "The input and output devices don't share a common sample rate");
    CHECK (busy.contains ("Dante Virtual Soundcard would not open its inputs"));
    CHECK (busy.contains ("common sample rate"));
    CHECK (! busy.contains ("Privacy"));
    CHECK (busy.contains ("MacBook Pro Speakers"));           // and where the mix is going meanwhile

    const auto refused = inputRefusedSentence ("Dante Virtual Soundcard", "Dante Virtual Soundcard", true, {});
    CHECK (refused.contains ("Privacy & Security > Microphone"));
    CHECK (refused.contains ("as soon as it is on"));          // no relaunch: DINE notices (retryHeldInput)

    const auto managed = inputRestrictedSentence ("MacBook Pro Speakers");
    CHECK (managed.contains ("managed"));
    CHECK (managed.contains ("MacBook Pro Speakers"));

    const auto notAsked = inputsNotAskedSentence ("SQ-6 USB", "SQ-6 USB");
    CHECK (notAsked.contains ("SQ-6 USB"));
    CHECK (! notAsked.contains ("Privacy"));
}

// =========================================================================== DRUM KITS
TEST_CASE ("Drum kits: every kit names sounds DINE ships, and choosing one sets every drum by name as one step")
{
    SampleLibrary library;
    library.load();
    if (library.slotFor (RoleFamily::Kick, "Perfect kick", false) < 0) return;   // no built-in folder on this machine

    for (const auto& kit : builtInDrumKits())
    {
        CHECK (library.slotFor (RoleFamily::Kick, kit.kick, false) >= 0);
        CHECK (library.slotFor (RoleFamily::Snare, kit.snare, false) >= 0);
        for (const auto& t : kit.rackToms) CHECK (library.slotFor (RoleFamily::Tom, t, false) >= 0);
        CHECK (library.slotFor (RoleFamily::Tom, kit.floorTom, false) >= 0);
    }

    MixSession s;
    s.name = "Kit";
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Snare", ChannelRole::SnareTop, 1, -1 },
                 { "Tom 1", ChannelRole::RackTom, 2, -1 }, { "Tom 2", ChannelRole::RackTom, 3, -1 },
                 { "Floor", ChannelRole::FloorTom, 4, -1 }, { "Lead", ChannelRole::LeadVocal, 5, -1 } };
    MixController c;
    DawEngine daw (c);
    c.setSession (s);
    daw.setSession (s);
    c.setSampleBanks (library.table());
    c.prepare (kSr, kBlock);

    // The snare's stage on, with a blend and a sensitivity of its own: a kit must not touch them.
    auto snare = c.getKept().strips[1].channel;
    snare.replaceEnabled = true;
    snare.replaceBlend = 0.37f;
    snare.replaceThresholdDb = -31.0f;
    c.setStripChannel (1, snare);
    const auto before = c.getKept();

    const auto* punchy = findDrumKit ("Punchy");
    REQUIRE (punchy != nullptr);
    const auto said = applyDrumKit (*punchy, library, c);
    CHECK (juce::String (said).contains ("Punchy"));
    std::array<SampleChoice, kMaxStrips> now {};
    readSampleChoices (c, library, now);
    CHECK (now[0].name == "Punch kick");
    CHECK (now[1].name == "Powerful snare");
    CHECK (now[2].name == "10 inch rack tom");
    CHECK (now[3].name == "12 inch rack tom");
    CHECK (now[4].name == "16 inch floor tom");
    CHECK (! now[5].set());                                  // the lead vocal has no drum sound
    CHECK (currentDrumKit (now, c) == "Punchy");
    CHECK (c.getKept().strips[1].channel.replaceEnabled);    // on, as it was
    CHECK_NEAR (c.getKept().strips[1].channel.replaceBlend, 0.37f, 1.0e-6);
    CHECK_NEAR (c.getKept().strips[1].channel.replaceThresholdDb, -31.0f, 1.0e-6);
    CHECK (! c.getKept().strips[0].channel.replaceEnabled);  // and off where it was off: nothing starts sounding
    CHECK (c.undoMixLabel() == "drum kit: Punchy");

    // One drum chosen by hand: the kit is Custom now, and says so.
    auto own = c.getKept().strips[1].channel;
    own.replaceSound = library.slotFor (RoleFamily::Snare, "Crisp snare", false);
    c.setStripChannel (1, own);
    readSampleChoices (c, library, now);
    CHECK (currentDrumKit (now, c) == "Custom");

    // Saved and opened again: the same sounds by name, so the same answer.
    auto state = captureSession (c, daw, DeviceChoice {}, 0);
    readSampleChoices (c, library, state.samples);
    SessionState back;
    REQUIRE (SessionStore::fromVar (SessionStore::toVar (state), back));
    MixController reopened;
    DawEngine reopenedDaw (reopened);
    applySession (back, reopened, reopenedDaw);
    reopened.setSampleBanks (library.table());
    resolveSampleChoices (back.samples, library, reopened);
    std::array<SampleChoice, kMaxStrips> again {};
    readSampleChoices (reopened, library, again);
    CHECK (again[1].name == "Crisp snare");
    CHECK (again[0].name == "Punch kick");
    CHECK (currentDrumKit (again, reopened) == "Custom");

    // Two Cmd+Z: the hand-picked snare, then the whole kit, back to where it started.
    c.undoMix();
    c.undoMix();
    CHECK (MixPlanner::countParameterChanges (before, c.getKept()) == 0);

    // A sound this Mac does not have: that drum keeps what it had, and the sentence names it.
    DrumKit odd = *punchy;
    odd.name = "Odd";
    odd.snare = "A snare nobody has";
    const int snareWas = c.getKept().strips[1].channel.replaceSound;
    const auto oddSaid = applyDrumKit (odd, library, c);
    CHECK (juce::String (oddSaid).contains ("A snare nobody has"));
    CHECK (c.getKept().strips[1].channel.replaceSound == snareWas);
    readSampleChoices (c, library, now);
    CHECK (now[0].name == "Punch kick");

    // A reload never invalidates what the engine was given: the bank it plays is still there.
    const auto* table = library.table();
    const auto* bank = table->bank (RoleFamily::Kick, c.getKept().strips[0].channel.replaceSound);
    library.load();
    CHECK (table->bank (RoleFamily::Kick, c.getKept().strips[0].channel.replaceSound) == bank);
    CHECK (bank != nullptr);
}

TEST_CASE ("Drum kits: a session with no kick, snare or tom has no kit to speak of")
{
    MixSession s;
    s.name = "Speech";
    s.inputs = { { "Pastor", ChannelRole::Speech, 0, -1 }, { "Hat", ChannelRole::HiHat, 1, -1 } };
    MixController c;
    c.setSession (s);
    CHECK (currentDrumKit ({}, c).empty());
}
