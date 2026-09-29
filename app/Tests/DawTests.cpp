// DLIVE DAW-layer tests: the playhead, the multitrack recorder, timeline playback, the
// monitoring rule, clip editing and the offline bounce. No device and no UI — the audio
// callback is played by the test, exactly as AudioHost would.
#include "TestFramework.h"
#include "native/DawEngine.h"
#include "native/MixBounce.h"
#include "DSP/LoudnessMeter.h"
#include "native/MultitrackImport.h"
#include "native/StemNames.h"
#include "native/SessionStore.h"
#include "native/SampleLibrary.h"
#include "native/SessionAutosave.h"
#include "native/DeviceState.h"
#include "native/InputMapStore.h"
#include "native/MonitorDevice.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dlive-tests");
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

TEST_CASE ("Recorder: a block bigger than the capture path is reported, never silently lost")
{
    const auto folder = scratchFolder().getChildFile ("oversized");
    folder.deleteRecursively();

    Recorder recorder;
    std::vector<Recorder::Spec> specs { { 0, "Kick", 0, -1 } };
    CHECK (recorder.start (folder, specs, kSr, 0).isEmpty());
    CHECK (recorder.getError().isEmpty());

    // Larger than the recorder's own maximum block: the audio cannot be captured, so the
    // take must say so rather than come out quietly short of the performance.
    constexpr int kHuge = 16384;
    std::vector<float> in (size_t (kHuge), 0.25f);
    const float* ip[1] = { in.data() };
    recorder.write (ip, 1, kHuge);
    CHECK (recorder.getError().isNotEmpty());
    CHECK (recorder.getFramesWritten() == 0);
    recorder.stop();
    folder.deleteRecursively();
}

namespace
{
    // What a take looks like after the app died while writing it: the RIFF and data sizes are
    // still the zeros the writer put there before its first flush, and the sidecar is still beside it.
    void makeUnfinished (const juce::File& wav, int track, const juce::String& name, int channels, juce::int64 timelineStart)
    {
        juce::int64 dataSizeAt = -1;
        {
            juce::FileInputStream in (wav);
            in.setPosition (12);
            while (in.getPosition() + 8 <= in.getTotalLength())
            {
                char id[4]; in.read (id, 4);
                const auto size = (juce::uint32) in.readInt();
                if (std::memcmp (id, "data", 4) == 0) { dataSizeAt = in.getPosition() - 4; break; }
                in.setPosition (in.getPosition() + size + (size & 1));
            }
        }
        REQUIRE (dataSizeAt > 0);
        juce::FileOutputStream out (wav);
        out.setPosition (4); out.writeInt (0);
        out.setPosition (dataSizeAt); out.writeInt (0);
        out.flush();
        Recorder::sidecarFor (wav).replaceWithText (
            "{\"app\":\"DLIVE\",\"schema\":1,\"track\":" + juce::String (track) + ",\"name\":\"" + name + "\","
            "\"sampleRate\":48000.0,\"channels\":" + juce::String (channels) + ",\"bitDepth\":24,"
            "\"timelineStart\":" + juce::String (timelineStart) + ",\"framesWritten\":0}");
    }

    juce::int64 readerLength (const juce::File& wav)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (wav));
        return r != nullptr ? r->lengthInSamples : -1;
    }
}

TEST_CASE ("Recorder: a take the app died in the middle of is repaired to the length on disk, and lands on its track")
{
    const auto folder = scratchFolder().getChildFile ("recover");
    folder.deleteRecursively();

    // Two real takes, then the crash: headers back to zero, sidecars left behind.
    Recorder recorder;
    std::vector<Recorder::Spec> specs { { 0, "Kick", 0, -1 }, { 2, "Keys", 2, 3 } };
    REQUIRE (recorder.start (folder, specs, kSr, 0).isEmpty());
    std::vector<std::vector<float>> in (4, std::vector<float> (size_t (kBlock), 0.25f));
    std::vector<const float*> ip (4, nullptr);
    for (size_t c = 0; c < in.size(); ++c) ip[c] = in[c].data();
    for (int b = 0; b < 60; ++b) recorder.write (ip.data(), 4, kBlock);
    const auto takes = recorder.stop();
    REQUIRE (takes.size() == 2);
    CHECK (! Recorder::sidecarFor (folder.getChildFile (takes[0].fileName)).existsAsFile());   // a clean stop leaves none

    const auto kick = folder.getChildFile (takes[0].fileName);
    const auto keys = folder.getChildFile (takes[1].fileName);
    makeUnfinished (kick, 0, "Kick", 1, 4800);
    makeUnfinished (keys, 2, "Keys", 2, 4800);
    { juce::FileOutputStream out (kick); out.setPosition (out.getFile().getSize()); out.writeByte (1); out.writeByte (1); }  // a frame cut short by the crash
    CHECK (readerLength (kick) <= 0);                 // the simulation is real: no reader, or no audio, as recorded
    CHECK (readerLength (keys) <= 0);

    auto recovered = Recorder::recoverUnfinishedTakes (folder);
    REQUIRE (recovered.size() == 2);
    std::sort (recovered.begin(), recovered.end(), [] (const auto& a, const auto& b) { return a.trackIndex < b.trackIndex; });
    CHECK (recovered[0].repaired);
    CHECK (recovered[0].trackIndex == 0);
    CHECK (recovered[0].length == 60 * kBlock);       // the two stray bytes are not a frame
    CHECK (recovered[0].timelineStart == 4800);
    CHECK (recovered[0].channels == 1);
    CHECK (recovered[0].note.contains ("recovered"));
    CHECK (recovered[1].repaired);
    CHECK (recovered[1].trackIndex == 2);
    CHECK (recovered[1].length == 60 * kBlock);
    CHECK (readerLength (kick) == 60 * kBlock);
    CHECK (readerLength (keys) == 60 * kBlock);
    CHECK (! Recorder::sidecarFor (kick).existsAsFile());
    CHECK (! Recorder::sidecarFor (keys).existsAsFile());
    CHECK (Recorder::recoverUnfinishedTakes (folder).empty());   // nothing left to do

    // A take that never got a byte is removed rather than recovered; a sidecar without its file is cleared.
    makeUnfinished (kick, 0, "Kick", 1, 0);
    { juce::FileOutputStream out (kick); out.setPosition (0); out.truncate(); out.flush(); }
    Recorder::sidecarFor (keys).replaceWithText ("{\"track\":2}");
    keys.deleteFile();
    recovered = Recorder::recoverUnfinishedTakes (folder);
    REQUIRE (recovered.size() == 2);
    CHECK (! recovered[0].repaired);
    CHECK (! recovered[1].repaired);
    CHECK (! kick.existsAsFile() || kick.getSize() > 0);
    CHECK (folder.getNumberOfChildFiles (juce::File::findFiles, "*.recording.json") == 0);

    // Through the engine: the recovered take becomes a clip on its track, once.
    folder.deleteRecursively();
    Recorder again;
    REQUIRE (again.start (folder, specs, kSr, 0).isEmpty());
    for (int b = 0; b < 60; ++b) again.write (ip.data(), 4, kBlock);
    const auto second = again.stop();
    REQUIRE (second.size() == 2);
    makeUnfinished (folder.getChildFile (second[0].fileName), 0, "Kick", 1, 9600);

    MixController controller;
    DawEngine daw (controller);
    daw.setSession (band());
    Project project;
    project.folder = folder.getParentDirectory();
    project.tracks.resize (band().inputs.size());
    daw.setProject (project);
    // The project's audio lives in folder/"Audio Files": move the takes there.
    const auto audio = project.audioFolder();
    audio.deleteRecursively();
    REQUIRE (folder.moveFileTo (audio));

    const auto found = daw.recoverUnfinishedTakes();
    REQUIRE (found.size() == 1);
    CHECK (found[0].repaired);
    REQUIRE (daw.getProject().tracks[0].clips.size() == 1);
    CHECK (daw.getProject().tracks[0].clips[0].file == second[0].fileName);
    CHECK (daw.getProject().tracks[0].clips[0].start == 9600);
    CHECK (daw.getProject().tracks[0].clips[0].length == 60 * kBlock);
    CHECK (daw.recoverUnfinishedTakes().empty());
    CHECK (daw.getProject().tracks[0].clips.size() == 1);
    audio.deleteRecursively();
}

TEST_CASE ("Recorder: while a take is being written its header and sidecar are kept current from the writer thread")
{
    const auto folder = scratchFolder().getChildFile ("inprogress");
    folder.deleteRecursively();

    Recorder recorder (0.05, 0.02);    // sidecar every 50 ms, header every 20 ms of audio (a service uses 20 s / 15 s)
    std::vector<Recorder::Spec> specs { { 1, "Kick", 0, -1 } };
    REQUIRE (recorder.start (folder, specs, kSr, 7000).isEmpty());
    const auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav");
    REQUIRE (files.size() == 1);
    const auto wav = files[0];
    const auto sidecar = Recorder::sidecarFor (wav);
    CHECK (sidecar.existsAsFile());                    // "in progress" from the first block

    std::vector<float> in (size_t (kBlock), 0.25f);
    const float* ip[1] = { in.data() };
    for (int b = 0; b < 60; ++b) recorder.write (ip, 1, kBlock);
    juce::Thread::sleep (400);

    const auto doc = juce::JSON::parse (sidecar.loadFileAsString());
    REQUIRE (doc.getDynamicObject() != nullptr);
    CHECK ((int) doc["track"] == 1);
    CHECK ((int) doc["channels"] == 1);
    CHECK ((double) doc["sampleRate"] == kSr);
    CHECK ((juce::int64) doc["timelineStart"] == 7000);
    CHECK ((juce::int64) doc["framesWritten"] == 60 * kBlock);

    // The header on disk is already valid: a copy of the file, as a crash would leave it, reads.
    const auto copy = folder.getChildFile ("copy.wav");
    REQUIRE (wav.copyFileTo (copy));
    CHECK (readerLength (copy) > 0);
    CHECK (readerLength (copy) <= 60 * kBlock);

    const auto takes = recorder.stop();
    REQUIRE (takes.size() == 1);
    CHECK (! sidecar.existsAsFile());
    CHECK (readerLength (wav) == 60 * kBlock);
    folder.deleteRecursively();
}

TEST_CASE ("Recorder: what a take costs per second, so the disk can be asked how long it will last")
{
    // 24-bit: three bytes a sample, a channel at a time.
    std::vector<Recorder::Spec> mono { { 0, "Kick", 0, -1 } };
    CHECK (std::abs (Recorder::bytesPerSecondFor (mono, kSr) - 3.0 * kSr) < 1.0);
    std::vector<Recorder::Spec> pair { { 0, "Kick", 0, -1 }, { 1, "Keys", 2, 3 } };
    CHECK (std::abs (Recorder::bytesPerSecondFor (pair, kSr) - 9.0 * kSr) < 1.0);
    CHECK (Recorder::bytesPerSecondFor ({}, kSr) == 0.0);
    // A volume that will not answer gives 0, never an invented number.
    CHECK (Recorder::secondsFreeOn (scratchFolder(), 0.0) == 0.0);
    CHECK (Recorder::secondsFreeOn (scratchFolder(), 3.0 * kSr) > 0.0);
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

TEST_CASE ("Outputs: a feed lands on its own pair, at its own level, and mute silences only it")
{
    MixController controller;
    DawEngine daw (controller);
    const auto session = band();
    controller.setSession (session);
    daw.setSession (session);
    controller.prepare (kSr, kBlock);
    daw.prepare (kSr, kBlock);

    // Six device outputs: the main pair, a cue pair, and one that nothing is sent to.
    std::vector<std::vector<float>> outs (6, std::vector<float> (size_t (kBlock), 0.0f));
    std::vector<std::vector<float>> ins (8, std::vector<float> (size_t (kBlock), 0.0f));
    std::vector<const float*> ip (8, nullptr);
    long long pos = 0;

    auto run = [&] (int blocks, float level, std::array<float, 6>& peaks)
    {
        peaks.fill (0.0f);
        for (int b = 0; b < blocks; ++b)
        {
            for (size_t c = 0; c < ins.size(); ++c)
            {
                for (int i = 0; i < kBlock; ++i)
                    ins[c][size_t (i)] = level * std::sin (2.0f * float (M_PI) * 440.0f * float (pos + i) / float (kSr));
                ip[c] = ins[c].data();
            }
            float* op[6];
            for (int o = 0; o < 6; ++o) op[size_t (o)] = outs[size_t (o)].data();
            daw.processBlock (ip.data(), int (ins.size()), op, 6, kBlock);
            for (int o = 0; o < 6; ++o)
                for (int i = 0; i < kBlock; ++i)
                    peaks[size_t (o)] = std::max (peaks[size_t (o)], std::fabs (outs[size_t (o)][size_t (i)]));
            pos += kBlock;
        }
    };

    std::array<float, 6> peaks {};

    // Out of the box: the main feed only, on outputs 1-2.
    run (40, 0.5f, peaks);
    CHECK (peaks[0] > 0.001f);
    CHECK (peaks[1] > 0.001f);
    CHECK (peaks[2] < 1.0e-6f);
    CHECK (peaks[3] < 1.0e-6f);

    // A second feed on outputs 3-4 carries the same mix, 12 dB down.
    auto feeds = controller.getOutputFeeds();
    feeds.count = 2;
    feeds.feeds[1].left = 2;
    feeds.feeds[1].right = 3;
    feeds.feeds[1].source = MixBus::Master;
    feeds.feeds[1].gainDb = -12.0f;
    controller.setOutputFeeds (feeds);

    run (40, 0.5f, peaks);
    CHECK (peaks[2] > 0.001f);
    CHECK (peaks[3] > 0.001f);
    CHECK (peaks[4] < 1.0e-6f);                        // nothing was sent there
    CHECK (peaks[2] < peaks[0] * 0.5f);                // -12 dB is a quarter of the level
    CHECK (peaks[2] > peaks[0] * 0.1f);

    // Muting the cue silences that pair and leaves the main one alone.
    feeds.feeds[1].mute = true;
    controller.setOutputFeeds (feeds);
    run (40, 0.5f, peaks);
    CHECK (peaks[0] > 0.001f);
    CHECK (peaks[2] < 1.0e-6f);

    // A feed that is not routed anywhere is silent without taking the main one with it.
    feeds.feeds[1].mute = false;
    feeds.feeds[1].left = -1;
    feeds.feeds[1].right = -1;
    controller.setOutputFeeds (feeds);
    run (40, 0.5f, peaks);
    CHECK (peaks[0] > 0.001f);
    CHECK (peaks[2] < 1.0e-6f);

    daw.release();
}

TEST_CASE ("Outputs: the routing survives a save and a reload, and an older session opens on the main pair")
{
    const auto folder = scratchFolder().getChildFile ("outputs-session");
    folder.deleteRecursively();
    folder.createDirectory();

    SessionStore::Document d;
    d.session = band();
    d.project.syncTracks (d.session);
    d.outputs.count = 2;
    d.outputs.feeds[1].left = 4;
    d.outputs.feeds[1].right = 5;
    d.outputs.feeds[1].source = MixBus::Vocals;
    d.outputs.feeds[1].gainDb = -6.0f;
    d.outputs.feeds[1].mono = true;

    const auto file = folder.getChildFile ("outputs.dlive.json");
    CHECK (SessionStore::save (d, file));

    SessionStore::Document back;
    CHECK (SessionStore::load (file, back));
    CHECK (back.outputs.count == 2);
    CHECK (back.outputs.feeds[1].left == 4);
    CHECK (back.outputs.feeds[1].right == 5);
    CHECK (back.outputs.feeds[1].source == MixBus::Vocals);
    CHECK (std::fabs (back.outputs.feeds[1].gainDb + 6.0f) < 0.001f);
    CHECK (back.outputs.feeds[1].mono);

    // A session written before outputs existed opens on the main pair, not in silence.
    auto older = juce::JSON::parse (file.loadFileAsString());
    if (auto* obj = older.getDynamicObject()) obj->removeProperty ("outputs");
    const auto olderFile = folder.getChildFile ("older.dlive.json");
    olderFile.replaceWithText (juce::JSON::toString (older));
    SessionStore::Document legacy;
    CHECK (SessionStore::load (olderFile, legacy));
    CHECK (legacy.outputs.count == 1);
    CHECK (legacy.outputs.feeds[0].left == 0);
    CHECK (legacy.outputs.feeds[0].right == 1);

    folder.deleteRecursively();
}

TEST_CASE ("SessionStore: the FX group's fader and mute survive, and an older session has neither")
{
    const auto folder = scratchFolder().getChildFile ("fx-group-session");
    folder.deleteRecursively();
    folder.createDirectory();

    SessionStore::Document d;
    d.session = band();
    d.project.syncTracks (d.session);
    d.hasMix = true;
    d.mix.numStrips = 4;
    d.mix.fxReturnDb = -4.5f;
    d.mix.fxMute = true;

    const auto file = folder.getChildFile ("fx.dlive.json");
    CHECK (SessionStore::save (d, file));

    SessionStore::Document back;
    CHECK (SessionStore::load (file, back));
    CHECK (std::fabs (back.mix.fxReturnDb + 4.5f) < 0.001f);
    CHECK (back.mix.fxMute);

    // Before the effects had a group fader the document said nothing about one, and "nothing
    // said" has to mean "exactly as TUNE MIX left it": 0 dB, not muted.
    auto older = juce::JSON::parse (file.loadFileAsString());
    if (auto* obj = older.getDynamicObject())
        if (auto* mix = obj->getProperty ("mix").getDynamicObject())
        {
            mix->removeProperty ("fxReturnDb");
            mix->removeProperty ("fxMute");
        }
    const auto olderFile = folder.getChildFile ("older.dlive.json");
    olderFile.replaceWithText (juce::JSON::toString (older));

    SessionStore::Document legacy;
    CHECK (SessionStore::load (olderFile, legacy));
    CHECK (std::fabs (legacy.mix.fxReturnDb) < 0.001f);
    CHECK (! legacy.mix.fxMute);

    folder.deleteRecursively();
}

TEST_CASE ("SessionStore: the effects switch on one channel is saved, and an older session has them on")
{
    const auto folder = scratchFolder().getChildFile ("effects-switch-session");
    folder.deleteRecursively();
    folder.createDirectory();

    SessionStore::Document d;
    d.session = band();
    d.project.syncTracks (d.session);
    d.hasMix = true;
    d.mix.numStrips = 5;
    d.mix.strips[3].effectsOff = true;                 // the lead, taken out of the plate
    d.mix.strips[3].sendDb[size_t (FxSlot::VocalPlate)] = -7.5f;   // ...with its level kept

    const auto file = folder.getChildFile ("effects.dlive.json");
    CHECK (SessionStore::save (d, file));

    SessionStore::Document back;
    CHECK (SessionStore::load (file, back));
    CHECK (back.mix.strips[3].effectsOff);
    CHECK (! back.mix.strips[0].effectsOff);
    // THE LEVEL IS WHAT MAKES THE PRESS BACK EXACT, so it is saved even while the gate is shut.
    CHECK (std::fabs (back.mix.strips[3].sendDb[size_t (FxSlot::VocalPlate)] + 7.5f) < 0.001f);

    // A session written before the switch existed says nothing about it, and "nothing said"
    // has to mean the effects are on - which is how every one of them sounded.
    auto older = juce::JSON::parse (file.loadFileAsString());
    if (auto* obj = older.getDynamicObject())
        if (auto* mix = obj->getProperty ("mix").getDynamicObject())
            if (auto* strips = mix->getProperty ("strips").getArray())
                for (auto& v : *strips)
                    if (auto* so = v.getDynamicObject()) so->removeProperty ("effectsOff");
    const auto olderFile = folder.getChildFile ("older.dlive.json");
    olderFile.replaceWithText (juce::JSON::toString (older));

    SessionStore::Document legacy;
    CHECK (SessionStore::load (olderFile, legacy));
    for (int i = 0; i < legacy.mix.numStrips; ++i) CHECK (! legacy.mix.strips[size_t (i)].effectsOff);

    folder.deleteRecursively();
}

TEST_CASE ("SessionStore: a session written before the speech group keeps its master")
{
    const auto folder = scratchFolder().getChildFile ("speech-bus-session");
    folder.deleteRecursively();
    folder.createDirectory();

    // Version 2 wrote five buses - DRUMS BASS MUSIC VOCALS MASTER - and named an output feed's
    // source by that index. SPEECH was inserted before MASTER, so reading such a file straight
    // through would put the master's fader, mutes and chain on the speech group and leave the
    // master at its defaults, and send the broadcast feed to the pastor instead of the mix.
    SessionStore::Document d;
    d.session = band();
    d.project.syncTracks (d.session);
    d.hasMix = true;
    d.mix.numStrips = 4;
    d.mix.buses[size_t (MixBus::Vocals)].faderDb = -2.5f;
    d.mix.buses[size_t (MixBus::Master)].faderDb = -7.5f;
    d.mix.buses[size_t (MixBus::Master)].mute = true;
    d.outputs.count = 2;
    d.outputs.feeds[1].source = MixBus::Master;

    const auto file = folder.getChildFile ("v2.dlive.json");
    CHECK (SessionStore::save (d, file));

    // Rewrite the document the way version 2 wrote it: five bus slots, master last.
    auto v2 = juce::JSON::parse (file.loadFileAsString());
    auto* obj = v2.getDynamicObject();
    REQUIRE (obj != nullptr);
    obj->setProperty ("version", 2);
    if (auto* mix = obj->getProperty ("mix").getDynamicObject())
        if (auto* buses = mix->getProperty ("buses").getArray())
        {
            buses->remove (int (MixBus::Speech));               // the slot that did not exist yet
            CHECK (buses->size() == int (MixBus::Count) - 1);
        }
    if (auto* feeds = obj->getProperty ("outputs").getArray())
        if (auto* feed = feeds->getReference (1).getDynamicObject())
            feed->setProperty ("source", int (MixBus::Count) - 2);   // the old MASTER index

    const auto v2File = folder.getChildFile ("written-as-v2.dlive.json");
    v2File.replaceWithText (juce::JSON::toString (v2));

    SessionStore::Document back;
    CHECK (SessionStore::load (v2File, back));
    CHECK (std::fabs (back.mix.buses[size_t (MixBus::Vocals)].faderDb + 2.5f) < 0.001f);
    CHECK (std::fabs (back.mix.buses[size_t (MixBus::Master)].faderDb + 7.5f) < 0.001f);
    CHECK (back.mix.buses[size_t (MixBus::Master)].mute);
    CHECK (std::fabs (back.mix.buses[size_t (MixBus::Speech)].faderDb) < 0.001f);   // a fresh group
    CHECK (! back.mix.buses[size_t (MixBus::Speech)].mute);
    CHECK (back.outputs.feeds[1].source == MixBus::Master);

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

    // No folder yet: DLIVE says where the audio would go rather than losing it.
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
    cb.run (40, 0.4f);

    // The playhead belongs to the take while it runs: the clip lands where recording
    // started, so a locate in the middle would only make the picture lie about the audio.
    const juce::int64 during = daw.getTransport().getPosition();
    daw.locate (0);
    CHECK (daw.getTransport().getPosition() >= during);
    CHECK (daw.isRecording());

    cb.run (40, 0.4f);

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

    const auto file = scratchFolder().getChildFile ("round-trip.dlive.json");
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

TEST_CASE ("StemNames: the labels a live desk actually writes, and no accidents inside longer words")
{
    auto guess = [] (const char* name)
    {
        ChannelRole r = ChannelRole::Count;
        return StemNames::guessRole (name, r) ? r : ChannelRole::Count;
    };

    // Overheads are the kit's cymbals and most of what it puts above 8 kHz. A desk writes them "OV" as often
    // as "OH"; without both the drums arrive with no top at all and nothing carries the toms between hits.
    CHECK (guess ("OV L #03") == ChannelRole::OverheadLeft);
    CHECK (guess ("OV R #03") == ChannelRole::OverheadRight);
    CHECK (guess ("OH L") == ChannelRole::OverheadLeft);
    CHECK (guess ("Overhead R") == ChannelRole::OverheadRight);
    CHECK (guess ("OV 1") == ChannelRole::Overhead);        // numbered, not sided
    CHECK (guess ("Ride") == ChannelRole::Overhead);

    // A two-letter abbreviation matched anywhere inside a name is a trap: "ld" lives inside "handheld", which
    // put the preacher's microphone on the vocal bus with the singers - and with their plate and delay on it.
    // Both of these are speaking microphones, and since 2026-09-29 the name says which kind.
    CHECK (guess ("Pastor Handheld #02") == ChannelRole::SpeechHandheld);
    CHECK (guess ("Pastor Lapel") == ChannelRole::SpeechLapel);
    CHECK (roleFamily (guess ("Pastor Handheld #02")) == RoleFamily::Speech);
    CHECK (roleFamily (guess ("Pastor Lapel")) == RoleFamily::Speech);
    CHECK (guess ("HOST") == ChannelRole::Speech);
    CHECK (guess ("Holy Ghost") == ChannelRole::Count);     // not a host microphone
    CHECK (guess ("Lead mic 2") == ChannelRole::LeadVocal); // the real lead still reads as one
    CHECK (guess ("LD Vox") == ChannelRole::LeadVocal);

    // Playback from the booth is music, and "Computer Audio" is what the desk calls it.
    CHECK (guess ("Computer Audio #01") == ChannelRole::SynthPad);
    CHECK (guess ("LOOP 1") == ChannelRole::SynthPad);

    // A channel named after the person singing on it cannot be guessed, and must not be guessed at:
    // the import lists it for the user to assign.
    CHECK (guess ("angelica") == ChannelRole::Count);
}

// The shape of the bug these guard: the ASSIGN page rebuilds MixSession::inputs from
// scratch, so an input dropped out of the middle used to leave the tracks where they were
// and every clip below it took the next input's name.
namespace
{
    Project bandProject (const MixSession& session)
    {
        Project p;
        p.tracks.resize (session.inputs.size());
        for (size_t i = 0; i < session.inputs.size(); ++i)
        {
            AudioClip clip;
            clip.name = juce::String (session.inputs[i].name);
            clip.length = 48000;
            p.tracks[i].clips.push_back (clip);
            p.tracks[i].armed = (i == 2);
        }
        return p;
    }
}

TEST_CASE ("Project: an input dropped from the middle takes its track and clips with it")
{
    const MixSession before = band();
    auto project = bandProject (before);

    MixSession after = before;
    after.inputs.erase (after.inputs.begin() + 1);          // Bass is no longer assigned
    project.syncTracks (before, after);

    REQUIRE (project.tracks.size() == after.inputs.size());
    for (size_t i = 0; i < after.inputs.size(); ++i)
    {
        REQUIRE (project.tracks[i].clips.size() == 1);
        CHECK (project.tracks[i].clips[0].name == juce::String (after.inputs[i].name));
    }
    CHECK (project.tracks[1].armed);                        // Keys kept its own state
}

TEST_CASE ("Project: a new input starts with an empty track and the rest keep their clips")
{
    const MixSession before = band();
    auto project = bandProject (before);

    MixSession after = before;
    after.inputs.insert (after.inputs.begin() + 1, InputAssignment { "Snare", ChannelRole::SnareTop, 7, -1 });
    project.syncTracks (before, after);

    REQUIRE (project.tracks.size() == 5);
    REQUIRE (project.tracks[0].clips.size() == 1);
    CHECK (project.tracks[0].clips[0].name == "Kick");
    CHECK (project.tracks[1].clips.empty());                // Snare has never been recorded
    REQUIRE (project.tracks[2].clips.size() == 1);
    CHECK (project.tracks[2].clips[0].name == "Bass");
    REQUIRE (project.tracks[4].clips.size() == 1);
    CHECK (project.tracks[4].clips[0].name == "Lead");
}

TEST_CASE ("Project: reordering the inputs reorders the clips with them")
{
    const MixSession before = band();
    auto project = bandProject (before);

    MixSession after;
    after.inputs = { before.inputs[3], before.inputs[0], before.inputs[2], before.inputs[1] };
    project.syncTracks (before, after);

    REQUIRE (project.tracks.size() == 4);
    CHECK (project.tracks[0].clips[0].name == "Lead");
    CHECK (project.tracks[1].clips[0].name == "Kick");
    CHECK (project.tracks[2].clips[0].name == "Keys");
    CHECK (project.tracks[3].clips[0].name == "Bass");
}

TEST_CASE ("Moving a channel carries its clips, its chain and its level with it")
{
    // The whole of what a host does when a row is dragged on the timeline: the session is
    // reordered, the timeline follows its tracks, the graph is rebuilt and the mix is carried
    // across. If any one of those three disagrees about which input is which, a volunteer ends
    // up with the kick's gate on the pastor.
    const MixSession before = band();
    auto project = bandProject (before);

    MixController controller;
    controller.setSession (before);
    controller.prepare (kSr, kBlock);
    controller.setStripFader (0, -3.0f);          // Kick
    controller.setStripFader (3, 2.5f);           // Lead
    controller.setStripMute (1, true);            // Bass
    auto lead = controller.getKept().strips[3].channel;
    lead.compThresholdDb = -19.5f;
    controller.setStripChannel (3, lead);
    const MixParameters mix = controller.getKept();
    const MixSession prepared = controller.getPreparedSession();

    // Move the lead vocal (3) to the top.
    MixSession after = before;
    auto moved = after.inputs[3];
    after.inputs.erase (after.inputs.begin() + 3);
    after.inputs.insert (after.inputs.begin(), moved);

    project.syncTracks (before, after);
    controller.setSession (after);
    controller.prepare (kSr, kBlock);
    controller.restoreKept (carryMix (mix, prepared, controller.getKept(), after), 1);

    // The timeline
    REQUIRE (project.tracks.size() == 4);
    CHECK (project.tracks[0].clips[0].name == "Lead");
    CHECK (project.tracks[1].clips[0].name == "Kick");

    // The console
    const auto& now = controller.getKept();
    CHECK (now.numStrips == 4);
    CHECK_NEAR (now.strips[0].faderDb, 2.5f, 0.001f);
    CHECK_NEAR (now.strips[0].channel.compThresholdDb, -19.5f, 0.001f);
    CHECK_NEAR (now.strips[1].faderDb, -3.0f, 0.001f);
    CHECK (now.strips[2].mute);                                  // the bass, one place down
    CHECK (! now.strips[0].mute);

    // And the graph, so the mixer and the Inspector read the new order too.
    CHECK (controller.getGraph().strips[0].name == "Lead");
    CHECK (controller.getGraph().strips[1].name == "Kick");
    CHECK (controller.getPreparedSession().inputs[0].name == "Lead");
}

TEST_CASE ("Project: a renamed input keeps its track - the device channel is the identity")
{
    const MixSession before = band();
    auto project = bandProject (before);

    MixSession after = before;
    after.inputs[2].name = "Jewel";
    project.syncTracks (before, after);

    REQUIRE (project.tracks.size() == 4);
    REQUIRE (project.tracks[2].clips.size() == 1);
    CHECK (project.tracks[2].clips[0].name == "Keys");      // the audio did not move
    CHECK (project.tracks[2].armed);
}

TEST_CASE ("MixController: renaming an input is a label, so the mix survives it")
{
    MixController controller;
    controller.setSession (band());
    controller.prepare (kSr, kBlock);
    controller.setStripFader (1, -4.5f);

    controller.setInputName (1, "Bass DI");
    CHECK (controller.getSession().inputs[1].name == "Bass DI");
    // The console and the Inspector read the graph, so they are renamed with the timeline.
    CHECK (controller.getGraph().strips[1].name == "Bass DI");
    // Nothing was rebuilt: the fader, the graph and the stage are exactly where they were.
    CHECK (controller.isPrepared());
    CHECK (controller.getGraph().numStrips() == 4);
    CHECK_NEAR (controller.getKept().strips[1].faderDb, -4.5f, 0.001f);

    controller.setInputName (1, "");                        // an empty name is not a rename
    CHECK (controller.getSession().inputs[1].name == "Bass DI");
}

TEST_CASE ("MixController: a track's icon is a label too, and it reaches the console")
{
    MixController controller;
    controller.setSession (band());
    controller.prepare (kSr, kBlock);
    controller.setStripFader (2, -3.0f);

    CHECK (controller.getSession().inputs[2].icon.empty());     // by default the role decides
    controller.setInputIcon (2, "waveform");                    // Keys is really playback tracks
    CHECK (controller.getSession().inputs[2].icon == "waveform");
    CHECK (controller.getGraph().strips[2].icon == "waveform");  // MIXER and TUNE read the graph
    CHECK (controller.isPrepared());
    CHECK_NEAR (controller.getKept().strips[2].faderDb, -3.0f, 0.001f);
    CHECK (controller.getSession().inputs[2].role == ChannelRole::Piano);   // the routing is untouched

    controller.setInputIcon (2, "");                            // back to the source's own icon
    CHECK (controller.getSession().inputs[2].icon.empty());
    CHECK (controller.getGraph().strips[2].icon.empty());
}

TEST_CASE ("SessionStore: a chosen icon survives the round trip, and an older session has none")
{
    const auto file = scratchFolder().getChildFile ("icons.dlive.json");
    file.deleteFile();

    SessionStore::Document d;
    d.session = band();
    d.session.inputs[2].icon = "waveform";
    d.project.syncTracks (d.session);
    REQUIRE (SessionStore::save (d, file));

    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));
    REQUIRE (back.session.inputs.size() == 4);
    CHECK (back.session.inputs[2].icon == "waveform");
    CHECK (back.session.inputs[0].icon.empty());                // never written when it is not set
    file.deleteFile();
}

TEST_CASE ("DawEngine: changing the assignments keeps every clip under its own source")
{
    MixController controller;
    DawEngine daw (controller);
    const MixSession before = band();
    controller.setSession (before);
    daw.setSession (before);
    daw.setProject (bandProject (before));

    MixSession after = before;
    after.inputs.erase (after.inputs.begin() + 1);          // Bass unassigned on the ASSIGN page
    controller.setSession (after);
    daw.setSession (after);                                 // what HostServices::reconfigure does

    const auto& tracks = daw.getProject().tracks;
    REQUIRE (tracks.size() == after.inputs.size());
    for (size_t i = 0; i < after.inputs.size(); ++i)
    {
        REQUIRE (tracks[i].clips.size() == 1);
        CHECK (tracks[i].clips[0].name == juce::String (after.inputs[i].name));
    }
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

TEST_CASE ("MixBounce: group stems and a raw multitrack are folders of files, and a stereo mix is one")
{
    const auto folder = scratchFolder().getChildFile ("bounce-parts");
    folder.deleteRecursively();
    const auto kick = writeTone (folder, "Kick.wav", 1.0, 0.5f, 80.0f);
    const auto vocal = writeTone (folder, "Lead.wav", 1.0, 0.4f, 440.0f);

    MixController controller;
    const auto session = band();
    controller.setSession (session);
    controller.prepare (kSr, kBlock);

    Project project;
    project.sampleRate = kSr;
    project.syncTracks (session);
    REQUIRE (project.tracks.size() >= 2);
    auto place = [&project] (int track, const juce::File& f)
    {
        AudioClip clip;
        clip.file = f.getFullPathName();
        clip.start = 0;
        clip.length = juce::int64 (kSr);
        clip.fileSampleRate = kSr;
        project.tracks[size_t (track)].clips.push_back (clip);
    };
    place (0, kick);
    place (1, vocal);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto lengthOf = [&formats] (const juce::File& f) -> juce::int64
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        return r != nullptr ? r->lengthInSamples : -1;
    };

    // ---- group stems: one stereo file per group that is used, in a folder of their own.
    {
        MixBounce::Options options;
        options.what = MixBounce::What::GroupStems;
        juce::StringArray written;
        const auto dest = folder.getChildFile ("Sunday.wav");
        const auto err = MixBounce::renderProject (session, controller.getKept(), project, dest,
                                                   MixBounce::Format::Wav, options, &written);
        CHECK (err.isEmpty());
        const auto stems = folder.getChildFile ("Sunday stems");
        REQUIRE (stems.isDirectory());
        CHECK (! dest.existsAsFile());                       // a folder of parts, not a file
        CHECK (written.size() >= 2);
        int used = 0;
        for (int b = 0; b < int (MixBus::Master); ++b)
            if (controller.getEngine().isBusUsed (MixBus (b))) ++used;
        CHECK (written.size() == used);
        for (const auto& name : written)
        {
            const auto f = stems.getChildFile (name);
            REQUIRE (f.existsAsFile());
            CHECK (lengthOf (f) == juce::int64 (kSr));
        }
    }

    // ---- the raw multitrack: one file per assigned input, whatever the mix is doing.
    {
        MixBounce::Options options;
        options.what = MixBounce::What::RawMultitrack;
        juce::StringArray written;
        const auto err = MixBounce::renderProject (session, controller.getKept(), project,
                                                   folder.getChildFile ("Sunday.wav"),
                                                   MixBounce::Format::Wav, options, &written);
        CHECK (err.isEmpty());
        const auto raw = folder.getChildFile ("Sunday multitrack");
        REQUIRE (raw.isDirectory());
        CHECK (written.size() == int (session.inputs.size()));
        // The first input is the kick, and it comes out as what was on the disk: mono, and
        // nothing in the way of it.
        REQUIRE (! written.isEmpty());
        const auto first = raw.getChildFile (written[0]);
        REQUIRE (first.existsAsFile());
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (first));
        REQUIRE (reader != nullptr);
        CHECK (reader->numChannels == 1);
        CHECK (reader->lengthInSamples == juce::int64 (kSr));
        juce::AudioBuffer<float> audio (1, int (reader->lengthInSamples));
        reader->read (&audio, 0, int (reader->lengthInSamples), 0, true, true);
        CHECK (audio.getMagnitude (0, audio.getNumSamples()) > 0.001f);
    }

    // ---- AIFF is a stereo mix in another container, and it is still one file.
    {
        MixBounce::Options options;
        const auto dest = folder.getChildFile ("Sunday.aiff");
        CHECK (MixBounce::renderProject (session, controller.getKept(), project, dest,
                                         MixBounce::Format::Aiff, options).isEmpty());
        REQUIRE (dest.existsAsFile());
        CHECK (lengthOf (dest) == juce::int64 (kSr));
    }

    folder.deleteRecursively();
}

// A mix asked for a platform's number lands on it. The render is measured and one gain is
// applied to all of it - nothing is compressed or limited on the way out, so the shape of the
// mix that comes back is the shape that went in.
TEST_CASE ("MixBounce: a loudness target is measured from the render and met by one gain")
{
    const auto folder = scratchFolder().getChildFile ("bounce-lufs");
    folder.deleteRecursively();
    const auto tone = writeTone (folder, "Kick.wav", 4.0, 0.25f, 200.0f);

    MixController controller;
    const auto session = band();
    controller.setSession (session);
    controller.prepare (kSr, kBlock);

    Project project;
    project.sampleRate = kSr;
    project.syncTracks (session);
    AudioClip clip;
    clip.file = tone.getFullPathName();
    clip.start = 0;
    clip.length = juce::int64 (kSr * 4);
    clip.fileSampleRate = kSr;
    project.tracks[0].clips.push_back (clip);

    auto measure = [] (const juce::File& f) -> float
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr) return -120.0f;
        LoudnessMeter meter;
        meter.prepare (reader->sampleRate, 1024, int (reader->numChannels));
        juce::AudioBuffer<float> block (int (reader->numChannels), 1024);
        for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += 1024)
        {
            const int n = int (juce::jmin ((juce::int64) 1024, reader->lengthInSamples - pos));
            reader->read (&block, 0, n, pos, true, true);
            float* ptrs[2] = { block.getWritePointer (0), block.getNumChannels() > 1 ? block.getWritePointer (1) : block.getWritePointer (0) };
            AudioBlockView view { ptrs, int (reader->numChannels), n };
            meter.process (view);
        }
        return meter.getIntegratedLufs();
    };

    const auto asMixed = folder.getChildFile ("as-mixed.wav");
    CHECK (MixBounce::renderProject (session, controller.getKept(), project, asMixed, MixBounce::Format::Wav).isEmpty());
    const float before = measure (asMixed);
    REQUIRE (before > -70.0f);

    MixBounce::Options options;
    options.loudness = MixBounce::Loudness::Stream14;
    const auto streamed = folder.getChildFile ("stream.wav");
    CHECK (MixBounce::renderProject (session, controller.getKept(), project, streamed,
                                     MixBounce::Format::Wav, options).isEmpty());
    REQUIRE (streamed.existsAsFile());
    const float after = measure (streamed);
    CHECK (std::fabs (after - (-14.0f)) < 1.0f);
    // It moved on purpose: a mix that was already at the target is the only one left alone.
    if (std::fabs (before - (-14.0f)) > 1.5f) CHECK (std::fabs (after - before) > 0.5f);

    folder.deleteRecursively();
}

TEST_CASE ("SessionStore: sessions are listed from their folders without reading their audio")
{
    const auto folder = SessionStore::folderFor ("DliveListTest");
    folder.deleteRecursively();

    SessionStore::Document d;
    d.session = band();
    d.session.name = "DliveListTest";
    REQUIRE (SessionStore::save (d, SessionStore::fileFor ("DliveListTest")));

    // A big pile of audio beside the document must not slow the list down or confuse it.
    folder.getChildFile ("Audio Files").createDirectory();
    for (int i = 0; i < 20; ++i)
        writeTone (folder.getChildFile ("Audio Files"), "Take_" + juce::String (i) + ".wav", 0.05, 0.1f);

    bool found = false;
    for (const auto& listing : SessionStore::listSessions())
        if (listing.name == "DliveListTest") { found = true; CHECK (listing.file.existsAsFile()); }
    CHECK (found);
    folder.deleteRecursively();
}

TEST_CASE ("SessionStore: the library reads what a session was for without opening it")
{
    const auto folder = SessionStore::folderFor ("DliveSummaryTest");
    folder.deleteRecursively();

    SessionStore::Document d;
    d.session = band();
    d.session.name = "DliveSummaryTest";
    d.session.profile = StyleProfileId::ModernWorship;
    d.session.purpose = MixPurpose::Livestream;
    d.devices.consoleInput = "Dante Virtual Soundcard";
    d.tuneCount = 3;
    d.hasMix = true;
    // One track with a take on it, so the library can say the session has been recorded.
    d.project.tracks.resize (d.session.inputs.size());
    d.project.tracks[0].clips.push_back ({ "Kick", "Kick_001.wav", 0, 0, 4800, kSr });
    const auto file = SessionStore::fileFor ("DliveSummaryTest");
    REQUIRE (SessionStore::save (d, file));

    const auto s = SessionStore::summarise (file);
    CHECK (s.valid);
    CHECK (s.profile == StyleProfileId::ModernWorship);
    CHECK (s.purpose == MixPurpose::Livestream);
    CHECK (s.inputs == int (d.session.inputs.size()));
    CHECK (s.tuneCount == 3);
    CHECK (s.hasMix);
    CHECK (s.tracks == 1);
    CHECK (s.inputDevice == "Dante Virtual Soundcard");

    // Every assigned input lands in exactly one group bus, and never in the master.
    int total = 0;
    for (int b = 0; b < int (MixBus::Master); ++b) total += s.perBus[size_t (b)];
    CHECK (total == s.inputs);

    // Anything that is not a DLIVE document says so rather than guessing.
    const auto stray = folder.getChildFile ("notes.json");
    stray.replaceWithText ("{ \"app\": \"Something Else\" }");
    CHECK (! SessionStore::summarise (stray).valid);
    CHECK (! SessionStore::summarise (folder.getChildFile ("nothing here.json")).valid);
    folder.deleteRecursively();
}

TEST_CASE ("DawEngine: every device input is measured before the mix touches it")
{
    MixController controller;
    DawEngine engine (controller);
    controller.setSession (band());
    controller.prepare (kSr, kBlock);
    engine.setSession (controller.getSession());
    engine.prepare (kSr, kBlock);

    // Nothing has arrived yet: no channel is carrying signal.
    CHECK (engine.numInputsCarryingSignal() == 0);

    // Channel 0 loud, channel 1 silent - the page must be able to tell them apart even
    // though neither has been named yet.
    std::vector<std::vector<float>> in (4, std::vector<float> (kBlock, 0.0f));
    for (int i = 0; i < kBlock; ++i) in[0][size_t (i)] = 0.5f;
    std::vector<const float*> ip;
    for (auto& c : in) ip.push_back (c.data());
    std::vector<float> outL (kBlock), outR (kBlock);
    float* op[2] = { outL.data(), outR.data() };
    engine.processBlock (ip.data(), 4, op, 2, kBlock);

    CHECK (engine.inputPeakDb (0) > -8.0f);
    CHECK (engine.inputPeakDb (1) <= -119.0f);
    CHECK (engine.numInputsCarryingSignal() == 1);
    CHECK (engine.inputPeakDb (-1) <= -119.0f);
    CHECK (engine.inputPeakDb (kMaxInputs) <= -119.0f);
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

// ---------------------------------------------------------------------------
// LIVE SAFE
//
// The lock is only worth having if it is enforced where the mix actually changes, rather
// than in the menu handler that happens to be the way most people reach it. These tests go
// straight at MixController, which is where every path ends up.
// ---------------------------------------------------------------------------
TEST_CASE ("LIVE SAFE: refuses what would change the mix wholesale, and says why")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    std::vector<std::string> said;
    c.onMessage = [&said] (const std::string& m) { said.push_back (m); };

    c.setLiveSafe (true);
    CHECK (c.isLiveSafe());

    // A re-tune mid-service is a whole new mix landing inside a song.
    said.clear();
    c.startTuneMix();
    CHECK (! c.isListening());
    REQUIRE (! said.empty());
    CHECK (said.back().find ("LIVE SAFE") != std::string::npos);
    CHECK (said.back().find ("locked") != std::string::npos);

    // BYPASS drops every chain at once.
    c.setBypass (true);
    CHECK (! c.isBypassed());

    // Moving the broadcast to different outputs.
    auto feeds = c.getOutputFeeds();
    feeds.feeds[0].left = 4;
    feeds.feeds[0].right = 5;
    c.setOutputFeeds (feeds);
    CHECK (c.getOutputFeeds().feeds[0].left == 0);

    // ...but the engineer's own monitor feed may always be moved: nobody else hears it.
    auto monitorFeeds = c.getOutputFeeds();
    monitorFeeds.count = 2;
    monitorFeeds.feeds[1].monitor = true;
    monitorFeeds.feeds[1].left = 2;
    monitorFeeds.feeds[1].right = 3;
    c.setOutputFeeds (monitorFeeds);
    CHECK (c.getOutputFeeds().count == 2);
    CHECK (c.getOutputFeeds().feeds[1].monitor);
    CHECK (c.hasMonitorOutput());

    c.setLiveSafe (false);
    c.setBypass (true);
    CHECK (c.isBypassed());
}

TEST_CASE ("LIVE SAFE: never locks the emergency controls, and keeps every move small")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setLiveSafe (true);
    const auto& policy = c.getLiveSafePolicy();

    // A mute is the one thing an operator must always be able to reach.
    c.setStripMute (0, true);
    CHECK (c.getKept().strips[0].mute);

    // Solo is monitoring, so it is never locked either - and it still cannot reach the master.
    c.setStripSolo (1, true);
    CHECK (c.getKept().strips[1].solo);
    CHECK (c.getMonitor().mode == SoloMode::Monitor);

    // A fader is not locked, but one move cannot throw it across the console.
    c.setStripFader (2, 0.0f);
    c.setStripFader (2, 30.0f);
    CHECK_NEAR (c.getKept().strips[2].faderDb, policy.maxFaderStepDb, 0.01);
    // Moving it again gets further: it is a step limit, not a ceiling.
    c.setStripFader (2, 30.0f);
    CHECK_NEAR (c.getKept().strips[2].faderDb, 2.0f * policy.maxFaderStepDb, 0.01);

    // The master is the broadcast, so it moves in smaller steps still.
    c.setBusFader (MixBus::Master, -20.0f);
    CHECK_NEAR (c.getKept().master().faderDb, -policy.maxMasterStepDb, 0.01);

    // With the lock off, the same move lands where it was asked to.
    c.setLiveSafe (false);
    c.setStripFader (2, 0.0f);
    CHECK_NEAR (c.getKept().strips[2].faderDb, 0.0f, 0.01);
}

// ---------------------------------------------------------------------------
// The monitor bus, at the controller
// ---------------------------------------------------------------------------
TEST_CASE ("Monitor: solo is monitoring, and the mix that is published is unchanged by it")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);

    const auto before = c.getRunning();
    c.setStripSolo (0, true);
    const auto after = c.getRunning();

    // Every fader, every chain, every bus: identical. Only the solo flag and the monitor
    // state moved, and neither of those is heard anywhere but the monitor output.
    CHECK (MixPlanner::countParameterChanges (before, after) == 0);
    for (int i = 0; i < after.numStrips; ++i)
        CHECK_NEAR (after.strips[size_t (i)].faderDb, before.strips[size_t (i)].faderDb, 1.0e-6);
    for (int b = 0; b < int (MixBus::Count); ++b)
        CHECK_NEAR (after.buses[size_t (b)].faderDb, before.buses[size_t (b)].faderDb, 1.0e-6);

    CHECK (c.numSoloed() == 1);
    c.setBusSolo (MixBus::Drums, true);
    c.setFxSolo (FxSlot::VocalPlate, true);
    CHECK (c.numSoloed() == 3);
    c.clearSolos();
    CHECK (c.numSoloed() == 0);
    CHECK (! c.anySolo());

    // The returns as one group: S on the FX RETURNS tile solos every return the session uses
    // and none it does not, and the mix that is published is still untouched.
    int usedReturns = 0;
    for (int f = 0; f < int (FxSlot::Count); ++f) if (c.getGraph().fxUsed[size_t (f)]) ++usedReturns;
    REQUIRE (usedReturns > 0);
    c.setFxSoloAll (true);
    CHECK (c.anyFxSolo());
    CHECK (c.numSoloed() == usedReturns);
    for (int f = 0; f < int (FxSlot::Count); ++f)
        CHECK (c.getKept().fx[size_t (f)].solo == c.getGraph().fxUsed[size_t (f)]);
    CHECK (MixPlanner::countParameterChanges (before, c.getRunning()) == 0);
    c.setFxSoloAll (false);
    CHECK (! c.anyFxSolo());
    CHECK (c.numSoloed() == 0);
}

// ---------------------------------------------------------------------------
// Mix history
// ---------------------------------------------------------------------------
TEST_CASE ("Mix history: a change can be undone by name, and redone")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);

    auto chain = c.getKept().strips[0].channel;
    const float was = chain.hpfHz;
    chain.hpfHz = was + 30.0f;
    c.setStripChannel (0, chain);
    CHECK_NEAR (c.getKept().strips[0].channel.hpfHz, was + 30.0f, 0.01);

    REQUIRE (c.canUndoMix());
    CHECK (c.undoMixLabel() == "a processing change");
    c.undoMix();
    CHECK_NEAR (c.getKept().strips[0].channel.hpfHz, was, 0.01);

    REQUIRE (c.canRedoMix());
    c.redoMix();
    CHECK_NEAR (c.getKept().strips[0].channel.hpfHz, was + 30.0f, 0.01);

    // Undo and redo are never locked: going back to the mix that was working a minute ago is
    // exactly what an operator needs most in the middle of a service.
    c.setLiveSafe (true);
    c.undoMix();
    CHECK_NEAR (c.getKept().strips[0].channel.hpfHz, was, 0.01);
}

// ---------------------------------------------------------------------------
// Linked faders
// ---------------------------------------------------------------------------
TEST_CASE ("Linked faders: members move by the same amount, keep their balance, and one can be moved alone")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setStripFader (0, -6.0f);
    c.setStripFader (1, -2.0f);
    c.setStripFader (2, 4.0f);

    const int group = c.linkStrips ({ 0, 1 });
    REQUIRE (group != 0);
    CHECK (c.getStripLink (0) == group);
    CHECK (c.getStripLink (1) == group);
    CHECK (c.getStripLink (2) == 0);
    CHECK ((c.linkedWith (0) == std::vector<int> { 1 }));
    CHECK (c.linkedNames (0) == band().inputs[1].name);

    // Linking starts them level, at the fader of the channel the link was made from.
    CHECK_NEAR (c.getKept().strips[0].faderDb, -6.0f, 0.01);
    CHECK_NEAR (c.getKept().strips[1].faderDb, -6.0f, 0.01);

    // Cmd-drag sets a balance between them (this one alone) ...
    c.setStripFader (1, -2.0f, false);
    CHECK_NEAR (c.getKept().strips[0].faderDb, -6.0f, 0.01);

    // ... and from then on, held from either end, the other member moves by the same dB and the 4 dB is kept.
    c.setStripFader (0, -3.0f);
    CHECK_NEAR (c.getKept().strips[1].faderDb, 1.0f, 0.01);
    c.setStripFader (1, -1.0f);
    CHECK_NEAR (c.getKept().strips[0].faderDb, -5.0f, 0.01);
    CHECK_NEAR (c.getKept().strips[2].faderDb, 4.0f, 0.01);        // an unlinked strip never moves

    c.setStripFader (0, 0.0f, false);
    CHECK_NEAR (c.getKept().strips[1].faderDb, -1.0f, 0.01);

    // A member at the end of its travel stops there; the held one still lands where it was put.
    c.setStripFader (1, 10.0f, false);
    c.setStripFader (0, 5.0f);
    CHECK_NEAR (c.getKept().strips[0].faderDb, 5.0f, 0.01);
    CHECK_NEAR (c.getKept().strips[1].faderDb, 12.0f, 0.01);

    // Solo follows the link - hearing one overhead on its own is never what S on a pair means -
    // and so does un-solo from either member. Mute and pan stay each channel's own.
    c.setStripSolo (0, true);
    CHECK (c.getKept().strips[1].solo);
    c.setStripSolo (1, false);
    CHECK (! c.getKept().strips[0].solo);
    c.setStripMute (0, true);
    CHECK (! c.getKept().strips[1].mute);
    c.setStripMute (0, false);

    // Linking a third to a member brings the group along, not a new pair, and the whole group
    // takes the new member's level (it is the channel the link was made from).
    CHECK (c.linkStrips ({ 2, 1 }) == group);
    CHECK ((c.linkedWith (0) == std::vector<int> { 1, 2 }));
    CHECK_NEAR (c.getKept().strips[0].faderDb, 4.0f, 0.01);
    CHECK_NEAR (c.getKept().strips[1].faderDb, 4.0f, 0.01);

    // Taking one out leaves the other two linked; a group of one dissolves.
    c.unlinkStrip (2);
    CHECK (c.getStripLink (2) == 0);
    CHECK (c.getStripLink (0) == group);
    c.unlinkStrip (0);
    CHECK (c.getStripLink (1) == 0);

    // Linking is a mix change, so UNDO brings the link back.
    CHECK (c.canUndoMix());
    c.undoMix();
    CHECK (c.getStripLink (0) == group);
    CHECK (c.getStripLink (1) == group);
}

TEST_CASE ("Linked faders: LIVE SAFE keeps every member's step small, and the link survives a save and a rearrangement")
{
    MixController c;
    c.setSession (band());
    c.prepare (kSr, kBlock);
    c.setStripFader (3, -20.0f);
    REQUIRE (c.linkStrips ({ 0, 1 }) != 0);
    c.setLiveSafe (true);
    // Linking is allowed mid-service, and the levelling it does is a fader move like any other:
    // the far member comes as close as the policy's step allows, no further.
    CHECK (c.linkStrips ({ 0, 3 }) != 0);
    CHECK_NEAR (c.getKept().strips[3].faderDb, -20.0f + c.getLiveSafePolicy().maxFaderStepDb, 0.01);
    c.setLiveSafe (false);
    c.setStripFader (3, 0.0f, false);
    c.setLiveSafe (true);
    c.setStripFader (0, 12.0f);       // asks for +12, gets the policy's step, and so does every member
    const float step = c.getLiveSafePolicy().maxFaderStepDb;
    CHECK_NEAR (c.getKept().strips[0].faderDb, step, 0.01);
    CHECK_NEAR (c.getKept().strips[1].faderDb, step, 0.01);
    CHECK_NEAR (c.getKept().strips[3].faderDb, step, 0.01);
    c.setLiveSafe (false);

    // The link is part of the kept mix: it is written with the session and read back.
    const auto folder = scratchFolder().getChildFile ("linked-session");
    folder.deleteRecursively();
    folder.createDirectory();
    SessionStore::Document d;
    d.session = band();
    d.project.syncTracks (d.session);
    d.hasMix = true;
    d.mix = c.getKept();
    const auto file = folder.getChildFile ("linked.dlive.json");
    CHECK (SessionStore::save (d, file));
    SessionStore::Document back;
    CHECK (SessionStore::load (file, back));
    CHECK (back.mix.strips[0].linkGroup == c.getKept().strips[0].linkGroup);
    CHECK (back.mix.strips[1].linkGroup == c.getKept().strips[0].linkGroup);
    CHECK (back.mix.strips[2].linkGroup == 0);
    folder.deleteRecursively();

    // Rearranging the inputs carries the link with each strip, because it is the strip's.
    const MixSession before = band();
    MixSession after = before;
    auto moved = after.inputs[3];
    after.inputs.erase (after.inputs.begin() + 3);
    after.inputs.insert (after.inputs.begin(), moved);
    MixParameters baseline;
    baseline.numStrips = after.numStrips();
    const auto carried = carryMix (c.getKept(), before, baseline, after);
    CHECK (carried.strips[0].linkGroup != 0);                                   // the moved input (was 3)
    CHECK (carried.strips[1].linkGroup == carried.strips[0].linkGroup);         // was 0
    CHECK (carried.strips[2].linkGroup == carried.strips[0].linkGroup);         // was 1
    CHECK (carried.strips[3].linkGroup == 0);                                   // was 2
}

// ---------------------------------------------------------------------------
// Input mappings
// ---------------------------------------------------------------------------
TEST_CASE ("Input mappings: a patch is saved, listed, applied and exported")
{
    const auto folder = scratchFolder().getChildFile ("maps");
    folder.deleteRecursively();

    MixSession s;
    s.name = "Main hall";
    s.delivery = DeliveryLoudness::StreamingLoud;
    s.inputs = { { "Kick",   ChannelRole::KickIn,      0, -1 },
                 { "Snare",  ChannelRole::SnareTop,    1, -1 },
                 { "Crowd",  ChannelRole::CrowdMic,    8,  9 },
                 { "Sax",    ChannelRole::SaxTenor,   12, -1 },
                 { "Lead",   ChannelRole::LeadVocal,  16, -1 } };

    auto map = InputMapStore::fromSession (s, "Sunday rig", "Test Desk", 32, true);
    CHECK (map.channelsNeeded() == 17);
    CHECK (map.numStereo() == 1);

    // The document survives a round trip through JSON, source roles and stereo links included.
    const auto file = folder.getChildFile ("sunday.dlivemap.json");
    REQUIRE (InputMapStore::saveAs (map, file));
    InputMap back;
    REQUIRE (InputMapStore::load (file, back));
    CHECK (back.name == "Sunday rig");
    CHECK (back.inputs.size() == 5);
    CHECK (back.inputs[2].role == ChannelRole::CrowdMic);
    CHECK (back.inputs[2].isStereo());
    CHECK (back.delivery == DeliveryLoudness::StreamingLoud);

    // A document from an unknown schema is ignored rather than half-read.
    auto bad = juce::JSON::parse (file);
    if (auto* o = bad.getDynamicObject()) o->setProperty ("schema", 99);
    InputMap refused;
    CHECK (! InputMapStore::fromVar (bad, refused));

    // Applied onto a desk that has the channels: everything comes back exactly as it was.
    MixSession current;
    const auto ok = InputMapStore::apply (back, current, 32, "Test Desk", true);
    CHECK (ok.ok());
    CHECK (ok.restored == 5);
    CHECK (ok.session.inputs.size() == 5);
    CHECK (ok.session.inputs[4].name == "Lead");
    CHECK (ok.session.delivery == DeliveryLoudness::StreamingLoud);

    folder.deleteRecursively();
}

TEST_CASE ("Input mappings: a device that cannot provide a channel is told, never guessed at")
{
    MixSession s;
    s.inputs = { { "Kick", ChannelRole::KickIn,     0, -1 },
                 { "Lead", ChannelRole::LeadVocal, 16, -1 } };
    const auto map = InputMapStore::fromSession (s, "Big desk", "32 channel desk", 32, false);

    // Eight inputs available, and the lead wants channel 17.
    const auto result = InputMapStore::apply (map, MixSession {}, 8, "Small interface", false);
    CHECK (! result.ok());
    CHECK (result.restored == 1);
    CHECK (result.unavailable == 1);
    REQUIRE (result.session.inputs.size() == 2);
    // The input is kept, named and switched off - never moved onto a channel that exists,
    // which is how the pastor's microphone ends up on the kick.
    CHECK (result.session.inputs[1].name == "Lead");
    CHECK (result.session.inputs[1].inputA == 16);
    CHECK (! result.session.inputs[1].enabled);
    CHECK (result.session.inputs[0].enabled);
    REQUIRE (! result.problems.empty());
    CHECK (result.problems.front().contains ("32"));
    CHECK (result.problems.front().contains ("8"));

    // Two inputs on one channel is a map that has been edited by hand: reported, not applied.
    MixSession clash;
    clash.inputs = { { "A", ChannelRole::KickIn, 3, -1 }, { "B", ChannelRole::SnareTop, 3, -1 } };
    const auto both = InputMapStore::apply (InputMapStore::fromSession (clash, "clash", "", 8, false),
                                            MixSession {}, 8, "Small interface", false);
    CHECK (both.conflicts == 1);
    CHECK (! both.session.inputs[1].enabled);
}

// ---------------------------------------------------------------------------
// Delivery loudness, and opening older sessions
// ---------------------------------------------------------------------------
TEST_CASE ("SessionStore: the delivery loudness, the monitor and the AMBIENCE bus survive a round trip")
{
    SessionStore::Document d;
    d.session = band();
    d.session.inputs.push_back ({ "Crowd", ChannelRole::CrowdMic, 8, 9 });
    d.session.delivery = DeliveryLoudness::StreamingLoud;
    d.project.syncTracks (d.session);
    d.hasMix = true;
    d.mix.numStrips = int (d.session.inputs.size());
    d.mix.monitor.mode = SoloMode::InPlace;
    d.mix.monitor.point = SoloPoint::PFL;
    d.mix.monitor.gainDb = -6.0f;
    d.mix.monitor.dim = true;
    d.mix.buses[size_t (MixBus::Ambience)].faderDb = -4.0f;
    d.mix.buses[size_t (MixBus::Master)].faderDb = -1.5f;
    d.mix.fx[size_t (FxSlot::VocalPlate)].solo = true;
    d.outputs.count = 2;
    d.outputs.feeds[1].monitor = true;
    d.outputs.feeds[1].left = 2;
    d.outputs.feeds[1].right = 3;
    d.trackPanelWidth = 340;

    const auto file = scratchFolder().getChildFile ("delivery.dlive.json");
    REQUIRE (SessionStore::save (d, file));
    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));

    CHECK (back.session.delivery == DeliveryLoudness::StreamingLoud);
    CHECK_NEAR (back.session.deliveryTargetLufs(), -14.0f, 0.01);
    CHECK (back.session.inputs.back().role == ChannelRole::CrowdMic);
    CHECK (back.mix.monitor.mode == SoloMode::InPlace);
    CHECK (back.mix.monitor.point == SoloPoint::PFL);
    CHECK_NEAR (back.mix.monitor.gainDb, -6.0f, 0.01);
    CHECK (back.mix.monitor.dim);
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Ambience)].faderDb, -4.0f, 0.01);
    CHECK_NEAR (back.mix.master().faderDb, -1.5f, 0.01);
    CHECK (back.mix.fx[size_t (FxSlot::VocalPlate)].solo);
    CHECK (back.outputs.feeds[1].monitor);
    CHECK (back.trackPanelWidth == 340);
    file.deleteFile();
}

TEST_CASE ("SessionStore: a session saved before AMBIENCE existed opens with its master on the master")
{
    // A version 3 document: six bus slots, the last of which was the master.
    auto* mix = new juce::DynamicObject();
    mix->setProperty ("numStrips", 1);
    juce::Array<juce::var> strips;
    auto* strip = new juce::DynamicObject();
    strip->setProperty ("faderDb", -2.0);
    strips.add (juce::var (strip));
    mix->setProperty ("strips", strips);
    juce::Array<juce::var> buses;
    for (int b = 0; b < 6; ++b)
    {
        auto* bo = new juce::DynamicObject();
        bo->setProperty ("faderDb", double (b));      // 5 = the old master
        buses.add (juce::var (bo));
    }
    mix->setProperty ("buses", buses);

    auto* doc = new juce::DynamicObject();
    doc->setProperty ("app", "DLIVE");
    doc->setProperty ("version", 3);
    doc->setProperty ("name", "Old service");
    doc->setProperty ("purpose", int (MixPurpose::ChurchBroadcast));
    juce::Array<juce::var> inputs;
    auto* in = new juce::DynamicObject();
    in->setProperty ("name", "Kick");
    in->setProperty ("role", int (ChannelRole::KickIn));
    in->setProperty ("inputA", 0);
    in->setProperty ("inputB", -1);
    in->setProperty ("enabled", true);
    inputs.add (juce::var (in));
    doc->setProperty ("inputs", inputs);
    doc->setProperty ("hasMix", true);
    doc->setProperty ("mix", juce::var (mix));
    // An output feed pointing at the old master index.
    juce::Array<juce::var> feeds;
    auto* feed = new juce::DynamicObject();
    feed->setProperty ("left", 0);
    feed->setProperty ("right", 1);
    feed->setProperty ("source", 5);
    feeds.add (juce::var (feed));
    doc->setProperty ("outputs", feeds);

    const auto file = scratchFolder().getChildFile ("v3.dlive.json");
    file.replaceWithText (juce::JSON::toString (juce::var (doc), false));

    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));
    // Every group kept its own fader...
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Drums)].faderDb, 0.0f, 0.01);
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Speech)].faderDb, 4.0f, 0.01);
    // ...the old master's fader is on the master, not on the new AMBIENCE group...
    CHECK_NEAR (back.mix.master().faderDb, 5.0f, 0.01);
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Ambience)].faderDb, 0.0f, 0.01);
    // ...and the output feed still carries the main mix.
    CHECK (back.outputs.feeds[0].source == MixBus::Master);
    CHECK (! back.outputs.feeds[0].monitor);
    // A session from before the setting existed aims where it always aimed.
    CHECK (back.session.delivery == DeliveryLoudness::FromPurpose);
    CHECK (back.mix.monitor.mode == SoloMode::Monitor);
    file.deleteFile();
}

// ---------------------------------------------------------------------------
// Which two devices "solo goes here" should join
//
// The rule that matters is the one a sound booth cares about: never suggest the laptop
// speaker when there is a real interface plugged in. Nobody wants to discover their solo came
// out of the Mac in the middle of a sermon.
// ---------------------------------------------------------------------------
TEST_CASE ("Monitoring: the device solo goes to is chosen sensibly, and says so when it cannot be")
{
    using MonitorDevice::Device;
    using Kind = Device::Kind;
    // What a device *is* comes from CoreAudio's transport type, never from its name: the names
    // here are deliberately unhelpful, because a booth's interface can be called anything.
    juce::Array<Device> devices;
    devices.add ({ "Console", "uid-console", 32, false, false, 32, Kind::Virtual });
    devices.add ({ "Speakers", "uid-builtin", 2, false, false, 0, Kind::BuiltIn });
    devices.add ({ "GF340A", "uid-gf", 4, false, false, 2, Kind::Interface });

    // The broadcast is whatever is already carrying the mix; the headphones are the interface,
    // not the laptop, wherever they sit in the list.
    const auto s = MonitorDevice::suggestFrom (devices, "Console");
    REQUIRE (s.valid);
    CHECK (s.broadcast.uid == "uid-console");
    CHECK (s.headphones.uid == "uid-gf");
    CHECK (s.why.contains ("GF340A"));

    // With no interface the built-in output is better than nothing, and is offered - and a pair
    // of Bluetooth headphones is better still. A virtual device is never suggested for listening:
    // there is no socket on it.
    juce::Array<Device> noInterface;
    noInterface.add ({ "Loop 2ch", "uid-loop", 2, false, false, 2, Kind::Virtual });
    noInterface.add ({ "Console", "uid-console", 32, false, false, 32, Kind::Virtual });
    noInterface.add ({ "Speakers", "uid-builtin", 2, false, false, 0, Kind::BuiltIn });
    const auto fallback = MonitorDevice::suggestFrom (noInterface, "Console");
    REQUIRE (fallback.valid);
    CHECK (fallback.headphones.uid == "uid-builtin");
    noInterface.add ({ "Buds", "uid-buds", 2, false, false, 1, Kind::Bluetooth });
    CHECK (MonitorDevice::suggestFrom (noInterface, "Console").headphones.uid == "uid-buds");

    // One device and nothing else: said plainly, never guessed at.
    juce::Array<Device> alone;
    alone.add ({ "Console", "uid-console", 32, false, false, 32, Kind::Virtual });
    const auto none = MonitorDevice::suggestFrom (alone, "Console");
    CHECK (! none.valid);
    CHECK (none.problem.contains ("only one output device"));

    // A combined device DLIVE built earlier is never itself a building block, and neither is one
    // the user built.
    juce::Array<Device> withOurs;
    withOurs.add ({ "DLIVE Monitoring", "com.dine.dlive.monitoring", 34, true, true });
    withOurs.add ({ "Console", "uid-console", 32, false, false, 32, Kind::Virtual });
    withOurs.add ({ "Theirs", "uid-theirs", 6, true, false });
    withOurs.add ({ "GF340A", "uid-gf", 4, false, false, 2, Kind::Interface });
    const auto again = MonitorDevice::suggestFrom (withOurs, "DLIVE Monitoring");
    REQUIRE (again.valid);
    CHECK (again.broadcast.uid == "uid-console");
    CHECK (again.headphones.uid == "uid-gf");
}

// ---------------------------------------------------------------------------
// The channel layout of the device DLIVE builds
//
// What broke with Dante: sixty-four Dante outputs, then the Scarlett at 64-65 - and the host
// opened the first sixteen channels, so solo pointed at a pair that was never open; and the
// console was opened twice, once as the input device and once inside the built device, glued
// together by JUCE's own combiner. The layout puts the console inside the built device (its
// inputs first, so channel 1 stays channel 1) and the host opens exactly the pairs the feeds
// need, wherever they sit.
// ---------------------------------------------------------------------------
TEST_CASE ("Monitoring: the built device carries the console's inputs first and the solo pair wherever it falls")
{
    using MonitorDevice::Device;
    const Device dante    { "Dante Virtual Soundcard", "uid-dante", 64, false, false, 64 };
    const Device scarlett { "Scarlett 2i2 USB", "uid-scarlett", 2, false, false, 2 };
    const Device usbDesk  { "X32 USB", "uid-x32", 0, false, false, 32 };   // a console that only sends inputs
    const Device macMic   { "MacBook Pro Microphone", "uid-mic", 0, false, false, 1 };

    // The usual booth: the console comes in and goes out on Dante, the headphones are on the interface.
    {
        const auto l = MonitorDevice::layoutFor (dante, scarlett, &dante);
        REQUIRE (l.problem.isEmpty());
        REQUIRE (l.pieces.size() == 2);
        CHECK (l.pieces[0].uid == "uid-dante");
        CHECK (l.pieces[1].uid == "uid-scarlett");
        CHECK (l.carriesInput);
        CHECK (l.broadcastChannel == 0);
        CHECK (l.headphoneChannel == 64);
        // The host opens the solo pair and as much of the Dante as fits beside it - never more
        // than the engine can address, and the solo pair is always in.
        const auto open = MonitorDevice::outputChannelsToOpen (l, dante.outputChannels, kMaxOutputs);
        CHECK (open.countNumberOfSetBits() == kMaxOutputs);
        CHECK (open[0]); CHECK (open[1]); CHECK (open[13]);
        CHECK (! open[14]);
        CHECK (open[64]); CHECK (open[65]);
        CHECK (! open[66]);
    }
    // The console comes in on the interface the headphones are in: the interface goes first
    // (its inputs keep their numbers), the broadcast follows.
    {
        const auto l = MonitorDevice::layoutFor (dante, scarlett, &scarlett);
        REQUIRE (l.problem.isEmpty());
        REQUIRE (l.pieces.size() == 2);
        CHECK (l.pieces[0].uid == "uid-scarlett");
        CHECK (l.carriesInput);
        CHECK (l.headphoneChannel == 0);
        CHECK (l.broadcastChannel == 2);
        const auto open = MonitorDevice::outputChannelsToOpen (l, dante.outputChannels, kMaxOutputs);
        CHECK (open[0]); CHECK (open[1]); CHECK (open[2]); CHECK (open[15]); CHECK (! open[16]);
    }
    // A third device carries the inputs: it goes first and, having no outputs, moves nothing.
    {
        const auto l = MonitorDevice::layoutFor (dante, scarlett, &usbDesk);
        REQUIRE (l.pieces.size() == 3);
        CHECK (l.pieces[0].uid == "uid-x32");
        CHECK (l.carriesInput);
        CHECK (l.broadcastChannel == 0);
        CHECK (l.headphoneChannel == 64);
        const auto mic = MonitorDevice::layoutFor (dante, scarlett, &macMic);
        CHECK (mic.pieces.size() == 3);
        CHECK (mic.carriesInput);
    }
    // No console at all (a session played from stems): two pieces, opened for output only.
    {
        const auto l = MonitorDevice::layoutFor (dante, scarlett, nullptr);
        REQUIRE (l.pieces.size() == 2);
        CHECK (! l.carriesInput);
        CHECK (l.headphoneChannel == 64);
    }
    // A small broadcast device: every one of its outputs is opened beside the solo pair.
    {
        const Device small { "Scarlett 4i4 USB", "uid-4i4", 4, false, false, 4 };
        const auto l = MonitorDevice::layoutFor (small, scarlett, &small);
        CHECK (l.headphoneChannel == 4);
        const auto open = MonitorDevice::outputChannelsToOpen (l, small.outputChannels, kMaxOutputs);
        CHECK (open.countNumberOfSetBits() == 6);
        CHECK (open[3]); CHECK (open[4]); CHECK (open[5]);
    }
    // What cannot be built is said, not attempted.
    CHECK (MonitorDevice::layoutFor (dante, dante, &dante).problem.isNotEmpty());
    const Device theirs { "My Aggregate", "uid-theirs", 66, true, false, 64 };
    CHECK (MonitorDevice::layoutFor (theirs, scarlett, &theirs).problem.contains ("combined device"));
    // A *virtual* device is not a combined one. The Dante Virtual Soundcard reports itself as
    // virtual, and it is the broadcast and the console in the setup this whole thing exists for:
    // refusing it as "already a combined device" left solo with nowhere to go (2026-09-21).
    const Device dvs { "Console", "uid-dvs", 16, false, false, 16, Device::Kind::Virtual };
    {
        const auto l = MonitorDevice::layoutFor (dvs, scarlett, &dvs);
        REQUIRE (l.problem.isEmpty());
        REQUIRE (l.pieces.size() == 2);
        CHECK (l.pieces[0].uid == "uid-dvs");
        CHECK (l.carriesInput);
        CHECK (l.headphoneChannel == 16);
    }
    // ... and as the console alone, with the broadcast on the interface the headphones are on.
    CHECK (MonitorDevice::layoutFor (scarlett, dvs, &dvs).problem.isEmpty());
    CHECK (MonitorDevice::layoutFor (theirs, scarlett, &dvs).problem.contains ("combined device"));
}

// ---------------------------------------------------------------------------
// THE CANONICAL SESSION
//
// One owned model (SessionState), one function that reads it out of the live objects and one
// that puts it back - and both of them compiled into this binary, which is the point. The
// code these replaced lived in app/Main.cpp, which no test target compiles, and that is why
// the same four bugs kept coming back. docs/SESSION-STATE.md has the audit.
// ---------------------------------------------------------------------------

namespace
{
    // A session with something in every corner of it: a tuned-looking mix on every strip, the
    // buses, the returns, the master, the monitor, the macros, a link, a scene, a reference,
    // some track history, two output feeds, LIVE SAFE with limits of its own, and a name for
    // one drum strip's sound.
    struct FullSession
    {
        MixController controller;
        DawEngine daw { controller };

        FullSession (bool openDevice)
        {
            auto s = band();
            s.purpose = MixPurpose::Livestream;
            s.profile = StyleProfileId::ModernWorship;
            s.delivery = DeliveryLoudness::StreamingLoud;
            s.voicing = MasterVoicing::Car;
            s.speechPriority = true;
            s.inputs[3].focus = true;                  // the lead is pinned
            s.inputs[2].icon = "waveform";
            controller.setSession (s);
            daw.setSession (s);
            if (openDevice) controller.prepare (kSr, kBlock);

            auto mix = controller.getKept();
            mix.strips[0].faderDb = -4.5f;
            mix.strips[0].channel.replaceEnabled = true;
            mix.strips[0].channel.replaceSound = 1;
            mix.strips[0].channel.compThresholdDb = -27.5f;
            mix.strips[1].inputGainDb = 3.0f;
            mix.strips[1].mute = true;
            mix.strips[2].pan = -0.4f;
            mix.strips[3].sendDb[size_t (FxSlot::VocalPlate)] = -7.0f;
            mix.strips[3].channel.toneBands[2] = { true, FilterType::Peak, 3200.0f, 2.5f, 1.1f };
            mix.buses[size_t (MixBus::Drums)].channel.compRatio = 3.3f;
            mix.buses[size_t (MixBus::Vocals)].faderDb = -1.5f;
            mix.master().channel.limiterCeilingDb = -1.5f;
            mix.fx[size_t (FxSlot::VocalPlate)].fx.reverbDecayS = 2.4f;
            mix.fx[size_t (FxSlot::VocalPlate)].returnDb = -2.0f;
            mix.fxReturnDb = -1.0f;
            mix.tempoBpm = 96.0f;
            mix.monitor.gainDb = -8.0f;
            mix.monitor.source = MixBus::Drums;
            mix.monitor.point = SoloPoint::PFL;
            controller.restoreKept (mix, 2);
            controller.linkStrips ({ 3, 4 });
            for (int i = 0; i < int (MixMacro::Count); ++i) controller.setMacro (MixMacro (i), 40.0f + float (i));

            OutputFeeds feeds;
            feeds.count = 2;
            feeds.feeds[0] = { 0, 1, MixBus::Master, 0.0f, false, false, false };
            feeds.feeds[1].monitor = true;
            feeds.feeds[1].left = 2;
            feeds.feeds[1].right = 3;
            controller.setOutputFeeds (feeds);

            ReferenceProfile ref;
            ref.valid = true;
            ref.name = "Sunday reference";
            ref.seconds = 184.0f;                   // a reference has to be long enough to be one
            ref.loudnessLufs = -14.2f;
            ref.crestFactorDb = 9.5f;
            controller.setReference (ref);

            controller.keepScene (1);
            controller.renameScene (1, "Sermon");

            auto policy = LiveSafePolicy::armed();
            policy.maxFaderStepDb = 2.5f;
            policy.maxMacroExcursion = 8.0f;
            controller.setLiveSafePolicy (policy);
            daw.setLiveSafe (true);

            auto project = daw.getProject();
            project.tempo = 84.0;
            project.loopEnabled = true;
            project.loopStart = 480;
            project.loopEnd = 96000;
            project.markers.push_back ({ "Sermon", 48000 });
            if (! project.tracks.empty())
            {
                project.tracks[0].armed = true;
                project.tracks[0].monitor = MonitorMode::Input;
                project.tracks[0].height = 96;
            }
            daw.setProject (project);
        }
    };

    // Everything that has to come back, compared field by field.
    void checkSameSession (const SessionState& a, const SessionState& b)
    {
        CHECK (a.session.name == b.session.name);
        CHECK (a.session.purpose == b.session.purpose);
        CHECK (a.session.profile == b.session.profile);
        CHECK (a.session.delivery == b.session.delivery);
        CHECK (a.session.voicing == b.session.voicing);
        CHECK (a.session.speechPriority == b.session.speechPriority);
        CHECK (a.session.focusInput() == b.session.focusInput());
        REQUIRE (a.session.inputs.size() == b.session.inputs.size());
        for (size_t i = 0; i < a.session.inputs.size(); ++i)
        {
            CHECK (a.session.inputs[i].name == b.session.inputs[i].name);
            CHECK (a.session.inputs[i].role == b.session.inputs[i].role);
            CHECK (a.session.inputs[i].icon == b.session.inputs[i].icon);
            CHECK (a.session.inputs[i].inputA == b.session.inputs[i].inputA);
            CHECK (a.session.inputs[i].inputB == b.session.inputs[i].inputB);
        }
        CHECK (a.hasMix == b.hasMix);
        CHECK (a.mix.numStrips == b.mix.numStrips);
        CHECK (MixPlanner::countParameterChanges (a.mix, b.mix) == 0);
        for (int i = 0; i < a.mix.numStrips; ++i)
        {
            CHECK (a.mix.strips[size_t (i)].mute == b.mix.strips[size_t (i)].mute);
            CHECK (a.mix.strips[size_t (i)].linkGroup == b.mix.strips[size_t (i)].linkGroup);
            CHECK_NEAR (a.mix.strips[size_t (i)].faderDb, b.mix.strips[size_t (i)].faderDb, 1e-3f);
            CHECK_NEAR (a.mix.strips[size_t (i)].pan, b.mix.strips[size_t (i)].pan, 1e-3f);
        }
        CHECK_NEAR (a.mix.tempoBpm, b.mix.tempoBpm, 1e-3f);
        CHECK_NEAR (a.mix.fxReturnDb, b.mix.fxReturnDb, 1e-3f);
        CHECK (a.mix.monitor.source == b.mix.monitor.source);
        CHECK (a.mix.monitor.point == b.mix.monitor.point);
        CHECK_NEAR (a.mix.monitor.gainDb, b.mix.monitor.gainDb, 1e-3f);
        for (int i = 0; i < int (MixMacro::Count); ++i)
            CHECK_NEAR (a.macros.get (MixMacro (i)), b.macros.get (MixMacro (i)), 1e-3f);
        CHECK (a.tuneCount == b.tuneCount);
        CHECK (a.outputs.count == b.outputs.count);
        CHECK (hasMonitorFeed (a.outputs) == hasMonitorFeed (b.outputs));
        CHECK (a.reference.valid == b.reference.valid);
        CHECK (a.reference.name == b.reference.name);
        CHECK_NEAR (a.reference.loudnessLufs, b.reference.loudnessLufs, 1e-3f);
        REQUIRE (a.scenes.size() == b.scenes.size());
        for (size_t i = 0; i < a.scenes.size(); ++i)
        {
            CHECK (a.scenes[i].name == b.scenes[i].name);
            CHECK (a.scenes[i].kept == b.scenes[i].kept);
        }
        CHECK (a.history.size() == b.history.size());
        CHECK (a.project.liveSafe == b.project.liveSafe);
        CHECK_NEAR (a.safety.maxFaderStepDb, b.safety.maxFaderStepDb, 1e-3f);
        CHECK_NEAR (a.safety.maxMacroExcursion, b.safety.maxMacroExcursion, 1e-3f);
        CHECK_NEAR (float (a.project.tempo), float (b.project.tempo), 1e-3f);
        CHECK (a.project.loopEnabled == b.project.loopEnabled);
        CHECK (a.project.loopStart == b.project.loopStart);
        CHECK (a.project.loopEnd == b.project.loopEnd);
        CHECK (a.project.markers.size() == b.project.markers.size());
        REQUIRE (a.project.tracks.size() == b.project.tracks.size());
        for (size_t i = 0; i < a.project.tracks.size(); ++i)
        {
            CHECK (a.project.tracks[i].armed == b.project.tracks[i].armed);
            CHECK (a.project.tracks[i].monitor == b.project.tracks[i].monitor);
            CHECK (a.project.tracks[i].height == b.project.tracks[i].height);
        }
        CHECK (a.devices.consoleInput == b.devices.consoleInput);
        CHECK (a.devices.broadcastOutput == b.devices.broadcastOutput);
        CHECK (a.devices.soloOutput == b.devices.soloOutput);
        CHECK (a.trackPanelWidth == b.trackPanelWidth);
        for (int i = 0; i < kMaxStrips; ++i) CHECK (a.samples[size_t (i)] == b.samples[size_t (i)]);
    }

    const DeviceChoice kDevices { "Dante Virtual Soundcard", "MacBook Pro Speakers", "Scarlett 2i2" };
}

TEST_CASE ("SessionState: a session with something in every corner survives capture, save, load and apply")
{
    FullSession live (true);
    auto out = captureSession (live.controller, live.daw, kDevices, 260);
    out.samples[0] = { "kick", "Ludwig 24 Soft", false, "Kick/Ludwig 24 Soft.wav" };
    REQUIRE (out.hasMix);

    const auto file = scratchFolder().getChildFile ("canonical.dlive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (out, file));

    SessionState back;
    REQUIRE (SessionStore::load (file, back));
    checkSameSession (out, back);

    // ...and applying it to a controller that has never seen it gives the same session again,
    // including the parameters the engine would end up running.
    MixController fresh;
    DawEngine freshDaw { fresh };
    applySession (back, fresh, freshDaw);
    fresh.prepare (kSr, kBlock);
    auto again = captureSession (fresh, freshDaw, kDevices, 260);
    again.samples = out.samples;    // the library names these, and there is none in this test
    checkSameSession (out, again);
    CHECK (MixPlanner::countParameterChanges (fresh.getRunning(), live.controller.getRunning()) == 0);
    file.deleteFile();
}

TEST_CASE ("SessionState: a session with no audio device open has a whole mix, and saves it")
{
    // No prepare() anywhere in this test: nothing is playing, nothing has a sample rate. The
    // mix is still the engineer's, because rebuild() needs neither.
    FullSession offline (false);
    CHECK (! offline.controller.isPrepared());
    CHECK (offline.controller.isBuilt());
    CHECK (offline.controller.getGraph().numStrips() == 4);
    CHECK (offline.controller.hasKeptMix());
    CHECK_NEAR (offline.controller.getKept().strips[0].faderDb, -4.5f, 1e-3f);

    auto out = captureSession (offline.controller, offline.daw, kDevices, 260);
    REQUIRE (out.hasMix);                      // the gate used to be `isPrepared() && ...`
    CHECK (out.mix.numStrips == 4);

    const auto file = scratchFolder().getChildFile ("offline.dlive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (out, file));
    SessionState back;
    REQUIRE (SessionStore::load (file, back));
    checkSameSession (out, back);

    MixController fresh;
    DawEngine freshDaw { fresh };
    applySession (back, fresh, freshDaw);      // still no device
    CHECK (! fresh.isPrepared());
    CHECK (fresh.getGraph().numStrips() == 4);
    CHECK_NEAR (fresh.getKept().strips[0].faderDb, -4.5f, 1e-3f);
    checkSameSession (out, captureSession (fresh, freshDaw, kDevices, 260));
    file.deleteFile();
}

TEST_CASE ("SessionState: opening a session without its console, saving, and opening it again with one loses nothing")
{
    // The bug this is here for: unplug the interface, launch DLIVE (the last session reloads),
    // quit. shutdown() always saves, `hasMix` was gated on a device being open, and the
    // fallback that should have covered it could not run - so the tuned mix, the scenes, the
    // reference and the track history were erased from the file by launching and quitting.
    const auto file = scratchFolder().getChildFile ("unplugged.dlive.json");
    file.deleteFile();
    {
        FullSession withDevice (true);
        auto saved = captureSession (withDevice.controller, withDevice.daw, kDevices, 260);
        REQUIRE (SessionStore::save (saved, file));
    }
    SessionState onDisk;
    REQUIRE (SessionStore::load (file, onDisk));

    // Open it with nothing plugged in, and save it again - which is all quitting does.
    SessionState reopened;
    REQUIRE (SessionStore::load (file, reopened));
    MixController noDevice;
    DawEngine noDeviceDaw { noDevice };
    applySession (reopened, noDevice, noDeviceDaw);
    CHECK (! noDevice.isPrepared());
    auto resaved = captureSession (noDevice, noDeviceDaw, kDevices, 260);
    resaved.samples = reopened.samples;        // the library is what names these; none here
    REQUIRE (SessionStore::save (resaved, file));

    SessionState afterQuit;
    REQUIRE (SessionStore::load (file, afterQuit));
    checkSameSession (onDisk, afterQuit);

    // And now with a console. Everything is still there and the engine runs the same mix.
    MixController withOne;
    DawEngine withOneDaw { withOne };
    applySession (afterQuit, withOne, withOneDaw);
    withOne.prepare (kSr, kBlock);
    CHECK (withOne.hasKeptMix());
    CHECK (withOne.getTuneCount() == 2);
    CHECK (withOne.hasReference());
    CHECK (withOne.getScene (1).kept);
    CHECK (withOne.isLiveSafe());
    CHECK_NEAR (withOne.getLiveSafePolicy().maxFaderStepDb, 2.5f, 1e-3f);
    checkSameSession (afterQuit, captureSession (withOne, withOneDaw, kDevices, 260));
    file.deleteFile();
}

TEST_CASE ("SessionState: changing the device keeps the reference, the scenes and the engineer's listen")
{
    // Every device change used to be wrapped in hold() / applyPendingMix(), and hold() left the
    // reference at its default, which applyPendingMix() then wrote back over the real one - and
    // setReference() arms the save, so the loss reached the disk a second later. Opening a
    // device, changing the output and adding an input all took that path.
    FullSession live (true);
    const auto before = captureSession (live.controller, live.daw, kDevices, 260);
    REQUIRE (before.reference.valid);

    // What AudioHost does on an open, an output swap and a reconfigure: prepare() again.
    live.controller.prepare (kSr, kBlock);
    CHECK (live.controller.hasReference());
    CHECK (live.controller.getReference().name == "Sunday reference");
    CHECK (live.controller.getScene (1).kept);
    CHECK (live.controller.getScene (1).name == "Sermon");
    CHECK_NEAR (live.controller.getMonitor().gainDb, -8.0f, 1e-3f);
    CHECK (live.controller.getMonitor().source == MixBus::Drums);
    CHECK (live.controller.getTuneCount() == 2);
    CHECK (hasMonitorFeed (live.controller.getOutputFeeds()));
    checkSameSession (before, captureSession (live.controller, live.daw, kDevices, 260));

    // And adding an input keeps every other channel exactly where it was.
    auto s = live.controller.getSession();
    s.inputs.insert (s.inputs.begin() + 1, { "Snare", ChannelRole::SnareTop, 7, -1 });
    live.controller.setSession (s);
    live.daw.setSession (s);
    live.controller.prepare (kSr, kBlock);
    CHECK (live.controller.getGraph().numStrips() == 5);
    CHECK_NEAR (live.controller.getKept().strips[0].faderDb, -4.5f, 1e-3f);      // the kick did not move
    CHECK_NEAR (live.controller.getKept().strips[2].inputGainDb, 3.0f, 1e-3f);   // the bass moved along with its gain
    CHECK (live.controller.getKept().strips[2].mute);
    CHECK (live.controller.hasReference());
    CHECK (live.controller.getTuneCount() == 2);
}

TEST_CASE ("SessionState: a new session keeps nothing from the one that was open")
{
    FullSession live (true);
    REQUIRE (live.controller.hasReference());

    SessionState fresh;
    fresh.session.name = "Untitled";
    applySession (fresh, live.controller, live.daw);
    CHECK (live.controller.getSession().name == "Untitled");
    CHECK (live.controller.getSession().inputs.empty());
    CHECK (live.controller.getGraph().numStrips() == 0);
    CHECK (! live.controller.hasReference());
    CHECK (! live.controller.hasKeptMix());
    CHECK (! live.controller.getScene (1).kept);
    CHECK (live.controller.getTuneCount() == 0);
    CHECK (! live.controller.isLiveSafe());
    CHECK (live.controller.getMacros().get (MixMacro::Space) == 50.0f);
    // The bus chains are the new session's own, not the last one's carried across an empty
    // assignment list: a different document is a different mix.
    CHECK_NEAR (live.controller.getKept().buses[size_t (MixBus::Vocals)].faderDb, 0.0f, 1e-3f);
}

TEST_CASE ("SessionState: every parameter the mix carries reaches the file")
{
    // The test that makes "a field was added to the chain and nobody taught the serialiser
    // about it" impossible rather than unlikely: walk every DSP and FX id and insist the JSON
    // names it. Adding a parameter without adding it to the document now fails here.
    FullSession live (true);
    auto state = captureSession (live.controller, live.daw, kDevices, 0);
    REQUIRE (state.hasMix);
    const auto text = juce::JSON::toString (SessionStore::toVar (state), true);

    ChannelParameters channel;
    int dsp = 0;
    forEachDspParameter (channel, [&] (const std::string& id, auto&)
    {
        ++dsp;
        CHECK_MESSAGE (text.contains ("\"" + juce::String (id) + "\""), "the document does not carry " + id);
    });
    CHECK (dsp > 80);

    FxParameters fx;
    int fxCount = 0;
    forEachFxParameter (fx, [&] (const std::string& id, auto&)
    {
        ++fxCount;
        CHECK_MESSAGE (text.contains ("\"" + juce::String (id) + "\""), "the document does not carry " + id);
    });
    CHECK (fxCount > 10);
}

namespace
{
    // Turns the document this build writes into one an older build would have written: the
    // group-bus array cut back to the layout that version had (the master has always been its
    // last slot), the output feeds' `source` re-indexed to match, and every key that version
    // did not know about removed. A fixture built from the real serialiser rather than typed
    // out, so it cannot drift away from what the code actually writes.
    juce::var downgradeDocument (const juce::var& v, int toVersion)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return v;
        auto* out = new juce::DynamicObject (*obj);
        out->setProperty ("version", toVersion);

        const int busCount = toVersion >= 4 ? int (MixBus::Count)
                           : toVersion == 3 ? int (MixBus::Count) - 1
                                            : int (MixBus::Count) - 2;
        auto trimBuses = [busCount] (juce::DynamicObject* mix)
        {
            if (mix == nullptr) return;
            if (auto* buses = mix->getProperty ("buses").getArray())
            {
                juce::Array<juce::var> older;
                for (int b = 0; b + 1 < busCount && b < buses->size(); ++b) older.add (buses->getReference (b));
                older.add (buses->getReference (buses->size() - 1));     // the master, wherever it sat
                mix->setProperty ("buses", older);
            }
        };
        trimBuses (out->getProperty ("mix").getDynamicObject());
        if (auto* scenes = out->getProperty ("scenes").getArray())
            for (auto& sv : *scenes)
                if (auto* so = sv.getDynamicObject()) trimBuses (so->getProperty ("mix").getDynamicObject());
        if (auto* feeds = out->getProperty ("outputs").getArray())
            for (auto& fv : *feeds)
                if (auto* fo = fv.getDynamicObject())
                    if (int (fo->getProperty ("source")) >= busCount - 1)
                        fo->setProperty ("source", busCount - 1);

        if (toVersion < 5) { out->removeProperty ("liveSafeLimits"); out->removeProperty ("samples"); }
        if (toVersion < 4)
        {
            out->removeProperty ("delivery");
            if (auto* mix = out->getProperty ("mix").getDynamicObject()) mix->removeProperty ("monitor");
        }
        if (toVersion < 2) out->removeProperty ("project");
        return juce::var (out);
    }
}

TEST_CASE ("SessionStore: a document from every version DLIVE has ever written still opens")
{
    FullSession live (true);
    auto state = captureSession (live.controller, live.daw, kDevices, 260);
    state.samples[0] = { "kick", "Ludwig 24 Soft", false, "Kick/Ludwig 24 Soft.wav" };
    for (int version = 1; version <= SessionStore::kVersion; ++version)
    {
        // Serialised afresh each time: juce::var holds its objects by reference, so downgrading
        // one document in place would quietly downgrade every later version's too.
        const auto older = downgradeDocument (SessionStore::toVar (state), version);
        SessionState back;
        REQUIRE (SessionStore::fromVar (older, back));

        // The assignments, the purpose and the sound have been in the file since version 1.
        CHECK (back.session.name == state.session.name);
        REQUIRE (back.session.inputs.size() == state.session.inputs.size());
        CHECK (back.session.inputs[3].role == ChannelRole::LeadVocal);
        CHECK (back.session.inputs[3].focus);
        REQUIRE (back.hasMix);
        CHECK (back.mix.numStrips == state.mix.numStrips);
        CHECK_NEAR (back.mix.strips[0].faderDb, -4.5f, 1e-3f);

        // Whatever layout the file used, a stored group is the group it always was and the
        // last slot is the master.
        CHECK_NEAR (back.mix.buses[size_t (MixBus::Drums)].channel.compRatio, 3.3f, 1e-3f);
        CHECK_NEAR (back.mix.master().channel.limiterCeilingDb, -1.5f, 1e-3f);
        CHECK (back.outputs.feeds[0].source == MixBus::Master);
        if (version >= 4) CHECK_NEAR (back.mix.buses[size_t (MixBus::Vocals)].faderDb, -1.5f, 1e-3f);

        // What a version did not carry takes its default, and nothing is guessed at.
        if (version >= 4)
        {
            CHECK (back.session.delivery == DeliveryLoudness::StreamingLoud);
            CHECK (back.mix.monitor.source == MixBus::Drums);
            CHECK_NEAR (back.mix.monitor.gainDb, -8.0f, 1e-3f);
        }
        else
        {
            CHECK (back.session.delivery == DeliveryLoudness::FromPurpose);
            CHECK (back.mix.monitor.mode == SoloMode::Monitor);          // the default listen
        }
        if (version >= 5)
        {
            CHECK_NEAR (back.safety.maxFaderStepDb, 2.5f, 1e-3f);
            CHECK (back.samples[0].name == "Ludwig 24 Soft");
        }
        else
        {
            CHECK_NEAR (back.safety.maxFaderStepDb, LiveSafePolicy {}.maxFaderStepDb, 1e-3f);
            CHECK (! back.samples[0].set());        // resolved from the index against the library instead
        }
        if (version >= 2) CHECK (back.project.markers.size() == 1);
        else              CHECK (back.project.tracks.size() == state.session.inputs.size());   // syncTracks builds them

        // And it applies: the mix a version-1 document describes still reaches the engine.
        MixController fresh;
        DawEngine freshDaw { fresh };
        applySession (back, fresh, freshDaw);
        fresh.prepare (kSr, kBlock);
        CHECK (fresh.hasKeptMix());
        CHECK_NEAR (fresh.getKept().strips[0].faderDb, -4.5f, 1e-3f);
        CHECK_NEAR (fresh.getKept().master().channel.limiterCeilingDb, -1.5f, 1e-3f);
    }
}

TEST_CASE ("SessionState: every change to the session moves its revision, and nothing else does")
{
    FullSession live (true);
    auto at = [&] { return live.controller.getRevision(); };

    // A mix change, an assignment change, a label, a setting, a scene, the monitor, the
    // routing, the timeline: whatever moved, the revision moved with it. This is what the host
    // watches, so a change that does not show up here is a change that never reaches the disk.
    struct Case { const char* what; std::function<void()> go; };
    const std::vector<Case> cases {
        { "a fader",            [&] { live.controller.setStripFader (0, -6.0f); } },
        { "a mute",             [&] { live.controller.setStripMute (2, true); } },
        { "a send",             [&] { live.controller.setStripSend (3, FxSlot::VocalDelay, -9.0f); } },
        { "an input gain",      [&] { live.controller.setStripInputGain (1, 5.0f); } },
        { "a bus fader",        [&] { live.controller.setBusFader (MixBus::Drums, -2.0f); } },
        { "the returns",        [&] { live.controller.setFxReturn (-3.0f); } },
        { "a macro",            [&] { live.controller.setMacro (MixMacro::Space, 61.0f); } },
        { "the voicing",        [&] { live.controller.setVoicing (MasterVoicing::Earbuds); } },
        { "the delivery",       [&] { live.controller.setDelivery (DeliveryLoudness::Podcast); } },
        { "the purpose",        [&] { live.controller.setPurpose (MixPurpose::LiveRecording); } },
        { "the sound",          [&] { live.controller.setProfile (StyleProfileId::ModernGospel); } },
        { "a channel name",     [&] { live.controller.setInputName (0, "Kick In"); } },
        { "a channel icon",     [&] { live.controller.setInputIcon (0, "drum"); } },
        { "the focal source",   [&] { live.controller.setFocusInput (4); } },
        { "speech priority",    [&] { live.controller.setSpeechPriority (false); } },
        { "the monitor level",  [&] { live.controller.setMonitorGain (-3.0f); } },
        { "solo",               [&] { live.controller.setStripSolo (1, true); } },
        { "a link",             [&] { live.controller.linkStrips ({ 0, 1 }); } },
        { "a scene",            [&] { live.controller.keepScene (2); } },
        { "a scene's name",     [&] { live.controller.renameScene (2, "Choir"); } },
        { "the reference",      [&] { live.controller.clearReference(); } },
        { "LIVE SAFE",          [&] { live.controller.setLiveSafe (false); } },
        { "an Inspector edit",  [&] { auto c = live.controller.getKept().strips[0].channel;
                                      c.gateEnabled = ! c.gateEnabled;
                                      live.controller.setStripChannel (0, c); } },
        { "the routing",        [&] { auto f = live.controller.getOutputFeeds(); f.feeds[0].gainDb = -1.0f;
                                      live.controller.setOutputFeeds (f); } },
        { "arming a track",     [&] { auto p = live.daw.getProject(); p.tracks[1].armed = true; live.daw.setProject (p); } },
        { "the loop",           [&] { live.daw.setLoop (true, 0, 48000); } },
        { "the assignments",    [&] { auto s = live.controller.getSession();
                                      s.inputs.push_back ({ "Pastor", ChannelRole::Speech, 9, -1 });
                                      live.controller.setSession (s); live.daw.setSession (s); } },
    };
    for (const auto& c : cases)
    {
        const auto before = at();
        c.go();
        CHECK_MESSAGE (at() > before, std::string ("the revision did not move for ") + c.what);
    }

    // And reading it does not. A page that draws thirty times a second must not look like a
    // change, or DLIVE would write the session file thirty times a second for ever.
    const auto quiet = at();
    (void) live.controller.getKept();
    (void) live.controller.getRunning();
    (void) live.controller.getMacros();
    (void) live.controller.getGraph().numStrips();
    (void) live.controller.getMasterLoudness();
    (void) live.controller.getMixHealthPercent();
    (void) live.daw.getProject().lengthSamples();
    CHECK (at() == quiet);
}

TEST_CASE ("SessionState: a drum strip's sound is remembered by name, and a sound that has gone says so")
{
    SampleLibrary library;
    library.load();
    const auto kicks = library.sounds (RoleFamily::Kick);
    if (kicks.size() < 2) return;      // a machine with no sample folder at all: nothing to check

    MixSession s;
    s.name = "Sampled";
    s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Snare", ChannelRole::SnareTop, 1, -1 } };
    MixController c;
    DawEngine daw { c };
    c.setSession (s);
    daw.setSession (s);
    c.setSampleBanks (library.table());
    c.prepare (kSr, kBlock);

    auto chain = c.getKept().strips[0].channel;
    chain.replaceEnabled = true;
    chain.replaceSound = 1;
    c.setStripChannel (0, chain);

    auto state = captureSession (c, daw, kDevices, 0);
    readSampleChoices (c, library, state.samples);
    CHECK (state.samples[0].family == "kick");
    CHECK (state.samples[0].name == kicks[1].name);
    // The snare's stage is off, and its sound is still named: the slot it is pointing at can
    // move just as easily, and turning the stage on next Sunday should give the same drum.
    const auto snares = library.sounds (RoleFamily::Snare);
    REQUIRE (! snares.empty());
    CHECK (state.samples[1].family == "snare");
    CHECK (state.samples[1].name == snares[0].name);

    // The whole point: the index moves, the name does not. Pretend the library came back with
    // that sound in a different slot - a new built-in shipped, a file renamed - and the session
    // still plays the sound it was given.
    MixController reopened;
    DawEngine reopenedDaw { reopened };
    applySession (state, reopened, reopenedDaw);
    reopened.setSampleBanks (library.table());
    reopened.prepare (kSr, kBlock);
    auto moved = reopened.getKept().strips[0].channel;
    moved.replaceSound = 0;                    // as a stale index would have left it
    reopened.setStripChannel (0, moved);
    const auto missing = resolveSampleChoices (state.samples, library, reopened);
    CHECK (missing.empty());
    CHECK (reopened.getKept().strips[0].channel.replaceSound == 1);
    CHECK (reopened.getKept().strips[0].channel.replaceEnabled);

    // And a sound that is not there any more is said out loud, with the stage switched off,
    // rather than silently playing whatever has moved into that slot.
    auto gone = state;
    gone.samples[0].name = "A Kick That Was Deleted";
    gone.samples[0].user = true;
    gone.samples[0].path = "Kick/A Kick That Was Deleted.wav";
    const auto notes = resolveSampleChoices (gone.samples, library, reopened);
    REQUIRE (notes.size() == 1);
    CHECK (juce::String (notes[0]).contains ("A Kick That Was Deleted"));
    CHECK (juce::String (notes[0]).contains ("Kick"));
    CHECK (! reopened.getKept().strips[0].channel.replaceEnabled);
}

// ---------------------------------------------------------------------------
// AUTOSAVE AND RECOVERY
// ---------------------------------------------------------------------------

namespace
{
    juce::File autosaveScratch()
    {
        auto f = scratchFolder().getChildFile ("autosave");
        f.deleteRecursively();
        f.createDirectory();
        return f;
    }

    bool waitForIdle (SessionAutosave& a, int ms = 4000)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (! a.isIdle() && juce::Time::getMillisecondCounter() < deadline) juce::Thread::sleep (5);
        return a.isIdle();
    }
}

TEST_CASE ("Autosave: the session is written beside the document, whole, from a worker thread")
{
    const auto folder = autosaveScratch();
    const auto document = folder.getChildFile ("Sunday.dlive.json");

    FullSession live (true);
    const auto state = captureSession (live.controller, live.daw, kDevices, 260);

    SessionAutosave autosave;
    autosave.open (document);
    // Open means open: the marker is what tells the next launch DLIVE was killed rather than closed.
    CHECK (SessionAutosave::markerFor (document).existsAsFile());
    CHECK (SessionAutosave::autosaveFor (document).getFileName() == "Sunday.dlive.autosave.json");

    autosave.note (state, true);
    REQUIRE (waitForIdle (autosave));
    const auto sidecar = SessionAutosave::autosaveFor (document);
    REQUIRE (sidecar.existsAsFile());

    SessionState back;
    REQUIRE (SessionStore::load (sidecar, back));
    checkSameSession (state, back);           // an autosave is a whole session, not a diff

    // A clean goodbye takes both with it, so there is nothing to offer next time.
    autosave.closeCleanly();
    CHECK (! sidecar.existsAsFile());
    CHECK (! SessionAutosave::markerFor (document).existsAsFile());
    folder.deleteRecursively();
}

TEST_CASE ("Autosave: a knob drag is one write, and a milestone does not wait")
{
    const auto folder = autosaveScratch();
    const auto document = folder.getChildFile ("Quiet.dlive.json");
    FullSession live (true);
    live.daw.setLiveSafe (false);      // this is about the writing, not about the lock's step limits

    SessionAutosave autosave;
    autosave.open (document);
    const auto sidecar = SessionAutosave::autosaveFor (document);

    // Thirty changes in a row, none of them urgent: nothing has landed yet, because the write
    // waits for the session to stop moving.
    for (int i = 0; i < 30; ++i)
    {
        live.controller.setStripFader (0, -float (i) * 0.1f);
        autosave.note (captureSession (live.controller, live.daw, kDevices, 0), false);
    }
    CHECK (! sidecar.existsAsFile());

    // A milestone is a moment a service does not get a second chance at, so it goes now.
    live.controller.setStripFader (0, -7.25f);
    autosave.note (captureSession (live.controller, live.daw, kDevices, 0), true);
    REQUIRE (waitForIdle (autosave));
    REQUIRE (sidecar.existsAsFile());

    SessionState back;
    REQUIRE (SessionStore::load (sidecar, back));
    CHECK_NEAR (back.mix.strips[0].faderDb, -7.25f, 1e-3f);   // the last value, not one from the drag
    autosave.closeCleanly();
    folder.deleteRecursively();
}

TEST_CASE ("Autosave: a crash is offered back, a clean quit is not, and neither is a crash that lost nothing")
{
    const auto folder = autosaveScratch();
    const auto document = folder.getChildFile ("Service.dlive.json");
    FullSession live (true);
    live.daw.setLiveSafe (false);

    // A session saved, then mixed on, then killed.
    auto saved = captureSession (live.controller, live.daw, kDevices, 260);
    REQUIRE (SessionStore::save (saved, document));
    juce::Thread::sleep (1100);                     // file times are seconds on some volumes

    {
        SessionAutosave autosave;
        autosave.open (document);
        live.controller.setStripFader (2, -12.5f);
        live.controller.setBusMute (MixBus::Ambience, true);
        autosave.note (captureSession (live.controller, live.daw, kDevices, 260), true);
        REQUIRE (waitForIdle (autosave));
        // ...and DLIVE never gets to closeCleanly(). The marker stays.
    }

    const auto found = SessionAutosave::check (document);
    REQUIRE (found.offer);
    CHECK (found.sentence.startsWith ("DLIVE found work from "));
    CHECK (found.sentence.endsWith (" that was not saved."));
    CHECK (found.when > found.documentWhen);

    SessionState recovered;
    REQUIRE (SessionStore::load (found.autosave, recovered));
    CHECK_NEAR (recovered.mix.strips[2].faderDb, -12.5f, 1e-3f);
    CHECK (recovered.mix.buses[size_t (MixBus::Ambience)].mute);

    // The user chose. Stop offering it.
    SessionAutosave::discard (document);
    CHECK (! SessionAutosave::check (document).offer);

    // A clean quit leaves nothing behind at all.
    {
        SessionAutosave autosave;
        autosave.open (document);
        autosave.note (captureSession (live.controller, live.daw, kDevices, 260), true);
        REQUIRE (waitForIdle (autosave));
        autosave.closeCleanly();
    }
    CHECK (! SessionAutosave::check (document).offer);

    // And a crash *after* a save lost nothing, so it is not worth a question: an autosave
    // older than the document is a session that was saved after it was written.
    {
        SessionAutosave autosave;
        autosave.open (document);
        autosave.note (captureSession (live.controller, live.daw, kDevices, 260), true);
        REQUIRE (waitForIdle (autosave));
    }
    juce::Thread::sleep (1100);
    REQUIRE (SessionStore::save (captureSession (live.controller, live.daw, kDevices, 260), document));
    CHECK (SessionAutosave::markerFor (document).existsAsFile());     // it still crashed...
    CHECK (! SessionAutosave::check (document).offer);                // ...and still lost nothing
    folder.deleteRecursively();
}

TEST_CASE ("Autosave: a half-written file never replaces a good one")
{
    // The write lands by renaming a temporary file, so a reader either sees the session that
    // was there before or the whole of the new one - never the middle of a JSON document.
    const auto folder = autosaveScratch();
    const auto document = folder.getChildFile ("Atomic.dlive.json");
    FullSession live (true);

    SessionAutosave autosave;
    autosave.open (document);
    const auto sidecar = SessionAutosave::autosaveFor (document);

    for (int i = 0; i < 12; ++i)
    {
        live.controller.setStripFader (1, -float (i));
        autosave.note (captureSession (live.controller, live.daw, kDevices, 0), true);
        // Read it while the worker is very probably mid-write. Whatever is there parses.
        if (sidecar.existsAsFile())
        {
            SessionState back;
            CHECK_MESSAGE (SessionStore::load (sidecar, back),
                           std::string ("the autosave was unreadable on pass ") + std::to_string (i));
        }
    }
    REQUIRE (waitForIdle (autosave));
    SessionState back;
    REQUIRE (SessionStore::load (sidecar, back));
    CHECK_NEAR (back.mix.strips[1].faderDb, -11.0f, 1e-3f);
    autosave.closeCleanly();
    folder.deleteRecursively();
}

TEST_CASE ("Autosave: the moments a service cannot lose are milestones")
{
    FullSession live (true);
    auto at = [&] { return live.controller.getMilestone(); };

    const auto start = at();
    live.controller.setStripFader (0, -2.0f);
    CHECK (at() == start);                       // an ordinary edit waits its two seconds

    live.controller.keepScene (3);
    CHECK (at() > start);
    CHECK (juce::String (live.controller.getLastMilestone()).contains ("Scene kept"));

    const auto afterScene = at();
    CHECK (live.controller.recallScene (3));
    CHECK (at() > afterScene);

    const auto afterRecall = at();
    ReferenceProfile ref;
    ref.valid = true;
    ref.name = "Another record";
    ref.seconds = 200.0f;
    live.controller.setReference (ref);
    CHECK (at() > afterRecall);
    CHECK (juce::String (live.controller.getLastMilestone()).contains ("Another record"));

    const auto afterRef = at();
    auto s = live.controller.getSession();
    s.inputs.push_back ({ "Choir", ChannelRole::Choir, 12, -1 });
    live.controller.setSession (s);
    CHECK (at() > afterRef);                     // the assignments changed: the graph is new
}

// ---------------------------------------------------------------------------
// MIX HISTORY
// ---------------------------------------------------------------------------

TEST_CASE ("Mix history: every milestone is a place to come back to, and going back is one too")
{
    FullSession live (true);
    live.daw.setLiveSafe (false);
    const auto start = live.controller.getCheckpoints().size();

    live.controller.setStripFader (0, -9.0f);
    const auto faderDb = live.controller.getKept().strips[0].faderDb;
    live.controller.keepScene (0);                       // a milestone
    REQUIRE (live.controller.getCheckpoints().size() == start + 1);
    const int mark = int (live.controller.getCheckpoints().size()) - 1;
    CHECK (juce::String (live.controller.getCheckpoints()[size_t (mark)].what).contains ("Scene kept"));

    // Mix on, then go back.
    live.controller.setStripFader (0, 1.0f);
    live.controller.setBusMute (MixBus::Music, true);
    CHECK_NEAR (live.controller.getKept().strips[0].faderDb, 1.0f, 1e-3f);

    REQUIRE (live.controller.restoreCheckpoint (mark));
    CHECK_NEAR (live.controller.getKept().strips[0].faderDb, faderDb, 1e-3f);
    CHECK (! live.controller.getKept().buses[size_t (MixBus::Music)].mute);

    // Going back is itself a checkpoint, and UNDO takes it forward again.
    const auto& list = live.controller.getCheckpoints();
    REQUIRE (list.size() >= size_t (mark) + 2);
    CHECK (juce::String (list.back().what).startsWith ("Before going back to"));
    CHECK_NEAR (list.back().mix.strips[0].faderDb, 1.0f, 1e-3f);
    REQUIRE (live.controller.canUndoMix());
    live.controller.undoMix();
    CHECK_NEAR (live.controller.getKept().strips[0].faderDb, 1.0f, 1e-3f);

    // The engineer's own listen is never part of going back: solo and the monitor stay put.
    live.controller.setMonitorGain (-15.0f);
    live.controller.setStripSolo (1, true);
    REQUIRE (live.controller.restoreCheckpoint (mark));
    CHECK_NEAR (live.controller.getMonitor().gainDb, -15.0f, 1e-3f);
    CHECK (live.controller.getKept().strips[1].solo);
}

TEST_CASE ("Mix history: it survives the session, and it is refused onto a different console")
{
    FullSession live (true);
    live.daw.setLiveSafe (false);
    live.controller.setStripFader (3, -5.0f);
    live.controller.keepScene (0);
    live.controller.setStripFader (3, 2.0f);
    live.controller.keepScene (1);
    const auto before = live.controller.getCheckpoints();
    REQUIRE (before.size() >= 2);

    auto state = captureSession (live.controller, live.daw, kDevices, 0);
    const auto file = scratchFolder().getChildFile ("history.dlive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (state, file));
    SessionState back;
    REQUIRE (SessionStore::load (file, back));
    REQUIRE (back.checkpoints.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        CHECK (back.checkpoints[i].what == before[i].what);
        CHECK (back.checkpoints[i].whenMs == before[i].whenMs);
        CHECK (back.checkpoints[i].fromTune == before[i].fromTune);
        CHECK (back.checkpoints[i].inputs == before[i].inputs);
        CHECK (MixPlanner::countParameterChanges (back.checkpoints[i].mix, before[i].mix) == 0);
    }

    // Reopened, the list is there and a mix from it goes back on.
    MixController fresh;
    DawEngine freshDaw { fresh };
    applySession (back, fresh, freshDaw);
    fresh.prepare (kSr, kBlock);
    REQUIRE (fresh.getCheckpoints().size() == before.size());
    REQUIRE (fresh.restoreCheckpoint (0));

    // A checkpoint is a balance between the sources that were there. Onto a console that is
    // not that one, it is refused with a sentence rather than applied by index.
    std::string said;
    fresh.onMessage = [&said] (const std::string& m) { said = m; };
    auto other = fresh.getSession();
    other.inputs.push_back ({ "Choir", ChannelRole::Choir, 11, -1 });
    fresh.setSession (other);
    freshDaw.setSession (other);
    fresh.prepare (kSr, kBlock);
    CHECK (! fresh.restoreCheckpoint (0));
    CHECK (juce::String (said).contains ("different set of inputs"));
}

TEST_CASE ("Mix history: it is bounded by what it costs, and a tune outlives the hand edits round it")
{
    std::vector<MixCheckpoint> list;
    auto add = [&list] (const char* what, bool fromTune, int strips)
    {
        MixCheckpoint c;
        c.what = what;
        c.fromTune = fromTune;
        c.mix.numStrips = strips;
        list.push_back (c);
    };

    // Twenty-one inputs: the budget is reached at about thirty-eight checkpoints, and the one
    // made by a tune is passed over while there is still a hand edit to drop instead.
    add ("TUNE MIX", true, 21);
    for (int i = 0; i < 200; ++i) add ("While mixing", false, 21);
    pruneCheckpoints (list);
    CHECK (int (list.size()) * 21 <= kCheckpointStripBudget);
    CHECK (list.size() > 20);                                    // a real morning still fits
    CHECK (list.front().what == "TUNE MIX");                     // ...and the tune is still there
    CHECK (list.back().what == "While mixing");                  // newest kept

    // A sixty-four channel console carries fewer, because each one costs three times as much.
    std::vector<MixCheckpoint> big;
    list.swap (big);
    big.clear();
    for (int i = 0; i < 200; ++i) { MixCheckpoint c; c.mix.numStrips = 64; c.what = "While mixing"; big.push_back (c); }
    pruneCheckpoints (big);
    CHECK (int (big.size()) * 64 <= kCheckpointStripBudget);
    CHECK (big.size() >= 8);

    // Every entry a tune: the oldest still goes, because a bounded list is bounded.
    std::vector<MixCheckpoint> tunes;
    for (int i = 0; i < 200; ++i) { MixCheckpoint c; c.mix.numStrips = 21; c.fromTune = true; c.what = "TUNE MIX"; tunes.push_back (c); }
    pruneCheckpoints (tunes);
    CHECK (int (tunes.size()) * 21 <= kCheckpointStripBudget);
}

// ---------------------------------------------------------------------------
// DEVICES NEVER GATE THE SESSION
// ---------------------------------------------------------------------------

TEST_CASE ("Devices: every state has a sentence, and \"no audio devices\" is only said when there are none")
{
    // The whole point of naming the states: "No audio devices" used to be DLIVE's answer to
    // four different situations, and the one it was most often wrong about is the console
    // being plugged in with macOS refusing the microphone.
    DeviceState nothing;
    CHECK (nothing.stage == DeviceStage::Absent);
    CHECK (deviceSentence (nothing, false) == "No audio devices are connected.");
    CHECK (deviceSentence (nothing, true).contains ("No device is open"));
    CHECK (! deviceSentence (nothing, true).contains ("No audio devices"));

    DeviceState running;
    running.stage = DeviceStage::Open;
    running.input = "Dante Virtual Soundcard";
    running.inputChannels = 32;
    running.outputChannels = 4;
    CHECK (running.hearing());
    CHECK (running.playing());
    CHECK (deviceSentence (running, true).contains ("32 in, 4 out"));

    // Playing and mixing, not hearing: the session is not broken and must not look it.
    DeviceState refused;
    refused.stage = DeviceStage::InputRefused;
    refused.input = "Scarlett 18i20";
    refused.output = "MacBook Pro Speakers";
    refused.outputChannels = 2;
    refused.why = inputRefusedSentence (refused.input, refused.output, true, "some CoreAudio error");
    CHECK (! refused.hearing());
    CHECK (refused.why.contains ("can play and mix"));
    CHECK (refused.why.contains ("System Settings > Privacy & Security > Microphone"));
    CHECK (refused.why.contains ("MacBook Pro Speakers"));
    CHECK (deviceSentence (refused, true) == refused.why);

    // ...and when it is *not* the microphone, it does not send somebody to that screen.
    const auto other = inputRefusedSentence ("Scarlett 18i20", "MacBook Pro Speakers", false, "the device is in use");
    CHECK (other.contains ("can play and mix"));
    CHECK (other.contains ("Scarlett 18i20 would not open its inputs"));
    CHECK (other.contains ("the device is in use"));
    CHECK (! other.contains ("Privacy"));

    DeviceState gone;
    gone.stage = DeviceStage::Disconnected;
    gone.input = "Dante Virtual Soundcard";
    CHECK (deviceSentence (gone, true).contains ("was unplugged"));

    DeviceState playingBack;
    playingBack.stage = DeviceStage::OutputOpen;
    playingBack.output = "MacBook Pro Speakers";
    playingBack.outputChannels = 2;
    CHECK (playingBack.playing());
    CHECK (! playingBack.hearing());
    CHECK (deviceSentence (playingBack, true).contains ("play and mix"));

    // Every named state says something, and none of them is empty.
    for (int i = 0; i < int (DeviceStage::Count); ++i)
    {
        DeviceState d;
        d.stage = DeviceStage (i);
        d.input = "A console";
        d.output = "An output";
        CHECK_MESSAGE (deviceSentence (d, true).isNotEmpty(),
                       std::string ("no sentence for the state ") + deviceStageName (DeviceStage (i)));
        CHECK (juce::String (deviceStageName (DeviceStage (i))) != "?");
    }
}

TEST_CASE ("Devices: a console pulled out comes back by itself, and only that console does")
{
    // Hot-plug, decided by a pure function so the rule is the same whatever hardware the Mac
    // has and can be checked on a Mac with none. AudioHost calls this and nothing else decides.
    const juce::StringArray nothing;
    const juce::StringArray consoleIn { "Dante Virtual Soundcard" };
    const juce::StringArray consoleOut { "Dante Virtual Soundcard", "MacBook Pro Speakers" };

    // The state a service is in when somebody kicks the cable out.
    DeviceState gone;
    gone.stage = DeviceStage::Disconnected;
    gone.input = "Dante Virtual Soundcard";
    gone.output = "Dante Virtual Soundcard";

    // Still gone: nothing happens, and DLIVE says so rather than sitting silent.
    CHECK (! deviceReturned (gone, nothing, nothing).reopen);
    CHECK (deviceSentence (gone, true).contains ("opens it again by itself"));
    CHECK (deviceLostSentence (gone, false).contains ("Dante Virtual Soundcard"));
    CHECK (deviceLostSentence (gone, false).contains ("Nothing about the mix or the session has changed"));
    // Mid-take, the sentence is about the take, because that is the question being asked.
    CHECK (deviceLostSentence (gone, true).contains ("safe on disk"));

    // Half of it back is not back: opening the console without the output it was playing
    // through would be a different session from the one that was running.
    CHECK (! deviceReturned (gone, consoleIn, nothing).reopen);
    CHECK (! deviceReturned (gone, nothing, consoleOut).reopen);

    // Both halves back: open it again, and say which device and what came with it.
    const auto back = deviceReturned (gone, consoleIn, consoleOut);
    CHECK (back.reopen);
    CHECK (back.what == "Dante Virtual Soundcard");
    CHECK (deviceBackSentence (back.what, 32, 4).contains ("Dante Virtual Soundcard is back"));
    CHECK (deviceBackSentence (back.what, 32, 4).contains ("32 in, 4 out"));
    CHECK (deviceBackSentence (back.what, 32, 4).contains ("on the channel it was on"));

    // SOMEBODY ELSE'S DEVICE IS NOT AN INVITATION. A pair of headphones plugged in during the
    // sermon must not become the console.
    const juce::StringArray headphones { "External Headphones" };
    CHECK (! deviceReturned (gone, headphones, headphones).reopen);

    // An output-only session - a recorded service being mixed with no console in the room -
    // waits for its output and for nothing else.
    DeviceState playbackGone;
    playbackGone.stage = DeviceStage::Disconnected;
    playbackGone.output = "MacBook Pro Speakers";
    CHECK (! deviceReturned (playbackGone, nothing, nothing).reopen);
    CHECK (deviceReturned (playbackGone, nothing, { "MacBook Pro Speakers" }).reopen);
    CHECK (deviceReturned (playbackGone, nothing, { "MacBook Pro Speakers" }).what == "MacBook Pro Speakers");

    // NOTHING ELSE REOPENS ANYTHING. Every other state is either running, or somebody's own
    // decision to make - a device nobody chose, a device the user closed, inputs macOS refused.
    for (int i = 0; i < int (DeviceStage::Count); ++i)
    {
        if (DeviceStage (i) == DeviceStage::Disconnected) continue;
        DeviceState d;
        d.stage = DeviceStage (i);
        d.input = "Dante Virtual Soundcard";
        d.output = "Dante Virtual Soundcard";
        CHECK_MESSAGE (! deviceReturned (d, consoleIn, consoleOut).reopen,
                       std::string ("DLIVE would open a device by itself from the state ")
                           + deviceStageName (DeviceStage (i)));
    }
}

TEST_CASE ("Devices: a session opens, edits and saves with no device at all")
{
    // The end of the whole phase, in one test: nothing is plugged in, nothing is prepared, and
    // the session is still a session - it has its mix, it can be changed, and what is written
    // down is everything.
    FullSession offline (false);
    offline.daw.setLiveSafe (false);
    CHECK (! offline.controller.isPrepared());
    CHECK (offline.controller.getEngine().getNumStrips() == 0);       // no audio graph at all
    CHECK (offline.controller.getGraph().numStrips() == 4);           // ...and a whole console

    // Reading a strip the engine does not have is silent, not a crash: the pages draw a
    // channel DLIVE is not yet playing, and draw it quiet.
    for (int i = 0; i < offline.controller.getGraph().numStrips(); ++i)
        CHECK (offline.controller.getEngine().getStrip (i).getOutputMeter().getMaxPeakDb() < -100.0f);

    offline.controller.setStripFader (1, -6.5f);
    offline.controller.setStripMute (2, true);
    offline.controller.keepScene (2);
    CHECK_NEAR (offline.controller.getKept().strips[1].faderDb, -6.5f, 1e-3f);
    CHECK (offline.controller.getScene (2).kept);
    CHECK (! offline.controller.getCheckpoints().empty());

    const auto file = scratchFolder().getChildFile ("nodevice.dlive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (captureSession (offline.controller, offline.daw, kDevices, 0), file));
    SessionState back;
    REQUIRE (SessionStore::load (file, back));
    REQUIRE (back.hasMix);
    CHECK_NEAR (back.mix.strips[1].faderDb, -6.5f, 1e-3f);
    CHECK (back.mix.strips[2].mute);
    CHECK (back.scenes[2].kept);
    CHECK (! back.checkpoints.empty());
    file.deleteFile();
}

// ---------------------------------------------------------------- percussion and brass
TEST_CASE ("Percussion and brass: the stored enum only grew, the names are guessed, and each lands on the right bus")
{
    // CHANNELROLE IS STORED. Every session, preset and input map on disk holds these as
    // integers, so a value that moves silently re-points somebody's console at a different
    // instrument. These are the anchors: the first, the two that were already pinned by hand,
    // and the last one that existed before percussion and brass were added.
    CHECK (int (ChannelRole::KickIn) == 0);
    CHECK (int (ChannelRole::CrowdMic) == 35);
    CHECK (int (ChannelRole::AmbienceMic) == 36);
    CHECK (int (ChannelRole::SaxBari) == 40);
    // ... and the new ones start after it, which is the only place they may start.
    CHECK (int (ChannelRole::Congas) == 41);
    CHECK (int (ChannelRole::BrassSection) == 48);
    // ... and the four speaking microphones after them, which is where 2026-09-29 appended.
    CHECK (int (ChannelRole::SpeechLapel) == 49);
    CHECK (int (ChannelRole::SpeechLectern) == int (ChannelRole::Count) - 1);
    // `Speech` did not move: a session saved before the split opens on exactly the role it
    // was saved with, and that role still means "somebody talking".
    CHECK (int (ChannelRole::Speech) == 15);

    // Every role has a name, and the table is the same length as the enum.
    for (int r = 0; r < int (ChannelRole::Count); ++r)
        CHECK_MESSAGE (juce::String (channelRoleName (ChannelRole (r))).isNotEmpty(),
                       "role " + std::to_string (r) + " has no name");

    auto guess = [] (const char* name)
    {
        ChannelRole r = ChannelRole::Count;
        return StemNames::guessRole (name, r) ? r : ChannelRole::Count;
    };

    // The names a desk actually writes on these channels.
    CHECK (guess ("Congas #12") == ChannelRole::Congas);
    CHECK (guess ("BONGO L") == ChannelRole::Bongos);
    CHECK (guess ("Djembe") == ChannelRole::Djembe);
    CHECK (guess ("Timbales") == ChannelRole::Timbales);
    CHECK (guess ("Shaker") == ChannelRole::Shaker);
    CHECK (guess ("Tambourine") == ChannelRole::Shaker);
    CHECK (guess ("Tamb 1") == ChannelRole::Shaker);
    CHECK (guess ("Trumpet 2") == ChannelRole::Trumpet);
    CHECK (guess ("TPT") == ChannelRole::Trumpet);
    CHECK (guess ("Trombone") == ChannelRole::Trombone);
    CHECK (guess ("Horns") == ChannelRole::BrassSection);

    // Percussion is played with the kit and has to be balanced against it, so it is on DRUMS.
    // A horn line is not, so it is on MUSIC with the rest of the band.
    for (auto r : { ChannelRole::Congas, ChannelRole::Bongos, ChannelRole::Djembe,
                    ChannelRole::Timbales, ChannelRole::Shaker })
        CHECK_MESSAGE (mixBusForRole (r) == MixBus::Drums,
                       juce::String (channelRoleName (r)).toStdString() + " should be on DRUMS");
    for (auto r : { ChannelRole::Trumpet, ChannelRole::Trombone, ChannelRole::BrassSection })
        CHECK_MESSAGE (mixBusForRole (r) == MixBus::Music,
                       juce::String (channelRoleName (r)).toStdString() + " should be on MUSIC");

    // A shaker never stops, so a gate on it chatters: the profile must say so, not the strategy.
    const auto& profile = Profiles::definition (StyleProfileId::ModernGospel);
    CHECK (! profile.targets[int (RoleFamily::Shaker)].gateAppropriate);
    CHECK (! profile.targets[int (RoleFamily::Shaker)].transientAppropriate);
    // Drum replacement is kick, snare and toms; a conga is none of them.
    CHECK (! profile.targets[int (RoleFamily::Percussion)].sampleAppropriate);
    // The ring is the instrument: a hand drum's gate may only ever take a little off.
    CHECK (profile.targets[int (RoleFamily::Percussion)].gateMaxRangeDb
               < profile.targets[int (RoleFamily::Tom)].gateMaxRangeDb);
}

// THE SPOKEN WORD, BY WHAT IT IS SPOKEN INTO. Four microphones a church actually patches,
// which are not the same instrument: a lapel is on the chest, a headset is at the mouth, a
// handheld moves, and a lectern gooseneck is a foot away with the room behind it.
TEST_CASE ("Speaking microphones: four kinds, one family, and a chain apiece")
{
    auto guess = [] (const char* name)
    {
        ChannelRole r = ChannelRole::Count;
        return StemNames::guessRole (name, r) ? r : ChannelRole::Count;
    };

    // The names a stage plot actually writes.
    CHECK (guess ("Pastor lapel") == ChannelRole::SpeechLapel);
    CHECK (guess ("Lav 2") == ChannelRole::SpeechLapel);
    CHECK (guess ("Headset 1") == ChannelRole::SpeechHeadset);
    CHECK (guess ("Countryman") == ChannelRole::SpeechHeadset);
    CHECK (guess ("Handheld") == ChannelRole::SpeechHandheld);
    CHECK (guess ("Lectern") == ChannelRole::SpeechLectern);
    CHECK (guess ("Pulpit mic") == ChannelRole::SpeechLectern);
    // ... and the word on its own still means what it always meant.
    CHECK (guess ("Pastor") == ChannelRole::Speech);
    CHECK (guess ("Sermon") == ChannelRole::Speech);

    // All five are the speech family, on the speech bus, and all five are voice channels a
    // volunteer can put on SPEAKING.
    for (auto r : { ChannelRole::Speech, ChannelRole::SpeechLapel, ChannelRole::SpeechHeadset,
                    ChannelRole::SpeechHandheld, ChannelRole::SpeechLectern })
    {
        CHECK_MESSAGE (roleFamily (r) == RoleFamily::Speech,
                       juce::String (channelRoleName (r)).toStdString() + " is not in the speech family");
        CHECK_MESSAGE (mixBusForRole (r) == MixBus::Speech,
                       juce::String (channelRoleName (r)).toStdString() + " should be on SPEECH");
        CHECK (juce::String (channelRoleName (r)).isNotEmpty());
    }

    // The chains differ where the microphones differ, and the numbers come from the profile.
    const auto lapel    = Profiles::baseline (StyleProfileId::ModernGospel, ChannelRole::SpeechLapel);
    const auto headset  = Profiles::baseline (StyleProfileId::ModernGospel, ChannelRole::SpeechHeadset);
    const auto handheld = Profiles::baseline (StyleProfileId::ModernGospel, ChannelRole::SpeechHandheld);
    const auto lectern  = Profiles::baseline (StyleProfileId::ModernGospel, ChannelRole::SpeechLectern);

    // A lectern is furthest from the mouth, a headset is closest: the high-pass follows.
    CHECK (lectern.hpfHz > lapel.hpfHz);
    CHECK (lapel.hpfHz > headset.hpfHz);
    // A lapel's box is its chest, an octave above a close microphone's proximity.
    CHECK (lapel.correctiveBands[0].enabled);
    CHECK (lapel.correctiveBands[0].gainDb < 0.0f);
    CHECK (lapel.correctiveBands[0].freqHz > handheld.correctiveBands[0].freqHz);
    // A handheld's distance changes every sentence, so it is the one that is compressed hardest.
    CHECK (handheld.compRatio > headset.compRatio);
    // The room is behind a lectern, so its expander reaches further and opens later than the
    // others' - and it is still a courtesy, never a gate: the family's own ceiling holds.
    CHECK (lectern.gateRangeDb > lapel.gateRangeDb);
    CHECK (lectern.gateThresholdDb > lapel.gateThresholdDb);
    CHECK (lectern.gateRangeDb <= Profiles::targets (StyleProfileId::ModernGospel, RoleFamily::Speech).gateMaxRangeDb);

    // WHAT THIS MICROPHONE IS DOING is a family, not a role: a lapel put on SPEAKING stays a
    // lapel, and one put on SINGING LEAD becomes the lead.
    MixController controller;
    MixSession session;
    InputAssignment a;
    a.role = ChannelRole::SpeechLapel;
    a.name = "Pastor lapel";
    a.inputA = 0;
    session.inputs.push_back (a);
    controller.setSession (session);
    CHECK (controller.isVoiceChannel (0));
    CHECK (controller.roleForJob (0, ChannelRole::Speech) == ChannelRole::SpeechLapel);
    CHECK (controller.roleForJob (0, ChannelRole::LeadVocal) == ChannelRole::LeadVocal);
}


TEST_CASE ("SessionStore: a session saved before LEAD existed opens with its master on the master")
{
    // A version 5 document: seven bus slots - DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE MASTER -
    // the last of which was the master. LEAD went in before MASTER on 2026-09-28, so the
    // master's stored index moved and nothing else's did.
    auto* mix = new juce::DynamicObject();
    mix->setProperty ("numStrips", 2);
    juce::Array<juce::var> strips;
    for (int i = 0; i < 2; ++i)
    {
        auto* strip = new juce::DynamicObject();
        strip->setProperty ("faderDb", -2.0 - double (i));
        strips.add (juce::var (strip));
    }
    mix->setProperty ("strips", strips);
    juce::Array<juce::var> buses;
    for (int b = 0; b < 7; ++b)
    {
        auto* bo = new juce::DynamicObject();
        bo->setProperty ("faderDb", double (b));      // 6 = the old master
        buses.add (juce::var (bo));
    }
    mix->setProperty ("buses", buses);

    auto* doc = new juce::DynamicObject();
    doc->setProperty ("app", "DLIVE");
    doc->setProperty ("version", 5);
    doc->setProperty ("name", "Last Sunday");
    doc->setProperty ("purpose", int (MixPurpose::ChurchBroadcast));
    juce::Array<juce::var> inputs;
    const struct { const char* name; ChannelRole role; int in; } band[2] = {
        { "Lead", ChannelRole::LeadVocal, 0 }, { "BGV 1", ChannelRole::BackingVocal, 1 }
    };
    for (const auto& b : band)
    {
        auto* in = new juce::DynamicObject();
        in->setProperty ("name", b.name);
        in->setProperty ("role", int (b.role));
        in->setProperty ("inputA", b.in);
        in->setProperty ("inputB", -1);
        in->setProperty ("enabled", true);
        inputs.add (juce::var (in));
    }
    doc->setProperty ("inputs", inputs);
    doc->setProperty ("hasMix", true);
    doc->setProperty ("mix", juce::var (mix));
    juce::Array<juce::var> feeds;
    auto* feed = new juce::DynamicObject();
    feed->setProperty ("left", 0);
    feed->setProperty ("right", 1);
    feed->setProperty ("source", 6);          // the old master index
    feeds.add (juce::var (feed));
    doc->setProperty ("outputs", feeds);

    const auto file = scratchFolder().getChildFile ("v5.dlive.json");
    file.replaceWithText (juce::JSON::toString (juce::var (doc), false));

    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));
    // Every group kept its own fader...
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Drums)].faderDb, 0.0f, 0.01);
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Vocals)].faderDb, 3.0f, 0.01);
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Ambience)].faderDb, 5.0f, 0.01);
    // ...the old master's fader is on the master, not on the new LEAD group...
    CHECK_NEAR (back.mix.master().faderDb, 6.0f, 0.01);
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Lead)].faderDb, 0.0f, 0.01);
    // ...and the output feed still carries the main mix rather than the lead.
    CHECK (back.outputs.feeds[0].source == MixBus::Master);

    // THE ROUTING IS BUILT, NOT STORED, so the lead is on LEAD the moment the session opens -
    // there is no migration to do for it and nothing to guess.
    const auto graph = RoutingGraph::build (back.session);
    REQUIRE (graph.numStrips() == 2);
    CHECK (graph.strips[0].bus == MixBus::Lead);
    CHECK (graph.strips[1].bus == MixBus::Vocals);
    file.deleteFile();
}

TEST_CASE ("SessionStore: LEAD and BGV survive the round trip as two groups")
{
    FullSession s (false);
    s.daw.setLiveSafe (false);          // a bus fader is only a trim under LIVE SAFE
    s.controller.setBusFader (MixBus::Lead, -1.5f);
    s.controller.setBusFader (MixBus::Vocals, -4.5f);
    s.controller.setBusMute (MixBus::Vocals, true);

    const auto file = scratchFolder().getChildFile ("lead.dlive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (captureSession (s.controller, s.daw, kDevices, 0), file));
    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Lead)].faderDb, -1.5f, 0.01);
    CHECK_NEAR (back.mix.buses[size_t (MixBus::Vocals)].faderDb, -4.5f, 0.01);
    CHECK (back.mix.buses[size_t (MixBus::Vocals)].mute);
    CHECK (! back.mix.buses[size_t (MixBus::Lead)].mute);
    // The file says version 6, which is what says the stored bus layout has eight slots.
    CHECK (file.loadFileAsString().contains ("\"version\": " + juce::String (SessionStore::kVersion))
             || file.loadFileAsString().contains ("\"version\":" + juce::String (SessionStore::kVersion)));
    file.deleteFile();
}

TEST_CASE ("SessionStore: a favourite and what it sounded like survive the round trip")
{
    FullSession s (false);
    s.daw.setLiveSafe (false);
    s.controller.keepScene (2);                            // one of the four, so both kinds are in the file
    REQUIRE (s.controller.markFavourite ("The good one"));
    // A fingerprint with something in it, whether or not this fixture has listened.
    CHECK (s.controller.numFavourites() == 1);

    const auto file = scratchFolder().getChildFile ("favourites.dlive.json");
    file.deleteFile();
    REQUIRE (SessionStore::save (captureSession (s.controller, s.daw, kDevices, 0), file));
    SessionStore::Document back;
    REQUIRE (SessionStore::load (file, back));

    REQUIRE (back.scenes.size() >= size_t (kMixScenes) + 1);
    CHECK (back.scenes[2].kept);
    CHECK (! back.scenes[2].favourite);
    const auto& fav = back.scenes[size_t (kMixScenes)];
    CHECK (fav.favourite);
    CHECK (fav.name == std::string ("The good one"));
    CHECK (fav.kept);

    // ...and it comes back as a favourite rather than as a fifth service slot.
    FullSession another (false);
    auto& other = another.controller;
    other.restoreScenes (back.scenes);
    CHECK (other.numFavourites() == 1);
    CHECK (other.getFavourite (0).name == std::string ("The good one"));
    for (int i = 0; i < kMixScenes; ++i) CHECK (! other.getScene (i).favourite);
    file.deleteFile();
}
