#include "ChainEditor.h"
#include "ChainStrip.h"
#include "Core/DbUtils.h"
#include "DSP/Biquad.h"
#include "DSP/ChannelProcessor.h"
#include "DSP/EqResponse.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kColW      = 300;  // the controls column
    constexpr int kColBandW  = 352;  // ... wider where it holds band cards, so a knob's readout fits
    constexpr int kKnobCellH = 54;
    constexpr int kBandCardH = 80;
    constexpr int kChipH     = 30;
    constexpr int kSendRowH  = 32;
    constexpr int kGraphMinW = 240;
    constexpr float kEqRangeDb = 18.0f;

    enum class Fmt { Db, DbPlain, Hz, Ms, Ratio, Percent, Q, Bipolar };

    juce::String signedNumber (double v, int decimals)
    {
        const juce::String n (std::fabs (v), decimals);
        return (v < 0.0 ? Glyph::minus() : juce::String ("+")) + n;
    }

    juce::String hzText (double v)
    {
        if (v < 20.0) return "off";
        if (v >= 1000.0) return juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String format (Fmt f, double v)
    {
        switch (f)
        {
            case Fmt::Db:      return signedNumber (v, 1) + " dB";
            case Fmt::DbPlain: return juce::String (v, 1) + " dB";
            case Fmt::Hz:      return hzText (v);
            case Fmt::Ms:      return juce::String (v, v < 10.0 ? 2 : 0) + " ms";
            case Fmt::Ratio:   return juce::String (v, 1) + ":1";
            case Fmt::Percent: return juce::String (juce::roundToInt (v * 100.0)) + "%";
            case Fmt::Q:       return "Q " + juce::String (v, 2);
            case Fmt::Bipolar: return signedNumber (v * 100.0, 0);
        }
        return juce::String (v, 1);
    }

    const char* filterTypeName (FilterType t) noexcept
    {
        return kFilterTypeNames[size_t (juce::jlimit (0, int (FilterType::Count) - 1, int (t)))];
    }

    const char* sendName (FxSlot f) noexcept
    {
        switch (f)
        {
            case FxSlot::VocalPlate: return "Plate";
            case FxSlot::VocalDelay: return "Delay";
            case FxSlot::BgvHall:    return "Hall";
            case FxSlot::SnarePlate: return "Snare plate";
            case FxSlot::DrumRoom:   return "Drum room";
            case FxSlot::Count:
            default:                 return "?";
        }
    }

    // Caps used as a section label, not as a word: letterspaced, small, quiet.
    juce::Font capsFont (float px, int weight = 700)
    {
        return Dine::text (px, weight).withExtraKerningFactor (0.09f);
    }

    // A knob's travel: the value at the middle of the sweep, so 120 Hz sits halfway up a
    // 20 Hz - 1 kHz range instead of down in the corner.
    double skewFor (double min, double max, double mid) noexcept
    {
        if (! (mid > min && mid < max)) return 1.0;
        return std::log (0.5) / std::log ((mid - min) / (max - min));
    }

    double toProportion (double v, double min, double max, double mid) noexcept
    {
        const double t = juce::jlimit (0.0, 1.0, (v - min) / juce::jmax (1.0e-9, max - min));
        return std::pow (t, skewFor (min, max, mid));
    }

    double fromProportion (double t, double min, double max, double mid) noexcept
    {
        return min + (max - min) * std::pow (juce::jlimit (0.0, 1.0, t), 1.0 / skewFor (min, max, mid));
    }

    float logX (float hz, float x, float w) noexcept
    {
        return x + w * std::log10 (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / 3.0f;
    }

    float freqAtX (float px, float x, float w) noexcept
    {
        return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, (px - x) / juce::jmax (1.0f, w)));
    }
}

// ------------------------------------------------------------------ specs
struct ChainEditor::Field
{
    enum class Kind { Slider, Toggle, Choice };

    Kind kind = Kind::Slider;
    juce::String label;
    double min = 0.0, max = 1.0, step = 0.01, mid = 0.0;   // mid inside the range: the sweep is skewed to it
    Fmt fmt = Fmt::Db;
    juce::StringArray choices;                              // Choice: the items; Toggle: { off, on }
    std::function<double (const ChannelParameters&)> get;
    std::function<void (ChannelParameters&, double)> set;
};

// Which stage this is. The panel draws a stage by what it does, so it has to know one
// from another; the order is the order the audio meets them.
enum class StageId
{
    Input, Filters, Gate, CorrectiveEq, DeEss, Comp, Transient, ToneEq, Sat, Width, Limiter, Output, Sends
};

struct ChainEditor::StageSpec
{
    StageId id = StageId::Input;
    juce::String name;                                       // "Corrective EQ"
    juce::String plain;                                      // the sentence under the name
    Dine::Icon icon = Dine::Icon::Sliders;
    std::function<bool (const ChannelParameters&)> isOn;      // null: the stage is always in the chain
    std::function<void (ChannelParameters&, bool)> setOn;
    std::function<juce::String (const ChannelParameters&)> summary;
    std::vector<Field> fields;
    int bands = 0;                                           // EQ bands, on cards of their own
    bool corrective = false;                                 // which band array they edit
    juce::StringArray ids;                                   // parameter-id prefixes: which TUNE MIX items are this stage's
};

namespace
{
    using Field = ChainEditor::Field;
    using StageSpec = ChainEditor::StageSpec;

    // What a stage draws in its graph.
    enum class GraphKind { Eq, Transfer, Bars, Sends };

    GraphKind graphFor (StageId id) noexcept
    {
        switch (id)
        {
            case StageId::Filters:
            case StageId::CorrectiveEq:
            case StageId::DeEss:
            case StageId::ToneEq:     return GraphKind::Eq;
            case StageId::Gate:
            case StageId::Comp:
            case StageId::Sat:
            case StageId::Limiter:    return GraphKind::Transfer;
            case StageId::Sends:      return GraphKind::Sends;
            default:                  return GraphKind::Bars;
        }
    }

    template <typename T>
    Field number (const char* label, T ChannelParameters::* member, double min, double max,
                  double step, double mid, Fmt fmt)
    {
        Field f;
        f.kind = Field::Kind::Slider;
        f.label = label;
        f.min = min; f.max = max; f.step = step; f.mid = mid; f.fmt = fmt;
        f.get = [member] (const ChannelParameters& p) { return double (p.*member); };
        f.set = [member] (ChannelParameters& p, double v) { p.*member = T (v); };
        return f;
    }

    Field toggle (const char* label, bool ChannelParameters::* member, const char* off, const char* on)
    {
        Field f;
        f.kind = Field::Kind::Toggle;
        f.label = label;
        f.choices = { off, on };
        f.get = [member] (const ChannelParameters& p) { return p.*member ? 1.0 : 0.0; };
        f.set = [member] (ChannelParameters& p, double v) { p.*member = v > 0.5; };
        return f;
    }

    Field choice (const char* label, int ChannelParameters::* member, juce::StringArray items)
    {
        Field f;
        f.kind = Field::Kind::Choice;
        f.label = label;
        f.choices = std::move (items);
        f.get = [member] (const ChannelParameters& p) { return double (p.*member); };
        f.set = [member] (ChannelParameters& p, double v) { p.*member = juce::roundToInt (v); };
        return f;
    }

    // The chain, in the order the audio meets it - the same list, and the same words, as
    // the strip along the foot of TRACKS and MIXER. A mono input has nothing for the width
    // stage to do and only the master owns a limiter, so those two are asked for.
    std::vector<StageSpec> chainSpecs (bool stereo, bool hasLimiter)
    {
        std::vector<StageSpec> v;

        {
            StageSpec s;
            s.id = StageId::Input;
            s.name = "Input";
            s.plain = "The trim and the polarity: what the rest of the chain receives.";
            s.icon = Dine::Icon::Sliders;
            s.ids = { "inputTrim", "polarity" };
            s.summary = [] (const ChannelParameters& p)
            {
                return signedNumber (p.inputTrimDb, 1) + " dB" + (p.polarityInvert ? "  " + Glyph::dot() + "  flipped" : juce::String());
            };
            s.fields = { number ("Trim", &ChannelParameters::inputTrimDb, -24.0, 24.0, 0.1, 0.0, Fmt::Db),
                         toggle ("Polarity", &ChannelParameters::polarityInvert, "Normal", "Flipped") };
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::Filters;
            s.name = "Filters";
            s.plain = "Clears the rumble under the sound, and the hiss above it.";
            s.icon = Dine::Icon::Waveform;
            s.ids = { "hpf", "lpf" };
            s.isOn = [] (const ChannelParameters& p) { return p.hpfEnabled || p.lpfEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.hpfEnabled = on; if (! on) p.lpfEnabled = false; };
            s.summary = [] (const ChannelParameters& p)
            {
                juce::StringArray parts;
                if (p.hpfEnabled) parts.add ("high-pass " + hzText (p.hpfHz));
                if (p.lpfEnabled) parts.add ("low-pass " + hzText (p.lpfHz));
                return parts.isEmpty() ? juce::String (Glyph::dash()) : parts.joinIntoString ("  " + Glyph::dot() + "  ");
            };
            s.fields = { number ("High-pass", &ChannelParameters::hpfHz, 20.0, 1000.0, 1.0, 120.0, Fmt::Hz),
                         number ("Low-pass", &ChannelParameters::lpfHz, 1000.0, 20000.0, 10.0, 6000.0, Fmt::Hz),
                         toggle ("High-pass", &ChannelParameters::hpfEnabled, "Off", "On"),
                         choice ("Slope", &ChannelParameters::hpfSlope, { "12 dB/oct", "24 dB/oct" }),
                         toggle ("Low-pass", &ChannelParameters::lpfEnabled, "Off", "On"),
                         choice ("Slope ", &ChannelParameters::lpfSlope, { "12 dB/oct", "24 dB/oct" }) };
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::Gate;
            s.name = "Gate";
            s.plain = "CLEAN-UP: quiet between the hits, so the room and the bleed stay out.";
            s.icon = Dine::Icon::Dash;
            s.ids = { "gate" };
            s.isOn = [] (const ChannelParameters& p) { return p.gateEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.gateEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                return signedNumber (p.gateThresholdDb, 0) + " dB  " + Glyph::dot() + "  "
                       + juce::String (p.gateRangeDb, 0) + " dB down  " + Glyph::dot() + "  "
                       + juce::String (p.gateRatio, 1) + ":1";
            };
            s.fields = { number ("Threshold", &ChannelParameters::gateThresholdDb, -80.0, 0.0, 0.1, 0.0, Fmt::Db),
                         number ("Range", &ChannelParameters::gateRangeDb, 0.0, 80.0, 0.1, 0.0, Fmt::DbPlain),
                         number ("Attack", &ChannelParameters::gateAttackMs, 0.01, 50.0, 0.01, 2.0, Fmt::Ms),
                         number ("Hold", &ChannelParameters::gateHoldMs, 0.0, 500.0, 1.0, 80.0, Fmt::Ms),
                         number ("Release", &ChannelParameters::gateReleaseMs, 5.0, 1000.0, 1.0, 120.0, Fmt::Ms),
                         number ("Hysteresis", &ChannelParameters::gateHysteresisDb, 0.0, 12.0, 0.1, 0.0, Fmt::DbPlain),
                         number ("Ratio", &ChannelParameters::gateRatio, 1.0, 20.0, 0.1, 4.0, Fmt::Ratio),
                         number ("Detector HP", &ChannelParameters::gateScHpfHz, 0.0, 500.0, 1.0, 120.0, Fmt::Hz) };
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::CorrectiveEq;
            s.name = "Corrective EQ";
            s.plain = "The problem notches: a ring or a boxy build-up, taken out narrowly.";
            s.icon = Dine::Icon::Target;
            s.ids = { "corrEq" };
            s.isOn = [] (const ChannelParameters& p) { return p.correctiveEqEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.correctiveEqEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                juce::StringArray parts;
                for (int i = 0; i < ParamID::kCorrectiveBands; ++i)
                {
                    const auto& b = p.correctiveBands[size_t (i)];
                    if (b.enabled) parts.add (hzText (b.freqHz) + " " + signedNumber (b.gainDb, 1));
                }
                return parts.isEmpty() ? juce::String (Glyph::dash()) : parts.joinIntoString ("  " + Glyph::dot() + "  ");
            };
            s.bands = ParamID::kCorrectiveBands;
            s.corrective = true;
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::DeEss;
            s.name = "De-esser";
            s.plain = "SMOOTH: takes the sting off the S and T sounds.";
            s.icon = Dine::Icon::Speech;
            s.ids = { "deEss" };
            s.isOn = [] (const ChannelParameters& p) { return p.deEssEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.deEssEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                return hzText (p.deEssHz) + "  " + Glyph::dot() + "  " + signedNumber (p.deEssThresholdDb, 0)
                       + " dB  " + Glyph::dot() + "  up to " + juce::String (p.deEssRangeDb, 0) + " dB down";
            };
            s.fields = { number ("Frequency", &ChannelParameters::deEssHz, 2000.0, 12000.0, 10.0, 6000.0, Fmt::Hz),
                         number ("Threshold", &ChannelParameters::deEssThresholdDb, -60.0, 0.0, 0.1, 0.0, Fmt::Db),
                         number ("Range", &ChannelParameters::deEssRangeDb, 0.0, 24.0, 0.1, 0.0, Fmt::DbPlain) };
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::Comp;
            s.name = "Compressor";
            s.plain = "STEADY: evens out the loud and the quiet so the level holds.";
            s.icon = Dine::Icon::Sliders;
            s.ids = { "comp" };
            s.isOn = [] (const ChannelParameters& p) { return p.compEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.compEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                return signedNumber (p.compThresholdDb, 0) + " dB  " + Glyph::dot() + "  "
                       + juce::String (p.compRatio, 1) + ":1  " + Glyph::dot() + "  makeup "
                       + signedNumber (p.compMakeupDb, 1) + " dB";
            };
            s.fields = { number ("Threshold", &ChannelParameters::compThresholdDb, -60.0, 0.0, 0.1, 0.0, Fmt::Db),
                         number ("Ratio", &ChannelParameters::compRatio, 1.0, 20.0, 0.1, 4.0, Fmt::Ratio),
                         number ("Attack", &ChannelParameters::compAttackMs, 0.1, 200.0, 0.1, 10.0, Fmt::Ms),
                         number ("Release", &ChannelParameters::compReleaseMs, 5.0, 2000.0, 1.0, 150.0, Fmt::Ms),
                         number ("Knee", &ChannelParameters::compKneeDb, 0.0, 24.0, 0.1, 0.0, Fmt::DbPlain),
                         number ("Makeup", &ChannelParameters::compMakeupDb, -12.0, 24.0, 0.1, 0.0, Fmt::Db),
                         number ("Blend", &ChannelParameters::compMix, 0.0, 1.0, 0.01, 0.0, Fmt::Percent),
                         number ("Detector HP", &ChannelParameters::compScHpfHz, 0.0, 500.0, 1.0, 120.0, Fmt::Hz) };
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::Transient;
            s.name = "Transient";
            s.plain = "More stick, or more room: the front of the sound against its tail.";
            s.icon = Dine::Icon::Drum;
            s.ids = { "trans" };
            s.isOn = [] (const ChannelParameters& p) { return p.transientEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.transientEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                return "attack " + signedNumber (p.transientAttack * 100.0f, 0) + "  " + Glyph::dot()
                       + "  sustain " + signedNumber (p.transientSustain * 100.0f, 0);
            };
            s.fields = { number ("Attack", &ChannelParameters::transientAttack, -1.0, 1.0, 0.01, 0.0, Fmt::Bipolar),
                         number ("Sustain", &ChannelParameters::transientSustain, -1.0, 1.0, 0.01, 0.0, Fmt::Bipolar) };
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::ToneEq;
            s.name = "Tone EQ";
            s.plain = "WARMTH and CLARITY: the broad shape of the sound.";
            s.icon = Dine::Icon::Waveform;
            s.ids = { "toneEq" };
            s.isOn = [] (const ChannelParameters& p) { return p.toneEqEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.toneEqEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                juce::StringArray parts;
                for (int i = 0; i < ParamID::kToneBands; ++i)
                {
                    const auto& b = p.toneBands[size_t (i)];
                    if (b.enabled) parts.add (hzText (b.freqHz) + " " + signedNumber (b.gainDb, 1));
                }
                return parts.isEmpty() ? juce::String (Glyph::dash()) : parts.joinIntoString ("  " + Glyph::dot() + "  ");
            };
            s.bands = ParamID::kToneBands;
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::Sat;
            s.name = "Saturation";
            s.plain = "WARMTH: a little valve colour, and the glue that comes with it.";
            s.icon = Dine::Icon::Fx;
            s.ids = { "sat" };
            s.isOn = [] (const ChannelParameters& p) { return p.satEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.satEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                return "drive " + juce::String (juce::roundToInt (p.satDrive * 100.0f)) + "%  " + Glyph::dot()
                       + "  blend " + juce::String (juce::roundToInt (p.satMix * 100.0f)) + "%";
            };
            s.fields = { number ("Drive", &ChannelParameters::satDrive, 0.0, 1.0, 0.01, 0.0, Fmt::Percent),
                         number ("Blend", &ChannelParameters::satMix, 0.0, 1.0, 0.01, 0.0, Fmt::Percent) };
            v.push_back (std::move (s));
        }
        if (stereo)
        {
            StageSpec s;
            s.id = StageId::Width;
            s.name = "Width";
            s.plain = "Wider or narrower, and how much of the low end stays in the middle.";
            s.icon = Dine::Icon::Room;
            s.ids = { "width" };
            s.isOn = [] (const ChannelParameters& p) { return p.widthEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.widthEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                juce::String t = juce::String (juce::roundToInt (p.widthAmount * 100.0f)) + "%";
                if (p.widthMonoBelowHz >= 20.0f) t += "  " + Glyph::dot() + "  mono below " + hzText (p.widthMonoBelowHz);
                return t;
            };
            s.fields = { number ("Width", &ChannelParameters::widthAmount, 0.0, 2.0, 0.01, 1.0, Fmt::Percent),
                         number ("Mono below", &ChannelParameters::widthMonoBelowHz, 0.0, 500.0, 1.0, 120.0, Fmt::Hz) };
            v.push_back (std::move (s));
        }
        if (hasLimiter)
        {
            StageSpec s;
            s.id = StageId::Limiter;
            s.name = "Limiter";
            s.plain = "LOUD: the ceiling the broadcast never goes above.";
            s.icon = Dine::Icon::Target;
            s.ids = { "limiter" };
            s.isOn = [] (const ChannelParameters& p) { return p.limiterEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.limiterEnabled = on; };
            s.summary = [] (const ChannelParameters& p)
            {
                return "ceiling " + signedNumber (p.limiterCeilingDb, 1) + " dB  " + Glyph::dot() + "  release "
                       + juce::String (p.limiterReleaseMs, 0) + " ms";
            };
            s.fields = { number ("Ceiling", &ChannelParameters::limiterCeilingDb, -12.0, 0.0, 0.1, 0.0, Fmt::Db),
                         number ("Release", &ChannelParameters::limiterReleaseMs, 10.0, 1000.0, 1.0, 120.0, Fmt::Ms) };
            v.push_back (std::move (s));
        }
        {
            StageSpec s;
            s.id = StageId::Output;
            s.name = "Output";
            s.plain = "The last trim on the way out of the chain, before the fader.";
            s.icon = Dine::Icon::Sliders;
            s.ids = { "outputTrim" };
            s.summary = [] (const ChannelParameters& p) { return signedNumber (p.outputTrimDb, 1) + " dB"; };
            s.fields = { number ("Trim", &ChannelParameters::outputTrimDb, -24.0, 24.0, 0.1, 0.0, Fmt::Db) };
            v.push_back (std::move (s));
        }
        return v;
    }

    // The stage's own contribution to the frequency response, with the rest of the chain
    // taken out - the curve you are drawing is the one you are working on.
    ChannelParameters isolate (const ChannelParameters& p, StageId id)
    {
        ChannelParameters q = p;
        q.hpfEnabled = q.lpfEnabled = false;
        q.correctiveEqEnabled = q.toneEqEnabled = false;
        switch (id)
        {
            case StageId::Filters:
                q.hpfEnabled = p.hpfEnabled;
                q.lpfEnabled = p.lpfEnabled;
                break;
            case StageId::CorrectiveEq:
                q.correctiveEqEnabled = p.correctiveEqEnabled;
                break;
            case StageId::ToneEq:
                q.toneEqEnabled = p.toneEqEnabled;
                break;
            case StageId::DeEss:
                // The de-esser is a dynamic dip: draw it as the notch it takes out when it works.
                q.correctiveEqEnabled = p.deEssEnabled;
                for (auto& b : q.correctiveBands) b.enabled = false;
                q.correctiveBands[0] = { true, FilterType::Peak, p.deEssHz, -p.deEssRangeDb, 2.4f };
                break;
            default:
                break;
        }
        return q;
    }
}

// ------------------------------------------------------------------ Knob
// One number: a 270-degree sweep, its name and what it reads. Drag up and down; a
// double-click puts it back where a fresh channel starts.
class ChainEditor::Knob : public juce::Component
{
public:
    Knob (Field f, std::function<void (double)> apply, juce::Colour tint, bool compact = false)
        : field (std::move (f)), commit (std::move (apply)), colour (tint), small (compact)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    int dial() const noexcept { return small ? 34 : kDial; }

    void setValue (double v)
    {
        if (std::fabs (v - value) < 1.0e-6) return;
        value = v;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool live = isEnabled();
        auto r = getLocalBounds();
        const int d = dial();
        auto face = r.removeFromLeft (d).toFloat().withSizeKeepingCentre (float (d), float (d));
        const float cx = face.getCentreX(), cy = face.getCentreY(), rad = d * 0.5f - 3.0f;
        const float a0 = juce::degreesToRadians (-135.0f), sweep = juce::degreesToRadians (270.0f);
        const float t = float (toProportion (value, field.min, field.max, field.mid));

        juce::Path track, arc;
        track.addCentredArc (cx, cy, rad, rad, 0.0f, a0, a0 + sweep, true);
        const float thickness = small ? 3.0f : 3.5f;
        g.setColour (juce::Colours::white.withAlpha (live ? 0.10f : 0.05f));
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (t > 0.004f)
        {
            arc.addCentredArc (cx, cy, rad, rad, 0.0f, a0, a0 + sweep * t, true);
            g.setColour (live ? colour : Dine::ink4);
            g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const float a = a0 + sweep * t;
        const float sx = std::sin (a), sy = -std::cos (a);
        g.setColour (live ? Dine::ink : Dine::ink4);
        g.drawLine (cx + sx * rad * 0.42f, cy + sy * rad * 0.42f, cx + sx * (rad - 1.0f), cy + sy * (rad - 1.0f), 2.0f);

        r.removeFromLeft (small ? 6 : 8);
        g.setColour (live ? Dine::ink3 : Dine::ink4);
        g.setFont (capsFont (9.0f, 600));
        g.drawText (field.label.trim().toUpperCase(), r.removeFromTop (r.getHeight() / 2).withTrimmedTop (small ? 3 : 6),
                    juce::Justification::bottomLeft, true);
        g.setColour (live ? Dine::ink : Dine::ink4);
        g.setFont (Dine::mono (small ? 11.0f : 12.0f, 500));
        g.drawText (format (field.fmt, value), r.withTrimmedBottom (small ? 3 : 6), juce::Justification::topLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = toProportion (value, field.min, field.max, field.mid);
        anchor = e.position.y;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        const double t = juce::jlimit (0.0, 1.0, dragFrom + double (anchor - e.position.y) / (e.mods.isShiftDown() ? 700.0 : 170.0));
        apply (fromProportion (t, field.min, field.max, field.mid));
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (isEnabled()) apply (field.get (ChannelParameters {}));
    }

    void enablementChanged() override { repaint(); }

    static constexpr int kDial = 42;

private:
    void apply (double v)
    {
        const double q = field.step > 0.0 ? field.min + std::round ((v - field.min) / field.step) * field.step : v;
        const double clamped = juce::jlimit (field.min, field.max, q);
        if (std::fabs (clamped - value) < 1.0e-9) return;
        value = clamped;
        commit (clamped);
        repaint();
    }

    Field field;
    std::function<void (double)> commit;
    juce::Colour colour;
    bool small = false;
    double value = 0.0, dragFrom = 0.0;
    float anchor = 0.0f;
};

// ------------------------------------------------------------------ SwitchChip
// A switch or a choice as one flat chip: its name in small caps, then what it is set to.
class ChainEditor::SwitchChip : public juce::Button
{
public:
    SwitchChip (Field f, std::function<void (double)> apply)
        : juce::Button (f.label), field (std::move (f)), commit (std::move (apply))
    {
        onClick = [this]
        {
            if (field.choices.size() <= 2)
            {
                commit (index > 0 ? 0.0 : 1.0);
                return;
            }
            juce::PopupMenu m;
            for (int i = 0; i < field.choices.size(); ++i) m.addItem (i + 1, field.choices[i], true, i == index);
            m.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (this),
                             [this] (int r) { if (r > 0) commit (double (r - 1)); });
        };
    }

    void setIndex (int i)
    {
        i = juce::jlimit (0, juce::jmax (0, field.choices.size() - 1), i);
        if (i == index) return;
        index = i;
        repaint();
    }

    int idealWidth() const
    {
        int widest = 0;
        for (const auto& c : field.choices) widest = juce::jmax (widest, Dine::textWidth (Dine::text (11.5f), c));
        return 11 + Dine::textWidth (capsFont (9.5f, 600), field.label.trim().toUpperCase()) + 9 + widest + 11;
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = index > 0, live = isEnabled();
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, on && live ? Dine::accent.withAlpha (0.16f)
                                            : juce::Colours::white.withAlpha (over && live ? 0.09f : 0.05f), Dine::Radius::chip);
        Dine::hairlineRounded (g, r, Dine::hair, Dine::Radius::chip);
        auto inner = getLocalBounds().reduced (11, 0);
        const auto label = field.label.trim().toUpperCase();
        g.setColour (live ? Dine::ink3 : Dine::ink4);
        g.setFont (capsFont (9.5f, 600));
        g.drawText (label, inner.removeFromLeft (Dine::textWidth (capsFont (9.5f, 600), label)), juce::Justification::centredLeft);
        inner.removeFromLeft (9);
        g.setColour (! live ? Dine::ink4 : on ? Dine::accent : Dine::ink);
        g.setFont (Dine::text (11.5f));
        g.drawText (index < field.choices.size() ? field.choices[index] : juce::String(), inner, juce::Justification::centredLeft);
    }

private:
    Field field;
    std::function<void (double)> commit;
    int index = 0;
};

// ------------------------------------------------------------------ BandCard
// One EQ band: its lamp, what shape it is, where it sits, and the three knobs that move
// it. Picking a card picks the node on the curve, and the other way round.
class ChainEditor::BandCard : public juce::Component
{
public:
    using Commit = std::function<void (const std::function<void (ChannelParameters&)>&)>;

    BandCard (int bandIndex, bool corrective, Commit c, std::function<void (int)> pick)
        : index (bandIndex), corr (corrective), commit (std::move (c)), select (std::move (pick))
    {
        auto edit = [this] (int which, double v)
        {
            commit ([this, which, v] (ChannelParameters& p)
            {
                auto& b = band (p);
                if (which == 0) b.freqHz = float (v);
                else if (which == 1) b.gainDb = float (v);
                else b.q = float (v);
            });
        };

        // A double-click puts a knob back where a fresh band starts, not to zero.
        Field f;
        f.label = "Freq"; f.min = 20.0; f.max = 20000.0; f.step = 1.0; f.mid = 630.0; f.fmt = Fmt::Hz;
        f.get = [] (const ChannelParameters&) { return 630.0; };
        knobs[0] = std::make_unique<Knob> (f, [edit] (double v) { edit (0, v); }, Dine::accent, true);
        f.label = "Gain"; f.min = -18.0; f.max = 18.0; f.step = 0.1; f.mid = 0.0; f.fmt = Fmt::Db;
        f.get = [] (const ChannelParameters&) { return 0.0; };
        knobs[1] = std::make_unique<Knob> (f, [edit] (double v) { edit (1, v); }, Dine::accent, true);
        f.label = "Q"; f.min = 0.1; f.max = 10.0; f.step = 0.01; f.mid = 1.0; f.fmt = Fmt::Q;
        f.get = [] (const ChannelParameters&) { return 1.0; };
        knobs[2] = std::make_unique<Knob> (f, [edit] (double v) { edit (2, v); }, Dine::accent, true);
        for (auto& k : knobs) addAndMakeVisible (*k);

        type.setValue (filterTypeName (shape));
        type.onClick = [this]
        {
            juce::PopupMenu m;
            for (int i = 0; i < int (FilterType::Count); ++i)
                m.addItem (i + 1, kFilterTypeNames[size_t (i)], true, i == int (shape));
            m.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (&type), [this] (int r)
            {
                if (r <= 0) return;
                const auto t = FilterType (r - 1);
                commit ([this, t] (ChannelParameters& p) { band (p).type = t; });
            });
        };
        addAndMakeVisible (type);
    }

    void pull (const ChannelParameters& p, bool live, bool selected)
    {
        const auto& b = band (p);
        knobs[0]->setValue (b.freqHz);
        knobs[1]->setValue (b.gainDb);
        knobs[2]->setValue (b.q);
        for (auto& k : knobs) k->setEnabled (live && b.enabled);
        type.setEnabled (live && b.enabled);
        if (shape != b.type) { shape = b.type; type.setValue (filterTypeName (shape)); }
        if (on != b.enabled || sel != selected || summary != summaryFor (b))
        {
            on = b.enabled;
            sel = selected;
            summary = summaryFor (b);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, sel ? juce::Colours::white.withAlpha (0.07f) : juce::Colours::white.withAlpha (0.03f), Dine::Radius::card);
        Dine::hairlineRounded (g, r, sel ? Dine::accent.withAlpha (0.6f) : Dine::hairSoft, Dine::Radius::card);

        auto head = getLocalBounds().reduced (11, 0).withHeight (kHeadH);
        auto lamp = head.removeFromLeft (10).withSizeKeepingCentre (9, 9).toFloat();
        g.setColour (on ? Dine::accent : juce::Colours::white.withAlpha (0.14f));
        g.fillEllipse (lamp);
        head.removeFromLeft (8);
        const auto nameFont = capsFont (10.5f, 700);
        const juce::String name = "BAND " + juce::String (index + 1);
        g.setColour (on ? Dine::ink : Dine::ink3);
        g.setFont (nameFont);
        g.drawText (name, head.removeFromLeft (Dine::textWidth (nameFont, name)), juce::Justification::centredLeft);
        g.setColour (on ? Dine::ink3 : Dine::ink4);
        g.setFont (Dine::mono (10.5f));
        g.drawText (summary, head.withTrimmedLeft (8 + kTypeW), juce::Justification::centredRight, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (11, 0);
        auto head = r.removeFromTop (kHeadH);
        head.removeFromLeft (10 + 8 + Dine::textWidth (capsFont (10.5f, 700), "BAND 8") + 8);
        type.setBounds (head.removeFromLeft (kTypeW).withSizeKeepingCentre (kTypeW, Dine::Metric::control));
        r.removeFromTop (2);
        const int cell = juce::jmax (70, (r.getWidth() - 12) / 3);
        for (auto& k : knobs)
        {
            k->setBounds (r.removeFromLeft (cell).withHeight (k->dial()).withY (r.getY() + 3));
            r.removeFromLeft (6);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < 22 && e.y < kHeadH)
        {
            commit ([this] (ChannelParameters& p) { auto& b = band (p); b.enabled = ! b.enabled; });
            return;
        }
        if (select) select (index);
    }

    static constexpr int kHeadH = 24, kTypeW = 96;

private:
    juce::String summaryFor (const EQBandParams& b) const
    {
        return hzText (b.freqHz) + "   " + signedNumber (b.gainDb, 1) + " dB";
    }

    EQBandParams& band (ChannelParameters& p) const
    {
        return corr ? p.correctiveBands[size_t (index)] : p.toneBands[size_t (index)];
    }

    const EQBandParams& band (const ChannelParameters& p) const
    {
        return corr ? p.correctiveBands[size_t (index)] : p.toneBands[size_t (index)];
    }

    int index = 0;
    bool corr = false, on = false, sel = false;
    Commit commit;
    std::function<void (int)> select;
    std::array<std::unique_ptr<Knob>, 3> knobs;
    DinePopup type;
    FilterType shape = FilterType::Peak;
    juce::String summary;
};

// ------------------------------------------------------------------ SendRow
// One FX send: how much of this channel is going to that return.
class ChainEditor::SendRow : public juce::Component
{
public:
    SendRow (FxSlot f, std::function<void (float)> apply) : slot (f), commit (std::move (apply))
    {
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        // The bottom stop is the off detent: the mix stores kSilenceDb, not -60.
        level.setRange (kOffDb, 6.0, 0.5);
        level.setDoubleClickReturnValue (true, kOffDb);
        level.onValueChange = [this]
        {
            if (updating) return;
            const double v = level.getValue();
            off = v <= kOffDb + 0.01;
            commit (off ? kSilenceDb : float (v));
            repaint();
        };
        addAndMakeVisible (level);
    }

    void pull (float db, bool live)
    {
        updating = true;
        // Off is decided from the mix value (kSilenceDb), not from the slider stop: clamping
        // silence up to kOffDb would otherwise make "off" look the same as a -60 dB send.
        off = db <= kSilenceDb + 0.01f;
        level.setValue (off ? kOffDb : juce::jlimit (kOffDb, 6.0, double (db)), juce::dontSendNotification);
        updating = false;
        level.setEnabled (live);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (isEnabled() ? Dine::ink2 : Dine::ink4);
        g.setFont (Dine::text (12.0f));
        g.drawText (sendName (slot), r.removeFromLeft (kLabelW), juce::Justification::centredLeft, true);
        g.setColour (off ? Dine::ink4 : Dine::ink);
        g.setFont (Dine::mono (11.5f, 500));
        g.drawText (off ? juce::String ("off") : signedNumber (level.getValue(), 1) + " dB",
                    r.removeFromRight (kValueW), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (kLabelW);
        r.removeFromRight (kValueW + 8);
        level.setBounds (r.withSizeKeepingCentre (r.getWidth(), 20));
    }

    void enablementChanged() override { level.setEnabled (isEnabled()); repaint(); }

    static constexpr int kLabelW = 92, kValueW = 62;
    static constexpr double kOffDb = -60.0;   // slider detent; the mix stores kSilenceDb when off

    FxSlot slot;

private:
    std::function<void (float)> commit;
    juce::Slider level;
    bool updating = false;
    bool off = true;
};

// ------------------------------------------------------------------ Graph
// What the stage is doing, drawn: an EQ curve with a node you can drag, a compressor's
// in-out line with the live gain reduction beside it, or the bars of a trim.
class ChainEditor::Graph : public juce::Component
{
public:
    using Commit = std::function<void (const std::function<void (ChannelParameters&)>&)>;

    Graph (Commit c, std::function<void (int)> pick) : commit (std::move (c)), select (std::move (pick)) {}

    void setStage (const StageSpec* s)
    {
        spec = s;
        history.assign (kHistory, 0.0f);
        repaint();
    }

    void update (const ChannelParameters& p, double sr, float grDb, float inDb, float outDb,
                 float levelNorm, int selectedBand, bool live, std::vector<std::pair<FxSlot, float>> sendLevels)
    {
        params = p;
        sampleRate = sr > 0.0 ? sr : 48000.0;
        gr = grDb;
        inputDb = inDb;
        outputDb = outDb;
        level = levelNorm;
        band = selectedBand;
        running = live;
        sends = std::move (sendLevels);
        if (spec != nullptr && graphFor (spec->id) == GraphKind::Transfer)
        {
            history.erase (history.begin());
            history.push_back (gr);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        Dine::drawWell (g, area, Dine::Radius::card);
        if (spec == nullptr) return;

        auto r = getLocalBounds().reduced (12);
        switch (graphFor (spec->id))
        {
            case GraphKind::Eq:       paintEq (g, r); break;
            case GraphKind::Transfer: paintTransfer (g, r); break;
            case GraphKind::Sends:    paintSends (g, r); break;
            case GraphKind::Bars:
            default:                  paintBars (g, r); break;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = -1;
        if (spec == nullptr || ! isEnabled() || graphFor (spec->id) != GraphKind::Eq) return;
        const auto ns = nodes();
        float best = 400.0f;
        for (size_t i = 0; i < ns.size(); ++i)
        {
            const float d = e.position.getDistanceSquaredFrom (ns[i].pos);
            if (d < best) { best = d; dragging = int (i); }
        }
        if (dragging >= 0 && best < 22.0f * 22.0f)
        {
            if (spec->bands > 0 && select) select (ns[size_t (dragging)].band);
            moveNode (ns[size_t (dragging)], e.position);
        }
        else dragging = -1;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        const auto ns = nodes();
        if (dragging < int (ns.size())) moveNode (ns[size_t (dragging)], e.position);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

private:
    struct Node { juce::Point<float> pos; float hz = 0.0f, gainDb = 0.0f; int band = -1; juce::String label; bool vertical = true; bool on = true; };

    juce::Rectangle<float> plot() const { return getLocalBounds().reduced (12).toFloat().withTrimmedBottom (14).withTrimmedLeft (22); }

    float yForDb (float db) const
    {
        auto p = plot();
        return p.getCentreY() - juce::jlimit (-1.0f, 1.0f, db / kEqRangeDb) * p.getHeight() * 0.5f;
    }

    float dbForY (float y) const
    {
        auto p = plot();
        return juce::jlimit (-kEqRangeDb, kEqRangeDb, (p.getCentreY() - y) / (p.getHeight() * 0.5f) * kEqRangeDb);
    }

    std::vector<Node> nodes() const
    {
        std::vector<Node> out;
        if (spec == nullptr) return out;
        auto p = plot();
        auto add = [&] (float hz, float db, int bandIndex, const juce::String& label, bool vertical, bool on)
        {
            out.push_back ({ { logX (hz, p.getX(), p.getWidth()), yForDb (db) }, hz, db, bandIndex, label, vertical, on });
        };
        if (spec->bands > 0)
        {
            for (int i = 0; i < spec->bands; ++i)
            {
                const auto& b = spec->corrective ? params.correctiveBands[size_t (i)] : params.toneBands[size_t (i)];
                add (b.freqHz, b.gainDb, i, juce::String (i + 1), true, b.enabled);
            }
        }
        else if (spec->id == StageId::Filters)
        {
            if (params.hpfEnabled) add (params.hpfHz, 0.0f, -1, "H", false, true);
            if (params.lpfEnabled) add (params.lpfHz, 0.0f, -1, "L", false, true);
        }
        else if (spec->id == StageId::DeEss)
        {
            add (params.deEssHz, -params.deEssRangeDb, -1, "S", true, params.deEssEnabled);
        }
        return out;
    }

    void moveNode (const Node& n, juce::Point<float> to)
    {
        auto p = plot();
        const float hz = freqAtX (to.x, p.getX(), p.getWidth());
        const float db = dbForY (to.y);
        const int index = n.band;
        const bool corr = spec->corrective;
        const StageId id = spec->id;
        const juce::String which = n.label;
        commit ([=] (ChannelParameters& q)
        {
            if (index >= 0)
            {
                auto& b = corr ? q.correctiveBands[size_t (index)] : q.toneBands[size_t (index)];
                b.freqHz = juce::jlimit (20.0f, 20000.0f, hz);
                b.gainDb = juce::jlimit (-18.0f, 18.0f, db);
            }
            else if (id == StageId::Filters)
            {
                if (which == "H") q.hpfHz = juce::jlimit (20.0f, 1000.0f, hz);
                else              q.lpfHz = juce::jlimit (1000.0f, 20000.0f, hz);
            }
            else if (id == StageId::DeEss)
            {
                q.deEssHz = juce::jlimit (2000.0f, 12000.0f, hz);
                q.deEssRangeDb = juce::jlimit (0.0f, 24.0f, -db);
            }
        });
    }

    void paintEq (juce::Graphics& g, juce::Rectangle<int>)
    {
        auto p = plot();
        const auto gridFont = Dine::mono (9.5f);

        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = logX (hz, p.getX(), p.getWidth());
            g.setColour (Dine::hairSoft);
            g.fillRect (x, p.getY(), 0.5f, p.getHeight());
            g.setColour (Dine::ink4);
            g.setFont (gridFont);
            g.drawText (hz >= 1000.0f ? juce::String (int (hz / 1000.0f)) + "k" : juce::String (int (hz)),
                        juce::Rectangle<float> (x - 18.0f, p.getBottom() + 1.0f, 36.0f, 12.0f), juce::Justification::centred);
        }
        for (float db : { 9.0f, 0.0f, -9.0f })
        {
            const float y = yForDb (db);
            g.setColour (db == 0.0f ? Dine::hair : Dine::hairSoft);
            g.fillRect (p.getX(), y, p.getWidth(), 0.5f);
            g.setColour (Dine::ink4);
            g.setFont (gridFont);
            g.drawText (db > 0.0f ? "+" + juce::String (int (db)) : db < 0.0f ? Glyph::minus() + juce::String (9) : juce::String ("0"),
                        juce::Rectangle<float> (p.getX() - 22.0f, y - 6.0f, 20.0f, 12.0f), juce::Justification::centredRight);
        }

        // A wash that follows the level, so the curve reads as live without pretending to
        // be a spectrum we do not measure.
        if (running && level > 0.01f)
        {
            g.setColour (Dine::accent.withAlpha (0.07f * level));
            g.fillRect (p.getX(), p.getBottom() - p.getHeight() * 0.45f * level, p.getWidth(), p.getHeight() * 0.45f * level);
        }

        const auto only = isolate (params, spec->id);
        juce::Path curve;
        constexpr int kPoints = 180;
        for (int i = 0; i < kPoints; ++i)
        {
            const float hz = 20.0f * std::pow (10.0f, 3.0f * float (i) / float (kPoints - 1));
            const float x = p.getX() + p.getWidth() * float (i) / float (kPoints - 1);
            const float y = yForDb (EqResponse::chainMagnitudeDb (only, sampleRate, hz));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        auto fill = curve;
        fill.lineTo (p.getRight(), yForDb (0.0f));
        fill.lineTo (p.getX(), yForDb (0.0f));
        fill.closeSubPath();
        g.setColour (Dine::accent.withAlpha (isEnabled() ? 0.13f : 0.05f));
        g.fillPath (fill);
        g.setColour (isEnabled() ? Dine::accent : Dine::ink4);
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        for (const auto& n : nodes())
        {
            const bool sel = n.band >= 0 && n.band == band;
            const float rad = sel ? 10.0f : 8.0f;
            auto c = juce::Rectangle<float> (n.pos.x - rad, n.pos.y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillEllipse (c.translated (0.0f, 1.0f));
            g.setColour (! n.on ? juce::Colours::white.withAlpha (0.16f) : sel ? Dine::accent : Dine::accent.withAlpha (0.55f));
            g.fillEllipse (c);
            if (sel)
            {
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawEllipse (c, 1.5f);
            }
            g.setColour (n.on && sel ? juce::Colour (0xff0f1a18) : Dine::ink);
            g.setFont (Dine::text (9.5f, 600));
            g.drawText (n.label, c, juce::Justification::centred);
        }

        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (10.5f));
        g.drawText (readout(), getLocalBounds().reduced (14, 8).removeFromTop (14), juce::Justification::topRight, true);
        g.setColour (Dine::ink4);
        g.setFont (capsFont (9.5f, 600));
        g.drawText (nodes().empty() ? juce::String() : juce::String ("DRAG A NODE"),
                    getLocalBounds().reduced (14, 8).removeFromTop (14), juce::Justification::topLeft, true);
    }

    juce::String readout() const
    {
        if (spec == nullptr) return {};
        if (spec->bands > 0)
        {
            const int i = juce::jlimit (0, spec->bands - 1, band);
            const auto& b = spec->corrective ? params.correctiveBands[size_t (i)] : params.toneBands[size_t (i)];
            return "BAND " + juce::String (i + 1) + "   " + hzText (b.freqHz) + "   " + signedNumber (b.gainDb, 1)
                   + " dB   " + format (Fmt::Q, b.q);
        }
        if (spec->id == StageId::DeEss)
            return hzText (params.deEssHz) + "   " + signedNumber (params.deEssThresholdDb, 0) + " dB   up to "
                   + juce::String (params.deEssRangeDb, 1) + " dB down";
        juce::StringArray parts;
        if (params.hpfEnabled) parts.add ("HIGH-PASS " + hzText (params.hpfHz));
        if (params.lpfEnabled) parts.add ("LOW-PASS " + hzText (params.lpfHz));
        return parts.isEmpty() ? juce::String (Glyph::dash()) : parts.joinIntoString ("   " + Glyph::dot() + "   ");
    }

    void paintTransfer (juce::Graphics& g, juce::Rectangle<int> r)
    {
        r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 300));
        const int size = juce::jlimit (120, juce::jmax (120, r.getHeight()), juce::jmin (r.getHeight(), r.getWidth() / 2));
        auto square = r.removeFromLeft (size).withSizeKeepingCentre (size, size).toFloat();
        r.removeFromLeft (18);

        const bool sat = spec->id == StageId::Sat;
        auto xFor = [&] (float db) { return square.getX() + square.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
        auto yFor = [&] (float db) { return square.getBottom() - square.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };

        for (int i = 0; i <= 4; ++i)
        {
            const float t = float (i) / 4.0f;
            g.setColour (Dine::hairSoft);
            g.fillRect (square.getX() + square.getWidth() * t, square.getY(), 0.5f, square.getHeight());
            g.fillRect (square.getX(), square.getY() + square.getHeight() * t, square.getWidth(), 0.5f);
        }
        g.setColour (Dine::hair);
        g.drawLine (square.getX(), square.getBottom(), square.getRight(), square.getY(), 1.0f);

        juce::Path curve;
        constexpr int kPoints = 80;
        for (int i = 0; i < kPoints; ++i)
        {
            const float din = -60.0f + 66.0f * float (i) / float (kPoints - 1);
            const float dout = throughStage (din);
            const float x = xFor (din), y = yFor (dout);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (isEnabled() ? Dine::accent : Dine::ink4);
        g.strokePath (curve, juce::PathStrokeType (2.2f));

        if (! sat)
        {
            const float thr = spec->id == StageId::Comp ? params.compThresholdDb
                            : spec->id == StageId::Gate ? params.gateThresholdDb : params.limiterCeilingDb;
            const float dashes[] = { 3.0f, 3.0f };
            g.setColour (Dine::warn.withAlpha (0.55f));
            g.drawDashedLine ({ xFor (thr), square.getY(), xFor (thr), square.getBottom() }, dashes, 2, 1.0f);
            g.drawDashedLine ({ square.getX(), yFor (thr), square.getRight(), yFor (thr) }, dashes, 2, 1.0f);
        }
        if (running && inputDb > -100.0f)
        {
            const float x = xFor (inputDb), y = yFor (throughStage (inputDb));
            g.setColour (Dine::ink);
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        }
        g.setColour (Dine::ink4);
        g.setFont (capsFont (9.5f, 600));
        g.drawText ("IN " + Glyph::dash() + " OUT", square.withHeight (12.0f).translated (4.0f, 2.0f).toNearestInt(),
                    juce::Justification::topLeft);

        // Beside it: how much the stage is taking off right now, and the last few seconds.
        auto side = r;
        const juce::Colour tone = gr > 9.0f ? Dine::crit : gr > 4.0f ? Dine::warn : Dine::accent;
        auto top = side.removeFromTop (juce::jmin (100, side.getHeight() / 2));
        g.setColour (Dine::ink4);
        g.setFont (capsFont (9.5f, 600));
        g.drawText (sat ? "DRIVE" : "GAIN REDUCTION", top.removeFromTop (14), juce::Justification::topLeft);
        auto figures = top.removeFromTop (38);
        const juce::String big = sat ? juce::String (juce::roundToInt (params.satDrive * 100.0f)) + "%"
                                     : juce::String (gr, 1);
        const auto bigFont = Dine::mono (30.0f, 500);
        g.setColour (isEnabled() ? tone : Dine::ink4);
        g.setFont (bigFont);
        const int bw = Dine::textWidth (bigFont, big) + 8;
        g.drawText (big, figures.removeFromLeft (bw), juce::Justification::centredLeft);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        g.drawText (sat ? "into the valve" : "dB now", figures, juce::Justification::bottomLeft);
        top.removeFromTop (6);
        auto bar = top.removeFromTop (8);
        Dine::drawWell (g, bar.toFloat(), 2.0f);
        g.setColour (tone);
        const float amount = sat ? params.satDrive : juce::jlimit (0.0f, 1.0f, gr / 15.0f);
        g.fillRect (bar.toFloat().withWidth (bar.getWidth() * amount));

        side.removeFromTop (14);
        auto hist = side.removeFromTop (juce::jmin (72, side.getHeight()));
        if (hist.getHeight() > 24)
        {
            Dine::drawWell (g, hist.toFloat(), 4.0f);
            auto hr = hist.toFloat().reduced (4.0f);
            juce::Path line;
            for (size_t i = 0; i < history.size(); ++i)
            {
                const float x = hr.getX() + hr.getWidth() * float (i) / float (history.size() - 1);
                const float y = hr.getY() + juce::jlimit (0.0f, 1.0f, history[i] / 15.0f) * hr.getHeight();
                if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            g.setColour (tone.withAlpha (0.9f));
            g.strokePath (line, juce::PathStrokeType (1.6f));
            g.setColour (Dine::ink4);
            g.setFont (capsFont (9.5f, 600));
            g.drawText ("LAST 8 SECONDS", hist.reduced (6, 4).removeFromTop (12), juce::Justification::topLeft);
        }
    }

    float throughStage (float din) const
    {
        switch (spec->id)
        {
            case StageId::Comp:
            {
                const float k = juce::jmax (0.01f, params.compKneeDb), over = din - params.compThresholdDb;
                float dout = din;
                if (over > k * 0.5f) dout = params.compThresholdDb + over / juce::jmax (1.0f, params.compRatio);
                else if (over > -k * 0.5f)
                {
                    const float x = over + k * 0.5f;
                    dout = din + (1.0f / juce::jmax (1.0f, params.compRatio) - 1.0f) * x * x / (2.0f * k);
                }
                return dout + params.compMakeupDb;
            }
            case StageId::Gate:
            {
                float dout = din;
                if (din < params.gateThresholdDb)
                    dout = params.gateThresholdDb - (params.gateThresholdDb - din) * juce::jmax (1.0f, params.gateRatio);
                return juce::jmax (dout, din - params.gateRangeDb);
            }
            case StageId::Limiter:
                return juce::jmin (din, params.limiterCeilingDb);
            case StageId::Sat:
            default:
            {
                const float x = juce::jlimit (0.0f, 1.0f, (din + 60.0f) / 66.0f);
                const float d = 1.0f + params.satDrive * 8.0f;
                const float y = (std::tanh (x * d) / std::tanh (d)) * params.satMix + x * (1.0f - params.satMix);
                return y * 66.0f - 60.0f;
            }
        }
    }

    void paintBars (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const bool staging = spec->id == StageId::Input || spec->id == StageId::Output;
        int sliders = 0;
        for (const auto& f : spec->fields) if (f.kind == Field::Kind::Slider) ++sliders;
        const int rowH = 56;
        const int needed = sliders * rowH + (staging ? 24 + 2 * 30 + 12 : 0);
        auto rows = r.reduced (10, 6);
        rows = rows.withSizeKeepingCentre (rows.getWidth(), juce::jmin (rows.getHeight(), needed));

        for (const auto& f : spec->fields)
        {
            if (f.kind != Field::Kind::Slider) continue;
            auto row = rows.removeFromTop (rowH).withTrimmedBottom (8);
            const double v = f.get (params);
            const double zero = f.min < 0.0 ? (0.0 - f.min) / (f.max - f.min) : 0.0;
            const double t = (v - f.min) / juce::jmax (1.0e-9, f.max - f.min);
            auto head = row.removeFromTop (16);
            g.setColour (Dine::ink3);
            g.setFont (capsFont (10.0f, 600));
            g.drawText (f.label.trim().toUpperCase(), head.removeFromLeft (head.getWidth() - 120), juce::Justification::centredLeft);
            g.setColour (isEnabled() ? Dine::ink : Dine::ink4);
            g.setFont (Dine::mono (12.5f, 500));
            g.drawText (format (f.fmt, v), head, juce::Justification::centredRight);
            auto bar = row.removeFromTop (14);
            Dine::drawWell (g, bar.toFloat(), 3.0f);
            const float x0 = bar.getX() + bar.getWidth() * float (juce::jmin (zero, t));
            const float w = juce::jmax (1.0f, bar.getWidth() * float (std::fabs (t - zero)));
            g.setColour (isEnabled() ? (v < 0.0 && f.min < 0.0 ? Dine::warn : Dine::accent) : Dine::ink4);
            g.fillRect (juce::Rectangle<float> (x0, bar.getY() + 2.0f, w, bar.getHeight() - 4.0f));
            if (f.min < 0.0)
            {
                g.setColour (Dine::hairStrong);
                g.fillRect (bar.getX() + bar.getWidth() * float (zero), float (bar.getY()), 0.5f, float (bar.getHeight()));
            }
            auto scale = row.removeFromTop (12);
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (9.5f));
            g.drawText (format (f.fmt, f.min), scale, juce::Justification::centredLeft);
            g.drawText (format (f.fmt, f.max), scale, juce::Justification::centredRight);
        }

        if (! staging || rows.getHeight() < 50) return;
        Dine::drawRule (g, rows.removeFromTop (1), Dine::hairSoft);
        rows.removeFromTop (8);
        g.setColour (Dine::ink4);
        g.setFont (capsFont (9.5f, 600));
        g.drawText ("GAIN STAGING, LIVE", rows.removeFromTop (14), juce::Justification::topLeft);
        const bool in = spec->id == StageId::Input;
        auto meterRow = [&] (const juce::String& label, const juce::String& note, float db)
        {
            auto row = rows.removeFromTop (30);
            auto text = row.removeFromLeft (140);
            g.setColour (Dine::ink3);
            g.setFont (capsFont (10.0f, 600));
            g.drawText (label, text.removeFromTop (14), juce::Justification::bottomLeft);
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.0f));
            g.drawText (note, text, juce::Justification::topLeft, true);
            auto value = row.removeFromRight (72);
            g.setColour (db <= -119.0f ? Dine::ink4 : Dine::ink);
            g.setFont (Dine::mono (12.0f, 500));
            g.drawText (db <= -119.0f ? Glyph::dash() : signedNumber (db, 1) + " dB", value, juce::Justification::centredRight);
            row.removeFromRight (10);
            auto bar = row.withSizeKeepingCentre (row.getWidth(), 9);
            Dine::drawWell (g, bar.toFloat(), 2.0f);
            g.setColour (Dine::levelColour (db));
            g.fillRect (bar.toFloat().withWidth (bar.getWidth() * DineMeter::norm (db)));
        };
        meterRow (in ? "ARRIVING" : "INTO THE TRIM", in ? "what the desk sends" : "the end of the chain", inputDb);
        meterRow (in ? "AFTER TRIM" : "INTO THE FADER",
                  format (Fmt::Db, in ? params.inputTrimDb : params.outputTrimDb)
                      + (in && params.polarityInvert ? "  " + Glyph::dot() + "  polarity flipped" : juce::String()),
                  outputDb);
    }

    void paintSends (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto rows = r.reduced (10, 8);
        const int rowH = 52;
        rows = rows.withSizeKeepingCentre (rows.getWidth(), juce::jmin (rows.getHeight(), rowH * int (sends.size())));
        for (const auto& s : sends)
        {
            auto row = rows.removeFromTop (rowH).withTrimmedBottom (8);
            auto head = row.removeFromTop (16);
            const bool off = s.second <= kSilenceDb + 0.01f;
            g.setColour (Dine::ink3);
            g.setFont (capsFont (10.0f, 600));
            g.drawText (juce::String (sendName (s.first)).toUpperCase(), head.removeFromLeft (head.getWidth() - 100),
                        juce::Justification::centredLeft);
            g.setColour (off ? Dine::ink4 : Dine::ink);
            g.setFont (Dine::mono (12.5f, 500));
            g.drawText (off ? juce::String ("off") : signedNumber (s.second, 1) + " dB", head, juce::Justification::centredRight);
            auto bar = row.removeFromTop (14);
            Dine::drawWell (g, bar.toFloat(), 3.0f);
            if (! off)
            {
                g.setColour (Dine::accent);
                g.fillRect (bar.toFloat().reduced (0.0f, 2.0f).withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, (s.second + 60.0f) / 66.0f)));
            }
        }
    }

    static constexpr size_t kHistory = 240;   // 8 seconds at the page's rate

    Commit commit;
    std::function<void (int)> select;
    const StageSpec* spec = nullptr;
    ChannelParameters params;
    std::vector<std::pair<FxSlot, float>> sends;
    std::vector<float> history { std::vector<float> (kHistory, 0.0f) };
    double sampleRate = 48000.0;
    float gr = 0.0f, inputDb = -120.0f, outputDb = -120.0f, level = 0.0f;
    int band = 0, dragging = -1;
    bool running = false;
};

// ------------------------------------------------------------------ ChainEditor
ChainEditor::ChainEditor (MixController& c) : controller (c)
{
    controlsView.setViewedComponent (&controlsHolder, false);
    controlsView.setScrollBarsShown (true, false);
    addAndMakeVisible (controlsView);

    graph = std::make_unique<Graph> ([this] (const std::function<void (ChannelParameters&)>& edit) { commit (edit); },
                                     [this] (int b) { band = b; refresh(); });
    addAndMakeVisible (*graph);

    power = std::make_unique<DineSwitch> ("IN", "OUT");
    power->setClickingTogglesState (true);
    power->onClick = [this]
    {
        const auto& s = spec();
        if (s.setOn) commit ([&s, on = power->getToggleState()] (ChannelParameters& p) { s.setOn (p, on); });
    };
    addChildComponent (*power);

    revertButton = std::make_unique<DineButton> ("Back to DINE", DineButton::Style::Standard);
    revertButton->setFontPx (11.5f);
    revertButton->setIcon (Dine::Icon::Refresh);
    revertButton->onClick = [this] { revertStage(); };
    addChildComponent (*revertButton);
}

ChainEditor::~ChainEditor() = default;

void ChainEditor::showStrip (int index)
{
    isBus = false;
    strip = index;
    build();
}

void ChainEditor::showBus (MixBus b)
{
    isBus = true;
    bus = b;
    build();
}

const ChainEditor::StageSpec& ChainEditor::spec() const
{
    static const StageSpec none {};
    if (stages.empty()) return none;
    return stages[size_t (juce::jlimit (0, int (stages.size()) - 1, selected))];
}

ChannelParameters ChainEditor::read() const
{
    const auto& base = controller.getBase();
    if (isBus) return base.buses[size_t (bus)].channel;
    if (strip >= 0 && strip < base.numStrips) return base.strips[size_t (strip)].channel;
    return {};
}

void ChainEditor::write (const ChannelParameters& p)
{
    if (isBus) controller.setBusChannel (bus, p);
    else       controller.setStripChannel (strip, p);
}

void ChainEditor::commit (const std::function<void (ChannelParameters&)>& edit)
{
    if (controller.isBypassed()) return;
    auto p = read();
    edit (p);
    write (p);
    refresh();
}

const StripParameters* ChainEditor::plannedStrip() const
{
    const auto* plan = controller.getPlan();
    if (plan == nullptr || isBus || strip < 0 || strip >= plan->proposed.numStrips) return nullptr;
    return &plan->proposed.strips[size_t (strip)];
}

const ChannelParameters* ChainEditor::plannedChannel() const
{
    const auto* plan = controller.getPlan();
    if (plan == nullptr) return nullptr;
    if (isBus) return &plan->proposed.buses[size_t (bus)].channel;
    if (const auto* s = plannedStrip()) return &s->channel;
    return nullptr;
}

void ChainEditor::build()
{
    bool stereo = true;
    if (! isBus)
    {
        const auto& graphRef = controller.getGraph();
        stereo = strip >= 0 && strip < graphRef.numStrips() && graphRef.strips[size_t (strip)].numChannels() == 2;
    }
    // Only the master's chain has a limiter built into it (MixEngine configures it there),
    // so nowhere else offers one: a control that does nothing is worse than no control.
    const bool hasLimiter = isBus && bus == MixBus::Master;

    stages = chainSpecs (stereo, hasLimiter);

    // The sends leave after the chain, so they close the path - and only where the
    // session actually uses a return.
    if (! isBus)
    {
        const auto& g = controller.getGraph();
        bool any = false;
        for (int f = 0; f < int (FxSlot::Count); ++f) any = any || g.fxUsed[size_t (f)];
        if (any)
        {
            StageSpec s;
            s.id = StageId::Sends;
            s.name = "Sends";
            s.plain = "How much of this channel goes to each FX return.";
            s.icon = Dine::Icon::Fx;
            stages.push_back (std::move (s));
        }
    }

    selected = juce::jlimit (0, int (stages.size()) - 1, selected);
    band = 0;
    buildControls();
    refresh();
    if (onStageChanged) onStageChanged();
}

void ChainEditor::selectStage (int index)
{
    index = juce::jlimit (0, juce::jmax (0, int (stages.size()) - 1), index);
    if (index == selected) return;
    selected = index;
    band = 0;
    buildControls();
    refresh();
    if (onStageChanged) onStageChanged();
}

void ChainEditor::toggleStage (int index)
{
    if (index < 0 || index >= int (stages.size())) return;
    const auto& s = stages[size_t (index)];
    if (! s.setOn || ! s.isOn) return;
    const bool on = ! s.isOn (read());
    commit ([&s, on] (ChannelParameters& p) { s.setOn (p, on); });
    if (onStageChanged) onStageChanged();
}

void ChainEditor::buildControls()
{
    controls.clear();
    controlsHolder.removeAllChildren();
    const auto& s = spec();
    graph->setStage (stages.empty() ? nullptr : &s);

    auto commitFn = [this] (const std::function<void (ChannelParameters&)>& edit) { commit (edit); };

    if (s.id == StageId::Sends)
    {
        const auto& g = controller.getGraph();
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (! g.fxUsed[size_t (f)]) continue;
            const auto slot = FxSlot (f);
            auto row = std::make_unique<SendRow> (slot, [this, slot] (float db)
            {
                if (! controller.isBypassed()) controller.setStripSend (strip, slot, db);
            });
            controlsHolder.addAndMakeVisible (*row);
            controls.push_back (std::move (row));
        }
    }
    else if (s.bands > 0)
    {
        for (int i = 0; i < s.bands; ++i)
        {
            auto card = std::make_unique<BandCard> (i, s.corrective, commitFn, [this] (int b) { band = b; refresh(); });
            controlsHolder.addAndMakeVisible (*card);
            controls.push_back (std::move (card));
        }
    }
    else
    {
        for (const auto& f : s.fields)
        {
            if (f.kind == Field::Kind::Slider)
            {
                auto knob = std::make_unique<Knob> (f, [this, f] (double v)
                {
                    commit ([&f, v] (ChannelParameters& p) { f.set (p, v); });
                }, Dine::accent);
                controlsHolder.addAndMakeVisible (*knob);
                controls.push_back (std::move (knob));
            }
            else
            {
                auto chip = std::make_unique<SwitchChip> (f, [this, f] (double v)
                {
                    commit ([&f, v] (ChannelParameters& p) { f.set (p, v); });
                });
                controlsHolder.addAndMakeVisible (*chip);
                controls.push_back (std::move (chip));
            }
        }
    }
    resized();
}

bool ChainEditor::stageEdited (int index) const
{
    if (index < 0 || index >= int (stages.size())) return false;
    const auto& s = stages[size_t (index)];
    const auto cur = read();

    if (s.id == StageId::Sends)
    {
        const auto* planned = plannedStrip();
        if (planned == nullptr || strip < 0 || strip >= controller.getBase().numStrips) return false;
        const auto& now = controller.getBase().strips[size_t (strip)];
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (std::fabs (now.sendDb[size_t (f)] - planned->sendDb[size_t (f)]) > 0.05f) return true;
        return false;
    }

    const auto* planned = plannedChannel();
    if (planned == nullptr) return false;
    if (s.isOn && s.isOn (cur) != s.isOn (*planned)) return true;
    for (const auto& f : s.fields)
        if (std::fabs (f.get (cur) - f.get (*planned)) > 1.0e-4) return true;
    for (int i = 0; i < s.bands; ++i)
    {
        const auto& a = s.corrective ? cur.correctiveBands[size_t (i)] : cur.toneBands[size_t (i)];
        const auto& b = s.corrective ? planned->correctiveBands[size_t (i)] : planned->toneBands[size_t (i)];
        if (a.enabled != b.enabled || a.type != b.type || std::fabs (a.freqHz - b.freqHz) > 0.5f
            || std::fabs (a.gainDb - b.gainDb) > 0.05f || std::fabs (a.q - b.q) > 0.005f)
            return true;
    }
    return false;
}

void ChainEditor::revertStage()
{
    const auto& s = spec();
    if (s.id == StageId::Sends)
    {
        if (const auto* planned = plannedStrip())
            for (int f = 0; f < int (FxSlot::Count); ++f)
                controller.setStripSend (strip, FxSlot (f), planned->sendDb[size_t (f)]);
        refresh();
        if (onStageChanged) onStageChanged();
        return;
    }
    const auto* planned = plannedChannel();
    if (planned == nullptr) return;
    auto p = read();
    for (const auto& f : s.fields) f.set (p, f.get (*planned));
    if (s.setOn && s.isOn) s.setOn (p, s.isOn (*planned));
    for (int i = 0; i < s.bands; ++i)
    {
        if (s.corrective) p.correctiveBands[size_t (i)] = planned->correctiveBands[size_t (i)];
        else              p.toneBands[size_t (i)] = planned->toneBands[size_t (i)];
    }
    write (p);
    refresh();
    if (onStageChanged) onStageChanged();
}

void ChainEditor::updateViews()
{
    const auto p = read();
    bool stereo = true;
    if (! isBus)
    {
        const auto& g = controller.getGraph();
        stereo = strip >= 0 && strip < g.numStrips() && g.strips[size_t (strip)].numChannels() == 2;
    }
    // The same words, in the same order, as the strip along the foot of TRACKS and MIXER.
    const auto readouts = chainStages (p, isBus && bus == MixBus::Master, stereo);

    const ChannelProcessor* proc = nullptr;
    if (controller.isPrepared())
    {
        if (isBus) proc = &controller.getEngine().getBus (bus);
        else if (strip >= 0 && strip < controller.getGraph().numStrips()) proc = &controller.getEngine().getStrip (strip);
    }
    auto reduction = [proc] (StageId id) -> float
    {
        if (proc == nullptr) return 0.0f;
        switch (id)
        {
            case StageId::Gate:    return juce::jmax (0.0f, -proc->getGate().getGainReductionDb());
            case StageId::Comp:    return juce::jmax (0.0f, -proc->getCompressor().getGainReductionDb());
            case StageId::DeEss:   return juce::jmax (0.0f, -proc->getDeEsser().getGainReductionDb());
            case StageId::Limiter: return juce::jmax (0.0f, -proc->getLimiter().getGainReductionDb());
            default:               return 0.0f;
        }
    };

    // The sentences TUNE MIX wrote about this channel, so a stage can carry its own why.
    std::vector<const Recommendation*> items;
    if (const auto* plan = controller.getPlan())
    {
        if (isBus)
        {
            const auto& bp = plan->buses[size_t (bus)];
            if (bp.tune.valid) for (const auto& r : bp.tune.report.items) items.push_back (&r);
        }
        else if (strip >= 0 && strip < int (plan->strips.size()))
        {
            const auto& sp = plan->strips[size_t (strip)];
            for (const auto& r : sp.mixItems) items.push_back (&r);
            for (const auto& r : sp.tune.report.items) items.push_back (&r);
        }
    }
    auto whyFor = [&items] (const StageSpec& s) -> juce::String
    {
        for (const auto* r : items)
            for (const auto& c : r->changes)
                for (const auto& id : s.ids)
                    if (juce::String (c.paramId).startsWith (id)) return juce::String (r->why);
        return s.plain;
    };

    views.resize (stages.size());
    for (size_t i = 0; i < stages.size(); ++i)
    {
        const auto& s = stages[i];
        auto& v = views[i];
        v.why = whyFor (s);
        v.icon = s.icon;
        v.switchable = s.setOn != nullptr;
        v.on = s.isOn ? s.isOn (p) : true;
        v.edited = stageEdited (int (i));
        v.grDb = reduction (s.id);
        if (s.id == StageId::Sends)
        {
            v.label = "SENDS";
            int used = 0;
            if (strip >= 0 && strip < controller.getBase().numStrips)
                for (const auto db : controller.getBase().strips[size_t (strip)].sendDb)
                    if (db > kSilenceDb + 0.01f) ++used;
            v.value = used > 0 ? juce::String (used) + (used == 1 ? " send" : " sends") : Glyph::dash();
            v.on = used > 0;
        }
        else if (i < readouts.size())
        {
            v.label = readouts[i].label;
            v.value = readouts[i].value;
        }
        else
        {
            v.label = s.name.toUpperCase();
            v.value = s.summary ? s.summary (p) : juce::String();
        }
    }
}

void ChainEditor::refresh()
{
    if (stages.empty()) return;
    bypassed = controller.isBypassed();
    updateViews();

    const auto p = read();
    const auto& s = spec();
    stageOn = s.isOn ? s.isOn (p) : true;
    edited = stageEdited (selected);
    const bool live = ! bypassed && (stageOn || ! s.isOn);

    if (edited)                 { badge = "HAND-EDITED";   badgeTint = Dine::warn; }
    else if (! stageOn)         { badge = "LEFT OUT";      badgeTint = Dine::ink3; }
    else if (controller.getPlan() != nullptr) { badge = "TUNED BY DINE"; badgeTint = Dine::accent; }
    else                        { badge = "BASELINE";      badgeTint = Dine::ink3; }

    if (power != nullptr)
    {
        power->setVisible (s.setOn != nullptr);
        power->setToggleState (stageOn, juce::dontSendNotification);
        power->setEnabled (! bypassed);
    }
    if (revertButton != nullptr)
    {
        const bool canRevert = edited && (s.id == StageId::Sends ? plannedStrip() != nullptr : plannedChannel() != nullptr);
        if (revertButton->isVisible() != canRevert) { revertButton->setVisible (canRevert); resized(); }
        revertButton->setEnabled (! bypassed);
    }

    // The controls follow the channel, whatever moved it: a macro, a TUNE MIX or a knob here.
    if (s.id == StageId::Sends)
    {
        const auto& base = controller.getBase();
        for (auto& c : controls)
            if (auto* row = dynamic_cast<SendRow*> (c.get()))
                row->pull (strip >= 0 && strip < base.numStrips ? base.strips[size_t (strip)].sendDb[size_t (row->slot)] : kSilenceDb,
                           ! bypassed);
    }
    else if (s.bands > 0)
    {
        for (size_t i = 0; i < controls.size(); ++i)
            if (auto* card = dynamic_cast<BandCard*> (controls[i].get()))
                card->pull (p, live, int (i) == band);
    }
    else
    {
        size_t next = 0;
        for (const auto& f : s.fields)
        {
            if (next >= controls.size()) break;
            if (auto* knob = dynamic_cast<Knob*> (controls[next].get())) knob->setValue (f.get (p));
            else if (auto* chip = dynamic_cast<SwitchChip*> (controls[next].get())) chip->setIndex (juce::roundToInt (f.get (p)));
            controls[next]->setEnabled (live);
            ++next;
        }
    }

    // What the graph needs: the live meters, the reduction of this stage and the sends.
    float grDb = 0.0f, inDb = -120.0f, outDb = -120.0f, level = 0.0f;
    if (controller.isPrepared())
    {
        const ChannelProcessor* proc = isBus ? &controller.getEngine().getBus (bus)
                                             : (strip >= 0 && strip < controller.getGraph().numStrips()
                                                    ? &controller.getEngine().getStrip (strip) : nullptr);
        if (proc != nullptr)
        {
            inDb = proc->getInputMeter().getMaxPeakDb();
            outDb = proc->getOutputMeter().getMaxPeakDb();
            level = DineMeter::norm (outDb);
            grDb = views[size_t (selected)].grDb;
        }
    }
    std::vector<std::pair<FxSlot, float>> sends;
    if (s.id == StageId::Sends)
    {
        const auto& g = controller.getGraph();
        const auto& base = controller.getBase();
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (g.fxUsed[size_t (f)])
                sends.push_back ({ FxSlot (f), strip >= 0 && strip < base.numStrips ? base.strips[size_t (strip)].sendDb[size_t (f)] : kSilenceDb });
    }
    graph->setEnabled (live);
    graph->update (p, controller.getSampleRate(), grDb, inDb, outDb, level, band, ! bypassed, std::move (sends));
    repaint();
}

void ChainEditor::resized()
{
    auto area = getLocalBounds();
    auto head = area.removeFromTop (kHeaderH).reduced (0, 4);
    if (power != nullptr && power->isVisible())
        power->setBounds (head.removeFromRight (70).withSizeKeepingCentre (70, 22));
    if (revertButton != nullptr && revertButton->isVisible())
    {
        head.removeFromRight (10);
        revertButton->setBounds (head.removeFromRight (juce::jmax (110, revertButton->idealWidth()))
                                     .withSizeKeepingCentre (juce::jmax (110, revertButton->idealWidth()), Dine::Metric::button));
    }

    area.removeFromTop (10);
    const auto& s = spec();
    const int colW = juce::jmin (s.bands > 0 ? kColBandW : kColW, juce::jmax (0, area.getWidth() - kGraphMinW - 14));
    auto column = area.removeFromRight (juce::jmax (0, colW));
    area.removeFromRight (14);
    graph->setBounds (area);

    controlsView.setBounds (column);
    int y = 0;
    const int inner = juce::jmax (60, column.getWidth() - 12);

    if (s.bands > 0)
    {
        for (auto& c : controls) { c->setBounds (0, y, inner, kBandCardH); y += kBandCardH + 8; }
    }
    else if (s.id == StageId::Sends)
    {
        for (auto& c : controls) { c->setBounds (0, y, inner, kSendRowH); y += kSendRowH + 6; }
    }
    else
    {
        const int cellW = (inner - 8) / 2;
        int col = 0;
        std::vector<juce::Component*> chips;
        for (auto& c : controls)
        {
            if (dynamic_cast<Knob*> (c.get()) == nullptr) { chips.push_back (c.get()); continue; }
            c->setBounds (col * (cellW + 8), y, cellW, Knob::kDial);
            if (++col == 2) { col = 0; y += kKnobCellH; }
        }
        if (col != 0) y += kKnobCellH;
        int x = 0;
        for (auto* c : chips)
        {
            const int w = juce::jmin (inner, dynamic_cast<SwitchChip*> (c) != nullptr
                                                 ? static_cast<SwitchChip*> (c)->idealWidth() : 120);
            if (x > 0 && x + w > inner) { x = 0; y += kChipH + 8; }
            c->setBounds (x, y, w, kChipH);
            x += w + 8;
        }
        if (! chips.empty()) y += kChipH + 8;
    }
    controlsHolder.setSize (juce::jmax (60, column.getWidth() - (y > column.getHeight() ? 10 : 0)),
                            juce::jmax (y, column.getHeight()));
}

void ChainEditor::paint (juce::Graphics& g)
{
    const auto& s = spec();
    auto head = getLocalBounds().removeFromTop (kHeaderH).reduced (0, 4);
    if (power != nullptr && power->isVisible()) head.removeFromRight (70);
    if (revertButton != nullptr && revertButton->isVisible())
        head.removeFromRight (10 + juce::jmax (110, revertButton->idealWidth()));

    Dine::drawIcon (g, s.icon, head.removeFromLeft (20).toFloat().withSizeKeepingCentre (19.0f, 19.0f),
                    stageOn && ! bypassed ? Dine::accent : Dine::glyph);
    head.removeFromLeft (11);
    const auto nameFont = Dine::text (19.0f, 600);
    g.setColour (Dine::ink);
    g.setFont (nameFont);
    g.drawText (s.name, head.removeFromLeft (juce::jmin (head.getWidth(), Dine::textWidth (nameFont, s.name))),
                juce::Justification::centredLeft);
    head.removeFromLeft (14);

    if (badge.isNotEmpty())
    {
        const int w = int (Dine::pillWidth (badge, false));
        auto pill = head.removeFromRight (juce::jmin (head.getWidth(), w)).withSizeKeepingCentre (w, 22).toFloat();
        Dine::drawPill (g, pill, badge, badgeTint);
        head.removeFromRight (12);
    }
    if (head.getWidth() > 80)
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawText (bypassed ? "BYPASS is on " + Glyph::dash() + " nothing in the chain is running." : s.plain,
                    head, juce::Justification::centredLeft, true);
    }
}

// ------------------------------------------------------------------ SignalPath
SignalPath::SignalPath (ChainEditor& c) : chain (c) {}

void SignalPath::refresh() { repaint(); }

juce::Rectangle<int> SignalPath::chipBounds (int index) const
{
    const int n = chain.numStages();
    if (n <= 0) return {};
    auto row = getLocalBounds().withTrimmedTop (titleH);
    const int gap = 10;
    const float w = float (row.getWidth() - gap * (n - 1)) / float (n);
    return juce::Rectangle<int> (row.getX() + juce::roundToInt (float (index) * (w + float (gap))), row.getY(),
                                 juce::jmax (24, juce::roundToInt (w)), row.getHeight());
}

int SignalPath::chipAt (juce::Point<int> p) const
{
    for (int i = 0; i < chain.numStages(); ++i)
        if (chipBounds (i).contains (p)) return i;
    return -1;
}

void SignalPath::paint (juce::Graphics& g)
{
    auto title = getLocalBounds().removeFromTop (titleH);
    g.setColour (Dine::ink3);
    g.setFont (capsFont (10.0f, 700));
    const juce::String label = "SIGNAL PATH";
    g.drawText (label, title.removeFromLeft (Dine::textWidth (capsFont (10.0f, 700), label)), juce::Justification::centredLeft);
    title.removeFromLeft (14);

    auto legend = [&] (const juce::String& text, juce::Colour c)
    {
        const int w = Dine::textWidth (Dine::text (11.0f), text) + 16;
        auto r = title.removeFromRight (w);
        g.setColour (c);
        g.fillRect (r.removeFromLeft (7).withSizeKeepingCentre (6, 6));
        r.removeFromLeft (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        g.drawText (text, r, juce::Justification::centredLeft);
        title.removeFromRight (14);
    };
    legend ("hand-edited", Dine::warn);
    legend ("tuned by DINE", Dine::accent);

    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
    g.drawText ("Click a stage to work on it. The lamp switches it in and out.", title,
                juce::Justification::centredLeft, true);

    const auto& views = chain.stageViews();
    for (int i = 0; i < int (views.size()); ++i)
    {
        const auto& v = views[size_t (i)];
        const bool sel = i == chain.selectedStage();
        auto chip = chipBounds (i);

        Dine::fillRounded (g, chip.toFloat(), sel ? juce::Colours::white.withAlpha (0.09f)
                                                  : v.on ? juce::Colours::white.withAlpha (hover == i ? 0.07f : 0.035f)
                                                         : juce::Colours::white.withAlpha (hover == i ? 0.05f : 0.015f),
                           Dine::Radius::card);
        Dine::hairlineRounded (g, chip.toFloat(), sel ? Dine::accent.withAlpha (0.85f) : Dine::hair, Dine::Radius::card);

        auto r = chip.reduced (7, 8);
        auto top = r.removeFromTop (10);
        auto lamp = top.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat();
        if (v.switchable)
        {
            g.setColour (v.on ? Dine::accent : juce::Colours::white.withAlpha (0.16f));
            g.fillEllipse (lamp);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawEllipse (lamp.reduced (0.5f), 1.0f);
        }
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (9.0f));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), top, juce::Justification::centredRight);

        r.removeFromTop (5);
        Dine::drawIcon (g, v.icon, r.removeFromTop (17).toFloat().removeFromLeft (17.0f),
                        ! v.on ? Dine::ink4 : sel ? Dine::accent : Dine::glyph);
        r.removeFromTop (4);
        g.setColour (v.on ? Dine::ink : Dine::ink3);
        g.setFont (Dine::text (10.0f, 700).withExtraKerningFactor (0.05f));
        // A long name (TRANSIENT) is squeezed rather than cut: a chip you cannot read is
        // a chip you cannot pick.
        g.drawFittedText (v.label, r.removeFromTop (14), juce::Justification::topLeft, 1, 0.62f);
        g.setColour (v.on ? Dine::ink3 : Dine::ink4);
        g.setFont (Dine::mono (9.5f));
        g.drawText (v.value, r.removeFromTop (13), juce::Justification::topLeft, true);

        // Along the foot: how hard the stage is working, and where its settings came from.
        auto foot = r.removeFromBottom (3);
        auto mark = foot.removeFromRight (6);
        if (v.grDb > 0.05f)
        {
            g.setColour (v.grDb > 8.0f ? Dine::warn : Dine::accent);
            const int w = juce::jlimit (1, foot.getWidth(), juce::roundToInt (foot.getWidth() * juce::jlimit (0.0f, 1.0f, v.grDb / 15.0f)));
            g.fillRect (foot.removeFromRight (w));
        }
        g.setColour (v.edited ? Dine::warn : v.on ? Dine::accent.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.12f));
        g.fillRect (mark.withSizeKeepingCentre (5, 5));

        if (i + 1 < int (views.size()))
        {
            auto arrow = juce::Rectangle<int> (chip.getRight(), chip.getY(), 10, chip.getHeight());
            g.setColour (v.on ? Dine::accent.withAlpha (0.55f) : Dine::ink4);
            g.setFont (Dine::text (11.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")), arrow, juce::Justification::centred);
        }
    }
}

void SignalPath::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown()) return;
    const int i = chipAt (e.getPosition());
    if (i < 0) return;
    auto chip = chipBounds (i);
    const bool onLamp = e.x < chip.getX() + 26 && e.y < chip.getY() + 26;
    if (onLamp && chain.stageViews()[size_t (i)].switchable) chain.toggleStage (i);
    else chain.selectStage (i);
}

void SignalPath::mouseMove (const juce::MouseEvent& e)
{
    const int i = chipAt (e.getPosition());
    if (i == hover) return;
    hover = i;
    setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void SignalPath::mouseExit (const juce::MouseEvent&)
{
    if (hover < 0) return;
    hover = -1;
    repaint();
}

} // namespace livemix
