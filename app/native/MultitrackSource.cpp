#include "MultitrackSource.h"
#include "StemNames.h"

namespace livemix
{

namespace
{
    constexpr int kBufferedSamples = 1 << 16;   // ~1.4 s per track at 48 kHz, read ahead on the thread
}

MultitrackSource::MultitrackSource()
{
    formats.registerBasicFormats();
}

MultitrackSource::~MultitrackSource()
{
    unload();
}

juce::String MultitrackSource::load (const juce::File& newFolder)
{
    unload();
    if (! newFolder.isDirectory()) return "That is not a folder.";
    juce::Array<juce::File> files = newFolder.findChildFiles (juce::File::findFiles, false, "*.aif;*.aiff;*.wav;*.flac;*.aifc;*.caf");
    files.sort();
    if (files.isEmpty()) return "No AIFF, WAV, FLAC or CAF files in " + newFolder.getFileName() + ".";

    std::vector<Source> opened;
    std::vector<Track> found;
    int inputs = 0;
    double rate = 0.0;
    juce::int64 longest = 0;
    juce::String skipped;
    for (const auto& f : files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr) { skipped += f.getFileName() + " "; continue; }
        if (rate == 0.0) rate = reader->sampleRate;
        if (std::fabs (reader->sampleRate - rate) > 1.0) { skipped += f.getFileName() + " (different sample rate) "; continue; }
        if (inputs + int (reader->numChannels) > kMaxInputs) { skipped += f.getFileName() + " (over " + juce::String (kMaxInputs) + " inputs) "; continue; }
        Source s;
        s.channels = std::min (2, int (reader->numChannels));
        s.firstInput = inputs;
        s.lengthFrames = reader->lengthInSamples;
        s.reader = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
        s.reader->setLooping (false);
        Track t;
        t.name = StemNames::cleanName (f.getFileNameWithoutExtension());
        t.channels = s.channels;
        t.firstInput = inputs;
        t.roleGuessed = StemNames::guessRole (f.getFileNameWithoutExtension(), t.guessedRole);
        inputs += s.channels;
        longest = std::max (longest, s.lengthFrames);
        opened.push_back (std::move (s));
        found.push_back (t);
    }
    if (opened.empty()) return "None of the files could be read." + (skipped.isNotEmpty() ? " Skipped: " + skipped : juce::String());

    folder = newFolder;
    sources = std::move (opened);
    tracks = std::move (found);
    totalChannels = inputs;
    fileRate = rate;
    longestFrames = longest;
    return {};
}

void MultitrackSource::unload()
{
    release();
    sources.clear();
    tracks.clear();
    totalChannels = 0;
    fileRate = 0.0;
    longestFrames = 0;
    folder = juce::File();
}

double MultitrackSource::getPositionSeconds() const noexcept
{
    return deviceRate > 0.0 ? double (position.load()) / deviceRate : 0.0;
}

MixSession MultitrackSource::suggestedSession (const MixSession& base) const
{
    MixSession s = base;
    s.name = folder.getFileName().toStdString();
    s.inputs.clear();
    for (const auto& t : tracks)
    {
        InputAssignment a;
        a.name = t.name.toStdString();
        a.role = t.roleGuessed ? t.guessedRole : ChannelRole::LeadVocal;
        a.inputA = t.firstInput;
        a.inputB = t.channels == 2 ? t.firstInput + 1 : -1;
        a.enabled = t.roleGuessed;   // an unrecognised file is listed but left for the user to assign
        s.inputs.push_back (a);
    }
    return s;
}

void MultitrackSource::prepare (int blockSize, double sampleRate)
{
    release();
    if (sources.empty()) return;
    deviceRate = sampleRate;
    const bool resample = std::fabs (sampleRate - fileRate) > 1.0;
    thread.startThread (juce::Thread::Priority::normal);
    for (auto& s : sources)
    {
        // Disk reads happen on the thread (BufferingAudioSource); when the device runs at another rate the
        // buffered stream is resampled on the audio thread (linear, cheap).
        s.reader->setNextReadPosition (0);
        s.buffering = std::make_unique<juce::BufferingAudioSource> (s.reader.get(), thread, false, kBufferedSamples, s.channels, true);
        s.buffering->prepareToPlay (blockSize, sampleRate);
        if (resample)
        {
            s.resampler = std::make_unique<juce::ResamplingAudioSource> (s.buffering.get(), false, s.channels);
            s.resampler->setResamplingRatio (fileRate / sampleRate);
            s.resampler->prepareToPlay (blockSize, sampleRate);
        }
    }
    scratch.setSize (2, blockSize * 4, false, true, true);
    loopDeviceFrames = juce::int64 (double (longestFrames) * sampleRate / fileRate);
    position.store (0);
    rewindRequested.store (false);
    prepared = true;
}

void MultitrackSource::release()
{
    prepared = false;
    for (auto& s : sources)
    {
        if (s.resampler != nullptr) s.resampler->releaseResources();
        if (s.buffering != nullptr) s.buffering->releaseResources();
        s.resampler.reset();
        s.buffering.reset();
    }
    thread.stopThread (2000);
}

void MultitrackSource::fillNext (float* const* dest, int numChannels, int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
        if (dest[ch] != nullptr) juce::FloatVectorOperations::clear (dest[ch], numSamples);
    if (! prepared || numSamples > scratch.getNumSamples()) return;

    if (rewindRequested.exchange (false) || (loopDeviceFrames > 0 && position.load() >= loopDeviceFrames))
    {
        for (auto& s : sources) s.buffering->setNextReadPosition (0);
        position.store (0);
    }

    for (auto& s : sources)
    {
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), s.channels, 0, numSamples);
        juce::AudioSourceChannelInfo info (&view, 0, numSamples);
        if (s.resampler != nullptr) s.resampler->getNextAudioBlock (info);
        else s.buffering->getNextAudioBlock (info);
        for (int ch = 0; ch < s.channels; ++ch)
        {
            const int input = s.firstInput + ch;
            if (input < numChannels && dest[input] != nullptr)
                juce::FloatVectorOperations::copy (dest[input], view.getReadPointer (ch), numSamples);
        }
    }
    position.fetch_add (numSamples);
}

} // namespace livemix
