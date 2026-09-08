#include "MixBounce.h"
#include <cmath>
#include <vector>

namespace livemix
{
namespace MixBounce
{
namespace
{
    constexpr int kBlock = 1024;

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
        // Common installs, for when PATH is thin inside a GUI app.
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
            juce::StringArray args { ffmpeg.getFullPathName(), "-y", "-i", wavFile.getFullPathName(),
                                     "-codec:a", "libmp3lame", "-q:a", "2", mp3File.getFullPathName() };
            if (! p.start (args)) return "Could not start ffmpeg.";
            if (! p.waitForProcessToFinish (600000) || p.getExitCode() != 0)
                return "ffmpeg could not make an MP3. Try exporting as WAV.";
            return {};
        }
        if (const auto lame = findTool ({ "lame" }); lame != juce::File())
        {
            juce::ChildProcess p;
            juce::StringArray args { lame.getFullPathName(), "-V2", wavFile.getFullPathName(), mp3File.getFullPathName() };
            if (! p.start (args)) return "Could not start lame.";
            if (! p.waitForProcessToFinish (600000) || p.getExitCode() != 0)
                return "lame could not make an MP3. Try exporting as WAV.";
            return {};
        }
        return "MP3 needs ffmpeg or lame on this Mac. Export as WAV, or install ffmpeg (brew install ffmpeg).";
    }

    // The whole render, straight into an open WAV writer.
    juce::String renderTo (const juce::File& file,
                           const MixSession& session,
                           const MixParameters& params,
                           const Project& project,
                           double sr,
                           juce::int64 from,
                           juce::int64 to,
                           const std::function<bool (float)>& onProgress)
    {
        file.getParentDirectory().createDirectory();
        if (file.existsAsFile()) file.deleteFile();

        juce::WavAudioFormat wav;
        auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        if (stream == nullptr) return "Could not create " + file.getFileName() + ".";
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
        if (writer == nullptr) return "Could not write WAV data.";
        stream.release();

        // The clips, resolved to real files, in the session's device-channel layout.
        std::vector<ClipSource::Track> clipTracks;
        clipTracks.resize (session.inputs.size());
        for (size_t i = 0; i < session.inputs.size(); ++i)
        {
            clipTracks[i].channels = session.inputs[i].isStereo() ? 2 : 1;
            if (i >= project.tracks.size()) continue;
            for (const auto& clip : project.tracks[i].clips)
            {
                const auto resolved = project.fileFor (clip);
                if (! resolved.existsAsFile()) continue;
                auto copy = clip;
                copy.file = resolved.getFullPathName();
                clipTracks[i].clips.push_back (copy);
            }
        }

        ClipSource source;
        source.prepare (sr, kBlock, clipTracks);

        MixEngine engine;
        engine.prepare (sr, kBlock, session);
        engine.setParameters (params);
        engine.reset();

        int matrixChannels = 0;
        for (const auto& in : session.inputs)
            matrixChannels = juce::jmax (matrixChannels, in.inputA + 1, in.inputB + 1);
        matrixChannels = juce::jlimit (1, kMaxInputs, matrixChannels);

        std::vector<float> silence (size_t (kBlock), 0.0f);
        std::vector<const float*> matrix (size_t (matrixChannels), silence.data());
        juce::AudioBuffer<float> out (2, kBlock);

        const juce::int64 total = juce::jmax ((juce::int64) 1, to - from);
        for (juce::int64 pos = from; pos < to; pos += kBlock)
        {
            const int n = int (juce::jmin ((juce::int64) kBlock, to - pos));
            source.read (pos, n);

            for (auto& p : matrix) p = silence.data();
            for (size_t t = 0; t < session.inputs.size(); ++t)
            {
                const auto& in = session.inputs[t];
                if (in.inputA >= 0 && in.inputA < matrixChannels)
                    if (const float* c = source.channel (int (t), 0)) matrix[size_t (in.inputA)] = c;
                if (in.inputB >= 0 && in.inputB < matrixChannels)
                    if (const float* c = source.channel (int (t), 1)) matrix[size_t (in.inputB)] = c;
            }

            float* op[2] = { out.getWritePointer (0), out.getWritePointer (1) };
            engine.process (matrix.data(), matrixChannels, op, 2, n);
            if (! writer->writeFromAudioSampleBuffer (out, 0, n))
            {
                writer.reset();
                file.deleteFile();
                return "The export could not be written. Check the disk.";
            }
            if (onProgress && ! onProgress (float (double (pos + n - from) / double (total))))
            {
                writer.reset();
                file.deleteFile();
                return "Export cancelled.";
            }
        }
        writer->flush();
        return {};
    }
}

juce::String renderProject (const MixSession& session,
                            const MixParameters& params,
                            const Project& project,
                            const juce::File& dest,
                            Format format,
                            Options options)
{
    if (session.inputs.empty() || params.numStrips <= 0)
        return "Assign inputs and build a mix before exporting.";
    if (! project.hasAudio())
        return "There is nothing recorded yet. Record or import a multitrack first.";
    if (! dest.getParentDirectory().isDirectory() && ! dest.getParentDirectory().createDirectory())
        return "Could not create the export folder.";

    const double sr = options.sampleRate > 0.0 ? options.sampleRate
                                               : (project.sampleRate > 0.0 ? project.sampleRate : 48000.0);
    const juce::int64 from = juce::jmax ((juce::int64) 0, options.from);
    const juce::int64 to = options.to > 0 ? options.to : project.lengthSamples();
    if (to <= from) return "That range is empty.";

    if (format == Format::Wav)
        return renderTo (dest.hasFileExtension (".wav") ? dest : dest.withFileExtension ("wav"),
                         session, params, project, sr, from, to, options.onProgress);

    // MP3: render a temporary WAV, encode it, remove the temporary.
    const auto wavDest = dest.getSiblingFile (dest.getFileNameWithoutExtension() + "-export-temp.wav");
    const auto mp3Dest = dest.hasFileExtension (".mp3") ? dest : dest.withFileExtension ("mp3");
    if (auto err = renderTo (wavDest, session, params, project, sr, from, to, options.onProgress); err.isNotEmpty())
        return err;
    const auto encoded = encodeMp3 (wavDest, mp3Dest);
    wavDest.deleteFile();
    return encoded;
}

} // namespace MixBounce
} // namespace livemix
