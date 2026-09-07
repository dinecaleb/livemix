#include "FxParameterSpecs.h"
#include "FxParameters.h"
#include "Core/StyleId.h"
#include <map>

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

    std::vector<ParameterSpec> build()
    {
        using namespace FxParamID;
        std::vector<ParameterSpec> v;

        std::vector<std::string> types, styles, modes, divisions;
        for (auto* n : kFxTypeNames) types.push_back (n);
        for (auto* n : kStyleProfileNames) styles.push_back (n);
        for (auto* n : kDelayModeNames) modes.push_back (n);
        for (auto* n : kNoteDivisionNames) divisions.push_back (n);

        auto type_ = c (fxType, "Type", types, int (FxType::VocalPlate)); type_.automatable = false; v.push_back (type_);
        auto prof_ = c (ParamID::profile, "Profile", styles, int (StyleProfileId::ModernGospel)); prof_.automatable = false; v.push_back (prof_);
        v.push_back (b (ParamID::bypass, "A/B Original", false));
        v.push_back (b (ParamID::abMatch, "A/B Loudness Match", true));
        auto live_ = b (ParamID::liveSafe, "Live Safe", false); live_.automatable = false; v.push_back (live_);

        v.push_back (f (space, "Space", 0.0f, 100.0f, 50.0f));
        v.push_back (f (length, "Length", 0.0f, 100.0f, 50.0f));
        v.push_back (f (warmth, "Warmth", 0.0f, 100.0f, 50.0f));
        v.push_back (f (clarity, "Clarity", 0.0f, 100.0f, 50.0f));
        v.push_back (f (distance, "Distance", 0.0f, 100.0f, 50.0f));

        FxParameters d; // neutral defaults for the DSP fields

        v.push_back (f (inputTrim, "Input Trim", -24.0f, 24.0f, d.inputTrimDb, "dB"));

        v.push_back (b (rvOn, "Reverb", d.reverbEnabled));
        v.push_back (f (rvDecay, "Reverb Decay", 0.2f, 20.0f, d.reverbDecayS, "s", 2.5f));
        v.push_back (f (rvPreDelay, "Reverb Pre-Delay", 0.0f, 250.0f, d.reverbPreDelayMs, "ms", 40.0f));
        v.push_back (f (rvSize, "Reverb Size", 0.0f, 100.0f, d.reverbSize, "%"));
        v.push_back (f (rvDamping, "Reverb Damping", 0.0f, 100.0f, d.reverbDamping, "%"));
        v.push_back (f (rvDiffusion, "Reverb Diffusion", 0.0f, 100.0f, d.reverbDiffusion, "%"));
        v.push_back (f (rvLowCut, "Reverb Low Cut", 20.0f, 1000.0f, d.reverbLowCutHz, "Hz", 150.0f));
        v.push_back (f (rvHighCut, "Reverb High Cut", 1000.0f, 20000.0f, d.reverbHighCutHz, "Hz", 6000.0f));
        v.push_back (f (rvModRate, "Reverb Mod Rate", 0.1f, 5.0f, d.reverbModRateHz, "Hz", 1.0f));
        v.push_back (f (rvModDepth, "Reverb Mod Depth", 0.0f, 100.0f, d.reverbModDepth, "%"));
        v.push_back (f (rvEarly, "Reverb Early Reflections", 0.0f, 100.0f, d.reverbEarly, "%"));
        v.push_back (f (rvLevel, "Reverb Level", -40.0f, 12.0f, d.reverbLevelDb, "dB"));

        v.push_back (b (dlOn, "Delay", d.delayEnabled));
        v.push_back (c (dlMode, "Delay Mode", modes, d.delayMode));
        v.push_back (b (dlSync, "Delay Sync", d.delaySync));
        v.push_back (f (dlTime, "Delay Time", 1.0f, 2000.0f, d.delayTimeMs, "ms", 250.0f));
        v.push_back (c (dlDivision, "Delay Division", divisions, d.delayDivision));
        v.push_back (f (dlOffset, "Delay Stereo Offset", -50.0f, 50.0f, d.delayOffset, "%"));
        v.push_back (f (dlFeedback, "Delay Feedback", 0.0f, 95.0f, d.delayFeedback, "%"));
        v.push_back (f (dlLowCut, "Delay Low Cut", 20.0f, 1000.0f, d.delayLowCutHz, "Hz", 150.0f));
        v.push_back (f (dlHighCut, "Delay High Cut", 1000.0f, 20000.0f, d.delayHighCutHz, "Hz", 5000.0f));
        v.push_back (f (dlWidth, "Delay Width", 0.0f, 100.0f, d.delayWidth, "%"));
        v.push_back (f (dlDuck, "Delay Ducking", 0.0f, 100.0f, d.delayDuck, "%"));
        v.push_back (f (dlDuckRelease, "Delay Duck Release", 50.0f, 2000.0f, d.delayDuckReleaseMs, "ms", 400.0f));
        v.push_back (f (dlModRate, "Delay Mod Rate", 0.1f, 5.0f, d.delayModRateHz, "Hz", 1.0f));
        v.push_back (f (dlModDepth, "Delay Mod Depth", 0.0f, 100.0f, d.delayModDepth, "%"));
        v.push_back (f (dlToReverb, "Delay To Reverb", 0.0f, 100.0f, d.delayToReverb, "%"));
        v.push_back (f (dlLevel, "Delay Level", -40.0f, 12.0f, d.delayLevelDb, "dB"));

        v.push_back (f (mix, "Mix", 0.0f, 1.0f, d.mix));
        v.push_back (f (outputTrim, "Output Trim", -24.0f, 24.0f, d.outputTrimDb, "dB"));
        return v;
    }
}

const std::vector<ParameterSpec>& fxParameterSpecs()
{
    static const std::vector<ParameterSpec> specs = build();
    return specs;
}

const ParameterSpec* findFxParameterSpec (const std::string& id)
{
    static const std::map<std::string, const ParameterSpec*> index = [] {
        std::map<std::string, const ParameterSpec*> m;
        for (const auto& s : fxParameterSpecs()) m[s.id] = &s;
        return m;
    }();
    auto it = index.find (id);
    return it == index.end() ? nullptr : it->second;
}

} // namespace livemix
