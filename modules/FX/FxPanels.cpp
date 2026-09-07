#include "FxPanels.h"
#include "FX/FxParameterSpecs.h"
#include "FX/FxProfiles.h"
#include "State/ParameterIDs.h"
#include <cmath>

namespace livemix
{

using LNF = LiveMixLookAndFeel;

namespace
{
    constexpr int kRailWidth = 168, kPadX = 22, kPadY = 16, kGap = 14, kSideWidth = 96;

    juce::String ms (float v) { return LNF::formatValue (v, "ms", 0.0f, 2000.0f); }
    juce::String db (float v) { return LNF::formatValue (v, "dB", -40.0f, 12.0f); }
    juce::String pct (float v) { return LNF::formatValue (v, "%", 0.0f, 100.0f); }
    juce::String seconds (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " s"; }
    juce::String division (int d) { return juce::String (kNoteDivisionNames[size_t (juce::jlimit (0, int (NoteDivision::Count) - 1, d))]); }

    juce::String delaySummary (const FxLiveState& live)
    {
        const auto& p = live.params;
        if (! p.delayEnabled) return "off";
        juce::String s = p.delaySync ? division (p.delayDivision) + " " + Glyph::dot() + " " + ms (live.delayMsL) : ms (live.delayMsL);
        s += " " + Glyph::dot() + " fb " + pct (p.delayFeedback);
        if (p.delayDuck > 0.0f) s += " " + Glyph::dot() + " duck";
        return s;
    }

    juce::String reverbSummary (const FxParameters& p)
    {
        if (! p.reverbEnabled) return "off";
        return seconds (p.reverbDecayS) + " " + Glyph::dot() + " pre " + ms (p.reverbPreDelayMs) + " " + Glyph::dot() + " size " + pct (p.reverbSize);
    }
}

const char* fxModuleName (FxModule m) noexcept
{
    switch (m)
    {
        case FxModule::Input:  return "INPUT";
        case FxModule::Delay:  return "DELAY";
        case FxModule::Reverb: return "REVERB";
        case FxModule::Output: return "OUTPUT";
        case FxModule::Count:
        default:               return "";
    }
}

bool fxModuleEnabled (FxModule m, const FxParameters& p) noexcept
{
    switch (m)
    {
        case FxModule::Delay:  return p.delayEnabled;
        case FxModule::Reverb: return p.reverbEnabled;
        case FxModule::Input:
        case FxModule::Output: return true;
        case FxModule::Count:
        default:               return false;
    }
}

juce::String fxModuleSummary (FxModule m, const FxLiveState& live)
{
    const auto& p = live.params;
    switch (m)
    {
        case FxModule::Input:  return LNF::formatValue (p.inputTrimDb, "dB", -24.0f, 24.0f);
        case FxModule::Delay:  return delaySummary (live);
        case FxModule::Reverb: return reverbSummary (p);
        case FxModule::Output: return "mix " + LNF::formatValue (p.mix, "", 0.0f, 1.0f) + " " + Glyph::dot() + " " + LNF::formatValue (p.outputTrimDb, "dB", -24.0f, 24.0f);
        case FxModule::Count:
        default:               return {};
    }
}

// ---------------------------------------------------------------------------
void FxChainStrip::Item::set (const juce::String& s, bool en, float act, juce::Colour col)
{
    const bool changed = summary != s || enabled != en || std::abs (activity - act) > 0.01f;
    summary = s; enabled = en; activity = act; activityColour = col;
    if (changed) repaint();
}

void FxChainStrip::Item::paintButton (juce::Graphics& g, bool over, bool)
{
    using namespace Tokens;
    auto b = getLocalBounds().toFloat();
    LNF::drawSurface (g, b, over ? juce::Colour (0xff1a1f24) : raised, over ? hairHover : hair2, Tokens::Radius::control);
    auto inner = b.reduced (9.0f, 0.0f).withTrimmedTop (8.0f).withTrimmedBottom (7.0f);
    auto row = inner.removeFromTop (13.0f);
    g.setColour (enabled ? ok : mark);
    g.fillEllipse (row.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
    row.removeFromLeft (5.0f);
    g.setColour (textHi);
    g.setFont (LNF::body (11.0f, 600));
    g.drawText (getButtonText(), row, juce::Justification::centredLeft);
    inner.removeFromTop (3.0f);
    g.setColour (textMid);
    g.setFont (LNF::mono (10.0f, 400));
    g.drawText (summary, inner.removeFromTop (12.0f), juce::Justification::centredLeft, true);
    auto strip = juce::Rectangle<float> (inner.getX(), b.getBottom() - 10.0f, inner.getWidth(), 3.0f);
    g.setColour (track);
    g.fillRoundedRectangle (strip, 1.5f);
    g.setColour (activityColour);
    g.fillRoundedRectangle (strip.withWidth (strip.getWidth() * juce::jlimit (0.0f, 1.0f, activity)), 1.5f);
}

FxChainStrip::FxChainStrip()
{
    for (auto& it : items)
    {
        addAndMakeVisible (it);
        it.onClick = [this, &it] { if (onModuleClicked) onModuleClicked (it.module); };
        it.setTooltip (juce::String ("Open ") + fxModuleName (it.module) + " in Advanced.");
    }
}

void FxChainStrip::update (const FxLiveState& live)
{
    for (auto& it : items)
    {
        float act = live.activity;
        juce::Colour col = Tokens::accentStroke;
        if (it.module == FxModule::Delay) { act = live.params.delayEnabled ? juce::jlimit (0.0f, 1.0f, 1.0f + live.duckDb / 30.0f) * live.activity : 0.0f; if (live.duckDb < -1.0f) col = Tokens::warn; }
        else if (it.module == FxModule::Reverb) act = live.params.reverbEnabled ? live.activity : 0.0f;
        else if (it.module == FxModule::Output) act = MeterComponent::normFor (live.outPeakDb);
        it.set (fxModuleSummary (it.module, live), fxModuleEnabled (it.module, live.params), act, col);
    }
}

void FxChainStrip::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    g.setColour (subBar);
    g.fillRect (b);
    g.setColour (hair);
    g.fillRect (b.removeFromTop (1));
    g.saveState();
    g.setColour (textLow);
    g.setFont (LNF::body (10.0f, 600));
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 22.0f, float (getHeight()) * 0.5f));
    g.drawText ("CHAIN", juce::Rectangle<int> (22 - 40, getHeight() / 2 - 7, 80, 14), juce::Justification::centred);
    g.restoreState();
    for (int i = 0; i + 1 < int (FxModule::Count); ++i)
    {
        const auto r = items[i].getBounds();
        LNF::drawIcon (g, LNF::Icon::ArrowRight, juce::Rectangle<float> (float (r.getRight()), float (r.getCentreY()) - 5.0f, 12.0f, 10.0f), mark);
    }
}

void FxChainStrip::resized()
{
    auto b = getLocalBounds().reduced (16, 0).withTrimmedLeft (18);
    const int n = int (FxModule::Count), gap = 12;
    const int w = (b.getWidth() - gap * (n - 1)) / n;
    auto row = b.withSizeKeepingCentre (b.getWidth(), 64);
    for (int i = 0; i < n; ++i) { items[i].setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
}

// ---------------------------------------------------------------------------
FxSimplePanel::FxSimplePanel (juce::AudioProcessorValueTreeState& apvts)
    : space (apvts, FxParamID::space, "SPACE"), length (apvts, FxParamID::length, "LENGTH"), warmth (apvts, FxParamID::warmth, "WARMTH"),
      clarity (apvts, FxParamID::clarity, "CLARITY"), distance (apvts, FxParamID::distance, "DISTANCE"), mix (apvts, FxParamID::mix, "MIX")
{
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    inMeter.setTooltip ("Level coming into Dine FX. Click to clear the red clip light.");
    outMeter.setTooltip ("Level going out. Click to clear the red clip light.");
    for (auto* k : { &space, &length, &warmth, &clarity, &distance, &mix }) addAndMakeVisible (*k);
    space.getSlider().setTooltip ("How big the room feels. Down = small and close, up = a big hall. 50 is the Dine starting point for this type.");
    length.getSlider().setTooltip ("How long the effect hangs on after each note. Down = short and tidy, up = long and lush.");
    warmth.getSlider().setTooltip ("Dark to bright. Down = warmer and softer, up = brighter and more sparkly.");
    clarity.getSlider().setTooltip ("Keeps the words clear. Up = the effect steps aside while someone sings, down = it blends in more.");
    distance.getSlider().setTooltip ("How far away the sound seems. Down = right in front of you, up = further back in the room.");
    mix.getSlider().setTooltip ("How much effect you hear. Leave at 100 % on an effects (send) channel; lower it if Dine FX sits directly on a vocal or instrument channel.");
    addAndMakeVisible (chain);
    chain.onModuleClicked = [this] (FxModule m) { if (onOpenModule) onOpenModule (m); };
}

void FxSimplePanel::update (const FxLiveState& live, const Status& s)
{
    inMeter.setLevels (live.inPeakDb, live.inHoldDb, live.inClipped);
    outMeter.setLevels (live.outPeakDb, live.outHoldDb, live.outClipped);
    chain.update (live);
    last = live;

    const juce::String newEngine = (live.params.reverbEnabled ? "REVERB " + reverbSummary (live.params) : juce::String())
                                 + (live.params.reverbEnabled && live.params.delayEnabled ? "   " : "")
                                 + (live.params.delayEnabled ? "DELAY " + delaySummary (live) : juce::String());
    const juce::String newIn = LNF::formatValue (live.inHoldDb, "dB", -60.0f, 0.0f), newOut = LNF::formatValue (live.outHoldDb, "dB", -60.0f, 0.0f);
    bool changed = s.liveSafe != status.liveSafe || s.type != status.type || s.style != status.style || hint.isEmpty();
    if (newEngine != engineLine || newIn != inText || newOut != outText) { engineLine = newEngine; inText = newIn; outText = newOut; repaint(); }
    status = s;
    if (! changed) return;
    hint = juce::String (styleProfileName (s.style)).toUpperCase() + " " + Glyph::dot() + " " + juce::String (juce::CharPointer_UTF8 (FxProfiles::intent (s.type)));
    for (auto* k : { &space, &length, &warmth, &clarity, &distance }) k->getSlider().setEnabled (true);
    resized();
    repaint();
}

void FxSimplePanel::paint (juce::Graphics& g)
{
    using namespace Tokens;
    g.setColour (window);
    g.fillRect (getLocalBounds());
    auto b = getLocalBounds().withTrimmedBottom (FxChainStrip::kHeight);

    auto side = [&] (juce::Rectangle<int> area, const juce::String& caption, const juce::String& value, MeterComponent& meter, bool left)
    {
        g.setColour (panel);
        g.fillRect (area);
        g.setColour (hair);
        g.fillRect (left ? area.removeFromRight (1) : area.removeFromLeft (1));
        auto col = area.reduced (16, 18);
        g.setColour (textLow);
        g.setFont (LNF::body (10.0f, 600));
        g.drawText (caption, col.removeFromTop (12), juce::Justification::centred);
        g.setColour (textMid);
        g.setFont (LNF::mono (12.0f, 500));
        g.drawText (value, col.removeFromBottom (14), juce::Justification::centred);
        // dB scale beside the meter
        auto scale = juce::Rectangle<int> (meter.getRight() + 4, meter.getY(), 30, meter.getHeight());
        g.setColour (textGrid);
        g.setFont (LNF::mono (8.5f, 400));
        for (int dbv : { 0, -12, -24, -36, -48 })
        {
            const int y = meter.getY() + int (MeterComponent::yFor (float (dbv), float (meter.getHeight())));
            g.drawText (juce::String (dbv), scale.getX(), y - 6, scale.getWidth(), 12, juce::Justification::centredLeft);
        }
    };
    side (b.removeFromLeft (kSideWidth), "INPUT", inText, inMeter, true);
    side (b.removeFromRight (kSideWidth), "OUTPUT", outText, outMeter, false);

    const int knobTop = space.getY();
    g.setColour (textLow);
    g.setFont (LNF::body (11.5f, 400, 0.04f));
    g.drawText (hint, b.getX(), knobTop - 26, b.getWidth(), 16, juce::Justification::centred);

    g.setColour (textMid);
    g.setFont (LNF::mono (10.5f, 400));
    g.drawFittedText (engineLine, b.getX() + 20, space.getBottom() + 18, b.getWidth() - 40, 16, juce::Justification::centred, 1, 1.0f);

    if (status.liveSafe)
    {
        g.setColour (ok);
        g.setFont (LNF::body (10.5f, 400));
        g.drawText (juce::String ("Live Safe is on ") + Glyph::dash() + " type, profile and presets are locked; the knobs stay usable.",
                    b.getX(), space.getBottom() + 40, b.getWidth(), 14, juce::Justification::centred);
    }
}

void FxSimplePanel::resized()
{
    auto b = getLocalBounds();
    chain.setBounds (b.removeFromBottom (FxChainStrip::kHeight));
    auto leftSide = b.removeFromLeft (kSideWidth).reduced (16, 18);
    leftSide.removeFromTop (18); leftSide.removeFromBottom (20);
    inMeter.setBounds (leftSide.removeFromLeft (22).withX (leftSide.getX() + 8));
    auto rightSide = b.removeFromRight (kSideWidth).reduced (16, 18);
    rightSide.removeFromTop (18); rightSide.removeFromBottom (20);
    outMeter.setBounds (rightSide.removeFromLeft (22).withX (rightSide.getX() + 8));

    const int blockH = 26 + MacroKnob::kHeight + 18 + 16 + 30;
    auto block = b.withSizeKeepingCentre (b.getWidth(), blockH);
    block.removeFromTop (26);
    auto knobs = block.removeFromTop (MacroKnob::kHeight);
    const int gap = 14, kw = MacroKnob::kWidth, n = 6;
    auto row = knobs.withSizeKeepingCentre (juce::jmin (knobs.getWidth(), kw * n + gap * (n - 1)), MacroKnob::kHeight);
    const int w = (row.getWidth() - gap * (n - 1)) / n;
    for (auto* k : { &space, &length, &warmth, &clarity, &distance, &mix }) { k->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (kw, MacroKnob::kHeight)); row.removeFromLeft (gap); }
}

// ---------------------------------------------------------------------------
class FxAdvancedPanel::RailButton : public juce::Button
{
public:
    explicit RailButton (FxModule m) : juce::Button (fxModuleName (m)), module (m) {}
    FxModule module;
    void set (const juce::String& s, bool en) { if (summary != s || enabled != en) { summary = s; enabled = en; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        const bool on = getToggleState();
        LNF::drawSurface (g, b, on ? accent.withAlpha (0.12f) : juce::Colours::transparentBlack,
                          on ? accent : over ? hairHover : hairRow, Tokens::Radius::control);
        auto inner = b.reduced (10.0f, 8.0f);
        auto row = inner.removeFromTop (13.0f);
        g.setColour (enabled ? ok : mark);
        g.fillEllipse (row.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
        row.removeFromLeft (6.0f);
        g.setColour (on ? textHi : textMid);
        g.setFont (LNF::body (11.0f, 600));
        g.drawText (getButtonText(), row, juce::Justification::centredLeft);
        inner.removeFromTop (2.0f);
        g.setColour (textLow);
        g.setFont (LNF::mono (9.5f, 400));
        g.drawText (summary, inner.removeFromTop (12.0f), juce::Justification::centredLeft, true);
    }
private:
    juce::String summary;
    bool enabled = true;
};

// Live visual: reverb = pre-delay gap, early reflections and the decay envelope on a dB/time
// grid; delay = the repeat pattern per side with feedback heights and the ducking meter.
class FxAdvancedPanel::Visual : public juce::Component
{
public:
    void set (FxModule m, const FxLiveState& l) { module = m; live = l; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        LNF::drawSurface (g, b, inset, hair, Tokens::Radius::control);
        auto plot = b.reduced (44.0f, 18.0f).withTrimmedBottom (14.0f);
        const auto& p = live.params;

        const float span = module == FxModule::Reverb ? juce::jmax (1.5f, p.reverbDecayS * 1.25f + p.reverbPreDelayMs * 0.001f)
                                                      : juce::jmax (1.2f, repeatsSpan());
        auto xFor = [&] (float t) { return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, t / span); };
        auto yFor = [&] (float dbv) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -dbv / 60.0f); };

        // Grid
        g.setFont (LNF::mono (8.5f, 400));
        for (int dbv : { 0, -12, -24, -36, -48, -60 })
        {
            const float y = yFor (float (dbv));
            g.setColour (grid);
            g.drawHorizontalLine (int (y), plot.getX(), plot.getRight());
            g.setColour (textGrid);
            g.drawText (juce::String (dbv), int (b.getX()) + 6, int (y) - 6, 32, 12, juce::Justification::centredRight);
        }
        const float step = span > 6.0f ? 2.0f : span > 3.0f ? 1.0f : span > 1.5f ? 0.5f : 0.25f;
        for (float t = 0.0f; t <= span + 1e-3f; t += step)
        {
            const float x = xFor (t);
            g.setColour (grid);
            g.drawVerticalLine (int (x), plot.getY(), plot.getBottom());
            g.setColour (textGrid);
            g.drawText (t < 1.0f ? juce::String (int (t * 1000.0f)) + " ms" : juce::String (t, step < 1.0f ? 1 : 0) + " s", int (x) - 24, int (plot.getBottom()) + 3, 48, 12, juce::Justification::centred);
        }

        if (module == FxModule::Reverb) paintReverb (g, plot, xFor, yFor);
        else paintDelay (g, plot, xFor, yFor);
    }

private:
    float repeatsSpan() const
    {
        const auto& p = live.params;
        const float fb = juce::jlimit (0.0f, 0.95f, p.delayFeedback * 0.01f);
        const float repeats = fb <= 0.01f ? 2.0f : juce::jmin (12.0f, std::log (0.001f) / std::log (fb) + 1.0f);
        return repeats * juce::jmax (live.delayMsL, live.delayMsR) * 0.001f;
    }

    template <typename X, typename Y>
    void paintReverb (juce::Graphics& g, juce::Rectangle<float> plot, X xFor, Y yFor)
    {
        using namespace Tokens;
        const auto& p = live.params;
        if (! p.reverbEnabled)
        {
            g.setColour (textLow); g.setFont (LNF::body (12.0f, 600));
            g.drawText ("REVERB OFF", plot, juce::Justification::centred);
            return;
        }
        const float pre = p.reverbPreDelayMs * 0.001f;
        const float sizeScale = (0.35f + 0.0125f * p.reverbSize) / (0.35f + 0.0125f * 50.0f);
        // Early reflections
        const float earlyMs[6] { 7.9f, 13.7f, 21.3f, 29.1f, 37.7f, 44.3f };
        const float earlyGain[6] { 0.90f, 0.70f, 0.55f, 0.45f, 0.35f, 0.25f };
        g.setColour (accentText.withAlpha (0.8f));
        for (int k = 0; k < 6; ++k)
        {
            const float lvl = 20.0f * std::log10 (juce::jmax (1e-4f, earlyGain[k] * 0.5f * p.reverbEarly * 0.01f));
            const float x = xFor (pre + earlyMs[k] * sizeScale * 0.001f);
            g.drawLine (x, yFor (lvl), x, plot.getBottom(), 1.5f);
        }
        // Decay envelope: 0 dB at the end of pre-delay, -60 dB at pre + decay (damping darkens, drawn as a second, faster line)
        juce::Path env;
        env.startNewSubPath (xFor (pre), yFor (0.0f));
        env.lineTo (xFor (pre + p.reverbDecayS), yFor (-60.0f));
        g.setColour (accentStroke);
        g.strokePath (env, juce::PathStrokeType (2.0f));
        const float hfDecay = p.reverbDecayS * juce::jlimit (0.15f, 1.0f, 1.0f - 0.85f * p.reverbDamping * 0.01f);
        juce::Path hf;
        hf.startNewSubPath (xFor (pre), yFor (0.0f));
        hf.lineTo (xFor (pre + hfDecay), yFor (-60.0f));
        g.setColour (warn.withAlpha (0.7f));
        g.strokePath (hf, juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        if (pre > 0.0f)
        {
            g.setColour (textLow.withAlpha (0.6f));
            g.drawVerticalLine (int (xFor (pre)), plot.getY(), plot.getBottom());
        }
        g.setColour (textMid);
        g.setFont (LNF::mono (10.0f, 500));
        g.drawText ("RT60 " + juce::String (p.reverbDecayS, 1) + " s   PRE " + juce::String (int (p.reverbPreDelayMs)) + " ms   HF " + juce::String (hfDecay, 1) + " s",
                    plot.reduced (8.0f, 4.0f), juce::Justification::topRight);
    }

    template <typename X, typename Y>
    void paintDelay (juce::Graphics& g, juce::Rectangle<float> plot, X xFor, Y yFor)
    {
        using namespace Tokens;
        const auto& p = live.params;
        if (! p.delayEnabled)
        {
            g.setColour (textLow); g.setFont (LNF::body (12.0f, 600));
            g.drawText ("DELAY OFF", plot, juce::Justification::centred);
            return;
        }
        const float fb = juce::jlimit (0.0f, 0.95f, p.delayFeedback * 0.01f);
        const float tL = live.delayMsL * 0.001f, tR = live.delayMsR * 0.001f;
        const auto mode = DelayMode (p.delayMode);
        auto bar = [&] (float t, float gain, bool left)
        {
            if (gain < 0.001f) return;
            const float x = xFor (t), y = yFor (20.0f * std::log10 (gain));
            g.setColour (left ? accentStroke : accentText.withAlpha (0.75f));
            g.fillRect (x - 2.0f + (left ? -1.5f : 1.5f), y, 3.0f, plot.getBottom() - y);
        };
        for (int k = 1; k <= 24; ++k)
        {
            if (mode == DelayMode::PingPong)
            {
                // L at tL, R at tL + tR, L at 2tL + tR (x fb), ...
                const int pair = (k - 1) / 2;
                const bool left = (k % 2) == 1;
                const float t = left ? float (pair + 1) * tL + float (pair) * tR : float (pair + 1) * (tL + tR);
                const float gain = std::pow (fb, float (pair));
                bar (t, gain, left);
            }
            else
            {
                bar (float (k) * tL, std::pow (fb, float (k - 1)), true);
                if (mode == DelayMode::Stereo) bar (float (k) * tR, std::pow (fb, float (k - 1)), false);
            }
        }
        // Ducking meter
        auto duck = plot.removeFromTop (14.0f).reduced (8.0f, 2.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, -live.duckDb / 30.0f);
        g.setColour (track);
        g.fillRect (duck.withWidth (120.0f));
        g.setColour (warn);
        g.fillRect (duck.withWidth (120.0f * frac));
        g.setColour (textMid);
        g.setFont (LNF::mono (10.0f, 500));
        juce::String tempoText = (live.hostTempo ? "" : "no host tempo " + Glyph::dot() + " ") + juce::String (live.tempo, 1) + " BPM";
        g.drawText ("DUCK " + juce::String (int (-live.duckDb)) + " dB", duck.withX (duck.getX() + 126.0f).withWidth (110.0f), juce::Justification::centredLeft);
        g.drawText (tempoText, duck, juce::Justification::centredRight);
    }

    FxModule module = FxModule::Reverb;
    FxLiveState live;
};

FxAdvancedPanel::FxAdvancedPanel (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    for (int i = 0; i < int (FxModule::Count); ++i)
    {
        auto rb = std::make_unique<RailButton> (FxModule (i));
        rb->onClick = [this, i] { setModule (FxModule (i)); };
        addAndMakeVisible (*rb);
        rail.push_back (std::move (rb));
    }
    visual = std::make_unique<Visual>();
    addChildComponent (*visual);
    inMeter = std::make_unique<MeterComponent>();
    outMeter = std::make_unique<MeterComponent>();
    addChildComponent (*inMeter);
    addChildComponent (*outMeter);
    setModule (FxModule::Reverb);
}

FxAdvancedPanel::~FxAdvancedPanel() = default;

void FxAdvancedPanel::setModule (FxModule m)
{
    module = m;
    for (auto& rb : rail) rb->setToggleState (rb->module == m, juce::dontSendNotification);
    title = fxModuleName (m);
    rebuildTiles();
    visual->setVisible (m == FxModule::Reverb || m == FxModule::Delay);
    inMeter->setVisible (m == FxModule::Input);
    outMeter->setVisible (m == FxModule::Output);
    resized();
    repaint();
}

void FxAdvancedPanel::rebuildTiles()
{
    tiles.clear();
    std::vector<const char*> ids;
    switch (module)
    {
        case FxModule::Input:  ids = { FxParamID::inputTrim }; break;
        case FxModule::Delay:  ids = { FxParamID::dlOn, FxParamID::dlMode, FxParamID::dlSync, FxParamID::dlDivision, FxParamID::dlTime, FxParamID::dlOffset,
                                       FxParamID::dlFeedback, FxParamID::dlLowCut, FxParamID::dlHighCut, FxParamID::dlWidth, FxParamID::dlDuck, FxParamID::dlDuckRelease,
                                       FxParamID::dlModRate, FxParamID::dlModDepth, FxParamID::dlToReverb, FxParamID::dlLevel }; break;
        case FxModule::Reverb: ids = { FxParamID::rvOn, FxParamID::rvDecay, FxParamID::rvPreDelay, FxParamID::rvSize, FxParamID::rvDamping, FxParamID::rvDiffusion,
                                       FxParamID::rvLowCut, FxParamID::rvHighCut, FxParamID::rvModRate, FxParamID::rvModDepth, FxParamID::rvEarly, FxParamID::rvLevel }; break;
        case FxModule::Output: ids = { FxParamID::mix, FxParamID::outputTrim }; break;
        case FxModule::Count:
        default: break;
    }
    for (auto* id : ids)
    {
        const ParameterSpec* spec = findFxParameterSpec (id);
        auto* param = apvts.getParameter (id);
        if (spec == nullptr || param == nullptr) continue;
        juce::String label (spec->name);
        for (auto* prefix : { "Reverb ", "Delay " })
            if (label.startsWith (prefix)) label = label.substring (juce::String (prefix).length());
        if (label == "Reverb") label = "REVERB";
        if (label == "Delay") label = "DELAY";
        auto tile = std::make_unique<ParamTile> (*param, *spec, label.toUpperCase());
        addAndMakeVisible (*tile);
        tiles.push_back (std::move (tile));
    }
}

void FxAdvancedPanel::update (const FxLiveState& live)
{
    last = live;
    for (auto& rb : rail) rb->set (fxModuleSummary (rb->module, live), fxModuleEnabled (rb->module, live.params));
    if (visual->isVisible()) visual->set (module, live);
    inMeter->setLevels (live.inPeakDb, live.inHoldDb, live.inClipped);
    outMeter->setLevels (live.outPeakDb, live.outHoldDb, live.outClipped);
}

void FxAdvancedPanel::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    g.setColour (window);
    g.fillRect (b);
    auto railArea = b.removeFromLeft (kRailWidth);
    g.setColour (panel);
    g.fillRect (railArea);
    g.setColour (hair);
    g.fillRect (railArea.removeFromRight (1));
    g.setColour (textDim);
    g.setFont (LNF::body (10.0f, 400));
    g.drawFittedText (juce::String ("Same parameters as Simple ") + Glyph::dash() + " switching modes never changes the sound.",
                      juce::Rectangle<int> (12, getHeight() - 52, kRailWidth - 24, 42), juce::Justification::bottomLeft, 3, 1.0f);

    auto content = b.reduced (kPadX, kPadY);
    auto head = content.removeFromTop (18);
    g.setColour (textHi);
    g.setFont (LNF::body (14.0f, 600));
    const int tw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 4;
    g.drawText (title, head.removeFromLeft (tw), juce::Justification::centredLeft);
    head.removeFromLeft (8);
    g.setColour (textLow);
    g.setFont (LNF::mono (10.5f, 400));
    g.drawText (subtitle, head, juce::Justification::bottomLeft);

    auto note = [&] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (textLow);
        g.setFont (LNF::body (11.0f, 400));
        g.drawFittedText (text, area, juce::Justification::centredLeft, 5, 1.0f);
    };
    if (module == FxModule::Input)
        note ("Trim sets the level into the delay and reverb engines. Send level from the console decides how much of each channel reaches this aux; keep the input peaking around -18 dBFS.",
              juce::Rectangle<int> (inMeter->getRight() + 14, inMeter->getY(), juce::jmin (440, getWidth() - inMeter->getRight() - 14 - kPadX), 64));
    else if (module == FxModule::Output)
        note (juce::String ("Mix is the dry/wet balance: 100 % on an aux/send, lower on an insert. Output trim sets the level handed back to the host. ")
              + "A/B ORIGINAL plays the dry signal, loudness-matched" + (std::abs (last.matchDb) > 0.05f ? " (" + LNF::formatValue (last.matchDb, "dB", -12.0f, 12.0f) + ")" : juce::String()) + ".",
              juce::Rectangle<int> (outMeter->getRight() + 14, outMeter->getY(), juce::jmin (440, getWidth() - outMeter->getRight() - 14 - kPadX), 64));
}

void FxAdvancedPanel::resized()
{
    auto b = getLocalBounds();
    auto railArea = b.removeFromLeft (kRailWidth).reduced (8, 10);
    for (auto& rb : rail) { rb->setBounds (railArea.removeFromTop (46)); railArea.removeFromTop (4); }

    auto content = b.reduced (kPadX, kPadY);
    content.removeFromTop (18 + kGap);
    const int perRow = juce::jmax (1, (content.getWidth() + 8) / (ParamTile::kWidth + 8));
    const int rows = tiles.empty() ? 0 : (int (tiles.size()) + perRow - 1) / perRow;
    auto tileArea = content.removeFromBottom (rows * ParamTile::kHeight + juce::jmax (0, rows - 1) * 8);
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const int r = int (i) / perRow, c = int (i) % perRow;
        tiles[i]->setBounds (tileArea.getX() + c * (ParamTile::kWidth + 8), tileArea.getY() + r * (ParamTile::kHeight + 8), ParamTile::kWidth, ParamTile::kHeight);
    }
    if (rows > 0) content.removeFromBottom (kGap);
    visual->setBounds (content);
    inMeter->setBounds (content.removeFromLeft (44));
    outMeter->setBounds (inMeter->getBounds());
}

} // namespace livemix
