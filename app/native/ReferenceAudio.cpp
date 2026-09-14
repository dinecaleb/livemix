#include "ReferenceAudio.h"
#include "Analysis/AnalysisAccumulator.h"

namespace livemix
{

ReferenceAudio::Result ReferenceAudio::measure (const juce::File& file, StyleProfileId profile)
{
    Result out;
    if (! file.existsAsFile())
    {
        out.error = "That file is not there any more.";
        return out;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        out.error = "DLIVE could not read " + file.getFileName() + ". Try a WAV, AIFF, MP3 or M4A.";
        return out;
    }

    // Two channels is what a mix is: a 5.1 stem set measured as one interleaved block would
    // read as a stereo image it does not have.
    const int channels = juce::jlimit (1, 2, int (reader->numChannels));
    const auto total = juce::jmin (reader->lengthInSamples,
                                   juce::int64 (double (kMaxSeconds) * reader->sampleRate));

    AnalysisAccumulator accumulator;
    accumulator.prepare (reader->sampleRate, channels);
    accumulator.reset();

    const int blockSize = 8192;
    juce::AudioBuffer<float> block (juce::jmax (2, int (reader->numChannels)), blockSize);
    std::vector<float> interleaved (size_t (blockSize * channels));

    for (juce::int64 pos = 0; pos < total;)
    {
        const int n = int (juce::jmin (juce::int64 (blockSize), total - pos));
        block.clear();
        if (! reader->read (&block, 0, n, pos, true, channels > 1))
        {
            out.error = "DLIVE could not read all of " + file.getFileName() + ".";
            return out;
        }
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < channels; ++c)
                interleaved[size_t (i * channels + c)] = block.getSample (c, i);
        accumulator.consume (interleaved.data(), n);
        pos += n;
    }

    const AnalysisResult measured = accumulator.finalise();
    out.adequacy = Reference::adequacy (measured, profile);
    if (! out.adequacy.usable) return out;

    out.profile = Reference::profileFrom (measured, file.getFileNameWithoutExtension().toStdString(),
                                          file.getFullPathName().toStdString());
    return out;
}

} // namespace livemix
