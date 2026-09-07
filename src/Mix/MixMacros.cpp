#include "MixMacros.h"
#include "Profiles/MixProfileData.h"
#include "Core/DbUtils.h"
#include <cmath>

namespace livemix
{

namespace
{
    // -1 .. +1 from 0 .. 100, exactly 0 at 50.
    float bipolar (float v) noexcept { return clamp ((v - 50.0f) / 50.0f, -1.0f, 1.0f); }

    // A shelf on top of what the plan left there: same frequency if the band is in use, else the profile's.
    void shelf (EQBandParams& band, FilterType type, float defaultHz, float deltaDb)
    {
        if (! band.enabled) { band = { true, type, defaultHz, 0.0f, 0.7f }; }
        band.type = type;
        band.gainDb = clamp (band.gainDb + deltaDb, -6.0f, 6.0f);
    }
}

namespace MixMacros
{

const char* name (MixMacro m) noexcept
{
    switch (m)
    {
        case MixMacro::Vocals: return "VOCALS";
        case MixMacro::Drums:  return "DRUMS";
        case MixMacro::Bass:   return "BASS";
        case MixMacro::Space:  return "SPACE";
        case MixMacro::Energy:
        default:               return "ENERGY";
    }
}

const char* lowLabel (MixMacro m) noexcept
{
    switch (m)
    {
        case MixMacro::Vocals: return "Warm";
        case MixMacro::Drums:  return "Tight";
        case MixMacro::Bass:   return "Clean";
        case MixMacro::Space:  return "Dry";
        case MixMacro::Energy:
        default:               return "Natural";
    }
}

const char* highLabel (MixMacro m) noexcept
{
    switch (m)
    {
        case MixMacro::Vocals: return "Bright";
        case MixMacro::Drums:  return "Big";
        case MixMacro::Bass:   return "Huge";
        case MixMacro::Space:  return "Wet";
        case MixMacro::Energy:
        default:               return "Polished";
    }
}

const char* tooltip (MixMacro m) noexcept
{
    switch (m)
    {
        case MixMacro::Vocals: return "The whole vocal group. Warm adds body and softens the top; Bright opens the top end for clarity.";
        case MixMacro::Drums:  return "The drum group. Tight is punchier and more controlled; Big is fuller and lets the kit breathe.";
        case MixMacro::Bass:   return "The bass. Clean keeps it defined and tidy; Huge adds weight and a little grit for small speakers.";
        case MixMacro::Space:  return "How much reverb and delay the vocals and drums get. The middle is what Tune Mix chose.";
        case MixMacro::Energy:
        default:               return "The finish on the whole mix. Natural leaves dynamics alone; Polished is denser and more broadcast-like.";
    }
}

MixParameters apply (const MixParameters& base, const MixMacroValues& values, const RoutingGraph& graph, StyleProfileId profile)
{
    MixParameters p = base;
    const auto& R = MixProfile::macroRanges (profile);

    // VOCALS: Warm <-> Bright on the vocal bus shelves.
    if (const float t = bipolar (values.get (MixMacro::Vocals)); t != 0.0f)
    {
        auto& c = p.buses[size_t (MixBus::Vocals)].channel;
        c.toneEqEnabled = true;
        shelf (c.toneBands[0], FilterType::LowShelf, R.vocalLowShelfHz, t < 0.0f ? -t * R.vocalWarmLowDb : t * R.vocalBrightLowDb);
        shelf (c.toneBands[3], FilterType::HighShelf, R.vocalHighShelfHz, t > 0.0f ? t * R.vocalBrightHighDb : -t * R.vocalWarmHighDb);
    }

    // DRUMS: Tight <-> Big on the drum bus.
    if (const float t = bipolar (values.get (MixMacro::Drums)); t != 0.0f)
    {
        auto& c = p.buses[size_t (MixBus::Drums)].channel;
        c.toneEqEnabled = true;
        shelf (c.toneBands[0], FilterType::LowShelf, R.drumLowShelfHz, t > 0.0f ? t * R.drumBigLowDb : -t * R.drumTightLowDb);
        if (t < 0.0f)
        {
            c.compEnabled = true;
            c.compRatio = clamp (c.compRatio * (1.0f - t * R.drumTightRatioScale), 1.2f, 8.0f);
            c.compAttackMs = clamp (c.compAttackMs * (1.0f + t * R.drumTightAttackScale), 1.0f, 100.0f);
        }
        else
        {
            c.compReleaseMs = clamp (c.compReleaseMs * (1.0f + t * R.drumBigReleaseScale), 40.0f, 600.0f);
            c.satEnabled = true;
            c.satDrive = clamp (c.satDrive + t * R.drumBigSatDrive, 0.0f, 0.5f);
        }
    }

    // BASS: Clean <-> Huge on the bass bus.
    if (const float t = bipolar (values.get (MixMacro::Bass)); t != 0.0f)
    {
        auto& c = p.buses[size_t (MixBus::Bass)].channel;
        c.toneEqEnabled = true;
        shelf (c.toneBands[0], FilterType::LowShelf, R.bassLowShelfHz, t > 0.0f ? t * R.bassHugeLowDb : -t * R.bassCleanLowDb);
        if (t > 0.0f) { c.satEnabled = true; c.satDrive = clamp (c.satDrive + t * R.bassHugeSatDrive, 0.0f, 0.5f); }
        else c.satDrive = clamp (c.satDrive * (1.0f + t), 0.0f, 0.5f);   // full Clean removes the drive
    }

    // SPACE: Dry <-> Wet on every send that exists.
    if (const float t = bipolar (values.get (MixMacro::Space)); t != 0.0f)
    {
        for (int i = 0; i < p.numStrips; ++i)
            for (int f = 0; f < int (FxSlot::Count); ++f)
            {
                float& send = p.strips[size_t (i)].sendDb[size_t (f)];
                if (send <= kSilenceDb || ! graph.fxUsed[size_t (f)]) continue;
                send = clamp (send + t * R.spaceSendRangeDb, -60.0f, 6.0f);
            }
    }

    // ENERGY: Natural <-> Polished on the master.
    if (const float t = bipolar (values.get (MixMacro::Energy)); t != 0.0f)
    {
        auto& c = p.master().channel;
        if (t > 0.0f)
        {
            c.compEnabled = true;
            c.compThresholdDb = clamp (c.compThresholdDb - t * R.energyPolishedThresholdDb, -60.0f, 0.0f);
            c.satEnabled = true;
            c.satDrive = clamp (c.satDrive + t * R.energyPolishedSatDrive, 0.0f, 0.5f);
        }
        else
        {
            c.compRatio = clamp (1.0f + (c.compRatio - 1.0f) * (1.0f + t * R.energyNaturalRatioScale), 1.0f, 8.0f);
            c.satDrive = clamp (c.satDrive * (1.0f + t), 0.0f, 0.5f);
        }
    }
    return p;
}

} // namespace MixMacros
} // namespace livemix
