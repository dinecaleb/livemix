#include "FavouritesPage.h"
#include "UI/Widgets.h"
#include "Core/StyleId.h"

namespace livemix
{

// ---------------------------------------------------------------------------- a card
// One favourite: its name, when it was kept and what it was aiming at, then the five
// relationships that are what anybody actually means when they say they liked a mix, then its
// two verbs. A card is 368 wide in the design and they wrap across the page.
class FavouritesPage::Card : public juce::Component
{
public:
    static constexpr int kWidth = 372, kHeight = 440;

    Card (MixController& c, int favouriteIndex) : controller (c), index (favouriteIndex)
    {
        addAndMakeVisible (aimButton);
        addAndMakeVisible (restoreButton);
        addAndMakeVisible (moreButton);
        restoreButton.setStyle (DineButton::Style::Standard);
        moreButton.setTooltip ("Rename or delete this favourite.");
        setInterceptsMouseClicks (true, true);
    }

    DineButton aimButton { "Aim TUNE MIX at this", DineButton::Style::Filled };
    DineButton restoreButton { "Restore mix", DineButton::Style::Standard };
    DineButton moreButton { "More", DineButton::Style::Ghost };

    void setAimed (bool a)
    {
        if (a == aimed) return;
        aimed = a;
        aimButton.setButtonText (aimed ? "Aimed at" : "Aim TUNE MIX at this");
        aimButton.setStyle (aimed ? DineButton::Style::Standard : DineButton::Style::Filled);
        aimButton.setEnabled (! aimed);
        repaint();
        resized();
    }
    bool isAimed() const noexcept { return aimed; }

    void paint (juce::Graphics& g) override
    {
        const auto& f = controller.getFavourite (index);
        auto r = getLocalBounds().toFloat();
        Dine::drawCard (g, r, Dine::card, aimed ? Dine::accent : Dine::hair);
        if (aimed) Dine::hairlineRounded (g, r.reduced (0.5f), Dine::accent, Dine::Radius::card);

        auto inner = getLocalBounds().reduced (20, 18);

        // ---- the head: a star, the name, and the AIMED AT badge while it is the target
        auto head = inner.removeFromTop (24);
        if (aimed)
        {
            const auto badgeFont = Dine::caps (10.0f, 0.04f, 600);
            const int w = Dine::textWidth (badgeFont, "AIMED AT") + 16;
            auto badge = head.removeFromRight (w).withSizeKeepingCentre (w, 18).toFloat();
            Dine::fillRounded (g, badge, Dine::accent.withAlpha (0.20f), Dine::Radius::chip);
            g.setColour (Dine::accent);
            g.setFont (badgeFont);
            Dine::drawText (g, "AIMED AT", badge.toNearestInt(), juce::Justification::centred);
        }
        g.setColour (aimed ? Dine::accent : Dine::ink3);
        drawStar (g, head.removeFromLeft (16).withSizeKeepingCentre (13, 13).toFloat());
        head.removeFromLeft (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        Dine::drawText (g, f.name, head, juce::Justification::centredLeft, true);

        inner.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f, 500));
        Dine::drawText (g, subtitle(), inner.removeFromTop (16), juce::Justification::centredLeft, true);
        inner.removeFromTop (16);

        // ---- what it sounded like
        if (! f.sound.valid)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.5f));
            Dine::drawFittedText (g, "This mix was kept before DINE had listened to anything, so there is nothing "
                                     "measured to aim at. Mark it again once it has been tuned.",
                                  inner.removeFromTop (60), juce::Justification::topLeft, 3);
            return;
        }

        struct Line { const char* label; juce::String value; };
        const Line lines[5] = {
            { "Lead over band",   db (f.sound.metric ("lead_over_band_db")) },
            { "Lead over BGV",    db (f.sound.metric ("lead_over_bgv_db", f.sound.metric ("lead_over_backing_db"))) },
            { "Speech over master", db (f.sound.metric ("speech_over_master_db")) },
            { "Drums against band", db (f.sound.metric ("drums_against_band_db", f.sound.metric ("kit_against_bass_db"))) },
            { "Master",           juce::String (f.sound.masterLufs, 1) + " LUFS" },
        };
        for (const auto& l : lines)
        {
            auto row = inner.removeFromTop (48);
            auto text = row.withTrimmedBottom (1);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (13.0f));
            const auto valueFont = Dine::mono (12.0f, 500);
            const int w = Dine::textWidth (valueFont, l.value);
            auto value = text.removeFromRight (w);
            Dine::drawText (g, l.label, text, juce::Justification::centredLeft, true);
            g.setColour (Dine::ink);
            g.setFont (valueFont);
            Dine::drawText (g, l.value, value, juce::Justification::centredRight);
            g.setColour (Dine::hair);
            g.fillRect (row.removeFromBottom (1));
        }
    }

    void resized() override
    {
        auto foot = getLocalBounds().reduced (20, 18).removeFromBottom (Dine::Metric::button);
        const int rw = restoreButton.idealWidth();
        restoreButton.setBounds (foot.removeFromRight (rw));
        foot.removeFromRight (10);
        const int aw = aimButton.idealWidth();
        aimButton.setBounds (foot.removeFromLeft (aw));
        const int mw = moreButton.idealWidth() + 4;
        moreButton.setBounds (getLocalBounds().reduced (14, 14).removeFromTop (Dine::Metric::button).removeFromRight (mw));
    }

    void lookAndFeelChanged() override { repaint(); }

private:
    juce::String subtitle() const
    {
        const auto& f = controller.getFavourite (index);
        juce::String when;
        if (f.whenMs > 0)
        {
            const juce::Time t (f.whenMs);
            when = t.toString (true, false, false, true);
        }
        const auto style = juce::String (styleProfileName (controller.getSession().profile));
        return when.isEmpty() ? style : when + " " + Glyph::dot() + " " + style;
    }

    static juce::String db (float v)
    {
        if (v <= -900.0f) return juce::String (Glyph::dash());
        return (v >= 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    }

    static void drawStar (juce::Graphics& g, juce::Rectangle<float> box)
    {
        juce::Path p;
        const auto c = box.getCentre();
        const float outer = box.getWidth() * 0.5f, inner = outer * 0.44f;
        for (int i = 0; i < 10; ++i)
        {
            const float a = float (i) * juce::MathConstants<float>::pi / 5.0f - juce::MathConstants<float>::halfPi;
            const float rad = (i % 2 == 0) ? outer : inner;
            const auto pt = c.translated (std::cos (a) * rad, std::sin (a) * rad);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        p.closeSubPath();
        g.fillPath (p);
    }

    MixController& controller;
    int index;
    bool aimed = false;
};

// ---------------------------------------------------------------------------- the page
FavouritesPage::FavouritesPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    addAndMakeVisible (markButton);
    markButton.setIcon (Dine::Icon::Check);
    markButton.setTooltip ("Keep the mix that is running now, with what it measured, so a later mix can be aimed at it.");
    markButton.onClick = [this] { markCurrent(); };

    Dine::nativeScrolling (view);
    view.setViewedComponent (&holder, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
    setOpaque (true);
}

FavouritesPage::~FavouritesPage() = default;

void FavouritesPage::markCurrent()
{
    const int n = controller.numFavourites();
    juce::String suggested = services.currentSessionName();
    if (suggested.isEmpty()) suggested = "Mix " + juce::String (n + 1);

    nameDialog = std::make_unique<juce::AlertWindow> ("Mark this mix as a favourite",
                                                      "What should it be called? TUNE MIX and Autopilot can aim at it by name.",
                                                      juce::MessageBoxIconType::NoIcon);
    nameDialog->addTextEditor ("name", suggested, "Name");
    nameDialog->addButton ("Mark it", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (nameDialog == nullptr) return;
        const auto name = nameDialog->getTextEditorContents ("name").trim();
        nameDialog.reset();
        if (result != 1 || name.isEmpty()) return;
        if (! controller.markFavourite (name.toStdString()))
        {
            if (onToast) onToast ("There is no mix to keep yet. Tune the mix first, then mark it.");
            return;
        }
        services.touchSession();
        seenCount = -1;
        refresh();
        if (onToast) onToast ("\"" + name + "\" is a favourite mix now.");
    }), false);
}

void FavouritesPage::rebuild()
{
    cards.clear();
    const int n = controller.numFavourites();
    for (int i = 0; i < n; ++i)
    {
        auto card = std::make_unique<Card> (controller, i);
        card->aimButton.onClick = [this, i]
        {
            if (! controller.useFavouriteAsReference (i))
                return;   // the controller has already said why
            services.touchSession();
            seenAimed = -2;
            refresh();
            if (onToast) onToast ("The next TUNE MIX aims at \"" + juce::String (controller.getFavourite (i).name) + "\".");
        };
        card->restoreButton.onClick = [this, i]
        {
            if (! controller.recallFavourite (i))
            {
                if (onToast) onToast ("That mix was kept on a different set of inputs, so it cannot be put back here.");
                return;
            }
            services.touchSession();
            if (onToast) onToast ("\"" + juce::String (controller.getFavourite (i).name) + "\" is on the console again.");
        };
        card->moreButton.onClick = [this, i, b = &card->moreButton]
        {
            juce::PopupMenu m;
            m.addItem (1, "Rename" + juce::String (Glyph::ellip()));
            m.addItem (2, "Delete");
            juce::Component::SafePointer<FavouritesPage> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b), [safe, i] (int r)
            {
                if (safe == nullptr || i >= safe->controller.numFavourites()) return;
                const juce::String was (safe->controller.getFavourite (i).name);
                if (r == 1)
                    Dine::askForName ("Rename this favourite", "TUNE MIX and Autopilot aim at it by this name.", was, "Rename",
                                      [safe, i] (const juce::String& name)
                    {
                        if (safe == nullptr || i >= safe->controller.numFavourites()) return;
                        safe->controller.renameFavourite (i, name.toStdString());
                        safe->services.touchSession();
                        safe->rebuild();
                        if (safe->onToast) safe->onToast ("Renamed to \"" + name + "\".");
                    });
                else if (r == 2)
                {
                    safe->controller.removeFavourite (i);      // the controller says the mix is still in the history
                    safe->services.touchSession();
                    safe->rebuild();
                }
            });
        };
        holder.addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }
    seenCount = n;
    resized();
}

void FavouritesPage::refresh()
{
    if (controller.numFavourites() != seenCount) rebuild();

    // Which one the mix is aimed at, by the name the reference carries.
    const auto& ref = controller.getReference();
    int aimed = -1;
    if (ref.valid)
        for (int i = 0; i < int (cards.size()); ++i)
            if (juce::String (controller.getFavourite (i).name) == juce::String (ref.name)) { aimed = i; break; }
    if (aimed != seenAimed)
    {
        seenAimed = aimed;
        for (int i = 0; i < int (cards.size()); ++i) cards[size_t (i)]->setAimed (i == aimed);
    }
    markButton.setEnabled (! controller.getSession().inputs.empty());
    repaint();
}

void FavouritesPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    auto r = getLocalBounds().reduced (Dine::Metric::padX, 0);
    r.removeFromTop (28);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, "Favourite mixes", r.removeFromTop (28).withTrimmedRight (markButton.getWidth() + 24),
                    juce::Justification::centredLeft, true);
    r.removeFromTop (2);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    Dine::drawText (g, "Mixes you marked as good. Tune and Autopilot aim at how things sit against each other, "
                       "not at fader positions.",
                    r.removeFromTop (18), juce::Justification::centredLeft, true);

    if (cards.empty())
    {
        r.removeFromTop (60);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, "Nothing is marked yet. When a mix sounds right, mark it: DINE keeps the whole mix "
                                 "and what it measured, and a later tune can be aimed at it.",
                              r.removeFromTop (48).withWidth (juce::jmin (620, r.getWidth())),
                              juce::Justification::topLeft, 2);
    }
}

void FavouritesPage::resized()
{
    auto r = getLocalBounds().reduced (Dine::Metric::padX, 0);
    auto head = r.removeFromTop (28 + 28 + 2 + 18);
    const int mw = markButton.idealWidth();
    markButton.setBounds (head.removeFromRight (mw).withY (28).withHeight (Dine::Metric::button));

    r.removeFromTop (24);
    view.setBounds (r.withTrimmedBottom (Dine::Metric::padY));

    // The cards wrap: as many across as the width takes, 20 pt apart.
    const int across = juce::jmax (1, (view.getWidth() + 20) / (Card::kWidth + 20));
    int x = 0, y = 0;
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const int col = int (i) % across, row = int (i) / across;
        x = col * (Card::kWidth + 20);
        y = row * (Card::kHeight + 20);
        cards[i]->setBounds (x, y, Card::kWidth, Card::kHeight);
    }
    const int rows = cards.empty() ? 0 : (int (cards.size()) + across - 1) / across;
    holder.setBounds (0, 0, juce::jmax (view.getWidth(), across * (Card::kWidth + 20)),
                      juce::jmax (view.getHeight(), rows * (Card::kHeight + 20)));
}

} // namespace livemix
