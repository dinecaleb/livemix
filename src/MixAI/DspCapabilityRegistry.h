#pragma once
#include <string>
#include <vector>
#include "Core/Json.h"
#include "Mix/MixSession.h"
#include "Mix/RoutingGraph.h"
#include "State/ParameterSpecs.h"
#include "DSP/ChannelParameters.h"

namespace livemix
{

inline constexpr int kDspCapabilitySchemaVersion = 1;

// WHAT TOOLS DO WE ACTUALLY HAVE.
//
// The registry is built from the real parameter tables (ParameterSpecs / FxParameterSpecs)
// and from how MixEngine actually configures each processor, so it cannot drift away from
// the engine: the limiter exists on the master because that is the only place MixEngine
// turns the stage on, and the width stage is offered where the signal is really stereo.
// Nothing invents a processor DLIVE does not have.
enum class DspProcessor : int
{
    InputGain = 0,     // DLIVE's own digital preamp (never the console's)
    HighPass,
    LowPass,
    Gate,
    CorrectiveEq,
    DeEsser,
    Compressor,
    Transient,
    ToneEq,
    Saturation,
    Width,
    Limiter,
    Fader,
    Pan,
    Send,
    Reverb,
    Delay,
    FxReturn,
    Count
};

const char* dspProcessorId (DspProcessor) noexcept;      // "compressor": the machine name, stable
const char* dspProcessorLabel (DspProcessor) noexcept;   // "Compressor"
DspProcessor dspProcessorFromId (const std::string&) noexcept;   // Count when unknown

// What a plan can point at.
enum class MixTargetKind : int { Strip = 0, Bus, FxSlot, Count };

struct MixTargetRef
{
    MixTargetKind kind = MixTargetKind::Strip;
    int index = -1;                       // strip index / MixBus / FxSlot

    bool operator== (const MixTargetRef& o) const noexcept { return kind == o.kind && index == o.index; }
    bool operator!= (const MixTargetRef& o) const noexcept { return ! (*this == o); }
    bool isMaster() const noexcept { return kind == MixTargetKind::Bus && index == int (MixBus::Master); }
    std::string id() const;               // "strip:3" / "bus:VOCALS" / "fx:Vocal Plate"
    static MixTargetRef parse (const std::string&);   // index -1 when it does not name anything
};

struct DspParameterCapability
{
    std::string id;                       // the engine's own parameter id ("compThreshold")
    std::string name;                     // "Threshold"
    std::string unit;                     // "dB"
    ParameterSpec::Type type = ParameterSpec::Type::Float;
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
    std::vector<std::string> choices;

    float clamp (float v) const noexcept { return v < minValue ? minValue : (v > maxValue ? maxValue : v); }
    json::Value toJson() const;
};

struct DspProcessorCapability
{
    DspProcessor processor = DspProcessor::Compressor;
    bool available = true;
    std::string unavailableBecause;       // said plainly when it is not: never silently missing
    std::vector<DspParameterCapability> parameters;

    const DspParameterCapability* find (const std::string& paramId) const noexcept;
    json::Value toJson() const;
};

struct DspTargetCapabilities
{
    MixTargetRef target;
    std::string name;                     // "Lead Vocal" / "VOCALS" / "Vocal Plate"
    std::string role;                     // the source role, where there is one
    bool stereo = false;
    std::vector<DspProcessorCapability> processors;

    const DspProcessorCapability* find (DspProcessor) const noexcept;
    json::Value toJson() const;
};

// Which stages are actually doing something on a channel right now. The chain's own
// enable flags, read in the engine's fixed order - the machine-readable counterpart of
// what the Inspector draws.
std::vector<DspProcessor> activeProcessors (const ChannelParameters&, bool stereo, bool master);

// The whole toolbox for one session. Rebuilt whenever the routing is rebuilt, because
// what is available depends on what is assigned.
class DspCapabilityRegistry
{
public:
    static DspCapabilityRegistry build (const MixSession&, const RoutingGraph&);

    const std::vector<DspTargetCapabilities>& targets() const noexcept { return entries; }
    const DspTargetCapabilities* find (const MixTargetRef&) const noexcept;
    const DspProcessorCapability* find (const MixTargetRef&, DspProcessor) const noexcept;
    const DspParameterCapability* findParameter (const MixTargetRef&, DspProcessor, const std::string& paramId) const noexcept;
    bool supports (const MixTargetRef& t, DspProcessor p) const noexcept;

    // The digest the reasoning layer is given: what exists, on what, within what range.
    json::Value toJson() const;

private:
    std::vector<DspTargetCapabilities> entries;
};

} // namespace livemix
