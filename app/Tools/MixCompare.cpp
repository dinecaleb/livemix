// DLIVE mix comparison: a session an engineer finished by hand against a fresh TUNE MIX of the
// same audio.
//
//   dlive_mix_compare <session.dlive.json> [startSeconds=0] [seconds=40] [--link-pairs]
//
// The session's own clips are played for the window (stereo pairs, joined L/R stems and all),
// the engine is built from the session's own assignments, and the window is rendered twice:
//
//   HAND  - the session's kept mix: what the engineer ended up with.
//   TUNE  - the profile's starting point, listened to and planned by MixPlanner: what one
//           TUNE MIX would have given them.
//
// Each render is listened to, and for every channel the tool prints where it lands - its
// processed level plus its fader plus its group's fader - relative to the lead, in both, and
// the difference. The groups are compared the same way, and so are the two masters' loudness
// and tone. What the engineer had to change by hand is the column that is not zero.
//
// --link-pairs links every pair of mono channels named as a left and a right ("keys1L",
// "keys1r") before TUNE runs, the way an engineer would on the console.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Mix/MixEngine.h"
#include "Mix/OfflineCapture.h"
#include "Mix/MixPlanner.h"
#include "Analysis/AnalysisAccumulator.h"
#include "native/SessionStore.h"
#include "native/ClipSource.h"
#include "native/SampleLibrary.h"
#include "Core/DbUtils.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace livemix;

namespace
{
    struct Rendered
    {
        juce::AudioBuffer<float> out;
        MixCapture::Result listen;
    };

    Rendered render (MixEngine& engine, OfflineCapture& capture, const std::vector<const float*>& inputs, int numSamples, int block)
    {
        Rendered r;
        r.out.setSize (2, numSamples);
        r.out.clear();
        std::vector<const float*> in (inputs.size());
        float* op[2];
        engine.reset();
        engine.setTap (&capture);
        capture.start();
        for (int pos = 0; pos < numSamples; pos += block)
        {
            const int n = std::min (block, numSamples - pos);
            for (size_t c = 0; c < inputs.size(); ++c) in[c] = inputs[c] + pos;
            op[0] = r.out.getWritePointer (0) + pos;
            op[1] = r.out.getWritePointer (1) + pos;
            engine.process (in.data(), int (in.size()), op, 2, n);
        }
        r.listen = capture.finish();
        engine.setTap (nullptr);
        return r;
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

    int sideOf (std::string name)
    {
        for (auto& c : name) c = char (std::tolower ((unsigned char) c));
        while (! name.empty() && (name.back() == ' ' || std::isdigit ((unsigned char) name.back()))) name.pop_back();
        if (! name.empty() && name.back() == 'l') return -1;
        if (! name.empty() && name.back() == 'r') return 1;
        return 0;
    }
    std::string baseOf (std::string name)
    {
        for (auto& c : name) c = char (std::tolower ((unsigned char) c));
        while (! name.empty() && name.back() == ' ') name.pop_back();
        if (! name.empty() && (name.back() == 'l' || name.back() == 'r')) name.pop_back();
        while (! name.empty() && name.back() == ' ') name.pop_back();
        return name;
    }

    // Where a channel lands: its processed level, its fader and its group's fader.
    float landsDb (const MixCapture::Result& listen, const MixParameters& p, const RoutingGraph& g, int i)
    {
        if (i >= int (listen.processed.size()) || ! listen.processed[size_t (i)].valid) return -120.0f;
        const float rms = listen.processed[size_t (i)].rmsDb;
        if (rms <= -100.0f || p.strips[size_t (i)].mute) return -120.0f;
        return rms + p.strips[size_t (i)].faderDb + p.buses[size_t (g.strips[size_t (i)].bus)].faderDb;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: dlive_mix_compare <session.dlive.json> [startSeconds=0] [seconds=40] [--link-pairs]\n");
        return 2;
    }
    const juce::File file { juce::String (argv[1]) };
    const double start = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 0.0;
    const double seconds = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 40.0;
    bool linkPairs = false;
    for (int i = 1; i < argc; ++i) if (juce::String (argv[i]) == "--link-pairs") linkPairs = true;

    SessionStore::Document doc;
    if (! SessionStore::load (file, doc) || ! doc.hasMix)
    {
        std::printf ("cannot read a mixed session from %s\n", file.getFullPathName().toRawUTF8());
        return 2;
    }
    const MixSession& session = doc.session;
    const double sr = doc.project.sampleRate > 0.0 ? doc.project.sampleRate : 48000.0;
    const int numSamples = int (seconds * sr);
    const juce::int64 from = juce::int64 (start * sr);

    // ---- the session's own audio, input by input ----
    std::vector<ClipSource::Track> tracks (session.inputs.size());
    int channels = 0;
    for (size_t i = 0; i < session.inputs.size(); ++i)
    {
        const auto& in = session.inputs[i];
        tracks[i].channels = in.isStereo() ? 2 : 1;
        channels = std::max ({ channels, in.inputA + 1, in.inputB + 1 });
        if (i >= doc.project.tracks.size()) continue;
        for (auto clip : doc.project.tracks[i].clips)
        {
            const auto f = doc.project.fileFor (clip);
            if (! f.existsAsFile()) { std::printf ("  missing: %s\n", f.getFullPathName().toRawUTF8()); continue; }
            clip.file = f.getFullPathName();
            const auto right = doc.project.rightFileFor (clip);
            clip.fileRight = right.existsAsFile() ? right.getFullPathName() : juce::String();
            tracks[i].clips.push_back (clip);
        }
    }
    std::vector<std::vector<float>> audio (size_t (channels), std::vector<float> (size_t (numSamples), 0.0f));
    {
        ClipSource source;
        const int chunk = 8192;
        source.prepare (sr, chunk, tracks);
        for (int pos = 0; pos < numSamples; pos += chunk)
        {
            const int n = std::min (chunk, numSamples - pos);
            source.read (from + pos, n);
            for (size_t i = 0; i < session.inputs.size(); ++i)
            {
                const auto& in = session.inputs[i];
                const float* a = source.channel (int (i), 0);
                const float* b = source.channel (int (i), in.isStereo() ? 1 : 0);
                if (in.inputA >= 0 && a != nullptr) std::copy (a, a + n, audio[size_t (in.inputA)].begin() + pos);
                if (in.inputB >= 0 && b != nullptr) std::copy (b, b + n, audio[size_t (in.inputB)].begin() + pos);
            }
        }
    }
    std::vector<const float*> inputs;
    for (auto& c : audio) inputs.push_back (c.data());

    // ---- the engine, the samples the hand mix chose ----
    const int block = 128;
    MixEngine engine;
    engine.prepare (sr, block, session);
    const auto& graph = engine.getGraph();
    SampleLibrary samples;
    samples.load();
    engine.setSampleBanks (samples.table());
    MixParameters hand = doc.mix;
    for (int i = 0; i < hand.numStrips && i < graph.numStrips(); ++i)
    {
        const auto& choice = doc.samples[size_t (i)];
        if (! choice.set()) continue;
        for (int f = 0; f < int (RoleFamily::Count); ++f)
            if (choice.family == sampleFamilyFolder (RoleFamily (f)))
            {
                const int slot = samples.slotFor (RoleFamily (f), choice.name, choice.user, choice.path);
                if (slot >= 0) hand.strips[size_t (i)].channel.replaceSound = slot;
                else hand.strips[size_t (i)].channel.replaceEnabled = false;
            }
    }
    OfflineCapture capture;
    capture.prepare (sr, graph);

    // ---- TUNE: the starting point, listened to and planned ----
    MixParameters start0 = startingPoint (session, graph);
    if (linkPairs)
    {
        int group = 1;
        for (int i = 0; i < graph.numStrips(); ++i)
            for (int k = i + 1; k < graph.numStrips(); ++k)
            {
                const auto& a = graph.strips[size_t (i)];
                const auto& b = graph.strips[size_t (k)];
                if (a.inputB >= 0 || b.inputB >= 0 || roleFamily (a.role) != roleFamily (b.role)) continue;
                const int sa = sideOf (a.name), sb = sideOf (b.name);
                if (sa == 0 || sb == 0 || sa == sb || baseOf (a.name) != baseOf (b.name)) continue;
                if (start0.strips[size_t (i)].linkGroup != 0 || start0.strips[size_t (k)].linkGroup != 0) continue;
                start0.strips[size_t (i)].linkGroup = start0.strips[size_t (k)].linkGroup = group++;
                start0.strips[size_t (i)].pan = float (sa);
                start0.strips[size_t (k)].pan = float (sb);
                std::printf ("  linked %s + %s\n", a.name.c_str(), b.name.c_str());
            }
    }
    engine.setParameters (start0);
    const auto listened = render (engine, capture, inputs, numSamples, block);
    MixPlanContext ctx;
    ctx.session = session;
    ctx.graph = graph;
    ctx.current = start0;
    ctx.atCapture = start0;
    ctx.capture = listened.listen;
    const MixPlan plan = MixPlanner::plan (ctx);
    std::printf ("%s  (%s, %.0f s from %.0f s)\n", plan.headline.c_str(), styleProfileName (session.profile), seconds, start);

    for (const auto& r : plan.relationships)
        if (r.kind == Recommendation::Kind::MixGain) std::printf ("  - %s\n", r.what.c_str());
    for (int b = 0; b < int (MixBus::Count); ++b)
        if (graph.busUsed[size_t (b)])
            std::printf ("  group %-8s fader tune %+5.1f  hand %+5.1f\n", mixBusName (MixBus (b)),
                         double (plan.proposed.buses[size_t (b)].faderDb), double (hand.buses[size_t (b)].faderDb));
    for (int i = 0; i < graph.numStrips(); ++i)
    {
        const auto& t = plan.proposed.strips[size_t (i)];
        const auto& h = hand.strips[size_t (i)];
        std::printf ("  %-24.24s gain %+5.1f/%+5.1f  fader %+5.1f/%+5.1f  replace %d/%d gain %+5.1f/%+5.1f\n", graph.strips[size_t (i)].name.c_str(),
                     double (t.inputGainDb), double (h.inputGainDb), double (t.faderDb), double (h.faderDb),
                     int (t.channel.replaceEnabled), int (h.channel.replaceEnabled), double (t.channel.replaceGainDb), double (h.channel.replaceGainDb));
    }

    engine.setParameters (plan.proposed);
    const auto tune = render (engine, capture, inputs, numSamples, block);
    engine.setParameters (hand);
    const auto handR = render (engine, capture, inputs, numSamples, block);

    // ---- where everything lands, against the lead ----
    int lead = -1;
    for (int i = 0; i < graph.numStrips(); ++i)
        if (roleFamily (graph.strips[size_t (i)].role) == RoleFamily::LeadVocal
            && (lead < 0 || landsDb (handR.listen, hand, graph, i) > landsDb (handR.listen, hand, graph, lead))) lead = i;
    const float leadTune = lead >= 0 ? landsDb (tune.listen, plan.proposed, graph, lead) : 0.0f;
    const float leadHand = lead >= 0 ? landsDb (handR.listen, hand, graph, lead) : 0.0f;
    std::printf ("\nCHANNELS  where each lands against the lead (dB): TUNE, HAND, and what the hand mix changed\n");
    std::printf ("  %-24s %-15s %7s %7s %7s\n", "channel", "source", "tune", "hand", "change");
    double sq = 0.0;
    int counted = 0;
    for (int i = 0; i < graph.numStrips(); ++i)
    {
        const auto& s = graph.strips[size_t (i)];
        const float t = landsDb (tune.listen, plan.proposed, graph, i), h = landsDb (handR.listen, hand, graph, i);
        if (t <= -100.0f && h <= -100.0f) continue;
        const bool both = t > -100.0f && h > -100.0f;
        std::printf ("  %-24.24s %-15.15s %7.1f %7.1f %7s%s\n", s.name.c_str(), channelRoleName (s.role),
                     double (t > -100.0f ? t - leadTune : -99.0f), double (h > -100.0f ? h - leadHand : -99.0f),
                     both ? juce::String ((h - leadHand) - (t - leadTune), 1).toRawUTF8() : "-",
                     plan.proposed.strips[size_t (i)].linkGroup != 0 ? "  (linked)" : "");
        if (both && i != lead) { const double d = double ((h - leadHand) - (t - leadTune)); sq += d * d; ++counted; }
    }
    std::printf ("  RMS difference across channels: %.1f dB over %d channels\n", counted > 0 ? std::sqrt (sq / counted) : 0.0, counted);

    std::printf ("\nGROUPS  what each group received plus its fader, against the lead group (dB)\n");
    auto groupLands = [&] (const Rendered& r, const MixParameters& p, MixBus b)
    {
        const auto& a = r.listen.buses[size_t (b)];
        return a.valid && a.rmsDb > -100.0f ? a.rmsDb + p.buses[size_t (b)].faderDb : -120.0f;
    };
    const float lt = groupLands (tune, plan.proposed, MixBus::Lead), lh = groupLands (handR, hand, MixBus::Lead);
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        if (! graph.busUsed[size_t (b)]) continue;
        const float t = groupLands (tune, plan.proposed, MixBus (b)), h = groupLands (handR, hand, MixBus (b));
        if (t <= -100.0f && h <= -100.0f) continue;
        std::printf ("  %-10s %7.1f %7.1f %7.1f\n", mixBusName (MixBus (b)), double (t - lt), double (h - lh), double ((h - lh) - (t - lt)));
    }

    const auto mt = measure (tune.out, sr), mh = measure (handR.out, sr);
    std::printf ("\nMASTER         LUFS   crest  corr  ");
    for (int b = 0; b < int (Band::Count); ++b) std::printf (" %9s", kBandNames[size_t (b)]);
    std::printf ("\n");
    auto row = [] (const char* label, const AnalysisResult& m)
    {
        std::printf ("  %-10s %6.1f  %5.1f  %4.2f  ", label, double (m.loudnessLufs), double (m.crestFactorDb), double (m.stereoCorrelation));
        for (int b = 0; b < int (Band::Count); ++b) std::printf (" %9.1f", double (m.bandEnergyDb[size_t (b)]));
        std::printf ("\n");
    };
    row ("tune", mt);
    row ("hand", mh);
    std::printf ("  %-10s %6.1f  %5.1f  %4.2f  ", "change", double (mh.loudnessLufs - mt.loudnessLufs), double (mh.crestFactorDb - mt.crestFactorDb),
                 double (mh.stereoCorrelation - mt.stereoCorrelation));
    for (int b = 0; b < int (Band::Count); ++b) std::printf (" %9.1f", double (mh.bandEnergyDb[size_t (b)] - mt.bandEnergyDb[size_t (b)]));
    std::printf ("\n");
    return 0;
}
