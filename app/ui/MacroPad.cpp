#include "MacroPad.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

namespace
{
    juce::String shortName (MixMacro m) { return juce::String (MixMacros::name (m)).toUpperCase(); }

    float easeOut (double t) noexcept
    {
        const double c = 1.0 - juce::jlimit (0.0, 1.0, t);
        return float (1.0 - c * c * c);
    }

    // The LIVE SAFE fence: the band a value may not enter, hatched so it reads as a fence
    // and not as a level, with a hairline along the edge that faces the plan.
    enum class Faces { Down, Up, Right, Left };
    void hatch (juce::Graphics& g, juce::Rectangle<float> band, Faces faces)
    {
        if (band.getWidth() < 1.0f || band.getHeight() < 1.0f) return;
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (band.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (float d = -band.getHeight(); d < band.getWidth(); d += 6.0f)
            g.drawLine (band.getX() + d, band.getY(), band.getX() + d + band.getHeight(), band.getBottom(), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        switch (faces)
        {
            case Faces::Down:  g.fillRect (juce::Rectangle<float> (band.getX(), band.getBottom() - 1.0f, band.getWidth(), 1.0f)); break;
            case Faces::Up:    g.fillRect (juce::Rectangle<float> (band.getX(), band.getY(), band.getWidth(), 1.0f)); break;
            case Faces::Right: g.fillRect (juce::Rectangle<float> (band.getRight() - 1.0f, band.getY(), 1.0f, band.getHeight())); break;
            case Faces::Left:  g.fillRect (juce::Rectangle<float> (band.getX(), band.getY(), 1.0f, band.getHeight())); break;
        }
    }

    void drawFocusRing (juce::Graphics& g, juce::Rectangle<int> r, float radius)
    {
        g.setColour (Dine::focusRing);
        g.drawRoundedRectangle (r.toFloat().expanded (2.5f), radius + 2.0f, 1.5f);
    }
}

// ================================================================== MacroPad
class MacroPad::Value : public juce::AccessibilityValueInterface
{
public:
    explicit Value (MacroPad& p) : pad (p) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return pad.x; }
    juce::String getCurrentValueAsString() const override { return pad.describeValues(); }
    void setValue (double v) override { pad.apply (float (v), pad.y); }
    void setValueAsString (const juce::String& s) override { setValue (s.getDoubleValue()); }
    AccessibleValueRange getRange() const override { return { { 0.0, 100.0 }, 1.0 }; }
private:
    MacroPad& pad;
};

MacroPad::MacroPad (const juce::String& t, MixMacro across, MixMacro up, Corners c, std::vector<Snap> s,
                    std::function<void (MixMacro, float)> onChange)
    : title (t), xm (across), ym (up), corners (c), snaps (std::move (s)), changed (std::move (onChange))
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setTitle (title);
    for (size_t i = 0; i < snaps.size(); ++i)
    {
        auto b = std::make_unique<DineButton> (snaps[i].name, DineButton::Style::Toggle);
        b->setFontPx (11.0f);
        b->setPadX (4);
        b->setTooltip (juce::String (shortName (xm)) + " " + juce::String (int (snaps[i].x)) + ", " + shortName (ym) + " "
                       + juce::String (int (snaps[i].y)) + ". The puck eases there; the sound moves at once.");
        b->onClick = [this, i] { settleTo (snaps[i].x, snaps[i].y); };
        addAndMakeVisible (*b);
        snapButtons.push_back (std::move (b));
    }
    refreshTooltip();
    refreshSnaps();
}

MacroPad::~MacroPad() = default;

juce::String MacroPad::describeValues() const
{
    return shortName (xm) + " " + juce::String (int (std::round (x))) + "  " + Glyph::dot() + "  "
         + shortName (ym) + " " + juce::String (int (std::round (y)));
}

void MacroPad::setValues (float across, float up)
{
    if (isTimerRunning() && std::abs (across - x) < 0.01f && std::abs (up - y) < 0.01f) return;   // easing there already
    stopTimer();
    const bool moved = std::abs (across - x) >= 0.01f || std::abs (up - y) >= 0.01f;
    x = shownX = across;
    y = shownY = up;
    if (moved) { refreshSnaps(); repaint(); }
}

void MacroPad::settleTo (float across, float up)
{
    across = juce::jlimit (lo, hi, std::round (across));
    up = juce::jlimit (lo, hi, std::round (up));
    easeFromX = shownX; easeFromY = shownY;
    easeStart = juce::Time::getMillisecondCounterHiRes();
    const bool moved = std::abs (across - x) >= 0.01f || std::abs (up - y) >= 0.01f;
    x = across; y = up;
    if (moved && changed) { changed (xm, x); changed (ym, y); announce(); }
    refreshSnaps();
    if (std::abs (shownX - x) < 0.01f && std::abs (shownY - y) < 0.01f) { repaint(); return; }
    if (! isTimerRunning()) startTimerHz (60);
}

void MacroPad::setLimits (float newLo, float newHi, const juce::String& reason)
{
    newLo = juce::jlimit (0.0f, 50.0f, newLo);
    newHi = juce::jlimit (50.0f, 100.0f, newHi);
    if (std::abs (newLo - lo) < 0.01f && std::abs (newHi - hi) < 0.01f && reason == limitReason) return;
    lo = newLo; hi = newHi; limitReason = reason;
    refreshTooltip();
    repaint();
}

void MacroPad::setPadSize (int px)
{
    px = juce::jlimit (kMinPad, kMaxPad, px);
    if (px == padSize) return;
    padSize = px;
    resized();
}

void MacroPad::setCompact (bool c)
{
    if (c == compact) return;
    compact = c;
    for (auto& b : snapButtons) b->setVisible (! compact);
    resized();
    repaint();
}

void MacroPad::refreshTooltip()
{
    // Short on purpose: the corner words already say what each direction does.
    juce::String t = "Across: " + juce::String (MixMacros::lowLabel (xm)) + " to " + MixMacros::highLabel (xm) + " (" + shortName (xm).toLowerCase() + ").  "
                   + "Up: " + juce::String (MixMacros::lowLabel (ym)) + " to " + MixMacros::highLabel (ym) + " (" + shortName (ym).toLowerCase() + ").  "
                   + "Press anywhere to put the puck there. Double-click for the plan.";
    if (limitReason.isNotEmpty()) t += "  " + limitReason;
    setTooltip (t);
}

void MacroPad::refreshSnaps()
{
    for (size_t i = 0; i < snaps.size() && i < snapButtons.size(); ++i)
        snapButtons[i]->setToggleState (std::abs (snaps[i].x - x) < 0.5f && std::abs (snaps[i].y - y) < 0.5f, juce::dontSendNotification);
}

void MacroPad::announce()
{
    setDescription (juce::String (MixMacros::name (xm)) + " " + juce::String (int (x)) + ", " + MixMacros::name (ym) + " " + juce::String (int (y)));
    if (auto* h = getAccessibilityHandler()) h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void MacroPad::apply (float across, float up)
{
    across = juce::jlimit (lo, hi, std::round (across));
    up = juce::jlimit (lo, hi, std::round (up));
    const bool moved = std::abs (across - x) >= 0.01f || std::abs (up - y) >= 0.01f;
    if (! moved) return;
    stopTimer();
    x = shownX = across;
    y = shownY = up;
    if (changed) { changed (xm, x); changed (ym, y); }
    announce();
    refreshSnaps();
    repaint();
}

juce::Point<float> MacroPad::puckAt (float across, float up) const noexcept
{
    const auto t = travel().toFloat();
    return { t.getX() + t.getWidth() * (across / 100.0f), t.getBottom() - t.getHeight() * (up / 100.0f) };
}

void MacroPad::pointerTo (juce::Point<float> p)
{
    const auto t = travel().toFloat();
    if (t.getWidth() <= 0.0f || t.getHeight() <= 0.0f) return;
    apply ((p.x - t.getX()) / t.getWidth() * 100.0f, (t.getBottom() - p.y) / t.getHeight() * 100.0f);
}

void MacroPad::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! pad.contains (e.getPosition())) return;
    grabKeyboardFocus();
    dragging = true;
    pointerTo (e.position);
    repaint();
    if (onActiveChanged) onActiveChanged();
}

void MacroPad::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    pointerTo (e.position);            // keeps tracking past the square: the drag is absolute
}

void MacroPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    repaint();
    if (onActiveChanged) onActiveChanged();
}

void MacroPad::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isEnabled() && pad.contains (e.getPosition())) settleTo (50.0f, 50.0f);
}

bool MacroPad::keyPressed (const juce::KeyPress& k)
{
    const float step = k.getModifiers().isShiftDown() ? 5.0f : 1.0f;
    if (k.isKeyCode (juce::KeyPress::leftKey))  { apply (x - step, y); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey)) { apply (x + step, y); return true; }
    if (k.isKeyCode (juce::KeyPress::upKey))    { apply (x, y + step); return true; }
    if (k.isKeyCode (juce::KeyPress::downKey))  { apply (x, y - step); return true; }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> MacroPad::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

void MacroPad::timerCallback()
{
    const float t = easeOut ((juce::Time::getMillisecondCounterHiRes() - easeStart) / kEaseMs);
    shownX = easeFromX + (x - easeFromX) * t;
    shownY = easeFromY + (y - easeFromY) * t;
    if (t >= 1.0f) { shownX = x; shownY = y; stopTimer(); }
    repaint (pad);
}

void MacroPad::resized()
{
    auto r = getLocalBounds();
    const int side = juce::jmin (padSize, r.getWidth());
    head = r.removeFromTop (headFor (side));
    r.removeFromTop (kHeadGap);
    pad = r.removeFromTop (side).withSizeKeepingCentre (side, side);
    head = head.withX (pad.getX()).withWidth (pad.getWidth());
    if (compact) { snapRow = {}; return; }
    r.removeFromTop (kSnapGap);
    snapRow = r.removeFromTop (Dine::Metric::control).withX (pad.getX()).withWidth (pad.getWidth());
    // The snaps sit in one track under the square, so they read as one control with three
    // places on it rather than three buttons.
    auto row = snapRow.reduced (2);
    const int n = int (snapButtons.size());
    const int gap = 2;
    const int w = n > 0 ? (row.getWidth() - gap * (n - 1)) / n : 0;
    for (int i = 0; i < n; ++i) { snapButtons[size_t (i)]->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
}

void MacroPad::paint (juce::Graphics& g)
{
    const bool fenced = lo > 0.01f || hi < 99.99f;

    // ---- the head: the title, centred over the square
    g.setColour (Dine::ink3);
    g.setFont (Dine::caps (10.5f, 0.10f));
    g.drawText (title, head, juce::Justification::centred);
    if (! compact && ! snapRow.isEmpty()) Dine::fillRounded (g, snapRow.toFloat(), Dine::tile, Dine::Radius::control);

    // ---- the square
    const auto sq = pad.toFloat();
    Dine::fillRounded (g, sq, Dine::tile, Dine::Radius::card);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path round; round.addRoundedRectangle (sq, Dine::Radius::card);
        g.reduceClipRegion (round);

        const float cx = sq.getCentreX(), cy = sq.getCentreY();
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        for (float d = 24.0f; d < sq.getWidth() * 0.5f; d += 24.0f)
        {
            g.fillRect (juce::Rectangle<float> (cx - d, sq.getY(), 1.0f, sq.getHeight()));
            g.fillRect (juce::Rectangle<float> (cx + d, sq.getY(), 1.0f, sq.getHeight()));
            g.fillRect (juce::Rectangle<float> (sq.getX(), cy - d, sq.getWidth(), 1.0f));
            g.fillRect (juce::Rectangle<float> (sq.getX(), cy + d, sq.getWidth(), 1.0f));
        }
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (cx, sq.getY(), 1.0f, sq.getHeight()));
        g.fillRect (juce::Rectangle<float> (sq.getX(), cy, sq.getWidth(), 1.0f));

        // the fence
        if (fenced)
        {
            const auto t = travel().toFloat();
            const float left = t.getX() + t.getWidth() * (lo / 100.0f), right = t.getX() + t.getWidth() * (hi / 100.0f);
            const float top = t.getBottom() - t.getHeight() * (hi / 100.0f), bottom = t.getBottom() - t.getHeight() * (lo / 100.0f);
            hatch (g, { sq.getX(), sq.getY(), sq.getWidth(), top - sq.getY() }, Faces::Down);
            hatch (g, { sq.getX(), bottom, sq.getWidth(), sq.getBottom() - bottom }, Faces::Up);
            hatch (g, { sq.getX(), top, left - sq.getX(), bottom - top }, Faces::Right);
            hatch (g, { right, top, sq.getRight() - right, bottom - top }, Faces::Left);
        }

        // the plan: a dashed ring at dead centre
        {
            juce::Path ring, dashed;
            ring.addEllipse (cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
            const float dash[] = { 2.0f, 2.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, ring, dash, 2);
            g.setColour (juce::Colours::white.withAlpha (isOffCentre() ? 0.30f : 0.55f));
            g.fillPath (dashed);
        }

        // the corner words: quiet, the way the ticks on a dial are quiet
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (9.5f, 0.08f, 500));
        const auto in = pad.reduced (10, 8);
        g.drawText (corners.tl, in, juce::Justification::topLeft);
        g.drawText (corners.tr, in, juce::Justification::topRight);
        g.drawText (corners.bl, in, juce::Justification::bottomLeft);
        g.drawText (corners.br, in, juce::Justification::bottomRight);

        // the values, inside the square on the line under the top corner words, only once
        // there is something to say (at the plan the dashed ring says it all)
        if (isOffCentre() || dragging)
        {
            g.setColour (dragging ? Dine::ink : Dine::ink2);
            g.setFont (Dine::mono (11.0f, 500));
            g.drawText (describeValues(), in.withTrimmedTop (16).removeFromTop (14), juce::Justification::centred);
        }

        // the puck
        const auto p = puckAt (shownX, shownY);
        if (dragging)
        {
            juce::ColourGradient glow (Dine::accent.withAlpha (0.22f), p.x, p.y, Dine::accent.withAlpha (0.0f), p.x + 40.0f, p.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (80.0f, 80.0f).withCentre (p));
        }
        g.setColour (Dine::accent);
        g.drawEllipse (juce::Rectangle<float> (float (kPuck) + 5.0f, float (kPuck) + 5.0f).withCentre (p), 1.0f);
        g.setColour (Dine::tile);
        g.fillEllipse (juce::Rectangle<float> (float (kPuck) + 4.0f, float (kPuck) + 4.0f).withCentre (p));
        g.setColour (Dine::accent);
        g.fillEllipse (juce::Rectangle<float> (float (kPuck), float (kPuck)).withCentre (p));
    }
    Dine::hairlineRounded (g, sq, dragging ? Dine::accent.withAlpha (0.45f) : Dine::hair, Dine::Radius::card);
    if (hasKeyboardFocus (false)) drawFocusRing (g, pad, Dine::Radius::card);
}

// ================================================================== MacroRibbon
class MacroRibbon::Value : public juce::AccessibilityValueInterface
{
public:
    explicit Value (MacroRibbon& r) : ribbon (r) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return ribbon.value; }
    juce::String getCurrentValueAsString() const override { return juce::String (MixMacros::name (ribbon.macro)) + " " + juce::String (int (ribbon.value)); }
    void setValue (double v) override { ribbon.apply (float (v)); }
    void setValueAsString (const juce::String& s) override { setValue (s.getDoubleValue()); }
    AccessibleValueRange getRange() const override { return { { 0.0, 100.0 }, 1.0 }; }
private:
    MacroRibbon& ribbon;
};

MacroRibbon::MacroRibbon (MixMacro m, std::function<void (float)> onChange) : macro (m), changed (std::move (onChange))
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    setTitle (MixMacros::name (macro));
    refreshTooltip();
}

MacroRibbon::~MacroRibbon() = default;

void MacroRibbon::refreshTooltip()
{
    juce::String t = juce::String (MixMacros::tooltip (macro)) + "  " + Glyph::dot() + "  " + MixMacros::lowLabel (macro) + " at the left, "
                   + MixMacros::highLabel (macro) + " at the right, the plan in the middle. Press anywhere to put it there; double-click for the plan.";
    if (limitReason.isNotEmpty()) t += "  " + limitReason;
    setTooltip (t);
}

void MacroRibbon::setValue (float v)
{
    if (isTimerRunning() && std::abs (v - value) < 0.01f) return;
    stopTimer();
    const bool moved = std::abs (v - value) >= 0.01f;
    value = shown = v;
    if (moved) repaint();
}

void MacroRibbon::settleTo (float v)
{
    v = juce::jlimit (lo, hi, std::round (v));
    easeFrom = shown;
    easeStart = juce::Time::getMillisecondCounterHiRes();
    if (std::abs (v - value) >= 0.01f) { value = v; if (changed) changed (value); announce(); }
    if (std::abs (shown - value) < 0.01f) { repaint(); return; }
    if (! isTimerRunning()) startTimerHz (60);
}

void MacroRibbon::setLimits (float newLo, float newHi, const juce::String& reason)
{
    newLo = juce::jlimit (0.0f, 50.0f, newLo);
    newHi = juce::jlimit (50.0f, 100.0f, newHi);
    if (std::abs (newLo - lo) < 0.01f && std::abs (newHi - hi) < 0.01f && reason == limitReason) return;
    lo = newLo; hi = newHi; limitReason = reason;
    refreshTooltip();
    repaint();
}

void MacroRibbon::announce()
{
    setDescription (juce::String (MixMacros::name (macro)) + " " + juce::String (int (value)));
    if (auto* h = getAccessibilityHandler()) h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void MacroRibbon::apply (float v)
{
    v = juce::jlimit (lo, hi, std::round (v));
    if (std::abs (v - value) < 0.01f) return;
    stopTimer();
    value = shown = v;
    if (changed) changed (value);
    announce();
    repaint();
}

void MacroRibbon::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    grabKeyboardFocus();
    dragging = true;
    if (track.getWidth() > 0) apply ((e.position.x - float (track.getX())) / float (track.getWidth()) * 100.0f);
    repaint();
}

void MacroRibbon::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging && track.getWidth() > 0) apply ((e.position.x - float (track.getX())) / float (track.getWidth()) * 100.0f);
}

void MacroRibbon::mouseUp (const juce::MouseEvent&) { if (dragging) { dragging = false; repaint(); } }
void MacroRibbon::mouseDoubleClick (const juce::MouseEvent&) { if (isEnabled()) settleTo (50.0f); }

bool MacroRibbon::keyPressed (const juce::KeyPress& k)
{
    const float step = k.getModifiers().isShiftDown() ? 5.0f : 1.0f;
    if (k.isKeyCode (juce::KeyPress::leftKey))  { apply (value - step); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey)) { apply (value + step); return true; }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> MacroRibbon::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

void MacroRibbon::timerCallback()
{
    const float t = easeOut ((juce::Time::getMillisecondCounterHiRes() - easeStart) / kEaseMs);
    shown = easeFrom + (value - easeFrom) * t;
    if (t >= 1.0f) { shown = value; stopTimer(); }
    repaint();
}

void MacroRibbon::resized()
{
    auto r = getLocalBounds();
    label = r.removeFromLeft (juce::jmax (56, Dine::textWidth (Dine::caps (10.5f, 0.10f), MixMacros::name (macro)) + 4));
    r.removeFromLeft (10);
    readout = r.removeFromRight (30);
    r.removeFromRight (10);
    const auto wordFont = Dine::caps (9.5f, 0.08f, 500);
    low = r.removeFromLeft (Dine::textWidth (wordFont, juce::String (MixMacros::lowLabel (macro)).toUpperCase()) + 2);
    r.removeFromLeft (12);
    high = r.removeFromRight (Dine::textWidth (wordFont, juce::String (MixMacros::highLabel (macro)).toUpperCase()) + 2);
    r.removeFromRight (12);
    track = r.reduced (7, 0);          // the cap's half-width, so 0 and 100 sit inside the ribbon
}

void MacroRibbon::paint (juce::Graphics& g)
{
    const bool moved = isOffCentre();
    g.setColour (Dine::ink3);
    g.setFont (Dine::caps (10.5f, 0.10f));
    g.drawText (MixMacros::name (macro), label, juce::Justification::centredLeft);

    // The two ends, as quiet as the pads' corner words; the one the value leans to lights up.
    g.setFont (Dine::caps (9.5f, 0.08f, 500));
    g.setColour (value < 49.5f ? Dine::ink2 : Dine::ink4);
    g.drawText (juce::String (MixMacros::lowLabel (macro)).toUpperCase(), low, juce::Justification::centredLeft);
    g.setColour (value > 50.5f ? Dine::ink2 : Dine::ink4);
    g.drawText (juce::String (MixMacros::highLabel (macro)).toUpperCase(), high, juce::Justification::centredRight);

    if (moved || dragging)
    {
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (juce::String (int (std::round (value))), readout, juce::Justification::centredRight);
    }

    if (track.getWidth() <= 0) return;
    const auto well = track.toFloat().expanded (7.0f, 0.0f).withSizeKeepingCentre (float (track.getWidth() + 14), 8.0f);
    Dine::fillRounded (g, well, dragging ? Dine::fill : Dine::well, 4.0f);
    const float cx = float (track.getX()) + float (track.getWidth()) * 0.5f;
    const float px = float (track.getX()) + float (track.getWidth()) * (shown / 100.0f);
    if (lo > 0.01f || hi < 99.99f)
    {
        const float left = float (track.getX()) + float (track.getWidth()) * (lo / 100.0f);
        const float right = float (track.getX()) + float (track.getWidth()) * (hi / 100.0f);
        hatch (g, { well.getX(), well.getY(), left - well.getX(), well.getHeight() }, Faces::Right);
        hatch (g, { right, well.getY(), well.getRight() - right, well.getHeight() }, Faces::Left);
    }
    if (std::abs (shown - 50.0f) >= 0.5f)
    {
        g.setColour (Dine::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, px), well.getY(), std::abs (px - cx), well.getHeight()), 4.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (cx - 0.5f, well.getY() - 3.0f, 1.0f, well.getHeight() + 6.0f));
    g.setColour (Dine::ink);
    g.fillRoundedRectangle (juce::Rectangle<float> (9.0f, 14.0f).withCentre ({ px, well.getCentreY() }), 2.0f);
    if (hasKeyboardFocus (false)) drawFocusRing (g, well.toNearestInt(), 4.0f);
}

} // namespace livemix
