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
    favouriteButton.setFontPx (12.0f);
    favouriteButton.setTooltip ("Keeps the mix that is running, by name, with what it actually sounds like measured "
                                "beside it - so a later mix can be aimed at it the way it is aimed at a record.");
    favouriteButton.onClick = [this] { askForFavouriteName(); };
    addAndMakeVisible (favouriteButton);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    Dine::nativeScrolling (viewport);
    addAndMakeVisible (viewport);
    setInterceptsMouseClicks (true, true);
    rebuild();
}

// MARK AS FAVOURITE. A favourite is asked for by name, because a list of "Favourite 3" is a
// list nobody reads.
void HistorySheet::askForFavouriteName()
{
    nameDialog = std::make_unique<juce::AlertWindow> ("Mark this mix as a favourite", "", juce::MessageBoxIconType::NoIcon);
    nameDialog->addTextEditor ("name", "Sunday " + juce::Time::getCurrentTime().toString (false, true, false, true), "Name");
    nameDialog->addButton ("Mark as favourite", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (nameDialog == nullptr) return;
        const auto typed = nameDialog->getTextEditorContents ("name").trim();
        nameDialog.reset();
        if (result != 1) return;
        if (controller.markFavourite (typed.toStdString()))
        {
            services.touchSession();
            rebuild();
            resized();
            repaint();
        }
    }), false);
}

void HistorySheet::rebuild()
{
    rows.clear();
    list.removeAllChildren();
    const auto& all = controller.getCheckpoints();
    builtFor = all.size();
    builtFavourites = controller.numFavourites();

    std::vector<std::string> now;
    for (const auto& in : controller.getSession().inputs) now.push_back (in.name);

    // THE FAVOURITES FIRST: the mixes somebody said worked, rather than every place the
    // morning happened to pass through.
    if (controller.numFavourites() > 0)
    {
        auto caption = std::make_unique<Row>();
        caption->kind = Row::Kind::Caption;
        caption->what = "FAVOURITES";
        rows.push_back (std::move (caption));
    }
    for (int i = 0; i < controller.numFavourites(); ++i)
    {
        const auto& fav = controller.getFavourite (i);
        auto row = std::make_unique<Row>();
        row->kind = Row::Kind::Favourite;
        row->index = i;
        row->when = whenText (fav.whenMs);
        row->what = juce::String (fav.name);
        row->sameConsole = fav.inputs == now;
        row->measured = fav.sound.valid && fav.sound.masterLufs > -100.0f;

        row->restore = std::make_unique<DineButton> ("Bring back", DineButton::Style::Ghost);
        row->restore->setFontPx (11.5f);
        row->restore->setEnabled (row->sameConsole);
        row->restore->setTooltip (row->sameConsole ? "Put this whole mix back. UNDO takes it forward again."
                                                   : "This was kept with a different set of inputs.");
        row->restore->onClick = [this, i]
        {
            if (controller.recallFavourite (i)) { services.touchSession(); rebuild(); resized(); repaint(); }
        };
        list.addAndMakeVisible (*row->restore);

        row->aim = std::make_unique<DineButton> ("Aim at this", DineButton::Style::Ghost);
        row->aim->setFontPx (11.5f);
        row->aim->setEnabled (row->measured);
        row->aim->setTooltip (row->measured
            ? "The next TUNE MIX moves the master towards how this mix sounded, inside the profile's own bounds - "
              "exactly as it does for a record."
            : "This was kept before DLIVE had listened to anything, so there is nothing measured to aim at.");
        row->aim->onClick = [this, i]
        {
            if (controller.useFavouriteAsReference (i)) { services.touchSession(); repaint(); }
        };
        list.addAndMakeVisible (*row->aim);

        row->drop = std::make_unique<DineButton> ("Remove", DineButton::Style::Ghost);
        row->drop->setFontPx (11.5f);
        row->drop->setTooltip ("It stops being a favourite. The mix it held is still in the list below.");
        row->drop->onClick = [this, i]
        {
            controller.removeFavourite (i);
            services.touchSession();
            rebuild();
            resized();
            repaint();
        };
        list.addAndMakeVisible (*row->drop);
        rows.push_back (std::move (row));
    }
    if (controller.numFavourites() > 0)
    {
        auto caption = std::make_unique<Row>();
        caption->kind = Row::Kind::Caption;
        caption->what = "EVERY PLACE TO GO BACK TO";
        rows.push_back (std::move (caption));
    }

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
    if (controller.getCheckpoints().size() != builtFor || controller.numFavourites() != builtFavourites)
    {
        rebuild();
        repaint();
    }
}

int HistorySheet::rowHeight (const Row& r) noexcept { return r.kind == Row::Kind::Caption ? kCaptionH : kRowH; }

juce::Rectangle<int> HistorySheet::cardBounds() const
{
    int listH = 0;
    for (const auto& r : rows) listH += rowHeight (*r) + 4;
    listH = juce::jmax (kRowH, listH);
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
    Dine::drawText (g, "Mix history", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (10);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    Dine::drawText (g, rows.empty() ? juce::String ("Nothing yet. Every tune, scene and morning of mixing lands here.")
                             : juce::String (rows.size()) + (rows.size() == 1 ? " place to go back to. Where the mix is now is kept before it moves."
                                                                              : " places to go back to. Where the mix is now is kept before it moves."),
                r.removeFromTop (22), juce::Justification::centredLeft, true);
}

void HistorySheet::paintRows (juce::Graphics& g)
{
    int y = 0;
    for (const auto& row : rows)
    {
        auto line = juce::Rectangle<int> (0, y, list.getWidth(), rowHeight (*row));
        y += rowHeight (*row) + 4;
        if (row->kind == Row::Kind::Caption)
        {
            Dine::drawSection (g, line.reduced (2, 0).withTrimmedTop (8), row->what);
            continue;
        }
        Dine::fillRounded (g, line.toFloat(), row->kind == Row::Kind::Favourite ? Dine::selected : Dine::item, Dine::Radius::control);
        auto t = line.reduced (14, 0);
        if (row->kind == Row::Kind::Favourite)
            t.removeFromRight (juce::jmax (78, row->restore->idealWidth())
                             + juce::jmax (78, row->aim->idealWidth())
                             + juce::jmax (64, row->drop->idealWidth()) + 36);
        else
            t.removeFromRight (juce::jmax (78, row->restore->idealWidth()) + 24);

        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.5f, 500));
        Dine::drawText (g, row->when, t.removeFromLeft (92), juce::Justification::centredLeft);
        t.removeFromLeft (10);

        // A favourite that was never listened to is kept but not aimable at, and says so here
        // rather than only in a disabled button's tooltip.
        if (row->kind == Row::Kind::Favourite && ! row->measured)
        {
            const int w = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), "NOT MEASURED") + 14;
            Dine::drawStatusChip (g, t.removeFromLeft (w).withSizeKeepingCentre (w, 17).toFloat(), "NOT MEASURED", Dine::ink3);
            t.removeFromLeft (10);
        }
        // A tune is the thing people come back to, so it is the thing that stands out.
        if (row->fromTune)
        {
            const int w = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), "TUNE") + 14;
            Dine::drawStatusChip (g, t.removeFromLeft (w).withSizeKeepingCentre (w, 17).toFloat(), "TUNE", Dine::accent);
            t.removeFromLeft (10);
        }
        g.setColour (row->sameConsole ? Dine::ink : Dine::ink4);
        const auto font = Dine::text (13.0f, 500);
        g.setFont (font);
        // An entry that carries its reason - "Autopilot: LEAD -1.0 dB. The lead had come up..." -
        // says the move alone when the sentence will not fit, rather than cutting the reason off
        // mid-word. The whole of it is on LIVE's Autopilot card.
        auto what = row->what;
        if (Dine::textWidth (font, what) > t.getWidth() && what.contains (". "))
            what = what.upToFirstOccurrenceOf (". ", false, false);
        Dine::drawText (g, row->sameConsole ? what : what + "   (different inputs)",
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
    favouriteButton.setBounds (foot.removeFromLeft (juce::jmax (200, favouriteButton.idealWidth())));

    viewport.setBounds (r);
    int total = 0;
    for (const auto& row : rows) total += rowHeight (*row) + 4;
    list.setSize (r.getWidth() - (total > r.getHeight() ? 10 : 0), juce::jmax (total, r.getHeight()));
    int y = 0;
    for (auto& row : rows)
    {
        auto line = juce::Rectangle<int> (0, y, list.getWidth(), rowHeight (*row));
        y += rowHeight (*row) + 4;
        if (row->kind == Row::Kind::Caption) continue;
        auto place = [&line] (DineButton& b, int minW)
        {
            const int w = juce::jmax (minW, b.idealWidth());
            b.setBounds (line.removeFromRight (w + 12).withTrimmedRight (12).withSizeKeepingCentre (w, Dine::Metric::control));
        };
        if (row->kind == Row::Kind::Favourite)
        {
            place (*row->drop, 64);
            place (*row->aim, 78);
        }
        place (*row->restore, 78);
    }
}

void HistorySheet::mouseUp (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()) && onClose) onClose();
}

} // namespace livemix
