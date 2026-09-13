// DLIVE offline success test: a folder of recorded stems goes through the complete
// standalone pipeline with no audio device and no UI.
//
//   dlive_mix_stems <stems folder> [seconds=30] [outdir=<folder>/dlive-out] [gospel|worship] [offsetSeconds] [broadcast|livestream|recording]
//
// 1. Files are assigned to sources by name (kick, snare, tom, drums/oh, room, bass, keys, lead, vox, pastor ...).
// 2. RoutingGraph builds the buses and returns; the engine starts on the profile baselines.
// 3. The window is rendered once with the baselines while OfflineCapture listens (this is TUNE MIX).
// 4. MixPlanner plans; the plan is applied and the window is rendered again.
// 5. raw.wav (flat console: no processing), before.wav (baselines) and after.wav (the plan) are written,
//    the plan is printed, the output loudness is measured and the plan is re-run on the same listen to
//    prove it changes nothing.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Mix/MixEngine.h"
#include "native/StemNames.h"
#include "Mix/OfflineCapture.h"
#include "Mix/MixPlanner.h"
#include "DSP/Compressor.h"
#include "Analysis/AnalysisAccumulator.h"
#include "Profiles/MixProfileData.h"
#include "Core/DbUtils.h"
#include <cstdio>
#include <memory>
#include <vector>

using namespace livemix;

namespace
{
    bool guessRole (const juce::String& fileName, ChannelRole& role) { return StemNames::guessRole (fileName, role); }

    struct Stem
    {
        juce::String name;
        ChannelRole role;
        int channels = 1;
        juce::AudioBuffer<float> audio;   // the window
    };

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sr)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());
        if (out == nullptr) { std::printf ("cannot write %s\n", file.getFullPathName().toRawUTF8()); return; }
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), sr, (unsigned) audio.getNumChannels(), 24, {}, 0));
        if (writer == nullptr) { std::printf ("cannot create writer for %s\n", file.getFullPathName().toRawUTF8()); return; }
        out.release();
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        writer->flush();
    }

    // Renders the window through the engine with the parameters it currently holds. Optionally listens.
    juce::AudioBuffer<float> render (MixEngine& engine, const std::vector<const float*>& inputs, int numSamples, int block, OfflineCapture* capture)
    {
        juce::AudioBuffer<float> out (2, numSamples);
        out.clear();
        std::vector<const float*> in (inputs.size());
        float* op[2];
        engine.reset();
        if (capture != nullptr) capture->start();
        for (int pos = 0; pos < numSamples; pos += block)
        {
            const int n = std::min (block, numSamples - pos);
            for (size_t c = 0; c < inputs.size(); ++c) in[c] = inputs[c] + pos;
            op[0] = out.getWritePointer (0) + pos;
            op[1] = out.getWritePointer (1) + pos;
            engine.process (in.data(), int (in.size()), op, 2, n);
        }
        return out;
    }

    AnalysisResult measure (const juce::AudioBuffer<float>& audio, double sr)
    {
        AnalysisAccumulator acc;
        acc.prepare (sr, audio.getNumChannels());
        juce::AudioBuffer<float> copy (audio);
        AudioBlockView v { copy.getArrayOfWritePointers(), copy.getNumChannels(), copy.getNumSamples() };
        acc.consume (v);
        return acc.finalise();
    }

    void printMeasurement (const char* label, const AnalysisResult& a)
    {
        std::printf ("  %-7s %6.1f LUFS  true peak %6.1f dBTP  crest %4.1f dB  bands", label, double (a.loudnessLufs), double (a.truePeakDb), double (a.crestFactorDb));
        for (int b = 0; b < int (Band::Count); ++b) std::printf (" %s %.0f", kBandNames[size_t (b)], double (a.bandEnergyDb[size_t (b)]));
        std::printf ("  corr %.2f\n", double (a.stereoCorrelation));
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: dlive_mix_stems <stems folder> [seconds=30] [outdir] [gospel|worship] [offsetSeconds] [broadcast|livestream|recording]\n");
        return 2;
    }
    const juce::File folder { juce::String (argv[1]) };
    const float seconds = argc > 2 ? juce::String (argv[2]).getFloatValue() : 30.0f;
    const juce::File outDir = argc > 3 ? juce::File (juce::String (argv[3])) : folder.getChildFile ("dlive-out");
    const StyleProfileId profile = argc > 4 && juce::String (argv[4]).containsIgnoreCase ("worship") ? StyleProfileId::ModernWorship : StyleProfileId::ModernGospel;
    const double offsetArg = argc > 5 ? juce::String (argv[5]).getDoubleValue() : -1.0;
    MixPurpose purpose = MixPurpose::ChurchBroadcast;
    if (argc > 6)
    {
        const juce::String p (argv[6]);
        if (p.containsIgnoreCase ("stream")) purpose = MixPurpose::Livestream;
        else if (p.containsIgnoreCase ("record")) purpose = MixPurpose::LiveRecording;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::Array<juce::File> files = folder.findChildFiles (juce::File::findFiles, false, "*.aif;*.aiff;*.wav;*.flac");
    files.sort();
    if (files.isEmpty()) { std::printf ("no audio files in %s\n", folder.getFullPathName().toRawUTF8()); return 1; }

    // ---- 1. Open every stem, decide what it is ----
    struct Open { juce::File file; std::unique_ptr<juce::AudioFormatReader> reader; ChannelRole role; };
    std::vector<Open> open;
    double sr = 0.0;
    juce::int64 length = 0;
    for (const auto& f : files)
    {
        ChannelRole role;
        if (! guessRole (f.getFileNameWithoutExtension(), role)) { std::printf ("skipping %s (no source guessed from the name)\n", f.getFileName().toRawUTF8()); continue; }
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr) { std::printf ("cannot read %s\n", f.getFileName().toRawUTF8()); continue; }
        if (sr == 0.0) sr = reader->sampleRate;
        if (std::fabs (reader->sampleRate - sr) > 1.0) { std::printf ("skipping %s (sample rate %.0f, session is %.0f)\n", f.getFileName().toRawUTF8(), reader->sampleRate, sr); continue; }
        length = std::max (length, reader->lengthInSamples);
        open.push_back ({ f, std::move (reader), role });
    }
    if (open.empty()) { std::printf ("nothing to mix\n"); return 1; }

    // ---- 2. Find the window: the first second where most of the band is playing ----
    // Per second, count the stems above -40 dBFS; print the timeline for the first minutes and start at the
    // earliest second that reaches 70 % of the busiest second (so a spoken intro on one mic does not win).
    juce::int64 start = 0;
    {
        const int step = int (sr);
        const int scanSeconds = int (std::min<juce::int64> (length / step, 600));
        std::vector<int> active (size_t (scanSeconds), 0);
        std::vector<int> leadActive (size_t (scanSeconds), 0);   // the lead vocal weighs more: a mix window needs the singer
        juce::AudioBuffer<float> buf (2, step);
        std::printf ("stems (seconds above -40 dBFS in the first %d s):\n", scanSeconds);
        for (auto& o : open)
        {
            const int ch = std::min (2, int (o.reader->numChannels));
            int secondsActive = 0;
            float peak = 0.0f;
            for (int sec = 0; sec < scanSeconds; ++sec)
            {
                const juce::int64 pos = juce::int64 (sec) * step;
                if (pos + step > o.reader->lengthInSamples) break;
                buf.setSize (ch, step, false, false, true);
                o.reader->read (&buf, 0, step, pos, true, ch > 1);
                const float m = buf.getMagnitude (0, step);
                peak = std::max (peak, m);
                if (m > 0.01f) { ++active[size_t (sec)]; ++secondsActive; if (roleFamily (o.role) == RoleFamily::LeadVocal) ++leadActive[size_t (sec)]; }
            }
            std::printf ("  %-14s %-16s %4d s active, peak %6.1f dBFS\n", o.file.getFileNameWithoutExtension().upToLastOccurrenceOf ("#", false, false).trim().toRawUTF8(),
                         channelRoleName (o.role), secondsActive, double (gainToDb (peak)));
        }
        std::vector<int> score (size_t (scanSeconds), 0);
        int busiest = 0;
        for (size_t i = 0; i < score.size(); ++i) { score[i] = active[i] + 3 * leadActive[i]; busiest = std::max (busiest, score[i]); }
        std::printf ("band activity (stems above -40 dBFS per 10 s):");
        for (int sec = 0; sec < scanSeconds; sec += 10)
        {
            int m = 0;
            for (int k = sec; k < std::min (scanSeconds, sec + 10); ++k) m = std::max (m, active[size_t (k)]);
            if (sec % 100 == 0) std::printf ("\n  %4d s:", sec);
            std::printf (" %2d", m);
        }
        std::printf ("\n");
        if (offsetArg >= 0.0) start = juce::int64 (offsetArg * sr);
        else
        {
            bool haveLead = false;
            for (const auto& o : open) if (roleFamily (o.role) == RoleFamily::LeadVocal) haveLead = true;
            const int need = std::max (1, int (std::ceil (0.7 * busiest)));
            for (int sec = 0; sec < scanSeconds; ++sec)
                if (score[size_t (sec)] >= need && (! haveLead || leadActive[size_t (sec)] > 0)) { start = juce::int64 (sec) * step; break; }
        }
    }
    const int numSamples = int (std::min (juce::int64 (seconds * sr), length - start));
    std::printf ("DLIVE mix of %s\n%d stems, %.0f Hz, window %.1f s from %.1f s, %s, %s\n\n", folder.getFileName().toRawUTF8(), int (open.size()), sr,
                 double (numSamples) / sr, double (start) / sr, styleProfileName (profile), mixPurposeName (purpose));

    // ---- 3. Session: one input per mono stem, two per stereo stem ----
    MixSession session;
    session.name = folder.getFileName().toStdString();
    session.profile = profile;
    session.purpose = purpose;
    std::vector<Stem> stems;
    std::vector<juce::AudioBuffer<float>> device;   // device input channels
    int nextInput = 0;
    for (auto& o : open)
    {
        Stem s;
        s.name = o.file.getFileNameWithoutExtension().upToLastOccurrenceOf ("#", false, false).trim();
        s.role = o.role;
        s.channels = std::min (2, int (o.reader->numChannels));
        s.audio.setSize (s.channels, numSamples);
        s.audio.clear();
        const juce::int64 avail = std::max<juce::int64> (0, std::min<juce::int64> (numSamples, o.reader->lengthInSamples - start));
        if (avail > 0) o.reader->read (&s.audio, 0, int (avail), start, true, s.channels > 1);
        InputAssignment a;
        a.name = s.name.toStdString();
        a.role = s.role;
        a.inputA = nextInput++;
        a.inputB = s.channels == 2 ? nextInput++ : -1;
        session.inputs.push_back (a);
        std::printf ("  in %2d%s  %-14s -> %s\n", a.inputA + 1, a.inputB >= 0 ? juce::String ("/" + juce::String (a.inputB + 1)).toRawUTF8() : "   ", s.name.toRawUTF8(), channelRoleName (s.role));
        stems.push_back (std::move (s));
    }
    std::vector<const float*> inputs;
    for (const auto& s : stems)
        for (int ch = 0; ch < s.channels; ++ch) inputs.push_back (s.audio.getReadPointer (ch));

    // ---- 4. Engine + routing ----
    const int block = 128;
    MixEngine engine;
    engine.prepare (sr, block, session);
    std::printf ("\nROUTING (built automatically)\n%s\n", engine.getGraph().describe().c_str());

    OfflineCapture capture;
    capture.prepare (sr, engine.getGraph());
    engine.setTap (&capture);

    // RAW: what a flat console would give (no processing, unity faders, no returns).
    MixParameters raw = engine.getAppliedParameters();
    raw.bypassProcessing = true;
    engine.setParameters (raw);
    const auto rawMix = render (engine, inputs, numSamples, block, nullptr);

    // BEFORE: the baselines, while listening. This is TUNE MIX.
    const MixParameters before = startingPoint (session, engine.getGraph());
    engine.setParameters (before);
    const auto beforeMix = render (engine, inputs, numSamples, block, &capture);
    const auto listened = capture.finish();
    engine.setTap (nullptr);

    MixPlanContext ctx;
    ctx.session = session;
    ctx.graph = engine.getGraph();
    ctx.current = before;
    ctx.atCapture = before;
    ctx.capture = listened;
    const MixPlan plan = MixPlanner::plan (ctx);

    std::printf ("%s\n", plan.headline.c_str());
    for (const auto& n : plan.notes) std::printf ("  %s\n", n.c_str());

    std::printf ("\nSOURCES\n");
    for (const auto& sp : plan.strips)
    {
        const auto& a = listened.strips[size_t (sp.strip)];
        std::printf ("  %-14s %-16s %s  peak %5.1f  hits %5.1f  active %5.1f  event %5.1f (%d)  floor %6.1f  quiet %3.0f%%  fund %4.0f Hz  bpm %5.1f/%.2f  gain %+5.1f  fader %+5.1f dB\n", sp.name.c_str(), channelRoleName (sp.role),
                     sp.heard ? "heard  " : sp.faint ? "FAINT  " : "SILENT ", double (a.peakDb), double (a.hitLevelDb), double (a.activeRmsDb), double (a.eventLevelDb), a.eventCount, double (a.noiseFloorDb), double (a.silencePercent),
                     double (a.fundamentalHz), double (a.tempoBpm), double (a.tempoConfidence), double (sp.inputGainDb), double (sp.faderDb));
        if (! sp.heard) { for (const auto& item : sp.mixItems) std::printf ("      * %s\n", item.what.c_str()); continue; }
        std::printf ("      %s\n", sp.tune.headline.c_str());
        for (const auto& item : sp.tune.report.items)
            if (! item.changes.empty() || item.kind == Recommendation::Kind::CaptureGain) std::printf ("      - %s\n", item.what.c_str());
        for (const auto& item : sp.mixItems) std::printf ("      * %s\n", item.what.c_str());
    }

    std::printf ("\nMIX RELATIONSHIPS\n");
    for (const auto& r : plan.relationships) std::printf ("  - %s\n      %s\n", r.what.c_str(), r.why.c_str());

    std::printf ("\nBUSES\n");
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        const auto& bp = plan.buses[size_t (b)];
        if (! bp.used) continue;
        std::printf ("  %-7s %s\n", mixBusName (MixBus (b)), bp.tune.valid ? bp.tune.headline.c_str() : "(not measured)");
        if (bp.tune.valid)
            for (const auto& item : bp.tune.report.items)
                if (! item.changes.empty()) std::printf ("      - %s\n", item.what.c_str());
    }

    // ---- 5. AFTER ----
    engine.setParameters (plan.proposed);
    const auto afterMix = render (engine, inputs, numSamples, block, nullptr);

    // RE-TUNE: the band plays again with the plan running; the second listen refines what one pass could
    // only predict (compressors fitted by the first pass change the processed levels the faders were set from).
    engine.setTap (&capture);
    const auto afterListenMix = render (engine, inputs, numSamples, block, &capture);
    const auto listenedAgain = capture.finish();
    engine.setTap (nullptr);
    MixPlanContext ctx2;
    ctx2.session = session;
    ctx2.graph = engine.getGraph();
    ctx2.current = plan.proposed;
    ctx2.atCapture = plan.proposed;
    ctx2.capture = listenedAgain;
    const MixPlan retune = MixPlanner::plan (ctx2);
    std::printf ("\nRE-TUNE (new listen with the plan running): %s\n", retune.headline.c_str());
    for (const auto& n : retune.notes) std::printf ("  %s\n", n.c_str());
    for (const auto& sp : retune.strips)
        for (const auto& item : sp.mixItems)
            if (item.kind == Recommendation::Kind::MixGain || item.kind == Recommendation::Kind::CaptureGain) std::printf ("  * %s\n", item.what.c_str());
    for (const auto& r : retune.relationships) if (! r.changes.empty() || r.kind == Recommendation::Kind::MixGain) std::printf ("  - %s\n", r.what.c_str());

    // How well the first pass predicted the processed peaks it fitted the faders from (the second listen measures them).
    std::printf ("\nPREDICTION CHECK (first pass predicted vs second listen measured, processed pre-fader peak)\n");
    for (const auto& sp : plan.strips)
    {
        if (! sp.balanced) continue;
        const int i = sp.strip;
        const auto& p = plan.proposed.strips[size_t (i)];
        const float predicted = MixPlanner::predictedProcessedPeakDb (ctx, i, p);
        const float measured = listenedAgain.processed[size_t (i)].peakDb;
        const float in = listenedAgain.strips[size_t (i)].peakDb;
        const float staticReduction = p.channel.compEnabled ? in - Compressor::computeGain (in, p.channel.compThresholdDb, p.channel.compRatio, p.channel.compKneeDb) : 0.0f;
        const float actualReduction = in - measured;
        // What the listen itself saw through the chain that ran then: the prediction starts from this number,
        // so when an error is large this says whether the model moved wrongly or started from the wrong place.
        std::printf ("  %-14s listen raw %6.1f -> chain %6.1f | in %6.1f  predicted %6.1f  measured %6.1f  error %+5.1f | comp %s%.1f:1 thr %5.1f att %4.1f ms  static GR %4.1f  actual GR %4.1f  share %.2f\n",
                     sp.name.c_str(), double (listened.strips[size_t (i)].peakDb), double (listened.processed[size_t (i)].peakDb),
                     double (in), double (predicted), double (measured), double (predicted - measured),
                     p.channel.compEnabled ? "" : "(off) ", double (p.channel.compRatio), double (p.channel.compThresholdDb), double (p.channel.compAttackMs),
                     double (staticReduction), double (actualReduction), staticReduction > 1.0f ? double (actualReduction / staticReduction) : 0.0);
    }
    std::printf ("  bus inputs (RMS): predicted shift vs measured, listen -> plan\n");
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        const auto& a1 = listened.buses[size_t (b)];
        const auto& a2 = listenedAgain.buses[size_t (b)];
        if (! a1.valid || ! a2.valid) continue;
        std::printf ("  %-14s in rms %6.1f -> %6.1f (%+5.1f)  predicted %+5.1f  | peak %6.1f -> %6.1f\n", mixBusName (MixBus (b)), double (a1.rmsDb), double (a2.rmsDb),
                     double (a2.rmsDb - a1.rmsDb), double (plan.buses[size_t (b)].predictedInShiftDb), double (a1.peakDb), double (a2.peakDb));
    }
    std::printf ("  master out: %6.1f LUFS -> %6.1f LUFS (%+5.1f); master trim %+5.1f -> %+5.1f\n", double (listened.masterOutput.loudnessLufs), double (listenedAgain.masterOutput.loudnessLufs),
                 double (listenedAgain.masterOutput.loudnessLufs - listened.masterOutput.loudnessLufs), double (before.master().channel.outputTrimDb), double (plan.proposed.master().channel.outputTrimDb));
    {
        const auto& m0 = before.master().channel; const auto& m1 = plan.proposed.master().channel;
        std::printf ("  master comp at listen: %s %.1f:1 thr %.1f knee %.1f makeup %.1f mix %.2f att %.0f rel %.0f | plan: %s %.1f:1 thr %.1f knee %.1f makeup %.1f mix %.2f att %.0f rel %.0f | limiter %s ceiling %.1f\n",
                     m0.compEnabled ? "on" : "off", double (m0.compRatio), double (m0.compThresholdDb), double (m0.compKneeDb), double (m0.compMakeupDb), double (m0.compMix), double (m0.compAttackMs), double (m0.compReleaseMs),
                     m1.compEnabled ? "on" : "off", double (m1.compRatio), double (m1.compThresholdDb), double (m1.compKneeDb), double (m1.compMakeupDb), double (m1.compMix), double (m1.compAttackMs), double (m1.compReleaseMs),
                     m1.limiterEnabled ? "on" : "off", double (m1.limiterCeilingDb));
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            const auto& c0 = before.buses[size_t (b)].channel; const auto& c1 = plan.proposed.buses[size_t (b)].channel;
            std::printf ("  %-7s comp at listen: %s %.1f:1 thr %.1f att %.0f rel %.0f | plan: %s %.1f:1 thr %.1f att %.0f rel %.0f\n", mixBusName (MixBus (b)),
                         c0.compEnabled ? "on" : "off", double (c0.compRatio), double (c0.compThresholdDb), double (c0.compAttackMs), double (c0.compReleaseMs),
                         c1.compEnabled ? "on" : "off", double (c1.compRatio), double (c1.compThresholdDb), double (c1.compAttackMs), double (c1.compReleaseMs));
        }
    }
    engine.setParameters (retune.proposed);
    const auto retunedMix = render (engine, inputs, numSamples, block, nullptr);

    outDir.createDirectory();
    writeWav (outDir.getChildFile ("raw.wav"), rawMix, sr);
    writeWav (outDir.getChildFile ("before.wav"), beforeMix, sr);
    writeWav (outDir.getChildFile ("after.wav"), afterMix, sr);
    writeWav (outDir.getChildFile ("after-retuned.wav"), retunedMix, sr);

    std::printf ("\nOUTPUT (%s)\n", outDir.getFullPathName().toRawUTF8());
    printMeasurement ("raw", measure (rawMix, sr));
    printMeasurement ("before", measure (beforeMix, sr));
    printMeasurement ("after", measure (afterMix, sr));
    printMeasurement ("retuned", measure (retunedMix, sr));
    std::printf ("  engine: %d strips, latency %d samples (offline: the listen runs inline on the render thread, so timings here are not the live cost)\n",
                 engine.getNumStrips(), engine.getLatencySamples());

    // ---- 6. The same listen again: nothing may change ----
    ctx.current = plan.proposed;
    const MixPlan again = MixPlanner::plan (ctx);
    std::printf ("\nRE-TUNE on the same listen: %s (%d parameters, %d faders, %d sends, %d gains)\n", again.headline.c_str(), again.parametersChanged, again.fadersChanged, again.sendsChanged, again.gainsChanged);
    if (! again.noChangeRequired)
    {
        for (const auto& sp : again.strips)
            for (const auto& c : diffParameters (again.before.strips[size_t (sp.strip)].channel, again.proposed.strips[size_t (sp.strip)].channel))
                std::printf ("  %s: %s -> %.3g\n", sp.name.c_str(), c.paramId.c_str(), double (c.value));
        for (int b = 0; b < int (MixBus::Count); ++b)
            for (const auto& c : diffParameters (again.before.buses[size_t (b)].channel, again.proposed.buses[size_t (b)].channel))
                std::printf ("  %s bus: %s -> %.3g\n", mixBusName (MixBus (b)), c.paramId.c_str(), double (c.value));
    }
    return again.noChangeRequired ? 0 : 3;
}
