#include "Recorder.h"

namespace livemix
{

namespace
{
    constexpr int kBitDepth = 24;
    constexpr int kFifoSamples = 1 << 17;     // ~2.7 s at 48 kHz per track, preallocated
    constexpr int kMaxBlock = 8192;
    constexpr double kMinMinutes = 10.0;          // less room than this and a take will stop in the middle
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
    if (specs.empty()) return "No tracks are set to record.";

    const auto result = audioFolder.createDirectory();
    if (result.failed()) return "Could not create " + audioFolder.getFullPathName() + ": " + result.getErrorMessage();

    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    startSample = timelineStart;
    frames.store (0, std::memory_order_relaxed);
    failed.store (false, std::memory_order_relaxed);
    oversized.store (false, std::memory_order_relaxed);

    // A service is an hour; a disk that cannot hold kMinMinutes of it will fail in the middle
    // of it. Better to refuse now, in words, than to stop recording during the sermon.
    const double perSecond = bytesPerSecondFor (specs, rate);
    const double secondsFree = secondsFreeOn (audioFolder, perSecond);
    if (secondsFree > 0.0 && secondsFree < kMinMinutes * 60.0)
        return "There is only " + juce::String (secondsFree / 60.0, 1) + " minutes of room left for "
             + juce::String (int (specs.size())) + " tracks on this disk. Free some space, "
               "or save this session somewhere with more room, and record again.";

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
    // seq_cst on both sides: write() sets inCallback then reads active, stop() clears active
    // then reads inCallback. Anything weaker lets both loads miss and the writers are freed
    // under a callback that is still using them.
    if (! active.exchange (false, std::memory_order_seq_cst))
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
    if (oversized.load (std::memory_order_relaxed))
        return "The audio device is using a buffer this recorder cannot capture. "
               "Choose a buffer size of 8192 samples or less under Audio device, then record again.";
    if (failed.load (std::memory_order_relaxed))
        return "The disk could not keep up with the recording. Stop, free some space, and record again.";
    return {};
}

double Recorder::bytesPerSecond() const noexcept
{
    int channels = 0;
    for (const auto& w : writers) channels += w.channels;
    return double (channels) * rate * double (kBitDepth / 8);
}

double Recorder::bytesPerSecondFor (const std::vector<Spec>& specs, double sampleRate) noexcept
{
    int channels = 0;
    for (const auto& s : specs) channels += s.inputB >= 0 ? 2 : 1;
    return double (channels) * (sampleRate > 0.0 ? sampleRate : 48000.0) * double (kBitDepth / 8);
}

double Recorder::secondsFreeOn (const juce::File& folder, double bytesPerSec) noexcept
{
    if (bytesPerSec <= 0.0) return 0.0;
    const juce::int64 free = folder.getBytesFreeOnVolume();
    if (free <= 0) return 0.0;                    // unknown volume: do not invent a number
    return double (free) / bytesPerSec;
}

void Recorder::write (const float* const* deviceInputs, int numInputChannels, int numSamples) noexcept
{
    inCallback.store (true, std::memory_order_seq_cst);
    if (active.load (std::memory_order_seq_cst) && numSamples > kMaxBlock)
    {
        // Bigger than anything we prepared for. Dropping it silently would leave a take that
        // is quietly short of the performance, so it is reported like any other write failure.
        oversized.store (true, std::memory_order_relaxed);
        failed.store (true, std::memory_order_relaxed);
    }
    else if (active.load (std::memory_order_seq_cst) && numSamples > 0)
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
