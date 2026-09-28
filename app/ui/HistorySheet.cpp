#include "HistorySheet.h"

namespace livemix
{

namespace
{
    // "9:42 am", or "Sun 9:42 am" once it is not today any more: an engineer looking for the
    // mix the service went out on is looking for a time of day, not a date stamp.
    juce::String whenText (long long whenMs)
    {
        if (whenMs <= 0) return "-";
        const juce::Time t (whenMs);
        const auto today = juce::Time::getCurrentTime();
        const bool sameDay = t.getYear() == today.getYear() && t.getDayOfYear() == today.getDayOfYear();
        const auto clock = t.toString (false, true, false, true);
        return sameDay ? clock : t.getWeekdayName (true) + " " + clock;
    }
}

HistorySheet::HistorySheet (MixController& c, AppServices& s) : controller (c), services (s)
{
    doneButton.setFontPx (12.0f);
    addAndMakeVisible (doneButton);
    doneButton.onClick = [this] { if (onClose) onClose(); };
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    Dine::nativeScrolling (viewport);
    addAndMakeVisible (viewport);
    setInterceptsMouseClicks (true, true);
    rebuild();
}

void HistorySheet::rebuild()
{
    rows.clear();
    list.removeAllChildren();
    const auto& all = controller.getCheckpoints();
    builtFor = all.size();

    std::vector<std::string> now;
    for (const auto& in : controller.getSession().inputs) now.push_back (in.name);

    for (int i = int (all.size()) - 1; i >= 0; --i)      // newest first: that is how a list like this is read
    {
        const auto& c = all[size_t (i)];
        auto row = std::make_unique<Row>();
        row->index = i;
        row->when = whenText (c.whenMs);
        row->what = juce::String (c.what);
        row->fromTune = c.fromTune;
        row->sameConsole = c.inputs == now;
        row->restore = std::make_unique<DineButton> ("Restore", DineButton::Style::Ghost);
        row->restore->setFontPx (11.5f);
        row->restore->setEnabled (row->sameConsole);
        row->restore->setTooltip (row->sameConsole
            ? "Put the whole mix back to how it was here. UNDO takes it forward again, and where it is now is kept too."
            : "This was kept with a different set of inputs, so it cannot go back onto this one.");
        const int index = i;
        row->restore->onClick = [this, index]
        {
            if (controller.restoreCheckpoint (index)) { services.touchSession(); rebuild(); resized(); repaint(); }
        };
        list.addAndMakeVisible (*row->restore);
        rows.push_back (std::move (row));
    }
    resized();
}

void HistorySheet::refresh()
{
    if (controller.getCheckpoints().size() != builtFor) { rebuild(); repaint(); }
}

juce::Rectangle<int> HistorySheet::cardBounds() const
{
    const int listH = juce::jmax (kRowH, int (rows.size()) * (kRowH + 4));
    const int h = 26 + 24 + 10 + 22 + 8 + juce::jmin (listH, 420) + 14 + Dine::Metric::button + 26;
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60), juce::jmin (h, getHeight() - 40));
}

void HistorySheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.84f));
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto r = card.reduced (26, 26);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));
    g.drawText ("Mix history", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (10);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    g.drawText (rows.empty() ? juce::String ("Nothing yet. Every tune, scene and morning of mixing lands here.")
                             : juce::String (rows.size()) + (rows.size() == 1 ? " place to go back to. Where the mix is now is kept before it moves."
                                                                              : " places to go back to. Where the mix is now is kept before it moves."),
                r.removeFromTop (22), juce::Justification::centredLeft, true);
}

void HistorySheet::paintRows (juce::Graphics& g)
{
    int y = 0;
    for (const auto& row : rows)
    {
        auto line = juce::Rectangle<int> (0, y, list.getWidth(), kRowH);
        y += kRowH + 4;
        Dine::fillRounded (g, line.toFloat(), Dine::item, Dine::Radius::control);
        auto t = line.reduced (14, 0);
        t.removeFromRight (juce::jmax (78, row->restore->idealWidth()) + 24);

        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.5f, 500));
        g.drawText (row->when, t.removeFromLeft (92), juce::Justification::centredLeft);
        t.removeFromLeft (10);

        // A tune is the thing people come back to, so it is the thing that stands out.
        if (row->fromTune)
        {
            const int w = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), "TUNE") + 14;
            Dine::drawStatusChip (g, t.removeFromLeft (w).withSizeKeepingCentre (w, 17).toFloat(), "TUNE", Dine::accent);
            t.removeFromLeft (10);
        }
        g.setColour (row->sameConsole ? Dine::ink : Dine::ink4);
        g.setFont (Dine::text (13.0f, 500));
        g.drawText (row->sameConsole ? row->what : row->what + "   (different inputs)",
                    t, juce::Justification::centredLeft, true);
    }
}

void HistorySheet::resized()
{
    auto r = cardBounds().reduced (26, 26);
    r.removeFromTop (24 + 10 + 22 + 8);
    auto foot = r.removeFromBottom (Dine::Metric::button);
    r.removeFromBottom (14);
    doneButton.setBounds (foot.removeFromRight (juce::jmax (86, doneButton.idealWidth())));

    viewport.setBounds (r);
    const int total = int (rows.size()) * (kRowH + 4);
    list.setSize (r.getWidth() - (total > r.getHeight() ? 10 : 0), juce::jmax (total, r.getHeight()));
    int y = 0;
    for (auto& row : rows)
    {
        auto line = juce::Rectangle<int> (0, y, list.getWidth(), kRowH);
        const int w = juce::jmax (78, row->restore->idealWidth());
        row->restore->setBounds (line.removeFromRight (w + 12).withTrimmedRight (12)
                                     .withSizeKeepingCentre (w, Dine::Metric::control));
        y += kRowH + 4;
    }
}

void HistorySheet::mouseUp (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()) && onClose) onClose();
}

} // namespace livemix
