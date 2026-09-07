// Offline Tune check on recorded stems: reads an audio file, runs the analysis engine over
// a window of it (from the first loud moment), prints the measurements and every decision
// Standard Tune makes for the given source, then re-tunes to prove idempotence.
//   livemix_tune_stems <source> <file> [seconds=12] [profile=gospel|worship] [offsetSeconds]
// <source> is a source name ("Lead Vocal", "Speech", "Piano", "Kick In", "Livestream" ...), case-insensitive.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Analysis/AnalysisEngine.h"
#include "Tune/TuneEngine.h"
#include "Profiles/StyleProfile.h"
#include "Core/ProductDefinition.h"
#include <cstdio>
#include <thread>
#include <chrono>

using namespace livemix;

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: livemix_tune_stems <source> <file> [seconds=12] [gospel|worship] [offsetSeconds]\n");
        return 2;
    }
    const juce::String roleName = juce::String (argv[1]).trim();
    ChannelRole role = ChannelRole::Count;
    for (int i = 0; i < int (ChannelRole::Count); ++i)
        if (juce::String (channelRoleName (ChannelRole (i))).equalsIgnoreCase (roleName)) role = ChannelRole (i);
    if (role == ChannelRole::Count)
    {
        std::printf ("unknown source '%s'. Sources:", argv[1]);
        for (int i = 0; i < int (ChannelRole::Count); ++i) std::printf (" \"%s\"", channelRoleName (ChannelRole (i)));
        std::printf ("\n");
        return 2;
    }
    const float seconds = argc > 3 ? juce::String (argv[3]).getFloatValue() : 12.0f;
    const StyleProfileId profile = argc > 4 && juce::String (argv[4]).containsIgnoreCase ("worship") ? StyleProfileId::ModernWorship : StyleProfileId::ModernGospel;
    const double offsetArg = argc > 5 ? juce::String (argv[5]).getDoubleValue() : -1.0;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (argv[2])));
    if (reader == nullptr) { std::printf ("cannot read %s\n", argv[2]); return 1; }
    const double sr = reader->sampleRate;
    const int channels = juce::jmin (2, int (reader->numChannels));
    std::printf ("%s: %.0f Hz, %d ch, %.1f s\n", argv[2], sr, int (reader->numChannels), double (reader->lengthInSamples) / sr);

    // Find the first moment louder than -40 dBFS (skip leading silence) unless an offset was given.
    juce::int64 start = 0;
    if (offsetArg >= 0.0) start = juce::int64 (offsetArg * sr);
    else
    {
        juce::AudioBuffer<float> scan (channels, 4096);
        for (juce::int64 pos = 0; pos + 4096 < reader->lengthInSamples; pos += 4096)
        {
            reader->read (&scan, 0, 4096, pos, true, channels > 1);
            if (scan.getMagnitude (0, 4096) > 0.01f) { start = pos; break; }
        }
    }
    const juce::int64 total = juce::jmin (juce::int64 (seconds * sr), reader->lengthInSamples - start);
    juce::AudioBuffer<float> audio (channels, int (total));
    reader->read (&audio, 0, int (total), start, true, channels > 1);
    std::printf ("window: %.1f s from %.1f s\n", double (total) / sr, double (start) / sr);

    AnalysisEngine engine;
    engine.prepare (sr, channels);
    engine.startCapture (float (double (total) / sr) - 0.2f);
    for (int i = 0; i < 200 && ! engine.isCapturing(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    const int block = 256;
    for (int i = 0; i + block <= int (total) && engine.getState() == AnalysisEngine::State::Capturing; i += block)
    {
        float* ptrs[2] = { audio.getWritePointer (0) + i, channels > 1 ? audio.getWritePointer (1) + i : nullptr };
        AudioBlockView v { ptrs, channels, block };
        engine.pushAudio (v);
        if ((i / block) % 16 == 0) std::this_thread::sleep_for (std::chrono::microseconds (700));
    }
    for (int i = 0; i < 20000 && (engine.getState() == AnalysisEngine::State::Capturing || engine.getState() == AnalysisEngine::State::Processing); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    const AnalysisResult a = engine.getResult();
    if (! a.valid) { std::printf ("analysis failed (state %d)\n", int (engine.getState())); return 1; }

    std::printf ("\nMEASUREMENTS\n  peak %.1f dBFS  rms %.1f  crest %.1f dB  phrase level %.1f  floor %.1f  silence %.0f %%  clips %d\n",
                 double (a.peakDb), double (a.rmsDb), double (a.crestFactorDb), double (a.hitLevelDb), double (a.noiseFloorDb), double (a.silencePercent), a.clipCount);
    std::printf ("  transients %.1f/s  rise %.1f dB  decay %.0f ms  bleed %.2f  fundamental %.0f Hz (%.1f dB)\n",
                 double (a.transientsPerSecond), double (a.meanTransientRiseDb), double (a.meanDecayMs), double (a.bleedEstimate), double (a.fundamentalHz), double (a.fundamentalLevelDb));
    std::printf ("  bands re total (dB):");
    for (int b = 0; b < int (Band::Count); ++b) std::printf (" %s %.0f", kBandNames[size_t (b)], double (a.bandEnergyDb[size_t (b)]));
    std::printf ("\n  sibilance %.1f dB (%.0f %% of loud frames)  correlation %.2f  balance %.1f dB  loudness %.1f LUFS  true peak %.1f dB\n",
                 double (a.sibilanceDb), double (a.sibilancePercent), double (a.stereoCorrelation), double (a.stereoBalanceDb), double (a.loudnessLufs), double (a.truePeakDb));
    for (const auto& r : a.resonances) std::printf ("  resonance %.0f Hz +%.1f dB\n", double (r.frequencyHz), double (r.prominenceDb));

    TuneContext ctx;
    ctx.analysis = a;
    ctx.role = role;
    ctx.profile = profile;
    ctx.current = StyleProfile::baseline (role, profile);
    const auto targets = StyleProfile::targets (role, profile);
    std::printf ("\nTUNE  %s / %s  (%s)\n", channelRoleName (role), styleProfileName (profile), targets.intent);
    auto r = TuneEngine::tune (ctx);
    std::printf ("%s  (%d parameters)\n", r.headline.c_str(), r.parametersChanged);
    for (int s = 0; s < int (TuneSection::Count); ++s)
        if (! r.sections[size_t (s)].summary.empty()) std::printf ("  %-9s %s\n", tuneSectionName (TuneSection (s)), r.sections[size_t (s)].summary.c_str());
    std::printf ("\nDECISIONS\n");
    for (const auto& i : r.report.items)
    {
        std::printf ("  [%s] %s\n      %s\n", confidenceName (i.confidence), i.what.c_str(), i.why.c_str());
        for (const auto& c : i.changes) std::printf ("      %s = %.3g\n", c.paramId.c_str(), double (c.value));
    }
    ctx.current = r.proposed;
    auto again = TuneEngine::tune (ctx);
    std::printf ("\nRE-TUNE: %s (%d parameters)\n", again.headline.c_str(), again.parametersChanged);
    return again.parametersChanged == 0 ? 0 : 3;
}
