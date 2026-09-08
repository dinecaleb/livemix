#include "ClipSource.h"
#include <algorithm>
#include <cmath>

namespace livemix
{

ClipSource::ClipSource()
{
    formats.registerBasicFormats();
}

void ClipSource::prepare (double projectSampleRate, int maxBlock, const std::vector<Track>& source)
{
    release();
    rate = projectSampleRate > 0.0 ? projectSampleRate : 48000.0;
    maxSamples = juce::jmax (32, maxBlock);

    tracks.resize (source.size());
    for (size_t i = 0; i < source.size(); ++i)
    {
        auto& t = tracks[i];
        t.channels = juce::jlimit (1, 2, source[i].channels);
        t.clips = source[i].clips;
        std::sort (t.clips.begin(), t.clips.end(), [] (const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
        for (int ch = 0; ch < t.channels; ++ch)
            t.buffer[size_t (ch)].assign (size_t (maxSamples), 0.0f);
    }
    // Enough room for the fastest resample we allow (a 96 kHz file into a 44.1 kHz project).
    scratch.setSize (2, maxSamples * 3 + 8, false, true, true);
}

void ClipSource::release()
{
    tracks.clear();
    readers.clear();
}

int ClipSource::numChannels (int track) const noexcept
{
    return (track >= 0 && track < int (tracks.size())) ? tracks[size_t (track)].channels : 0;
}

const float* ClipSource::channel (int track, int ch) const noexcept
{
    if (track < 0 || track >= int (tracks.size())) return nullptr;
    const auto& t = tracks[size_t (track)];
    return (ch >= 0 && ch < t.channels) ? t.buffer[size_t (ch)].data() : nullptr;
}

juce::AudioFormatReader* ClipSource::readerFor (const juce::String& path)
{
    const auto found = readers.find (path);
    if (found != readers.end()) return found->second.get();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (path)));
    auto* raw = reader.get();
    readers[path] = std::move (reader);          // a file that will not open is remembered as null, not retried per block
    return raw;
}

void ClipSource::read (juce::int64 from, int count)
{
    count = juce::jlimit (0, maxSamples, count);
    for (auto& track : tracks)
    {
        for (int ch = 0; ch < track.channels; ++ch)
            juce::FloatVectorOperations::clear (track.buffer[size_t (ch)].data(), count);
        if (count == 0) continue;

        for (const auto& clip : track.clips)
        {
            if (clip.start >= from + count) break;         // sorted: nothing further can overlap
            const juce::int64 begin = juce::jmax (from, clip.start);
            const juce::int64 end = juce::jmin (from + count, clip.end());
            if (end <= begin) continue;

            auto* reader = readerFor (clip.file);
            if (reader == nullptr || reader->numChannels == 0) continue;

            const int n = int (end - begin);
            const int destOffset = int (begin - from);
            const double ratio = (clip.fileSampleRate > 0.0) ? clip.fileSampleRate / rate : 1.0;
            const bool sameRate = std::abs (ratio - 1.0) < 1.0e-9;
            const juce::int64 clipOffset = begin - clip.start;

            const int needed = sameRate ? n : int (std::ceil (double (n) * ratio)) + 2;
            if (needed <= 0 || needed > scratch.getNumSamples()) continue;

            const int sourceChannels = juce::jmin (2, int (reader->numChannels));
            scratch.clear (0, needed);
            float* src[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
            reader->read (src, sourceChannels, clip.offset + juce::int64 (double (clipOffset) * ratio), needed);
            if (sourceChannels == 1)
                juce::FloatVectorOperations::copy (src[1], src[0], needed);   // a mono file feeds both sides of a stereo track

            for (int ch = 0; ch < track.channels; ++ch)
            {
                const float* in = scratch.getReadPointer (juce::jmin (ch, 1));
                float* out = track.buffer[size_t (ch)].data() + destOffset;
                if (sameRate)
                {
                    juce::FloatVectorOperations::add (out, in, n);
                }
                else
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const double at = double (i) * ratio;
                        const int i0 = juce::jlimit (0, needed - 1, int (at));
                        const int i1 = juce::jlimit (0, needed - 1, i0 + 1);
                        const float f = float (at - double (i0));
                        out[i] += in[i0] + (in[i1] - in[i0]) * f;
                    }
                }
            }
        }
    }
}

} // namespace livemix
