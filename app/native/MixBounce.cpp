#include "MixBounce.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace livemix
{
namespace MixBounce
{
namespace
{
    juce::String writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sr)
    {
        file.getParentDirectory().createDirectory();
        if (file.existsAsFile()) file.deleteFile();
        juce::WavAudioFormat wav;
        auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        if (out == nullptr) return "Could not create " + file.getFileName() + ".";
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (out.get(), sr, (unsigned) audio.getNumChannels(), 24, {}, 0));
        if (writer == nullptr) return "Could not write WAV data.";
        out.release();
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        writer->flush();
        return {};
    }

    juce::File findTool (const juce::StringArray& names)
    {
        for (const auto& name : names)
        {
            const auto which = juce::File ("/usr/bin/which");
            if (! which.existsAsFile()) break;
            juce::ChildProcess probe;
            juce::StringArray args;
            args.add (which.getFullPathName());
            args.add (name);
            if (! probe.start (args)) continue;
            probe.waitForProcessToFinish (2000);
            const auto path = probe.readAllProcessOutput().trim();
            if (path.isNotEmpty() && juce::File (path).existsAsFile()) return juce::File (path);
        }
        // Common installs when PATH is thin in a GUI app.
        for (const auto& name : names)
            for (const auto& root : { "/opt/homebrew/bin/", "/usr/local/bin/", "/usr/bin/" })
            {
                const juce::File f (root + name);
                if (f.existsAsFile()) return f;
            }
        return {};
    }

    juce::String encodeMp3 (const juce::File& wavFile, const juce::File& mp3File)
    {
        if (mp3File.existsAsFile()) mp3File.deleteFile();
        if (const auto ffmpeg = findTool ({ "ffmpeg" }); ffmpeg != juce::File())
        {
            juce::ChildProcess p;
            juce::StringArray args;
            args.add (ffmpeg.getFullPathName());
            args.add ("-y");
            args.add ("-i");
            args.add (wavFile.getFullPathName());
            args.add ("-codec:a");
            args.add ("libmp3lame");
            args.add ("-q:a");
            args.add ("2");
            args.add (mp3File.getFullPathName());
            if (! p.start (args))
                return "Could not start ffmpeg.";
            if (! p.waitForProcessToFinish (600000) || p.getExitCode() != 0)
                return "ffmpeg could not make an MP3. Try exporting as WAV.";
            return {};
        }
        if (const auto lame = findTool ({ "lame" }); lame != juce::File())
        {
            juce::ChildProcess p;
            juce::StringArray args;
            args.add (lame.getFullPathName());
            args.add ("-V2");
            args.add (wavFile.getFullPathName());
            args.add (mp3File.getFullPathName());
            if (! p.start (args))
                return "Could not start lame.";
            if (! p.waitForProcessToFinish (600000) || p.getExitCode() != 0)
                return "lame could not make an MP3. Try exporting as WAV.";
            return {};
        }
        return "MP3 needs ffmpeg or lame on this Mac. Export as WAV, or install ffmpeg (brew install ffmpeg).";
    }
}

juce::String renderToFile (const MixSession& session,
                           const MixParameters& params,
                           const juce::File& stemsFolder,
                           const juce::File& dest,
                           Format format,
                           double maxSeconds)
{
    if (! stemsFolder.isDirectory())
        return "Choose a folder of multitrack stems to export from.";
    if (session.inputs.empty() || params.numStrips <= 0)
        return "Assign inputs and build a mix before exporting.";
    if (! dest.getParentDirectory().isDirectory() && ! dest.getParentDirectory().createDirectory())
        return "Could not create the export folder.";

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::Array<juce::File> files = stemsFolder.findChildFiles (juce::File::findFiles, false,
                                                                "*.aif;*.aiff;*.wav;*.flac;*.aifc;*.caf");
    files.sort();
    if (files.isEmpty()) return "No AIFF, WAV, FLAC or CAF files in " + stemsFolder.getFileName() + ".";

    // Same device-channel packing as MultitrackSource: files in name order, mono/stereo packed tightly.
    struct Stem
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        int firstInput = 0;
        int channels = 1;
    };
    std::vector<Stem> stems;
    double sr = 0.0;
    juce::int64 longest = 0;
    int inputs = 0;
    for (const auto& f : files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr) continue;
        if (sr == 0.0) sr = reader->sampleRate;
        if (std::fabs (reader->sampleRate - sr) > 1.0) continue;
        if (inputs + int (reader->numChannels) > kMaxInputs) break;
        Stem s;
        s.channels = std::min (2, int (reader->numChannels));
        s.firstInput = inputs;
        s.reader = std::move (reader);
        longest = std::max (longest, s.reader->lengthInSamples);
        inputs += s.channels;
        stems.push_back (std::move (s));
    }
    if (stems.empty() || sr <= 0.0) return "None of the stem files could be read.";

    juce::int64 numSamples = longest;
    if (maxSeconds > 0.0)
        numSamples = std::min (numSamples, juce::int64 (std::ceil (maxSeconds * sr)));
    if (numSamples <= 0) return "That recording is empty.";

    // Interleaved device layout buffer: one channel vector per input index.
    std::vector<std::vector<float>> channels;
    channels.resize (size_t (inputs));
    for (auto& ch : channels) ch.assign (size_t (numSamples), 0.0f);

    juce::AudioBuffer<float> readBuf;
    for (size_t si = 0; si < stems.size(); ++si)
    {
        auto& s = stems[si];
        const int n = int (std::min (numSamples, s.reader->lengthInSamples));
        readBuf.setSize (s.channels, n, false, false, true);
        if (! s.reader->read (&readBuf, 0, n, 0, true, s.channels > 1))
            return "Could not read one of the stem files.";
        for (int c = 0; c < s.channels; ++c)
            std::memcpy (channels[size_t (s.firstInput + c)].data(),
                         readBuf.getReadPointer (c), sizeof (float) * size_t (n));
    }

    std::vector<const float*> inPtrs;
    inPtrs.resize (size_t (inputs));
    for (int i = 0; i < inputs; ++i) inPtrs[size_t (i)] = channels[size_t (i)].data();

    constexpr int kBlock = 512;
    MixEngine engine;
    engine.prepare (sr, kBlock, session);
    engine.setParameters (params);

    juce::AudioBuffer<float> out (2, int (numSamples));
    out.clear();
    engine.reset();
    std::vector<const float*> blockIn;
    blockIn.resize (size_t (inputs));
    float* op[2];
    for (int pos = 0; pos < int (numSamples); pos += kBlock)
    {
        const int n = std::min (kBlock, int (numSamples) - pos);
        for (int i = 0; i < inputs; ++i) blockIn[size_t (i)] = inPtrs[size_t (i)] + pos;
        op[0] = out.getWritePointer (0) + pos;
        op[1] = out.getWritePointer (1) + pos;
        engine.process (blockIn.data(), inputs, op, 2, n);
    }

    if (format == Format::Wav)
        return writeWav (dest.hasFileExtension (".wav") ? dest : dest.withFileExtension ("wav"), out, sr);

    // MP3: write a temp WAV, encode, delete the temp.
    const auto wavDest = dest.getSiblingFile (dest.getFileNameWithoutExtension() + "-export-temp.wav");
    const auto mp3Dest = dest.hasFileExtension (".mp3") ? dest : dest.withFileExtension ("mp3");
    if (auto err = writeWav (wavDest, out, sr); err.isNotEmpty()) return err;
    const auto enc = encodeMp3 (wavDest, mp3Dest);
    wavDest.deleteFile();
    return enc;
}

} // namespace MixBounce
} // namespace livemix
