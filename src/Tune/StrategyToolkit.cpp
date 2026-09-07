// Shared engineering rules. Every rule is bounded by the profile's safe ranges
// and returns without a decision when the measurement is inside tolerance.
#include "SourceStrategy.h"
#include "Core/DbUtils.h"
#include "State/ParameterIDs.h"
#include "Core/ProductDefinition.h"
#include <cmath>
#include <cstdio>
#include <cctype>

namespace livemix
{

// ---------------------------------------------------------------------------
// TuneDecisions
// ---------------------------------------------------------------------------
void TuneDecisions::move (Recommendation::Kind kind, TuneSection section, std::string what, std::string why,
                          Confidence confidence, const std::function<void (ChannelParameters&)>& edit)
{
    const ChannelParameters snapshot = proposed;
    edit (proposed);
    auto changes = diffParameters (snapshot, proposed);
    if (changes.empty()) return;
    Recommendation r;
    r.kind = kind;
    r.section = section;
    r.what = std::move (what);
    r.why = std::move (why);
    r.confidence = confidence;
    r.changes = std::move (changes);
    r.safeToAutoApply = true; // strategies only make moves they would make themselves
    items.push_back (std::move (r));
}

void TuneDecisions::note (Recommendation::Kind kind, TuneSection section, std::string what, std::string why, Confidence confidence)
{
    Recommendation r;
    r.kind = kind;
    r.section = section;
    r.what = std::move (what);
    r.why = std::move (why);
    r.confidence = confidence;
    items.push_back (std::move (r));
}

bool TuneDecisions::hasChangesIn (TuneSection s) const
{
    for (const auto& i : items) if (i.section == s && ! i.changes.empty()) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Parameter snapshot helpers
// ---------------------------------------------------------------------------
ChannelParameters applyChanges (const ChannelParameters& base, const std::vector<ParameterChange>& changes)
{
    ChannelParameters p = base;
    for (const auto& c : changes)
    {
        forEachDspParameter (p, [&] (const std::string& id, auto& v)
        {
            if (id != c.paramId) return;
            using T = std::remove_reference_t<decltype (v)>;
            if constexpr (std::is_same_v<T, bool>) v = c.value >= 0.5f;
            else if constexpr (std::is_same_v<T, int>) v = int (std::lround (c.value));
            else v = c.value;
        });
    }
    return p;
}

std::vector<ParameterChange> diffParameters (const ChannelParameters& from, const ChannelParameters& to)
{
    std::vector<float> a, b;
    std::vector<std::string> ids;
    ChannelParameters fromCopy = from, toCopy = to;
    forEachDspParameter (fromCopy, [&] (const std::string& id, auto& v) { ids.push_back (id); a.push_back (float (v)); });
    forEachDspParameter (toCopy, [&] (const std::string&, auto& v) { b.push_back (float (v)); });
    std::vector<ParameterChange> out;
    for (size_t i = 0; i < ids.size(); ++i)
        if (std::fabs (a[i] - b[i]) > 1.0e-6f) out.push_back ({ ids[i], b[i] });
    return out;
}

namespace tune
{

std::string fmtDb (float db, int decimals)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), decimals == 0 ? "%+.0f dB" : "%+.1f dB", double (db));
    return buf;
}

std::string fmtHz (float hz)
{
    char buf[64];
    if (hz >= 1000.0f) std::snprintf (buf, sizeof (buf), "%.1f kHz", double (hz / 1000.0f));
    else std::snprintf (buf, sizeof (buf), "%.0f Hz", double (hz));
    return buf;
}

namespace
{
    std::string num (const char* format, double value)
    {
        char buf[96];
        std::snprintf (buf, sizeof (buf), format, value);
        return buf;
    }
    float roundHz (float hz)
    {
        if (hz < 100.0f) return std::round (hz);
        if (hz < 1000.0f) return std::round (hz / 5.0f) * 5.0f;
        return std::round (hz / 50.0f) * 50.0f;
    }
    float roundDb (float db) { return std::round (db * 2.0f) * 0.5f; }
    bool near (float a, float b, float relTol) { return std::fabs (a - b) <= relTol * std::max (std::fabs (a), std::fabs (b)); }
    float bandCentreHz (Band b) { return std::sqrt (kBandEdgesHz[size_t (b)] * kBandEdgesHz[size_t (int (b) + 1)]); }
}

const char* eventNoun (const TuneContext& ctx) { return productDefinitionFor (ctx.role).eventNoun; }
const char* mixNoun (const TuneContext& ctx) { return productDefinitionFor (ctx.role).mixNoun; }

namespace
{
    std::string plural (const char* noun) { return std::string (noun) + "s"; }
    std::string capital (std::string s) { if (! s.empty()) s[0] = char (std::toupper (static_cast<unsigned char> (s[0]))); return s; }
}

Levels levels (const TuneContext& ctx)
{
    Levels l;
    l.hitDb = ctx.analysis.hitLevelDb + ctx.current.inputTrimDb;
    l.floorDb = ctx.analysis.noiseFloorDb + ctx.current.inputTrimDb;
    l.sparse = ctx.analysis.silencePercent > 60.0f;
    return l;
}

float captureGainToHealthyDb (const AnalysisResult& a, const SourceTargets& t)
{
    const float centre = 0.5f * (t.capturePeakMinDb + t.capturePeakMaxDb);
    if (a.clipCount > 0 || a.peakDb > -0.5f) return std::min (std::round (centre - a.peakDb) - 2.0f, -3.0f);
    if (a.peakDb > t.capturePeakMaxDb) return std::min (std::round (centre - a.peakDb), -1.0f);
    if (a.peakDb < t.capturePeakMinDb) return std::max (std::round (t.capturePeakMinDb + 3.0f - a.peakDb), 1.0f);
    return 0.0f;
}

bool evaluateInput (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, RecommendationResult& report)
{
    const auto& a = ctx.analysis;
    report.measuredPeakDb = a.peakDb;
    report.measuredRmsDb = a.rmsDb;
    report.suggestedCaptureGainDb = 0.0f;

    const bool noSignal = a.silencePercent > 95.0f || a.peakDb < -70.0f;
    if (noSignal)
    {
        report.inputHealth = "No signal";
        d.note (Recommendation::Kind::Info, TuneSection::Input, "No usable signal was detected.",
                "Check the input routing and mute state, then Tune again while " + std::string (productDefinitionFor (ctx.role).sourceNoun) + " is playing.", Confidence::High);
        return false;
    }

    const bool sparse = a.silencePercent > 60.0f;
    const float step = t.captureGainMaxStepDb;
    const std::string preampNote = " This is the console or interface preamp, not the plugin trim: trim cannot restore resolution that was never captured.";

    if (a.clipCount > 0 || a.peakDb > -0.5f)
    {
        report.inputHealth = "Clipping";
        const float delta = std::max (captureGainToHealthyDb (a, t), -step);
        report.suggestedCaptureGainDb = delta;
        d.note (Recommendation::Kind::CaptureGain, TuneSection::Input, "Reduce preamp approximately " + fmtDb (delta, 0),
                std::to_string (a.clipCount) + " clipped samples were detected. Digital clipping cannot be repaired after the converter." + preampNote,
                Confidence::High);
    }
    else if (a.peakDb > t.capturePeakMaxDb)
    {
        report.inputHealth = "Hot";
        const float delta = std::max (captureGainToHealthyDb (a, t), -step);
        report.suggestedCaptureGainDb = delta;
        d.note (Recommendation::Kind::CaptureGain, TuneSection::Input, "Reduce preamp approximately " + fmtDb (delta, 0),
                "Peaks reached " + num ("%.1f dBFS", double (a.peakDb)) + ", above the healthy range of " + num ("%.0f", double (t.capturePeakMinDb))
                + " to " + num ("%.0f dBFS", double (t.capturePeakMaxDb)) + ". A louder " + eventNoun (ctx) + " could clip the converter." + preampNote,
                Confidence::High);
    }
    else if (a.peakDb < t.capturePeakMinDb)
    {
        report.inputHealth = "Low";
        // Conservative: aim for the low edge of the healthy range plus a little, never more than one bounded step.
        const float delta = std::min (captureGainToHealthyDb (a, t), step);
        report.suggestedCaptureGainDb = delta;
        std::string why = "Peaks reached only " + num ("%.1f dBFS", double (a.peakDb)) + ". The signal is clean but below the healthy range of "
                        + num ("%.0f", double (t.capturePeakMinDb)) + " to " + num ("%.0f dBFS", double (t.capturePeakMaxDb)) + "." + preampNote
                        + " Adjust the preamp, then Re-Tune.";
        if (sparse) why += " Confidence is reduced because the capture contained long quiet sections.";
        d.note (Recommendation::Kind::CaptureGain, TuneSection::Input, "Increase preamp approximately " + fmtDb (delta, 0), why,
                (delta >= 6.0f && ! sparse) ? Confidence::High : Confidence::Medium);
    }
    else
    {
        report.inputHealth = "Healthy";
    }
    return true;
}

void removeDcOffset (const TuneContext& ctx, TuneDecisions& d)
{
    if (std::fabs (ctx.analysis.dcOffset) <= 0.01f || ctx.current.hpfEnabled) return;
    d.move (Recommendation::Kind::Filter, TuneSection::Tone, "Enabled the high-pass filter",
            "A DC offset of " + num ("%.3f", double (ctx.analysis.dcOffset)) + " was measured; the high-pass removes it.",
            Confidence::High, [] (ChannelParameters& p) { p.hpfEnabled = true; });
}

float fundamental (const TuneContext& ctx, const SourceTargets& t)
{
    const float f = ctx.analysis.fundamentalHz;
    if (t.fundamentalMaxHz <= 0.0f || f <= 0.0f) return 0.0f;
    if (f < t.fundamentalMinHz || f > t.fundamentalMaxHz) return 0.0f;
    if (ctx.analysis.fundamentalLevelDb < -30.0f) return 0.0f; // not a dominant component
    return f;
}

float bandDeviation (const TuneContext& ctx, const SourceTargets& t, Band b)
{
    return ctx.analysis.bandEnergyDb[size_t (b)] - t.bandTargetDb[size_t (b)];
}

float bandExcess (const TuneContext& ctx, const SourceTargets& t, Band b)
{
    const float dev = bandDeviation (ctx, t, b);
    const float tol = t.bandToleranceDb[size_t (b)];
    if (dev > tol) return dev - tol;
    if (dev < -tol) return dev + tol;
    return 0.0f;
}

float templateHighPassHz (const TuneContext& ctx, const SourceTargets& t)
{
    const ChannelParameters tpl = Profiles::baseline (ctx.profile, ctx.role);
    return tpl.hpfEnabled ? clamp (tpl.hpfHz, t.hpfMinHz, t.hpfMaxHz) : t.hpfMinHz;
}

void placeHighPass (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float hz, const char* why)
{
    const float target = roundHz (clamp (hz, t.hpfMinHz, t.hpfMaxHz));
    const auto& cur = d.proposed;
    if (cur.hpfEnabled && near (cur.hpfHz, target, 0.15f)) return;
    const std::string what = (cur.hpfEnabled ? "High-pass moved to " : "High-pass enabled at ") + fmtHz (target);
    d.move (Recommendation::Kind::Filter, TuneSection::Tone, what, why, Confidence::Medium,
            [target] (ChannelParameters& p) { p.hpfEnabled = true; p.hpfHz = target; });
}

void controlLowMid (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    const float lowMid = bandExcess (ctx, t, Band::LowMid);
    const float mid = bandExcess (ctx, t, Band::Mid);
    auto& band0 = d.proposed.correctiveBands[0];

    if (lowMid <= 0.0f && mid <= 0.0f)
    {
        // Not boxy. A template cut deeper than 2 dB that now pushes the region under target is eased.
        const float dev = bandDeviation (ctx, t, Band::LowMid);
        if (band0.enabled && band0.gainDb < -2.0f && dev < -0.5f * t.bandToleranceDb[size_t (Band::LowMid)])
        {
            d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Eased the low-mid cut to " + fmtDb (-1.5f) + " at " + fmtHz (band0.freqHz),
                    "This source is not boxy (low-mids are " + num ("%.0f dB", double (-dev)) + " under the profile target), so the template cut was reduced.",
                    Confidence::Medium, [] (ChannelParameters& p) { p.correctiveBands[0].gainDb = -1.5f; });
        }
        return;
    }

    const bool useMid = mid > lowMid;
    const float excess = useMid ? mid : lowMid;
    const float lo = useMid ? 400.0f : 200.0f, hi = useMid ? 1000.0f : 800.0f;
    float centre = useMid ? 630.0f : t.boxinessHz;
    float q = 1.4f;
    for (const auto& r : ctx.analysis.resonances)
        if (r.frequencyHz >= lo && r.frequencyHz <= hi && r.prominenceDb >= t.resonanceMinProminenceDb * 0.75f) { centre = r.frequencyHz; q = 2.0f; break; }
    centre = roundHz (centre);
    const float gain = -clamp (1.5f + 0.5f * excess, 1.5f, t.maxEqCutDb);
    if (band0.enabled && near (band0.freqHz, centre, 0.2f) && band0.gainDb <= gain + 0.5f) return; // already handled

    d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Reduced " + std::string (useMid ? "boxiness" : "low-mid mud") + ": " + fmtDb (gain) + " at " + fmtHz (centre),
            std::string (useMid ? "Mid" : "Low-mid") + " energy is " + num ("%.0f dB", double (excess)) + " above the profile tolerance"
            + (q > 1.5f ? ", concentrated around a resonance." : "."),
            excess > 4.0f ? Confidence::High : Confidence::Medium,
            [=] (ChannelParameters& p) { p.correctiveBands[0] = { true, FilterType::Peak, centre, roundDb (gain), q }; });
}

void notchResonance (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float minHz, float maxHz, const char* character)
{
    const ResonancePeak* best = nullptr;
    for (const auto& r : ctx.analysis.resonances)
        if (r.frequencyHz >= minHz && r.frequencyHz <= maxHz && r.prominenceDb >= t.resonanceMinProminenceDb && (best == nullptr || r.prominenceDb > best->prominenceDb))
            best = &r;
    if (best == nullptr) return;
    const auto& band0 = d.proposed.correctiveBands[0];
    if (band0.enabled && near (band0.freqHz, best->frequencyHz, 0.2f)) return; // the low-mid cut already sits on it

    const float f = roundHz (best->frequencyHz);
    const float gain = -clamp (0.6f * best->prominenceDb, 2.0f, t.maxNotchCutDb);
    const float q = best->prominenceDb > 10.0f ? 5.0f : 4.0f;
    d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Notched " + std::string (character) + ": " + fmtDb (gain) + " at " + fmtHz (f),
            "A resonance stands " + num ("%.0f dB", double (best->prominenceDb)) + " above the surrounding spectrum; a narrow cut controls it without changing the overall tone.",
            best->prominenceDb >= t.resonanceMinProminenceDb + 4.0f ? Confidence::High : Confidence::Medium,
            [=] (ChannelParameters& p) { p.correctiveBands[1] = { true, FilterType::Peak, f, roundDb (gain), q }; });
}

void shapeBody (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float fundamentalHz)
{
    const float sub = bandExcess (ctx, t, Band::Sub);
    const float low = bandExcess (ctx, t, Band::Low);
    auto& shelf = d.proposed.toneBands[0];
    const float centre = fundamentalHz > 0.0f ? fundamentalHz : t.bodyHz;

    if (sub > 0.0f && t.hpfMaxHz > t.hpfMinHz)
    {
        // Excess sub is rumble and stage noise, not tone: filter it, don't EQ it. Never above the fundamental.
        float hz = clamp (std::max (templateHighPassHz (ctx, t) * 1.3f, centre * 0.6f), t.hpfMinHz, t.hpfMaxHz);
        if (fundamentalHz > 0.0f) hz = std::min (hz, fundamentalHz * 0.8f);
        placeHighPass (ctx, t, d, hz, ("Sub energy is " + num ("%.0f dB", double (sub)) + " above the profile tolerance; the high-pass is raised toward the fundamental to remove rumble without thinning the body.").c_str());
    }

    if (low < 0.0f)
    {
        const float deficit = -low;
        const float desired = roundDb (clamp (1.0f + 0.6f * deficit, 1.0f, t.maxEqBoostDb));
        const float freq = roundHz (clamp (centre * 1.15f, 40.0f, 250.0f));
        if (shelf.enabled && shelf.gainDb >= desired - 0.5f && near (shelf.freqHz, freq, 0.3f)) return;
        const float gain = std::max (shelf.enabled ? shelf.gainDb : 0.0f, desired);
        std::string why = "Low energy is " + num ("%.0f dB", double (deficit)) + " under the profile target";
        why += fundamentalHz > 0.0f ? "; the shelf is placed just above the measured fundamental (" + fmtHz (fundamentalHz) + ") so the boost adds weight, not mud."
                                    : "; the shelf sits in this source's body region.";
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Added body: " + fmtDb (gain) + " low shelf at " + fmtHz (freq), why,
                deficit > 3.0f ? Confidence::High : Confidence::Medium,
                [=] (ChannelParameters& p) { p.toneBands[0] = { true, FilterType::LowShelf, freq, gain, 0.7f }; });
    }
    else if (low > 0.0f)
    {
        // Relative to the profile template, not the current setting: re-tuning the same capture lands on the same value.
        const auto tpl = Profiles::baseline (ctx.profile, ctx.role).toneBands[0];
        const float currentGain = shelf.enabled ? shelf.gainDb : 0.0f;
        const float templateGain = tpl.enabled ? tpl.gainDb : 0.0f;
        const float gain = roundDb (clamp (templateGain - 0.6f * low, -0.5f * t.maxEqCutDb, t.maxEqBoostDb));
        if (std::fabs (gain - currentGain) < 0.5f) return;
        const bool disable = std::fabs (gain) < 0.5f;
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, disable ? "Removed the low shelf boost" : "Trimmed the low end: " + fmtDb (gain) + " low shelf",
                "Low energy is " + num ("%.0f dB", double (low)) + " above the profile tolerance; the source already has the weight the profile wants.",
                Confidence::Medium,
                [=] (ChannelParameters& p) { p.toneBands[0].enabled = ! disable; p.toneBands[0].gainDb = disable ? 0.0f : gain; });
    }
    else if (fundamentalHz > 0.0f && shelf.enabled && shelf.gainDb > 0.5f)
    {
        // Inside tolerance: keep the template boost but sit it on the real fundamental.
        const float freq = roundHz (clamp (fundamentalHz * 1.15f, 40.0f, 250.0f));
        if (near (shelf.freqHz, freq, 0.25f)) return;
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Moved the body shelf to " + fmtHz (freq),
                "The measured fundamental is " + fmtHz (fundamentalHz) + "; the shelf now sits just above it.",
                Confidence::Medium, [=] (ChannelParameters& p) { p.toneBands[0].freqHz = freq; });
    }
}

void shapeAttack (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    const float presence = bandExcess (ctx, t, Band::Presence);
    const auto& a = ctx.analysis;
    const bool soft = a.transientCount >= 4 && a.meanTransientRiseDb < t.transientRiseLowDb;
    auto& peak = d.proposed.toneBands[2];
    const float freq = roundHz (t.attackHz);

    if (presence < 0.0f || soft)
    {
        const float deficit = std::max (-presence, 0.0f);
        const float desired = roundDb (clamp (1.5f + 0.5f * deficit, 1.5f, t.maxEqBoostDb));
        const float currentGain = peak.enabled ? peak.gainDb : 0.0f;
        if (presence < 0.0f && currentGain < desired - 0.5f)
        {
            d.move (Recommendation::Kind::EQ, TuneSection::Attack, "Added definition: " + fmtDb (desired) + " at " + fmtHz (freq),
                    "Presence energy is " + num ("%.0f dB", double (deficit)) + " under the profile target; attack lives here for this source.",
                    deficit > 3.0f ? Confidence::High : Confidence::Medium,
                    [=] (ChannelParameters& p) { p.toneBands[2] = { true, FilterType::Peak, freq, desired, 1.0f }; });
        }
        if (soft && t.transientAppropriate)
        {
            // From the template and the measured softness (never the current value): re-tuning the same capture lands on the same amount.
            const float templateAttack = Profiles::baseline (ctx.profile, ctx.role).transientAttack;
            const float riseDeficit = std::max (0.0f, t.transientRiseLowDb - a.meanTransientRiseDb);
            const float attack = clamp (roundDb ((templateAttack + 0.15f + 0.03f * riseDeficit) * 10.0f) / 10.0f, 0.0f, t.transientMaxAttack);
            if (attack > d.proposed.transientAttack + 0.05f)
                d.move (Recommendation::Kind::Transient, TuneSection::Attack, "Sharpened the attack: transient " + num ("%+.0f%%", double (attack * 100.0f)),
                        capital (plural (eventNoun (ctx))) + " rise by only " + num ("%.0f dB", double (a.meanTransientRiseDb)) + " on average; a little transient emphasis restores definition without EQ.",
                        Confidence::Medium, [=] (ChannelParameters& p) { p.transientEnabled = true; p.transientAttack = attack; });
        }
    }
    else if (presence > 0.0f)
    {
        const auto tpl = Profiles::baseline (ctx.profile, ctx.role).toneBands[2];
        const float currentGain = peak.enabled ? peak.gainDb : 0.0f;
        const float templateGain = tpl.enabled ? tpl.gainDb : 0.0f;
        const float gain = roundDb (clamp (templateGain - 0.6f * presence, -0.5f * t.maxEqCutDb, t.maxEqBoostDb));
        if (currentGain - gain >= 0.5f)
        {
            const bool disable = std::fabs (gain) < 0.5f;
            d.move (Recommendation::Kind::EQ, TuneSection::Attack, disable ? "Removed the attack boost" : "Eased the attack region: " + fmtDb (gain) + " at " + fmtHz (freq),
                    "Presence energy is " + num ("%.0f dB", double (presence)) + " above the profile tolerance; the source is already forward.",
                    Confidence::Medium, [=] (ChannelParameters& p) { p.toneBands[2].enabled = ! disable; p.toneBands[2].gainDb = disable ? 0.0f : gain; p.toneBands[2].freqHz = freq; });
        }
        if (t.transientAppropriate && d.proposed.transientEnabled && d.proposed.transientAttack > 0.15f)
            d.move (Recommendation::Kind::Transient, TuneSection::Attack, "Reduced transient emphasis to +15%",
                    "The source is already forward in the attack region; extra transient emphasis would read as harsh.",
                    Confidence::Medium, [] (ChannelParameters& p) { p.transientAttack = 0.15f; });
    }
}

void controlHarshness (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    const ResonancePeak* best = nullptr;
    for (const auto& r : ctx.analysis.resonances)
        if (r.frequencyHz >= t.harshnessMinHz && r.frequencyHz <= t.harshnessMaxHz && r.prominenceDb >= t.resonanceMinProminenceDb && (best == nullptr || r.prominenceDb > best->prominenceDb))
            best = &r;
    const float presence = bandExcess (ctx, t, Band::Presence);

    if (best != nullptr)
    {
        const float f = roundHz (best->frequencyHz);
        const float gain = -clamp (0.5f * best->prominenceDb, 1.5f, t.maxEqCutDb);
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Controlled harshness: " + fmtDb (gain) + " at " + fmtHz (f),
                "A peak stands " + num ("%.0f dB", double (best->prominenceDb)) + " above its surroundings in the harshness region; cutting it keeps detail while removing the edge.",
                best->prominenceDb >= t.resonanceMinProminenceDb + 4.0f ? Confidence::High : Confidence::Medium,
                [=] (ChannelParameters& p) { p.correctiveBands[2] = { true, FilterType::Peak, f, roundDb (gain), 2.5f }; });
    }
    else if (presence > 0.0f)
    {
        const float f = roundHz (bandCentreHz (Band::Presence));
        const float gain = -clamp (0.5f * presence, 1.5f, t.maxEqCutDb);
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Softened the presence region: " + fmtDb (gain) + " at " + fmtHz (f),
                "Presence energy is " + num ("%.0f dB", double (presence)) + " above the profile tolerance; a broad cut keeps it from getting harsh rather than boosting elsewhere.",
                presence > 3.0f ? Confidence::High : Confidence::Medium,
                [=] (ChannelParameters& p) { p.correctiveBands[2] = { true, FilterType::Peak, f, roundDb (gain), 1.2f }; });
    }
}

void shapeAir (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    const float brilliance = bandExcess (ctx, t, Band::Brilliance);
    const float air = bandExcess (ctx, t, Band::Air);
    const float excess = 0.7f * brilliance + 0.3f * air;
    auto& shelf = d.proposed.toneBands[3];
    const float currentGain = shelf.enabled ? shelf.gainDb : 0.0f;
    const float freq = roundHz (t.airHz);

    if (excess < -0.5f)
    {
        const float desired = roundDb (clamp (1.0f + 0.4f * -excess, 1.0f, 0.8f * t.maxEqBoostDb));
        if (currentGain >= desired - 0.5f) return;
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, "Opened the top end: " + fmtDb (desired) + " high shelf at " + fmtHz (freq),
                "Brilliance and air are " + num ("%.0f dB", double (-excess)) + " under the profile target; a gentle shelf adds detail.",
                Confidence::Medium, [=] (ChannelParameters& p) { p.toneBands[3] = { true, FilterType::HighShelf, freq, desired, 0.7f }; });
    }
    else if (excess > 0.5f)
    {
        const auto tpl = Profiles::baseline (ctx.profile, ctx.role).toneBands[3];
        const float templateGain = tpl.enabled ? tpl.gainDb : 0.0f;
        const float gain = roundDb (clamp (templateGain - 0.5f * excess, -0.6f * t.maxEqCutDb, t.maxEqBoostDb));
        if (currentGain - gain < 0.5f) return;
        const bool disable = std::fabs (gain) < 0.5f;
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, disable ? "Removed the high shelf boost" : "Smoothed the top end: " + fmtDb (gain) + " high shelf at " + fmtHz (freq),
                "Brilliance and air are " + num ("%.0f dB", double (excess)) + " above the profile tolerance; the profile wants detail without splash.",
                Confidence::Medium, [=] (ChannelParameters& p) { p.toneBands[3].enabled = ! disable; p.toneBands[3].gainDb = disable ? 0.0f : gain; p.toneBands[3].freqHz = freq; });
    }
}

void setCompression (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    if (! t.compressionAppropriate) return;
    const auto& a = ctx.analysis;
    const Levels L = levels (ctx);
    const float crest = a.crestFactorDb;
    const auto& cur = d.proposed;

    if (crest < t.crestFactorMinDb)
    {
        if (cur.compEnabled)
            d.move (Recommendation::Kind::Compression, TuneSection::Dynamics, "Compression bypassed",
                    "Crest factor is only " + num ("%.0f dB", double (crest)) + ": the source is already dense, and more compression would only add pumping.",
                    Confidence::High, [] (ChannelParameters& p) { p.compEnabled = false; });
        else
            d.note (Recommendation::Kind::Compression, TuneSection::Dynamics, "No compression needed",
                    "Crest factor is only " + num ("%.0f dB", double (crest)) + "; the source is already dense.", Confidence::High);
        return;
    }

    const float severity = clamp ((crest - t.crestFactorMaxDb) / 6.0f, 0.0f, 1.0f);
    const bool needsControl = crest > t.crestFactorMaxDb;
    if (! needsControl && ! cur.compEnabled)
    {
        d.note (Recommendation::Kind::Compression, TuneSection::Dynamics, "Dynamics left open",
                "Crest factor is " + num ("%.0f dB", double (crest)) + ", inside the profile range; nothing to control.", Confidence::Medium);
        return;
    }
    if (L.sparse && ! needsControl) return;

    // Inside the profile range the current (template or user) character is kept and only the
    // threshold is fitted to the measured level; above it, ratio/attack/release scale with severity.
    const float ratio = needsControl ? lerp (t.compRatioMin, t.compRatioMax, severity) : clamp (cur.compRatio, t.compRatioMin, t.compRatioMax);
    const float grDb = needsControl ? lerp (0.8f * t.compTargetGrDb, 1.3f * t.compTargetGrDb, severity) : 0.7f * t.compTargetGrDb;
    const float threshold = roundDb (clamp (L.hitDb - grDb * ratio / (ratio - 1.0f), -60.0f, -3.0f));
    const float attack = needsControl ? std::round (lerp (t.compAttackMaxMs, t.compAttackMinMs, severity)) : clamp (cur.compAttackMs, t.compAttackMinMs, t.compAttackMaxMs);
    const float rate = clamp ((a.transientsPerSecond - 1.0f) / 6.0f, 0.0f, 1.0f);
    const float release = needsControl ? std::round (lerp (t.compReleaseMaxMs, t.compReleaseMinMs, rate) / 5.0f) * 5.0f : clamp (cur.compReleaseMs, t.compReleaseMinMs, t.compReleaseMaxMs);
    const float ratioR = std::round (ratio * 2.0f) * 0.5f;
    const float detHpf = cur.compScHpfHz >= 20.0f ? cur.compScHpfHz : t.compDetectorHpfHz;

    const bool small = cur.compEnabled && std::fabs (cur.compThresholdDb - threshold) < 1.5f && std::fabs (cur.compRatio - ratioR) < 0.6f
                    && std::fabs (cur.compAttackMs - attack) < 0.35f * cur.compAttackMs && std::fabs (cur.compReleaseMs - release) < 0.35f * cur.compReleaseMs;
    if (small) return;

    std::string what = cur.compEnabled ? "Compression re-fitted: " : "Compression added: ";
    what += num ("%.1f:1", double (ratioR)) + ", threshold " + fmtDb (threshold, 0) + ", " + num ("%.0f ms", double (attack)) + " / " + num ("%.0f ms", double (release));
    std::string why = capital (plural (eventNoun (ctx))) + " reach " + num ("%.0f dBFS", double (L.hitDb)) + " with a crest factor of " + num ("%.0f dB", double (crest));
    why += needsControl ? " (above the profile's " + num ("%.0f dB", double (t.crestFactorMaxDb)) + "). " : " (inside the profile range). ";
    why += "The threshold is set for about " + num ("%.0f dB", double (grDb)) + " of gain reduction on a normal " + eventNoun (ctx) + "; attack lets the start through, release follows the playing ("
         + num ("%.1f", double (a.transientsPerSecond)) + " " + plural (eventNoun (ctx)) + "/s).";
    if (L.sparse) why += " Long quiet sections in the capture reduce confidence.";

    d.move (Recommendation::Kind::Compression, TuneSection::Dynamics, what, why,
            (severity > 0.4f && ! L.sparse) ? Confidence::High : Confidence::Medium,
            [=] (ChannelParameters& p)
            {
                p.compEnabled = true; p.compThresholdDb = threshold; p.compRatio = ratioR;
                p.compAttackMs = attack; p.compReleaseMs = release;
                if (p.compKneeDb < 3.0f) p.compKneeDb = 6.0f;
                if (detHpf >= 20.0f) p.compScHpfHz = detHpf;
            });
}

void setGate (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float fundamentalHz)
{
    const auto& cur = d.proposed;
    if (! t.gateAppropriate)
    {
        if (cur.gateEnabled)
            d.move (Recommendation::Kind::Gate, TuneSection::Bleed, "Gate bypassed",
                    "Gating is not appropriate for this source in this profile; bleed is managed with filtering instead.",
                    Confidence::High, [] (ChannelParameters& p) { p.gateEnabled = false; });
        return;
    }

    const auto& a = ctx.analysis;
    const Levels L = levels (ctx);
    const float bleed = a.bleedEstimate;
    const float floorGap = L.hitDb - L.floorDb;
    const float detHpf = clamp (std::max (t.gateDetectorHpfHz, fundamentalHz > 0.0f ? 0.7f * fundamentalHz : 0.0f), 0.0f, 250.0f);

    // A continuous source (held notes, sustained singing) has no gaps: the "floor" is the source itself.
    const bool sustained = a.silencePercent < 5.0f && a.transientsPerSecond < 0.5f && floorGap < 20.0f;
    if (sustained)
    {
        if (cur.gateEnabled)
            d.move (Recommendation::Kind::Gate, TuneSection::Bleed, "Clean-up bypassed: the source is continuous",
                    "There were no gaps in the capture (the level between " + plural (eventNoun (ctx)) + " sits only " + num ("%.0f dB", double (floorGap))
                    + " under them), so an expander would cut into the source itself rather than into bleed.",
                    Confidence::High, [] (ChannelParameters& p) { p.gateEnabled = false; });
        else
            d.note (Recommendation::Kind::Gate, TuneSection::Bleed, "No clean-up needed", "The source played continuously; there is nothing between " + plural (eventNoun (ctx)) + " to clean up.", Confidence::Medium);
        return;
    }

    if (bleed > t.bleedGateThreshold)
    {
        const float threshold = roundDb (clamp (L.floorDb + 0.4f * floorGap, -70.0f, L.hitDb - 12.0f));
        const float range = std::round (clamp (10.0f + 30.0f * bleed, 8.0f, t.gateMaxRangeDb));
        const float hold = a.meanDecayMs > 0.0f ? std::round (clamp (0.6f * a.meanDecayMs, 40.0f, 200.0f)) : cur.gateHoldMs;
        const float release = a.meanDecayMs > 0.0f ? std::round (clamp (0.8f * a.meanDecayMs, 60.0f, 300.0f)) : cur.gateReleaseMs;
        const bool small = cur.gateEnabled && std::fabs (cur.gateThresholdDb - threshold) < 2.0f && std::fabs (cur.gateRangeDb - range) < 3.0f;
        if (small) return;
        std::string what = (cur.gateEnabled ? "Expander re-fitted: threshold " : "Expander enabled: threshold ") + fmtDb (threshold, 0) + ", " + num ("%.0f dB range", double (range));
        std::string why = "Between " + plural (eventNoun (ctx)) + " the channel sits at " + num ("%.0f dBFS", double (L.floorDb)) + " while " + plural (eventNoun (ctx)) + " reach " + num ("%.0f dBFS", double (L.hitDb))
                        + ": bleed from nearby sources. The range is conservative (expansion, not a hard mute) so soft " + plural (eventNoun (ctx)) + " survive";
        if (a.meanDecayMs > 0.0f) why += ", and hold/release follow the measured decay (" + num ("%.0f ms", double (a.meanDecayMs)) + ")";
        if (detHpf >= 20.0f) why += "; the detector ignores energy below " + fmtHz (detHpf) + " so low bleed does not open it";
        why += ".";
        d.move (Recommendation::Kind::Gate, TuneSection::Bleed, what, why, bleed > 0.6f ? Confidence::High : Confidence::Medium,
                [=] (ChannelParameters& p)
                {
                    p.gateEnabled = true; p.gateThresholdDb = threshold; p.gateRangeDb = range;
                    p.gateHoldMs = hold; p.gateReleaseMs = release;
                    if (p.gateRatio < 3.0f) p.gateRatio = 4.0f;
                    if (p.gateHysteresisDb < 2.0f) p.gateHysteresisDb = 3.0f;
                    if (detHpf >= 20.0f) p.gateScHpfHz = detHpf;
                });
    }
    else if (bleed < 0.12f && cur.gateEnabled)
    {
        d.move (Recommendation::Kind::Gate, TuneSection::Bleed, "Gate bypassed: no meaningful bleed",
                "Between " + plural (eventNoun (ctx)) + " the channel sits " + num ("%.0f dB", double (floorGap)) + " below them; a gate would only risk cutting soft playing.",
                Confidence::High, [] (ChannelParameters& p) { p.gateEnabled = false; });
    }
    else if (cur.gateEnabled)
    {
        // Moderate bleed: keep a gentle expander and fit its threshold to the actual level.
        const float threshold = roundDb (clamp (L.floorDb + 0.4f * floorGap, -70.0f, L.hitDb - 12.0f));
        const float range = std::min (cur.gateRangeDb, 16.0f);
        if (std::fabs (cur.gateThresholdDb - threshold) < 2.0f && std::fabs (cur.gateRangeDb - range) < 1.0f) return;
        d.move (Recommendation::Kind::Gate, TuneSection::Bleed, "Expander kept gentle: threshold " + fmtDb (threshold, 0) + ", " + num ("%.0f dB range", double (range)),
                "Bleed is moderate (floor " + num ("%.0f dB", double (floorGap)) + " below the " + plural (eventNoun (ctx)) + "); the expander is fitted to the measured level with a shallow range so nothing is chopped.",
                Confidence::Medium,
                [=] (ChannelParameters& p) { p.gateThresholdDb = threshold; p.gateRangeDb = range; if (detHpf >= 20.0f && p.gateScHpfHz < 20.0f) p.gateScHpfHz = detHpf; });
    }
    else
    {
        d.note (Recommendation::Kind::Gate, TuneSection::Bleed, "No gate needed",
                "Bleed is low (floor " + num ("%.0f dB", double (floorGap)) + " below the " + plural (eventNoun (ctx)) + ").", Confidence::Medium);
    }
}

void setMixGain (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    if (! ctx.hasOutput || ! ctx.output.valid || ctx.output.peakDb <= -60.0f || ctx.analysis.silencePercent > 60.0f) return;
    const float delta = std::round (t.mixPeakTargetDb - ctx.output.peakDb);
    if (std::fabs (delta) < 2.0f) return;
    const float bounded = clamp (delta, -8.0f, 8.0f);
    const float newTrim = clamp (d.proposed.outputTrimDb + bounded, -24.0f, 24.0f);
    d.move (Recommendation::Kind::MixGain, TuneSection::Mix, std::string (bounded > 0 ? "Raised" : "Lowered") + " the mix level " + fmtDb (bounded, 0),
            "The processed output peaks at " + num ("%.0f dBFS", double (ctx.output.peakDb)) + "; this channel usually sits around " + num ("%.0f dBFS", double (t.mixPeakTargetDb))
            + " in " + mixNoun (ctx) + ". This is the plugin's output trim, not capture gain.",
            Confidence::Medium, [=] (ChannelParameters& p) { p.outputTrimDb = newTrim; });
}

void stereoBalanceNote (const TuneContext& ctx, TuneDecisions& d)
{
    const auto& a = ctx.analysis;
    if (a.numChannels < 2 || std::fabs (a.stereoBalanceDb) <= 3.0f) return;
    d.note (Recommendation::Kind::Info, TuneSection::Notes,
            "Stereo balance is offset " + num ("%.0f dB", double (std::fabs (a.stereoBalanceDb))) + (a.stereoBalanceDb > 0 ? " toward the left" : " toward the right"),
            "Check microphone placement or the individual preamp gains; Dine does not re-balance the image automatically.", Confidence::Medium);
}


void controlSibilance (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    const auto& cur = d.proposed;
    if (! t.deEssAppropriate)
    {
        if (cur.deEssEnabled)
            d.move (Recommendation::Kind::EQ, TuneSection::Tone, "S control bypassed",
                    "This source does not need sibilance control in this profile.", Confidence::High,
                    [] (ChannelParameters& p) { p.deEssEnabled = false; });
        return;
    }
    const float sib = ctx.analysis.sibilanceDb;
    if (sib <= -100.0f) return; // not measured (too little loud material)
    const Levels L = levels (ctx);
    const float excess = sib - t.sibilanceMaxDb;

    if (excess > 0.0f)
    {
        const float range = std::round (clamp (3.0f + 1.0f * excess, 3.0f, t.deEssMaxRangeDb));
        // The band above the split needs to come down when it approaches the full-band level of a loud phrase.
        const float threshold = roundDb (clamp (L.hitDb + t.sibilanceMaxDb - 2.0f, -60.0f, -6.0f));
        const float hz = roundHz (t.deEssHz);
        const bool small = cur.deEssEnabled && std::fabs (cur.deEssThresholdDb - threshold) < 2.0f && std::fabs (cur.deEssRangeDb - range) < 1.5f && near (cur.deEssHz, hz, 0.15f);
        if (small) return;
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, (cur.deEssEnabled ? "S control re-fitted: up to " : "S control added: up to ") + fmtDb (-range, 0) + " above " + fmtHz (hz),
                "The sharpest " + std::string (ctx.analysis.sibilancePercent > 15.0f ? "S sounds are frequent and " : "S sounds ") + "sit " + num ("%.0f dB", double (excess))
                + " above what the profile accepts (high band " + num ("%.0f dB", double (sib)) + " re the whole voice). Only those moments are turned down; the rest of the voice is untouched.",
                excess > 4.0f ? Confidence::High : Confidence::Medium,
                [=] (ChannelParameters& p) { p.deEssEnabled = true; p.deEssHz = hz; p.deEssThresholdDb = threshold; p.deEssRangeDb = range; });
    }
    else if (excess < -6.0f && cur.deEssEnabled)
    {
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, "S control bypassed",
                "S sounds sit " + num ("%.0f dB", double (-excess)) + " below the level that needs control; leaving the de-esser on would only dull the voice.",
                Confidence::Medium, [] (ChannelParameters& p) { p.deEssEnabled = false; });
    }
    else if (cur.deEssEnabled && cur.deEssRangeDb > 4.0f && excess < -2.0f)
    {
        d.move (Recommendation::Kind::EQ, TuneSection::Tone, "S control kept gentle: up to " + fmtDb (-3.0f, 0),
                "S sounds are inside the profile tolerance; the template de-esser is eased so it only catches the sharpest moments.",
                Confidence::Medium, [] (ChannelParameters& p) { p.deEssRangeDb = 3.0f; });
    }
    else if (! cur.deEssEnabled)
    {
        d.note (Recommendation::Kind::Info, TuneSection::Tone, "No S control needed",
                "S sounds sit inside the profile tolerance (high band " + num ("%.0f dB", double (sib)) + " re the whole voice).", Confidence::Medium);
    }
}

void setWidth (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    const auto& cur = d.proposed;
    if (! t.widthAppropriate)
    {
        if (cur.widthEnabled && std::fabs (cur.widthAmount - 1.0f) > 0.01f)
            d.move (Recommendation::Kind::Info, TuneSection::Mix, "Width left as recorded",
                    "Stereo width changes are not part of this source's profile.", Confidence::High,
                    [] (ChannelParameters& p) { p.widthAmount = 1.0f; });
        return;
    }
    const auto& a = ctx.analysis;
    if (a.numChannels < 2)
    {
        if (cur.widthEnabled && cur.widthAmount > 1.01f)
            d.move (Recommendation::Kind::Info, TuneSection::Mix, "Width reset: the source is mono",
                    "A mono channel has no stereo image to widen; the width control is left at 1.0.", Confidence::High,
                    [] (ChannelParameters& p) { p.widthAmount = 1.0f; });
        return;
    }
    const float corr = a.stereoCorrelation;
    float width = t.widthTarget;
    std::string why;
    if (corr < t.correlationMin)
    {
        // Too wide / partly out of phase: narrow toward mono-compatible, keep the low end centred.
        width = clamp (t.widthTarget * (0.6f + 0.4f * clamp (corr / t.correlationMin, 0.0f, 1.0f)), t.widthMin, t.widthTarget);
        why = "Left and right only agree " + num ("%.0f %%", double (corr * 100.0f)) + " of the time (correlation " + num ("%.2f", double (corr)) + "), which collapses on mono systems and phones. The image is narrowed";
    }
    else if (corr > 0.95f)
    {
        width = clamp (t.widthTarget * 1.15f, t.widthTarget, t.widthMax);
        why = "The source is nearly mono (correlation " + num ("%.2f", double (corr)) + "); a little width gives it the space the profile wants";
    }
    else
    {
        why = "The stereo image is healthy (correlation " + num ("%.2f", double (corr)) + "); width sits at the profile target";
    }
    width = std::round (width * 20.0f) / 20.0f;
    const float monoBelow = t.monoBelowHz;
    why += monoBelow >= 20.0f ? ", and everything below " + fmtHz (monoBelow) + " is kept in the centre so the low end stays solid." : ".";
    const bool small = cur.widthEnabled && std::fabs (cur.widthAmount - width) < 0.06f && std::fabs (cur.widthMonoBelowHz - monoBelow) < 15.0f;
    if (small) return;
    d.move (Recommendation::Kind::Info, TuneSection::Mix, "Width set to " + num ("%.2f", double (width)) + (monoBelow >= 20.0f ? ", low end centred below " + fmtHz (monoBelow) : std::string()),
            why, corr < t.correlationMin ? Confidence::High : Confidence::Medium,
            [=] (ChannelParameters& p) { p.widthEnabled = true; p.widthAmount = width; p.widthMonoBelowHz = monoBelow; });
}

void setLoudness (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d)
{
    const auto& cur = d.proposed;
    const auto& a = ctx.analysis;
    if (! t.loudnessTargetAppropriate)
    {
        // Room feed: safety limiting only, at the profile ceiling.
        if (! cur.limiterEnabled || std::fabs (cur.limiterCeilingDb - t.truePeakCeilingDb) > 0.3f)
            d.move (Recommendation::Kind::MixGain, TuneSection::Mix, "Safety limiter set to " + fmtDb (t.truePeakCeilingDb, 1),
                    "This output has no loudness target; the limiter only stops peaks from clipping the next device.", Confidence::High,
                    [=] (ChannelParameters& p) { p.limiterEnabled = true; p.limiterCeilingDb = t.truePeakCeilingDb; });
        return;
    }
    if (a.loudnessLufs <= -100.0f || a.silencePercent > 60.0f) return;
    if (a.peakDb < t.capturePeakMinDb)
    {
        d.note (Recommendation::Kind::MixGain, TuneSection::Mix, "Loudness waits for a healthy input level",
                "The mix reaches the plugin " + num ("%.0f dB", double (t.capturePeakMinDb - a.peakDb)) + " below the healthy range (see Input). Raise it at the console first, then Re-Tune; "
                "pushing the plugin's gain that far would only make the limiter work hard.", Confidence::High);
        return;
    }

    // The plugin's own gain is on the way; what the capture measured is the input. Predict the output loudness
    // from the input loudness plus the current output trim (the limiter is assumed to only touch peaks).
    const float predicted = a.loudnessLufs + cur.outputTrimDb;
    const float delta = t.targetLufs - predicted;
    const float ceiling = t.truePeakCeilingDb;
    const bool ceilingOk = cur.limiterEnabled && std::fabs (cur.limiterCeilingDb - ceiling) < 0.3f;
    if (std::fabs (delta) <= t.loudnessToleranceLu && ceilingOk)
    {
        d.note (Recommendation::Kind::MixGain, TuneSection::Mix, "Loudness on target",
                "The mix measures " + num ("%.1f LUFS", double (predicted)) + " against a target of " + num ("%.0f LUFS", double (t.targetLufs)) + " for this output.", Confidence::High);
        return;
    }
    const float bounded = clamp (std::round (delta * 2.0f) * 0.5f, -12.0f, 12.0f);
    const float newTrim = clamp (cur.outputTrimDb + bounded, -24.0f, 24.0f);
    std::string what = std::fabs (delta) > t.loudnessToleranceLu
        ? (bounded > 0 ? "Raised the output " : "Lowered the output ") + fmtDb (bounded, 1) + " toward " + num ("%.0f LUFS", double (t.targetLufs))
        : "Limiter ceiling set to " + fmtDb (ceiling, 1);
    std::string why = "The mix measures " + num ("%.1f LUFS", double (predicted)) + "; this output wants about " + num ("%.0f LUFS", double (t.targetLufs))
                    + " with true peaks under " + fmtDb (ceiling, 1) + ". The limiter catches what the extra level pushes over the ceiling";
    if (bounded > 6.0f) why += " (a large push: check the limiter's gain reduction stays under a few dB)";
    why += ".";
    d.move (Recommendation::Kind::MixGain, TuneSection::Mix, what, why, std::fabs (delta) > 3.0f ? Confidence::High : Confidence::Medium,
            [=] (ChannelParameters& p) { p.outputTrimDb = newTrim; p.limiterEnabled = true; p.limiterCeilingDb = ceiling; });
}

} // namespace tune
} // namespace livemix
