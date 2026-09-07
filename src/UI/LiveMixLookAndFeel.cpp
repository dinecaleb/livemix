#include "LiveMixLookAndFeel.h"
#include "BinaryData.h"

namespace livemix
{

namespace
{
    // JUCE's kerning factor is a fraction of the font height, which is ~1.2x the
    // CSS pixel size for these families; scale em-based letter-spacing accordingly.
    constexpr float kEmToKerning = 0.84f;

    juce::Font makeFont (juce::Typeface::Ptr tf, float px, float spacingEm)
    {
        return juce::Font (juce::FontOptions().withTypeface (tf).withPointHeight (px).withKerningFactor (spacingEm * kEmToKerning));
    }
}

juce::Typeface::Ptr LiveMixLookAndFeel::typefaceFor (int family, int weight)
{
    // Shares the cache owned by the live LookAndFeel(s); only creates one if none exists yet.
    // family 0 = UI label (Barlow), 1 = body (Barlow), 2 = mono (IBM Plex Mono).
    // Labels share Barlow with body so tracking-heavy condensed caps don't dominate.
    juce::SharedResourcePointer<TypefaceCache> cache;
    const int w = weight >= 600 ? 2 : weight >= 500 ? 1 : 0;
    auto& slot = cache->faces[family][w];
    if (slot != nullptr) return slot;

    const char* data = nullptr; int size = 0;
    using namespace LiveMixFonts;
    if (family == 0 || family == 1)
    {
        data = w == 2 ? BarlowSemiBold_ttf : w == 1 ? BarlowMedium_ttf : BarlowRegular_ttf;
        size = w == 2 ? BarlowSemiBold_ttfSize : w == 1 ? BarlowMedium_ttfSize : BarlowRegular_ttfSize;
    }
    else
    {
        data = w == 2 ? IBMPlexMonoSemiBold_ttf : w == 1 ? IBMPlexMonoMedium_ttf : IBMPlexMonoRegular_ttf;
        size = w == 2 ? IBMPlexMonoSemiBold_ttfSize : w == 1 ? IBMPlexMonoMedium_ttfSize : IBMPlexMonoRegular_ttfSize;
    }
    slot = juce::Typeface::createSystemTypefaceFor (data, size_t (size));
    return slot;
}

juce::Font LiveMixLookAndFeel::condensed (float px, int weight, float spacingEm)
{
    // Soft-cap tracking: call sites still pass design-era spacing, but heavy
    // letter-spacing reads as "AI plugin" rather than native macOS.
    return makeFont (typefaceFor (0, weight), px, juce::jmin (spacingEm, 0.04f));
}
juce::Font LiveMixLookAndFeel::body (float px, int weight, float spacingEm) { return makeFont (typefaceFor (1, weight), px, juce::jmin (spacingEm, 0.04f)); }
juce::Font LiveMixLookAndFeel::mono (float px, int weight, float spacingEm) { return makeFont (typefaceFor (2, weight), px, spacingEm); }

void LiveMixLookAndFeel::fillSurface (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill, float radius)
{
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
}

void LiveMixLookAndFeel::strokeSurface (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour stroke, float radius, float thickness)
{
    g.setColour (stroke);
    g.drawRoundedRectangle (bounds.reduced (thickness * 0.5f), radius, thickness);
}

void LiveMixLookAndFeel::drawSurface (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill, juce::Colour stroke, float radius)
{
    fillSurface (g, bounds, fill, radius);
    if (stroke.getAlpha() > 0)
        strokeSurface (g, bounds, stroke, radius);
}

LiveMixLookAndFeel::LiveMixLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Tokens::window);
    setColour (juce::DocumentWindow::backgroundColourId, Tokens::window);
    setColour (juce::Label::textColourId, Tokens::textHi);
    setColour (juce::Slider::thumbColourId, Tokens::textHi);
    setColour (juce::Slider::trackColourId, Tokens::accentStroke);
    setColour (juce::Slider::backgroundColourId, Tokens::track);
    setColour (juce::Slider::rotarySliderFillColourId, Tokens::accentStroke);
    setColour (juce::Slider::rotarySliderOutlineColourId, Tokens::track);
    setColour (juce::Slider::textBoxTextColourId, Tokens::textHi);
    setColour (juce::Slider::textBoxBackgroundColourId, Tokens::inset);
    setColour (juce::Slider::textBoxOutlineColourId, Tokens::hair2);
    setColour (juce::TextButton::buttonColourId, Tokens::raised);
    setColour (juce::TextButton::buttonOnColourId, Tokens::accentSolid);
    setColour (juce::TextButton::textColourOffId, Tokens::textMid);
    setColour (juce::TextButton::textColourOnId, Tokens::solidText);
    setColour (juce::ToggleButton::textColourId, Tokens::textHi);
    setColour (juce::ToggleButton::tickColourId, Tokens::accentText);
    setColour (juce::ComboBox::backgroundColourId, Tokens::raised);
    setColour (juce::ComboBox::textColourId, Tokens::textHi);
    setColour (juce::ComboBox::outlineColourId, Tokens::hair2);
    setColour (juce::ComboBox::arrowColourId, Tokens::textLow);
    setColour (juce::PopupMenu::backgroundColourId, Tokens::menuBg);
    setColour (juce::PopupMenu::textColourId, Tokens::textHi);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff1e242a));
    setColour (juce::PopupMenu::highlightedTextColourId, Tokens::textHi);
    setColour (juce::PopupMenu::headerTextColourId, Tokens::textLow);
    setColour (juce::GroupComponent::outlineColourId, Tokens::hair);
    setColour (juce::GroupComponent::textColourId, Tokens::textMid);
    setColour (juce::ProgressBar::backgroundColourId, Tokens::track);
    setColour (juce::ProgressBar::foregroundColourId, Tokens::accentStroke);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff39434d));
    setColour (juce::TextEditor::backgroundColourId, Tokens::inset);
    setColour (juce::TextEditor::textColourId, Tokens::textHi);
    setColour (juce::TextEditor::outlineColourId, Tokens::hair2);
    setColour (juce::TextEditor::focusedOutlineColourId, Tokens::accent);
    setColour (juce::TextEditor::highlightColourId, Tokens::accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, Tokens::accentText);
    setColour (juce::TooltipWindow::backgroundColourId, Tokens::menuBg);
    setColour (juce::TooltipWindow::textColourId, Tokens::textHi);
    setColour (juce::TooltipWindow::outlineColourId, Tokens::hairStrong);
    setColour (juce::AlertWindow::backgroundColourId, Tokens::window);
    setColour (juce::AlertWindow::textColourId, Tokens::textMid);
    setColour (juce::AlertWindow::outlineColourId, Tokens::hairStrong);
}

juce::Typeface::Ptr LiveMixLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return typefaceFor (1, f.isBold() ? 600 : 400);
    if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return typefaceFor (2, f.isBold() ? 600 : 400);
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LiveMixLookAndFeel::getLabelFont (juce::Label& l)             { return l.getFont(); }
juce::Font LiveMixLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return body (12.0f, 600); }
juce::Font LiveMixLookAndFeel::getComboBoxFont (juce::ComboBox&)          { return body (13.0f, 500); }
juce::Font LiveMixLookAndFeel::getPopupMenuFont()                          { return body (13.0f, 400); }
juce::Font LiveMixLookAndFeel::getAlertWindowTitleFont()                   { return body (15.0f, 600); }
juce::Font LiveMixLookAndFeel::getAlertWindowMessageFont()                 { return body (12.5f, 400); }
juce::Font LiveMixLookAndFeel::getAlertWindowFont()                        { return body (12.0f, 400); }

// ---------------------------------------------------------------------------
juce::String LiveMixLookAndFeel::formatValue (float v, const juce::String& unit, float minValue, float maxValue)
{
    auto sign = [] (float x) { return x > 0.0f ? juce::String ("+") : x < 0.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String(); };
    if (unit == "dB")  return sign (v) + juce::String (std::abs (v), 1) + " dB";
    if (unit == "ms")  return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms";
    if (unit == "Hz")  return v < 10.0f ? juce::String (v, 2) + " Hz" : v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz";
    if (unit == "s")   return juce::String (v, v < 10.0f ? 2 : 1) + " s";
    if (unit == ":1")  return juce::String (v, 1) + ":1";
    if (unit == "%")   return juce::String (juce::roundToInt (v)) + " %";
    if (unit.isEmpty() && maxValue <= 1.0f)
    {
        const int pct = juce::roundToInt (v * 100.0f);
        return (minValue < 0.0f ? sign (float (pct)) + juce::String (std::abs (pct)) : juce::String (pct)) + " %";
    }
    if (unit.isEmpty() && maxValue <= 100.0f && minValue >= 0.0f && maxValue > 10.0f) return juce::String (juce::roundToInt (v));
    return juce::String (v, 2);
}

void LiveMixLookAndFeel::drawHairline (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    g.fillRect (r);
}

void LiveMixLookAndFeel::drawCornerMarks (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float)
{
    // Intentionally empty — blueprint registration marks read as design-tool chrome.
}

void LiveMixLookAndFeel::drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> b, juce::Colour c)
{
    g.setColour (c);
    const auto ctr = b.getCentre();
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    juce::Path p;
    switch (icon)
    {
        case Icon::Check:
            p.startNewSubPath (ctr.x - s * 0.36f, ctr.y + 0.02f * s);
            p.lineTo (ctr.x - s * 0.1f, ctr.y + s * 0.3f);
            p.lineTo (ctr.x + s * 0.38f, ctr.y - s * 0.32f);
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, s * 0.16f), juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            break;
        case Icon::Warning:
            p.addTriangle (ctr.x, ctr.y - s * 0.38f, ctr.x + s * 0.4f, ctr.y + s * 0.32f, ctr.x - s * 0.4f, ctr.y + s * 0.32f);
            g.fillPath (p);
            break;
        case Icon::Bang:
            g.fillRect (ctr.x - s * 0.08f, ctr.y - s * 0.4f, s * 0.16f, s * 0.5f);
            g.fillRect (ctr.x - s * 0.08f, ctr.y + s * 0.22f, s * 0.16f, s * 0.16f);
            break;
        case Icon::Up:
        case Icon::Down:
        {
            const float d = icon == Icon::Up ? -1.0f : 1.0f;
            p.startNewSubPath (ctr.x, ctr.y - d * s * 0.38f);
            p.lineTo (ctr.x, ctr.y + d * s * 0.38f);
            p.startNewSubPath (ctr.x - s * 0.3f, ctr.y + d * s * 0.08f);
            p.lineTo (ctr.x, ctr.y + d * s * 0.38f);
            p.lineTo (ctr.x + s * 0.3f, ctr.y + d * s * 0.08f);
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.4f, s * 0.14f), juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            break;
        }
        case Icon::ArrowRight:
            p.startNewSubPath (ctr.x - s * 0.4f, ctr.y);
            p.lineTo (ctr.x + s * 0.4f, ctr.y);
            p.startNewSubPath (ctr.x + s * 0.1f, ctr.y - s * 0.3f);
            p.lineTo (ctr.x + s * 0.4f, ctr.y);
            p.lineTo (ctr.x + s * 0.1f, ctr.y + s * 0.3f);
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.2f, s * 0.12f), juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            break;
        case Icon::ChevronDown:
            p.addTriangle (ctr.x - s * 0.32f, ctr.y - s * 0.18f, ctr.x + s * 0.32f, ctr.y - s * 0.18f, ctr.x, ctr.y + s * 0.22f);
            g.fillPath (p);
            break;
        case Icon::Gear:
        {
            const float ro = s * 0.46f, ri = s * 0.3f, rh = s * 0.13f;
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * float (i) / 8.0f;
                juce::Path tooth;
                tooth.addRectangle (-s * 0.08f, -ro, s * 0.16f, ro - ri + 1.0f);
                tooth.applyTransform (juce::AffineTransform::rotation (a).translated (ctr));
                p.addPath (tooth);
            }
            p.addEllipse (ctr.x - ri, ctr.y - ri, ri * 2.0f, ri * 2.0f);
            p.setUsingNonZeroWinding (true);
            g.fillPath (p);
            g.setColour (Tokens::topBar);
            g.fillEllipse (ctr.x - rh, ctr.y - rh, rh * 2.0f, rh * 2.0f);
            g.setColour (c);
            break;
        }
        case Icon::Dash:
            g.fillRect (ctr.x - s * 0.3f, ctr.y - 0.75f, s * 0.6f, 1.5f);
            break;
        case Icon::Polarity:
            g.drawEllipse (ctr.x - s * 0.3f, ctr.y - s * 0.3f, s * 0.6f, s * 0.6f, 1.4f);
            p.startNewSubPath (ctr.x + s * 0.4f, ctr.y - s * 0.42f);
            p.lineTo (ctr.x - s * 0.4f, ctr.y + s * 0.42f);
            g.strokePath (p, juce::PathStrokeType (1.4f));
            break;
    }
}

float LiveMixLookAndFeel::chipWidth (const juce::String& text, float fontPx, bool withIcon)
{
    auto f = body (fontPx, 600);
    return juce::GlyphArrangement::getStringWidth (f, text) + 14.0f + (withIcon ? fontPx + 3.0f : 0.0f);
}

void LiveMixLookAndFeel::drawChip (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& text,
                                   juce::Colour fg, juce::Colour border, juce::Colour bg, float fontPx, Icon* icon)
{
    drawSurface (g, b, bg, border, Tokens::Radius::chip);
    auto inner = b.reduced (6.0f, 0.0f);
    if (icon != nullptr)
    {
        auto ib = inner.removeFromLeft (fontPx).withSizeKeepingCentre (fontPx, fontPx);
        drawIcon (g, *icon, ib, fg);
        inner.removeFromLeft (3.0f);
    }
    g.setColour (fg);
    g.setFont (body (fontPx, 600));
    g.drawText (text, inner, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
void LiveMixLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    // Matches the prototype's 84px SVG knob: 5px track arc at r=32, 22px body, pointer 22..31 from the top.
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float unit = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 84.0f;
    const auto ctr = bounds.getCentre();
    const float arcR = 32.0f * unit, arcW = 5.0f * unit, bodyR = 22.0f * unit;
    const bool enabled = s.isEnabled();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (ctr.x, ctr.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Tokens::track);
    g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bipolar ? float ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : 0.0f;
    const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);
    if (std::abs (angle - zeroAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (ctr.x, ctr.y, arcR, arcR, 0.0f, juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
        g.setColour (enabled ? Tokens::accentStroke : Tokens::textLow);
        g.strokePath (value, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    g.setColour (Tokens::knobBody);
    g.fillEllipse (ctr.x - bodyR, ctr.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (Tokens::hairStrong);
    g.drawEllipse (ctr.x - bodyR, ctr.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.startNewSubPath (0.0f, -20.0f * unit);
    pointer.lineTo (0.0f, -11.0f * unit);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (ctr));
    g.setColour (enabled ? Tokens::textHi : Tokens::textLow);
    g.strokePath (pointer, juce::PathStrokeType (2.5f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LiveMixLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool enabled = s.isEnabled();
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
    {
        const float cy = float (y) + float (h) * 0.5f;
        g.setColour (Tokens::track);
        g.fillRoundedRectangle (float (x), cy - 1.5f, float (w), 3.0f, 1.5f);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zeroX = bipolar ? float (x) + float (w) * float ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : float (x);
        g.setColour (enabled ? Tokens::accentStroke : Tokens::textLow);
        g.fillRoundedRectangle (juce::jmin (zeroX, pos), cy - 1.5f, std::abs (pos - zeroX), 3.0f, 1.5f);
        g.setColour (Tokens::textHi);
        g.fillRoundedRectangle (pos - 3.0f, cy - 6.0f, 6.0f, 12.0f, 3.0f);
        return;
    }
    if (style == juce::Slider::LinearVertical)
    {
        const float cx = float (x) + float (w) * 0.5f;
        g.setColour (Tokens::track);
        g.fillRoundedRectangle (cx - 1.5f, float (y), 3.0f, float (h), 1.5f);
        g.setColour (enabled ? Tokens::accentStroke : Tokens::textLow);
        g.fillRoundedRectangle (cx - 1.5f, pos, 3.0f, float (y + h) - pos, 1.5f);
        g.setColour (Tokens::textHi);
        g.fillRoundedRectangle (cx - 6.0f, pos - 3.0f, 12.0f, 6.0f, 3.0f);
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
}

juce::Label* LiveMixLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (11.0f, 500));
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, Tokens::inset);
    l->setColour (juce::Label::outlineWhenEditingColourId, Tokens::accent);
    return l;
}

// ---------------------------------------------------------------------------
void LiveMixLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // Compact iOS-style pill switch.
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();
    juce::Rectangle<float> sw (2.0f, (float (b.getHeight()) - 18.0f) * 0.5f, 32.0f, 18.0f);
    const float r = Tokens::Radius::pill;
    g.setColour ((on ? Tokens::accent : Tokens::track).brighter (highlighted ? 0.08f : 0.0f).withAlpha (enabled ? 1.0f : 0.5f));
    g.fillRoundedRectangle (sw, r);
    g.setColour ((on ? Tokens::accentStroke : Tokens::hair2).withAlpha (enabled ? 1.0f : 0.5f));
    g.drawRoundedRectangle (sw.reduced (0.5f), r, 1.0f);

    const float knob = 14.0f;
    const float kx = sw.getX() + (on ? sw.getWidth() - knob - 2.0f : 2.0f);
    g.setColour ((on ? Tokens::solidText : Tokens::textMid).withAlpha (enabled ? 1.0f : 0.5f));
    g.fillEllipse (kx, sw.getCentreY() - knob * 0.5f, knob, knob);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.findColour (juce::ToggleButton::textColourId).withAlpha (enabled ? 1.0f : 0.5f));
        g.setFont (body (12.0f, 500));
        g.drawText (b.getButtonText(), 42, 0, b.getWidth() - 44, b.getHeight(), juce::Justification::centredLeft);
    }
}

void LiveMixLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour fill = on ? Tokens::accentSolid : background;
    if (! b.isEnabled()) fill = on ? Tokens::disabledBg : background.withAlpha (0.5f);
    if (down) fill = fill.darker (0.15f); else if (highlighted) fill = on ? Tokens::accentHover : fill.brighter (0.06f);
    drawSurface (g, bounds, fill, on ? fill : (highlighted ? Tokens::hairHover : Tokens::hair2), Tokens::Radius::control);
}

void LiveMixLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const auto c = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred);
}

// ---------------------------------------------------------------------------
void LiveMixLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    drawSurface (g, bounds,
                 box.findColour (juce::ComboBox::backgroundColourId).withAlpha (box.isEnabled() ? 1.0f : 0.5f),
                 box.findColour (juce::ComboBox::outlineColourId),
                 Tokens::Radius::control);
    drawIcon (g, Icon::ChevronDown, juce::Rectangle<float> (bounds.getRight() - 18.0f, bounds.getCentreY() - 5.0f, 10.0f, 10.0f),
              box.findColour (juce::ComboBox::arrowColourId));
}

void LiveMixLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LiveMixLookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int w, int h, const juce::String& text,
                                                    const juce::Justification&, juce::GroupComponent& gc)
{
    auto bounds = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    drawSurface (g, bounds, Tokens::raised, gc.findColour (juce::GroupComponent::outlineColourId), Tokens::Radius::card);
    g.setColour (gc.findColour (juce::GroupComponent::textColourId));
    g.setFont (body (11.0f, 600));
    g.drawText (text, 10, 4, w - 20, 14, juce::Justification::centredLeft);
}

void LiveMixLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& pb, int w, int h, double progress, const juce::String&)
{
    auto bounds = juce::Rectangle<int> (0, 0, w, h).toFloat();
    auto bar = bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);
    g.setColour (pb.findColour (juce::ProgressBar::backgroundColourId));
    g.fillRoundedRectangle (bar, 2.0f);
    if (progress > 0.0)
    {
        g.setColour (pb.findColour (juce::ProgressBar::foregroundColourId));
        g.fillRoundedRectangle (bar.withWidth (float (w) * float (juce::jlimit (0.0, 1.0, progress))), 2.0f);
    }
}

void LiveMixLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& sb, int x, int y, int w, int h, bool vertical,
                                        int thumbStart, int thumbSize, bool over, bool)
{
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> (float (x) + 3.0f, float (thumbStart), float (w) - 6.0f, float (thumbSize))
                                            : juce::Rectangle<float> (float (thumbStart), float (y) + 3.0f, float (thumbSize), float (h) - 6.0f);
    g.setColour (sb.findColour (juce::ScrollBar::thumbColourId).withAlpha (over ? 1.0f : 0.55f));
    g.fillRoundedRectangle (thumb, Tokens::Radius::pill);
}

void LiveMixLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h)
{
    auto bounds = juce::Rectangle<int> (0, 0, w, h).toFloat();
    drawSurface (g, bounds, findColour (juce::TooltipWindow::backgroundColourId),
                 findColour (juce::TooltipWindow::outlineColourId), Tokens::Radius::control);
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (t, body (12.0f, 400), findColour (juce::TooltipWindow::textColourId));
    juce::TextLayout tl;
    tl.createLayout (s, float (w) - 16.0f);
    tl.draw (g, bounds.reduced (8.0f, 4.0f));
}

void LiveMixLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    fillSurface (g, juce::Rectangle<float> (0, 0, float (w), float (h)),
                 te.findColour (juce::TextEditor::backgroundColourId), Tokens::Radius::control);
}

void LiveMixLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    strokeSurface (g, juce::Rectangle<float> (0, 0, float (w), float (h)),
                   te.hasKeyboardFocus (true) ? te.findColour (juce::TextEditor::focusedOutlineColourId)
                                              : te.findColour (juce::TextEditor::outlineColourId),
                   Tokens::Radius::control);
}

void LiveMixLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    drawSurface (g, juce::Rectangle<float> (0, 0, float (w), float (h)),
                 findColour (juce::PopupMenu::backgroundColourId), Tokens::hairStrong, Tokens::Radius::card);
}

void LiveMixLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 60; idealHeight = 9; return; }
    juce::String label = text.upToFirstOccurrenceOf ("\t", false, false), meta = text.fromFirstOccurrenceOf ("\t", false, false);
    idealHeight = 30;
    idealWidth = int (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), label)
                    + (meta.isNotEmpty() ? juce::GlyphArrangement::getStringWidth (mono (10.0f, 500), meta) + 16.0f : 0.0f)) + 46;
    idealWidth = juce::jmax (idealWidth, 200);
}

void LiveMixLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                            const juce::String&, const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (Tokens::hair);
        g.fillRect (area.reduced (8, 0).withSizeKeepingCentre (area.getWidth() - 16, 1));
        return;
    }
    auto r = area.reduced (5, 2).toFloat();
    if (isTicked) { g.setColour (Tokens::accent.withAlpha (0.14f)); g.fillRoundedRectangle (r, Tokens::Radius::chip); }
    if (isHighlighted && isActive) { g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId)); g.fillRoundedRectangle (r, Tokens::Radius::chip); }

    // "Label\tmeta": the part after a tab is drawn right-aligned in mono, like the prototype's menus.
    juce::String label = text.upToFirstOccurrenceOf ("\t", false, false), meta = text.fromFirstOccurrenceOf ("\t", false, false);
    auto inner = area.reduced (10, 0);
    if (hasSubMenu)
    {
        auto ab = inner.removeFromRight (12).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        juce::Path p; p.addTriangle (ab.getX(), ab.getY(), ab.getRight(), ab.getCentreY(), ab.getX(), ab.getBottom());
        g.setColour (Tokens::textLow); g.fillPath (p);
    }
    if (meta.isNotEmpty())
    {
        g.setColour (Tokens::textLow);
        g.setFont (mono (10.0f, 500));
        g.drawText (meta, inner, juce::Justification::centredRight);
    }
    g.setColour (textColour != nullptr ? *textColour : (isActive ? Tokens::textHi : Tokens::textDim));
    g.setFont (getPopupMenuFont());
    g.drawText (label, inner, juce::Justification::centredLeft);
}

void LiveMixLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea, juce::TextLayout& tl)
{
    auto b = w.getLocalBounds().toFloat();
    drawSurface (g, b, Tokens::window, Tokens::hairStrong, Tokens::Radius::card);
    g.setColour (Tokens::textHi);
    g.setFont (getAlertWindowTitleFont());
    g.drawText (w.getName(), textArea.getX(), 14, textArea.getWidth(), 22, juce::Justification::centredLeft);
    tl.draw (g, textArea.toFloat());
}

void LiveMixLookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& w, juce::Graphics& g, int width, int h, int, int, const juce::Image*, bool)
{
    g.setColour (Tokens::topBar);
    g.fillRect (0, 0, width, h);
    g.setColour (Tokens::textMid);
    g.setFont (body (12.0f, 600));
    g.drawText (w.getName(), 0, 0, width, h, juce::Justification::centred);
}

} // namespace livemix
