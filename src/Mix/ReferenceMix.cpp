// REFERENCE MIX: aiming a live mix at a finished recording.
//
// The whole of the decision is here, and it is deliberately small: a reference moves the
// master's *targets*, and the master strategy that already exists does the work. Nothing
// here writes a parameter, so every bound, every explanation and the idempotency rule that
// re-tuning the same listen changes nothing all keep working unchanged.
#include "ReferenceMix.h"
#include "Profiles/MixProfileData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace
{
    float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    std::string num (const char* fmt, double v)
    {
        char buf[64];
        std::snprintf (buf, sizeof (buf), fmt, v);
        return buf;
    }

    // "2 dB" / "1.5 dB": whole numbers where the ear cannot hear the decimal anyway.
    std::string db (float v)
    {
        const float a = std::fabs (v);
        return a < 9.95f ? num ("%.1f dB", double (a)) : num ("%.0f dB", double (a));
    }

    // The side/mid energy ratio a measured correlation implies. Width scales the side, so
    // this is what turns "the reference is wider than us" into a number the width control takes.
    double sideRatio (float correlation)
    {
        const double r = double (clampf (correlation, -0.98f, 0.999f));
        return (1.0 - r) / (1.0 + r);
    }
}

namespace Reference
{

const char* bandWord (Band b)
{
    switch (b)
    {
        case Band::Sub:        return "sub";
        case Band::Low:        return "low end";
        case Band::LowMid:     return "low-mid";
        case Band::Mid:        return "midrange";
        case Band::UpperMid:   return "upper-mid";
        case Band::Presence:   return "presence";
        case Band::Brilliance: return "top end";
        case Band::Air:        return "air";
        default:               return "tone";
    }
}

ReferenceAdequacy adequacy (const AnalysisResult& a, StyleProfileId profile)
{
    const auto& B = MixProfile::referenceBounds (profile);
    ReferenceAdequacy r;
    if (! a.valid)
    {
        r.reason = "DLIVE could not read that file.";
        r.guidance = "Try a WAV, AIFF, MP3 or M4A of the finished song.";
        return r;
    }
    if (a.durationSeconds < B.minSeconds)
    {
        r.reason = "That is only " + num ("%.0f seconds", double (a.durationSeconds)) + " long.";
        r.guidance = "A reference needs at least " + num ("%.0f seconds", double (B.minSeconds))
                   + " of the song playing before its tone means anything. Use the whole track.";
        return r;
    }
    if (a.loudnessLufs < B.minLoudnessLufs)
    {
        r.reason = "That file is silent, or close to it (" + num ("%.0f LUFS", double (a.loudnessLufs)) + ").";
        r.guidance = "Pick the finished mix rather than a stem, a count-in or an empty track.";
        return r;
    }
    if (a.silencePercent > B.maxSilencePercent)
    {
        r.reason = num ("%.0f %%", double (a.silencePercent)) + " of that file is silence.";
        r.guidance = "DLIVE would be measuring the gaps. Use a recording that plays most of the way through.";
        return r;
    }
    r.usable = true;
    return r;
}

ReferenceProfile profileFrom (const AnalysisResult& a, std::string name, std::string path)
{
    ReferenceProfile p;
    p.name = std::move (name);
    p.path = std::move (path);
    if (! a.valid) return p;
    p.valid = true;
    p.seconds = a.durationSeconds;
    p.channels = a.numChannels;
    p.bandEnergyDb = a.bandEnergyDb;
    p.thirdOctaveDb = a.thirdOctaveDb;
    p.crestFactorDb = a.crestFactorDb;
    p.loudnessLufs = a.loudnessLufs;
    p.truePeakDb = a.truePeakDb;
    p.stereoCorrelation = a.stereoCorrelation;
    p.spectralCentroidHz = a.spectralCentroidHz;
    p.highFrequencyRatioDb = a.highFrequencyRatioDb;
    p.tempoBpm = a.tempoBpm;
    p.tempoConfidence = a.tempoConfidence;
    return p;
}

SourceTargets targets (const SourceTargets& base, const ReferenceProfile& ref,
                       const AnalysisResult& masterInput, StyleProfileId profile, ReferenceMatch& out)
{
    SourceTargets t = base;
    out = ReferenceMatch {};
    if (! ref.valid) return t;

    const auto& B = MixProfile::referenceBounds (profile);
    out.used = true;
    out.name = ref.name;

    // ---- Tone. Band energy is measured relative to the whole, so it says nothing about how
    // loud either recording is: a finished master at -9 LUFS and a live mix at -23 are directly
    // comparable here, and the fader moves the plan is about to make do not disturb it.
    struct Ask { Band band; float asked; };
    std::vector<Ask> asks;
    std::vector<Band> bounded;
    for (int i = 0; i < int (Band::Count); ++i)
    {
        const Band b = Band (i);
        const float wanted = ref.bandEnergyDb[size_t (i)];
        const float profileAim = base.bandTargetDb[size_t (i)];
        const float aim = clampf (wanted, profileAim - B.maxTargetShiftDb, profileAim + B.maxTargetShiftDb);

        t.bandTargetDb[size_t (i)] = aim;
        // Matching aims closer than the profile's own "that will do": the point of a reference
        // is the difference you can hear between two records, which is smaller than a tolerance
        // meant to stop DLIVE fussing over a source that is already fine.
        t.bandToleranceDb[size_t (i)] = std::min (base.bandToleranceDb[size_t (i)], B.toleranceDb);

        auto& m = out.bands[size_t (i)];
        m.band = b;
        m.referenceDb = wanted;
        m.mixDb = masterInput.valid ? masterInput.bandEnergyDb[size_t (i)] : 0.0f;
        m.profileAimDb = profileAim;
        m.aimDb = aim;
        m.askedDb = masterInput.valid ? aim - m.mixDb : 0.0f;
        m.pulledDb = aim - profileAim;
        m.bounded = std::fabs (wanted - profileAim) > B.maxTargetShiftDb + 0.05f;

        // What is worth a sentence is what having a reference *changed*: where the aim now
        // sits against where this profile would have put it on its own. How far the mix still
        // has to travel is the chart's job, and saying it here reads as a contradiction
        // whenever the profile and the reference pull the same band opposite ways.
        if (std::fabs (m.pulledDb) >= B.noteDb)
            asks.push_back ({ b, m.pulledDb });
        if (m.bounded)
            bounded.push_back (b);
    }

    // One sentence for every band a reference asked too much of, not one per band: eight
    // lines of the same refusal is a wall nobody reads, and the thing that has to be read is
    // that the master stopped short of the record on purpose.
    if (! bounded.empty())
    {
        std::string names;
        for (size_t i = 0; i < bounded.size(); ++i)
            names += (i == 0 ? "" : (i + 1 == bounded.size() ? " and " : ", ")) + std::string (bandWord (bounded[i]));
        out.limits.push_back ("The reference sits further from a " + std::string (styleProfileName (profile)) + " mix than a reference may pull one in "
                              + names + ". Each aim may move " + db (B.maxTargetShiftDb)
                              + " and no further, so the master goes that far and stops.");
    }

    // The loudest asks first: four sentences is a paragraph somebody reads, twelve is a wall.
    std::sort (asks.begin(), asks.end(), [] (const Ask& a, const Ask& b) { return std::fabs (a.asked) > std::fabs (b.asked); });
    for (size_t i = 0; i < asks.size() && i < 4; ++i)
    {
        const bool more = asks[i].asked > 0.0f;
        std::string line = more ? "More " : "Less ";
        line += bandWord (asks[i].band);
        line += ": the aim moves " + db (asks[i].asked) + (more ? " above " : " below ") + "the profile's own.";
        out.aims.push_back (line);
    }
    if (out.aims.empty())
        out.aims.push_back (std::string ("This reference wants the same balance a ") + styleProfileName (profile)
                            + " mix already aims at, so the tone is left where it is.");

    // ---- Width. Correlation says how much side energy a recording carries; the width control
    // scales exactly that, so the two can be matched honestly rather than by feel.
    if (! base.widthAppropriate)
        out.limits.push_back ("Stereo width is not part of this output's profile, so the reference's image was left out of it.");
    else if (ref.channels < 2)
        out.limits.push_back ("The reference is mono, so it has no stereo image to copy. The master keeps the profile's width.");
    else if (! masterInput.valid || masterInput.numChannels < 2)
        out.limits.push_back ("This mix reaches the master in mono, so there is no image to widen. The master keeps the profile's width.");
    else
    {
        const double mixSide = sideRatio (masterInput.stereoCorrelation);
        const double refSide = sideRatio (ref.stereoCorrelation);
        if (mixSide > 1.0e-6 && refSide > 1.0e-6)
        {
            const float scale = clampf (float (std::sqrt (refSide / mixSide)), 1.0f - B.maxWidthDelta, 1.0f + B.maxWidthDelta);
            const float width = std::round (clampf (base.widthTarget * scale, base.widthMin, base.widthMax) * 20.0f) / 20.0f;
            t.widthTarget = width;
            out.widthMatched = true;
            out.widthTarget = width;
            if (std::fabs (width - base.widthTarget) >= 0.05f)
                out.aims.push_back (std::string (width > base.widthTarget ? "The reference is wider than this mix" : "The reference is narrower than this mix")
                                    + num (", so the master image goes to %.2f.", double (width)));
        }
    }

    // ---- Density. How hard the glue works, never whether there is any: switching the master
    // compressor on or off from somebody else's record is a bigger decision than a reference
    // gets to make, and crest factor on a mastered file is measured after a limiter this mix
    // has not reached yet.
    if (! base.compressionAppropriate)
        out.limits.push_back ("This output's profile does not compress the master, so the reference's density was left out of it.");
    else if (ref.crestFactorDb > 0.5f)
    {
        const float density = base.crestFactorMaxDb - ref.crestFactorDb;   // > 0: denser than the profile expects
        const float shift = clampf (0.5f * density, -B.maxCompTargetShiftDb, B.maxCompTargetShiftDb);
        t.compTargetGrDb = std::max (0.5f, base.compTargetGrDb + shift);
        out.densityMatched = std::fabs (t.compTargetGrDb - base.compTargetGrDb) >= 0.1f;
        out.compTargetGrDb = t.compTargetGrDb;
        if (out.densityMatched)
            out.aims.push_back (std::string ("The reference is ") + (shift > 0.0f ? "denser" : "more open")
                                + " than the profile's default, so the master glue works " + db (shift)
                                + (shift > 0.0f ? " harder." : " less hard."));
    }

    // ---- Loudness. Measured, reported, never copied.
    if (base.loudnessTargetAppropriate)
    {
        out.loudnessDifferenceLu = ref.loudnessLufs - base.targetLufs;
        if (std::fabs (out.loudnessDifferenceLu) >= 1.0f)
            out.limits.push_back ("The reference measures " + num ("%.0f LUFS", double (ref.loudnessLufs))
                                  + " and this output is delivered at " + num ("%.0f LUFS", double (base.targetLufs))
                                  + ". How loud the stream is belongs to the delivery, not to a reference, so the loudness target was not touched.");
    }

    out.limits.push_back ("A reference sets the tone of the finished mix, not who is loud inside it. "
                          "The balance between your sources stays where DLIVE heard it should be.");
    return t;
}

} // namespace Reference
} // namespace livemix
