#include "MixContext.h"
#include "DspCapabilityRegistry.h"
#include "Profiles/MixProfileData.h"
#include "Profiles/StyleProfile.h"
#include <algorithm>
#include <cmath>

namespace livemix
{

namespace
{
    // A listen shorter than this measured nothing worth a mix decision, however loud it was.
    constexpr float kMinListenSeconds = 6.0f;
    // One source playing is a soundcheck of one source. The reasoning layer is told so rather
    // than being handed a band's worth of confidence about a single channel.
    constexpr int kMinActiveTracks = 2;

    const char* familyName (RoleFamily f) noexcept
    {
        switch (f)
        {
            case RoleFamily::Kick:            return "Kick";
            case RoleFamily::Snare:           return "Snare";
            case RoleFamily::HiHat:           return "HiHat";
            case RoleFamily::Tom:             return "Tom";
            case RoleFamily::Overhead:        return "Overhead";
            case RoleFamily::Room:            return "Room";
            case RoleFamily::Bus:             return "DrumBus";
            case RoleFamily::LeadVocal:       return "LeadVocal";
            case RoleFamily::BackingVocal:    return "BackingVocal";
            case RoleFamily::Choir:           return "Choir";
            case RoleFamily::Speech:          return "Speech";
            case RoleFamily::VocalBus:        return "VocalBus";
            case RoleFamily::Piano:           return "Piano";
            case RoleFamily::ElectricPiano:   return "ElectricPiano";
            case RoleFamily::Organ:           return "Organ";
            case RoleFamily::Synth:           return "Synth";
            case RoleFamily::KeysBus:         return "KeysBus";
            case RoleFamily::AcousticGuitar:  return "AcousticGuitar";
            case RoleFamily::ElectricGuitar:  return "ElectricGuitar";
            case RoleFamily::GuitarBus:       return "GuitarBus";
            case RoleFamily::ElectricBass:    return "ElectricBass";
            case RoleFamily::SynthBass:       return "SynthBass";
            case RoleFamily::BassBus:         return "BassBus";
            case RoleFamily::Master:          return "Master";
            default:                          return "Other";
        }
    }

    json::Value bandJson (const std::array<float, int (Band::Count)>& bands)
    {
        auto v = json::Value::object();
        for (int b = 0; b < int (Band::Count); ++b) v.set (kBandNames[size_t (b)], bands[size_t (b)]);
        return v;
    }
}

MixContextMeasurements MixContextMeasurements::from (const AnalysisResult& a)
{
    MixContextMeasurements m;
    m.peakDb = a.peakDb;
    m.truePeakDb = a.truePeakDb;
    m.rmsDb = a.rmsDb;
    m.activeRmsDb = a.activeRmsDb;
    m.musicalPeakDb = a.musicalPeakDb;
    m.hitLevelDb = a.hitLevelDb;
    m.crestFactorDb = a.crestFactorDb;
    m.noiseFloorDb = a.noiseFloorDb;
    m.dynamicRangeDb = a.dynamicRangeDb;
    m.silencePercent = a.silencePercent;
    m.clipCount = a.clipCount;
    m.loudnessLufs = a.loudnessLufs;
    m.spectralCentroidHz = a.spectralCentroidHz;
    m.highFrequencyRatioDb = a.highFrequencyRatioDb;
    m.fundamentalHz = a.fundamentalHz;
    m.bandEnergyDb = a.bandEnergyDb;
    // The three that stand furthest above the local trend: past that it is the source's own
    // tone, not a resonance, and a list of twenty peaks is not a decision aid.
    auto peaks = a.resonances;
    std::sort (peaks.begin(), peaks.end(), [] (const ResonancePeak& x, const ResonancePeak& y) { return x.prominenceDb > y.prominenceDb; });
    if (peaks.size() > 3) peaks.resize (3);
    m.resonances = std::move (peaks);
    m.transientsPerSecond = a.transientsPerSecond;
    m.meanTransientRiseDb = a.meanTransientRiseDb;
    m.meanDecayMs = a.meanDecayMs;
    m.bleedEstimate = a.bleedEstimate;
    m.sibilanceDb = a.sibilanceDb;
    m.numChannels = a.numChannels;
    m.stereoCorrelation = a.stereoCorrelation;
    m.stereoBalanceDb = a.stereoBalanceDb;
    m.tempoBpm = a.tempoBpm;
    m.tempoConfidence = a.tempoConfidence;
    return m;
}

json::Value MixContextMeasurements::toJson() const
{
    auto v = json::Value::object();
    v.set ("peakDbfs", peakDb);
    v.set ("truePeakDbfs", truePeakDb);
    v.set ("rmsDbfs", rmsDb);
    v.set ("activeRmsDbfs", activeRmsDb);
    v.set ("musicalPeakDbfs", musicalPeakDb);
    v.set ("hitLevelDbfs", hitLevelDb);
    v.set ("crestFactorDb", crestFactorDb);
    v.set ("noiseFloorDbfs", noiseFloorDb);
    v.set ("dynamicRangeDb", dynamicRangeDb);
    v.set ("silencePercent", silencePercent);
    v.set ("clippedSamples", clipCount);
    if (loudnessLufs > -119.0f) v.set ("loudnessLufs", loudnessLufs);
    v.set ("spectralCentroidHz", spectralCentroidHz);
    v.set ("highFrequencyRatioDb", highFrequencyRatioDb);
    if (fundamentalHz > 0.0f) v.set ("fundamentalHz", fundamentalHz);
    v.set ("bandEnergyDbReTotal", bandJson (bandEnergyDb));
    if (! resonances.empty())
    {
        auto r = json::Value::array();
        for (const auto& p : resonances)
        {
            auto o = json::Value::object();
            o.set ("hz", p.frequencyHz);
            o.set ("prominenceDb", p.prominenceDb);
            r.add (std::move (o));
        }
        v.set ("resonances", std::move (r));
    }
    v.set ("transientsPerSecond", transientsPerSecond);
    v.set ("meanTransientRiseDb", meanTransientRiseDb);
    if (meanDecayMs > 0.0f) v.set ("meanDecayMs", meanDecayMs);
    v.set ("bleedEstimate", bleedEstimate);
    if (sibilanceDb > -119.0f) v.set ("sibilanceDb", sibilanceDb);
    v.set ("channels", numChannels);
    if (numChannels > 1)
    {
        v.set ("stereoCorrelation", stereoCorrelation);
        v.set ("stereoBalanceDb", stereoBalanceDb);
    }
    if (tempoBpm > 0.0f)
    {
        v.set ("tempoBpm", tempoBpm);
        v.set ("tempoConfidence", tempoConfidence);
    }
    return v;
}

json::Value MixContextTrack::toJson() const
{
    auto v = json::Value::object();
    v.set ("id", MixTargetRef { MixTargetKind::Strip, id }.id());
    v.set ("name", name);
    v.set ("role", role);
    v.set ("family", family);
    v.set ("bus", bus);
    v.set ("format", stereo ? "stereo" : "mono");
    v.set ("heard", heard);
    if (faint) v.set ("faint", true);
    if (bleedOnly) v.set ("heardAsSpillOnly", true);
    v.set ("faderDb", faderDb);
    v.set ("pan", pan);
    v.set ("inputGainDb", inputGainDb);
    if (! sends.empty())
    {
        auto s = json::Value::object();
        for (const auto& [slot, db] : sends) s.set (slot, db);
        v.set ("sendsDb", std::move (s));
    }
    auto proc = json::Value::array();
    for (const auto& p : processing) proc.add (p);
    v.set ("processingNow", std::move (proc));
    auto health = json::Value::object();
    health.set ("state", signalHealth);
    health.set ("capturePeakDbfsAtDevice", capturePeakDb);
    // Capture gain and mix gain are different things and the context says so: this is the
    // move the console preamp itself should make, which no fader inside DLIVE can do for it.
    health.set ("consolePreampMoveDb", consoleMoveDb);
    v.set ("signalHealth", std::move (health));
    v.set ("measurements", measurements.toJson());
    return v;
}

json::Value MixContextBus::toJson() const
{
    auto v = json::Value::object();
    v.set ("id", name);
    v.set ("used", used);
    v.set ("faderDb", faderDb);
    v.set ("sourceCount", sourceCount);
    auto proc = json::Value::array();
    for (const auto& p : processing) proc.add (p);
    v.set ("processingNow", std::move (proc));
    if (used) v.set ("measurements", measurements.toJson());
    return v;
}

json::Value MixCaptureAdequacy::toJson() const
{
    auto v = json::Value::object();
    v.set ("sufficient", sufficient);
    v.set ("listenSeconds", seconds);
    v.set ("tracksAssigned", tracksAssigned);
    v.set ("tracksActive", tracksActive);
    v.set ("tracksFaint", tracksFaint);
    v.set ("tracksClipping", tracksClipping);
    if (! reason.empty()) v.set ("reason", reason);
    if (! guidance.empty()) v.set ("guidance", guidance);
    return v;
}

json::Value MixContext::toJson() const
{
    auto v = json::Value::object();
    v.set ("schema", "dlive.mixContext");
    v.set ("schemaVersion", schemaVersion);

    auto s = json::Value::object();
    s.set ("name", sessionName);
    s.set ("profile", profile);
    s.set ("purpose", purpose);
    s.set ("sampleRate", sampleRate);
    s.set ("listenSeconds", listenSeconds);
    if (tempoBpm > 0.0f) s.set ("tempoBpm", tempoBpm);
    s.set ("trackCount", int (tracks.size()));
    v.set ("session", std::move (s));

    v.set ("capture", adequacy.toJson());

    auto ts = json::Value::array();
    for (const auto& t : tracks) ts.add (t.toJson());
    v.set ("tracks", std::move (ts));

    auto bs = json::Value::array();
    for (int b = 0; b < int (MixBus::Master); ++b)
        if (buses[size_t (b)].used) bs.add (buses[size_t (b)].toJson());
    v.set ("buses", std::move (bs));

    auto m = json::Value::object();
    m.set ("bus", buses[size_t (MixBus::Master)].toJson());
    m.set ("output", master.toJson());
    v.set ("master", std::move (m));

    auto rs = json::Value::array();
    for (const auto& r : relationships)
    {
        auto o = json::Value::object();
        o.set ("kind", mixRelationKindName (r.kind));
        o.set ("metric", r.metric);
        if (r.stripA >= 0) o.set ("a", MixTargetRef { MixTargetKind::Strip, r.stripA }.id());
        if (r.stripB >= 0) o.set ("b", MixTargetRef { MixTargetKind::Strip, r.stripB }.id());
        o.set ("value", r.value);
        o.set ("tolerance", r.tolerance);
        o.set ("concern", r.concern);
        o.set ("says", r.headline);
        rs.add (std::move (o));
    }
    v.set ("relationships", std::move (rs));

    auto bd = json::Value::array();
    for (const auto& d : baselineDecisions) bd.add (d);
    v.set ("baselineAlreadyDecided", std::move (bd));
    return v;
}

MixContext buildMixContext (const MixPlanContext& ctx, const MixPlan& plan)
{
    MixContext out;
    out.sessionName = ctx.session.name;
    out.profile = styleProfileName (ctx.session.profile);
    out.purpose = mixPurposeName (ctx.session.purpose);
    out.sampleRate = ctx.capture.strips.empty() ? 48000.0 : ctx.capture.strips.front().sampleRate;
    out.listenSeconds = ctx.capture.seconds;
    out.tempoBpm = plan.proposed.tempoBpm;

    const int n = std::min ({ ctx.graph.numStrips(), ctx.current.numStrips, int (ctx.capture.strips.size()) });
    std::vector<bool> heardStrips (size_t (std::max (n, 0)), false);

    for (int i = 0; i < n; ++i)
    {
        const auto& route = ctx.graph.strips[size_t (i)];
        const auto& sp = ctx.current.strips[size_t (i)];
        const StripPlan* sd = i < int (plan.strips.size()) ? &plan.strips[size_t (i)] : nullptr;

        MixContextTrack t;
        t.id = i;
        t.name = route.name;
        t.role = channelRoleName (route.role);
        t.family = familyName (roleFamily (route.role));
        t.bus = mixBusName (route.bus);
        t.stereo = route.numChannels() == 2;
        t.heard = sd != nullptr && sd->heard;
        t.faint = sd != nullptr && sd->faint;
        t.bleedOnly = sd != nullptr && sd->bleedOnly;
        heardStrips[size_t (i)] = t.heard;
        t.faderDb = sp.faderDb;
        t.pan = sp.pan;
        t.inputGainDb = sp.inputGainDb;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (ctx.graph.fxUsed[size_t (f)] && sp.sendDb[size_t (f)] > kSilenceDb + 1.0f)
                t.sends.emplace_back (fxSlotName (FxSlot (f)), sp.sendDb[size_t (f)]);
        for (const auto p : activeProcessors (sp.channel, t.stereo, false)) t.processing.push_back (dspProcessorId (p));

        t.capturePeakDb = sd != nullptr ? sd->capturePeakDb : -120.0f;
        // The health verdict is the one Tune already reached about this input, not a second
        // opinion: the console strip, the Inspector and this document all read the same word.
        if (t.faint) t.signalHealth = "Faint";
        else if (! t.heard) t.signalHealth = "No signal";
        else if (sd != nullptr && sd->tune.valid)
        {
            t.signalHealth = sd->tune.report.inputHealth;
            t.consoleMoveDb = sd->tune.report.suggestedCaptureGainDb;
        }
        else t.signalHealth = "Healthy";

        t.measurements = MixContextMeasurements::from (ctx.capture.strips[size_t (i)]);
        out.tracks.push_back (std::move (t));
    }

    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        auto& bus = out.buses[size_t (b)];
        bus.name = mixBusName (MixBus (b));
        bus.used = ctx.graph.busUsed[size_t (b)] || MixBus (b) == MixBus::Master;
        bus.faderDb = ctx.current.buses[size_t (b)].faderDb;
        bus.sourceCount = ctx.graph.stripsOnBus (MixBus (b));
        for (const auto p : activeProcessors (ctx.current.buses[size_t (b)].channel, true, MixBus (b) == MixBus::Master))
            bus.processing.push_back (dspProcessorId (p));
        if (bus.used) bus.measurements = MixContextMeasurements::from (ctx.capture.buses[size_t (b)]);
    }
    out.master = MixContextMeasurements::from (ctx.capture.masterOutput);

    out.relationships = RelationshipEngine::measure (ctx, &heardStrips);

    // ---- Is this listen worth reasoning about at all? ----
    auto& ad = out.adequacy;
    ad.seconds = ctx.capture.seconds;
    ad.tracksAssigned = n;
    ad.tracksActive = plan.stripsHeard;
    ad.tracksFaint = plan.stripsFaint;
    for (int i = 0; i < n; ++i) if (ctx.capture.strips[size_t (i)].clipCount > 0) ++ad.tracksClipping;

    if (ad.seconds < kMinListenSeconds)
    {
        ad.reason = "The listen was only " + std::to_string (int (std::round (ad.seconds))) + " seconds long.";
        ad.guidance = "Have the band play and run TUNE MIX again so DLIVE hears a full passage.";
    }
    else if (ad.tracksActive == 0)
    {
        ad.reason = "No input carried a usable signal during the listen.";
        ad.guidance = "Check that the band is playing into the assigned inputs, then run TUNE MIX again.";
    }
    else if (ad.tracksActive < kMinActiveTracks && ad.tracksAssigned >= kMinActiveTracks)
    {
        std::string who;
        for (const auto& t : out.tracks) if (t.heard) { if (! who.empty()) who += " and "; who += t.name; }
        ad.reason = "Only " + who + (ad.tracksActive == 1 ? " was" : " were") + " active during the listen.";
        ad.guidance = "Play the full band and run TUNE MIX again.";
    }
    else ad.sufficient = true;

    for (const auto& note : plan.notes) out.baselineDecisions.push_back (note);
    for (const auto& r : plan.relationships)
        if (! r.what.empty()) out.baselineDecisions.push_back (r.what);

    return out;
}

} // namespace livemix
