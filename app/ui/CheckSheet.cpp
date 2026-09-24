#include "CheckSheet.h"
#include <cmath>

namespace livemix
{

namespace
{
    const char* stateWord (CheckSheet::State s) noexcept
    {
        switch (s)
        {
            case CheckSheet::State::Silent: return "SILENT";
            case CheckSheet::State::Low:    return "LOW";
            case CheckSheet::State::Hot:    return "HOT";
            case CheckSheet::State::Clip:   return "CLIP";
            case CheckSheet::State::Ok:
            default:                        return "OK";
        }
    }
    juce::Colour stateColour (CheckSheet::State s) noexcept
    {
        switch (s)
        {
            case CheckSheet::State::Silent: return Dine::crit;
            case CheckSheet::State::Low:    return Dine::warn;
            case CheckSheet::State::Hot:    return Dine::hot;
            case CheckSheet::State::Clip:   return Dine::crit;
            case CheckSheet::State::Ok:
            default:                        return Dine::ok;
        }
    }
}

CheckSheet::CheckSheet (MixController& c, AppServices& s) : controller (c), services (s)
{
    const auto& inputs = controller.getSession().inputs;
    for (const auto& in : inputs)
    {
        Row r;
        r.name = juce::String (in.name);
        r.role = juce::String (channelRoleName (in.role));
        rows.push_back (r);
    }
    for (auto* b : { &resetButton, &doneButton })
    {
        b->setFontPx (12.0f);
        addAndMakeVisible (*b);
    }
    resetButton.setTooltip ("Forget every clip seen so far, so the next one is the one that matters.");
    resetButton.onClick = [this] { resetClips(); };
    doneButton.onClick = [this] { if (onClose) onClose(); };
    setInterceptsMouseClicks (true, true);
}

void CheckSheet::resetClips()
{
    for (auto& r : rows) { r.clipped = false; r.holdDb = -120.0f; }
    if (controller.isPrepared())
        for (int i = 0; i < int (rows.size()) && i < controller.getGraph().numStrips(); ++i)
            (void) controller.getEngine().getStrip (i).getInputMeter().consumeMaxPeakDb();
    repaint();
}

// The meters, thirty times a second: the last second's peak for the bar, the hold for the
// word, and when the input was last heard at all.
void CheckSheet::refresh()
{
    now = double (++ticks) / 30.0;
    if (! controller.isPrepared()) return;
    const auto& engine = controller.getEngine();
    const int strips = std::min (int (rows.size()), engine.getNumStrips());
    bool changed = false;
    for (int i = 0; i < strips; ++i)
    {
        auto& r = rows[size_t (i)];
        const auto& m = engine.getStrip (i).getInputMeter();
        const float peak = m.consumeMaxPeakDb();
        if (peak > -119.0f)
        {
            // A one-second hold on the bar, falling 30 dB a second.
            r.peakDb = std::max (peak, r.peakDb - 1.0f);
            if (peak > r.holdDb) r.holdDb = peak;
            if (peak > -60.0f) r.lastHeard = now;
        }
        else r.peakDb = std::max (-120.0f, r.peakDb - 1.0f);
        if (m.hasClipped()) r.clipped = true;

        State next;
        if (r.clipped) next = State::Clip;
        else if (r.lastHeard < 0.0 ? now >= 3.0 : now - r.lastHeard >= 3.0) next = State::Silent;
        else if (r.holdDb > -6.0f) next = State::Hot;
        else if (r.holdDb < -30.0f) next = State::Low;
        else next = State::Ok;
        if (next != r.state) { r.state = next; changed = true; }
    }
    if (changed) repaint();
    else repaint (cardBounds().reduced (26, 26).withTrimmedTop (24 + 12 + 24 + 12 + 22));   // the bars only
}

juce::String CheckSheet::headline() const
{
    int ok = 0, silent = 0, low = 0, hot = 0, clip = 0;
    for (const auto& r : rows)
        switch (r.state)
        {
            case State::Silent: ++silent; break;
            case State::Low:    ++low; break;
            case State::Hot:    ++hot; break;
            case State::Clip:   ++clip; break;
            case State::Ok: default: ++ok; break;
        }
    juce::StringArray parts;
    parts.add (juce::String (rows.size()) + (rows.size() == 1 ? " input" : " inputs"));
    if (ok > 0) parts.add (juce::String (ok) + " OK");
    if (silent > 0) parts.add (juce::String (silent) + " silent");
    if (low > 0) parts.add (juce::String (low) + " low");
    if (hot > 0) parts.add (juce::String (hot) + " hot");
    if (clip > 0) parts.add (juce::String (clip) + (clip == 1 ? " clipped" : " clipped"));
    return parts.joinIntoString ("  " + juce::String (Glyph::dot()) + "  ");
}

juce::Rectangle<int> CheckSheet::cardBounds() const
{
    const int h = 26 + 24 + 12 + 24 + 12 + 22 + int (rows.size()) * (kRowH + 4) + 14 + Dine::Metric::button + 26;
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60), juce::jmin (h, getHeight() - 40));
}

void CheckSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.84f));
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto r = card.reduced (26, 26);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));
    g.drawText ("Check inputs", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (12);
    g.setColour (rows.empty() ? Dine::ink3 : Dine::ink2);
    g.setFont (Dine::mono (12.5f, 500));
    g.drawText (rows.empty() ? juce::String ("No inputs are assigned yet.") : headline(), r.removeFromTop (24), juce::Justification::centredLeft, true);
    r.removeFromTop (12);

    // The column heads.
    {
        auto head = r.removeFromTop (22);
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (9.5f, 0.08f, 500));
        g.drawText ("INPUT", head.removeFromLeft (170), juce::Justification::centredLeft);
        g.drawText ("SOURCE", head.removeFromLeft (120), juce::Justification::centredLeft);
        g.drawText ("STATE", head.removeFromRight (74), juce::Justification::centredRight);
        head.removeFromRight (12);
        g.drawText ("PEAK", head.removeFromRight (64), juce::Justification::centredRight);
        head.removeFromRight (12);
        g.drawText ("LEVEL NOW", head, juce::Justification::centredLeft);
    }

    r.removeFromBottom (14 + Dine::Metric::button);
    for (const auto& row : rows)
    {
        if (r.getHeight() < kRowH) break;
        auto line = r.removeFromTop (kRowH);
        r.removeFromTop (4);
        Dine::fillRounded (g, line.toFloat(), Dine::item, Dine::Radius::control);
        auto t = line.reduced (12, 0);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 500));
        g.drawText (row.name, t.removeFromLeft (158), juce::Justification::centredLeft, true);
        t.removeFromLeft (12);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        g.drawText (row.role, t.removeFromLeft (108), juce::Justification::centredLeft, true);
        t.removeFromLeft (12);
        // The word, then the peak, then the bar in what is left.
        auto stateCell = t.removeFromRight (74);
        const juce::String word (stateWord (row.state));
        const int w = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), word) + 14;
        Dine::drawStatusChip (g, stateCell.removeFromRight (w).withSizeKeepingCentre (w, 17).toFloat(), word, stateColour (row.state));
        t.removeFromRight (12);
        g.setColour (row.holdDb > -119.0f ? Dine::ink2 : Dine::ink4);
        g.setFont (Dine::mono (11.5f, 500));
        g.drawText (row.holdDb > -119.0f ? juce::String (row.holdDb, 1) : Glyph::dash(), t.removeFromRight (64), juce::Justification::centredRight);
        t.removeFromRight (12);
        auto bar = t.withSizeKeepingCentre (t.getWidth(), 6);
        Dine::drawWell (g, bar.toFloat(), 2.0f);
        if (row.peakDb > -60.0f)
        {
            const float fill = juce::jlimit (0.0f, 1.0f, (row.peakDb + 60.0f) / 60.0f);
            g.setColour (row.peakDb > -6.0f ? Dine::hot : row.peakDb > -18.0f ? Dine::accent : Dine::ink2);
            g.fillRect (bar.toFloat().withWidth (bar.getWidth() * fill));
        }
    }
}

void CheckSheet::resized()
{
    auto r = cardBounds().reduced (26, 26);
    auto foot = r.removeFromBottom (Dine::Metric::button);
    doneButton.setBounds (foot.removeFromRight (juce::jmax (86, doneButton.idealWidth())));
    foot.removeFromRight (8);
    resetButton.setBounds (foot.removeFromRight (juce::jmax (110, resetButton.idealWidth())));
}

void CheckSheet::mouseUp (const juce::MouseEvent& e)
{
    if (cardBounds().contains (e.getPosition())) return;
    if (onClose) onClose();
}

} // namespace livemix
