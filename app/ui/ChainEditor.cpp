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
    // The design's Stage card (`65:9397`) and its Controls (`84:9339`), to the pixel: the
    // well is inset 24 from the card, the drawing 20 inside the well, a knob is an 80 x 82
    // cell and a choice is a 14 pt caption over a 28 pt segment track.
    constexpr int kCardPad   = 24;   // the card's own gutter
    constexpr int kWellPad   = 20;   // ... and the well's, inside it
    constexpr int kGraphH    = 220;  // what the drawing gets when the card has the room
    constexpr int kGraphMinH = 130;  // ... and the least it is worth drawing in
    constexpr int kRowGap    = 22;   // between the drawing and the knobs, and between knob rows
    constexpr int kChoiceH   = 46;   // caption 14 + 4 + track 28
    constexpr int kExtraH    = 32;   // the popup / button row (HEAR IT, Import a sound...)
    constexpr int kSendRowH  = 44;   // one send, as a labelled bar in the stage's drawing
    constexpr int kBarLabelW = 124;  // the name beside a labelled bar (the trims, the sends)
    constexpr float kEqRangeDb = 18.0f;

    enum class Fmt { Db, DbPlain, Hz, Ms, Ratio, Percent, Q, Bipolar, Semitones };

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

    juce::String formatValue (Fmt f, double v)
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
            case Fmt::Semitones: return std::fabs (v) < 0.05 ? juce::String ("as recorded") : signedNumber (v, 1) + " st";
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
    Input, Filters, Gate, Sample, CorrectiveEq, DeEss, Comp, Transient, ToneEq, Sat, Width, Limiter, Output, Sends
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

    // What a stage draws in its well. One per shape in the design's `Stage Controls / *`
    // set: a curve, a threshold over the signal, a gain-reduction trace, a transfer, the
    // envelope before and after, the stereo picture, labelled bars, or the sends.
    enum class GraphKind { Eq, Threshold, Reduction, Curve, Ceiling, Envelope, Stereo, Meters, Sends };

    GraphKind graphFor (StageId id) noexcept
    {
        switch (id)
        {
            case StageId::Filters:
            case StageId::CorrectiveEq:
            case StageId::ToneEq:     return GraphKind::Eq;
            case StageId::Gate:
            case StageId::Sample:     return GraphKind::Threshold;
            case StageId::DeEss:
            case StageId::Comp:       return GraphKind::Reduction;
            case StageId::Sat:        return GraphKind::Curve;
            case StageId::Limiter:    return GraphKind::Ceiling;
            case StageId::Transient:  return GraphKind::Envelope;
            case StageId::Width:      return GraphKind::Stereo;
            case StageId::Sends:      return GraphKind::Sends;
            default:                  return GraphKind::Meters;
        }
    }

    // The plain word a stage answers to on a workspace, beside its engineer's name. The
    // words are the product's, not this file's: CLEAN-UP, SMOOTH, STEADY, WARMTH, LOUD.
    juce::String macroWordFor (StageId id)
    {
        switch (id)
        {
            case StageId::Gate:     return "CLEAN-UP";
            case StageId::DeEss:    return "SMOOTH";
            case StageId::Comp:     return "STEADY";
            case StageId::ToneEq:   return "WARMTH and CLARITY";
            case StageId::Sat:      return "WARMTH";
            case StageId::Limiter:  return "LOUD";
            default:                return {};
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
    // The chain, in the order the audio meets it. `hasSample` is true on a kick, snare or tom
    // strip (MixEngine gives those the stage); `sounds` is the SOUND list the library loaded.
    std::vector<StageSpec> chainSpecs (bool stereo, bool hasLimiter, bool hasSample, const juce::StringArray& sounds)
    {
        std::vector<StageSpec> v;

        {
            StageSpec s;
            s.id = StageId::Input;
            s.name = "Input";
            s.plain = "Trim sets what arrives at the chain. It cannot undo a clip at the desk.";
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
            s.plain = "Takes out the rumble below the sound. Never above its lowest note.";
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
            s.plain = "Quiet between the hits, so the room and the bleed stay out.";
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
        if (hasSample)
        {
            // SAMPLE: a sound blended in on every hit the microphone catches. The microphone
            // stays; the sample sits with it. Plain words on the knobs; the engineer's words
            // (trigger, velocity, varispeed) are in the tooltips of the Advanced ones.
            StageSpec s;
            s.id = StageId::Sample;
            s.name = "Sample";
            s.plain = "A recorded drum, added on every hit this microphone catches. BLEND says how much of it you hear next to the microphone.";
            s.icon = Dine::Icon::Drum;
            s.ids = { "replace" };
            s.isOn = [] (const ChannelParameters& p) { return p.replaceEnabled; };
            s.setOn = [] (ChannelParameters& p, bool on) { p.replaceEnabled = on; };
            s.summary = [sounds] (const ChannelParameters& p)
            {
                if (! p.replaceEnabled) return juce::String ("off");
                const int i = juce::jlimit (0, juce::jmax (0, sounds.size() - 1), p.replaceSound);
                juce::String out = juce::String (juce::roundToInt (p.replaceBlend * 100.0f)) + " %";
                if (i < sounds.size()) out += "  " + Glyph::dot() + "  " + sounds[i];
                if (p.replaceFollowDrum && p.replaceDrumHz > 0.0f) out += "  " + Glyph::dot() + "  pitched to the drum (" + hzText (p.replaceDrumHz) + ")";
                return out;
            };
            Field sound;
            sound.kind = Field::Kind::Choice;
            sound.label = "Sound";
            sound.choices = sounds.isEmpty() ? juce::StringArray { "No sounds loaded" } : sounds;
            sound.get = [n = juce::jmax (1, sounds.size())] (const ChannelParameters& p) { return double (juce::jlimit (0, n - 1, p.replaceSound)); };
            sound.set = [] (ChannelParameters& p, double v) { p.replaceSound = juce::roundToInt (v); };
            s.fields = { number ("Blend", &ChannelParameters::replaceBlend, 0.0, 1.0, 0.01, 0.0, Fmt::Percent),
                         number ("Sensitivity", &ChannelParameters::replaceThresholdDb, -80.0, 0.0, 0.5, 0.0, Fmt::Db),
                         number ("Level", &ChannelParameters::replaceGainDb, -60.0, 12.0, 0.5, 0.0, Fmt::Db),
                         number ("Pitch", &ChannelParameters::replaceRateSemitones, -5.0, 5.0, 0.1, 0.0, Fmt::Semitones),
                         number ("Align", &ChannelParameters::replaceOffsetMs, 0.0, 5.0, 0.05, 0.0, Fmt::Ms),
                         number ("Rise", &ChannelParameters::replaceRiseDb, 0.0, 40.0, 0.5, 0.0, Fmt::DbPlain),
                         number ("Mask", &ChannelParameters::replaceMaskMs, 1.0, 500.0, 1.0, 60.0, Fmt::Ms),
                         number ("Listen above", &ChannelParameters::replaceDetHpfHz, 20.0, 2000.0, 1.0, 150.0, Fmt::Hz),
                         number ("Listen below", &ChannelParameters::replaceDetLpfHz, 100.0, 20000.0, 10.0, 3000.0, Fmt::Hz),
                         sound,
                         toggle ("Feel", &ChannelParameters::replaceSteady, "Follows the drummer", "Steady"),
                         toggle ("Tuning", &ChannelParameters::replaceFollowDrum, "As recorded", "Follows the drum"),
                         choice ("Polarity", &ChannelParameters::replacePolarity, { "Normal", "Flipped" }) };
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
            s.plain = "Takes the sting off the S and T sounds.";
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
            s.plain = "Evens out the loud and the quiet so the level holds.";
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
            s.plain = "More crack at the start of each hit and less ring after it.";
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
            s.plain = "The broad shape of the sound.";
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
            s.plain = "A little valve colour, and the glue that comes with it.";
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
            s.plain = "Wider or narrower than recorded, so the lead keeps the centre. The low end stays mono.";
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
            s.plain = "Holds the broadcast under its ceiling. It looks 1.5 ms ahead, and that latency is always reported.";
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
            s.plain = "What leaves the chain and meets the fader.";
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
            default:
                break;
        }
        return q;
    }
}

// ------------------------------------------------------------------ Knob
// One number, in the design's shape (`Knob`, 62:9301). The drawing, the drag and the
// double-click are DineKnob's - every rotary in the product is the same control - and this
// is only what a Field means by them.
class ChainEditor::Knob : public DineKnob
{
public:
    Knob (Field f, std::function<void (double)> apply, juce::Colour tint)
        : field (std::move (f))
    {
        const auto fmt = field.fmt;
        setRange (field.min, field.max, field.step, field.mid);
        setDefaultValue (field.get ? field.get (ChannelParameters {}) : field.min);
        setFormat ([fmt] (double v) { return formatValue (fmt, v); });
        setCaption (field.label);
        setTint (tint);
        setTooltip (field.label.trim());
        onChange = std::move (apply);
    }

    static int cellHeight() { return DineKnob::cellHeight (kDial); }

    static constexpr int kDial = 48;

private:
    Field field;
};

// ------------------------------------------------------------------ ChoiceGroup
// A switch or a choice the way the design writes one: the caption above, then the answer -
// a two- or three-segment track when there are that few, a popup when there are more (the
// sample library's SOUND list). Never a chip with the name inside it: a caption is read
// once and the answer is read every time.
class ChainEditor::ChoiceGroup : public juce::Component
{
public:
    ChoiceGroup (Field f, std::function<void (double)> apply)
        : field (std::move (f)), commit (std::move (apply))
    {
        // Two answers are a segment you can read both of at once; three or more is a list,
        // which is what the design draws for the sample library's SOUND and for a band's shape.
        usePopup = field.choices.size() > 2;
        if (usePopup)
        {
            popup = std::make_unique<DinePopup>();
            popup->onClick = [this]
            {
                juce::PopupMenu m;
                for (int i = 0; i < field.choices.size(); ++i) m.addItem (i + 1, field.choices[i], true, i == index);
                m.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (popup.get()),
                                 [this] (int r) { if (r > 0) commit (double (r - 1)); });
            };
            addAndMakeVisible (*popup);
        }
        else
        {
            addAndMakeVisible (track);
            for (int i = 0; i < field.choices.size(); ++i)
            {
                auto b = std::make_unique<DineButton> (field.choices[i], DineButton::Style::Segment);
                b->setFontPx (12.0f);
                b->onClick = [this, i] { commit (double (i)); };
                track.addAndMakeVisible (*b);
                segments.push_back (std::move (b));
            }
        }
        setIndex (0);
    }

    bool isPopup() const noexcept { return usePopup; }

    void setIndex (int i)
    {
        i = juce::jlimit (0, juce::jmax (0, field.choices.size() - 1), i);
        index = i;
        if (usePopup) popup->setValue (field.choices.isEmpty() ? juce::String() : field.choices[i]);
        else
            for (size_t k = 0; k < segments.size(); ++k)
                segments[k]->setToggleState (int (k) == i, juce::dontSendNotification);
        repaint();
    }

    int idealWidth() const
    {
        if (usePopup) return juce::jlimit (160, 300, popup->idealWidth());
        int w = 4;
        for (const auto& b : segments) w += segmentWidth (*b) + 2;
        return juce::jmax (w - 2, Dine::textWidth (Dine::text (11.0f, 500), field.label.trim()) + 8);
    }

    int height() const { return usePopup ? kExtraH : captionH() + 4 + Dine::Metric::control; }

    void paint (juce::Graphics& g) override
    {
        if (usePopup) return;                 // a popup says its own name in its value
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, field.label.trim(), getLocalBounds().removeFromTop (captionH()), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (usePopup) { popup->setBounds (r.withHeight (juce::jmin (r.getHeight(), kExtraH))); return; }
        r.removeFromTop (captionH() + 4);
        track.setBounds (r.withHeight (juce::jmin (r.getHeight(), Dine::Metric::control)));
        auto inner = track.getLocalBounds().reduced (2, 2);
        for (size_t k = 0; k < segments.size(); ++k)
        {
            const int w = k + 1 == segments.size() ? inner.getWidth() : segmentWidth (*segments[k]);
            segments[k]->setBounds (inner.removeFromLeft (w));
            inner.removeFromLeft (2);
        }
    }

    void enablementChanged() override
    {
        if (popup != nullptr) popup->setEnabled (isEnabled());
        for (auto& b : segments) b->setEnabled (isEnabled());
        repaint();
    }

private:
    int captionH() const { return juce::jmax (14, int (Dine::text (11.0f).getHeight())); }
    static int segmentWidth (const DineButton& b) { return juce::jmax (48, b.idealWidth()); }

    Field field;
    std::function<void (double)> commit;
    DineSegmentRow track;
    std::vector<std::unique_ptr<DineButton>> segments;
    std::unique_ptr<DinePopup> popup;
    bool usePopup = false;
    int index = 0;
};

// ------------------------------------------------------------------ SendKnob
// One FX send. A send is a knob on every console ever built, so it is a knob here: the
// stage's drawing shows how much of this channel each effect is getting, and the row of
// knobs under it is what sets them - the same control, in the same cell, as every other
// number in the Inspector.
class ChainEditor::SendKnob : public DineKnob
{
public:
    // The bottom of the travel is the off detent: the mix stores kSilenceDb, not kOffDb.
    static constexpr double kOffDb = -60.0;

    SendKnob (FxSlot f, std::function<void (float)> apply) : slot (f)
    {
        setRange (kOffDb, 6.0, 0.5);          // linear in decibels: the ladder above reads the same travel
        setDefaultValue (kOffDb);
        setCaption (sendName (slot));
        setFormat ([] (double v) { return v <= kOffDb + 0.01 ? juce::String ("off") : signedNumber (v, 1) + " dB"; });
        setTooltip (juce::String (sendName (slot)) + ": how much of this channel the effect gets. "
                    "All the way down is off.");
        onChange = [this, apply = std::move (apply)] (double v)
        {
            apply (v <= kOffDb + 0.01 ? kSilenceDb : float (v));
        };
    }

    // Off is decided from the mix value (kSilenceDb), not from the knob's stop: clamping
    // silence up to kOffDb would otherwise make "off" look the same as a -60 dB send.
    void pull (float db, bool live)
    {
        setValue (db <= kSilenceDb + 0.01f ? kOffDb : juce::jlimit (kOffDb, 6.0, double (db)));
        setEnabled (live);
    }

    FxSlot slot;
};

// ------------------------------------------------------------------ Graph
// WHAT THE STAGE IS DOING, DRAWN.
//
// One shape per stage, the design's `Stage Controls / *`: an EQ curve with a node you can
// drag; the signal with the line the stage is listening for across it; the gain reduction
// it is taking, second by second; the transfer it puts the signal through; the envelope
// before and after it; the stereo picture; the levels either side of a trim; the sends.
//
// Everything here is either measured from the engine or computed from the stage's own
// numbers. Nothing is decoration: a drawing that does not follow the sound is a lie about
// the sound, and an engineer would find it out in the first minute.
class ChainEditor::Graph : public juce::Component
{
public:
    using Commit = std::function<void (const std::function<void (ChannelParameters&)>&)>;

    Graph (Commit c, std::function<void (int)> pick) : commit (std::move (c)), select (std::move (pick)) {}

    void setStage (const StageSpec* s)
    {
        if (s == spec) return;             // picking a band is not a change of stage
        spec = s;
        grHistory.assign (kHistory, 0.0f);
        levelHistory.assign (kHistory, -120.0f);
        marks.assign (kHistory, false);
        repaint();
    }

    void update (const ChannelParameters& p, double sr, float grDb, float inDb, float outDb,
                 float levelNorm, int selectedBand, bool live, bool sampleFired,
                 std::vector<std::pair<FxSlot, float>> sendLevels)
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
        grHistory.erase (grHistory.begin());
        grHistory.push_back (gr);
        levelHistory.erase (levelHistory.begin());
        levelHistory.push_back (spec != nullptr && spec->id == StageId::Limiter ? outDb : inDb);
        marks.erase (marks.begin());
        marks.push_back (sampleFired);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        Dine::fillRounded (g, area, Dine::menubar, Dine::Radius::control);
        if (spec == nullptr) return;

        auto r = getLocalBounds().reduced (10, 10);
        switch (graphFor (spec->id))
        {
            case GraphKind::Eq:        paintEq (g); break;
            case GraphKind::Threshold: paintThreshold (g, r); break;
            case GraphKind::Reduction: spec->id == StageId::Comp ? paintCompressor (g, r) : paintReduction (g, r); break;
            case GraphKind::Curve:     paintCurve (g, r); break;
            case GraphKind::Ceiling:   paintCeiling (g, r); break;
            case GraphKind::Envelope:  paintEnvelope (g, r); break;
            case GraphKind::Stereo:    paintStereo (g, r); break;
            case GraphKind::Sends:     paintSends (g, r); break;
            case GraphKind::Meters:
            default:                   paintMeters (g, r); break;
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

    juce::Rectangle<float> plot() const { return getLocalBounds().reduced (10).toFloat().withTrimmedBottom (14); }

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
        });
    }

    // The caption a drawing carries in its own corner, in the design's Caption face.
    void caption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text,
                  juce::Colour colour, juce::Justification just = juce::Justification::topLeft) const
    {
        g.setColour (colour);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, text, r, just, true);
    }

    void paintEq (juce::Graphics& g)
    {
        auto p = plot();
        const auto gridFont = Dine::mono (10.0f);

        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = logX (hz, p.getX(), p.getWidth());
            g.setColour (Dine::hairSoft);
            g.fillRect (x, p.getY(), 0.5f, p.getHeight());
        }
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = logX (hz, p.getX(), p.getWidth());
            g.setColour (Dine::ink4);
            g.setFont (gridFont);
            Dine::drawText (g, hz >= 1000.0f ? juce::String (int (hz / 1000.0f)) + "k" : juce::String (int (hz)),
                        juce::Rectangle<float> (x - 22.0f, p.getBottom() - 15.0f, 20.0f, 13.0f), juce::Justification::centredRight);
        }
        for (float db : { 9.0f, 0.0f, -9.0f })
        {
            const float y = yForDb (db);
            g.setColour (db == 0.0f ? Dine::hair : Dine::hairSoft);
            g.fillRect (p.getX(), y, p.getWidth(), 0.5f);
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
        g.setColour (isEnabled() ? Dine::accent : Dine::ink4);
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        for (const auto& n : nodes())
        {
            const bool sel = n.band >= 0 && n.band == band;
            const float rad = sel ? 7.0f : 5.5f;
            auto c = juce::Rectangle<float> (n.pos.x - rad, n.pos.y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (! n.on ? juce::Colours::white.withAlpha (0.16f) : Dine::accent);
            g.fillEllipse (c);
            if (sel)
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawEllipse (c.expanded (2.0f), 1.2f);
            }
        }

        if (! nodes().empty())
            caption (g, getLocalBounds().reduced (12, 10).removeFromTop (14), "Drag a node", Dine::ink4);
    }

    // The signal, and the line the stage is listening for across it. The bars are the level
    // this channel has actually been running at; the line is where the threshold sits on the
    // same scale, so "is it opening" is one look.
    void paintThreshold (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const bool sample = spec->id == StageId::Sample;
        const float thresholdDb = sample ? params.replaceThresholdDb : params.gateThresholdDb;
        paintBars (g, r, levelHistory, isEnabled() ? Dine::ink3.withAlpha (0.55f) : Dine::ink4.withAlpha (0.35f));
        if (sample)
        {
            // Every hit the sampler actually fired, where it fired it.
            auto plotR = r.toFloat();
            for (size_t i = 0; i < marks.size(); ++i)
            {
                if (! marks[i]) continue;
                const float x = plotR.getX() + plotR.getWidth() * float (i) / float (juce::jmax<size_t> (1, marks.size() - 1));
                g.setColour (Dine::warn.withAlpha (0.85f));
                g.fillRect (x - 1.0f, plotR.getY(), 2.0f, plotR.getHeight());
            }
        }
        paintLevelLine (g, r, thresholdDb, sample ? Dine::crit : Dine::accent,
                        sample ? signedNumber (thresholdDb, 0) + " dB" : "Threshold");
    }

    // The ceiling and what is arriving at it: the same drawing, in the master's words.
    void paintCeiling (juce::Graphics& g, juce::Rectangle<int> r)
    {
        paintBars (g, r, levelHistory, isEnabled() ? Dine::accent.withAlpha (0.75f) : Dine::ink4.withAlpha (0.35f));
        paintLevelLine (g, r, params.limiterCeilingDb, Dine::crit,
                        "Ceiling " + signedNumber (params.limiterCeilingDb, 1) + " dBTP");
    }

    // How much the stage is taking off, second by second, and what that is right now.
    void paintReduction (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto tone = gr > 9.0f ? Dine::crit : Dine::warn;
        auto head = r.removeFromTop (18);
        caption (g, head, "Gain reduction", Dine::ink3);
        g.setColour (isEnabled() ? tone : Dine::ink4);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, juce::String (gr, 1) + " dB", head, juce::Justification::topRight);
        r.removeFromTop (4);
        Dine::drawRule (g, r.removeFromTop (1), Dine::hairSoft);

        // A scale, so the well below the trace reads as the room the stage has left rather
        // than as an empty box: 18 dB of reduction, marked every six.
        auto p = r.toFloat();
        for (int db = 6; db <= 12; db += 6)
        {
            const float y = p.getY() + p.getHeight() * float (db) / 18.0f;
            g.setColour (Dine::hairSoft);
            g.drawLine (p.getX(), y, p.getRight(), y, 1.0f);
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (9.5f, 500));
            Dine::drawText (g, "-" + juce::String (db), juce::Rectangle<float> (p.getX() + 2.0f, y + 1.0f, 26.0f, 11.0f).toNearestInt(),
                            juce::Justification::centredLeft);
        }

        juce::Path line;
        for (size_t i = 0; i < grHistory.size(); ++i)
        {
            const float x = p.getX() + p.getWidth() * float (i) / float (grHistory.size() - 1);
            const float y = p.getY() + juce::jlimit (0.0f, 1.0f, grHistory[i] / 18.0f) * p.getHeight();
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        g.setColour (isEnabled() ? tone.withAlpha (0.9f) : Dine::ink4);
        g.strokePath (line, juce::PathStrokeType (1.6f));
    }

    // THE COMPRESSOR, AS A COMPRESSOR.
    //
    // A gain-reduction trace on its own is a flat line at nought whenever nothing is playing,
    // which is most of the time somebody is setting one up - and it never says anything about
    // what the knobs are set to. So the well carries the transfer the stage is putting the
    // signal through, drawn from the engine's own gain computer (`Compressor::computeGain`,
    // so the picture and the audio cannot disagree) with the makeup and the blend on top of
    // it: threshold, ratio, knee, makeup and blend are all visible in the shape, and turning
    // any of them moves it. The live trace keeps its place beside it.
    void paintCompressor (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto tone = gr > 9.0f ? Dine::crit : Dine::warn;
        const auto ink = isEnabled() ? Dine::accent : Dine::ink4;

        // -60 .. +6 dB on both axes, the range the saturation curve uses, so the two stages
        // read as the same kind of picture.
        constexpr float kLo = -60.0f, kSpan = 66.0f;
        const auto out = [this] (float inDb)
        {
            const float wet = Compressor::computeGain (inDb, params.compThresholdDb, juce::jmax (1.0f, params.compRatio),
                                                       juce::jmax (0.0f, params.compKneeDb)) + params.compMakeupDb;
            const float blend = juce::jlimit (0.0f, 1.0f, params.compMix);
            return inDb * (1.0f - blend) + wet * blend;
        };

        auto square = r.removeFromLeft (juce::jlimit (110, 240, juce::jmin (r.getHeight(), r.getWidth() / 2)));
        r.removeFromLeft (18);
        {
            auto head = square.removeFromTop (18);
            caption (g, head, "In / out", Dine::ink3);
            square.removeFromTop (4);
            const float side = float (juce::jmin (square.getWidth(), square.getHeight()));
            auto box = square.toFloat().withSizeKeepingCentre (side, side);
            const auto xFor = [&] (float db) { return box.getX() + box.getWidth() * juce::jlimit (0.0f, 1.0f, (db - kLo) / kSpan); };
            const auto yFor = [&] (float db) { return box.getBottom() - box.getHeight() * juce::jlimit (0.0f, 1.0f, (db - kLo) / kSpan); };

            Dine::fillRounded (g, box, Dine::deep, 4.0f);
            // The line the signal would be on with the stage off, and the threshold it leaves it at.
            g.setColour (Dine::hairSoft);
            g.drawLine (box.getX(), box.getBottom(), box.getRight(), box.getY(), 1.0f);
            const float tx = xFor (params.compThresholdDb);
            g.setColour (Dine::hair);
            g.drawLine (tx, box.getY(), tx, box.getBottom(), 1.0f);

            juce::Path curve;
            constexpr int kPoints = 120;
            for (int i = 0; i < kPoints; ++i)
            {
                const float din = kLo + kSpan * float (i) / float (kPoints - 1);
                const float x = xFor (din), y = yFor (out (din));
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            g.setColour (ink);
            g.strokePath (curve, juce::PathStrokeType (2.2f));

            // Where the signal is on that curve right now, and how far the curve has pulled it down.
            if (running && inputDb > -100.0f)
            {
                const float x = xFor (inputDb), y = yFor (out (inputDb));
                if (gr > 0.05f)
                {
                    g.setColour (tone.withAlpha (0.55f));
                    g.drawLine (x, yFor (inputDb), x, y, 1.0f);
                }
                g.setColour (Dine::ink);
                g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            }

            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (9.5f, 500));
            Dine::drawText (g, juce::String (int (params.compThresholdDb)),
                            juce::Rectangle<float> (tx - 18.0f, box.getBottom() - 13.0f, 36.0f, 12.0f).toNearestInt(),
                            juce::Justification::centred);
        }

        paintReduction (g, r);
    }

    // The transfer the stage puts the signal through, against the line it would be without it.
    void paintCurve (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto square = r.toFloat().withSizeKeepingCentre (float (juce::jmin (r.getWidth(), r.getHeight())),
                                                         float (juce::jmin (r.getWidth(), r.getHeight())));
        auto xFor = [&] (float db) { return square.getX() + square.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
        auto yFor = [&] (float db) { return square.getBottom() - square.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };

        g.setColour (Dine::hairSoft);
        g.drawLine (square.getX(), square.getBottom(), square.getRight(), square.getY(), 1.0f);

        juce::Path curve;
        constexpr int kPoints = 90;
        for (int i = 0; i < kPoints; ++i)
        {
            const float din = -60.0f + 66.0f * float (i) / float (kPoints - 1);
            const float x = xFor (din), y = yFor (throughStage (din));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (isEnabled() ? Dine::accent : Dine::ink4);
        g.strokePath (curve, juce::PathStrokeType (2.2f));
        if (running && inputDb > -100.0f)
        {
            g.setColour (Dine::ink);
            g.fillEllipse (xFor (inputDb) - 3.5f, yFor (throughStage (inputDb)) - 3.5f, 7.0f, 7.0f);
        }
    }

    // The front of a hit and its tail, before the stage and after it: the shaping it is
    // doing, computed from its own two numbers.
    void paintEnvelope (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto legend = r.removeFromTop (18);
        caption (g, legend, "After", Dine::accent, juce::Justification::topRight);
        caption (g, legend.withTrimmedRight (Dine::textWidth (Dine::text (11.0f, 500), "After") + 14),
                 "Before", Dine::ink3, juce::Justification::topRight);
        r.removeFromTop (4);

        auto p = r.toFloat();
        constexpr int kHits = 4;
        const float hitW = p.getWidth() / float (kHits);
        auto envelope = [] (float t, float attack, float sustain)
        {
            // t in 0..1 across one hit: a fast rise then an exponential tail, with the two
            // controls doing to it exactly what the stage does to the signal.
            const float rise = juce::jlimit (0.0f, 1.0f, t / 0.02f);
            const float tail = std::exp (-t * (7.0f - sustain * 4.0f));
            const float peak = 1.0f + attack * 0.45f;
            return juce::jlimit (0.0f, 1.2f, rise * tail * peak);
        };
        auto trace = [&] (float attack, float sustain, juce::Colour colour, float thickness)
        {
            juce::Path path;
            constexpr int kPoints = 60;
            for (int h = 0; h < kHits; ++h)
                for (int i = 0; i < kPoints; ++i)
                {
                    const float t = float (i) / float (kPoints - 1);
                    const float x = p.getX() + hitW * (float (h) + t);
                    const float y = p.getBottom() - p.getHeight() * juce::jmin (1.0f, envelope (t, attack, sustain)) * 0.85f;
                    if (i == 0) path.startNewSubPath (x, p.getBottom()), path.lineTo (x, y);
                    else path.lineTo (x, y);
                }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (thickness));
        };
        trace (0.0f, 0.0f, Dine::ink3.withAlpha (0.55f), 1.2f);
        trace (params.transientAttack, params.transientSustain, isEnabled() ? Dine::accent : Dine::ink4, 1.8f);
    }

    // The stereo picture: the circle a mono source would sit on the middle of, and the
    // ellipse this width setting makes of it, with the frequency the bottom stays mono below.
    void paintStereo (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto foot = r.removeFromBottom (18);
        const float w = params.widthAmount;
        caption (g, foot, "Width " + juce::String (juce::roundToInt (w * 100.0f)) + "%"
                          + (params.widthMonoBelowHz >= 20.0f ? "   " + juce::String (Glyph::dot()) + "   mono below " + hzText (params.widthMonoBelowHz)
                                                              : juce::String()),
                 isEnabled() ? Dine::accent : Dine::ink4);

        const float size = float (juce::jmin (r.getWidth(), r.getHeight()));
        auto circle = r.toFloat().withSizeKeepingCentre (size, size).reduced (4.0f);
        g.setColour (Dine::hairSoft);
        g.drawEllipse (circle, 1.0f);
        g.fillRect (circle.getCentreX() - 0.25f, circle.getY(), 0.5f, circle.getHeight());

        // A width of 1.0 is the circle; wider spreads it sideways, narrower squeezes it in.
        const float spread = juce::jlimit (0.06f, 1.0f, w * 0.5f);
        auto shape = circle.withSizeKeepingCentre (circle.getWidth() * spread, circle.getHeight() * 0.94f);
        g.setColour ((isEnabled() ? Dine::accent : Dine::ink4).withAlpha (0.18f));
        g.fillEllipse (shape);
        g.setColour (isEnabled() ? Dine::accent : Dine::ink4);
        g.drawEllipse (shape, 1.6f);
    }

    // The levels either side of a trim, as the design's Input and Output draw them: what
    // arrives, what the trim made of it, and what leaves.
    // A LADDER, NOT A TRACK. Everything the drawing says about a level it says in lit and
    // unlit steps, the way a meter does: nothing that is only a reading should look like
    // something you could drag.
    void drawLadder (juce::Graphics& g, juce::Rectangle<int> row, float norm) const
    {
        auto bar = row.withSizeKeepingCentre (row.getWidth(), 7).toFloat();
        const float pitch = 6.0f, seg = 3.0f;
        const float lit = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, norm);
        for (float x = bar.getX(); x + seg <= bar.getRight(); x += pitch)
        {
            g.setColour (x < lit ? (isEnabled() ? Dine::accent : Dine::ink4) : Dine::well);
            g.fillRoundedRectangle (x, bar.getY(), seg, bar.getHeight(), 1.0f);
        }
    }

    // HOW MUCH OF THIS CHANNEL EACH EFFECT IS GETTING. One labelled bar per send, so the
    // picture says at a glance which effects this source is in and how far up - the knobs
    // under the drawing are what move them.
    void paintSends (juce::Graphics& g, juce::Rectangle<int> r) const
    {
        if (sends.empty())
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (13.0f));
            Dine::drawText (g, "This session has no effects set up yet.", r, juce::Justification::centred, true);
            return;
        }

        auto block = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), kSendRowH * int (sends.size())));
        for (const auto& send : sends)
        {
            auto line = block.removeFromTop (kSendRowH);
            // Off is the mix's own silence, not the bottom of the knob's travel.
            const bool off = send.second <= kSilenceDb + 0.01f;
            g.setColour (isEnabled() && ! off ? Dine::ink2 : Dine::ink4);
            g.setFont (Dine::text (13.0f, 500));
            Dine::drawText (g, sendName (send.first), line.removeFromLeft (kBarLabelW), juce::Justification::centredLeft, true);
            g.setColour (off ? Dine::ink4 : Dine::ink2);
            g.setFont (Dine::mono (11.0f, 500));
            Dine::drawText (g, off ? juce::String ("off") : signedNumber (send.second, 1) + " dB",
                            line.removeFromRight (62), juce::Justification::centredRight);
            line.removeFromRight (14);
            // It reads over the knob's own travel - all the way down is off, +6 dB is the top -
            // so the picture and the knob under it are saying the same thing.
            const float n = off ? 0.0f : juce::jlimit (0.0f, 1.0f,
                                                       float ((send.second - SendKnob::kOffDb) / (6.0 - SendKnob::kOffDb)));
            drawLadder (g, line, n);
        }
    }

    void paintMeters (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const bool in = spec->id == StageId::Input;
        struct Row { juce::String label; float db; };
        std::vector<Row> rows;
        if (in)
        {
            rows.push_back ({ "Arriving", inputDb });
            rows.push_back ({ "After trim", inputDb <= -119.0f ? inputDb : inputDb + params.inputTrimDb });
            rows.push_back ({ "Into the fader", outputDb });
        }
        else
        {
            rows.push_back ({ "Into the fader", outputDb <= -119.0f ? outputDb : outputDb - params.outputTrimDb });
            rows.push_back ({ "Out", outputDb });
        }

        const int rowH = 50;
        auto block = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), rowH * int (rows.size())));
        for (const auto& row : rows)
        {
            auto line = block.removeFromTop (rowH);
            auto label = line.removeFromLeft (kBarLabelW);
            g.setColour (isEnabled() ? Dine::ink2 : Dine::ink4);
            g.setFont (Dine::text (13.0f, 500));
            Dine::drawText (g, row.label, label, juce::Justification::centredLeft, true);
            drawLadder (g, line, DineMeter::norm (row.db));
        }
    }

    // The signal as bars, oldest at the left. One rectangle per column, so a 220 pt well
    // costs the same whatever the console is doing.
    void paintBars (juce::Graphics& g, juce::Rectangle<int> r, const std::vector<float>& history, juce::Colour colour) const
    {
        auto p = r.toFloat();
        const int columns = juce::jlimit (1, int (history.size()), int (p.getWidth() / 5.6f));
        const float step = p.getWidth() / float (columns);
        g.setColour (colour);
        for (int i = 0; i < columns; ++i)
        {
            const size_t from = history.size() * size_t (i) / size_t (columns);
            const size_t to   = juce::jmax (from + 1, history.size() * size_t (i + 1) / size_t (columns));
            float peak = -120.0f;
            for (size_t k = from; k < to && k < history.size(); ++k) peak = juce::jmax (peak, history[k]);
            const float h = juce::jmax (1.0f, p.getHeight() * DineMeter::norm (peak));
            g.fillRect (p.getX() + step * float (i), p.getBottom() - h, juce::jmax (1.0f, step - 2.6f), h);
        }
    }

    // A line across the drawing at a level, with what it is written above it.
    void paintLevelLine (juce::Graphics& g, juce::Rectangle<int> r, float db, juce::Colour colour,
                         const juce::String& label) const
    {
        auto p = r.toFloat();
        const float y = p.getBottom() - p.getHeight() * DineMeter::norm (db);
        g.setColour (isEnabled() ? colour : Dine::ink4);
        g.fillRect (p.getX(), y - 0.5f, p.getWidth(), 1.0f);
        g.setFont (Dine::text (11.0f, 500));
        // A ceiling at -1 dBTP sits right under the top of the well, so the label goes below
        // its line rather than half off the drawing.
        const int above = int (y) - 17;
        Dine::drawText (g, label, juce::Rectangle<int> (r.getX() + 2, above < r.getY() ? int (y) + 3 : above, r.getWidth() - 4, 14),
                    juce::Justification::topLeft, true);
    }

    float throughStage (float din) const
    {
        const float x = juce::jlimit (0.0f, 1.0f, (din + 60.0f) / 66.0f);
        const float d = 1.0f + params.satDrive * 8.0f;
        const float y = (std::tanh (x * d) / std::tanh (d)) * params.satMix + x * (1.0f - params.satMix);
        return y * 66.0f - 60.0f;
    }

    static constexpr size_t kHistory = 240;   // 8 seconds at the page's rate

    Commit commit;
    std::function<void (int)> select;
    const StageSpec* spec = nullptr;
    ChannelParameters params;
    std::vector<std::pair<FxSlot, float>> sends;
    std::vector<float> grHistory { std::vector<float> (kHistory, 0.0f) };
    std::vector<float> levelHistory { std::vector<float> (kHistory, -120.0f) };
    std::vector<char> marks { std::vector<char> (kHistory, 0) };
    double sampleRate = 48000.0;
    float gr = 0.0f, inputDb = -120.0f, outputDb = -120.0f, level = 0.0f;
    int band = 0, dragging = -1;
    bool running = false;
};

// ------------------------------------------------------------------ ChainEditor
ChainEditor::ChainEditor (MixController& c) : controller (c)
{
    controlsView.setViewedComponent (&controlsHolder, false);
    Dine::nativeScrolling (controlsView);
    controlsView.setScrollBarsShown (true, false);
    addAndMakeVisible (controlsView);

    graph = std::make_unique<Graph> ([this] (const std::function<void (ChannelParameters&)>& edit) { commit (edit); },
                                     [this] (int b) { band = b; buildControls(); refresh(); });
    controlsHolder.addAndMakeVisible (*graph);

    // Off / On, at the right of the title row. Two words rather than a lamp: a stage that is
    // out of the chain has to say so in the same breath as it says what it is.
    addChildComponent (onOffTrack);
    for (auto* b : { &offButton, &onButton })
    {
        b->setFontPx (12.0f);
        onOffTrack.addAndMakeVisible (*b);
    }
    auto power = [this] (bool on)
    {
        const auto& s = spec();
        if (s.setOn) commit ([&s, on] (ChannelParameters& p) { s.setOn (p, on); });
        if (onStageChanged) onStageChanged();
    };
    offButton.onClick = [power] { power (false); };
    onButton.onClick  = [power] { power (true); };

    revertButton = std::make_unique<DineButton> ("Put back", DineButton::Style::Standard);
    revertButton->setFontPx (11.5f);
    revertButton->setCaps (true);
    revertButton->setTooltip ("This stage back to what TUNE MIX set. Nothing else on the channel moves.");
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

    // A kick, snare or tom strip carries the sample stage, with the sounds the library loaded.
    bool hasSample = false;
    juce::StringArray sounds;
    if (! isBus)
    {
        const auto& g = controller.getGraph();
        if (strip >= 0 && strip < g.numStrips() && hasSampleStage (g.strips[size_t (strip)].role))
        {
            hasSample = true;
            if (const auto* table = controller.getSampleBanks())
                for (int i = 0; i < SampleBankTable::kSounds; ++i)
                    if (const auto* b = table->bank (roleFamily (g.strips[size_t (strip)].role), i)) sounds.add (juce::String (b->name));
        }
    }
    stages = chainSpecs (stereo, hasLimiter, hasSample, sounds);

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
            s.plain = "How much of this channel goes to each effect return.";
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
    controlsHolder.addAndMakeVisible (*graph);
    const auto& s = spec();
    graph->setStage (stages.empty() ? nullptr : &s);

    auto addKnob = [this] (Field f, std::function<void (ChannelParameters&, double)> set)
    {
        auto knob = std::make_unique<Knob> (f, [this, set] (double v)
        {
            commit ([&set, v] (ChannelParameters& p) { set (p, v); });
        }, Dine::accent);
        controlsHolder.addAndMakeVisible (*knob);
        controls.push_back (std::move (knob));
    };
    auto addChoice = [this] (Field f, std::function<void (ChannelParameters&, double)> set)
    {
        auto group = std::make_unique<ChoiceGroup> (f, [this, set] (double v)
        {
            commit ([&set, v] (ChannelParameters& p) { set (p, v); });
        });
        controlsHolder.addAndMakeVisible (*group);
        controls.push_back (std::move (group));
    };

    if (s.id == StageId::Sends)
    {
        const auto& g = controller.getGraph();
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (! g.fxUsed[size_t (f)]) continue;
            const auto slot = FxSlot (f);
            auto knob = std::make_unique<SendKnob> (slot, [this, slot] (float db)
            {
                if (! controller.isBypassed()) controller.setStripSend (strip, slot, db);
            });
            controlsHolder.addAndMakeVisible (*knob);
            controls.push_back (std::move (knob));
        }
    }
    else if (s.bands > 0)
    {
        // THE CURVE IS THE CONTROL, AND THE KNOBS ARE ONE PER BAND.
        //
        // The design draws a gain knob per band under the curve. A band also has a frequency,
        // a Q and a shape, and dropping them would make the Inspector unable to do the one
        // thing an engineer opens it for - so the band you picked (on the curve, or on its
        // knob) opens its own three under the row. Picking is the only thing that changes;
        // the sound never does.
        const bool corr = s.corrective;
        for (int i = 0; i < s.bands; ++i)
        {
            Field f;
            f.label = "Band " + juce::String (i + 1);
            f.min = -18.0; f.max = 18.0; f.step = 0.1; f.mid = 0.0; f.fmt = Fmt::Db;
            f.get = [] (const ChannelParameters&) { return 0.0; };
            addKnob (f, [corr, i] (ChannelParameters& p, double v)
            {
                auto& b = corr ? p.correctiveBands[size_t (i)] : p.toneBands[size_t (i)];
                b.gainDb = float (v);
                b.enabled = true;              // moving a band's gain is asking for the band
            });
        }
        const int sel = juce::jlimit (0, s.bands - 1, band);
        {
            Field f;
            f.label = "Frequency"; f.min = 20.0; f.max = 20000.0; f.step = 1.0; f.mid = 630.0; f.fmt = Fmt::Hz;
            f.get = [] (const ChannelParameters&) { return 630.0; };
            addKnob (f, [corr, sel] (ChannelParameters& p, double v)
            {
                (corr ? p.correctiveBands[size_t (sel)] : p.toneBands[size_t (sel)]).freqHz = float (v);
            });
            f.label = "Q"; f.min = 0.1; f.max = 10.0; f.step = 0.01; f.mid = 1.0; f.fmt = Fmt::Q;
            f.get = [] (const ChannelParameters&) { return 1.0; };
            addKnob (f, [corr, sel] (ChannelParameters& p, double v)
            {
                (corr ? p.correctiveBands[size_t (sel)] : p.toneBands[size_t (sel)]).q = float (v);
            });
        }
        {
            Field f;
            f.kind = Field::Kind::Choice;
            f.label = "Shape";
            for (const auto* name : kFilterTypeNames) f.choices.add (name);
            addChoice (f, [corr, sel] (ChannelParameters& p, double v)
            {
                (corr ? p.correctiveBands[size_t (sel)] : p.toneBands[size_t (sel)]).type
                    = FilterType (juce::jlimit (0, int (FilterType::Count) - 1, juce::roundToInt (v)));
            });
            Field on;
            on.kind = Field::Kind::Toggle;
            on.label = "Band " + juce::String (sel + 1);
            on.choices = { "Off", "On" };
            addChoice (on, [corr, sel] (ChannelParameters& p, double v)
            {
                (corr ? p.correctiveBands[size_t (sel)] : p.toneBands[size_t (sel)]).enabled = v > 0.5;
            });
        }
    }
    else
    {
        for (const auto& f : s.fields)
        {
            if (f.kind == Field::Kind::Slider) addKnob (f, [f] (ChannelParameters& p, double v) { f.set (p, v); });
            else                               addChoice (f, [f] (ChannelParameters& p, double v) { f.set (p, v); });
        }
        if (s.id == StageId::Sample)
        {
            // HEAR IT: the chosen sound, once, where solo goes. Nothing about the mix changes.
            auto hear = std::make_unique<DineButton> ("Hear it", DineButton::Style::Standard);
            hear->setCaps (true);
            hear->setFontPx (11.5f);
            hear->setTooltip ("Plays this sound once in your own listen (where solo goes). The broadcast never hears it.");
            hear->onClick = [this] { controller.auditionSample (strip); };
            controlsHolder.addAndMakeVisible (*hear);
            controls.push_back (std::move (hear));

            // ADD A SOUND: this church's own kick, in the list beside the built-in ones, copied
            // into the session folder so handing the session to somebody else hands them the
            // sound it was mixed with.
            auto add = std::make_unique<DineButton> ("Import a sound...", DineButton::Style::Standard);
            add->setFontPx (11.5f);
            add->setTooltip ("Bring in a .wav of your own. It is copied into this session, so the session travels with "
                             "the sound it was mixed with - it is never a link to a file on this Mac.");
            add->onClick = [this]
            {
                const auto& g = controller.getGraph();
                if (strip < 0 || strip >= g.numStrips() || ! onImportSample) return;
                onImportSample (roleFamily (g.strips[size_t (strip)].role));
            };
            controlsHolder.addAndMakeVisible (*add);
            controls.push_back (std::move (add));
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
    bool hasSample = false;
    for (const auto& s : stages) hasSample = hasSample || s.id == StageId::Sample;
    const auto readouts = chainStages (p, isBus && bus == MixBus::Master, stereo, hasSample);

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
            case StageId::Sample:  return proc->getOptions().sampleReplacement && proc->getSampler().isPlaying() ? 6.0f : 0.0f;   // the bar lights while a sample plays
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
            // EFFECTS OFF holds the sends at silence and keeps their levels, so the numbers in
            // this stage are still the right ones and nothing is going through them. Say so,
            // and put the lamp out: a level that is being read but not heard has to look like one.
            if (strip >= 0 && strip < controller.getBase().numStrips
                && controller.getBase().strips[size_t (strip)].effectsOff)
            {
                v.value = "off";
                v.on = false;
                v.why = "The effects are off on this microphone, so nothing is reaching the returns. The levels below "
                        "are kept for when they go back on - one press, here or on the strip.  " + v.why;
            }
        }
        else if (i < readouts.size())
        {
            v.label = readouts[i].label;
            v.value = readouts[i].value;
            // The sample stage says how many hits it has played (and held back as bleed) since the
            // graph was built - in its sentence, where there is room; the chip keeps the blend.
            if (s.id == StageId::Sample && proc != nullptr && proc->getOptions().sampleReplacement && v.on)
            {
                const int hits = proc->getSampler().getHitCount(), held = proc->getSampler().getVetoCount();
                juce::String count = juce::String (hits) + (hits == 1 ? " hit played" : " hits played");
                if (held > 0) count += ", " + juce::String (held) + " held back as another drum's bleed";
                v.why = count + ".  " + v.why;
            }
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

    // WHO SET THIS STAGE, in the design's one line under the title. It is DINE that set it,
    // and a hand edit says so in the same place rather than in a badge somewhere else.
    if (bypassed)               { provenance = "BYPASS is on - nothing in the chain is running."; badgeTint = Dine::warn; }
    else if (edited)            { provenance = "Hand-edited"; badgeTint = Dine::monitor; }
    else if (! stageOn)         { provenance = "Left out of the chain"; badgeTint = Dine::ink4; }
    else if (controller.getPlan() != nullptr)
    {
        provenance = "Set by TUNE MIX";
        if (const auto when = lastTuneClock(); when.isNotEmpty()) provenance += " at " + when;
        badgeTint = Dine::accent;
    }
    else                        { provenance = "The profile's baseline for this source"; badgeTint = Dine::ink4; }

    // The sentence along the foot: what TUNE MIX said about this stage, or what it is for.
    sentence = selected < int (views.size()) ? views[size_t (selected)].why : s.plain;

    const bool switchable = s.setOn != nullptr;
    if (onOffTrack.isVisible() != switchable)
    {
        onOffTrack.setVisible (switchable);
        resized();
    }
    offButton.setToggleState (! stageOn, juce::dontSendNotification);
    onButton.setToggleState (stageOn, juce::dontSendNotification);
    offButton.setEnabled (! bypassed);
    onButton.setEnabled (! bypassed);

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
            if (auto* knob = dynamic_cast<SendKnob*> (c.get()))
                knob->pull (strip >= 0 && strip < base.numStrips ? base.strips[size_t (strip)].sendDb[size_t (knob->slot)] : kSilenceDb,
                           ! bypassed);
    }
    else if (s.bands > 0)
    {
        const int sel = juce::jlimit (0, s.bands - 1, band);
        auto bandOf = [&] (int i) -> const EQBandParams& { return s.corrective ? p.correctiveBands[size_t (i)] : p.toneBands[size_t (i)]; };
        size_t next = 0;
        for (int i = 0; i < s.bands && next < controls.size(); ++i, ++next)
            if (auto* knob = dynamic_cast<Knob*> (controls[next].get())) knob->setValue (bandOf (i).gainDb);
        if (next < controls.size()) if (auto* k = dynamic_cast<Knob*> (controls[next].get())) k->setValue (bandOf (sel).freqHz);
        ++next;
        if (next < controls.size()) if (auto* k = dynamic_cast<Knob*> (controls[next].get())) k->setValue (bandOf (sel).q);
        ++next;
        if (next < controls.size()) if (auto* g = dynamic_cast<ChoiceGroup*> (controls[next].get())) g->setIndex (int (bandOf (sel).type));
        ++next;
        if (next < controls.size()) if (auto* g = dynamic_cast<ChoiceGroup*> (controls[next].get())) g->setIndex (bandOf (sel).enabled ? 1 : 0);
        for (auto& c : controls) c->setEnabled (live);
    }
    else
    {
        size_t next = 0;
        for (const auto& f : s.fields)
        {
            if (next >= controls.size()) break;
            if (auto* knob = dynamic_cast<Knob*> (controls[next].get())) knob->setValue (f.get (p));
            else if (auto* group = dynamic_cast<ChoiceGroup*> (controls[next].get())) group->setIndex (juce::roundToInt (f.get (p)));
            controls[next]->setEnabled (live);
            ++next;
        }
        for (; next < controls.size(); ++next) controls[next]->setEnabled (live);
    }

    // What the graph needs: the live meters, the reduction of this stage and the sends.
    float grDb = 0.0f, inDb = -120.0f, outDb = -120.0f, level = 0.0f;
    bool fired = false;
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
            if (s.id == StageId::Sample)
            {
                const int hits = proc->getSampler().getHitCount();
                fired = hits != sampleHits;
                sampleHits = hits;
            }
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
    graph->update (p, controller.getSampleRate(), grDb, inDb, outDb, level, band, ! bypassed, fired, std::move (sends));
    repaint();
}

// The clock on the newest tune this channel carries, for the provenance line.
juce::String ChainEditor::lastTuneClock() const
{
    if (isBus || strip < 0) return {};
    const auto& records = controller.getStripHistory (strip);
    for (auto it = records.rbegin(); it != records.rend(); ++it)
        if (juce::String (it->what).startsWith ("TUNE") && it->whenMs > 0)
            return juce::Time (juce::int64 (it->whenMs)).formatted ("%l:%M %p").trim();
    return {};
}

// The well the drawing and the controls live in: the design insets it 24 from the card and
// leaves the closing sentence its own band along the foot.
juce::Rectangle<int> ChainEditor::wellBounds() const
{
    return getLocalBounds().reduced (kCardPad, 0)
                           .withTrimmedTop (kHeaderH)
                           .withTrimmedBottom (kSentenceH);
}

void ChainEditor::resized()
{
    auto head = getLocalBounds().reduced (kCardPad, 0).withTrimmedTop (20).withHeight (Dine::Metric::control);
    if (onOffTrack.isVisible())
    {
        const int w = juce::jmax (74, offButton.idealWidth() + onButton.idealWidth() + 8);
        onOffTrack.setBounds (head.removeFromRight (w));
        auto track = onOffTrack.getLocalBounds().reduced (2, 2);
        offButton.setBounds (track.removeFromLeft (track.getWidth() / 2));
        onButton.setBounds (track);
        head.removeFromRight (8);
    }
    if (revertButton != nullptr && revertButton->isVisible())
        revertButton->setBounds (head.removeFromRight (juce::jmax (84, revertButton->idealWidth())));

    const auto well = wellBounds();
    controlsView.setBounds (well);

    const int inner = juce::jmax (120, well.getWidth() - 2 * kWellPad);
    const int knobCellH = Knob::cellHeight();

    // Everything under the drawing, measured first: the drawing takes what is left, down to
    // the height below which it stops saying anything, and then the well scrolls instead.
    std::vector<juce::Component*> knobs, extras, choices;
    for (auto& c : controls)
    {
        // A send is a knob like any other, so it wraps into the same row: one family of cells
        // under the drawing, whatever the stage is.
        if (dynamic_cast<DineKnob*> (c.get()) != nullptr) knobs.push_back (c.get());
        else if (auto* g = dynamic_cast<ChoiceGroup*> (c.get())) (g->isPopup() ? extras : choices).push_back (c.get());
        else extras.push_back (c.get());
    }

    // The knobs wrap at the width the well has, each taking what its own label needs.
    std::vector<std::vector<juce::Component*>> knobLines;
    {
        int x = 0;
        for (auto* c : knobs)
        {
            const int w = static_cast<DineKnob*> (c)->cellWidth();
            if (knobLines.empty() || x + w > inner) { knobLines.emplace_back(); x = 0; }
            knobLines.back().push_back (c);
            x += w;
        }
    }
    const int knobRows = int (knobLines.size());
    int below = knobRows > 0 ? kRowGap + knobRows * knobCellH : 0;
    if (! extras.empty()) below += kRowGap + kExtraH;
    int choiceRows = 0;
    {
        int x = 0;
        for (auto* c : choices)
        {
            const int w = juce::jmin (inner, static_cast<ChoiceGroup*> (c)->idealWidth());
            if (choiceRows == 0 || x + w > inner) { ++choiceRows; x = 0; }
            x += w + 24;
        }
    }
    if (choiceRows > 0) below += kRowGap + choiceRows * kChoiceH + (choiceRows - 1) * 12;

    const int graphH = juce::jmax (kGraphMinH, juce::jmin (kGraphH, well.getHeight() - 2 * kWellPad - below));
    const int content = 2 * kWellPad + graphH + below;
    const bool scrolls = content > well.getHeight();
    controlsHolder.setSize (juce::jmax (120, well.getWidth() - (scrolls ? 10 : 0)),
                            juce::jmax (content, well.getHeight()));

    auto r = controlsHolder.getLocalBounds().reduced (kWellPad, kWellPad);
    const int width = r.getWidth();
    graph->setBounds (r.removeFromTop (graphH));

    if (! knobLines.empty())
    {
        r.removeFromTop (kRowGap);
        for (const auto& row : knobLines)
        {
            auto line = r.removeFromTop (knobCellH);
            for (auto* c : row) c->setBounds (line.removeFromLeft (static_cast<DineKnob*> (c)->cellWidth()));
        }
    }
    if (! extras.empty())
    {
        r.removeFromTop (kRowGap);
        auto line = r.removeFromTop (kExtraH);
        for (auto* c : extras)
        {
            int w = 120;
            if (auto* group = dynamic_cast<ChoiceGroup*> (c))      w = group->idealWidth();
            else if (auto* button = dynamic_cast<DineButton*> (c)) w = juce::jmax (84, button->idealWidth());
            w = juce::jmin (juce::jmax (48, line.getWidth()), w);
            c->setBounds (line.removeFromLeft (w).withHeight (Dine::Metric::control));
            line.removeFromLeft (12);
        }
    }
    if (! choices.empty())
    {
        r.removeFromTop (kRowGap);
        auto line = r.removeFromTop (kChoiceH);
        int x = 0;
        for (auto* c : choices)
        {
            const int w = juce::jmin (width, static_cast<ChoiceGroup*> (c)->idealWidth());
            if (x > 0 && x + w > width) { r.removeFromTop (12); line = r.removeFromTop (kChoiceH); x = 0; }
            c->setBounds (line.getX() + x, line.getY(), w, kChoiceH);
            x += w + 24;
        }
    }
}

void ChainEditor::paint (juce::Graphics& g)
{
    const auto& s = spec();
    Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::card, Dine::Radius::card);
    // The well the whole stage lives in, a plane below the card: the drawing, the knobs and
    // the choices are one thing, and the card's own head and foot are outside it.
    Dine::fillRounded (g, wellBounds().toFloat(), Dine::inset, Dine::Radius::well);
    Dine::hairlineRounded (g, wellBounds().toFloat(), Dine::hairSoft, Dine::Radius::well);

    auto r = getLocalBounds().reduced (kCardPad, 0);
    auto title = r.withTrimmedTop (20).withHeight (22);
    if (onOffTrack.isVisible()) title.removeFromRight (onOffTrack.getWidth() + 8);
    if (revertButton != nullptr && revertButton->isVisible()) title.removeFromRight (revertButton->getWidth() + 8);

    const auto nameFont = Dine::text (17.0f, 600);
    const int nameW = juce::jmin (title.getWidth(), Dine::textWidth (nameFont, s.name));
    g.setColour (stageOn && ! bypassed ? Dine::ink : Dine::ink3);
    g.setFont (nameFont);
    Dine::drawText (g, s.name, title.removeFromLeft (nameW), juce::Justification::centredLeft);
    // The plain word this stage answers to, beside its name: CLEAN-UP, SMOOTH, STEADY, WARMTH.
    if (const auto word = macroWordFor (s.id); word.isNotEmpty() && title.getWidth() > 60)
    {
        title.removeFromLeft (12);
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (11.0f, 0.36f, 600));
        Dine::drawText (g, word, title, juce::Justification::centredLeft, true);
    }

    // Who set it, with a lamp in front: the same sentence the trail tells in its own words.
    auto line = r.withTrimmedTop (52).withHeight (14);
    g.setColour (badgeTint);
    g.fillEllipse (line.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
    line.removeFromLeft (6);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 500));
    Dine::drawText (g, provenance, line, juce::Justification::centredLeft, true);

    auto foot = r.removeFromBottom (kSentenceH).withTrimmedTop (8).withTrimmedBottom (16);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
    Dine::drawFittedText (g, bypassed ? "BYPASS is on - nothing in the chain is running." : sentence,
                      foot, juce::Justification::topLeft, 2, 1.0f);
}

// ------------------------------------------------------------------ SignalPath
SignalPath::SignalPath (ChainEditor& c) : chain (c) {}

// The path follows the editor: whichever stage is open is a chip you can see, so picking one
// from the trail or with the keyboard never leaves it off the end of a scrolled row.
void SignalPath::refresh()
{
    const int i = chain.selectedStage();
    if (i >= 0 && i < chain.numStages() && getWidth() > 0)
    {
        auto chip = chipBounds (i);
        if (chip.getX() < 0)              scrollX += chip.getX() - gap;
        else if (chip.getRight() > getWidth()) scrollX += chip.getRight() - getWidth() + gap;
        clampScroll();
    }
    repaint();
}

namespace
{
    // The chip's name, in the design's sentence case: INPUT -> Input, DE-ESS -> De-ess.
    juce::String chipName (const juce::String& label)
    {
        if (label == "EQ") return label;          // an initialism is not a word to put back into sentence case
        return label.substring (0, 1) + label.substring (1).toLowerCase();
    }

    juce::Font chipFont() { return Dine::text (12.0f, 500); }
}

// A chip is as wide as its name needs and no wider: 10 for the gutter, 5 for the lamp, 6
// between, the name, then 10 at the end - the design's `Stage - *`, to the pixel.
int SignalPath::chipWidth (int index) const
{
    const auto& views = chain.stageViews();
    if (index < 0 || index >= int (views.size())) return 0;
    return 31 + Dine::textWidth (chipFont(), chipName (views[size_t (index)].label));
}

juce::Rectangle<int> SignalPath::chipBounds (int index) const
{
    auto row = getLocalBounds().withHeight (chipH);
    int x = row.getX() - scrollX;
    for (int i = 0; i < index; ++i) x += chipWidth (i) + gap;
    return { x, row.getY(), chipWidth (index), chipH };
}

int SignalPath::contentWidth() const
{
    const int n = chain.numStages();
    if (n <= 0) return 0;
    int w = 0;
    for (int i = 0; i < n; ++i) w += chipWidth (i) + gap;
    return juce::jmax (0, w - gap);
}

int SignalPath::maxScroll() const { return juce::jmax (0, contentWidth() - getWidth()); }
void SignalPath::clampScroll() { scrollX = juce::jlimit (0, maxScroll(), scrollX); }
void SignalPath::resized() { clampScroll(); }

void SignalPath::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (maxScroll() <= 0) return;
    const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    scrollX -= juce::roundToInt (delta * 220.0f);
    clampScroll();
    repaint();
}

int SignalPath::chipAt (juce::Point<int> p) const
{
    for (int i = 0; i < chain.numStages(); ++i)
        if (chipBounds (i).contains (p)) return i;
    return -1;
}

void SignalPath::paint (juce::Graphics& g)
{
    const auto& views = chain.stageViews();
    juce::Graphics::ScopedSaveState clipToRow (g);
    g.reduceClipRegion (getLocalBounds());

    for (int i = 0; i < int (views.size()); ++i)
    {
        const auto& v = views[size_t (i)];
        const bool sel = i == chain.selectedStage();
        auto chip = chipBounds (i);
        if (chip.getRight() < 0 || chip.getX() > getWidth()) continue;

        juce::Colour ground = sel ? Dine::controlOn : Dine::control;
        if (hover == i && ! sel) ground = Dine::controlHot;
        Dine::fillRounded (g, chip.toFloat(), ground, Dine::Radius::chip);

        auto r = chip.reduced (10, 0);
        auto lamp = r.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat();
        // THE LAMP IS A READING, NOT A SWITCH. It says where the setting came from - DINE's,
        // a hand edit - and an empty ring says the stage is out of the chain. Nothing on this
        // row is two controls in one place: the whole chip opens the stage, and Off | On at the
        // top of the card is what switches it.
        if (v.on)
        {
            g.setColour (v.edited ? Dine::monitor : Dine::accent);
            g.fillEllipse (lamp);
        }
        else
        {
            g.setColour (Dine::ink4);
            g.drawEllipse (lamp.reduced (0.5f), 1.2f);
        }
        r.removeFromLeft (6);
        g.setColour (! v.on ? Dine::ink4 : sel ? Dine::ink : Dine::ink2);
        g.setFont (chipFont());
        Dine::drawText (g, chipName (v.label), r, juce::Justification::centredLeft, true);
    }

    // A chip cut off at the edge has to look cut off, or the path reads as if it ended there.
    auto row = getLocalBounds().toFloat();
    auto fade = [&] (bool left)
    {
        auto edge = left ? row.withWidth (26.0f) : row.withTrimmedLeft (row.getWidth() - 26.0f);
        g.setGradientFill (juce::ColourGradient (Dine::window, left ? edge.getX() : edge.getRight(), 0.0f,
                                                 Dine::window.withAlpha (0.0f), left ? edge.getRight() : edge.getX(), 0.0f, false));
        g.fillRect (edge);
    };
    if (scrollX > 0)             fade (true);
    if (scrollX < maxScroll())   fade (false);
}

// A CHIP DOES ONE THING: it opens the stage. It used to switch the stage in or out when the
// click landed in its left 18 pt - a hit zone with nothing drawn under it - so the same
// press selected or toggled depending on a pixel, and picking a stage read as broken. The
// switch lives where it is written in words: Off | On at the top of the card, and the
// stage's own entry on this row's menu.
void SignalPath::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown()) return;
    const int i = chipAt (e.getPosition());
    if (i < 0) return;
    if (e.mods.isPopupMenu()) { chain.selectStage (i); showMenu (i); return; }
    chain.selectStage (i);
}

// The one shortcut the lamp used to be, said in words instead.
void SignalPath::showMenu (int index)
{
    const auto& views = chain.stageViews();
    if (index < 0 || index >= int (views.size())) return;
    const auto& v = views[size_t (index)];
    if (! v.switchable) return;
    juce::PopupMenu m;
    m.addSectionHeader (chipName (v.label));
    m.addItem (1, v.on ? "Switch it off" : "Switch it on");
    m.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (this)
                         .withTargetScreenArea (localAreaToGlobal (chipBounds (index))),
                     [this, index] (int r) { if (r == 1) chain.toggleStage (index); });
}

void SignalPath::mouseMove (const juce::MouseEvent& e)
{
    const int i = chipAt (e.getPosition());
    if (i == hover) return;
    hover = i;
    setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    // What this stage is for, or why TUNE MIX set it the way it is, without opening it.
    const auto& views = chain.stageViews();
    if (i >= 0 && i < int (views.size()))
    {
        const auto& v = views[size_t (i)];
        auto tip = v.why.isNotEmpty() ? v.why : chipName (v.label);
        if (! v.on) tip = chipName (v.label) + " is out of the chain. " + tip;
        setTooltip (tip);
    }
    else setTooltip ({});
    repaint();
}

void SignalPath::mouseExit (const juce::MouseEvent&)
{
    if (hover < 0) return;
    hover = -1;
    repaint();
}

} // namespace livemix
