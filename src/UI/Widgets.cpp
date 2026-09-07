#include "Widgets.h"
#include "Recommendations/Recommendation.h"
#include <map>

namespace livemix
{

using LNF = LiveMixLookAndFeel;

// ---------------------------------------------------------------------------
FlatButton::FlatButton (const juce::String& text, Style s) : juce::Button (text), style (s)
{
    switch (style)
    {
        case Style::Solid:   fontPx = 13.0f; padX = 16; break;
        case Style::Tab:     fontPx = 12.5f; padX = 11; break;
        case Style::Segment: fontPx = 12.0f; padX = 10; break;
        case Style::Module:  fontPx = 13.0f; padX = 10; chevron = true; break;
        case Style::Accent:  fontPx = 12.0f; padX = 12; break;
        case Style::Ghost:   fontPx = 12.0f; padX = 12; break;
        case Style::Outline: default: break;
    }
}

void FlatButton::setCustomColours (std::optional<juce::Colour> fg, std::optional<juce::Colour> border, std::optional<juce::Colour> bg)
{
    customFg = fg; customBorder = border; customBg = bg;
    repaint();
}

int FlatButton::getIdealWidth() const
{
    const float w = juce::GlyphArrangement::getStringWidth (LNF::body (fontPx, 600, spacingEm), getButtonText());
    return int (w) + padX * 2 + (hasDot ? 14 : 0) + (chevron ? 14 : 0) + (trailingIcon ? 16 : 0) + 2;
}

void FlatButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    using namespace Tokens;
    auto b = getLocalBounds().toFloat();
    const bool on = getToggleState(), enabled = isEnabled();
    juce::Colour fg = textMid, border = hair2, bg = juce::Colours::transparentBlack;
    const float radius = (style == Style::Tab) ? 0.0f : Radius::control;

    switch (style)
    {
        case Style::Outline:
            fg = on ? accentText : textMid; border = on ? accentDim : (over ? hairHover : hair2);
            bg = on ? accent.withAlpha (0.08f) : juce::Colours::transparentBlack;
            if (over && ! on) fg = textHi;
            break;
        case Style::Solid:
            fg = solidText; bg = over ? accentHover : accentSolid; border = bg;
            if (! enabled) { fg = textLow; bg = disabledBg; border = hair2; }
            break;
        case Style::Ghost:
            fg = over ? textMid : textLow; border = juce::Colours::transparentBlack;
            break;
        case Style::Accent:
            fg = accentText; border = over ? accent : hair2; bg = over ? accent.withAlpha (0.12f) : juce::Colours::transparentBlack;
            break;
        case Style::Tab:
            fg = on ? textHi : (over ? textHi : textLow); border = juce::Colours::transparentBlack;
            break;
        case Style::Segment:
            fg = on ? textHi : textLow; bg = on ? accent.withAlpha (0.2f) : juce::Colours::transparentBlack; border = juce::Colours::transparentBlack;
            break;
        case Style::Module:
            fg = accentText; border = over ? accent : accentDim; bg = accent.withAlpha (0.1f);
            break;
    }
    if (customFg) fg = *customFg;
    if (customBorder) border = *customBorder;
    if (customBg) bg = *customBg;
    if (! enabled && style != Style::Solid) { fg = fg.withAlpha (0.45f); border = border.withAlpha (0.6f); }
    if (down && enabled) bg = bg.getAlpha() > 0.0f ? bg.darker (0.15f) : textHi.withAlpha (0.05f);

    if (bg.getAlpha() > 0.0f || border.getAlpha() > 0.0f)
        LNF::drawSurface (g, b, bg, border.getAlpha() > 0.0f ? border : juce::Colours::transparentBlack, radius);
    if (style == Style::Tab && on)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (b.getX() + 4.0f, b.getBottom() - 2.0f, b.getWidth() - 8.0f, 2.0f, 1.0f);
    }

    auto inner = b.reduced (float (padX), 0.0f);
    if (hasDot)
    {
        auto d = inner.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (enabled ? dotColour : dotColour.withAlpha (0.5f));
        g.fillEllipse (d);
        inner.removeFromLeft (7.0f);
    }
    if (chevron)
    {
        auto c = inner.removeFromRight (8.0f).withSizeKeepingCentre (8.0f, 8.0f);
        LNF::drawIcon (g, LNF::Icon::ChevronDown, c, fg);
        inner.removeFromRight (4.0f);
    }
    if (trailingIcon)
    {
        auto ib = inner.removeFromRight (11.0f).withSizeKeepingCentre (11.0f, 11.0f);
        LNF::drawIcon (g, *trailingIcon, ib, fg);
        inner.removeFromRight (5.0f);
    }
    g.setColour (fg);
    g.setFont (LNF::body (fontPx, 600, spacingEm));
    g.drawText (getButtonText(), inner, hasDot || chevron || trailingIcon ? juce::Justification::centredLeft : juce::Justification::centred);
}

// ---------------------------------------------------------------------------
DropdownButton::DropdownButton (const juce::String& c) : juce::Button (c), caption (c) {}

int DropdownButton::getIdealWidth() const
{
    const float cw = juce::GlyphArrangement::getStringWidth (LNF::body (11.0f, 500), caption);
    const float vw = juce::GlyphArrangement::getStringWidth (LNF::body (13.0f, 600), value);
    return int (juce::jmax (cw, vw + 14.0f)) + 22;
}

void DropdownButton::paintButton (juce::Graphics& g, bool over, bool)
{
    using namespace Tokens;
    auto b = getLocalBounds().toFloat();
    LNF::strokeSurface (g, b, over ? hairHover : hair2, Radius::control);
    auto inner = b.reduced (10.0f, 4.0f);
    g.setColour (textLow);
    g.setFont (LNF::body (11.0f, 500));
    g.drawText (caption, inner.removeFromTop (12.0f), juce::Justification::centredLeft);
    g.setColour (isEnabled() ? textHi : textLow);
    g.setFont (LNF::body (13.0f, 600));
    auto valueArea = inner;
    const float vw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), value);
    g.drawText (value, valueArea, juce::Justification::centredLeft);
    LNF::drawIcon (g, LNF::Icon::ChevronDown, juce::Rectangle<float> (valueArea.getX() + vw + 5.0f, valueArea.getCentreY() - 4.0f, 8.0f, 8.0f), textLow);
}

// ---------------------------------------------------------------------------
juce::String ParamTile::format (const ParameterSpec& spec, float value)
{
    if (spec.type == ParameterSpec::Type::Bool) return value >= 0.5f ? "ON" : "OFF";
    if (spec.type == ParameterSpec::Type::Choice)
    {
        const int i = juce::jlimit (0, int (spec.choices.size()) - 1, juce::roundToInt (value));
        return spec.choices.empty() ? juce::String() : juce::String (spec.choices[size_t (i)]);
    }
    return LNF::formatValue (value, spec.unit, spec.minValue, spec.maxValue);
}

ParamTile::ParamTile (juce::RangedAudioParameter& p, const ParameterSpec& s, const juce::String& l)
    : param (p), spec (s), label (l),
      attachment (p, [this] (float v) { current = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (spec.type == ParameterSpec::Type::Float ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::PointingHandCursor);
    setTooltip (juce::String (spec.name) + (spec.type == ParameterSpec::Type::Float ? "  (drag up/down, double-click resets)" : "  (click)"));
}

ParamTile::~ParamTile() = default;

void ParamTile::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds().toFloat();
    LNF::drawSurface (g, b, raised, hover || dragging ? hairHover : hair2, Radius::control);

    auto inner = b.reduced (10.0f, 8.0f);
    g.setColour (textMid);
    g.setFont (LNF::body (11.0f, 500));
    g.drawText (label, inner.removeFromTop (12.0f), juce::Justification::centredLeft);
    inner.removeFromTop (3.0f);
    const bool boolOn = spec.type == ParameterSpec::Type::Bool && current >= 0.5f;
    g.setColour (spec.type == ParameterSpec::Type::Bool ? (boolOn ? accentText : textLow) : textHi);
    g.setFont (LNF::mono (14.0f, 500));
    g.drawText (format (spec, current), inner.removeFromTop (17.0f), juce::Justification::centredLeft);

    auto bar = juce::Rectangle<float> (inner.getX(), b.getBottom() - 10.0f, inner.getWidth(), 3.0f);
    g.setColour (track);
    g.fillRoundedRectangle (bar, 1.5f);
    float pct = 0.0f;
    if (spec.type == ParameterSpec::Type::Bool) pct = boolOn ? 1.0f : 0.0f;
    else if (spec.type == ParameterSpec::Type::Choice) pct = spec.maxValue > 0.0f ? current / spec.maxValue : 0.0f;
    else pct = param.convertTo0to1 (current);
    g.setColour (accentStroke);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, pct)), 1.5f);
}

void ParamTile::mouseDown (const juce::MouseEvent& e)
{
    if (spec.type == ParameterSpec::Type::Bool)
    {
        attachment.setValueAsCompleteGesture (current >= 0.5f ? 0.0f : 1.0f);
        return;
    }
    if (spec.type == ParameterSpec::Type::Choice)
    {
        juce::PopupMenu m;
        for (size_t i = 0; i < spec.choices.size(); ++i)
            m.addItem (int (i) + 1, spec.choices[i], true, juce::roundToInt (current) == int (i));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [safe = juce::Component::SafePointer<ParamTile> (this)] (int r) { if (safe != nullptr && r > 0) safe->attachment.setValueAsCompleteGesture (float (r - 1)); });
        return;
    }
    juce::ignoreUnused (e);
    dragStartNorm = param.convertTo0to1 (current);
    dragging = true;
    attachment.beginGesture();
}

void ParamTile::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float sens = e.mods.isShiftDown() ? 800.0f : 160.0f;
    const float n = juce::jlimit (0.0f, 1.0f, dragStartNorm - float (e.getDistanceFromDragStartY()) / sens);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
}

void ParamTile::mouseUp (const juce::MouseEvent&)
{
    if (dragging) { dragging = false; attachment.endGesture(); repaint(); }
}

void ParamTile::mouseDoubleClick (const juce::MouseEvent&)
{
    if (spec.type == ParameterSpec::Type::Float)
        attachment.setValueAsCompleteGesture (spec.defaultValue);
}

void ParamTile::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (spec.type != ParameterSpec::Type::Float) return;
    const float n = juce::jlimit (0.0f, 1.0f, param.convertTo0to1 (current) + w.deltaY * 0.15f);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (n));
}

// ---------------------------------------------------------------------------
MacroKnob::MacroKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& n)
    : name (n)
{
    addAndMakeVisible (slider);
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    slider.setDoubleClickReturnValue (true, 50.0);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
    percentOfUnit = slider.getMaximum() <= 1.0;
    if (percentOfUnit) slider.setDoubleClickReturnValue (true, double (findParameterSpec (id.toStdString())->defaultValue));
    slider.onValueChange = [this] { repaint(); };
}

void MacroKnob::resized()
{
    slider.setBounds (getLocalBounds().withHeight (84).withSizeKeepingCentre (84, 84));
}

void MacroKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    b.removeFromTop (84 + 7);
    g.setColour (slider.isEnabled() ? Tokens::textHi : Tokens::textLow);
    g.setFont (LNF::body (12.0f, 600));
    g.drawText (name, b.removeFromTop (14), juce::Justification::centred);
    const double v = slider.getValue();
    g.setColour (Tokens::textLow);
    g.setFont (LNF::mono (11.0f, 500));
    g.drawText (juce::String (juce::roundToInt (percentOfUnit ? v * 100.0 : v)), b.removeFromTop (14), juce::Justification::centred);
}

// ---------------------------------------------------------------------------
juce::String formatRecommendationValues (const Recommendation& r)
{
    struct Band { bool hasFreq = false, hasGain = false, hasQ = false; float freq = 0, gain = 0, q = 0; };
    std::map<juce::String, Band> bands; // keyed by "corrEq2"
    std::vector<juce::String> order;
    juce::String out, lastGroup;
    auto append = [&] (const juce::String& group, const juce::String& text)
    {
        if (out.isNotEmpty()) out += " " + Glyph::dot() + " ";
        if (group != lastGroup) { out += group + " "; lastGroup = group; }
        out += text;
    };
    for (const auto& c : r.changes)
    {
        const juce::String id (c.paramId);
        if (id.startsWith ("corrEq") || id.startsWith ("toneEq"))
        {
            const juce::String key = id.substring (0, 7);
            auto& b = bands[key];
            if (std::find (order.begin(), order.end(), key) == order.end()) order.push_back (key);
            if (id.endsWith ("Freq")) { b.hasFreq = true; b.freq = c.value; }
            else if (id.endsWith ("Gain")) { b.hasGain = true; b.gain = c.value; }
            else if (id.endsWith ("Q")) { b.hasQ = true; b.q = c.value; }
            continue;
        }
        const auto* spec = findParameterSpec (c.paramId);
        if (spec == nullptr) continue;
        const juce::String name (spec->name);
        juce::String group = name.upToFirstOccurrenceOf (" ", false, false).toUpperCase();
        juce::String rest = name.fromFirstOccurrenceOf (" ", false, false).toLowerCase();
        if (rest.isEmpty()) { rest = group.toLowerCase(); group = "INPUT"; }
        if (group == "HPF" || group == "LPF") { rest = group.toLowerCase() + " " + rest; group = "INPUT"; }
        append (group, rest + " " + ParamTile::format (*spec, c.value));
    }
    for (const auto& key : order)
    {
        const auto& b = bands[key];
        juce::String t;
        if (b.hasFreq) t += LNF::formatValue (b.freq, "Hz", 20.0f, 20000.0f);
        if (b.hasGain) t += (t.isEmpty() ? "" : " " + Glyph::dot() + " ") + LNF::formatValue (b.gain, "dB", -18.0f, 18.0f);
        if (b.hasQ) t += (t.isEmpty() ? "" : " " + Glyph::dot() + " ") + "Q " + juce::String (b.q, 1);
        if (t.isEmpty()) t = "on";
        if (out.isNotEmpty()) out += " " + Glyph::dot() + " ";
        out += "EQ " + t;
        lastGroup = "EQ";
    }
    return out;
}

BoolParamAttachment::BoolParamAttachment (juce::RangedAudioParameter& p, juce::Button& b, std::function<void (bool)> cb)
    : param (p), button (b),
      attachment (p, [this] (float v) { state = v >= 0.5f; button.setToggleState (state, juce::dontSendNotification); if (onChange) onChange (state); }),
      onChange (std::move (cb))
{
    attachment.sendInitialUpdate();
    button.onClick = [this] { set (! state); };
}

void BoolParamAttachment::set (bool on)
{
    attachment.setValueAsCompleteGesture (on ? 1.0f : 0.0f);
}

} // namespace livemix
