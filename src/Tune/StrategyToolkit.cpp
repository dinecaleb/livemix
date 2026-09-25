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
    // Gain staging follows the level the source really plays at, not an isolated click: see musicalPeakDb.
    const float peak = a.musicalPeakDb > -119.0f ? a.musicalPeakDb : a.peakDb;
    if (a.clipCount > 0 || peak > -0.5f) return std::min (std::round (centre - peak) - 2.0f, -3.0f);
    if (peak > t.capturePeakMaxDb) return std::min (std::round (centre - peak), -1.0f);
    if (peak < t.capturePeakMinDb) return std::max (std::round (t.capturePeakMinDb + 3.0f - peak), 1.0f);
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
    // Gain staging reads the level the source really plays at, so one click cannot call a healthy channel hot.
    // The raw peak is still the clipping test - a clipped sample is damage whatever caused it.
    const float peak = a.musicalPeakDb > -119.0f ? a.musicalPeakDb : a.peakDb;
    const bool spiky = a.peakDb - peak > 6.0f;

    if (spiky && a.peakDb > -0.5f)
    {
        // The track's loudest sample is far above anything musical on it: a click, not the source. Turning the
        // preamp down would only make the source quieter and leave the click at full scale.
        report.inputHealth = "Clicks";
        d.note (Recommendation::Kind::CaptureGain, TuneSection::Input, "Isolated clicks on this input, not a level problem",
                "The loudest sample reaches " + num ("%.1f dBFS", double (a.peakDb)) + " while the source itself plays around "
                + num ("%.0f dBFS", double (peak)) + " - " + num ("%.0f dB", double (a.peakDb - peak)) + " below it. That is a click or a pop "
                "(a patch change, phantom power, a loose connector), not the instrument. The level is left alone; check the cable and the connector.",
                Confidence::High);
    }
    else if (a.clipCount > 0 || peak > -0.5f)
    {
        report.inputHealth = "Clipping";
        const float delta = std::max (captureGainToHealthyDb (a, t), -step);
        report.suggestedCaptureGainDb = delta;
        d.note (Recommendation::Kind::CaptureGain, TuneSection::Input, "Reduce preamp approximately " + fmtDb (delta, 0),
                std::to_string (a.clipCount) + " clipped samples were detected. Digital clipping cannot be repaired after the converter." + preampNote,
                Confidence::High);
    }
    else if (peak > t.capturePeakMaxDb)
    {
        report.inputHealth = "Hot";
        const float delta = std::max (captureGainToHealthyDb (a, t), -step);
        report.suggestedCaptureGainDb = delta;
        d.note (Recommendation::Kind::CaptureGain, TuneSection::Input, "Reduce preamp approximately " + fmtDb (delta, 0),
                "Peaks reached " + num ("%.1f dBFS", double (peak)) + ", above the healthy range of " + num ("%.0f", double (t.capturePeakMinDb))
                + " to " + num ("%.0f dBFS", double (t.capturePeakMaxDb)) + ". A louder " + eventNoun (ctx) + " could clip the converter." + preampNote,
                Confidence::High);
    }
    else if (peak < t.capturePeakMinDb)
    {
        report.inputHealth = "Low";
        // Conservative: aim for the low edge of the healthy range plus a little, never more than one bounded step.
        const float delta = std::min (captureGainToHealthyDb (a, t), step);
        report.suggestedCaptureGainDb = delta;
        std::string why = "Peaks reached only " + num ("%.1f dBFS", double (peak)) + ". The signal is clean but below the healthy range of "
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

void capHighPassToFundamental (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float fundamentalHz)
{
    if (fundamentalHz <= 0.0f) return;
    const auto& cur = d.proposed;
    if (! cur.hpfEnabled) return;
    const float cap = roundHz (0.8f * fundamentalHz);
    if (cur.hpfHz <= cap + 0.5f) return;
    // Deliberately not clamped to t.hpfMinHz: that number says how high the filter may be
    // *chosen*, and this is the rule that says how high it may ever sit.
    const float target = roundHz (clamp (cap, 20.0f, t.hpfMaxHz));
    d.move (Recommendation::Kind::Filter, TuneSection::Tone,
            "High-pass lowered to " + fmtHz (target) + ", under the source's own lowest note",
            "The listen measured this source's fundamental at " + fmtHz (fundamentalHz) + ", and the high-pass sat at "
            + fmtHz (cur.hpfHz) + " - above it. A filter there takes the body out of the sound it is meant to be cleaning up: "
            "a low voice reads as thin and, because the weight is what carries it, quieter than it is. It is lowered to "
            + fmtHz (target) + " and the cleaning up is left to the rest of the chain.",
            Confidence::High,
            [=] (ChannelParameters& p) { p.hpfEnabled = true; p.hpfHz = target; });
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

    // A boost and a cut never land inside the same octave in one pass. The corrective bands
    // have already been placed on what the listen actually measured - a resonance, a ring, a
    // hard edge - and a lift on top of one is two filters arguing about the same octave: what
    // comes out is a shape nobody chose, and the thing that was cut for a reason comes half
    // way back. The measured cut stands. A lift that would have followed is left off, and a
    // lift the profile's own template put there is taken back out, both with the reason.
    const EQBandParams* clash = nullptr;
    for (const auto& b : d.proposed.correctiveBands)
        if (b.enabled && b.gainDb <= -1.5f && b.freqHz > 0.0f && freq < b.freqHz * 2.0f && b.freqHz < freq * 2.0f) { clash = &b; break; }
    const float currentGain = peak.enabled ? peak.gainDb : 0.0f;
    bool tookItOut = false;
    if (clash != nullptr && currentGain > 0.5f)
    {
        tookItOut = true;
        d.move (Recommendation::Kind::EQ, TuneSection::Attack, "Definition taken back out at " + fmtHz (freq),
                "The listen found something to cut at " + fmtHz (clash->freqHz) + " (" + fmtDb (clash->gainDb)
                + "), and the lift this source normally carries at " + fmtHz (freq) + " sits inside the same octave. "
                "One filter pulling where another is pushing is not a tone; it is a wobble, and the part that was cut for a "
                "reason comes half way back. The cut stays and the lift goes.",
                Confidence::Medium,
                [] (ChannelParameters& p) { p.toneBands[2].enabled = false; p.toneBands[2].gainDb = 0.0f; });
    }

    if (presence < 0.0f || soft)
    {
        const float deficit = std::max (-presence, 0.0f);
        const float desired = roundDb (clamp (1.5f + 0.5f * deficit, 1.5f, t.maxEqBoostDb));
        if (presence < 0.0f && currentGain < desired - 0.5f)
        {
            if (clash != nullptr)
            {
                if (! tookItOut)
                    d.note (Recommendation::Kind::EQ, TuneSection::Attack, "Definition left where it is",
                            "Presence energy is " + num ("%.0f dB", double (deficit)) + " under the profile target, so a lift at " + fmtHz (freq)
                            + " would normally follow - but " + fmtDb (clash->gainDb) + " is being taken out at " + fmtHz (clash->freqHz)
                            + ", inside the same octave. Boosting across a cut that was made on what the listen measured only brings back what "
                            "was wrong with it. The cut stands; if the source still needs to come forward, its level is the honest way.",
                            Confidence::Medium);
            }
            else
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
        const float templateGain = tpl.enabled ? tpl.gainDb : 0.0f;
        const float gain = roundDb (clamp (templateGain - 0.6f * presence, -0.5f * t.maxEqCutDb, t.maxEqBoostDb));
        // With a measured cut inside the same octave this band stays out of it altogether: the
        // broad cut the harshness rule just placed is the move, and a second filter in the same
        // octave - up or down - is the wobble this rule exists to prevent.
        if (clash == nullptr && currentGain - gain >= 0.5f)
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

void setSampleReplacement (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float fundamentalHz)
{
    if (! t.sampleAppropriate) return;
    // One trigger per drum: the inside / top microphone. The outside and bottom microphones
    // keep their own chains and are never fitted, so two samples never land on one hit.
    if (ctx.role == ChannelRole::KickOut || ctx.role == ChannelRole::SnareBottom) return;
    const auto& a = ctx.analysis;
    const auto& cur = d.proposed;
    const Levels L = levels (ctx);
    if (L.hitDb < -70.0f) return;                        // nothing was heard: nothing to fit

    // The threshold sits between the bleed and the hits when the listen could tell them
    // apart, and above the floor when it could not. Every number is absolute from the
    // capture, so the same listen always fits the same threshold.
    const float trim = ctx.current.inputTrimDb;
    const bool bleedKnown = a.bleedLevelDb > -119.0f;
    const float bleedDb = bleedKnown ? a.bleedLevelDb + trim : L.floorDb;
    const float hitDb = a.eventLevelDb > -119.0f ? a.eventLevelDb + trim : L.hitDb;
    // The microphone's peak in normal playing (spike-resistant): where the sample's own peak
    // goes, so a 50 % blend moves the drum's level by nothing. The event level is a 10 ms frame
    // level, 10 dB or more under the peak on a kick, and on a tom it is mostly bleed.
    const float peakDb = a.musicalPeakDb > -119.0f ? a.musicalPeakDb + trim : hitDb;
    float threshold = bleedKnown ? bleedDb + 0.6f * (hitDb - bleedDb) : L.floorDb + 0.5f * (hitDb - L.floorDb);
    threshold = std::max (threshold, bleedDb + 6.0f);
    // A tom is sparse and hears the whole kit: its real hits sit near its peak and everything
    // far under that is another drum, so its trigger never opens more than 12 dB under the
    // peak (kick and snare, played constantly, 18 dB). QUEENSVIEW, 2026-09-24: with the halfway
    // rule alone a rack tom fired 114 times a minute for two real hits.
    const bool tom = roleFamily (ctx.role) == RoleFamily::Tom;
    threshold = std::max (threshold, peakDb - (tom ? 12.0f : 18.0f));
    threshold = roundDb (clamp (threshold, -70.0f, peakDb - 3.0f));
    const float level = roundDb (clamp (peakDb, -40.0f, 0.0f));
    // The band: the profile's, with the low edge kept under the drum's own fundamental.
    const float detHpf = std::round (clamp (fundamentalHz > 0.0f ? std::min (t.sampleDetHpfHz, 0.7f * fundamentalHz) : t.sampleDetHpfHz, 20.0f, 2000.0f));
    const float detLpf = std::round (clamp (t.sampleDetLpfHz, detHpf * 2.0f, 20000.0f));
    const float mask = std::round (clamp (t.sampleMaskMs, 1.0f, 500.0f));
    const float rise = std::round (clamp (t.sampleRiseDb, 0.0f, 40.0f));

    // The drum's own pitch, for a sample that follows it (toms). Kept when the listen found none.
    const float drumHz = fundamentalHz > 0.0f ? std::round (fundamentalHz * 10.0f) * 0.1f : cur.replaceDrumHz;
    const bool same = std::fabs (cur.replaceThresholdDb - threshold) < 0.5f && std::fabs (cur.replaceGainDb - level) < 0.5f
                   && std::fabs (cur.replaceDetHpfHz - detHpf) < 0.5f && std::fabs (cur.replaceDetLpfHz - detLpf) < 0.5f
                   && std::fabs (cur.replaceMaskMs - mask) < 0.5f && std::fabs (cur.replaceRiseDb - rise) < 0.5f
                   && std::fabs (cur.replaceDrumHz - drumHz) < 0.05f;
    if (same) return;

    std::string what = "Sample trigger fitted: threshold " + fmtDb (threshold, 0) + ", sample at " + fmtDb (level, 0);
    std::string why = bleedKnown
        ? "Between " + plural (eventNoun (ctx)) + " the microphone hears the rest of the kit at " + num ("%.0f dBFS", double (bleedDb))
          + " and the " + plural (eventNoun (ctx)) + " themselves reach " + num ("%.0f dBFS", double (hitDb))
          + "; the trigger sits between the two, and only an onset that jumps " + num ("%.0f dB", double (rise)) + " counts."
        : "The listen found no separable bleed on this microphone; the trigger sits halfway between the floor ("
          + num ("%.0f dBFS", double (L.floorDb)) + ") and the " + plural (eventNoun (ctx)) + " (" + num ("%.0f dBFS", double (hitDb)) + ").";
    why += " The sample's own peak is placed at the microphone's own peak (" + num ("%.0f dBFS", double (peakDb)) + "), so blending it in changes the drum's level by nothing.";
    if (fundamentalHz > 0.0f && cur.replaceFollowDrum) why += " The drum rings at " + fmtHz (fundamentalHz) + ", and a sample that follows the drum plays at that pitch.";
    why += " It plays only while the stage is switched on";
    why += cur.replaceEnabled ? "." : " - it is off; switch it on from the Sample stage to hear it.";
    d.move (Recommendation::Kind::Sample, TuneSection::Bleed, what, why, bleedKnown ? Confidence::High : Confidence::Medium,
            [=] (ChannelParameters& p)
            {
                p.replaceThresholdDb = threshold; p.replaceGainDb = level;
                p.replaceDetHpfHz = detHpf; p.replaceDetLpfHz = detLpf;
                p.replaceMaskMs = mask; p.replaceRiseDb = rise;
                p.replaceDrumHz = drumHz;
            });
}

void setGate (const TuneContext& ctx, const SourceTargets& t, TuneDecisions& d, float fundamentalHz)
{
    const auto& cur = d.proposed;
    const RoleFamily family = roleFamily (ctx.role);
    const bool closeDrum = family == RoleFamily::Kick || family == RoleFamily::Snare || family == RoleFamily::Tom || family == RoleFamily::HiHat;

    // ---- a sampled microphone: the sample carries the drum's body, the microphone supplies the
    // attack, and everything the microphone hears between hits is dirt under a clean sample.
    // The gate is far harder than a profile would ever fit on a microphone that is on its own.
    if (ctx.sampled && closeDrum)
    {
        const auto& a = ctx.analysis;
        const Levels L = levels (ctx);
        const float floorGap = L.hitDb - L.floorDb;
        if (floorGap < 6.0f) return;                      // nothing to gate between: leave what is there
        const float detHpf = clamp (std::max (t.gateDetectorHpfHz, fundamentalHz > 0.0f ? 0.7f * fundamentalHz : 0.0f), 0.0f, 250.0f);
        // A snare's ghost notes live 20 to 30 dB under its hits and the sample never fires on
        // them, so a snare keeps a shallower, gentler expander with its threshold low: the
        // ghost notes come through the microphone, the sample carries the hits. A hat plays
        // through everything and keeps the same courtesy. A kick or a tom has no ghost notes
        // to keep and closes hard.
        const bool snare = family == RoleFamily::Snare || family == RoleFamily::HiHat;
        float threshold = roundDb (clamp (L.floorDb + (snare ? 0.4f : 0.6f) * floorGap, -70.0f, L.hitDb - (snare ? 14.0f : 9.0f)));
        // Never above the sample's own trigger: a stroke that fires the sample must open the
        // microphone too, or the attack is cut and the body arrives from nowhere. The engine
        // opens the gate on every trigger regardless; this keeps the two numbers honest with
        // each other, so what the Inspector shows is what happens. The trigger was fitted
        // just before this (setSampleReplacement runs first on a drum strategy), absolute
        // from the same listen, so the rule is as repeatable as the rest.
        if (cur.replaceThresholdDb > -119.0f)
            threshold = roundDb (clamp (std::min (threshold, cur.replaceThresholdDb - 3.0f), L.floorDb + 3.0f, L.hitDb - 3.0f));
        const float range = snare ? 20.0f : std::round (clamp (t.gateMaxRangeDb + 15.0f, 20.0f, 50.0f));
        const float ratio = snare ? 4.0f : 10.0f;
        const float hold = a.meanDecayMs > 0.0f ? std::round (clamp (0.4f * a.meanDecayMs, 25.0f, 120.0f)) : 40.0f;
        const float release = a.meanDecayMs > 0.0f ? std::round (clamp (0.5f * a.meanDecayMs, 40.0f, 200.0f)) : 80.0f;
        const bool same = cur.gateEnabled && std::fabs (cur.gateThresholdDb - threshold) < 1.0f && std::fabs (cur.gateRangeDb - range) < 1.0f
                       && std::fabs (cur.gateHoldMs - hold) < 1.0f && std::fabs (cur.gateReleaseMs - release) < 1.0f && std::fabs (cur.gateRatio - ratio) < 0.5f;
        if (same) return;
        std::string why = "The sample carries this drum's body now, so the microphone only has to supply the attack: between " + plural (eventNoun (ctx))
                + " it closes " + num ("%.0f dB", double (range)) + " (it sits at " + num ("%.0f dBFS", double (L.floorDb)) + " there, the "
                + plural (eventNoun (ctx)) + " reach " + num ("%.0f dBFS", double (L.hitDb)) + "), holds " + num ("%.0f ms", double (hold))
                + " and lets go in " + num ("%.0f ms", double (release)) + ".";
        why += family == RoleFamily::Snare ? " On a snare the expander stays shallow and its threshold low, so the ghost notes - which the sample never fires on - still come through the microphone."
             : family == RoleFamily::HiHat ? " On a hat the expander stays shallow and its threshold low: the hat plays through everything, and only what sits well under its own strokes is turned down."
             : " Nothing the microphone hears of the rest of the kit is left under a clean sample.";
        why += " Every hit the sample fires on opens it too, so a soft stroke is never a sample with no microphone under it.";
        d.move (Recommendation::Kind::Gate, TuneSection::Bleed,
                "Gate tightened for the sample: threshold " + fmtDb (threshold, 0) + ", " + num ("%.0f dB range", double (range)), why,
                Confidence::High,
                [=] (ChannelParameters& p)
                {
                    p.gateEnabled = true; p.gateThresholdDb = threshold; p.gateRangeDb = range; p.gateRatio = ratio;
                    p.gateHoldMs = hold; p.gateReleaseMs = release;
                    if (p.gateHysteresisDb < 3.0f) p.gateHysteresisDb = snare ? 3.0f : 4.0f;
                    if (detHpf >= 20.0f) p.gateScHpfHz = detHpf;
                });
        return;
    }

    // ---- a hi-hat in a sampled kit: the profile never gates a hat on its own (it plays
    // through everything), but once the kick and snare are carried by samples, what the hat
    // microphone hears of them is the dirt in a clean kit. A gentle expander, never a gate.
    if (ctx.kitSampled && family == RoleFamily::HiHat && ! t.gateAppropriate)
    {
        const auto& a = ctx.analysis;
        const Levels L = levels (ctx);
        const float floorGap = L.hitDb - L.floorDb;
        if (a.bleedEstimate > 0.25f && floorGap >= 10.0f)
        {
            const float threshold = roundDb (clamp (L.floorDb + 0.35f * floorGap, -70.0f, L.hitDb - 12.0f));
            const float range = 10.0f;
            if (cur.gateEnabled && std::fabs (cur.gateThresholdDb - threshold) < 2.0f && std::fabs (cur.gateRangeDb - range) < 1.0f) return;
            d.move (Recommendation::Kind::Gate, TuneSection::Bleed, "Gentle expander for the sampled kit: threshold " + fmtDb (threshold, 0) + ", 10 dB range",
                    "The kick and snare are carried by samples now, so what this microphone hears of them between the hat's own strokes is the dirt left in an otherwise clean kit. "
                    "A shallow expander (10 dB, 2:1) turns it down without ever cutting into the hat.",
                    Confidence::Medium,
                    [=] (ChannelParameters& p)
                    {
                        p.gateEnabled = true; p.gateThresholdDb = threshold; p.gateRangeDb = range;
                        p.gateRatio = 2.0f; p.gateAttackMs = 0.5f; p.gateHoldMs = 60.0f; p.gateReleaseMs = 120.0f; p.gateHysteresisDb = 3.0f;
                        p.gateScHpfHz = 200.0f;
                    });
        }
        return;
    }

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

    // A drum microphone without a sample in a sampled kit (a kick-out, a snare-bottom, a tom on
    // its own): its bleed of the sampled drums is the dirt now, so the expander is fitted more
    // readily and closes further than the profile would ask on a kit that is all microphones.
    const bool kitNeighbour = ctx.kitSampled && closeDrum && ! ctx.sampled;
    const float gateFrom = kitNeighbour ? 0.5f * t.bleedGateThreshold : t.bleedGateThreshold;
    const float extraRange = kitNeighbour ? 6.0f : 0.0f;
    if (bleed > gateFrom)
    {
        // A voice is not a drum. `meanDecayMs` on a voice is how fast one syllable falls away,
        // and fitting the hold and the release to that shuts the expander between the syllables
        // of a word: the tail of every phrase goes, and a soft word after a loud one starts
        // underneath. So on a voice the decay only ever lengthens them, and the profile's own
        // gentle ratio stands - the clean-up on a microphone somebody is speaking into is a
        // courtesy, and a courtesy does not close at 4:1.
        const bool voice = family == RoleFamily::Speech || family == RoleFamily::LeadVocal
                        || family == RoleFamily::BackingVocal || family == RoleFamily::Choir;
        const float holdMin = voice ? 150.0f : 40.0f;
        const float releaseMin = voice ? 200.0f : 60.0f;
        const float threshold = roundDb (clamp (L.floorDb + (kitNeighbour ? 0.5f : 0.4f) * floorGap, -70.0f, L.hitDb - 12.0f));
        const float range = std::round (clamp (10.0f + 30.0f * bleed + extraRange, 8.0f, t.gateMaxRangeDb + extraRange));
        const float hold = a.meanDecayMs > 0.0f ? std::round (clamp (0.6f * a.meanDecayMs, holdMin, 200.0f)) : std::max (cur.gateHoldMs, holdMin);
        const float release = a.meanDecayMs > 0.0f ? std::round (clamp (0.8f * a.meanDecayMs, releaseMin, 300.0f)) : std::max (cur.gateReleaseMs, releaseMin);
        const bool small = cur.gateEnabled && std::fabs (cur.gateThresholdDb - threshold) < 2.0f && std::fabs (cur.gateRangeDb - range) < 3.0f;
        if (small) return;
        std::string what = (cur.gateEnabled ? "Expander re-fitted: threshold " : "Expander enabled: threshold ") + fmtDb (threshold, 0) + ", " + num ("%.0f dB range", double (range));
        std::string why = "Between " + plural (eventNoun (ctx)) + " the channel sits at " + num ("%.0f dBFS", double (L.floorDb)) + " while " + plural (eventNoun (ctx)) + " reach " + num ("%.0f dBFS", double (L.hitDb))
                        + ": bleed from nearby sources. The range is conservative (expansion, not a hard mute) so soft " + plural (eventNoun (ctx)) + " survive";
        if (kitNeighbour) why += "; the kick and snare are carried by samples now, so what this microphone hears of them is the dirt in a clean kit and it closes further than it otherwise would";
        if (voice) why += ". It holds " + num ("%.0f ms", double (hold)) + " and lets go over " + num ("%.0f ms", double (release))
                        + " so the tail of a phrase is never cut, and it stays at the gentle ratio a voice is given";
        else if (a.meanDecayMs > 0.0f) why += ", and hold/release follow the measured decay (" + num ("%.0f ms", double (a.meanDecayMs)) + ")";
        if (detHpf >= 20.0f) why += "; the detector ignores energy below " + fmtHz (detHpf) + " so low bleed does not open it";
        why += ".";
        d.move (Recommendation::Kind::Gate, TuneSection::Bleed, what, why, bleed > 0.6f ? Confidence::High : Confidence::Medium,
                [=] (ChannelParameters& p)
                {
                    p.gateEnabled = true; p.gateThresholdDb = threshold; p.gateRangeDb = range;
                    p.gateHoldMs = hold; p.gateReleaseMs = release;
                    if (! voice && p.gateRatio < 3.0f) p.gateRatio = 4.0f;
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
    // The delivery number is a gated one - a broadcaster's meter, a platform's meter and the
    // app's own master readout all drop the gaps before they average - so that is what the
    // output is fitted against. Ungated, a mix with pauses in it (a sermon, a quiet song, a
    // band that stops between phrases) reads low and asks to be pushed louder than it is.
    const float measured = a.loudnessGatedLufs > -100.0f ? a.loudnessGatedLufs : a.loudnessLufs;
    if (measured <= -100.0f || a.silencePercent > 60.0f) return;
    if (a.peakDb < t.capturePeakMinDb)
    {
        d.note (Recommendation::Kind::MixGain, TuneSection::Mix, "Loudness waits for a healthy input level",
                "The mix reaches the plugin " + num ("%.0f dB", double (t.capturePeakMinDb - a.peakDb)) + " below the healthy range (see Input). Raise it at the console first, then Re-Tune; "
                "pushing the plugin's gain that far would only make the limiter work hard.", Confidence::High);
        return;
    }

    // The plugin's own gain is on the way; what the capture measured is the input. Predict the output loudness
    // from the input loudness plus the current output trim (the limiter is assumed to only touch peaks).
    const float predicted = measured + cur.outputTrimDb;
    const float delta = t.targetLufs - predicted;
    const float ceiling = t.truePeakCeilingDb;
    const bool ceilingOk = cur.limiterEnabled && std::fabs (cur.limiterCeilingDb - ceiling) < 0.3f;
    if (std::fabs (delta) <= t.loudnessToleranceLu && ceilingOk)
    {
        d.note (Recommendation::Kind::MixGain, TuneSection::Mix, "Loudness on target",
                "The mix measures " + num ("%.1f LUFS", double (predicted)) + " against a target of " + num ("%.0f LUFS", double (t.targetLufs)) + " for this output.", Confidence::High);
        return;
    }
    // The output trim is a digital gain set from a measurement, not a preamp a person turns one step at a
    // time, so the move is bounded by what the limiter can honestly absorb rather than by a human step: a
    // live sum at -22 LUFS asked for a -14 stream is a 15 dB move, and stopping at 12 left every such mix a
    // few LU short with a RE-TUNE that still had something to say.
    const float bounded = clamp (std::round (delta * 2.0f) * 0.5f, -18.0f, 18.0f);
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
