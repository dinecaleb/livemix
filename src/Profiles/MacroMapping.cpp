#include "MacroMapping.h"
#include "Core/DbUtils.h"
#include "Core/ProductDefinition.h"
#include "State/ParameterIDs.h"

namespace livemix
{

namespace MacroMapping
{

namespace
{
    // -1 .. +1 around the 50 baseline.
    float centred (float v) { return clamp ((v - 50.0f) / 50.0f, -1.0f, 1.0f); }

    void toneBandIds (std::vector<std::string>& v)
    {
        for (int i = 0; i < ParamID::kToneBands; ++i)
        {
            v.push_back (eqBandId ("toneEq", i, "On"));
            v.push_back (eqBandId ("toneEq", i, "Gain"));
        }
    }

    // ---- Dine Vocals: WARMTH CLARITY SMOOTH STEADY CLEAN-UP ----
    ChannelParameters applyVocals (const ChannelParameters& baseline, const MacroValues& m, RoleFamily family)
    {
        ChannelParameters p = baseline;
        const float warmth = centred (m.v[0]), clarity = centred (m.v[1]), smooth = centred (m.v[2]), steady = centred (m.v[3]), cleanup = centred (m.v[4]);

        // Warmth: low shelf and low-mid body.
        p.toneBands[0].gainDb = clamp (baseline.toneBands[0].gainDb + 3.0f * warmth, -12.0f, 12.0f);
        p.toneBands[0].enabled = baseline.toneBands[0].enabled || warmth != 0.0f;
        p.toneBands[1].gainDb = clamp (baseline.toneBands[1].gainDb + 1.5f * warmth, -12.0f, 12.0f);
        p.toneBands[1].enabled = baseline.toneBands[1].enabled || warmth != 0.0f;

        // Clarity: presence and air, minus a touch when smoothing.
        p.toneBands[2].gainDb = clamp (baseline.toneBands[2].gainDb + 2.5f * clarity - 1.0f * std::max (0.0f, smooth), -12.0f, 12.0f);
        p.toneBands[2].enabled = baseline.toneBands[2].enabled || clarity != 0.0f || smooth > 0.0f;
        p.toneBands[3].gainDb = clamp (baseline.toneBands[3].gainDb + 2.0f * clarity, -12.0f, 12.0f);
        p.toneBands[3].enabled = baseline.toneBands[3].enabled || clarity != 0.0f;

        // Smooth: de-esser depth and threshold; harshness cut on corrective band 3.
        p.deEssRangeDb = clamp (baseline.deEssRangeDb + 6.0f * smooth, 0.0f, 24.0f);
        p.deEssThresholdDb = clamp (baseline.deEssThresholdDb - 6.0f * smooth, -60.0f, 0.0f);
        p.deEssEnabled = smooth <= -0.99f ? false : (baseline.deEssEnabled || smooth > 0.0f);
        if (smooth > 0.0f)
        {
            p.correctiveBands[2].enabled = true;
            p.correctiveBands[2].type = FilterType::Peak;
            if (! baseline.correctiveBands[2].enabled) { p.correctiveBands[2].freqHz = 4000.0f; p.correctiveBands[2].q = 1.2f; }
            p.correctiveBands[2].gainDb = clamp (std::min (baseline.correctiveBands[2].enabled ? baseline.correctiveBands[2].gainDb : 0.0f, 0.0f) - 3.0f * smooth, -12.0f, 0.0f);
        }

        // Steady: compression amount.
        p.compRatio = clamp (baseline.compRatio * (1.0f + 0.5f * steady), 1.0f, 20.0f);
        p.compThresholdDb = clamp (baseline.compThresholdDb - 4.0f * steady, -60.0f, 0.0f);
        p.compKneeDb = clamp (baseline.compKneeDb - 2.0f * steady, 0.0f, 24.0f);
        if (steady > 0.0f) p.compEnabled = true;

        // Clean-up: high-pass and expander (gentle: it is a voice, never a hard mute).
        p.hpfHz = clamp (baseline.hpfHz * (1.0f + 0.5f * cleanup), 20.0f, 1000.0f);
        if (cleanup > 0.0f) p.hpfEnabled = true;
        const bool expanderOk = family != RoleFamily::Choir && family != RoleFamily::VocalBus;
        if (expanderOk)
        {
            p.gateRangeDb = clamp (baseline.gateRangeDb + 8.0f * cleanup, 0.0f, 80.0f);
            p.gateThresholdDb = clamp (baseline.gateThresholdDb + 6.0f * cleanup, -80.0f, 0.0f);
            if (cleanup > 0.0f) p.gateEnabled = true;
            if (cleanup <= -0.99f) p.gateEnabled = false;
        }
        return p;
    }

    // ---- Dine Keys: WARMTH SHINE CLEAN-UP STEADY WIDTH ----
    ChannelParameters applyKeys (const ChannelParameters& baseline, const MacroValues& m, RoleFamily)
    {
        ChannelParameters p = baseline;
        const float warmth = centred (m.v[0]), shine = centred (m.v[1]), cleanup = centred (m.v[2]), steady = centred (m.v[3]), width = centred (m.v[4]);

        p.toneBands[0].gainDb = clamp (baseline.toneBands[0].gainDb + 3.0f * warmth, -12.0f, 12.0f);
        p.toneBands[0].enabled = baseline.toneBands[0].enabled || warmth != 0.0f;

        p.toneBands[2].gainDb = clamp (baseline.toneBands[2].gainDb + 1.0f * shine, -12.0f, 12.0f);
        p.toneBands[2].enabled = baseline.toneBands[2].enabled || shine != 0.0f;
        p.toneBands[3].gainDb = clamp (baseline.toneBands[3].gainDb + 3.0f * shine, -12.0f, 12.0f);
        p.toneBands[3].enabled = baseline.toneBands[3].enabled || shine != 0.0f;

        // Clean-up: high-pass up, low-mid cut deeper (corrective band 1 is the mud band in every keys baseline).
        p.hpfHz = clamp (baseline.hpfHz * (1.0f + 0.6f * cleanup), 20.0f, 1000.0f);
        if (cleanup > 0.0f) p.hpfEnabled = true;
        p.correctiveBands[0].gainDb = clamp ((baseline.correctiveBands[0].enabled ? baseline.correctiveBands[0].gainDb : 0.0f) - 3.0f * cleanup, -12.0f, 3.0f);
        p.correctiveBands[0].enabled = baseline.correctiveBands[0].enabled || cleanup > 0.0f;

        p.compRatio = clamp (baseline.compRatio * (1.0f + 0.5f * steady), 1.0f, 20.0f);
        p.compThresholdDb = clamp (baseline.compThresholdDb - 4.0f * steady, -60.0f, 0.0f);
        if (steady > 0.0f) p.compEnabled = true;

        p.widthAmount = clamp (baseline.widthAmount * (1.0f + 0.5f * width), 0.0f, 2.0f);
        p.widthEnabled = baseline.widthEnabled || width != 0.0f;
        return p;
    }

    // ---- Dine Guitar: WARMTH CLARITY SMOOTH STEADY CLEAN-UP ----
    ChannelParameters applyGuitar (const ChannelParameters& baseline, const MacroValues& m, RoleFamily family)
    {
        ChannelParameters p = baseline;
        const float warmth = centred (m.v[0]), clarity = centred (m.v[1]), smooth = centred (m.v[2]), steady = centred (m.v[3]), cleanup = centred (m.v[4]);

        // Warmth: low shelf (body) and low-mid fullness.
        p.toneBands[0].gainDb = clamp (baseline.toneBands[0].gainDb + 3.0f * warmth, -12.0f, 12.0f);
        p.toneBands[0].enabled = baseline.toneBands[0].enabled || warmth != 0.0f;
        p.toneBands[1].gainDb = clamp (baseline.toneBands[1].gainDb + 1.5f * warmth, -12.0f, 12.0f);
        p.toneBands[1].enabled = baseline.toneBands[1].enabled || warmth != 0.0f;

        // Clarity: pick definition and sparkle, minus a touch when smoothing.
        p.toneBands[2].gainDb = clamp (baseline.toneBands[2].gainDb + 2.5f * clarity - 1.0f * std::max (0.0f, smooth), -12.0f, 12.0f);
        p.toneBands[2].enabled = baseline.toneBands[2].enabled || clarity != 0.0f || smooth > 0.0f;
        p.toneBands[3].gainDb = clamp (baseline.toneBands[3].gainDb + 2.0f * clarity - 1.0f * std::max (0.0f, smooth), -12.0f, 12.0f);
        p.toneBands[3].enabled = baseline.toneBands[3].enabled || clarity != 0.0f || smooth > 0.0f;

        // Smooth: deeper cut on the harshness band (corrective band 3 is the quack / fizz band in every guitar baseline).
        if (smooth > 0.0f)
        {
            p.correctiveBands[2].enabled = true;
            p.correctiveBands[2].type = FilterType::Peak;
            if (! baseline.correctiveBands[2].enabled) { p.correctiveBands[2].freqHz = 3000.0f; p.correctiveBands[2].q = 1.5f; }
            p.correctiveBands[2].gainDb = clamp (std::min (baseline.correctiveBands[2].enabled ? baseline.correctiveBands[2].gainDb : 0.0f, 0.0f) - 4.0f * smooth, -12.0f, 0.0f);
        }
        else if (smooth < 0.0f && baseline.correctiveBands[2].enabled)
            p.correctiveBands[2].gainDb = clamp (baseline.correctiveBands[2].gainDb * (1.0f + smooth), -12.0f, 0.0f); // more bite: the template cut fades out

        // Steady: compression amount.
        p.compRatio = clamp (baseline.compRatio * (1.0f + 0.5f * steady), 1.0f, 20.0f);
        p.compThresholdDb = clamp (baseline.compThresholdDb - 4.0f * steady, -60.0f, 0.0f);
        p.compKneeDb = clamp (baseline.compKneeDb - 2.0f * steady, 0.0f, 24.0f);
        if (steady > 0.0f) p.compEnabled = true;

        // Clean-up: high-pass and expander (amp noise between phrases; never a hard mute, never on a bus).
        p.hpfHz = clamp (baseline.hpfHz * (1.0f + 0.5f * cleanup), 20.0f, 1000.0f);
        if (cleanup > 0.0f) p.hpfEnabled = true;
        if (family != RoleFamily::GuitarBus)
        {
            p.gateRangeDb = clamp (baseline.gateRangeDb + 10.0f * cleanup, 0.0f, 80.0f);
            p.gateThresholdDb = clamp (baseline.gateThresholdDb + 6.0f * cleanup, -80.0f, 0.0f);
            if (cleanup > 0.0f) p.gateEnabled = true;
            if (cleanup <= -0.99f) p.gateEnabled = false;
        }
        return p;
    }

    // ---- Dine Bass: WARMTH CLARITY GRIT STEADY CLEAN-UP ----
    ChannelParameters applyBass (const ChannelParameters& baseline, const MacroValues& m, RoleFamily family)
    {
        ChannelParameters p = baseline;
        const float warmth = centred (m.v[0]), clarity = centred (m.v[1]), grit = centred (m.v[2]), steady = centred (m.v[3]), cleanup = centred (m.v[4]);

        // Warmth: low shelf (weight) and the body band.
        p.toneBands[0].gainDb = clamp (baseline.toneBands[0].gainDb + 3.0f * warmth, -12.0f, 12.0f);
        p.toneBands[0].enabled = baseline.toneBands[0].enabled || warmth != 0.0f;
        p.toneBands[1].gainDb = clamp (baseline.toneBands[1].gainDb + 1.5f * warmth, -12.0f, 12.0f);
        p.toneBands[1].enabled = baseline.toneBands[1].enabled || warmth != 0.0f;

        // Clarity: string definition and the finger / pick top.
        p.toneBands[2].gainDb = clamp (baseline.toneBands[2].gainDb + 2.5f * clarity, -12.0f, 12.0f);
        p.toneBands[2].enabled = baseline.toneBands[2].enabled || clarity != 0.0f;
        p.toneBands[3].gainDb = clamp (baseline.toneBands[3].gainDb + 1.5f * clarity, -12.0f, 12.0f);
        p.toneBands[3].enabled = baseline.toneBands[3].enabled || clarity != 0.0f;

        // Grit: saturation drive around the template; 0 switches it off.
        p.satDrive = clamp (baseline.satDrive + 0.35f * grit, 0.0f, 0.7f);
        p.satEnabled = grit <= -0.99f ? false : (baseline.satEnabled || grit > 0.0f);

        // Steady: compression amount (bass takes more than most sources).
        p.compRatio = clamp (baseline.compRatio * (1.0f + 0.5f * steady), 1.0f, 20.0f);
        p.compThresholdDb = clamp (baseline.compThresholdDb - 4.0f * steady, -60.0f, 0.0f);
        p.compKneeDb = clamp (baseline.compKneeDb - 2.0f * steady, 0.0f, 24.0f);
        if (steady > 0.0f) p.compEnabled = true;

        // Clean-up: a small high-pass move (the lowest note is only 41 Hz) and an expander for hum or noise
        // between notes; never on a sustained synth bass, never on the bus.
        p.hpfHz = clamp (baseline.hpfHz * (1.0f + 0.2f * cleanup), 20.0f, 1000.0f);
        if (cleanup > 0.0f) p.hpfEnabled = true;
        if (family == RoleFamily::ElectricBass)
        {
            p.gateRangeDb = clamp (baseline.gateRangeDb + 8.0f * cleanup, 0.0f, 80.0f);
            p.gateThresholdDb = clamp (baseline.gateThresholdDb + 6.0f * cleanup, -80.0f, 0.0f);
            if (cleanup > 0.0f) p.gateEnabled = true;
            if (cleanup <= -0.99f) p.gateEnabled = false;
        }
        return p;
    }

    // ---- Dine Master: WARMTH CLARITY GLUE LOUD WIDTH ----
    ChannelParameters applyMaster (const ChannelParameters& baseline, const MacroValues& m, RoleFamily)
    {
        ChannelParameters p = baseline;
        const float warmth = centred (m.v[0]), clarity = centred (m.v[1]), glue = centred (m.v[2]), loud = centred (m.v[3]), width = centred (m.v[4]);

        p.toneBands[0].gainDb = clamp (baseline.toneBands[0].gainDb + 2.0f * warmth, -12.0f, 12.0f);
        p.toneBands[0].enabled = baseline.toneBands[0].enabled || warmth != 0.0f;
        p.toneBands[2].gainDb = clamp (baseline.toneBands[2].gainDb + 1.5f * clarity, -12.0f, 12.0f);
        p.toneBands[2].enabled = baseline.toneBands[2].enabled || clarity != 0.0f;
        p.toneBands[3].gainDb = clamp (baseline.toneBands[3].gainDb + 2.0f * clarity, -12.0f, 12.0f);
        p.toneBands[3].enabled = baseline.toneBands[3].enabled || clarity != 0.0f;

        p.compThresholdDb = clamp (baseline.compThresholdDb - 4.0f * glue, -60.0f, 0.0f);
        p.compRatio = clamp (baseline.compRatio * (1.0f + 0.3f * glue), 1.0f, 20.0f);
        p.compEnabled = glue <= -0.99f ? false : (baseline.compEnabled || glue > 0.0f);

        // Loud: drive into the limiter (the ceiling never moves).
        p.outputTrimDb = clamp (baseline.outputTrimDb + 6.0f * loud, -24.0f, 24.0f);
        p.limiterEnabled = true;

        p.widthAmount = clamp (baseline.widthAmount * (1.0f + 0.3f * width), 0.0f, 2.0f);
        p.widthEnabled = baseline.widthEnabled || width != 0.0f;
        return p;
    }
}

ChannelParameters apply (const ChannelParameters& baseline, const Macros& m, RoleFamily family)
{
    ChannelParameters p = baseline;

    const float punch  = clamp ((m.punch  - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float body   = clamp ((m.body   - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float attack = clamp ((m.attack - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float bleed  = clamp ((m.bleedReduction - 50.0f) / 50.0f, -1.0f, 1.0f);
    const float character = clamp ((m.character - 0.35f) / 0.65f, -0.54f, 1.0f); // 0 at baseline, 1 fully aggressive

    // --- Punch: compression density + transient snap + upper-mid presence ---
    p.compRatio = clamp (baseline.compRatio * (1.0f + 0.5f * punch) + 1.2f * character, 1.0f, 20.0f);
    p.compAttackMs = clamp (baseline.compAttackMs * (1.0f + 0.6f * punch), 0.1f, 200.0f);
    p.compThresholdDb = clamp (baseline.compThresholdDb - 3.0f * punch - 3.0f * character, -60.0f, 0.0f);
    p.compKneeDb = clamp (baseline.compKneeDb - 4.0f * character, 0.0f, 24.0f);

    const bool transientOk = family != RoleFamily::Overhead && family != RoleFamily::HiHat && family != RoleFamily::Bus;
    if (transientOk)
    {
        p.transientAttack = clamp (baseline.transientAttack + 0.3f * punch + 0.4f * attack, -1.0f, 1.0f);
        p.transientSustain = clamp (baseline.transientSustain - 0.15f * punch, -1.0f, 1.0f);
        if (punch != 0.0f || attack != 0.0f) p.transientEnabled = true;
    }

    // --- Tone EQ: band 0 = body (low shelf), band 2 = presence/attack peak, band 3 = air/high shelf ---
    p.toneBands[0].gainDb = clamp (baseline.toneBands[0].gainDb + 3.0f * body, -12.0f, 12.0f);
    p.toneBands[0].enabled = baseline.toneBands[0].enabled || body != 0.0f;

    p.toneBands[1].gainDb = clamp (baseline.toneBands[1].gainDb + 1.5f * body, -12.0f, 12.0f);
    p.toneBands[1].enabled = baseline.toneBands[1].enabled || body != 0.0f;

    p.toneBands[2].gainDb = clamp (baseline.toneBands[2].gainDb + 2.0f * punch + 2.0f * character, -12.0f, 12.0f);
    p.toneBands[2].enabled = baseline.toneBands[2].enabled || punch != 0.0f || character != 0.0f;

    p.toneBands[3].gainDb = clamp (baseline.toneBands[3].gainDb + 2.0f * attack, -12.0f, 12.0f);
    p.toneBands[3].enabled = baseline.toneBands[3].enabled || attack != 0.0f;

    // --- Bleed reduction: gate depth/threshold (only where a gate makes sense) ---
    const bool gateOk = family == RoleFamily::Kick || family == RoleFamily::Snare || family == RoleFamily::Tom;
    if (gateOk)
    {
        p.gateRangeDb = clamp (baseline.gateRangeDb + 20.0f * bleed, 0.0f, 80.0f);
        p.gateThresholdDb = clamp (baseline.gateThresholdDb + 6.0f * bleed, -80.0f, 0.0f);
        p.gateRatio = clamp (baseline.gateRatio + 4.0f * bleed, 1.0f, 20.0f);
        if (bleed > 0.0f) p.gateEnabled = true;
        if (bleed <= -0.99f) p.gateEnabled = false;
    }
    else
    {
        // Overheads/hat/room: bleed reduction raises the high-pass to tame rumble/kick spill.
        p.hpfHz = clamp (baseline.hpfHz * (1.0f + 0.6f * bleed), 20.0f, 1000.0f);
        if (bleed > 0.0f) p.hpfEnabled = true;
    }

    // --- Character: saturation ---
    if (character > 0.15f)
    {
        p.satEnabled = true;
        p.satDrive = clamp (baseline.satDrive + 0.5f * (character - 0.15f), 0.0f, 0.7f);
    }
    else
    {
        p.satEnabled = baseline.satEnabled;
        p.satDrive = baseline.satDrive;
    }

    return p;
}

ChannelParameters apply (Product product, const ChannelParameters& baseline, const MacroValues& values, RoleFamily family)
{
    switch (product)
    {
        case Product::Vocals: return applyVocals (baseline, values, family);
        case Product::Keys:   return applyKeys (baseline, values, family);
        case Product::Master: return applyMaster (baseline, values, family);
        case Product::Guitar: return applyGuitar (baseline, values, family);
        case Product::Bass:   return applyBass (baseline, values, family);
        case Product::Drums:
        default:              return apply (baseline, toDrums (values), family);
    }
}

const std::vector<std::string>& affectedParameterIds()
{
    static const std::vector<std::string> ids = [] {
        using namespace ParamID;
        std::vector<std::string> v {
            compRatio, compAttack, compThreshold, compKnee,
            transOn, transAttack, transSustain,
            gateOn, gateRange, gateThreshold, gateRatio,
            hpfOn, hpfFreq,
            satOn, satDrive
        };
        toneBandIds (v);
        return v;
    }();
    return ids;
}

const std::vector<std::string>& affectedParameterIds (Product product)
{
    using namespace ParamID;
    static const std::vector<std::string> vocals = [] {
        std::vector<std::string> v {
            compOn, compRatio, compThreshold, compKnee,
            gateOn, gateRange, gateThreshold,
            hpfOn, hpfFreq,
            deEssOn, deEssRange, deEssThreshold,
            eqBandId ("corrEq", 2, "On"), eqBandId ("corrEq", 2, "Type"), eqBandId ("corrEq", 2, "Freq"), eqBandId ("corrEq", 2, "Gain"), eqBandId ("corrEq", 2, "Q")
        };
        toneBandIds (v);
        return v;
    }();
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> v {
            compOn, compRatio, compThreshold,
            hpfOn, hpfFreq,
            eqBandId ("corrEq", 0, "On"), eqBandId ("corrEq", 0, "Gain"),
            widthOn, widthAmount
        };
        toneBandIds (v);
        return v;
    }();
    static const std::vector<std::string> master = [] {
        std::vector<std::string> v {
            compOn, compRatio, compThreshold,
            outputTrim, limiterOn,
            widthOn, widthAmount
        };
        toneBandIds (v);
        return v;
    }();
    static const std::vector<std::string> guitar = [] {
        std::vector<std::string> v {
            compOn, compRatio, compThreshold, compKnee,
            gateOn, gateRange, gateThreshold,
            hpfOn, hpfFreq,
            eqBandId ("corrEq", 2, "On"), eqBandId ("corrEq", 2, "Type"), eqBandId ("corrEq", 2, "Freq"), eqBandId ("corrEq", 2, "Gain"), eqBandId ("corrEq", 2, "Q")
        };
        toneBandIds (v);
        return v;
    }();
    static const std::vector<std::string> bass = [] {
        std::vector<std::string> v {
            compOn, compRatio, compThreshold, compKnee,
            gateOn, gateRange, gateThreshold,
            hpfOn, hpfFreq,
            satOn, satDrive
        };
        toneBandIds (v);
        return v;
    }();
    switch (product)
    {
        case Product::Vocals: return vocals;
        case Product::Keys:   return keys;
        case Product::Master: return master;
        case Product::Guitar: return guitar;
        case Product::Bass:   return bass;
        case Product::Drums:
        default:              return affectedParameterIds();
    }
}

MacroValues defaults (Product product)
{
    MacroValues m;
    const auto& d = productDefinition (product);
    for (size_t i = 0; i < 5; ++i) m.v[i] = d.macros[i].defaultValue;
    return m;
}

MacroValues fromDrums (const Macros& m)
{
    MacroValues v;
    v.v = { m.punch, m.body, m.attack, m.character, m.bleedReduction };
    return v;
}

Macros toDrums (const MacroValues& v)
{
    Macros m;
    m.punch = v.v[0]; m.body = v.v[1]; m.attack = v.v[2]; m.character = v.v[3]; m.bleedReduction = v.v[4];
    return m;
}

} // namespace MacroMapping
} // namespace livemix
