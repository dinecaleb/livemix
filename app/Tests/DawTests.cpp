// DLIVE DAW-layer tests: the playhead, the multitrack recorder, timeline playback, the
// monitoring rule, clip editing and the offline bounce. No device and no UI — the audio
// callback is played by the test, exactly as AudioHost would.
#include "TestFramework.h"
#include "native/DawEngine.h"
#include "native/MixBounce.h"
#include "native/MultitrackImport.h"
#include "native/StemNames.h"
#include "native/SessionStore.h"
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
    CHECK (guess ("Pastor Handheld #02") == ChannelRole::Speech);
    CHECK (guess ("Pastor Lapel") == ChannelRole::Speech);
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
    d.inputDevice = "Dante Virtual Soundcard";
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
    juce::Array<Device> devices;
    devices.add ({ "Dante Virtual Soundcard", "uid-dante", 32, false, false });
    devices.add ({ "MacBook Pro Speakers",    "uid-builtin", 2, false, false });
    devices.add ({ "Scarlett 2i2 USB",        "uid-scarlett", 4, false, false });

    // The broadcast is whatever is already carrying the mix; the headphones are the interface,
    // not the laptop.
    const auto s = MonitorDevice::suggestFrom (devices, "Dante Virtual Soundcard");
    REQUIRE (s.valid);
    CHECK (s.broadcast.name == "Dante Virtual Soundcard");
    CHECK (s.headphones.name == "Scarlett 2i2 USB");
    CHECK (s.why.contains ("Scarlett"));

    // With no interface the built-in output is better than nothing, and is offered.
    juce::Array<Device> noInterface;
    noInterface.add ({ "Dante Virtual Soundcard", "uid-dante", 32, false, false });
    noInterface.add ({ "MacBook Pro Speakers", "uid-builtin", 2, false, false });
    const auto fallback = MonitorDevice::suggestFrom (noInterface, "Dante Virtual Soundcard");
    REQUIRE (fallback.valid);
    CHECK (fallback.headphones.name == "MacBook Pro Speakers");

    // One device and nothing else: said plainly, never guessed at.
    juce::Array<Device> alone;
    alone.add ({ "Dante Virtual Soundcard", "uid-dante", 32, false, false });
    const auto none = MonitorDevice::suggestFrom (alone, "Dante Virtual Soundcard");
    CHECK (! none.valid);
    CHECK (none.problem.contains ("only one output device"));

    // A combined device DLIVE built earlier is never itself a building block.
    juce::Array<Device> withOurs;
    withOurs.add ({ "DLIVE Monitoring", "com.dine.dlive.monitoring", 34, true, true });
    withOurs.add ({ "Dante Virtual Soundcard", "uid-dante", 32, false, false });
    withOurs.add ({ "Scarlett 2i2 USB", "uid-scarlett", 4, false, false });
    const auto again = MonitorDevice::suggestFrom (withOurs, "DLIVE Monitoring");
    REQUIRE (again.valid);
    CHECK (again.broadcast.name == "Dante Virtual Soundcard");
    CHECK (again.headphones.name == "Scarlett 2i2 USB");

    // A virtual device has no headphone socket: it is never suggested as the headphones when a real
    // interface is there, even when it is listed first - but it is a perfectly good broadcast.
    juce::Array<Device> withVirtual;
    withVirtual.add ({ "BlackHole 2ch", "uid-blackhole", 2, false, false, 2, true });
    withVirtual.add ({ "Dante Virtual Soundcard", "uid-dante", 32, false, false, 32, true });
    withVirtual.add ({ "Scarlett 2i2 USB", "uid-scarlett", 4, false, false, 2 });
    const auto real = MonitorDevice::suggestFrom (withVirtual, "Dante Virtual Soundcard");
    REQUIRE (real.valid);
    CHECK (real.broadcast.name == "Dante Virtual Soundcard");
    CHECK (real.headphones.name == "Scarlett 2i2 USB");
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
    const Device dvs { "Dante Virtual Soundcard", "uid-dvs", 16, false, false, 16, true };
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
