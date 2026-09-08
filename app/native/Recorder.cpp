#include "Recorder.h"

namespace livemix
{

namespace
{
    constexpr int kBitDepth = 24;
    constexpr int kFifoSamples = 1 << 17;     // ~2.7 s at 48 kHz per track, preallocated
    constexpr int kMaxBlock = 8192;
}

Recorder::Recorder()
{
    silence.assign (kMaxBlock, 0.0f);
}

Recorder::~Recorder()
{
    stop();
}

juce::File Recorder::uniqueTakeFile (const juce::File& folder, const juce::String& trackName)
{
    const auto stem = juce::File::createLegalFileName (trackName.isEmpty() ? "Track" : trackName);
    for (int take = 1; take < 10000; ++take)
    {
        auto candidate = folder.getChildFile (stem + "_" + juce::String (take).paddedLeft ('0', 3) + ".wav");
        if (! candidate.existsAsFile()) return candidate;
    }
    return folder.getChildFile (stem + "_" + juce::String (juce::Time::currentTimeMillis()) + ".wav");
}

juce::String Recorder::start (const juce::File& audioFolder,
                              const std::vector<Spec>& specs,
                              double sampleRate,
                              juce::int64 timelineStart)
{
    stop();
    if (specs.empty()) return "Arm at least one track before recording.";

    const auto result = audioFolder.createDirectory();
    if (result.failed()) return "Could not create " + audioFolder.getFullPathName() + ": " + result.getErrorMessage();

    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    startSample = timelineStart;
    frames.store (0, std::memory_order_relaxed);
    failed.store (false, std::memory_order_relaxed);

    juce::WavAudioFormat wav;
    std::vector<Writer> made;
    made.reserve (specs.size());

    for (const auto& spec : specs)
    {
        Writer w;
        w.trackIndex = spec.trackIndex;
        w.name = spec.name;
        w.inputA = spec.inputA;
        w.inputB = spec.inputB;
        w.channels = spec.inputB >= 0 ? 2 : 1;
        w.file = uniqueTakeFile (audioFolder, spec.name);

        std::unique_ptr<juce::FileOutputStream> stream (w.file.createOutputStream());
        if (stream == nullptr)
        {
            for (auto& done : made) done.writer.reset();
            for (auto& done : made) done.file.deleteFile();
            return "Could not write to " + audioFolder.getFullPathName() + ". Check the disk and its permissions.";
        }
        if (auto* writer = wav.createWriterFor (stream.get(), rate, (unsigned int) w.channels, kBitDepth, {}, 0))
        {
            stream.release();
            w.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, thread, kFifoSamples);
            made.push_back (std::move (w));
        }
        else
        {
            for (auto& done : made) done.writer.reset();
            for (auto& done : made) done.file.deleteFile();
            return "Could not start a WAV file for " + spec.name + ".";
        }
    }

    writers = std::move (made);
    if (! thread.isThreadRunning()) thread.startThread (juce::Thread::Priority::high);
    active.store (true, std::memory_order_release);
    return {};
}

std::vector<Recorder::Take> Recorder::stop()
{
    std::vector<Take> takes;
    if (! active.exchange (false, std::memory_order_acq_rel))
    {
        writers.clear();
        return takes;
    }
    // Let any callback that is already inside write() finish before the writers go.
    for (int spins = 0; inCallback.load (std::memory_order_acquire) && spins < 2000; ++spins)
        juce::Thread::sleep (1);

    const juce::int64 length = frames.load (std::memory_order_relaxed);
    for (auto& w : writers)
    {
        w.writer.reset();                 // flushes and closes the file
        if (length > 0)
        {
            Take t;
            t.trackIndex = w.trackIndex;
            t.name = w.name;
            t.fileName = w.file.getFileName();
            t.length = length;
            takes.push_back (t);
        }
        else
        {
            w.file.deleteFile();          // nothing was captured: leave no empty files behind
        }
    }
    writers.clear();
    return takes;
}

juce::String Recorder::getError() const
{
    if (failed.load (std::memory_order_relaxed))
        return "The disk could not keep up with the recording. Stop, free some space, and record again.";
    return {};
}

void Recorder::write (const float* const* deviceInputs, int numInputChannels, int numSamples) noexcept
{
    inCallback.store (true, std::memory_order_seq_cst);
    if (active.load (std::memory_order_seq_cst) && numSamples > 0 && numSamples <= kMaxBlock)
    {
        bool ok = true;
        for (auto& w : writers)
        {
            const int a = w.inputA, b = w.inputB;
            w.ptrs[0] = (a >= 0 && a < numInputChannels && deviceInputs[a] != nullptr) ? deviceInputs[a] : silence.data();
            if (w.channels == 2)
                w.ptrs[1] = (b >= 0 && b < numInputChannels && deviceInputs[b] != nullptr) ? deviceInputs[b] : silence.data();
            ok = w.writer->write (w.ptrs.data(), numSamples) && ok;
        }
        if (ok) frames.fetch_add (numSamples, std::memory_order_relaxed);
        else    failed.store (true, std::memory_order_relaxed);
    }
    inCallback.store (false, std::memory_order_seq_cst);
}

} // namespace livemix
