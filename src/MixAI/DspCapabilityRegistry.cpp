#include "DspCapabilityRegistry.h"
#include "FX/FxParameterSpecs.h"
#include "FX/FxParameterIDs.h"
#include "State/ParameterIDs.h"
#include "Profiles/MixProfileData.h"
#include <algorithm>

namespace livemix
{

namespace
{
    struct ProcessorNames { const char* id; const char* label; };

    constexpr ProcessorNames kProcessorNames[int (DspProcessor::Count)] = {
        { "inputGain",    "Input gain" },
        { "highPass",     "High-pass filter" },
        { "lowPass",      "Low-pass filter" },
        { "gate",         "Gate / expander" },
        { "correctiveEq", "Corrective EQ" },
        { "deEsser",      "De-esser" },
        { "compressor",   "Compressor" },
        { "transient",    "Transient shaper" },
        { "toneEq",       "Tone EQ" },
        { "saturation",   "Saturation" },
        { "width",        "Stereo width" },
        { "limiter",      "Limiter" },
        { "fader",        "Fader" },
        { "pan",          "Pan" },
        { "send",         "FX send" },
        { "reverb",       "Reverb" },
        { "delay",        "Delay" },
        { "fxReturn",     "FX return" },
    };

    // The parameter ids each processor owns, in the engine's own names. EQ bands are added
    // separately because the band count comes from ParamID and the ids are built per band.
    const std::vector<std::string>& channelParamIds (DspProcessor p)
    {
        using namespace ParamID;
        static const std::vector<std::string> none {};
        static const std::vector<std::string> hpf   { hpfOn, hpfFreq, hpfSlope };
        static const std::vector<std::string> lpf   { lpfOn, lpfFreq, lpfSlope };
        static const std::vector<std::string> gate  { gateOn, gateThreshold, gateRange, gateAttack, gateHold, gateRelease,
                                                      gateHysteresis, gateRatio, gateScHpf };
        static const std::vector<std::string> deEss { deEssOn, deEssFreq, deEssThreshold, deEssRange };
        static const std::vector<std::string> comp  { compOn, compThreshold, compRatio, compAttack, compRelease, compKnee,
                                                      compMakeup, compMix, compScHpf };
        static const std::vector<std::string> trans { transOn, transAttack, transSustain };
        static const std::vector<std::string> sat   { satOn, satDrive, satMix };
        static const std::vector<std::string> width { widthOn, widthAmount, widthMonoBelow };
        static const std::vector<std::string> lim   { limiterOn, limiterCeiling, limiterRelease };
        switch (p)
        {
            case DspProcessor::HighPass:     return hpf;
            case DspProcessor::LowPass:      return lpf;
            case DspProcessor::Gate:         return gate;
            case DspProcessor::DeEsser:      return deEss;
            case DspProcessor::Compressor:   return comp;
            case DspProcessor::Transient:    return trans;
            case DspProcessor::Saturation:   return sat;
            case DspProcessor::Width:        return width;
            case DspProcessor::Limiter:      return lim;
            default:                         return none;
        }
    }

    DspParameterCapability fromSpec (const ParameterSpec& s)
    {
        DspParameterCapability c;
        c.id = s.id;
        c.name = s.name;
        c.unit = s.unit;
        c.type = s.type;
        c.minValue = s.minValue;
        c.maxValue = s.maxValue;
        c.defaultValue = s.defaultValue;
        c.choices = s.choices;
        return c;
    }

    // A control the mix owns rather than the channel chain: a fader, a pan, a send. These
    // have no entry in the plug-in parameter tables because they are not plug-in parameters,
    // so their ranges are stated here, once, next to everything else the registry publishes.
    DspParameterCapability mixParam (const char* id, const char* name, const char* unit, float lo, float hi, float def)
    {
        DspParameterCapability c;
        c.id = id;
        c.name = name;
        c.unit = unit;
        c.type = ParameterSpec::Type::Float;
        c.minValue = lo;
        c.maxValue = hi;
        c.defaultValue = def;
        return c;
    }

    void addChannelProcessors (DspTargetCapabilities& t, Product product, bool stereo, bool master)
    {
        auto addFromSpecs = [&] (DspProcessor p, bool available, const char* why)
        {
            DspProcessorCapability cap;
            cap.processor = p;
            cap.available = available;
            if (! available) cap.unavailableBecause = why;
            for (const auto& id : channelParamIds (p))
            {
                if (! productUsesParameter (product, id)) continue;
                const ParameterSpec* spec = findParameterSpec (id);
                if (spec != nullptr) cap.parameters.push_back (fromSpec (*spec));
            }
            // A stage this kind of source does not carry is reported as unavailable with the
            // reason, never left out of the table: a processor that is silently missing is a
            // processor a reasoning layer will keep asking for and a user will never understand.
            if (cap.parameters.empty() && available)
            {
                cap.available = false;
                cap.unavailableBecause = std::string ("DLIVE does not fit a ") + dspProcessorLabel (p)
                                       + " to this kind of source.";
            }
            t.processors.push_back (std::move (cap));
        };

        addFromSpecs (DspProcessor::HighPass, true, nullptr);
        addFromSpecs (DspProcessor::LowPass, true, nullptr);
        addFromSpecs (DspProcessor::Gate, true, nullptr);

        // The two EQs: one card per band, with the band's own four ids.
        auto addEq = [&] (DspProcessor p, const char* onId, const char* prefix, int bands)
        {
            if (! productUsesParameter (product, onId)) return;
            DspProcessorCapability cap;
            cap.processor = p;
            cap.available = true;
            if (const ParameterSpec* on = findParameterSpec (onId)) cap.parameters.push_back (fromSpec (*on));
            for (int b = 0; b < bands; ++b)
                for (const char* field : { "On", "Type", "Freq", "Gain", "Q" })
                    if (const ParameterSpec* s = findParameterSpec (eqBandId (prefix, b, field)))
                        cap.parameters.push_back (fromSpec (*s));
            if (! cap.parameters.empty()) t.processors.push_back (std::move (cap));
        };
        addEq (DspProcessor::CorrectiveEq, ParamID::corrEqOn, "corrEq", ParamID::kCorrectiveBands);
        addFromSpecs (DspProcessor::DeEsser, true, nullptr);
        addFromSpecs (DspProcessor::Compressor, true, nullptr);
        addFromSpecs (DspProcessor::Transient, true, nullptr);
        addEq (DspProcessor::ToneEq, ParamID::toneEqOn, "toneEq", ParamID::kToneBands);
        addFromSpecs (DspProcessor::Saturation, true, nullptr);

        // Width is mid/side: on a mono source there is no side to scale, so the stage exists
        // but has nothing to do. Said plainly rather than offered and quietly ignored.
        addFromSpecs (DspProcessor::Width, stereo,
                      "This source arrives mono, so there is no stereo image to widen.");

        // MixEngine turns the limiter stage on for the master bus and nowhere else.
        addFromSpecs (DspProcessor::Limiter, master,
                      "DLIVE runs the limiter on the master bus only, where it holds the broadcast ceiling.");
    }
}

const char* dspProcessorId (DspProcessor p) noexcept
{
    const int i = int (p);
    return (i >= 0 && i < int (DspProcessor::Count)) ? kProcessorNames[i].id : "unknown";
}

const char* dspProcessorLabel (DspProcessor p) noexcept
{
    const int i = int (p);
    return (i >= 0 && i < int (DspProcessor::Count)) ? kProcessorNames[i].label : "Unknown";
}

DspProcessor dspProcessorFromId (const std::string& id) noexcept
{
    for (int i = 0; i < int (DspProcessor::Count); ++i)
        if (id == kProcessorNames[i].id) return DspProcessor (i);
    return DspProcessor::Count;
}

std::string MixTargetRef::id() const
{
    switch (kind)
    {
        case MixTargetKind::Strip:  return "strip:" + std::to_string (index);
        case MixTargetKind::Bus:    return std::string ("bus:") + (index >= 0 && index < int (MixBus::Count) ? mixBusName (MixBus (index)) : "?");
        case MixTargetKind::FxSlot: return std::string ("fx:") + (index >= 0 && index < int (FxSlot::Count) ? fxSlotName (FxSlot (index)) : "?");
        case MixTargetKind::Count:
        default:                    return "?";
    }
}

MixTargetRef MixTargetRef::parse (const std::string& text)
{
    MixTargetRef r;
    const auto colon = text.find (':');
    if (colon == std::string::npos) return r;
    const std::string kind = text.substr (0, colon);
    const std::string rest = text.substr (colon + 1);
    if (kind == "strip")
    {
        r.kind = MixTargetKind::Strip;
        r.index = rest.empty() ? -1 : std::atoi (rest.c_str());
        if (r.index < 0) r.index = -1;
    }
    else if (kind == "bus")
    {
        r.kind = MixTargetKind::Bus;
        for (int b = 0; b < int (MixBus::Count); ++b) if (rest == mixBusName (MixBus (b))) r.index = b;
    }
    else if (kind == "fx")
    {
        r.kind = MixTargetKind::FxSlot;
        for (int f = 0; f < int (FxSlot::Count); ++f) if (rest == fxSlotName (FxSlot (f))) r.index = f;
    }
    return r;
}

const DspParameterCapability* DspProcessorCapability::find (const std::string& paramId) const noexcept
{
    for (const auto& p : parameters) if (p.id == paramId) return &p;
    return nullptr;
}

const DspProcessorCapability* DspTargetCapabilities::find (DspProcessor p) const noexcept
{
    for (const auto& c : processors) if (c.processor == p) return &c;
    return nullptr;
}

std::vector<DspProcessor> activeProcessors (const ChannelParameters& p, bool stereo, bool master)
{
    std::vector<DspProcessor> out;
    auto anyBand = [] (const auto& bands) { for (const auto& b : bands) if (b.enabled) return true; return false; };
    if (p.hpfEnabled) out.push_back (DspProcessor::HighPass);
    if (p.lpfEnabled) out.push_back (DspProcessor::LowPass);
    if (p.gateEnabled) out.push_back (DspProcessor::Gate);
    if (p.correctiveEqEnabled && anyBand (p.correctiveBands)) out.push_back (DspProcessor::CorrectiveEq);
    if (p.deEssEnabled) out.push_back (DspProcessor::DeEsser);
    if (p.compEnabled) out.push_back (DspProcessor::Compressor);
    if (p.transientEnabled) out.push_back (DspProcessor::Transient);
    if (p.toneEqEnabled && anyBand (p.toneBands)) out.push_back (DspProcessor::ToneEq);
    if (p.satEnabled) out.push_back (DspProcessor::Saturation);
    if (p.widthEnabled && stereo) out.push_back (DspProcessor::Width);
    if (p.limiterEnabled && master) out.push_back (DspProcessor::Limiter);
    return out;
}

DspCapabilityRegistry DspCapabilityRegistry::build (const MixSession& session, const RoutingGraph& graph)
{
    DspCapabilityRegistry reg;
    const auto& R = MixProfile::relationships (session.profile);

    for (int i = 0; i < graph.numStrips(); ++i)
    {
        const auto& route = graph.strips[size_t (i)];
        DspTargetCapabilities t;
        t.target = { MixTargetKind::Strip, i };
        t.name = route.name;
        t.role = channelRoleName (route.role);
        t.stereo = route.numChannels() == 2;
        addChannelProcessors (t, productOf (roleFamily (route.role)), t.stereo, false);

        DspProcessorCapability gain;
        gain.processor = DspProcessor::InputGain;
        gain.parameters.push_back (mixParam ("inputGainDb", "Input gain", "dB", -R.maxInputGainDb, R.maxInputGainDb, 0.0f));
        t.processors.push_back (std::move (gain));

        DspProcessorCapability fader;
        fader.processor = DspProcessor::Fader;
        fader.parameters.push_back (mixParam ("faderDb", "Fader", "dB", -60.0f, 12.0f, 0.0f));
        t.processors.push_back (std::move (fader));

        DspProcessorCapability pan;
        pan.processor = DspProcessor::Pan;
        pan.parameters.push_back (mixParam ("pan", t.stereo ? "Balance" : "Pan", "", -1.0f, 1.0f, 0.0f));
        t.processors.push_back (std::move (pan));

        DspProcessorCapability send;
        send.processor = DspProcessor::Send;
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (! graph.fxUsed[size_t (f)]) continue;
            // A send is either off (kSilenceDb) or a level; the resolver never invents a
            // return that this session does not run.
            send.parameters.push_back (mixParam (fxSlotName (FxSlot (f)), fxSlotName (FxSlot (f)), "dB", kSilenceDb, 0.0f, kSilenceDb));
        }
        send.available = ! send.parameters.empty();
        if (! send.available) send.unavailableBecause = "No effect return is running in this session.";
        t.processors.push_back (std::move (send));

        reg.entries.push_back (std::move (t));
    }

    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        if (! graph.busUsed[size_t (b)] && MixBus (b) != MixBus::Master) continue;
        const bool master = MixBus (b) == MixBus::Master;
        DspTargetCapabilities t;
        t.target = { MixTargetKind::Bus, b };
        t.name = mixBusName (MixBus (b));
        t.role = channelRoleName (busRole (MixBus (b), session.purpose));
        t.stereo = true;                      // every bus is prepared stereo
        addChannelProcessors (t, productOf (roleFamily (busRole (MixBus (b), session.purpose))), true, master);

        DspProcessorCapability fader;
        fader.processor = DspProcessor::Fader;
        fader.parameters.push_back (mixParam ("faderDb", "Fader", "dB", -60.0f, 12.0f, 0.0f));
        t.processors.push_back (std::move (fader));

        reg.entries.push_back (std::move (t));
    }

    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        if (! graph.fxUsed[size_t (f)]) continue;
        const FxType type = graph.fxType[size_t (f)];
        DspTargetCapabilities t;
        t.target = { MixTargetKind::FxSlot, f };
        t.name = fxSlotName (FxSlot (f));
        t.role = fxTypeName (type);
        t.stereo = true;

        auto addFx = [&] (DspProcessor p, bool available, const char* why, std::initializer_list<const char*> ids)
        {
            DspProcessorCapability cap;
            cap.processor = p;
            cap.available = available;
            if (! available) cap.unavailableBecause = why;
            for (const char* id : ids)
                if (const ParameterSpec* s = findFxParameterSpec (id)) cap.parameters.push_back (fromSpec (*s));
            t.processors.push_back (std::move (cap));
        };

        using namespace FxParamID;
        // A slot runs one engine or the other (VOCAL THROW runs both). What the slot is not
        // running is reported as unavailable with the reason, never silently accepted.
        const bool isReverb = fxFamily (type) == FxFamily::Reverb || type == FxType::VocalThrow;
        const bool isDelay  = fxFamily (type) == FxFamily::Delay;
        addFx (DspProcessor::Reverb, isReverb,
               "This return runs a delay, not a reverb. Send to a reverb return instead.",
               { rvOn, rvDecay, rvPreDelay, rvSize, rvDamping, rvDiffusion, rvLowCut, rvHighCut, rvModRate, rvModDepth, rvEarly, rvLevel });
        addFx (DspProcessor::Delay, isDelay || type == FxType::VocalThrow,
               "This return runs a reverb, not a delay. Send to a delay return instead.",
               { dlOn, dlMode, dlSync, dlTime, dlDivision, dlOffset, dlFeedback, dlLowCut, dlHighCut, dlWidth,
                 dlDuck, dlDuckRelease, dlModRate, dlModDepth, dlToReverb, dlLevel });

        DspProcessorCapability ret;
        ret.processor = DspProcessor::FxReturn;
        ret.parameters.push_back (mixParam ("returnDb", "Return level", "dB", -60.0f, 12.0f, 0.0f));
        t.processors.push_back (std::move (ret));

        reg.entries.push_back (std::move (t));
    }

    return reg;
}

const DspTargetCapabilities* DspCapabilityRegistry::find (const MixTargetRef& t) const noexcept
{
    for (const auto& e : entries) if (e.target == t) return &e;
    return nullptr;
}

const DspProcessorCapability* DspCapabilityRegistry::find (const MixTargetRef& t, DspProcessor p) const noexcept
{
    const auto* e = find (t);
    return e != nullptr ? e->find (p) : nullptr;
}

const DspParameterCapability* DspCapabilityRegistry::findParameter (const MixTargetRef& t, DspProcessor p, const std::string& paramId) const noexcept
{
    const auto* c = find (t, p);
    if (c == nullptr || ! c->available) return nullptr;
    return c->find (paramId);
}

bool DspCapabilityRegistry::supports (const MixTargetRef& t, DspProcessor p) const noexcept
{
    const auto* c = find (t, p);
    return c != nullptr && c->available;
}

json::Value DspParameterCapability::toJson() const
{
    auto v = json::Value::object();
    v.set ("id", id);
    v.set ("name", name);
    if (! unit.empty()) v.set ("unit", unit);
    switch (type)
    {
        case ParameterSpec::Type::Bool:   v.set ("type", "bool"); break;
        case ParameterSpec::Type::Choice:
        {
            v.set ("type", "choice");
            auto c = json::Value::array();
            for (const auto& s : choices) c.add (s);
            v.set ("choices", std::move (c));
            break;
        }
        case ParameterSpec::Type::Float:
        default:
            v.set ("type", "float");
            v.set ("min", minValue);
            v.set ("max", maxValue);
            break;
    }
    return v;
}

json::Value DspProcessorCapability::toJson() const
{
    auto v = json::Value::object();
    v.set ("processor", dspProcessorId (processor));
    v.set ("available", available);
    if (! available)
    {
        v.set ("unavailableBecause", unavailableBecause);
        return v;
    }
    auto ps = json::Value::array();
    for (const auto& p : parameters) ps.add (p.toJson());
    v.set ("parameters", std::move (ps));
    return v;
}

json::Value DspTargetCapabilities::toJson() const
{
    auto v = json::Value::object();
    v.set ("target", target.id());
    v.set ("name", name);
    if (! role.empty()) v.set ("role", role);
    v.set ("stereo", stereo);
    auto ps = json::Value::array();
    for (const auto& p : processors) ps.add (p.toJson());
    v.set ("processors", std::move (ps));
    return v;
}

json::Value DspCapabilityRegistry::toJson() const
{
    auto v = json::Value::object();
    v.set ("schemaVersion", kDspCapabilitySchemaVersion);
    auto ts = json::Value::array();
    for (const auto& e : entries) ts.add (e.toJson());
    v.set ("targets", std::move (ts));
    return v;
}

} // namespace livemix
