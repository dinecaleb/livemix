// DINELIVE DAW-layer tests: the playhead, the multitrack recorder, timeline playback, the
// monitoring rule, clip editing and the offline bounce. No device and no UI — the audio
// callback is played by the test, exactly as AudioHost would.
#include "TestFramework.h"
#include "native/DawEngine.h"
#include "native/MixBounce.h"
#include "native/MultitrackImport.h"
#include "native/SessionStore.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>

using namespace livemix;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;

    MixSession band()
    {
        MixSession s;
        s.name = "Daw Test";
        s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 },
                     { "Bass", ChannelRole::BassDI, 1, -1 },
                     { "Keys", ChannelRole::Piano, 2, 3 },
                     { "Lead", ChannelRole::LeadVocal, 4, -1 } };
        return s;
    }

    juce::File scratchFolder()
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dinelive-tests");
        f.createDirectory();
        return f;
    }

    // Writes a WAV of a steady tone so a clip has something recognisable in it.
    juce::File writeTone (const juce::File& folder, const juce::String& name, double seconds, float amplitude,
                          float hz = 220.0f, int channels = 1, double rate = kSr)
    {
        folder.createDirectory();
        const auto file = folder.getChildFile (name);
        file.deleteFile();
        const int frames = int (seconds * rate);
        juce::AudioBuffer<float> buffer (channels, frames);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i)
                buffer.setSample (ch, i, amplitude * std::sin (2.0f * float (M_PI) * hz * float (i) / float (rate)));

        juce::WavAudioFormat wav;
        if (auto* stream = file.createOutputStream().release())
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, rate, (unsigned) channels, 24, {}, 0));
            if (writer != nullptr) writer->writeFromAudioSampleBuffer (buffer, 0, frames);
            else delete stream;
        }
        return file;
    }

    // One audio callback: silence in unless `level` says otherwise, stereo out.
    struct Callback
    {
        DawEngine& daw;
        std::vector<std::vector<float>> in;
        std::vector<const float*> ip;
        std::vector<float> l, r;
        long long pos = 0;

        explicit Callback (DawEngine& d, int inputs = 8)
            : daw (d), in (size_t (inputs), std::vector<float> (size_t (kBlock), 0.0f)),
              ip (size_t (inputs), nullptr), l (size_t (kBlock), 0.0f), r (size_t (kBlock), 0.0f) {}

        // Runs `blocks` callbacks with every device input at `level` and returns the loudest output sample.
        float run (int blocks, float level = 0.0f)
        {
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                for (size_t c = 0; c < in.size(); ++c)
                {
                    for (int i = 0; i < kBlock; ++i)
                        in[c][size_t (i)] = level * std::sin (2.0f * float (M_PI) * 440.0f * float (pos + i) / float (kSr));
                    ip[c] = in[c].data();
                }
                float* op[2] = { l.data(), r.data() };
                daw.processBlock (ip.data(), int (in.size()), op, 2, kBlock);
                for (int i = 0; i < kBlock; ++i) peak = std::max (peak, std::max (std::fabs (l[size_t (i)]), std::fabs (r[size_t (i)])));
                pos += kBlock;
            }
            return peak;
        }
    };
}

// ---------------------------------------------------------------- transport
TEST_CASE ("Transport: the playhead advances by the block and locates where it is told")
{
    Transport t;
    t.prepare (kSr);
    CHECK (t.getPosition() == 0);
    CHECK (! t.isPlaying());

    t.play();
    CHECK (t.advance (kBlock) == 0);
    CHECK (t.getPosition() == kBlock);
    t.advance (kBlock);
    CHECK (t.getPosition() == 2 * kBlock);

    t.setPosition (juce::int64 (kSr));
    CHECK (std::fabs (t.getPositionSeconds() - 1.0) < 1.0e-9);

    t.stop();
    CHECK (! t.isPlaying());
    CHECK (! t.isRecording());
}

TEST_CASE ("Transport: the loop wraps to the sample, so audio and playhead cannot drift")
{
    Transport t;
    t.prepare (kSr);
    t.setLoop (true, 1000, 1300);
    t.setPosition (1200);
    t.play();
    // 1200 + 128 = 1328, which is 28 past the loop end: it comes back 28 past the start.
    t.advance (kBlock);
    CHECK (t.getPosition() == 1000 + 28);
}

TEST_CASE ("Transport: the clock reads as hours, minutes, seconds and milliseconds")
{
    CHECK (Transport::formatTime (0.0) == "00:00:00.000");
    CHECK (Transport::formatTime (3661.5) == "01:01:01.500");
}

// ---------------------------------------------------------------- monitoring
TEST_CASE ("Monitoring: the rule is the whole rule, in one place")
{
    // Off never hears the input.
    CHECK (! monitorUsesLiveInput (MonitorMode::Off, true, false, false, false));
    CHECK (! monitorUsesLiveInput (MonitorMode::Off, true, true, true, true));
    // Input always does.
    CHECK (monitorUsesLiveInput (MonitorMode::Input, false, true, true, false));
    // Auto: the console stays live when nothing is playing back...
    CHECK (monitorUsesLiveInput (MonitorMode::Auto, false, false, false, false));
    CHECK (monitorUsesLiveInput (MonitorMode::Auto, false, true, false, false));
    // ...gives way to the recording while the timeline plays it...
    CHECK (! monitorUsesLiveInput (MonitorMode::Auto, false, true, true, false));
    // ...but a track being recorded always stays on its input.
    CHECK (monitorUsesLiveInput (MonitorMode::Auto, true, true, true, true));
    // A track with no recording under the playhead has nothing else to hear.
    CHECK (monitorUsesLiveInput (MonitorMode::Auto, false, false, true, false));
}

// ---------------------------------------------------------------- recorder
TEST_CASE ("Recorder: an armed track becomes a WAV, and an empty take leaves no files behind")
{
    const auto folder = scratchFolder().getChildFile ("recorder");
    folder.deleteRecursively();

    Recorder recorder;
    std::vector<Recorder::Spec> specs { { 0, "Kick", 0, -1 }, { 2, "Keys", 2, 3 } };
    CHECK (recorder.start (folder, specs, kSr, 0).isEmpty());
    CHECK (recorder.isRecording());

    std::vector<std::vector<float>> in (4, std::vector<float> (size_t (kBlock), 0.25f));
    std::vector<const float*> ip (4, nullptr);
    for (size_t c = 0; c < in.size(); ++c) ip[c] = in[c].data();
    for (int b = 0; b < 60; ++b) recorder.write (ip.data(), 4, kBlock);

    const auto takes = recorder.stop();
    CHECK (! recorder.isRecording());
    REQUIRE (takes.size() == 2);
    CHECK (takes[0].length == 60 * kBlock);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> mono (formats.createReaderFor (folder.getChildFile (takes[0].fileName)));
    REQUIRE (mono != nullptr);
    CHECK (mono->numChannels == 1);
    CHECK (mono->lengthInSamples == 60 * kBlock);
    std::unique_ptr<juce::AudioFormatReader> stereo (formats.createReaderFor (folder.getChildFile (takes[1].fileName)));
    REQUIRE (stereo != nullptr);
    CHECK (stereo->numChannels == 2);

    // A take that captured nothing is not left on disk.
    CHECK (recorder.start (folder, specs, kSr, 0).isEmpty());
    CHECK (recorder.stop().empty());
    CHECK (folder.getNumberOfChildFiles (juce::File::findFiles) == 2);
    folder.deleteRecursively();
}

TEST_CASE ("Recorder: refuses to start with nothing armed")
{
    Recorder recorder;
    CHECK (recorder.start (scratchFolder(), {}, kSr, 0).isNotEmpty());
    CHECK (! recorder.isRecording());
}

// ---------------------------------------------------------------- playback
TEST_CASE ("ClipSource: clips land at their place on the timeline and are silent elsewhere")
{
    const auto folder = scratchFolder().getChildFile ("clips");
    folder.deleteRecursively();
    const auto file = writeTone (folder, "tone.wav", 1.0, 0.5f);

    ClipSource::Track track;
    track.channels = 1;
    AudioClip clip;
    clip.file = file.getFullPathName();
    clip.start = 1000;
    clip.offset = 0;
    clip.length = juce::int64 (kSr);
    clip.fileSampleRate = kSr;
    track.clips.push_back (clip);

    ClipSource source;
    source.prepare (kSr, 512, { track });

    auto peakOf = [&source] (juce::int64 from, int count)
    {
        source.read (from, count);
        const float* c = source.channel (0, 0);
        float peak = 0.0f;
        for (int i = 0; i < count; ++i) peak = std::max (peak, std::fabs (c[i]));
        return peak;
    };

    CHECK (peakOf (0, 512) < 1.0e-6f);                 // before the clip: silence
    CHECK (peakOf (10000, 512) > 0.3f);                // inside the clip: the tone
    CHECK (peakOf (juce::int64 (kSr) + 5000, 512) < 1.0e-6f);   // after it: silence again
    folder.deleteRecursively();
}

TEST_CASE ("TimelinePlayer: what the audio thread reads is what the clips hold")
{
    const auto folder = scratchFolder().getChildFile ("player");
    folder.deleteRecursively();
    const auto file = writeTone (folder, "tone.wav", 2.0, 0.5f);

    TimelinePlayer::TrackClips track;
    track.channels = 1;
    AudioClip clip;
    clip.file = file.getFullPathName();
    clip.start = 0;
    clip.length = juce::int64 (2.0 * kSr);
    clip.fileSampleRate = kSr;
    track.clips.push_back (clip);

    TimelinePlayer player;
    player.prepare (kSr, kBlock, { track });
    REQUIRE (player.isPrepared());
    player.prime (0);

    float peak = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        player.read (kBlock);
        const float* c = player.channel (0, 0);
        REQUIRE (c != nullptr);
        for (int i = 0; i < kBlock; ++i) peak = std::max (peak, std::fabs (c[i]));
    }
    CHECK (peak > 0.3f);
    player.release();
    folder.deleteRecursively();
}

// ---------------------------------------------------------------- the DAW as a whole
TEST_CASE ("DawEngine: a live input is heard, a recorded track plays back, monitoring decides which")
{
    const auto folder = scratchFolder().getChildFile ("daw");
    folder.deleteRecursively();
    const auto file = writeTone (folder, "Kick_001.wav", 2.0, 0.6f, 80.0f);

    MixController controller;
    DawEngine daw (controller);
    const auto session = band();
    controller.setSession (session);
    daw.setSession (session);
    controller.prepare (kSr, kBlock);
    daw.prepare (kSr, kBlock);

    Callback cb (daw);

    // Nothing recorded: the live input is what the mix hears.
    CHECK (cb.run (20, 0.5f) > 0.001f);
    cb.run (400, 0.0f);                                // let the returns ring out
    CHECK (cb.run (20, 0.0f) < 0.02f);

    // Put a take on track 0 and play: track 0 comes off the timeline, the rest stay live.
    auto project = daw.getProject();
    project.folder = folder;
    project.syncTracks (session);
    AudioClip clip;
    clip.file = file.getFullPathName();
    clip.start = 0;
    clip.length = juce::int64 (2.0 * kSr);
    clip.fileSampleRate = kSr;
    project.tracks[0].clips.push_back (clip);
    daw.setProject (project);

    daw.locate (0);
    daw.play();
    CHECK (daw.getTransport().isPlaying());
    const float withTape = cb.run (60, 0.0f);          // silent inputs: anything heard is the recording
    CHECK (withTape > 0.0005f);
    CHECK (daw.getTransport().getPosition() == 60 * kBlock);

    // Monitoring Off on that track and no other input: silence.
    daw.stop();
    project = daw.getProject();
    project.tracks[0].monitor = MonitorMode::Off;
    daw.setProject (project);
    cb.run (400, 0.0f);                                // let the returns ring out
    CHECK (cb.run (20, 0.0f) < 0.02f);

    daw.release();
    folder.deleteRecursively();
}

TEST_CASE ("DawEngine: recording an armed track adds it to the timeline as a clip")
{
    const auto folder = scratchFolder().getChildFile ("record-session");
    folder.deleteRecursively();
    folder.createDirectory();

    MixController controller;
    DawEngine daw (controller);
    const auto session = band();
    controller.setSession (session);
    daw.setSession (session);
    controller.prepare (kSr, kBlock);
    daw.prepare (kSr, kBlock);

    // No folder yet: DINELIVE says where the audio would go rather than losing it.
    CHECK (daw.startRecording().isNotEmpty());

    auto project = daw.getProject();
    project.folder = folder;
    project.tracks[0].armed = true;
    project.tracks[2].armed = true;
    daw.setProject (project);

    CHECK (daw.startRecording().isEmpty());
    CHECK (daw.isRecording());
    CHECK (daw.getTransport().isPlaying());

    Callback cb (daw);
    cb.run (80, 0.4f);

    CHECK (daw.stopRecording() == 2);
    daw.stop();
    CHECK (! daw.isRecording());

    const auto& after = daw.getProject();
    REQUIRE (after.tracks.size() == 4);
    CHECK (after.tracks[0].clips.size() == 1);
    CHECK (after.tracks[1].clips.empty());
    CHECK (after.tracks[2].clips.size() == 1);
    CHECK (after.tracks[0].clips[0].length == 80 * kBlock);
    CHECK (after.audioFolder().getChildFile (after.tracks[0].clips[0].file).existsAsFile());
    CHECK (after.hasAudio());

    // Play what was just recorded: silent inputs, so anything heard came off the timeline.
    project = daw.getProject();
    for (auto& t : project.tracks) t.armed = false;
    daw.setProject (project);
    daw.locate (0);
    daw.play();
    CHECK (cb.run (60, 0.0f) > 0.0005f);
    daw.stop();

    // ...and bounce it: record -> clip -> export, without leaving the session.
    const auto dest = folder.getChildFile ("bounce.wav");
    CHECK (MixBounce::renderProject (session, controller.getKept(), daw.getProject(), dest, MixBounce::Format::Wav).isEmpty());
    CHECK (dest.existsAsFile());

    daw.release();
    folder.deleteRecursively();
}

// ---------------------------------------------------------------- documents
TEST_CASE ("SessionStore: the timeline survives the round trip, and a version 1 file still opens")
{
    SessionStore::Document d;
    d.session = band();
    d.project.sampleRate = kSr;
    d.project.tempo = 96.0;
    d.project.liveSafe = true;
    d.project.loopEnabled = true;
    d.project.loopStart = 1000;
    d.project.loopEnd = 50000;
    d.project.syncTracks (d.session);
    d.project.tracks[1].armed = true;
    d.project.tracks[1].monitor = MonitorMode::Input;
    d.project.tracks[1].height = 96;
    d.project.tracks[1].clips.push_back ({ "Bass", "Bass_001.wav", 4800, 0, 96000, kSr });
    d.project.markers.push_back ({ "Sermon", 240000 });

    const auto file = scratchFolder().getChildFile ("round-trip.dinelive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (d, file));

    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));
    CHECK (back.project.tempo == 96.0);
    CHECK (back.project.liveSafe);
    CHECK (back.project.loopEnabled);
    CHECK (back.project.loopEnd == 50000);
    REQUIRE (back.project.tracks.size() == 4);
    CHECK (back.project.tracks[1].armed);
    CHECK (back.project.tracks[1].monitor == MonitorMode::Input);
    CHECK (back.project.tracks[1].height == 96);
    REQUIRE (back.project.tracks[1].clips.size() == 1);
    CHECK (back.project.tracks[1].clips[0].start == 4800);
    CHECK (back.project.tracks[1].clips[0].file == "Bass_001.wav");
    REQUIRE (back.project.markers.size() == 1);
    CHECK (back.project.markers[0].name == "Sermon");
    CHECK (back.project.folder == file.getParentDirectory());

    // A version 1 document has no "project" at all: it opens with an empty timeline.
    juce::var v1 = SessionStore::toVar (d);
    REQUIRE (v1.getDynamicObject() != nullptr);
    v1.getDynamicObject()->removeProperty ("project");
    v1.getDynamicObject()->setProperty ("version", 1);
    SessionStore::Document old;
    REQUIRE (SessionStore::fromVar (v1, old));
    CHECK (old.project.tracks.size() == old.session.inputs.size());
    CHECK (! old.project.hasAudio());
    file.deleteFile();
}

TEST_CASE ("MultitrackImport: a folder of stems becomes tracks, clips and guessed sources")
{
    const auto folder = scratchFolder().getChildFile ("stems");
    folder.deleteRecursively();
    writeTone (folder, "Kick.wav", 0.5, 0.4f, 60.0f);
    writeTone (folder, "Lead Vox.wav", 0.5, 0.4f, 300.0f);
    writeTone (folder, "Keys.wav", 0.5, 0.4f, 400.0f, 2);

    const auto result = MultitrackImport::fromFolder (folder, MixSession {});
    CHECK (result.error.isEmpty());
    CHECK (result.files == 3);
    REQUIRE (result.session.inputs.size() == 3);
    // Files are taken in name order: Keys (stereo), Kick, Lead Vox.
    CHECK (result.session.inputs[0].isStereo());       // the stereo file takes a pair of inputs
    CHECK (result.session.inputs[0].inputA == 0);
    CHECK (result.session.inputs[1].role == ChannelRole::KickIn);
    CHECK (result.session.inputs[1].inputA == 2);      // ...so the next one starts past that pair
    CHECK (result.session.inputs[2].role == ChannelRole::LeadVocal);
    CHECK (result.session.inputs[2].inputA == 3);
    REQUIRE (result.project.tracks.size() == 3);
    CHECK (result.project.tracks[0].clips.size() == 1);
    CHECK (result.project.hasAudio());
    CHECK (result.project.folder == juce::File());     // imported audio stays where it is
    folder.deleteRecursively();
}

TEST_CASE ("MixBounce: the timeline renders offline to a stereo WAV of the right length")
{
    const auto folder = scratchFolder().getChildFile ("bounce");
    folder.deleteRecursively();
    const auto file = writeTone (folder, "Kick.wav", 1.0, 0.5f, 80.0f);

    MixController controller;
    const auto session = band();
    controller.setSession (session);
    controller.prepare (kSr, kBlock);

    Project project;
    project.sampleRate = kSr;
    project.syncTracks (session);
    AudioClip clip;
    clip.file = file.getFullPathName();
    clip.start = 0;
    clip.length = juce::int64 (kSr);
    clip.fileSampleRate = kSr;
    project.tracks[0].clips.push_back (clip);

    const auto dest = folder.getChildFile ("mix.wav");
    const auto err = MixBounce::renderProject (session, controller.getKept(), project, dest, MixBounce::Format::Wav);
    CHECK (err.isEmpty());
    REQUIRE (dest.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (dest));
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
    CHECK (reader->lengthInSamples == juce::int64 (kSr));
    juce::AudioBuffer<float> audio (2, int (reader->lengthInSamples));
    reader->read (&audio, 0, int (reader->lengthInSamples), 0, true, true);
    CHECK (audio.getMagnitude (0, audio.getNumSamples()) > 0.001f);

    // An empty timeline is refused with a reason rather than writing an empty file.
    Project empty;
    empty.syncTracks (session);
    CHECK (MixBounce::renderProject (session, controller.getKept(), empty, dest, MixBounce::Format::Wav).isNotEmpty());
    folder.deleteRecursively();
}

TEST_CASE ("SessionStore: sessions are listed from their folders without reading their audio")
{
    const auto folder = SessionStore::folderFor ("DineliveListTest");
    folder.deleteRecursively();

    SessionStore::Document d;
    d.session = band();
    d.session.name = "DineliveListTest";
    REQUIRE (SessionStore::save (d, SessionStore::fileFor ("DineliveListTest")));

    // A big pile of audio beside the document must not slow the list down or confuse it.
    folder.getChildFile ("Audio Files").createDirectory();
    for (int i = 0; i < 20; ++i)
        writeTone (folder.getChildFile ("Audio Files"), "Take_" + juce::String (i) + ".wav", 0.05, 0.1f);

    bool found = false;
    for (const auto& listing : SessionStore::listSessions())
        if (listing.name == "DineliveListTest") { found = true; CHECK (listing.file.existsAsFile()); }
    CHECK (found);
    folder.deleteRecursively();
}

TEST_CASE ("Project: length, arming and clip file resolution")
{
    Project p;
    p.folder = juce::File ("/tmp/example");
    p.tracks.resize (2);
    p.tracks[0].clips.push_back ({ "a", "Kick_001.wav", 0, 0, 1000, kSr });
    p.tracks[1].clips.push_back ({ "b", "/elsewhere/Snare.wav", 500, 0, 2000, kSr });
    p.tracks[1].armed = true;

    CHECK (p.lengthSamples() == 2500);
    CHECK (p.numArmed() == 1);
    CHECK (p.hasAudio());
    CHECK (p.fileFor (p.tracks[0].clips[0]) == juce::File ("/tmp/example/Audio Files/Kick_001.wav"));
    CHECK (p.fileFor (p.tracks[1].clips[0]) == juce::File ("/elsewhere/Snare.wav"));
}
