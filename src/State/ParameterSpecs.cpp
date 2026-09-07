#include "ParameterSpecs.h"
#include "ParameterIDs.h"
#include "Core/ChannelRole.h"
#include "Core/ProductDefinition.h"
#include "Core/StyleId.h"
#include "DSP/Biquad.h"
#include "DSP/ChannelParameters.h"
#include "FX/FxParameterSpecs.h"
#include <map>
#include <set>

namespace livemix
{

namespace
{
    using T = ParameterSpec::Type;

    ParameterSpec f (const char* id, const char* name, float lo, float hi, float def, const char* unit = "", float skewMid = 0.0f)
    {
        ParameterSpec s; s.id = id; s.name = name; s.type = T::Float; s.minValue = lo; s.maxValue = hi; s.defaultValue = def; s.unit = unit; s.skewMidpoint = skewMid; return s;
    }
    ParameterSpec b (const char* id, const char* name, bool def)
    {
        ParameterSpec s; s.id = id; s.name = name; s.type = T::Bool; s.minValue = 0; s.maxValue = 1; s.defaultValue = def ? 1.0f : 0.0f; return s;
    }
    ParameterSpec c (const char* id, const char* name, std::vector<std::string> choices, int def)
    {
        ParameterSpec s; s.id = id; s.name = name; s.type = T::Choice; s.minValue = 0; s.maxValue = float (choices.size() - 1); s.defaultValue = float (def); s.choices = std::move (choices); return s;
    }

    // Which DSP stages each product exposes. Unlisted stages stay at their struct defaults (off).
    bool usesStage (Product p, const std::string& id)
    {
        auto starts = [&] (const char* prefix) { return id.rfind (prefix, 0) == 0; };
        const bool gate = starts ("gate");
        const bool transient = starts ("trans");
        const bool deEss = starts ("deEss");
        const bool width = starts ("width");
        const bool limiter = starts ("limiter");
        switch (p)
        {
            case Product::Drums:  return ! deEss && ! width && ! limiter;
            case Product::Vocals: return ! transient && ! width && ! limiter;
            case Product::Keys:   return ! gate && ! transient && ! deEss && ! limiter;
            case Product::Master: return ! gate && ! transient && ! deEss;
            case Product::Guitar: return ! transient && ! deEss && ! limiter;
            case Product::Bass:   return ! transient && ! deEss && ! width && ! limiter;
            case Product::Count:
            default:              return true;
        }
    }

    // Every DSP field with its bounds, in visitor order (the same for every product).
    std::vector<ParameterSpec> dspSpecs()
    {
        using namespace ParamID;
        std::vector<ParameterSpec> v;
        std::vector<std::string> filterTypes;
        for (auto* n : kFilterTypeNames) filterTypes.push_back (n);
        ChannelParameters d; // neutral defaults for the DSP fields

        v.push_back (f (inputTrim, "Input Trim", -24.0f, 24.0f, d.inputTrimDb, "dB"));
        v.push_back (b (polarity, "Polarity Invert", d.polarityInvert));
        v.push_back (b (hpfOn, "HPF", d.hpfEnabled));
        v.push_back (f (hpfFreq, "HPF Frequency", 20.0f, 1000.0f, d.hpfHz, "Hz", 120.0f));
        v.push_back (c (hpfSlope, "HPF Slope", { "12 dB/oct", "24 dB/oct" }, d.hpfSlope));
        v.push_back (b (lpfOn, "LPF", d.lpfEnabled));
        v.push_back (f (lpfFreq, "LPF Frequency", 1000.0f, 20000.0f, d.lpfHz, "Hz", 6000.0f));
        v.push_back (c (lpfSlope, "LPF Slope", { "12 dB/oct", "24 dB/oct" }, d.lpfSlope));

        v.push_back (b (gateOn, "Gate", d.gateEnabled));
        v.push_back (f (gateThreshold, "Gate Threshold", -80.0f, 0.0f, d.gateThresholdDb, "dB"));
        v.push_back (f (gateRange, "Gate Range", 0.0f, 80.0f, d.gateRangeDb, "dB"));
        v.push_back (f (gateAttack, "Gate Attack", 0.01f, 50.0f, d.gateAttackMs, "ms", 2.0f));
        v.push_back (f (gateHold, "Gate Hold", 0.0f, 500.0f, d.gateHoldMs, "ms", 80.0f));
        v.push_back (f (gateRelease, "Gate Release", 5.0f, 1000.0f, d.gateReleaseMs, "ms", 120.0f));
        v.push_back (f (gateHysteresis, "Gate Hysteresis", 0.0f, 12.0f, d.gateHysteresisDb, "dB"));
        v.push_back (f (gateRatio, "Gate Ratio", 1.0f, 20.0f, d.gateRatio, ":1", 4.0f));
        v.push_back (f (gateScHpf, "Gate Detector HPF", 0.0f, 500.0f, d.gateScHpfHz, "Hz"));

        v.push_back (b (corrEqOn, "Corrective EQ", d.correctiveEqEnabled));
        for (int i = 0; i < kCorrectiveBands; ++i)
        {
            const auto& band = d.correctiveBands[size_t (i)];
            const std::string n = "Corr EQ " + std::to_string (i + 1) + " ";
            v.push_back (b (eqBandId ("corrEq", i, "On").c_str(), (n + "On").c_str(), band.enabled));
            v.push_back (c (eqBandId ("corrEq", i, "Type").c_str(), (n + "Type").c_str(), filterTypes, int (band.type)));
            v.push_back (f (eqBandId ("corrEq", i, "Freq").c_str(), (n + "Freq").c_str(), 20.0f, 20000.0f, band.freqHz, "Hz", 630.0f));
            v.push_back (f (eqBandId ("corrEq", i, "Gain").c_str(), (n + "Gain").c_str(), -18.0f, 18.0f, band.gainDb, "dB"));
            v.push_back (f (eqBandId ("corrEq", i, "Q").c_str(), (n + "Q").c_str(), 0.1f, 10.0f, band.q, "", 1.0f));
        }

        v.push_back (b (compOn, "Compressor", d.compEnabled));
        v.push_back (f (compThreshold, "Comp Threshold", -60.0f, 0.0f, d.compThresholdDb, "dB"));
        v.push_back (f (compRatio, "Comp Ratio", 1.0f, 20.0f, d.compRatio, ":1", 4.0f));
        v.push_back (f (compAttack, "Comp Attack", 0.1f, 200.0f, d.compAttackMs, "ms", 10.0f));
        v.push_back (f (compRelease, "Comp Release", 5.0f, 2000.0f, d.compReleaseMs, "ms", 150.0f));
        v.push_back (f (compKnee, "Comp Knee", 0.0f, 24.0f, d.compKneeDb, "dB"));
        v.push_back (f (compMakeup, "Comp Makeup", -12.0f, 24.0f, d.compMakeupDb, "dB"));
        v.push_back (f (compMix, "Comp Mix", 0.0f, 1.0f, d.compMix));
        v.push_back (f (compScHpf, "Comp Detector HPF", 0.0f, 500.0f, d.compScHpfHz, "Hz"));

        v.push_back (b (transOn, "Transient", d.transientEnabled));
        v.push_back (f (transAttack, "Transient Attack", -1.0f, 1.0f, d.transientAttack));
        v.push_back (f (transSustain, "Transient Sustain", -1.0f, 1.0f, d.transientSustain));

        v.push_back (b (toneEqOn, "Tone EQ", d.toneEqEnabled));
        for (int i = 0; i < kToneBands; ++i)
        {
            const auto& band = d.toneBands[size_t (i)];
            const std::string n = "Tone EQ " + std::to_string (i + 1) + " ";
            v.push_back (b (eqBandId ("toneEq", i, "On").c_str(), (n + "On").c_str(), band.enabled));
            v.push_back (c (eqBandId ("toneEq", i, "Type").c_str(), (n + "Type").c_str(), filterTypes, int (band.type)));
            v.push_back (f (eqBandId ("toneEq", i, "Freq").c_str(), (n + "Freq").c_str(), 20.0f, 20000.0f, band.freqHz, "Hz", 630.0f));
            v.push_back (f (eqBandId ("toneEq", i, "Gain").c_str(), (n + "Gain").c_str(), -18.0f, 18.0f, band.gainDb, "dB"));
            v.push_back (f (eqBandId ("toneEq", i, "Q").c_str(), (n + "Q").c_str(), 0.1f, 10.0f, band.q, "", 1.0f));
        }

        v.push_back (b (satOn, "Saturation", d.satEnabled));
        v.push_back (f (satDrive, "Saturation Drive", 0.0f, 1.0f, d.satDrive));
        v.push_back (f (satMix, "Saturation Mix", 0.0f, 1.0f, d.satMix));

        v.push_back (b (deEssOn, "De-esser", d.deEssEnabled));
        v.push_back (f (deEssFreq, "De-esser Frequency", 2000.0f, 12000.0f, d.deEssHz, "Hz", 6000.0f));
        v.push_back (f (deEssThreshold, "De-esser Threshold", -60.0f, 0.0f, d.deEssThresholdDb, "dB"));
        v.push_back (f (deEssRange, "De-esser Range", 0.0f, 24.0f, d.deEssRangeDb, "dB"));

        v.push_back (b (widthOn, "Width", d.widthEnabled));
        v.push_back (f (widthAmount, "Width Amount", 0.0f, 2.0f, d.widthAmount));
        v.push_back (f (widthMonoBelow, "Width Mono Below", 0.0f, 500.0f, d.widthMonoBelowHz, "Hz"));

        v.push_back (b (limiterOn, "Limiter", d.limiterEnabled));
        v.push_back (f (limiterCeiling, "Limiter Ceiling", -12.0f, 0.0f, d.limiterCeilingDb, "dB"));
        v.push_back (f (limiterRelease, "Limiter Release", 10.0f, 1000.0f, d.limiterReleaseMs, "ms", 120.0f));

        v.push_back (f (outputTrim, "Output Trim", -24.0f, 24.0f, d.outputTrimDb, "dB"));
        return v;
    }

    std::vector<ParameterSpec> build (Product product)
    {
        using namespace ParamID;
        const ProductDefinition& def = productDefinition (product);
        std::vector<ParameterSpec> v;

        std::vector<std::string> roles, styles;
        for (auto r : def.roles) roles.push_back (channelRoleName (r));
        for (auto* n : kStyleProfileNames) styles.push_back (n);

        auto role_ = c (role, product == Product::Master ? "Output" : "Channel", roles, roleIndexInProduct (def, def.defaultRole));
        role_.automatable = false; v.push_back (role_);
        // Modern Worship stays the released Drums default; the newer products start on the family default (Modern Gospel).
        const int defaultProfile = int (product == Product::Drums ? StyleProfileId::ModernWorship : StyleProfileId::ModernGospel);
        auto prof_ = c (profile, "Profile", styles, defaultProfile); prof_.automatable = false; v.push_back (prof_);
        v.push_back (b (bypass, "A/B Original", false));
        v.push_back (b (abMatch, "A/B Loudness Match", true));
        auto live_ = b (liveSafe, "Live Safe", false); live_.automatable = false; v.push_back (live_);

        for (const auto& m : def.macros)
        {
            // Released Drums names are kept verbatim; the newer products use the knob label.
            const char* name = product == Product::Drums
                ? (std::string (m.id) == character ? "Character" : std::string (m.id) == bleed ? "Bleed Reduction"
                   : std::string (m.id) == punch ? "Punch" : std::string (m.id) == body ? "Body" : "Attack")
                : m.label;
            v.push_back (f (m.id, name, m.minValue, m.maxValue, m.defaultValue));
        }

        for (auto& s : dspSpecs())
            if (usesStage (product, s.id)) v.push_back (s);
        return v;
    }
}

const std::vector<ParameterSpec>& channelParameterSpecs (Product p)
{
    static const std::vector<ParameterSpec> drums = build (Product::Drums);
    static const std::vector<ParameterSpec> vocals = build (Product::Vocals);
    static const std::vector<ParameterSpec> keys = build (Product::Keys);
    static const std::vector<ParameterSpec> master = build (Product::Master);
    static const std::vector<ParameterSpec> guitar = build (Product::Guitar);
    static const std::vector<ParameterSpec> bass = build (Product::Bass);
    switch (p)
    {
        case Product::Vocals: return vocals;
        case Product::Keys:   return keys;
        case Product::Master: return master;
        case Product::Guitar: return guitar;
        case Product::Bass:   return bass;
        case Product::Drums:
        default:              return drums;
    }
}

const std::vector<ParameterSpec>& allParameterSpecs()
{
    return channelParameterSpecs (Product::Drums);
}

bool productUsesParameter (Product p, const std::string& id)
{
    static std::map<int, std::set<std::string>> sets = [] {
        std::map<int, std::set<std::string>> m;
        for (int i = 0; i < int (Product::Count); ++i)
            for (const auto& s : channelParameterSpecs (Product (i))) m[i].insert (s.id);
        return m;
    }();
    return sets[int (p)].count (id) > 0;
}

const ParameterSpec* findParameterSpec (const std::string& id)
{
    static const std::map<std::string, const ParameterSpec*> index = [] {
        std::map<std::string, const ParameterSpec*> m;
        // Drums first (released), then the others; DSP ids are identical wherever they appear.
        for (int i = 0; i < int (Product::Count); ++i)
            for (const auto& s : channelParameterSpecs (Product (i))) m.emplace (s.id, &s);
        return m;
    }();
    auto it = index.find (id);
    if (it != index.end()) return it->second;
    return findFxParameterSpec (id); // shared UI widgets look up any product's parameter by id
}

} // namespace livemix
