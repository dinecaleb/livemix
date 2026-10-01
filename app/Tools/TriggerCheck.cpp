// DINE sample replacement, Phase 0: what the trigger does on real drum microphones.
//
//   dine_trigger_check <folder of takes> [seconds=60] [offsetSeconds=30] [name filter]
//
// Every file whose name says kick, snare or tom is read for the window, measured the way a
// listen measures it, fitted the way TUNE fits the stage (the threshold between the bleed and
// the hits, the level at the hits, the profile's band, mask and rise), and then run through
// the detector exactly as the engine runs it. What comes out, per drum: the fitted numbers,
// how many hits, how many a second, where their levels sit over the threshold, the shortest
// gap between two, and how many landed within two milliseconds of a kick or snare hit - the
// ones a tom's veto would hold back. No sample is played and nothing is written: this is the
// table the profile's numbers are checked against.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Analysis/AnalysisAccumulator.h"
#include "Core/DbUtils.h"
#include "DSP/SampleBank.h"
#include "DSP/SampleTrigger.h"
#include "Profiles/StyleProfile.h"
#include "Tune/TuneEngine.h"
#include "native/StemNames.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

using namespace livemix;

namespace
{
    struct Take
    {
        juce::String name;
        ChannelRole role = ChannelRole::KickIn;
        std::vector<float> mono;
        ChannelParameters fitted;
        std::vector<long long> hits;
        std::vector<float> levels;
        AnalysisResult analysis;
    };

    bool readWindow (const juce::File& f, double& sr, double seconds, double offset, std::vector<float>& mono)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr) return false;
        sr = reader->sampleRate;
        const juce::int64 start = juce::int64 (offset * sr);
        const int length = int (std::min<juce::int64> (juce::int64 (seconds * sr), reader->lengthInSamples - start));
        if (length <= 0) return false;
        juce::AudioBuffer<float> buffer (int (reader->numChannels), length);
        if (! reader->read (&buffer, 0, length, start, true, true)) return false;
        mono.assign (size_t (length), 0.0f);
        const float scale = 1.0f / float (buffer.getNumChannels());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* x = buffer.getReadPointer (ch);
            for (int i = 0; i < length; ++i) mono[size_t (i)] += x[i] * scale;
        }
        return true;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: dine_trigger_check <folder of takes> [seconds=60] [offsetSeconds=30] [name filter]\n");
        return 2;
    }
    const juce::File folder { juce::String (argv[1]) };
    const double seconds = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 60.0;
    const double offset = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 30.0;
    const juce::String filter = argc > 4 ? juce::String (argv[4]) : juce::String();

    std::vector<Take> takes;
    double sr = 48000.0;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac"))
    {
        if (filter.isNotEmpty() && ! f.getFileName().contains (filter)) continue;
        ChannelRole role;
        if (! StemNames::guessRole (f.getFileNameWithoutExtension(), role)) continue;
        if (! sampleReplacementAppropriate (roleFamily (role))) continue;
        Take t;
        t.name = f.getFileNameWithoutExtension();
        t.role = role;
        double fileSr = 48000.0;
        if (! readWindow (f, fileSr, seconds, offset, t.mono)) { std::printf ("cannot read %s\n", f.getFileName().toRawUTF8()); continue; }
        sr = fileSr;
        takes.push_back (std::move (t));
    }
    if (takes.empty()) { std::printf ("no kick, snare or tom takes in %s\n", folder.getFullPathName().toRawUTF8()); return 1; }

    std::printf ("Sample trigger check: %s, %.0f s from %.0f s, %.0f Hz, Modern Gospel\n\n", folder.getFileName().toRawUTF8(), seconds, offset, sr);

    for (auto& t : takes)
    {
        // The listen, then TUNE's fit of the stage on the profile's baseline for the role.
        AnalysisAccumulator acc;
        acc.prepare (sr, 1);
        float* ptr = t.mono.data();
        AudioBlockView v { &ptr, 1, int (t.mono.size()) };
        acc.consume (v);
        t.analysis = acc.finalise();

        TuneContext ctx;
        ctx.analysis = t.analysis;
        ctx.role = t.role;
        ctx.profile = StyleProfileId::ModernGospel;
        ctx.current = StyleProfile::baseline (t.role, StyleProfileId::ModernGospel);
        const auto result = TuneEngine::tune (ctx);
        t.fitted = result.valid ? result.proposed : ctx.current;
        const char* fittedBy = "baseline";
        for (const auto& item : result.report.items)
            if (item.what.find ("Sample trigger fitted") != std::string::npos) fittedBy = "TUNE";

        // The detector, exactly as the engine runs it (the trim is the chain's; the listen is pre-trim).
        SampleTrigger trigger;
        trigger.prepare (sr);
        SampleTrigger::Params p;
        p.enabled = true;
        p.thresholdDb = t.fitted.replaceThresholdDb;
        p.riseDb = t.fitted.replaceRiseDb;
        p.hpfHz = t.fitted.replaceDetHpfHz;
        p.lpfHz = t.fitted.replaceDetLpfHz;
        p.maskMs = t.fitted.replaceMaskMs;
        trigger.setParams (p);
        const float trim = dbToGain (t.fitted.inputTrimDb);
        std::vector<float> trimmed (t.mono);
        for (auto& x : trimmed) x *= trim;
        SampleTrigger::Hit hits[SampleTrigger::kMaxHits];
        constexpr int block = 128;
        for (size_t i = 0; i + block <= trimmed.size(); i += block)
        {
            const int n = trigger.process (trimmed.data() + i, block, hits, SampleTrigger::kMaxHits);
            for (int h = 0; h < n; ++h) { t.hits.push_back ((long long) i + hits[h].offset); t.levels.push_back (hits[h].levelDb); }
        }

        std::printf ("%-14s %-11s  listen: hits %6.1f  events %6.1f  bleed %6.1f  floor %6.1f dBFS  f0 %5.1f Hz  trim %+.1f dB\n",
                     t.name.toRawUTF8(), channelRoleName (t.role), double (t.analysis.hitLevelDb), double (t.analysis.eventLevelDb),
                     double (t.analysis.bleedLevelDb), double (t.analysis.noiseFloorDb), double (t.analysis.fundamentalHz), double (t.fitted.inputTrimDb));
        std::printf ("%-14s fitted by %-8s threshold %6.1f dB  level %6.1f dB  band %4.0f-%5.0f Hz  rise %.0f dB  mask %.0f ms  drum %.1f Hz\n",
                     "", fittedBy, double (p.thresholdDb), double (t.fitted.replaceGainDb), double (p.hpfHz), double (p.lpfHz), double (p.riseDb), double (p.maskMs), double (t.fitted.replaceDrumHz));
        if (t.hits.empty()) { std::printf ("%-14s no hits\n\n", ""); continue; }
        auto sorted = t.levels;
        std::sort (sorted.begin(), sorted.end());
        long long minGap = -1;
        for (size_t i = 1; i < t.hits.size(); ++i) { const long long g = t.hits[i] - t.hits[i - 1]; if (minGap < 0 || g < minGap) minGap = g; }
        std::printf ("%-14s hits %5zu  %5.2f /s  levels over threshold: min %+5.1f  median %+5.1f  max %+5.1f dB  shortest gap %.0f ms  musical peak %.1f dBFS\n",
                     "", t.hits.size(), double (t.hits.size()) / seconds,
                     double (sorted.front() - p.thresholdDb), double (sorted[sorted.size() / 2] - p.thresholdDb), double (sorted.back() - p.thresholdDb),
                     minGap < 0 ? 0.0 : 1000.0 * double (minGap) / sr, double (t.analysis.musicalPeakDb));
        // Where the hits sit, 3 dB a bin from the threshold up: two clusters are bleed and drum.
        std::printf ("%-14s levels:", "");
        for (int bin = 0; bin < 12; ++bin)
        {
            int count = 0;
            for (float l : t.levels) { const float over = l - p.thresholdDb; if (over >= bin * 3.0f && (over < (bin + 1) * 3.0f || bin == 11)) ++count; }
            std::printf (" %+3d:%-3d", bin * 3, count);
        }
        std::printf ("\n\n");
    }

    // Coincidences: a tom hit within two milliseconds of a kick or snare hit is what the veto
    // holds back when the tom's own hit is soft; how often does that happen here?
    const long long window = (long long) (0.002 * sr);
    for (const auto& tom : takes)
    {
        if (roleFamily (tom.role) != RoleFamily::Tom) continue;
        for (const auto& other : takes)
        {
            const auto f = roleFamily (other.role);
            if (f != RoleFamily::Kick && f != RoleFamily::Snare) continue;
            int near = 0;
            for (size_t i = 0; i < tom.hits.size(); ++i)
                for (long long h : other.hits) if (std::llabs (h - tom.hits[i]) <= window) { ++near; break; }
            // The veto as the engine runs it: against the loudest hit so far (falling 1 dB a second), 9 dB under.
            int vetoed = 0;
            float loudest = -120.0f;
            long long lastAt = 0;
            for (size_t i = 0; i < tom.hits.size(); ++i)
            {
                loudest -= float (tom.hits[i] - lastAt) / float (sr);
                lastAt = tom.hits[i];
                if (tom.levels[i] > loudest) loudest = tom.levels[i];
                bool close = false;
                for (long long h : other.hits) if (std::llabs (h - tom.hits[i]) <= window) { close = true; break; }
                if (close && loudest - tom.levels[i] > 9.0f) ++vetoed;
            }
            std::printf ("%-14s hits within 2 ms of a %-11s hit: %3d of %zu; held back by the veto (9 dB under the loudest so far): %d\n",
                         tom.name.toRawUTF8(), channelRoleName (other.role), near, tom.hits.size(), vetoed);
        }
    }
    return 0;
}
