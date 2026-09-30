#include "Recorder.h"
#include <cstring>

namespace livemix
{

namespace
{
    constexpr int kBitDepth = 24;
    // ~11 s at 48 kHz per track, preallocated. A disk that stalls for a couple of seconds - Spotlight,
    // Time Machine, a USB hub - is ordinary; 2.7 s (the old size) was not enough to ride it out.
    constexpr int kFifoSamples = 1 << 19;
    constexpr int kCatchUpBlocks = 8;             // silence chunks one callback may write to realign a track
    constexpr int kMaxBlock = 8192;
    constexpr double kMinMinutes = 10.0;          // less room than this and a take will stop in the middle
}

Recorder::Recorder (double sidecarSeconds, double flushSeconds, int fifo)
    : fifoSamples (fifo > 0 ? fifo : kFifoSamples), sidecarMs (juce::jmax (1, int (sidecarSeconds * 1000.0))), headerFlushSeconds (juce::jmax (0.001, flushSeconds))
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
    diskFault.store (0, std::memory_order_relaxed);
    droppedSamples.store (0, std::memory_order_relaxed);

    // A service is an hour; a disk that cannot hold kMinMinutes of it will fail in the middle
    // of it. Better to refuse now, in words, than to stop recording during the sermon.
    const double perSecond = bytesPerSecondFor (specs, rate);
    const double secondsFree = secondsFreeOn (audioFolder, perSecond);
    if (secondsFree > 0.0 && secondsFree < kMinMinutes * 60.0)
        return "There is only " + juce::String (secondsFree / 60.0, 1) + " minutes of room left for "
             + juce::String (int (specs.size())) + " tracks on this disk. Free some space, "
               "or save this session somewhere with more room, and record again.";

    juce::WavAudioFormat wav;
    // BROADCAST WAV: every take says when it began - the date, the time, and the time of day as
    // a sample count - so a service recorded here lines up with the video recorded beside it.
    const auto began = juce::Time::getCurrentTime();
    const auto sinceMidnight = juce::int64 ((began.getHours() * 3600 + began.getMinutes() * 60 + began.getSeconds()) * rate
                                            + began.getMilliseconds() * 0.001 * rate);
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
        const auto bext = juce::WavAudioFormat::createBWAVMetadata (spec.name, "DLIVE", {}, began, sinceMidnight, {});
        if (auto* writer = wav.createWriterFor (stream.get(), rate, (unsigned int) w.channels, kBitDepth, bext, 0))
        {
            stream.release();
            w.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, thread, fifoSamples);
            // The header is rewritten from the writer thread every few seconds of audio, so a
            // crash leaves a file that is short by at most that much, not one that is unreadable.
            w.writer->setFlushInterval (juce::jmax (1, int (rate * headerFlushSeconds)));
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
    sidecars.clear();
    for (const auto& w : writers)
        sidecars.push_back ({ sidecarFor (w.file), w.trackIndex, w.channels, w.name });
    writeSidecars();                                 // "in progress" from the first block, not the first interval
    thread.addTimeSliceClient (&sidecarWriter, sidecarMs);
    if (! thread.isThreadRunning()) thread.startThread (juce::Thread::Priority::high);
    active.store (true, std::memory_order_release);
    return {};
}

int Recorder::SidecarWriter::useTimeSlice()
{
    owner.writeSidecars();
    owner.checkDisk();
    return owner.sidecarMs;
}

void Recorder::checkDisk()
{
    if (! active.load (std::memory_order_acquire) || writers.empty()) return;
    const juce::int64 written = frames.load (std::memory_order_relaxed);

    // Nearly full: stop while the headers can still be written, and say why, rather than
    // carrying on with a red light over a take that is no longer landing.
    const double perSecond = double (kBitDepth / 8) * rate * [this] { int c = 0; for (const auto& w : writers) c += w.channels; return c; }();
    const juce::int64 free = writers.front().file.getBytesFreeOnVolume();
    if (free > 0 && double (free) < perSecond * 30.0)
    {
        diskFault.store (1, std::memory_order_relaxed);
        failed.store (true, std::memory_order_relaxed);
        return;
    }

    // Falling behind for good: more than a minute of the take has had to become silence. A
    // stall is ridden out; a disk that cannot keep up at all is a take to stop and say so.
    if (double (droppedSamples.load (std::memory_order_relaxed)) > rate * 60.0 * double (writers.size()))
    {
        failed.store (true, std::memory_order_relaxed);
        return;
    }

    // Still landing: every file grows while audio arrives. The header is flushed every
    // headerFlushSeconds of audio at the latest, so a file that has not grown for well over
    // that while the take went on is not being written - the drive went, or it is full.
    const juce::int64 patience = juce::int64 (rate * (headerFlushSeconds + 10.0));
    for (auto& w : writers)
    {
        const juce::int64 size = w.file.existsAsFile() ? w.file.getSize() : -1;
        if (size > w.lastSize) { w.lastSize = size; w.framesAtGrowth = written; continue; }
        if (size < 0 || written - w.framesAtGrowth > patience)
        {
            diskFault.store (2, std::memory_order_relaxed);
            failed.store (true, std::memory_order_relaxed);
            return;
        }
    }
}

void Recorder::writeSidecars()
{
    // Writer thread (and once from start()). Only `frames` is shared with the audio thread,
    // and it is an atomic; the rest was fixed when the take began.
    const juce::int64 written = frames.load (std::memory_order_relaxed);
    for (const auto& s : sidecars)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("app", "DLIVE");
        o->setProperty ("schema", 1);
        o->setProperty ("track", s.trackIndex);
        o->setProperty ("name", s.name);
        o->setProperty ("sampleRate", rate);
        o->setProperty ("channels", s.channels);
        o->setProperty ("bitDepth", kBitDepth);
        o->setProperty ("timelineStart", startSample);
        o->setProperty ("framesWritten", written);
        o->setProperty ("updated", juce::Time::getCurrentTime().toISO8601 (true));
        s.file.replaceWithText (juce::JSON::toString (juce::var (o)));
    }
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
    // Let any callback that is already inside write() finish before the writers go. A callback
    // that is still inside after two seconds belongs to a device that has hung; freeing the
    // writers under it would be a crash on the audio thread, so in that one case they are
    // let go of without being freed (a few kilobytes, once, against a crash mid-service).
    for (int spins = 0; inCallback.load (std::memory_order_acquire) && spins < 2000; ++spins)
        juce::Thread::sleep (1);
    const bool stuck = inCallback.load (std::memory_order_acquire);

    // Blocks until a sidecar write in progress has finished, so nothing below races it.
    thread.removeTimeSliceClient (&sidecarWriter);

    const juce::int64 length = frames.load (std::memory_order_relaxed);
    for (auto& w : writers)
    {
        // What the disk still owed this track, as silence, so every file ends at the same sample.
        const float* quiet[2] = { silence.data(), silence.data() };
        for (int tries = 0; w.owedSilence > 0 && tries < 4000 && ! stuck; ++tries)
        {
            const int n = int (std::min<juce::int64> (w.owedSilence, std::min (kMaxBlock, fifoSamples / 4)));
            if (w.writer->write (quiet, n)) w.owedSilence -= n;
            else juce::Thread::sleep (1);
        }
        if (stuck) { (void) w.writer.release(); continue; }
        w.writer.reset();                 // flushes and closes the file: the header is final now
        sidecarFor (w.file).deleteFile(); // and the take is no longer "in progress"
        if (length > 0)
        {
            Take t;
            t.trackIndex = w.trackIndex;
            t.name = w.name;
            t.fileName = w.file.getFileName();
            t.length = length;
            t.inputA = w.inputA;
            t.inputB = w.inputB;
            takes.push_back (t);
        }
        else
        {
            w.file.deleteFile();          // nothing was captured: leave no empty files behind
        }
    }
    writers.clear();
    sidecars.clear();
    return takes;
}

juce::File Recorder::sidecarFor (const juce::File& take)
{
    return take.getSiblingFile (take.getFileName() + ".recording.json");
}

juce::int64 Recorder::repairWavHeader (const juce::File& wav, juce::String& error)
{
    int channels = 0, bits = 0;
    juce::int64 dataOffset = -1, fileSize = 0;
    {
        juce::FileInputStream in (wav);
        if (! in.openedOk()) { error = "could not be read"; return -1; }
        fileSize = in.getTotalLength();
        char id[4] = {};
        auto readId = [&] { return in.read (id, 4) == 4; };
        auto is = [&] (const char* s) { return std::memcmp (id, s, 4) == 0; };
        if (! readId()) { error = "is empty"; return -1; }
        if (is ("RF64")) { error = "is a 64-bit (RF64) file, which this build does not repair"; return -1; }
        if (! is ("RIFF")) { error = "is not a WAV file"; return -1; }
        in.readInt();                                            // the RIFF size, whatever it says
        if (! readId() || ! is ("WAVE")) { error = "is not a WAV file"; return -1; }
        while (in.getPosition() + 8 <= fileSize)
        {
            if (! readId()) break;
            const auto size = (juce::uint32) in.readInt();
            const auto body = in.getPosition();
            if (is ("fmt "))
            {
                in.readShort();                                  // format tag
                channels = in.readShort();
                in.readInt();                                    // sample rate
                in.readInt();                                    // byte rate
                in.readShort();                                  // block align
                bits = in.readShort();
            }
            else if (is ("data"))
            {
                dataOffset = body;
                break;
            }
            in.setPosition (body + size + (size & 1));
        }
    }
    if (dataOffset < 0 || channels <= 0 || bits <= 0) { error = "has no usable header"; return -1; }

    const int frameBytes = channels * bits / 8;
    juce::int64 dataBytes = juce::jmax ((juce::int64) 0, fileSize - dataOffset);
    dataBytes -= dataBytes % frameBytes;                         // a frame cut short by the crash is not audio
    if (dataOffset + dataBytes - 8 > 0xFFFFFFFFLL)
    {
        error = "is over 4 GB, which needs an RF64 header this build does not write";
        return -1;
    }

    juce::FileOutputStream out (wav);                            // opens without truncating; positioned by hand
    if (out.failedToOpen()) { error = "could not be written"; return -1; }
    if (! out.setPosition (4) || ! out.writeInt ((int) (juce::uint32) (dataOffset + dataBytes - 8))
     || ! out.setPosition (dataOffset - 4) || ! out.writeInt ((int) (juce::uint32) dataBytes))
    {
        error = "could not be written";
        return -1;
    }
    out.flush();
    return dataBytes / frameBytes;
}

std::vector<Recorder::Recovered> Recorder::recoverUnfinishedTakes (const juce::File& audioFolder)
{
    std::vector<Recovered> out;
    if (! audioFolder.isDirectory()) return out;

    for (const auto& sidecar : audioFolder.findChildFiles (juce::File::findFiles, false, "*.recording.json"))
    {
        Recovered r;
        const auto doc = juce::JSON::parse (sidecar.loadFileAsString());
        if (auto* o = doc.getDynamicObject())
        {
            r.trackIndex    = (int) o->getProperty ("track");
            r.name          = o->getProperty ("name").toString();
            r.timelineStart = (juce::int64) o->getProperty ("timelineStart");
            r.sampleRate    = (double) o->getProperty ("sampleRate");
            r.channels      = (int) o->getProperty ("channels");
        }
        const auto wav = sidecar.getSiblingFile (sidecar.getFileName().dropLastCharacters (int (juce::String (".recording.json").length())));
        r.fileName = wav.getFileName();

        if (! wav.existsAsFile())
        {
            sidecar.deleteFile();
            r.note = wav.getFileName() + " was still recording when DLIVE last closed, but the file is gone.";
            out.push_back (r);
            continue;
        }

        juce::String err;
        const auto frames = wav.getSize() == 0 ? (juce::int64) 0 : repairWavHeader (wav, err);
        if (frames < 0)
        {
            r.note = wav.getFileName() + " was still recording when DLIVE last closed and " + err + ".";
        }
        else if (frames == 0)
        {
            wav.deleteFile();
            sidecar.deleteFile();
            r.note = wav.getFileName() + " was still recording when DLIVE last closed and held no audio; it was removed.";
        }
        else
        {
            r.length = frames;
            r.repaired = true;
            sidecar.deleteFile();
            const double seconds = r.sampleRate > 0.0 ? double (frames) / r.sampleRate : 0.0;
            r.note = wav.getFileName() + " was still recording when DLIVE last closed; "
                   + juce::String (seconds, 1) + " s of it were recovered.";
        }
        out.push_back (r);
    }
    return out;
}

juce::String Recorder::getError() const
{
    if (oversized.load (std::memory_order_relaxed))
        return "The audio device is using a buffer this recorder cannot capture. "
               "Choose a buffer size of 8192 samples or less under Audio device, then record again.";
    const int fault = diskFault.load (std::memory_order_relaxed);
    if (fault == 1)
        return "The recording disk is full, so the take has been stopped with everything up to now kept. "
               "Free some space, or save the session somewhere with more room, and record again.";
    if (fault == 2)
        return "The take stopped reaching the disk - the drive may have been unplugged or be full. It has been stopped "
               "with what was written kept. Check the drive, then record again.";
    if (failed.load (std::memory_order_relaxed))
        return "The disk could not keep up with the recording. Stop, free some space, and record again.";
    return {};
}

const char* Recorder::getErrorCode() const noexcept
{
    if (oversized.load (std::memory_order_relaxed)) return "buffer_too_large";
    if (diskFault.load (std::memory_order_relaxed) == 1) return "disk_full";
    if (diskFault.load (std::memory_order_relaxed) == 2) return "disk_lost";
    if (failed.load (std::memory_order_relaxed)) return "disk_too_slow";
    return "";
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

void Recorder::write (const float* const* deviceInputs, int numInputChannels, int numSamples) noexcept LIVEMIX_NONBLOCKING
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
        // EVERY TRACK STAYS ON THE SAME CLOCK. A track whose FIFO is full cannot take this block;
        // the others can. Dropping it on that track alone left it short and every later sample
        // early against the rest - a multitrack that no longer lines up. So what one track could
        // not take is owed to it as silence, written ahead of its next audio once there is room:
        // the take has an honest gap, in the same place on every clock, and the gap is counted.
        const float* quiet[2] = { silence.data(), silence.data() };
        for (auto& w : writers)
        {
            for (int chunk = 0; w.owedSilence > 0 && chunk < kCatchUpBlocks; ++chunk)
            {
                const int n = int (std::min<juce::int64> (w.owedSilence, std::min (kMaxBlock, fifoSamples / 4)));
                if (! w.writer->write (quiet, n)) break;
                w.owedSilence -= n;
            }
            const int a = w.inputA, b = w.inputB;
            w.ptrs[0] = (a >= 0 && a < numInputChannels && deviceInputs[a] != nullptr) ? deviceInputs[a] : silence.data();
            if (w.channels == 2)
                w.ptrs[1] = (b >= 0 && b < numInputChannels && deviceInputs[b] != nullptr) ? deviceInputs[b] : silence.data();
            if (w.owedSilence > 0 || ! w.writer->write (w.ptrs.data(), numSamples))
            {
                w.owedSilence += numSamples;
                droppedSamples.fetch_add (numSamples, std::memory_order_relaxed);
            }
        }
        frames.fetch_add (numSamples, std::memory_order_relaxed);
    }
    inCallback.store (false, std::memory_order_seq_cst);
}

} // namespace livemix
