#include "MixBounce.h"
#include "DSP/LoudnessMeter.h"
#include <cmath>
#include <memory>
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

    // Waits for an encoder, asking `stop` every tenth of a second, so an export that is being
    // cancelled - or a window that is quitting - is not held for the length of an MP3 encode.
    juce::String finishEncoder (juce::ChildProcess& p, const char* tool, const std::function<bool()>& stop)
    {
        for (int waited = 0; waited < 600000; waited += 100)
        {
            if (p.waitForProcessToFinish (100))
                return p.getExitCode() == 0 ? juce::String() : juce::String (tool) + " could not make an MP3. Try exporting as WAV.";
            if (stop && stop()) { p.kill(); return "Export cancelled."; }
        }
        p.kill();
        return juce::String (tool) + " could not make an MP3. Try exporting as WAV.";
    }

    juce::String encodeMp3 (const juce::File& wavFile, const juce::File& mp3File, const std::function<bool()>& stop)
    {
        if (mp3File.existsAsFile()) mp3File.deleteFile();
        juce::String err;
        if (const auto ffmpeg = findTool ({ "ffmpeg" }); ffmpeg != juce::File())
        {
            juce::ChildProcess p;
            juce::StringArray args { ffmpeg.getFullPathName(), "-y", "-i", wavFile.getFullPathName(),
                                     "-codec:a", "libmp3lame", "-q:a", "2", mp3File.getFullPathName() };
            if (! p.start (args)) return "Could not start ffmpeg.";
            err = finishEncoder (p, "ffmpeg", stop);
        }
        else if (const auto lame = findTool ({ "lame" }); lame != juce::File())
        {
            juce::ChildProcess p;
            juce::StringArray args { lame.getFullPathName(), "-V2", wavFile.getFullPathName(), mp3File.getFullPathName() };
            if (! p.start (args)) return "Could not start lame.";
            err = finishEncoder (p, "lame", stop);
        }
        else
            return "MP3 needs ffmpeg or lame on this Mac. Export as WAV, or install ffmpeg (brew install ffmpeg).";
        if (err.isNotEmpty()) mp3File.deleteFile();   // a half-written MP3 is not an export
        return err;
    }

    // The extension a format lands on. MP3 is encoded from a WAV, so its render is a WAV.
    const char* extensionFor (Format f) noexcept { return f == Format::Aiff ? ".aiff" : ".wav"; }

    // One open file, ready to be written a block at a time.
    struct Sink
    {
        juce::File file;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        juce::AudioBuffer<float> buffer;
    };

    juce::String openSink (Sink& sink, const juce::File& file, Format format, double sr, int channels)
    {
        file.getParentDirectory().createDirectory();
        if (file.existsAsFile()) file.deleteFile();
        auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        if (stream == nullptr) return "Could not create " + file.getFileName() + ".";

        std::unique_ptr<juce::AudioFormat> af;
        if (format == Format::Aiff) af = std::make_unique<juce::AiffAudioFormat>();
        else                        af = std::make_unique<juce::WavAudioFormat>();
        sink.writer.reset (af->createWriterFor (stream.get(), sr, juce::uint32 (channels), 24, {}, 0));
        if (sink.writer == nullptr) return "Could not write audio into " + file.getFileName() + ".";
        stream.release();
        sink.file = file;
        sink.buffer.setSize (channels, kBlock);
        return {};
    }

    void discard (std::vector<Sink>& sinks)
    {
        for (auto& s : sinks) { s.writer.reset(); s.file.deleteFile(); }
        sinks.clear();
    }

    // A name that can be a file: the input's own name, with anything a filesystem argues
    // about taken out, and never empty.
    juce::String safeName (const juce::String& name, int fallbackNumber)
    {
        auto clean = juce::File::createLegalFileName (name).trim();
        clean = clean.removeCharacters ("/:\\");
        return clean.isNotEmpty() ? clean : "Input " + juce::String (fallbackNumber);
    }

    juce::String busFileName (MixBus bus)
    {
        const juce::String raw (mixBusName (bus));
        return raw.length() <= 3 ? raw.toUpperCase()
                                 : raw.substring (0, 1).toUpperCase() + raw.substring (1).toLowerCase();
    }

    // The clips, resolved to real files, in the session's device-channel layout.
    std::vector<ClipSource::Track> resolveClips (const MixSession& session, const Project& project)
    {
        std::vector<ClipSource::Track> tracks;
        tracks.resize (session.inputs.size());
        for (size_t i = 0; i < session.inputs.size(); ++i)
        {
            tracks[i].channels = session.inputs[i].isStereo() ? 2 : 1;
            if (i >= project.tracks.size()) continue;
            for (const auto& clip : project.tracks[i].clips)
            {
                const auto resolved = project.fileFor (clip);
                if (! resolved.existsAsFile()) continue;
                auto copy = clip;
                copy.file = resolved.getFullPathName();
                const auto right = project.rightFileFor (clip);
                copy.fileRight = right.existsAsFile() ? right.getFullPathName() : juce::String();
                tracks[i].clips.push_back (copy);
            }
        }
        return tracks;
    }

    // ------------------------------------------------------------------ the raw multitrack
    // No engine at all: what is on the disk, per input, with nothing in the way.
    juce::String renderRaw (const MixSession& session,
                            const Project& project,
                            const juce::File& folder,
                            Format format,
                            double sr,
                            juce::int64 from,
                            juce::int64 to,
                            const std::function<bool (float)>& onProgress,
                            juce::StringArray* written)
    {
        auto clipTracks = resolveClips (session, project);
        ClipSource source;
        source.prepare (sr, kBlock, clipTracks);

        std::vector<Sink> sinks;
        std::vector<int> trackOf;
        for (size_t i = 0; i < session.inputs.size(); ++i)
        {
            const int channels = clipTracks[i].channels;
            Sink sink;
            const auto name = juce::String (int (i) + 1).paddedLeft ('0', 2) + " "
                            + safeName (juce::String (session.inputs[i].name), int (i) + 1);
            if (auto err = openSink (sink, folder.getChildFile (name + extensionFor (format)), format, sr, channels);
                err.isNotEmpty())
            {
                discard (sinks);
                return err;
            }
            sinks.push_back (std::move (sink));
            trackOf.push_back (int (i));
        }
        if (sinks.empty()) return "There are no inputs to write.";

        const juce::int64 total = juce::jmax ((juce::int64) 1, to - from);
        for (juce::int64 pos = from; pos < to; pos += kBlock)
        {
            const int n = int (juce::jmin ((juce::int64) kBlock, to - pos));
            source.read (pos, n);
            for (size_t k = 0; k < sinks.size(); ++k)
            {
                auto& sink = sinks[k];
                const int channels = sink.buffer.getNumChannels();
                for (int ch = 0; ch < channels; ++ch)
                {
                    const float* in = source.channel (trackOf[k], ch);
                    if (in != nullptr) sink.buffer.copyFrom (ch, 0, in, n);
                    else               sink.buffer.clear (ch, 0, n);
                }
                if (! sink.writer->writeFromAudioSampleBuffer (sink.buffer, 0, n))
                {
                    discard (sinks);
                    return "The export could not be written. Check the disk.";
                }
            }
            if (onProgress && ! onProgress (float (double (pos + n - from) / double (total))))
            {
                discard (sinks);
                return "Export cancelled.";
            }
        }
        for (auto& s : sinks) { s.writer->flush(); if (written != nullptr) written->add (s.file.getFileName()); }
        return {};
    }

    // ------------------------------------------------------------------ through the mix
    // The stereo mix, or one file per group. Both are the same walk through the engine; what
    // differs is which of its buffers is written.
    juce::String renderThroughMix (const MixSession& session,
                                   const MixParameters& params,
                                   const Project& project,
                                   const juce::File& dest,
                                   Format format,
                                   What what,
                                   double sr,
                                   juce::int64 from,
                                   juce::int64 to,
                                   const std::function<bool (float)>& onProgress,
                                   LoudnessMeter* measure,
                                   juce::StringArray* written,
                                   const SampleBankTable* samples)
    {
        auto clipTracks = resolveClips (session, project);
        ClipSource source;
        source.prepare (sr, kBlock, clipTracks);

        MixEngine engine;
        engine.prepare (sr, kBlock, session);
        // The drum sounds the live mix blends in: without them a replaced kick or snare would
        // be exported as the bare microphone.
        engine.setSampleBanks (samples);
        engine.setParameters (params);
        engine.reset();

        std::vector<Sink> sinks;
        std::vector<MixBus> stemOf;
        if (what == What::GroupStems)
        {
            for (int b = 0; b < int (MixBus::Master); ++b)
            {
                const auto bus = mixBusInDisplayOrder (b);
                if (! engine.isBusUsed (bus)) continue;
                Sink sink;
                if (auto err = openSink (sink, dest.getChildFile (busFileName (bus) + extensionFor (format)), format, sr, 2);
                    err.isNotEmpty())
                {
                    discard (sinks);
                    return err;
                }
                sinks.push_back (std::move (sink));
                stemOf.push_back (bus);
            }
            if (sinks.empty()) return "This mix has no group buses to write.";
        }
        else
        {
            Sink sink;
            if (auto err = openSink (sink, dest, format, sr, 2); err.isNotEmpty()) return err;
            sinks.push_back (std::move (sink));
        }

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

            bool ok = true;
            if (what == What::GroupStems)
            {
                for (size_t k = 0; k < sinks.size() && ok; ++k)
                {
                    auto& sink = sinks[k];
                    const float g = engine.busFaderGain (stemOf[k]);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float* in = engine.busOutput (stemOf[k], ch);
                        if (in == nullptr) { sink.buffer.clear (ch, 0, n); continue; }
                        sink.buffer.copyFrom (ch, 0, in, n, g);
                    }
                    ok = sink.writer->writeFromAudioSampleBuffer (sink.buffer, 0, n);
                }
            }
            else
            {
                if (measure != nullptr)
                {
                    float* mp[2] = { out.getWritePointer (0), out.getWritePointer (1) };
                    AudioBlockView view { mp, 2, n };
                    measure->process (view);
                }
                ok = sinks[0].writer->writeFromAudioSampleBuffer (out, 0, n);
            }
            if (! ok)
            {
                discard (sinks);
                return "The export could not be written. Check the disk.";
            }
            if (onProgress && ! onProgress (float (double (pos + n - from) / double (total))))
            {
                discard (sinks);
                return "Export cancelled.";
            }
        }
        for (auto& s : sinks) { s.writer->flush(); if (written != nullptr) written->add (s.file.getFileName()); }
        return {};
    }

    // ------------------------------------------------------------------ the loudness pass
    // One gain over the whole render, from what the render actually measured. Streaming, so a
    // three-hour service is normalised without three hours of RAM, and the ceiling is held so
    // a quiet mix asked up to -14 cannot be turned into a clipped one.
    juce::String applyGain (const juce::File& src, const juce::File& dest, Format format, float gain,
                            const std::function<bool (float)>& onProgress)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (src));
        if (reader == nullptr) return "Could not read the render back to set its loudness.";

        Sink sink;
        if (auto err = openSink (sink, dest, format, reader->sampleRate, int (reader->numChannels)); err.isNotEmpty())
            return err;

        juce::AudioBuffer<float> block (int (reader->numChannels), kBlock);
        for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += kBlock)
        {
            const int n = int (juce::jmin ((juce::int64) kBlock, reader->lengthInSamples - pos));
            reader->read (&block, 0, n, pos, true, true);
            block.applyGain (0, n, gain);
            if (! sink.writer->writeFromAudioSampleBuffer (block, 0, n))
            {
                sink.writer.reset();
                dest.deleteFile();
                return "The export could not be written. Check the disk.";
            }
            if (onProgress && ! onProgress (float (double (pos + n) / double (juce::jmax ((juce::int64) 1, reader->lengthInSamples)))))
            {
                sink.writer.reset();
                dest.deleteFile();
                return "Export cancelled.";
            }
        }
        sink.writer->flush();
        return {};
    }
}

juce::String renderProject (const MixSession& session,
                            const MixParameters& params,
                            const Project& project,
                            const juce::File& dest,
                            Format format,
                            Options options,
                            juce::StringArray* written)
{
    if (session.inputs.empty() || params.numStrips <= 0)
        return "Assign inputs and build a mix before exporting.";
    if (! project.hasAudio())
        return "There is nothing recorded yet. Record or import a multitrack first.";

    const double sr = options.sampleRate > 0.0 ? options.sampleRate
                                               : (project.sampleRate > 0.0 ? project.sampleRate : 48000.0);
    const juce::int64 from = juce::jmax ((juce::int64) 0, options.from);
    const juce::int64 to = options.to > 0 ? options.to : project.lengthSamples();
    if (to <= from) return "That range is empty.";

    const auto stage = [&options] (Stage st, bool measurable) { if (options.onStage) options.onStage (st, measurable); };
    // A stage with no number of its own is still asked whether to stop.
    const std::function<bool()> stopRequested = [&options] { return options.onProgress && ! options.onProgress (-1.0f); };
    stage (Stage::Mixing, true);

    // Stems and a raw multitrack are a folder of files, named after what the single file
    // would have been called, beside where it would have gone.
    if (options.what != What::StereoMix)
    {
        const auto folder = dest.getParentDirectory()
                                .getChildFile (dest.getFileNameWithoutExtension()
                                               + (options.what == What::GroupStems ? " stems" : " multitrack"));
        if (! folder.createDirectory()) return "Could not create the export folder.";
        // MP3 is a delivery format for a finished mix; a set of parts is written as audio.
        const auto partFormat = format == Format::Mp3 ? Format::Wav : format;
        if (options.what == What::RawMultitrack)
            return renderRaw (session, project, folder, partFormat, sr, from, to, options.onProgress, written);
        return renderThroughMix (session, params, project, folder, partFormat, What::GroupStems,
                                 sr, from, to, options.onProgress, nullptr, written, options.samples);
    }

    if (! dest.getParentDirectory().isDirectory() && ! dest.getParentDirectory().createDirectory())
        return "Could not create the export folder.";

    const bool normalising = options.loudness != Loudness::AsMixed;
    const auto renderFormat = format == Format::Mp3 ? Format::Wav : format;
    const auto finalFile = format == Format::Mp3 ? dest.withFileExtension ("mp3")
                                                 : dest.withFileExtension (extensionFor (format) + 1);
    // Where the render lands first: straight at the answer when nothing has to happen to it,
    // and beside it when the loudness or the encoder still has a pass to make.
    const bool needsTemp = normalising || format == Format::Mp3;
    const auto renderFile = needsTemp
        ? dest.getSiblingFile (dest.getFileNameWithoutExtension() + "-export-temp" + extensionFor (renderFormat))
        : finalFile;

    LoudnessMeter meter;
    if (normalising) meter.prepare (sr, kBlock, 2);
    if (auto err = renderThroughMix (session, params, project, renderFile, renderFormat, What::StereoMix,
                                     sr, from, to, options.onProgress, normalising ? &meter : nullptr,
                                     needsTemp ? nullptr : written, options.samples);
        err.isNotEmpty())
        return err;

    auto gained = renderFile;
    if (normalising)
    {
        const float measured = meter.getIntegratedLufs();
        if (measured > -70.0f)
        {
            // The ceiling the delivery already promised: a mix turned up to the target must
            // not be turned into a clipped one, so the lift stops at what the peak allows.
            const float wanted = targetLufs (options.loudness) - measured;
            const float lift = juce::jlimit (-24.0f, 12.0f, wanted);
            if (std::fabs (lift) > 0.05f)
            {
                const auto adjusted = renderFile.getSiblingFile (renderFile.getFileNameWithoutExtension() + "-lufs"
                                                                 + extensionFor (renderFormat));
                stage (Stage::Loudness, true);
                if (auto err = applyGain (renderFile, adjusted, renderFormat, std::pow (10.0f, lift / 20.0f), options.onProgress);
                    err.isNotEmpty())
                {
                    renderFile.deleteFile();
                    return err;
                }
                renderFile.deleteFile();
                gained = adjusted;
            }
        }
    }

    if (format == Format::Mp3)
    {
        stage (Stage::Encoding, false);
        const auto encoded = encodeMp3 (gained, finalFile, stopRequested);
        gained.deleteFile();
        if (encoded.isNotEmpty()) return encoded;
        if (written != nullptr) written->add (finalFile.getFileName());
        return {};
    }

    if (gained != finalFile)
    {
        stage (Stage::Finishing, false);
        finalFile.deleteFile();
        if (! gained.moveFileTo (finalFile)) return "Could not put the export where it was asked for.";
    }
    if (written != nullptr && written->isEmpty()) written->add (finalFile.getFileName());
    return {};
}

} // namespace MixBounce
} // namespace livemix
