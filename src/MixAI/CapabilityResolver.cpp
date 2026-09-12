#include "CapabilityResolver.h"
#include "Profiles/MixProfileData.h"
#include "FX/FxParameterIDs.h"
#include "FX/FxProfiles.h"
#include "State/ParameterIDs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace
{
    std::string num (const char* fmt, double v) { char b[96]; std::snprintf (b, sizeof (b), fmt, v); return b; }
    std::string fmtDb (float v) { return num (v >= 0.0f ? "+%.1f dB" : "%.1f dB", double (v)); }
    std::string fmtHz (float hz) { return hz >= 1000.0f ? num ("%.1f kHz", double (hz) / 1000.0) : num ("%.0f Hz", double (hz)); }

    bool sustainedSource (const std::string& family)
    {
        return family == "Organ" || family == "Synth" || family == "Piano" || family == "ElectricPiano"
            || family == "Choir" || family == "Room" || family == "Overhead" || family == "AcousticGuitar";
    }

    // Collects actions for one plan, merging anything that writes the same control twice so
    // two objectives asking for the same shelf produce one move with both reasons on it.
    class Sink
    {
    public:
        explicit Sink (ProcessingPlan& p) : plan (p) {}

        void write (const MixTargetRef& target, DspProcessor processor, const std::string& paramId,
                    float value, float previous, MixObjectiveType objective, int intentIndex,
                    Confidence confidence, ResolutionStatus resolution, std::string reason, std::string note = {})
        {
            for (auto& a : plan.actions)
            {
                if (! (a.target == target) || a.paramId != paramId) continue;
                a.value = value;
                if (! reason.empty() && a.reason.find (reason) == std::string::npos)
                    a.reason += a.reason.empty() ? reason : " " + reason;
                if (resolution != ResolutionStatus::Exact) a.resolution = resolution;
                if (! note.empty() && a.note.empty()) a.note = std::move (note);
                return;
            }
            MixAction a;
            a.target = target;
            a.processor = processor;
            a.paramId = paramId;
            a.value = value;
            a.previousValue = previous;
            a.objective = objective;
            a.intentIndex = intentIndex;
            a.confidence = confidence;
            a.resolution = resolution;
            a.reason = std::move (reason);
            a.note = std::move (note);
            plan.actions.push_back (std::move (a));
        }

        // The value a later objective should build on: what an earlier one already wrote here,
        // or the baseline when nothing has touched it. Keeps two objectives on one shelf additive.
        float current (const MixTargetRef& target, const std::string& paramId, float fallback) const
        {
            for (const auto& a : plan.actions)
                if (a.target == target && a.paramId == paramId) return a.value;
            return fallback;
        }

        void unsupported (std::string what) { plan.unsupported.push_back (std::move (what)); }

    private:
        ProcessingPlan& plan;
    };

    // ---- Which EQ band serves which objective -------------------------------------------
    // The tone EQ's layout is fixed (low shelf, two peaks, high shelf), so an objective always
    // lands on the band that role belongs to. That means a reasoning move and a hand edit
    // afterwards reach for the same control, and it means two passes never fight over a band.
    struct BandSlot { const char* prefix; int index; FilterType type; };

    BandSlot toneSlotFor (MixObjectiveType t)
    {
        switch (t)
        {
            case MixObjectiveType::Body:
            case MixObjectiveType::Warmth:     return { "toneEq", 0, FilterType::LowShelf };
            case MixObjectiveType::Clarity:    return { "toneEq", 1, FilterType::Peak };
            case MixObjectiveType::Presence:   return { "toneEq", 2, FilterType::Peak };
            case MixObjectiveType::Brightness:
            default:                           return { "toneEq", 3, FilterType::HighShelf };
        }
    }

    const EQBandParams& bandOf (const ChannelParameters& p, const BandSlot& s)
    {
        return std::string (s.prefix) == "toneEq" ? p.toneBands[size_t (s.index)] : p.correctiveBands[size_t (s.index)];
    }

    // A free corrective band for a cut that makes room for a named source, or the one already
    // working nearest that frequency. Corrective bands are where Tune puts resonance cuts, so
    // a pocket cut joins them rather than displacing the source's own tone shaping.
    int pickCorrectiveBand (const ChannelParameters& p, float hz)
    {
        for (int i = 0; i < ParamID::kCorrectiveBands; ++i)
            if (! p.correctiveBands[size_t (i)].enabled) return i;
        int best = 0;
        float bestDistance = 1.0e9f;
        for (int i = 0; i < ParamID::kCorrectiveBands; ++i)
        {
            const float d = std::fabs (std::log2 (std::max (p.correctiveBands[size_t (i)].freqHz, 1.0f) / std::max (hz, 1.0f)));
            if (d < bestDistance) { bestDistance = d; best = i; }
        }
        return best;
    }

    // ---- Characters of space, and what DLIVE can really build -----------------------------
    struct CharacterRecipe
    {
        const char* word;
        ResolutionStatus resolution;
        float sizeDelta, decayScale, diffusionDelta, dampingDelta, earlyDelta, modDepth;
        float highCutOctaves, lowCutHz;
        const char* note;
    };

    // Everything here is built out of the reverb DLIVE actually has. A recipe is not a preset:
    // it is a direction the running return is shaped in, from wherever the profile left it.
    const CharacterRecipe* recipeFor (const std::string& wordIn)
    {
        static const CharacterRecipe recipes[] = {
            { "plate",   ResolutionStatus::Exact,         -10.0f,  0.00f,  15.0f,  -5.0f, -10.0f,  0.0f,  0.0f,    0.0f, "" },
            { "hall",    ResolutionStatus::Exact,          20.0f,  0.30f,   8.0f,   5.0f,  -5.0f,  0.0f,  0.0f,    0.0f, "" },
            { "room",    ResolutionStatus::Exact,         -20.0f, -0.35f,   0.0f,   5.0f,  15.0f,  0.0f,  0.0f,    0.0f, "" },
            { "ambient", ResolutionStatus::Exact,          25.0f,  0.35f,  10.0f,  10.0f, -15.0f,  0.0f, -0.5f,    0.0f, "" },
            { "chamber", ResolutionStatus::Approximated,    5.0f,  0.10f,  12.0f,  10.0f,   5.0f,  0.0f, -0.5f,    0.0f,
              "DLIVE has no chamber algorithm. Built from the plate engine with more damping and earlier reflections, which gets close." },
            { "spring",  ResolutionStatus::Approximated,  -25.0f, -0.30f,  -5.0f,  15.0f,   5.0f, 35.0f, -1.0f,  220.0f,
              "DLIVE has no spring reverb. Built from the plate engine: short, band-limited and modulated, which is most of what a spring does. "
              "It will not have a spring's boing." },
            { "gated",   ResolutionStatus::Unsupported,     0.0f,  0.00f,   0.0f,   0.0f,   0.0f,  0.0f,  0.0f,    0.0f,
              "A gated reverb needs a gate across the return, which DLIVE does not have." },
            { "shimmer", ResolutionStatus::Unsupported,     0.0f,  0.00f,   0.0f,   0.0f,   0.0f,  0.0f,  0.0f,    0.0f,
              "A shimmer reverb needs pitch shifting inside the tail, which DLIVE does not have." },
            { "reverse", ResolutionStatus::Unsupported,     0.0f,  0.00f,   0.0f,   0.0f,   0.0f,  0.0f,  0.0f,    0.0f,
              "A reverse reverb needs the tail played backwards, which DLIVE does not have." },
        };
        std::string word = wordIn;
        for (auto& c : word) c = char (std::tolower (static_cast<unsigned char> (c)));
        for (const auto& r : recipes)
            if (word.find (r.word) != std::string::npos) return &r;
        return nullptr;
    }

    // Which return already carries this source, so depth is bought on a shared space rather
    // than by instantiating a reverb per channel.
    int sendSlotFor (const StripParameters& strip, const RoutingGraph& graph, RoleFamily family, StyleProfileId profile)
    {
        int best = -1;
        float bestDb = kSilenceDb;
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (! graph.fxUsed[size_t (f)]) continue;
            if (strip.sendDb[size_t (f)] > bestDb) { bestDb = strip.sendDb[size_t (f)]; best = f; }
        }
        if (best >= 0 && bestDb > kSilenceDb + 1.0f) return best;
        // Nothing open yet: the return the profile would have chosen for this kind of source.
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (! graph.fxUsed[size_t (f)]) continue;
            if (MixProfile::defaultSendDb (profile, family, FxSlot (f)) > kSilenceDb + 1.0f) return f;
        }
        return -1;
    }
}

namespace CapabilityResolver
{

ProcessingPlan resolve (const MixIntent& intent, const Context& ctx)
{
    ProcessingPlan plan;
    if (! ctx.valid()) { plan.summary = "Nothing to resolve against."; return plan; }

    const auto& ranges = MixProfile::aiRanges (ctx.profile);
    const auto& rel = MixProfile::relationships (ctx.profile);
    Sink sink (plan);

    for (const auto& u : intent.unsupportedRequests) sink.unsupported (u);

    for (int ti = 0; ti < int (intent.targets.size()); ++ti)
    {
        const auto& target = intent.targets[size_t (ti)];
        const auto* caps = ctx.registry->find (target.target);
        if (caps == nullptr) continue;

        const std::string reasonPrefix = target.reason;
        const Confidence confidence = target.confidence;

        // ---- Effect returns: build the space out of what the engine has ----
        if (target.target.kind == MixTargetKind::FxSlot)
        {
            const int slot = target.target.index;
            if (slot < 0 || slot >= int (FxSlot::Count) || ! ctx.graph->fxUsed[size_t (slot)]) continue;
            const auto& base = ctx.baseline->fx[size_t (slot)];
            const bool runsReverb = ctx.registry->supports (target.target, DspProcessor::Reverb) && base.fx.reverbEnabled;
            const FxType type = ctx.graph->fxType[size_t (slot)];
            const auto& character = FxProfiles::baseline (ctx.profile, type);

            for (const auto& o : target.objectives)
            {
                switch (o.type)
                {
                    case MixObjectiveType::Character:
                    {
                        const auto* recipe = recipeFor (o.character);
                        if (recipe == nullptr)
                        {
                            sink.unsupported ("A \"" + o.character + "\" character was asked for on " + caps->name
                                              + ", and DLIVE has nothing that resembles it. Nothing was changed.");
                            break;
                        }
                        if (recipe->resolution == ResolutionStatus::Unsupported)
                        {
                            sink.unsupported (std::string (recipe->note) + " " + caps->name + " was left as it is.");
                            break;
                        }
                        if (! runsReverb)
                        {
                            sink.unsupported (caps->name + " runs a delay, not a reverb, so a \"" + o.character
                                              + "\" character cannot be built there.");
                            break;
                        }
                        const float strength = std::max (0.3f, std::fabs (o.strength));
                        const std::string why = reasonPrefix.empty()
                            ? caps->name + " reshaped towards a " + o.character + " space."
                            : reasonPrefix;
                        auto shape = [&] (const char* id, DspProcessor proc, float value, float previous)
                        {
                            sink.write (target.target, proc, id, value, previous, MixObjectiveType::Character, ti,
                                        confidence, recipe->resolution, why, recipe->note);
                        };
                        shape (FxParamID::rvSize, DspProcessor::Reverb,
                               std::clamp (base.fx.reverbSize + recipe->sizeDelta * strength, 0.0f, 100.0f), base.fx.reverbSize);
                        shape (FxParamID::rvDecay, DspProcessor::Reverb,
                               std::max (0.2f, base.fx.reverbDecayS * (1.0f + recipe->decayScale * strength)), base.fx.reverbDecayS);
                        shape (FxParamID::rvDiffusion, DspProcessor::Reverb,
                               std::clamp (base.fx.reverbDiffusion + recipe->diffusionDelta * strength, 0.0f, 100.0f), base.fx.reverbDiffusion);
                        shape (FxParamID::rvDamping, DspProcessor::Reverb,
                               std::clamp (base.fx.reverbDamping + recipe->dampingDelta * strength, 0.0f, 100.0f), base.fx.reverbDamping);
                        shape (FxParamID::rvEarly, DspProcessor::Reverb,
                               std::clamp (base.fx.reverbEarly + recipe->earlyDelta * strength, 0.0f, 100.0f), base.fx.reverbEarly);
                        if (recipe->modDepth > 0.0f)
                            shape (FxParamID::rvModDepth, DspProcessor::Reverb,
                                   std::clamp (base.fx.reverbModDepth + recipe->modDepth * strength, 0.0f, 100.0f), base.fx.reverbModDepth);
                        if (recipe->highCutOctaves != 0.0f)
                            shape (FxParamID::rvHighCut, DspProcessor::Reverb,
                                   std::clamp (base.fx.reverbHighCutHz * std::exp2 (recipe->highCutOctaves * strength), 1000.0f, 20000.0f),
                                   base.fx.reverbHighCutHz);
                        if (recipe->lowCutHz > 0.0f)
                            shape (FxParamID::rvLowCut, DspProcessor::Reverb,
                                   std::clamp (recipe->lowCutHz, 20.0f, 1000.0f), base.fx.reverbLowCutHz);
                        break;
                    }
                    case MixObjectiveType::SpatialDepth:
                    {
                        if (! runsReverb) break;
                        // The FX profile's own decay is the return's character and stays the ceiling
                        // when lengthening: a plate asked to be a hall becomes a wash, not a hall.
                        const float wanted = base.fx.reverbDecayS * (1.0f + ranges.reverbDecayScale * o.strength);
                        const float value = std::clamp (wanted, 0.3f, std::max (character.reverbDecayS, base.fx.reverbDecayS));
                        sink.write (target.target, DspProcessor::Reverb, FxParamID::rvDecay, value, base.fx.reverbDecayS,
                                    o.type, ti, confidence, ResolutionStatus::Exact,
                                    reasonPrefix.empty() ? caps->name + " tail set to " + num ("%.1f s", double (value)) + "." : reasonPrefix);
                        if (o.preserveArticulation)
                            sink.write (target.target, DspProcessor::Reverb, FxParamID::rvPreDelay,
                                        std::clamp (base.fx.reverbPreDelayMs + ranges.articulationPreDelayMs, 0.0f, 250.0f),
                                        base.fx.reverbPreDelayMs, o.type, ti, confidence, ResolutionStatus::Exact,
                                        "Pre-delay opened up so the tail arrives after the word rather than over it.");
                        break;
                    }
                    case MixObjectiveType::Brightness:
                        if (! runsReverb) break;
                        sink.write (target.target, DspProcessor::Reverb, FxParamID::rvHighCut,
                                    std::clamp (base.fx.reverbHighCutHz * std::exp2 (ranges.reverbHighCutOctaves * o.strength), 1000.0f, 20000.0f),
                                    base.fx.reverbHighCutHz, o.type, ti, confidence, ResolutionStatus::Exact,
                                    reasonPrefix.empty() ? caps->name + (o.strength > 0.0f ? " tail opened up on top." : " tail darkened.") : reasonPrefix);
                        break;
                    case MixObjectiveType::Warmth:
                        if (! runsReverb) break;
                        sink.write (target.target, DspProcessor::Reverb, FxParamID::rvDamping,
                                    std::clamp (base.fx.reverbDamping + ranges.reverbDampingDelta * o.strength, 0.0f, 100.0f),
                                    base.fx.reverbDamping, o.type, ti, confidence, ResolutionStatus::Exact,
                                    reasonPrefix.empty() ? caps->name + " tail warmed by damping its top end as it decays." : reasonPrefix);
                        break;
                    case MixObjectiveType::Level:
                        sink.write (target.target, DspProcessor::FxReturn, "returnDb",
                                    base.returnDb + ranges.faderDb * o.strength, base.returnDb,
                                    o.type, ti, confidence, ResolutionStatus::Exact,
                                    reasonPrefix.empty() ? caps->name + " return " + fmtDb (ranges.faderDb * o.strength) + "." : reasonPrefix);
                        break;
                    default:
                        sink.unsupported (std::string (mixObjectiveLabel (o.type)) + " is not something an effect return can be asked for.");
                        break;
                }
            }
            continue;
        }

        // ---- Strips and buses ----
        const bool isBus = target.target.kind == MixTargetKind::Bus;
        const int index = target.target.index;
        if (isBus && (index < 0 || index >= int (MixBus::Count))) continue;
        if (! isBus && (index < 0 || index >= ctx.baseline->numStrips)) continue;

        const ChannelParameters& channel = isBus ? ctx.baseline->buses[size_t (index)].channel
                                                 : ctx.baseline->strips[size_t (index)].channel;
        const float baseFader = isBus ? ctx.baseline->buses[size_t (index)].faderDb
                                      : ctx.baseline->strips[size_t (index)].faderDb;
        const MixContextTrack* track = nullptr;
        if (! isBus)
            for (const auto& t : ctx.mix->tracks) if (t.id == index) track = &t;

        // A source that did not really play is not tuned, whatever the reasoning layer thinks
        // it heard. This is the same rule the deterministic planner works to.
        if (track != nullptr && (! track->heard || track->faint || track->bleedOnly))
        {
            sink.unsupported (caps->name + " was not playing during the listen, so nothing was changed on it.");
            continue;
        }

        const RoleFamily family = isBus ? roleFamily (busRole (MixBus (index), MixPurpose::ChurchBroadcast))
                                        : roleFamily (ctx.graph->strips[size_t (index)].role);

        for (const auto& o : target.objectives)
        {
            const std::string why = reasonPrefix;
            switch (o.type)
            {
                case MixObjectiveType::Body:
                case MixObjectiveType::Warmth:
                case MixObjectiveType::Clarity:
                case MixObjectiveType::Presence:
                case MixObjectiveType::Brightness:
                {
                    if (! ctx.registry->supports (target.target, DspProcessor::ToneEq))
                    {
                        sink.unsupported (caps->name + " has no tone EQ, so its " + mixObjectiveLabel (o.type) + " could not be shaped.");
                        break;
                    }
                    const BandSlot slotDef = toneSlotFor (o.type);
                    const auto& band = bandOf (channel, slotDef);
                    float hz = slotDef.type == FilterType::Peak ? (o.type == MixObjectiveType::Clarity ? ranges.clarityHz : ranges.presenceHz)
                                                                : (o.type == MixObjectiveType::Body ? ranges.bodyHz
                                                                   : o.type == MixObjectiveType::Warmth ? ranges.warmthHz : ranges.brightnessHz);
                    float q = o.type == MixObjectiveType::Clarity ? ranges.clarityQ
                            : o.type == MixObjectiveType::Presence ? ranges.presenceQ : band.q;
                    // Clarity is a cut: asking for more of it takes the mud out rather than
                    // adding anything, which is the move that actually makes a source clearer.
                    const float perUnit = o.type == MixObjectiveType::Body ? ranges.bodyDb
                                        : o.type == MixObjectiveType::Warmth ? ranges.warmthDb
                                        : o.type == MixObjectiveType::Clarity ? -ranges.clarityDb
                                        : o.type == MixObjectiveType::Presence ? ranges.presenceDb : ranges.brightnessDb;
                    const std::string gainId = eqBandId (slotDef.prefix, slotDef.index, "Gain");
                    const float from = sink.current (target.target, gainId, band.gainDb);
                    const float value = from + perUnit * o.strength;
                    // The band already in use keeps its own frequency: it is Tune's decision
                    // about this source, and an objective is a refinement of it, not a reset.
                    if (band.enabled) { hz = band.freqHz; q = band.q; }

                    const std::string sentence = why.empty()
                        ? caps->name + ": " + std::string (mixObjectiveLabel (o.type)) + " "
                          + (o.strength > 0.0f ? "up " : "down ") + fmtDb (perUnit * o.strength) + " at " + fmtHz (hz) + "."
                        : why;
                    sink.write (target.target, DspProcessor::ToneEq, ParamID::toneEqOn, 1.0f, channel.toneEqEnabled ? 1.0f : 0.0f,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::ToneEq, eqBandId (slotDef.prefix, slotDef.index, "On"), 1.0f,
                                band.enabled ? 1.0f : 0.0f, o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::ToneEq, eqBandId (slotDef.prefix, slotDef.index, "Type"),
                                float (int (slotDef.type)), float (int (band.type)), o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::ToneEq, eqBandId (slotDef.prefix, slotDef.index, "Freq"), hz, band.freqHz,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::ToneEq, eqBandId (slotDef.prefix, slotDef.index, "Q"), q, band.q,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::ToneEq, gainId, value, band.gainDb,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    break;
                }

                case MixObjectiveType::Separation:
                {
                    const auto* other = ctx.registry->find (o.against);
                    if (other == nullptr) break;
                    if (! ctx.registry->supports (target.target, DspProcessor::CorrectiveEq))
                    {
                        sink.unsupported (caps->name + " has no corrective EQ, so no room could be made for " + other->name + ".");
                        break;
                    }
                    // DLIVE has no dynamic EQ. A narrow static cut in the other source's pocket
                    // does most of the same work here, and the honest word for that is
                    // "approximated" - a static cut is there when the voice is not.
                    const int band = pickCorrectiveBand (channel, rel.vocalPocketHz);
                    const auto& b = channel.correctiveBands[size_t (band)];
                    const float cut = -ranges.separationCutDb * std::fabs (o.strength);
                    const float q = o.preserveArticulation ? rel.vocalPocketQ * 1.5f : rel.vocalPocketQ;
                    const std::string sentence = why.empty()
                        ? caps->name + " stepped back " + fmtDb (cut) + " at " + fmtHz (rel.vocalPocketHz)
                          + " so " + other->name + " has that band to itself."
                        : why;
                    const char* note = "DLIVE has no dynamic EQ, so this is a narrow static cut in the pocket. It is there "
                                       "whether or not the other source is, which is why it is small.";
                    auto w = [&] (const char* field, float value, float previous)
                    {
                        sink.write (target.target, DspProcessor::CorrectiveEq, eqBandId ("corrEq", band, field), value, previous,
                                    o.type, ti, confidence, ResolutionStatus::Approximated, sentence, note);
                    };
                    sink.write (target.target, DspProcessor::CorrectiveEq, ParamID::corrEqOn, 1.0f,
                                channel.correctiveEqEnabled ? 1.0f : 0.0f, o.type, ti, confidence,
                                ResolutionStatus::Approximated, sentence, note);
                    w ("On", 1.0f, b.enabled ? 1.0f : 0.0f);
                    w ("Type", float (int (FilterType::Peak)), float (int (b.type)));
                    w ("Freq", rel.vocalPocketHz, b.freqHz);
                    w ("Q", q, b.q);
                    w ("Gain", std::min (b.enabled ? b.gainDb + cut : cut, 0.0f), b.gainDb);
                    break;
                }

                case MixObjectiveType::DynamicStability:
                {
                    if (! ctx.registry->supports (target.target, DspProcessor::Compressor)) break;
                    const std::string sentence = why.empty()
                        ? caps->name + (o.strength > 0.0f ? " held steadier: " : " let breathe: ")
                          + "threshold " + fmtDb (-ranges.compThresholdDb * o.strength) + "."
                        : why;
                    if (o.preserveTransients && o.strength > 0.0f)
                    {
                        // Steadier without losing the hit: the attack is opened up rather than
                        // the threshold pushed further down, so the front of the note still gets out.
                        sink.write (target.target, DspProcessor::Compressor, ParamID::compAttack,
                                    std::clamp (channel.compAttackMs * (1.0f + 0.6f * o.strength), 0.1f, 200.0f),
                                    channel.compAttackMs, o.type, ti, confidence, ResolutionStatus::Exact,
                                    sentence + " The attack was opened up instead of the threshold pushed down, so the transient survives.");
                    }
                    sink.write (target.target, DspProcessor::Compressor, ParamID::compOn, 1.0f, channel.compEnabled ? 1.0f : 0.0f,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::Compressor, ParamID::compThreshold,
                                channel.compThresholdDb - ranges.compThresholdDb * o.strength, channel.compThresholdDb,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::Compressor, ParamID::compRatio,
                                std::max (1.0f, channel.compRatio + ranges.compRatioDelta * o.strength), channel.compRatio,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    break;
                }

                case MixObjectiveType::Punch:
                {
                    if (! ctx.registry->supports (target.target, DspProcessor::Transient)) break;
                    const float attack = std::clamp (channel.transientAttack + ranges.transientAttack * o.strength, -1.0f, 1.0f);
                    const std::string sentence = why.empty()
                        ? caps->name + (o.strength > 0.0f ? " given more attack." : " softened at the front of each hit.") : why;
                    sink.write (target.target, DspProcessor::Transient, ParamID::transOn, 1.0f, channel.transientEnabled ? 1.0f : 0.0f,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::Transient, ParamID::transAttack, attack, channel.transientAttack,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    break;
                }

                case MixObjectiveType::Cleanup:
                {
                    if (! ctx.registry->supports (target.target, DspProcessor::Gate)) break;
                    if (isBus || sustainedSource (track != nullptr ? track->family : std::string()))
                    {
                        sink.unsupported (caps->name + " holds notes rather than striking them, so an expander would chop it. "
                                                       "Nothing was gated.");
                        break;
                    }
                    const float range = std::clamp (channel.gateRangeDb + ranges.gateRangeDb * o.strength, 0.0f, 60.0f);
                    const std::string sentence = why.empty()
                        ? caps->name + (o.strength > 0.0f ? " cleaned up between hits." : " allowed more of the room between hits.") : why;
                    sink.write (target.target, DspProcessor::Gate, ParamID::gateOn, range > 1.0f ? 1.0f : 0.0f,
                                channel.gateEnabled ? 1.0f : 0.0f, o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::Gate, ParamID::gateRange, range, channel.gateRangeDb,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    break;
                }

                case MixObjectiveType::Sibilance:
                {
                    if (! ctx.registry->supports (target.target, DspProcessor::DeEsser))
                    {
                        sink.unsupported (caps->name + " has no de-esser: DLIVE fits one to voices, and this is not one.");
                        break;
                    }
                    const float range = std::clamp (channel.deEssRangeDb + ranges.deEssRangeDb * o.strength, 0.0f, 12.0f);
                    const std::string sentence = why.empty()
                        ? caps->name + (o.strength > 0.0f ? " de-essed further." : " given its S sounds back.") : why;
                    sink.write (target.target, DspProcessor::DeEsser, ParamID::deEssOn, range > 0.5f ? 1.0f : 0.0f,
                                channel.deEssEnabled ? 1.0f : 0.0f, o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::DeEsser, ParamID::deEssRange, range, channel.deEssRangeDb,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    break;
                }

                case MixObjectiveType::SpatialDepth:
                {
                    if (isBus)
                    {
                        sink.unsupported (std::string (mixBusName (MixBus (index)))
                                          + " has no sends: depth is bought on the sources that feed it.");
                        break;
                    }
                    const auto& strip = ctx.baseline->strips[size_t (index)];
                    const int slot = sendSlotFor (strip, *ctx.graph, family, ctx.profile);
                    if (slot < 0)
                    {
                        sink.unsupported ("No effect return is running that suits " + caps->name + ", so no depth could be added.");
                        break;
                    }
                    const float from = strip.sendDb[size_t (slot)] <= kSilenceDb + 1.0f
                                           ? MixProfile::defaultSendDb (ctx.profile, family, FxSlot (slot))
                                           : strip.sendDb[size_t (slot)];
                    const float value = std::clamp (from + ranges.sendDb * o.strength, -40.0f, 0.0f);
                    std::string sentence = why.empty()
                        ? caps->name + (o.strength > 0.0f ? " sent further into " : " pulled forward out of ")
                          + fxSlotName (FxSlot (slot)) + " (" + fmtDb (value - from) + ")."
                        : why;
                    if (o.preserveArticulation && o.strength > 0.0f)
                        sentence += " The return's pre-delay carries the depth, so the words stay in front of it.";
                    sink.write (target.target, DspProcessor::Send, fxSlotName (FxSlot (slot)), value, strip.sendDb[size_t (slot)],
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    if (o.preserveArticulation && o.strength > 0.0f)
                    {
                        const MixTargetRef fxTarget { MixTargetKind::FxSlot, slot };
                        const auto& fx = ctx.baseline->fx[size_t (slot)];
                        if (fx.fx.reverbEnabled)
                            sink.write (fxTarget, DspProcessor::Reverb, FxParamID::rvPreDelay,
                                        std::clamp (fx.fx.reverbPreDelayMs + ranges.articulationPreDelayMs, 0.0f, 250.0f),
                                        fx.fx.reverbPreDelayMs, o.type, ti, confidence, ResolutionStatus::Exact,
                                        std::string (fxSlotName (FxSlot (slot)))
                                        + " pre-delay opened up so the tail arrives after the consonant, not over it.");
                    }
                    break;
                }

                case MixObjectiveType::Width:
                {
                    if (! ctx.registry->supports (target.target, DspProcessor::Width))
                    {
                        sink.unsupported (caps->name + " arrives mono, so there is no stereo image to widen. "
                                                       "Nothing was changed - a mono source cannot be made wide by a width control.");
                        break;
                    }
                    const float value = std::clamp (channel.widthAmount + ranges.widthDelta * o.strength, 0.0f, 2.0f);
                    const std::string sentence = why.empty()
                        ? caps->name + (o.strength > 0.0f ? " opened out wider." : " brought in towards the centre.") : why;
                    sink.write (target.target, DspProcessor::Width, ParamID::widthOn, 1.0f, channel.widthEnabled ? 1.0f : 0.0f,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    sink.write (target.target, DspProcessor::Width, ParamID::widthAmount, value, channel.widthAmount,
                                o.type, ti, confidence, ResolutionStatus::Exact, sentence);
                    break;
                }

                case MixObjectiveType::Level:
                {
                    const float value = baseFader + ranges.faderDb * o.strength;
                    sink.write (target.target, DspProcessor::Fader, "faderDb", value, baseFader,
                                o.type, ti, confidence, ResolutionStatus::Exact,
                                why.empty() ? caps->name + " " + fmtDb (value - baseFader) + " against the rest of the mix." : why);
                    break;
                }

                case MixObjectiveType::Character:
                    sink.unsupported ("A character was asked for on " + caps->name
                                      + ", which is a channel rather than an effect return. Characters belong to the returns.");
                    break;

                case MixObjectiveType::Count:
                default:
                    break;
            }
        }
    }

    plan.valid = true;
    const int applied = int (plan.actions.size());
    if (intent.noChangeRequired && applied == 0) plan.summary = "No change required.";
    else if (applied == 0) plan.summary = "Nothing in the intent could be built with the processors this session has.";
    else plan.summary = intent.summary;
    return plan;
}

} // namespace CapabilityResolver
} // namespace livemix
