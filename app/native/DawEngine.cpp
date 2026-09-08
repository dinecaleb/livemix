#include "DawEngine.h"
#include <algorithm>

namespace livemix
{

DawEngine::DawEngine (MixController& c) : controller (c)
{
    matrix.assign (size_t (kMaxInputs), nullptr);
}

DawEngine::~DawEngine()
{
    release();
}

void DawEngine::setProject (const Project& p)
{
    project = p;
    project.syncTracks (session);
    clipsDirty = true;
    if (prepared) rebuildPlayer();
    refresh();
}

void DawEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    blockSize = juce::jmax (32, maxBlockSize);
    project.sampleRate = sampleRate;
    transport.prepare (sampleRate);
    silence.assign (size_t (blockSize) * 2, 0.0f);
    prepared = true;
    clipsDirty = true;
    rebuildPlayer();
    refresh();
}

void DawEngine::release()
{
    if (recorder.isRecording()) stopRecording();
    transport.stop();
    player.release();
    prepared = false;
}

void DawEngine::rebuildPlayer()
{
    const bool wasPlaying = transport.isPlaying();
    const juce::int64 at = transport.getPosition();

    // Take the player away from the audio thread before its buffers are freed.
    rebuilding.store (true, std::memory_order_seq_cst);
    for (int spins = 0; inBlock.load (std::memory_order_seq_cst) && spins < 2000; ++spins)
        juce::Thread::sleep (1);

    std::vector<TimelinePlayer::TrackClips> forPlayer;
    forPlayer.resize (project.tracks.size());
    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        forPlayer[i].channels = (i < session.inputs.size() && session.inputs[i].isStereo()) ? 2 : 1;
        for (const auto& clip : project.tracks[i].clips)
        {
            auto resolved = clip;
            const auto file = project.fileFor (clip);
            if (! file.existsAsFile()) continue;         // a missing take is silence, never a crash
            resolved.file = file.getFullPathName();
            forPlayer[i].clips.push_back (resolved);
        }
    }
    player.prepare (sampleRate, blockSize * 2, forPlayer);
    player.setLoop (project.loopEnabled, project.loopStart, project.loopEnd);
    clipsDirty = false;

    if (wasPlaying) player.prime (at);
    rebuilding.store (false, std::memory_order_seq_cst);
}

void DawEngine::refresh()
{
    if (clipsDirty && prepared) rebuildPlayer();

    auto& table = routeMailbox.beginWrite();
    table = RouteTable {};
    const int n = juce::jmin (int (session.inputs.size()), int (project.tracks.size()), kMaxStrips);
    table.count = n;
    int highest = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto& in = session.inputs[size_t (i)];
        auto& r = table.tracks[size_t (i)];
        r.inputA = in.inputA;
        r.inputB = in.inputB;
        r.channels = in.numChannels();
        r.armed = project.tracks[size_t (i)].armed;
        r.hasClips = ! project.tracks[size_t (i)].clips.empty();
        r.monitor = project.tracks[size_t (i)].monitor;
        highest = juce::jmax (highest, in.inputA + 1, in.inputB + 1);
    }
    table.matrixChannels = juce::jlimit (0, kMaxInputs, highest);
    routeMailbox.publish();

    transport.setEnd (project.lengthSamples());
    transport.setLoop (project.loopEnabled, project.loopStart, project.loopEnd);
    player.setLoop (project.loopEnabled, project.loopStart, project.loopEnd);
}

void DawEngine::play()
{
    if (transport.isPlaying()) return;
    player.prime (transport.getPosition());
    transport.play();
}

void DawEngine::stop()
{
    if (recorder.isRecording()) stopRecording();
    transport.stop();
}

void DawEngine::locate (juce::int64 sample)
{
    const bool wasPlaying = transport.isPlaying() && ! recorder.isRecording();
    if (wasPlaying) transport.stop();
    transport.setPosition (sample);
    if (prepared) player.prime (sample);
    if (wasPlaying) transport.play();
}

void DawEngine::setLoop (bool on, juce::int64 start, juce::int64 end)
{
    project.loopEnabled = on;
    project.loopStart = start;
    project.loopEnd = end;
    refresh();
}

juce::String DawEngine::startRecording()
{
    if (recorder.isRecording()) return {};
    if (project.folder == juce::File()) return "Save this session before recording, so DINELIVE knows where the audio goes.";

    std::vector<Recorder::Spec> specs;
    const int n = juce::jmin (int (session.inputs.size()), int (project.tracks.size()));
    for (int i = 0; i < n; ++i)
    {
        if (! project.tracks[size_t (i)].armed) continue;
        const auto& in = session.inputs[size_t (i)];
        Recorder::Spec s;
        s.trackIndex = i;
        s.name = juce::String (in.name);
        s.inputA = in.inputA;
        s.inputB = in.inputB;
        specs.push_back (s);
    }
    if (specs.empty()) return "Arm the tracks you want to record first.";

    const auto err = recorder.start (project.audioFolder(), specs, sampleRate, transport.getPosition());
    if (err.isNotEmpty()) return err;

    if (! transport.isPlaying()) { player.prime (transport.getPosition()); transport.play(); }
    transport.setRecording (true);
    return {};
}

int DawEngine::stopRecording()
{
    if (! recorder.isRecording()) return 0;
    const juce::int64 start = recorder.getTimelineStart();
    transport.setRecording (false);
    const auto takes = recorder.stop();

    for (const auto& take : takes)
    {
        if (take.trackIndex < 0 || take.trackIndex >= int (project.tracks.size())) continue;
        AudioClip clip;
        clip.name = take.name;
        clip.file = take.fileName;
        clip.start = start;
        clip.offset = 0;
        clip.length = take.length;
        clip.fileSampleRate = sampleRate;
        auto& clips = project.tracks[size_t (take.trackIndex)].clips;
        clips.push_back (clip);
        std::sort (clips.begin(), clips.end(), [] (const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
    }
    clipsDirty = true;
    refresh();
    return int (takes.size());
}

void DawEngine::processBlock (const float* const* deviceInputs, int numInputChannels,
                              float* const* outputs, int numOutputs, int numSamples) noexcept
{
    inBlock.store (true, std::memory_order_seq_cst);
    if (routeMailbox.hasNew()) routes = routeMailbox.acquire();

    // 1. The raw inputs are captured exactly as they arrived, before anything touches them.
    if (recorder.isRecording()) recorder.write (deviceInputs, numInputChannels, numSamples);

    const bool playing = transport.isPlaying() && ! rebuilding.load (std::memory_order_seq_cst);
    const bool recording = recorder.isRecording();
    if (playing) player.read (numSamples);

    // 2. Build the input matrix: every device channel, with playback swapped in per track.
    const int channels = juce::jlimit (0, kMaxInputs, juce::jmax (numInputChannels, routes.matrixChannels));
    const float* quiet = silence.empty() ? nullptr : silence.data();
    for (int c = 0; c < channels; ++c)
        matrix[size_t (c)] = (c < numInputChannels && deviceInputs != nullptr && deviceInputs[c] != nullptr)
                                 ? deviceInputs[c] : quiet;

    // A track that is not on its live input hears its own recording, or nothing at all.
    for (int t = 0; t < routes.count; ++t)
    {
        const auto& r = routes.tracks[size_t (t)];
        if (monitorUsesLiveInput (r.monitor, r.armed, r.hasClips, playing, recording)) continue;

        const bool fromTape = playing && r.hasClips;
        const float* a = fromTape ? player.channel (t, 0) : nullptr;
        const float* b = fromTape ? player.channel (t, r.channels > 1 ? 1 : 0) : nullptr;
        if (a == nullptr) a = quiet;
        if (b == nullptr) b = quiet;
        if (r.inputA >= 0 && r.inputA < channels) matrix[size_t (r.inputA)] = a;
        if (r.inputB >= 0 && r.inputB < channels) matrix[size_t (r.inputB)] = b;
    }

    controller.process (matrix.data(), channels, outputs, numOutputs, numSamples);

    if (playing) transport.advance (numSamples);
    inBlock.store (false, std::memory_order_seq_cst);
}

} // namespace livemix
