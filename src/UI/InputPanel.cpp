#include "InputPanel.h"
#include "Widgets.h"
#include "State/ParameterIDs.h"
#include "Profiles/StyleProfile.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

const char* InputPanel::healthLabel (Health h) noexcept
{
    switch (h)
    {
        case Health::Healthy:  return "HEALTHY";
        case Health::Low:      return "INPUT LOW";
        case Health::High:     return "INPUT HIGH";
        case Health::Clipping: return "CLIPPING";
        case Health::NoSignal:
        default:               return "NO SIGNAL";
    }
}

// ---------------------------------------------------------------------------
InputPanel::MixGainRow::MixGainRow (juce::RangedAudioParameter& p)
    : param (p), attachment (p, [this] (float v) { current = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip ("How loud this channel leaves Dine (its output level). This is not the preamp. Drag up/down; double-click resets.");
}

void InputPanel::MixGainRow::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    g.setColour (hair);
    g.fillRect (b.removeFromTop (1));
    b.removeFromTop (9);
    auto value = b.removeFromRight (70);
    g.setColour (textLow);
    g.setFont (LNF::body (10.0f, 600));
    g.drawText (juce::String ("DINE ") + Glyph::dot() + " OUTPUT LEVEL", b.removeFromTop (12), juce::Justification::centredLeft);
    b.removeFromTop (1);
    g.setColour (textDim);
    g.setFont (LNF::body (9.5f, 400));
    g.drawText ("not the preamp", b.removeFromTop (12), juce::Justification::centredLeft);
    g.setColour (textMid);
    g.setFont (LNF::mono (13.0f, 500));
    g.drawText (LNF::formatValue (current, "dB", -24.0f, 24.0f), value, juce::Justification::centredRight);
}

void InputPanel::MixGainRow::mouseDown (const juce::MouseEvent&)
{
    dragStartNorm = param.convertTo0to1 (current);
    attachment.beginGesture();
}
void InputPanel::MixGainRow::mouseDrag (const juce::MouseEvent& e)
{
    const float n = juce::jlimit (0.0f, 1.0f, dragStartNorm - float (e.getDistanceFromDragStartY()) / 160.0f);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
}
void InputPanel::MixGainRow::mouseUp (const juce::MouseEvent&) { attachment.endGesture(); }
void InputPanel::MixGainRow::mouseDoubleClick (const juce::MouseEvent&) { attachment.setValueAsCompleteGesture (0.0f); }

// ---------------------------------------------------------------------------
InputPanel::InputPanel (juce::AudioProcessorValueTreeState& apvts)
    : mixGain (*apvts.getParameter (ParamID::outputTrim))
{
    addAndMakeVisible (meter);
    addAndMakeVisible (mixGain);
    meter.setTooltip ("Input level at the plugin input (peak, 60 dB scale). Click to clear the clip lamp.");
}

InputPanel::~InputPanel() = default;

void InputPanel::update (const LiveState& live, ChannelRole role, StyleProfileId style)
{
    meter.setLevels (live.inPeakDb, live.inHoldDb, live.inClipped);
    const auto t = StyleProfile::targets (role, style);
    const float recent = live.inRecentMaxDb;
    Health h = Health::Healthy;
    if (live.inClipped || recent >= -0.3f) h = Health::Clipping;
    else if (recent < -60.0f) h = Health::NoSignal;
    else if (recent < t.capturePeakMinDb) h = Health::Low;
    else if (recent > t.capturePeakMaxDb) h = Health::High;
    const float centre = 0.5f * (t.capturePeakMinDb + t.capturePeakMaxDb);
    const float suggested = h == Health::Clipping ? std::round (centre - juce::jmax (recent, -0.3f)) - 2.0f : std::round (centre - recent);
    const float headroom = juce::jmax (0.0f, -live.inHoldDb);

    const bool changed = h != health || std::abs (peakDb - live.inHoldDb) > 0.05f || std::abs (suggested - suggestedDb) > 0.5f
                      || std::abs (headroom - headroomDb) > 0.05f;
    health = h; peakDb = live.inHoldDb; suggestedDb = suggested; headroomDb = headroom;
    if (changed) repaint();
}

void InputPanel::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    g.setColour (panel);
    g.fillRect (b);
    g.setColour (hair);
    g.fillRect (b.removeFromRight (1));

    auto area = getLocalBounds().withTrimmedRight (1).reduced (14, 0).withTrimmedTop (12).withTrimmedBottom (10);

    // Header: INPUT + health chip
    auto header = area.removeFromTop (18);
    g.setColour (textMid);
    g.setFont (LNF::body (12.0f, 600));
    g.drawText ("INPUT", header, juce::Justification::centredLeft);
    {
        juce::Colour fg = okText, bd = ok.withAlpha (0.5f), bg = ok.withAlpha (0.08f);
        LNF::Icon icon = LNF::Icon::Check;
        switch (health)
        {
            case Health::Low:      fg = warn; bd = warn.withAlpha (0.5f); bg = warn.withAlpha (0.08f); icon = LNF::Icon::Bang; break;
            case Health::High:     fg = warn; bd = warn.withAlpha (0.5f); bg = warn.withAlpha (0.08f); icon = LNF::Icon::Down; break;
            case Health::Clipping: fg = critText; bd = crit.withAlpha (0.55f); bg = crit.withAlpha (0.1f); icon = LNF::Icon::Warning; break;
            case Health::NoSignal: fg = textLow; bd = hair2; bg = juce::Colours::transparentBlack; icon = LNF::Icon::Dash; break;
            case Health::Healthy: default: break;
        }
        const juce::String label = healthLabel (health);
        const float cw = LNF::chipWidth (label, 10.0f, true);
        LNF::drawChip (g, header.toFloat().removeFromRight (cw).withSizeKeepingCentre (cw, 18.0f), label, fg, bd, bg, 10.0f, &icon);
    }
    area.removeFromTop (10);

    // Meter row: meter (34) · scale · readouts
    auto meterRow = area.removeFromTop (meter.getHeight());
    meterRow.removeFromLeft (34 + 9);
    auto scale = meterRow.removeFromLeft (20);
    g.setColour (textLow);
    g.setFont (LNF::mono (8.5f, 400));
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, -60.0f })
    {
        const float y = float (scale.getY()) + MeterComponent::yFor (db, float (scale.getHeight()));
        const juce::String t = db == 0.0f ? "0" : Glyph::minus() + juce::String (int (-db));
        g.drawText (t, scale.getX(), int (y) - (db == 0.0f ? 0 : db == -60.0f ? 10 : 5), scale.getWidth(), 10, juce::Justification::centredLeft);
    }
    meterRow.removeFromLeft (4);
    auto readouts = meterRow.withTrimmedTop (2);
    g.setColour (textHi);
    g.setFont (LNF::mono (21.0f, 500, -0.02f));
    const juce::String peakText = peakDb <= -99.0f ? Glyph::minus() + "inf" : (peakDb > 0.0f ? "+" : Glyph::minus()) + juce::String (std::abs (peakDb), 1);
    g.drawText (peakText, readouts.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (textLow);
    g.setFont (LNF::mono (9.5f, 400));
    g.drawText ("dBFS peak", readouts.removeFromTop (12), juce::Justification::centredLeft);
    readouts.removeFromTop (8);
    g.setColour (textMid);
    g.setFont (LNF::mono (12.0f, 500));
    g.drawText (peakDb <= -99.0f ? Glyph::dash() : juce::String (headroomDb, 1) + " dB", readouts.removeFromTop (15), juce::Justification::centredLeft);
    g.setColour (textLow);
    g.setFont (LNF::mono (9.5f, 400));
    g.drawText ("headroom", readouts.removeFromTop (12), juce::Justification::centredLeft);

    // Preamp card
    if (health == Health::Low || health == Health::Clipping)
    {
        const bool clip = health == Health::Clipping;
        auto card = cardArea.toFloat();
        LNF::drawSurface (g, card, (clip ? crit : warn).withAlpha (clip ? 0.08f : 0.07f),
                          (clip ? crit : warn).withAlpha (clip ? 0.5f : 0.45f), Tokens::Radius::control);
        auto inner = cardArea.reduced (9, 8);
        auto title = inner.removeFromTop (13);
        LNF::drawIcon (g, clip ? LNF::Icon::Warning : LNF::Icon::Bang, title.removeFromLeft (10).toFloat().withSizeKeepingCentre (9.0f, 9.0f), clip ? critText : warn);
        title.removeFromLeft (4);
        g.setColour (clip ? critText : warn);
        g.setFont (LNF::body (11.0f, 600));
        g.drawText (clip ? "CLIPPING" : "INPUT LOW", title, juce::Justification::centredLeft);
        inner.removeFromTop (3);
        auto line = inner.removeFromTop (15);
        g.setColour (textHi);
        g.setFont (LNF::body (12.0f, 500));
        const juce::String verb = clip ? "Reduce preamp " : "Increase preamp ";
        g.drawText (verb + Glyph::approx(), line, juce::Justification::centredLeft);
        const float vw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), verb + Glyph::approx()) + 4.0f;
        g.setFont (LNF::mono (12.0f, 500));
        g.drawText ((suggestedDb > 0.0f ? "+" : Glyph::minus()) + juce::String (std::abs (juce::roundToInt (suggestedDb))) + " dB", line.withTrimmedLeft (int (vw)), juce::Justification::centredLeft);
        inner.removeFromTop (3);
        g.setColour (textMid);
        g.setFont (LNF::body (10.5f, 400));
        g.drawFittedText (clip ? juce::String ("The signal is distorting before it reaches Dine ") + Glyph::dash() + " turn the preamp down."
                               : juce::String ("Turn the preamp up on the console ") + Glyph::dash() + " Dine cannot add what was never recorded.",
                          inner, juce::Justification::topLeft, 2, 1.0f);
    }
}

void InputPanel::resized()
{
    auto area = getLocalBounds().withTrimmedRight (1).reduced (14, 0).withTrimmedTop (12).withTrimmedBottom (10);
    area.removeFromTop (18 + 10);
    auto mix = area.removeFromBottom (48);
    mixGain.setBounds (mix);
    area.removeFromBottom (10);
    cardArea = area.removeFromBottom (76);
    area.removeFromBottom (10);
    meter.setBounds (area.removeFromLeft (34));
}

} // namespace livemix
