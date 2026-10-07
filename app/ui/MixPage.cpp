#include "MixPage.h"
#include "ReferenceSheet.h"
#include "Core/DbUtils.h"
#include "Profiles/Profile.h"

namespace livemix
{

namespace
{
    // TUNE reads the mix as its groups: every group bus in console order, then the FX
    // returns as one more tile. Both counts come from MixBus, so a new group bus becomes a
    // tile here without being wired in by hand.
    constexpr int kGroupBuses = int (MixBus::Master);
    constexpr int kGroupTiles = kGroupBuses + 1;
    // ... and behind the FX tile, one tile per effect return. Pressing the FX tile opens the
    // row out to them; Back to groups closes it again. They are never on the row together with
    // the groups, so no fader on the page ever gets narrower for them.
    constexpr int kReturnTiles = int (FxSlot::Count);
    constexpr int kAllTiles = kGroupTiles + kReturnTiles;
    FxSlot returnSlot (int tile) noexcept { return FxSlot (tile - kGroupTiles); }

    // A tile's position is the console's order, not the enum's: LEAD sits with the voices
    // where an engineer looks for it, rather than at the end where it was appended.
    MixBus groupBus (int tile) noexcept { return mixBusInDisplayOrder (tile); }

    // Sentence case, because a group is a thing and not a verb. BGV and FX stay as they are
    // written: they are initialisms, not shouting.
    juce::String groupName (int i)
    {
        if (i >= kGroupTiles)
            return returnSlot (i) == FxSlot::BgvHall ? juce::String ("BGV Hall") : juce::String (fxSlotName (returnSlot (i)));
        if (i >= kGroupBuses) return "FX returns";
        const juce::String raw (mixBusName (groupBus (i)));
        return raw.length() <= 3 ? raw.toUpperCase()
                                 : raw.substring (0, 1).toUpperCase() + raw.substring (1).toLowerCase();
    }

    // The same group named in as few letters as it can be, for a tile too narrow for the
    // whole word: the long names lose all but their first three letters.
    juce::String groupNameBrief (int i)
    {
        if (i >= kGroupTiles)
            switch (returnSlot (i))
            {
                case FxSlot::VocalPlate: return "Plate";
                case FxSlot::VocalDelay: return "Delay";
                case FxSlot::BgvHall:    return "Hall";
                case FxSlot::SnarePlate: return "Snare";
                case FxSlot::DrumRoom:   return "Room";
                case FxSlot::BandHall:   return "Band";
                case FxSlot::Count:      break;
            }
        if (i >= kGroupBuses) return "FX";
        // A name longer than five letters keeps its first three (Amb, Spe); the tooltip and
        // every wider place still say it whole.
        const auto full = groupName (i);
        return full.length() > 5 ? full.substring (0, 3) : full;
    }

    juce::Colour groupColour (int i) noexcept
    {
        return i >= 0 && i < kGroupBuses ? Dine::busTint (groupBus (i)) : i >= kGroupTiles ? Dine::keyFx : Dine::ink2;
    }

    // WHAT A TUNE IS ABOUT, in one place. The listen card and the result card both say it, and
    // saying it twice in two ways is how they come to disagree.
    juce::String tuneVerb (const MixController& c, bool live)
    {
        if (live) return "TUNE LIVE MIX";
        if (c.isTuningBus()) return "TUNE " + juce::String (mixBusName (c.getTuningBus())).toUpperCase();
        if (c.isTuningStrips()) return "TUNE " + juce::String (int (c.getTuningStrips().size())) + " CHANNELS";
        if (c.isTuningChannel()) return "TUNE CHANNEL";
        return "TUNE MIX";
    }

    juce::String dbText (float db)
    {
        if (db <= -119.0f) return Glyph::dash();
        return (db >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (db), 1);
    }
}

// ------------------------------------------------------------------ GroupTile
// ONE GROUP, AS A COLUMN.
//
// A group is a fader, and a fader stands up. TUNE used to draw its seven groups as horizontal
// rows, which reads as a list of settings; LIVE has always drawn them as columns, which reads
// as a console - and the two workspaces are looking at exactly the same faders. So they are
// the same shape in both: the group's colour across the top, its name and what it is set to,
// a standing fader with its meter beside it, TUNE for this group alone, and M and S along the
// foot. Nothing about the sound changed with the shape.
class MixPage::GroupTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    // `minWidth` is the narrowest a tile can be and still be a tile: the fader, its meter
    // and the space between them, plus the tighter padding `paddingX` gives back below it.
    static constexpr int width = 140, height = 246, minWidth = 52;

    GroupTile (MixController& c, int index)
        : controller (c), group (index),
          muteButton ("M", Dine::keyMute), soloButton ("S", Dine::keySolo)
    {
        if (index < kGroupBuses)
            setTooltip ("TUNE listens to the whole band and sets " + groupName (index)
                        + " alone: its channels and its group chain. Nothing else in the mix, and not the master, moves.");
        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        Dine::dragOnly (fader);
        fader.setRange (-60.0, 12.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.getProperties().set ("dineFader", true);
        fader.setTooltip (isFx() ? "Level for every effect return together. Double-click for 0.0 dB, which is what TUNE MIX set."
                          : isReturn() ? "This effect's own level, on top of what TUNE MIX set for it. Double-click for 0.0 dB."
                                       : "Level for the whole group. Double-click for 0.0 dB.");
        if (isFx()) setTooltip ("Each effect opens the row out to the reverbs and the delay, each on its own fader.");
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (isFx())          controller.setFxReturn (float (fader.getValue()));
            else if (isReturn()) controller.setFxSlotReturn (returnSlot (group), float (fader.getValue()));
            else                 controller.setBusFader (groupBus (group), float (fader.getValue()));
            repaint();
        };

        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);
        muteButton.setTooltip (isFx() ? "Muted: the effects are not heard"
                               : isReturn() ? "Muted: this effect is not heard. The others stay as they are"
                                            : "Muted: the whole group is not heard");
        soloButton.setTooltip (isFx() ? "Soloed: the effect returns and nothing else"
                               : isReturn() ? "Soloed: this effect on its own, in your listen only"
                                            : "Soloed: this group and nothing else");
        muteButton.onClick = [this]
        {
            if (isFx())          controller.setFxMute (! controller.getBase().fxMute);
            else if (isReturn()) controller.setFxSlotMute (returnSlot (group), ! controller.getBase().fx[size_t (returnSlot (group))].mute);
            else                 controller.setGroupMuted (groupBus (group), ! controller.isGroupMuted (groupBus (group)));
        };
        soloButton.onClick = [this]
        {
            if (isFx()) return;
            if (isReturn()) { controller.setFxSolo (returnSlot (group), ! controller.getBase().fx[size_t (returnSlot (group))].solo); return; }
            controller.setBusSolo (groupBus (group), ! controller.getBase().buses[size_t (groupBus (group))].solo);
        };
        soloButton.setVisible (! isFx());
    }

    void set (bool isUsed, float peakDb, float holdDb, bool clipped, bool isMuted, float faderDb, int heardState)
    {
        updating = true;
        const bool wasUsed = used;
        bool body = used != isUsed || heard != heardState || muted != isMuted;
        used = isUsed; heard = heardState; muted = isMuted;
        if (wasUsed != used) resized();
        meter.setLevels (peakDb, holdDb, clipped);
        meter.setMuted (isMuted || ! used);
        if (std::fabs (faderDb - float (fader.getValue())) > 0.01f) { fader.setValue (faderDb, juce::dontSendNotification); body = true; }
        fader.setEnabled (used);
        muteButton.setOn (isMuted);
        if (! isFx())
        {
            const bool s = isReturn() ? controller.getBase().fx[size_t (returnSlot (group))].solo
                                      : controller.getBase().buses[size_t (groupBus (group))].solo;
            if (s != soloed) { soloed = s; body = true; }
            soloButton.setOn (s);
        }
        updating = false;
        if (body) repaint();
    }

    // TUNE <GROUP>, the same verb the input rail carries for one channel: DINE listens to
    // the whole console and applies only this group, so the band can be tuned during the
    // song and the pastor during the sermon without either moving the other.
    std::function<void()> onTune;
    // The FX tile's own verb row: Each effect opens the row out to the returns, Back closes it.
    std::function<void()> onOpen;
    void setOpen (bool o) { if (o != open) { open = o; resized(); repaint(); } }

    void mouseEnter (const juce::MouseEvent&) override { if (! verbRect.isEmpty()) repaint (verbRect); }
    void mouseExit  (const juce::MouseEvent&) override { if (! verbRect.isEmpty()) repaint (verbRect); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor ((canTune() || canOpen()) && verbRect.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                                        : juce::MouseCursor::NormalCursor);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! verbRect.contains (e.getPosition())) return;
        if (canTune() && onTune) onTune();
        else if (canOpen() && onOpen) onOpen();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        Dine::fillRounded (g, r.toFloat(), muted ? Dine::refuse : soloed ? Dine::soloGround : Dine::card, Dine::Radius::card);
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path round; round.addRoundedRectangle (r.toFloat(), Dine::Radius::card);
            g.reduceClipRegion (round);
            g.setColour (used ? groupColour (group) : Dine::ink4);
            g.fillRect (r.removeFromTop (3));
        }

        auto inner = getLocalBounds().reduced (paddingX(), 12);
        auto head = inner.removeFromTop (18);
        // The lamp that says this group is being listened to, beside its name.
        if (heard != 0)
        {
            g.setColour (heard == 2 ? Dine::ok : Dine::warn);
            g.fillEllipse (head.removeFromRight (6).withSizeKeepingCentre (5, 5).toFloat());
            head.removeFromRight (5);
        }
        g.setColour (! used ? Dine::ink4 : muted ? Dine::ink3 : Dine::ink);
        // A column is narrow, and a group's name is the one thing on it that must be readable.
        // A smaller tile carries smaller type, and where the whole name still does not fit the
        // group says it in fewer letters. It is never squeezed and never cut off.
        {
            const auto nameFont = Dine::text (nameSizePx(), 600);
            g.setFont (nameFont);
            const auto full = groupName (group);
            const bool whole = Dine::textWidth (nameFont, full) <= head.getWidth();
            Dine::drawFittedText (g, whole ? full : groupNameBrief (group), head, juce::Justification::centredLeft, 1);
        }

        auto sub = inner.removeFromTop (16);
        g.setColour (! used ? Dine::ink4 : muted ? Dine::warn : soloed ? Dine::accent : Dine::ink3);
        const auto subFont = ! used || muted ? Dine::caps (10.0f, 0.04f, 600) : Dine::mono (11.0f, 500);
        g.setFont (subFont);
        {
            // The state in words, and the one-word form of it for a tile too narrow for two.
            const juce::String full = ! used ? "NOT USED" : muted ? "NOT HEARD" : dbText (float (fader.getValue()));
            const juce::String brief = ! used ? "OFF" : muted ? "MUTED" : full;
            Dine::drawText (g, Dine::textWidth (subFont, full) <= sub.getWidth() ? full : brief,
                            sub, juce::Justification::centredLeft, true);
        }

        if (canTune())
        {
            const bool over = isMouseOver (true) && verbRect.contains (getMouseXYRelative());
            Dine::fillRounded (g, verbRect.toFloat(), over ? Dine::controlHot : Dine::control, Dine::Radius::control);
            g.setColour (over ? Dine::accent : Dine::ink3);
            g.setFont (Dine::caps (10.0f, 0.06f, 600));
            Dine::drawText (g, "TUNE", verbRect, juce::Justification::centred);
        }
        else if (canOpen())
        {
            const bool over = isMouseOver (true) && verbRect.contains (getMouseXYRelative());
            Dine::fillRounded (g, verbRect.toFloat(), open ? Dine::selected : over ? Dine::controlHot : Dine::control, Dine::Radius::control);
            g.setColour (over || open ? Dine::accent : Dine::ink2);
            const auto f = Dine::text (11.0f, 600);
            g.setFont (f);
            const juce::String full = open ? "Back" : "Each effect", brief = open ? "Back" : "Each";
            Dine::drawText (g, Dine::textWidth (f, full) <= verbRect.getWidth() - 6 ? full : brief, verbRect, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (paddingX(), 12);
        inner.removeFromTop (18 + 16 + 10);
        auto keys = inner.removeFromBottom (20);
        inner.removeFromBottom (8);
        // The verb's row is taken out of every tile, not only the ones that have one: eight
        // faders that are the same length read as one console, and one that is longer because
        // its group happens not to be tunable reads as a mistake.
        auto verb = inner.removeFromBottom (20);
        inner.removeFromBottom (10);
        verbRect = canTune() || canOpen() ? verb : juce::Rectangle<int>();

        // the throw: the fader standing, its meter beside it
        const int faderW = 22, meterW = 6;
        auto pair = inner.withSizeKeepingCentre (faderW + 10 + meterW, juce::jmax (40, inner.getHeight()));
        fader.setBounds (pair.removeFromLeft (faderW));
        pair.removeFromLeft (10);
        meter.setBounds (pair);

        const int each = soloButton.isVisible() ? (keys.getWidth() - 4) / 2 : keys.getWidth();
        muteButton.setBounds (keys.removeFromLeft (each).withSizeKeepingCentre (each, 20));
        if (soloButton.isVisible()) { keys.removeFromLeft (4); soloButton.setBounds (keys.withSizeKeepingCentre (each, 20)); }
    }

private:
    // A narrow console gives its padding up before anything on the tile gives up a letter.
    int paddingX() const noexcept { return getWidth() >= 96 ? 12 : getWidth() >= 64 ? 8 : getWidth() >= 44 ? 6 : 3; }
    float nameSizePx() const noexcept { return getWidth() >= 96 ? 13.0f : getWidth() >= 64 ? 12.0f : 11.5f; }
    bool isFx() const noexcept { return group == kGroupBuses; }
    bool isReturn() const noexcept { return group >= kGroupTiles; }
    // The returns are not a group of sources, so there is nothing to listen to and tune.
    bool canTune() const noexcept { return used && ! isFx() && ! isReturn(); }
    bool canOpen() const noexcept { return used && isFx() && onOpen != nullptr; }

    MixController& controller;
    int group;
    bool used = false, muted = false, soloed = false, updating = false, open = false;
    int heard = 0;
    juce::Rectangle<int> verbRect;
    DineMeter meter { DineMeter::Style::Bar };
    juce::Slider fader;
    DineKey muteButton, soloButton;
};

// ------------------------------------------------------------------ VoiceRow
// WHAT THIS MICROPHONE IS DOING, on TUNE's own panel (design: `05 - Tune`, 71:12025): a lamp
// in the group's colour, the channel's name, and one segment saying whether somebody is
// speaking into it or singing into it. It is a starting point, not a preset: what lands is
// what the profile gives a source of that kind, and everything stays editable. The full four
// jobs - speaking, singing lead, singing backing, choir - are on the MIXER strip's menu and on
// the TRACKS header's menu; here the choice is the one a volunteer makes mid-service.
class MixPage::VoiceRow : public juce::Component
{
public:
    static constexpr int height = 34;

    VoiceRow (const juce::String& n, juce::Colour c) : name (n), tint (c)
    {
        addAndMakeVisible (speaking);
        addAndMakeVisible (singing);
        for (auto* b : { &speaking, &singing }) { b->setFontPx (11.0f); b->setPadX (10); }
        speaking.setTooltip ("Levelled to a spoken target, held steady, gated and de-essed, and on SPEECH where it "
                             "has its own fader for the whole service.");
        singing.setTooltip ("Levelled to a sung target, never gated, and given a pocket in the band.");
    }

    void setJob (bool isSpeaking)
    {
        speaking.setToggleState (isSpeaking, juce::dontSendNotification);
        singing.setToggleState (! isSpeaking, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), 20);
        g.setColour (tint);
        g.fillEllipse (r.removeFromLeft (7).withSizeKeepingCentre (7, 7).toFloat());
        r.removeFromLeft (9);
        r.removeFromRight (speaking.getWidth() + singing.getWidth() + 10);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f, 500));
        Dine::drawText (g, name, r, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), Dine::Metric::control);
        const int sw = juce::jmax (58, singing.idealWidth());
        singing.setBounds (r.removeFromRight (sw));
        const int pw = juce::jmax (66, speaking.idealWidth());
        speaking.setBounds (r.removeFromRight (pw));
    }

    DineButton speaking { "Speaking", DineButton::Style::Segment };
    DineButton singing { "Singing", DineButton::Style::Segment };
    juce::String name;
    juce::Colour tint;
};

// ------------------------------------------------------------------ SidePanel
// The right column, in its own scroll: the verbs, a card that says what the pad under the
// pointer does (or which pad is off the plan), then MIX HEALTH wrapped to the panel's width.
// The buttons are the page's; they are laid out here so the stack is measured in one place.
class MixPage::SidePanel : public juce::Component
{
public:
    // Buffered: the panel's cards are wrapped text, and they change a few times a minute, not
    // every time something beside them repaints.
    explicit SidePanel (MixPage& p) : page (p) { setBufferedToImage (true); }

    static constexpr int kPad = 18;

    // Measures the stack for a width; `place` also positions the buttons. Returns the height
    // needed. The order is the design's (`05 - Tune`, 71:12025): Tune, then Voices, then what
    // the mix is aimed at, then Mix health.
    // v4's order (docs/design/v4): what to tune, TUNE MIX, its sentence, TUNE LIVE MIX, Match
    // to reference and Check inputs, Aim at, Mix health as a card, Voices as a card; then the
    // verbs v3 kept on this panel (docs/DESIGN-V3.md section 5) and the pad card.
    int layoutFor (int width, bool place)
    {
        const int inner = width - kPad * 2;
        int y = kPad;
        auto put = [&] (juce::Component& c, juce::Rectangle<int> r) { if (place) c.setBounds (r); };

        tuneCap = {};
        scopeCap = {};
        {
            const int track = Dine::Metric::control;
            scopeTrack = { kPad, y, inner, track };
            const int each = (inner - 4) / 3;
            for (int i = 0; i < 3; ++i)
                if (page.scopeTabs[size_t (i)] != nullptr)
                    put (*page.scopeTabs[size_t (i)], { kPad + 2 + i * each, y + 2, each, track - 4 });
            y += track + 12;
        }
        put (page.tuneButton, { kPad, y, inner, 50 });            y += 50 + 8;
        scopeNote = { kPad, y, inner, textHeight (Dine::text (11.5f), scopeSentence(), inner) };
        y += scopeNote.getHeight() + 10;
        put (page.liveTuneButton, { kPad, y, inner, 38 });        y += 38 + 10;
        const int half = (inner - 8) / 2;
        put (page.referenceButton, { kPad, y, half, 32 });
        put (page.checkButton, { kPad + half + 8, y, inner - half - 8, 32 });
        y += 32 + 12;

        // ---- Aim at
        aimCap = { kPad, y, inner, 16 };                          y += 16 + 4;
        put (page.aimButton, { kPad, y, inner, 34 });             y += 34 + 12;

        // ---- Mix health: a card - the score out of a hundred, a bar, the notes as bullets
        {
            const int textW = inner - 24 - 14;
            notes.clear();
            int h = 14 + 22 + 10 + 4 + 12;
            for (const auto& n : page.controller.getMixHealthNotes())
            {
                const juce::String text (n);
                const auto lower = text.toLowerCase();
                const bool bad = lower.contains ("clipping") || lower.contains ("barely") || lower.contains ("not heard");
                const bool watch = lower.contains ("preamp") || lower.contains ("digital");
                const int th = textHeight (Dine::text (12.0f), text, textW);
                notes.push_back ({ text, bad ? Dine::crit : watch ? Dine::warn : Dine::ok, { kPad + 12 + 14, y + h, textW, th } });
                h += th + 8;
            }
            // The status line often is the notes, joined: said once, as the bullets.
            bool statusIsNote = false;
            for (const auto& n : notes) if (n.text == page.status || page.status.contains (n.text)) statusIsNote = true;
            statusBox = {};
            if (! statusIsNote && page.status.isNotEmpty())
            {
                const int th = textHeight (Dine::text (12.0f), page.status, inner - 24);
                statusBox = { kPad + 12, y + h, inner - 24, th };
                h += th + 8;
            }
            healthCard = { kPad, y, inner, h + 6 };
            y += healthCard.getHeight() + 12;
        }

        // ---- Voices: a card of rows
        voicesCard = {};
        if (! page.voiceRows.empty())
        {
            const int top = y;
            y += 14;
            voicesCap = { kPad + 12, y, inner - 24, 18 };          y += 18 + 8;
            for (auto& v : page.voiceRows) { put (*v, { kPad + 12, y, inner - 24, VoiceRow::height }); y += VoiceRow::height; }
            voicesNote = { kPad + 12, y + 4, inner - 24, textHeight (Dine::text (11.0f), kVoicesNote, inner - 24) };
            y += voicesNote.getHeight() + 4 + 12;
            voicesCard = { kPad, top, inner, y - top };
            y += 12;
        }

        // ---- the verbs v3 kept here: Mix Buddy, Undo / Redo, Mix history, the Inspector
        put (page.chatButton, { kPad, y, inner, 30 });            y += 30 + 8;
        put (page.undoButton, { kPad, y, half, 30 });
        put (page.redoButton, { kPad + half + 8, y, inner - half - 8, 30 });
        y += 30 + 8;
        put (page.historyButton, { kPad, y, half, 30 });
        put (page.advancedButton, { kPad + half + 8, y, inner - half - 8, 30 });
        y += 30 + 10;
        stamp = { kPad, y, inner, 16 };                           y += 16 + kPad;

        // the pad card. The body is measured at exactly the width it is drawn at - the card
        // reduced by 16 either side - because a measure two pixels wider than the draw wraps
        // to one line fewer than it paints, and the paint then runs out of the card.
        {
            const int textW = inner - 16 * 2;
            headingH = juce::jmax (14, int (std::ceil (Dine::caps (10.5f, 0.04f, 600).getHeight())));
            const int bodyH = juce::jmax (16, textHeight (Dine::text (12.5f), body, textW));
            card = { kPad, y, inner, 14 + headingH + 8 + bodyH + 14 };
            y += card.getHeight() + kPad;
        }
        healthCap = {};
        scoreBox = {};
        return y + kPad;
    }

    juce::String scopeSentence() const
    {
        switch (page.wantedScope)
        {
            case 1:  return "Listens to the whole console and sets one group alone: its channels and its group chain. "
                            "Nothing else in the mix, and not the master, moves.";
            case 2:  return "Listens to the whole console and sets only the channels you pick. The groups and the "
                            "master stay where they are.";
            default: return "Listens to the band for about 30 seconds.";
        }
    }

    void setCard (const juce::String& h, const juce::String& b) { heading_ = h; body = b; }

    void resized() override { layoutFor (getWidth(), true); }

    void paint (juce::Graphics& g) override
    {
        auto caption = [&g] (juce::Rectangle<int> r, const juce::String& text)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 500));
            Dine::drawText (g, text, r, juce::Justification::centredLeft, true);
        };
        auto cardAt = [&g] (juce::Rectangle<int> r)
        {
            Dine::fillRounded (g, r.toFloat(), Dine::card, 12.0f);
            Dine::hairlineRounded (g, r.toFloat().reduced (0.5f), juce::Colours::white.withAlpha (0.06f), 11.5f);
        };

        Dine::drawSegmentTrack (g, scopeTrack);
        {
            auto note = scopeNote;
            drawWrapped (g, scopeSentence(), Dine::text (11.5f), Dine::ink3, note, juce::Justification::centredTop);
        }
        caption (aimCap, "Aim at");

        // ---- Mix health
        if (! healthCard.isEmpty())
        {
            cardAt (healthCard);
            auto r = healthCard.reduced (12, 14);
            auto head = r.removeFromTop (22);
            const auto scoreFont = Dine::text (22.0f, 700);
            const juce::String score = page.health > 0 ? juce::String (page.health) : juce::String (Glyph::dash());
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f));
            const int outW = Dine::textWidth (Dine::text (11.0f), "/ 100");
            Dine::drawText (g, "/ 100", head.removeFromRight (outW), juce::Justification::bottomRight);
            head.removeFromRight (5);
            g.setColour (Dine::ink);
            g.setFont (scoreFont);
            Dine::drawText (g, score, head.removeFromRight (Dine::textWidth (scoreFont, score)), juce::Justification::centredRight);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawText (g, "Mix health", head, juce::Justification::centredLeft);
            r.removeFromTop (10);
            auto bar = r.removeFromTop (4).toFloat();
            Dine::fillRounded (g, bar, juce::Colours::white.withAlpha (0.10f), 2.0f);
            if (page.health > 0)
                Dine::fillRounded (g, bar.withWidth (bar.getWidth() * float (page.health) / 100.0f),
                                   page.health >= 80 ? Dine::accent : page.health >= 55 ? Dine::warn : Dine::crit, 2.0f);
            for (const auto& n : notes)
            {
                g.setColour (n.colour);
                g.fillEllipse (float (n.box.getX() - 13), float (n.box.getY() + 5), 6.0f, 6.0f);
                drawWrapped (g, n.text, Dine::text (12.0f), Dine::ink.withAlpha (0.85f), n.box);
            }
            if (! statusBox.isEmpty()) drawWrapped (g, page.status, Dine::text (12.0f), Dine::ink3, statusBox);
        }

        // ---- Voices
        if (! voicesCard.isEmpty())
        {
            cardAt (voicesCard);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawText (g, "Voices", voicesCap, juce::Justification::centredLeft, true);
            drawWrapped (g, kVoicesNote, Dine::text (11.0f), Dine::ink3, voicesNote);
        }

        // the stamp under the verbs
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (12.0f));
        const int tunes = page.controller.getTuneCount();
        juce::String text = tunes > 0 ? "Tuned " + juce::String (tunes) + (tunes == 1 ? " time" : " times") + " this session"
                                      : juce::String ("Not tuned yet");
        Dine::drawText (g, text, stamp, juce::Justification::centredLeft, true);

        // the pad card: a 2 px accent edge down its left
        {
            const auto r = card.toFloat();
            Dine::drawCard (g, r, Dine::card);
            {
                juce::Graphics::ScopedSaveState clip (g);
                juce::Path round; round.addRoundedRectangle (r, Dine::Radius::card);
                g.reduceClipRegion (round);
                g.setColour (Dine::accent);
                g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), 2.0f, r.getHeight()));
            }
            auto in = card.reduced (16, 14).withTrimmedLeft (0);
            g.setColour (Dine::accent);
            g.setFont (Dine::caps (10.5f, 0.04f, 600));
            Dine::drawText (g, heading_, in.removeFromTop (headingH), juce::Justification::centredLeft, true);
            in.removeFromTop (8);
            drawWrapped (g, body, Dine::text (12.5f), Dine::ink2, in);
        }
    }

private:
    static int textHeight (const juce::Font& f, const juce::String& s, int width)
    {
        if (s.isEmpty() || width <= 0) return 0;
        juce::AttributedString a; a.setText (s); a.setFont (f);
        juce::TextLayout tl; tl.createLayout (a, float (width));
        return int (std::ceil (tl.getHeight())) + 2;
    }
    static void drawWrapped (juce::Graphics& g, const juce::String& s, const juce::Font& f, juce::Colour c, juce::Rectangle<int> box,
                             juce::Justification j = juce::Justification::topLeft)
    {
        if (s.isEmpty() || box.isEmpty()) return;
        juce::AttributedString a; a.setText (s); a.setFont (f); a.setColour (c); a.setJustification (j);
        juce::TextLayout tl; tl.createLayout (a, float (box.getWidth()));
        tl.draw (g, box.toFloat());
    }

    static constexpr const char* kVoicesNote = "A starting point for each voice. Every setting stays editable.";

    struct Note { juce::String text; juce::Colour colour; juce::Rectangle<int> box; };
    MixPage& page;
    juce::String heading_, body;
    juce::Rectangle<int> stamp, card, healthCap, statusBox, tuneCap, scopeCap, scopeTrack, scopeNote,
                         voicesCap, voicesNote, aimCap, scoreBox, healthCard, voicesCard;
    int headingH = 14;                 // the card's heading, measured rather than assumed (Text size)
    std::vector<Note> notes;
};

// ------------------------------------------------------------------ InputRow (the rail)
// A name and the one verb this rail exists for. Clicking the row picks the channel out
// (the chain along the foot follows it); clicking TUNE CHANNEL listens to it alone.
class MixPage::InputRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    // A ROW IS A ROW, AND A VERB IS A BUTTON.
    //
    // The rail used to write TUNE CHANNEL and FOCUS along a row as soon as the pointer was on
    // it, and hit-test those words by where they had been drawn. On a 198 pt rail that put
    // FOCUS in the middle of the row, over the name - so the obvious thing, clicking an input
    // to look at it, made it the source the whole mix is built around instead.
    //
    // Now a click on a row picks the input out, always. The row you have picked out grows and
    // offers its two verbs as chips of their own underneath, which is also the answer to what
    // the click was reaching for: more about this input.
    // v4: a name over what it is, and the verb as a pill at the right of every row; the open
    // row adds FOCUS under them. The first input of a family carries the family's caption.
    static constexpr int rowH = 37, openH = 37 + 30, captionH = 26;

    InputRow (const juce::String& n, ChannelRole role, int number, const std::string& iconKey)
        : name (n), roleText (Dine::friendlyRoleName (role)), icon (Dine::iconFor (iconKey, role)), num (number)
    {
        setTooltip ("Click to pick " + name + " out. TUNE CHANNEL listens to it on its own - nothing else in the mix moves. "
                    "FOCUS makes it the source the whole mix is built around: every level is set against it, and the music "
                    "makes room for it rather than the other way round.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        // A row changes when it is picked, hovered or its verdict moves: a repaint of the rail
        // is a blit of each row's image, not twenty rows of measured type and hairlines.
        setBufferedToImage (true);
    }

    std::function<void()> onTune, onSelect, onFocus;
    // The rail re-lays itself out when a row opens or closes: only one row is ever open.
    std::function<void()> onHeightChanged;

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition())) return;
        if (verbRect.contains (e.getPosition()))  { if (onTune) onTune();  return; }
        if (selected && focusRect.contains (e.getPosition())) { if (onFocus) onFocus(); return; }
        if (onSelect) onSelect();
    }

    void set (bool isMuted, bool isFaint, bool isSelected, bool isFocal, const juce::String& gainChip = {})
    {
        if (muted == isMuted && faint == isFaint && selected == isSelected && focal == isFocal && chip == gainChip) return;
        const bool opened = selected != isSelected;
        muted = isMuted; faint = isFaint; selected = isSelected; focal = isFocal; chip = gainChip;
        if (opened && onHeightChanged) onHeightChanged();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto all = getLocalBounds();
        if (caption.isNotEmpty())
        {
            auto cap = all.removeFromTop (captionH).reduced (16, 0).withTrimmedTop (8);
            g.setColour (tint);
            g.fillRoundedRectangle (cap.removeFromLeft (7).withSizeKeepingCentre (7, 7).toFloat(), 1.5f);
            cap.removeFromLeft (7);
            g.setFont (Dine::text (11.0f, 600));
            Dine::drawText (g, caption, cap, juce::Justification::centredLeft, true);
        }
        auto card = all.reduced (8, 1);
        if (selected)   Dine::fillRounded (g, card.toFloat(), juce::Colours::white.withAlpha (0.08f), 8.0f);
        else if (hover) Dine::fillRounded (g, card.toFloat(), juce::Colours::white.withAlpha (0.04f), 8.0f);

        auto r = all.withHeight (rowH).reduced (16, 0);
        // the verb, a pill at the right of every row
        {
            const auto font = Dine::caps (9.0f, 0.04f, 700);
            const int w = Dine::textWidth (font, "TUNE CHANNEL") + 18;
            verbRect = r.removeFromRight (w).withSizeKeepingCentre (w, 22);
            const bool over = isMouseOver (true) && verbRect.contains (getMouseXYRelative());
            Dine::fillRounded (g, verbRect.toFloat(), juce::Colours::white.withAlpha (over ? 0.10f : 0.0f), 11.0f);
            Dine::hairlineRounded (g, verbRect.toFloat().reduced (0.5f), juce::Colours::white.withAlpha (0.16f), 10.5f);
            g.setColour (over ? Dine::ink : Dine::ink2);
            g.setFont (font);
            Dine::drawText (g, "TUNE CHANNEL", verbRect, juce::Justification::centred);
            r.removeFromRight (8);
        }
        // FOCUS is a fact about the mix, said on the row whether it is open or not
        if (focal && ! selected)
        {
            const auto focusFont = Dine::caps (9.0f, 0.04f, 700);
            auto mark = r.removeFromRight (Dine::textWidth (focusFont, "FOCUS") + 4);
            g.setColour (Dine::accent);
            g.setFont (focusFont);
            Dine::drawText (g, "FOCUS", mark, juce::Justification::centredRight);
            r.removeFromRight (4);
        }
        auto nameLine = r.withHeight (r.getHeight() / 2 + 2).withTrimmedTop (4);
        auto subLine = r.withTrimmedTop (r.getHeight() / 2 + 2).withTrimmedBottom (4);
        g.setColour (muted ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (13.0f, selected ? 600 : 500));
        Dine::drawText (g, name, nameLine, juce::Justification::bottomLeft, true);
        // What the input is, under its name - left off altogether on a rail too narrow for it
        // rather than cut: the name is the row, and the Inspector says the rest.
        {
            const auto roleFont = Dine::text (10.5f);
            const int full = Dine::textWidth (roleFont, roleText);
            if (full <= subLine.getWidth())
            {
                g.setColour (Dine::ink3);
                g.setFont (roleFont);
                Dine::drawFittedText (g, roleText, subLine.removeFromLeft (full), juce::Justification::topLeft, 1);
            }
        }
        // a gain chip when the desk has something to do, or a lamp when the input is muted / faint
        const juce::String note = chip.isNotEmpty() ? chip : muted ? juce::String ("Muted") : faint ? juce::String ("Faint") : juce::String();
        if (note.isNotEmpty())
        {
            subLine.removeFromLeft (6);
            const auto f = Dine::text (9.5f, 600);
            const int w = Dine::textWidth (f, note) + 8;
            if (w <= subLine.getWidth())
            {
                const auto c = muted ? Dine::warn : Dine::crit;
                auto box = subLine.removeFromLeft (w).withHeight (13);
                Dine::fillRounded (g, box.toFloat(), c.withAlpha (0.22f), 4.0f);
                g.setColour (c.brighter (0.25f));
                g.setFont (f);
                Dine::drawText (g, note, box, juce::Justification::centred);
            }
        }

        focusRect = {};
        if (! selected) return;
        // the open row: FOCUS, which makes this the source the whole mix is built around
        auto chips = all.withTrimmedTop (rowH).reduced (16, 0).withTrimmedBottom (6);
        chips = chips.withHeight (juce::jmin (chips.getHeight(), 22));
        const auto chipFont = Dine::caps (9.0f, 0.04f, 700);
        const int w = Dine::textWidth (chipFont, "FOCUS") + 18;
        focusRect = chips.removeFromLeft (w);
        const bool over = isMouseOver (true) && focusRect.contains (getMouseXYRelative());
        Dine::fillRounded (g, focusRect.toFloat(), focal ? Dine::accent.withAlpha (0.20f)
                                                         : juce::Colours::white.withAlpha (over ? 0.12f : 0.06f), 11.0f);
        g.setColour (focal ? Dine::accent : over ? Dine::ink : Dine::ink2);
        g.setFont (chipFont);
        Dine::drawText (g, "FOCUS", focusRect, juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent&) override { if (selected) repaint(); }

    int wantedHeight() const noexcept { return (selected ? openH : rowH) + (caption.isNotEmpty() ? captionH : 0); }

    juce::String name, roleText, caption, chip;
    Dine::Icon icon;
    juce::Colour tint { Dine::ink4 };     // the group this input feeds
    int num = 0;
    bool muted = false, faint = false, hover = false, selected = false, focal = false;
    juce::Rectangle<int> verbRect, focusRect;
};

// The steps of a TUNE LIVE MIX run, as the user sees them.
namespace
{
    struct LiveStep { const char* label; TuneLiveCoordinator::State from; };

    constexpr LiveStep kLiveSteps[] = {
        { "Listen",   TuneLiveCoordinator::State::CapturingInitial },
        { "Measure",  TuneLiveCoordinator::State::AnalyzingInitial },
        { "Decide",   TuneLiveCoordinator::State::WaitingForReasoning },
        { "Resolve",  TuneLiveCoordinator::State::Resolving },
        { "Check",    TuneLiveCoordinator::State::Validating },
        { "Apply",    TuneLiveCoordinator::State::Applying },
        { "Verify",   TuneLiveCoordinator::State::CapturingVerify },
        { "Refine",   TuneLiveCoordinator::State::WaitingForRefinement },
    };
    constexpr int kNumLiveSteps = int (sizeof (kLiveSteps) / sizeof (kLiveSteps[0]));

    int liveStepFor (TuneLiveCoordinator::State s)
    {
        using State = TuneLiveCoordinator::State;
        switch (s)
        {
            case State::CapturingInitial:     return 0;
            case State::AnalyzingInitial:     return 1;
            case State::WaitingForReasoning:  return 2;
            case State::Resolving:            return 3;
            case State::Validating:           return 4;
            case State::Applying:             return 5;
            case State::CapturingVerify:
            case State::AnalyzingVerify:      return 6;
            case State::WaitingForRefinement:
            case State::ValidatingRefinement:
            case State::ApplyingRefinement:   return 7;
            case State::Ready:                return kNumLiveSteps;
            default:                          return -1;
        }
    }
}

// ------------------------------------------------------------------ ScopeSheet
// WHAT SHOULD DINE TUNE?
//
// TUNE is the one verb on this workspace, and until now it always meant the same thing - the
// whole mix. `startTuneBus` existed and was reachable only from a small word inside a group
// tile, and nothing at all offered "these three microphones". So the verb opens this first:
// the three scopes, the groups by name, the channels by name, and the sentence that says
// exactly what is about to happen before it happens.
//
// Nothing here decides anything about the mix. Every choice ends in one of three calls on
// MixController - the same calls the Mix menu and the group tiles already make.
class MixPage::ScopeSheet : public juce::Component
{
public:
    enum class Scope { Mix = 0, Group, Channels };

    // One input, with a tick. The name and its group, because "BGV 2" means more beside the
    // word VOCALS than it does on its own.
    class Row : public juce::Button
    {
    public:
        Row (const juce::String& n, int number, MixBus b) : juce::Button (n), name (n), index (number), bus (b)
        {
            setClickingTogglesState (false);
            setWantsKeyboardFocus (false);
        }

        void setPicked (bool p) { if (p != picked) { picked = p; repaint(); } }

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds();
            if (picked) Dine::fillRounded (g, r.toFloat(), Dine::selected, Dine::Radius::chip);
            else if (over) Dine::fillRounded (g, r.toFloat(), Dine::fillSoft, Dine::Radius::chip);
            r = r.reduced (10, 0);

            auto box = r.removeFromLeft (16).withSizeKeepingCentre (14, 14).toFloat();
            Dine::hairlineRounded (g, box, picked ? Dine::accent : Dine::edge, 3.0f);
            if (picked)
            {
                Dine::fillRounded (g, box, Dine::accent, 3.0f);
                g.setColour (Dine::onAccent);
                g.setFont (Dine::text (10.0f, 600));
                Dine::drawText (g, Glyph::check(), box.toNearestInt(), juce::Justification::centred, false);
            }
            r.removeFromLeft (12);

            const auto numberFont = Dine::mono (11.0f);
            g.setColour (Dine::ink4);
            g.setFont (numberFont);
            const juce::String num = (index < 9 ? "0" : "") + juce::String (index + 1);
            Dine::drawText (g, num, r.removeFromLeft (Dine::textWidth (numberFont, "00") + 2), juce::Justification::centredLeft);
            r.removeFromLeft (10);

            const auto groupFont = Dine::caps (9.5f, 0.08f);
            const juce::String groupText = juce::String (mixBusName (bus)).toUpperCase();
            auto groupCell = r.removeFromRight (Dine::textWidth (groupFont, groupText) + 4);
            g.setColour (Dine::busTint (bus));
            g.setFont (groupFont);
            Dine::drawText (g, groupText, groupCell, juce::Justification::centredRight);

            g.setColour (picked ? Dine::ink : Dine::ink2);
            g.setFont (Dine::text (13.0f));
            Dine::drawText (g, name, r.withTrimmedRight (10), juce::Justification::centredLeft);
        }

    private:
        juce::String name;
        int index;
        MixBus bus;
        bool picked = false;
    };

    explicit ScopeSheet (MixController& c) : controller (c)
    {
        const char* names[3] = { "The whole mix", "One group", "Some channels" };
        for (int i = 0; i < 3; ++i)
        {
            tabs[size_t (i)] = std::make_unique<DineButton> (names[i], DineButton::Style::Segment);
            tabs[size_t (i)]->setClickingTogglesState (false);
            tabs[size_t (i)]->setFontPx (13.0f);
            tabs[size_t (i)]->onClick = [this, i] { setScope (Scope (i)); };
            addAndMakeVisible (*tabs[size_t (i)]);
        }
        for (int i = 0; i < kGroupBuses; ++i)
        {
            groupButtons[size_t (i)] = std::make_unique<DineButton> (groupName (i), DineButton::Style::Toggle);
            groupButtons[size_t (i)]->setClickingTogglesState (false);
            groupButtons[size_t (i)]->setFontPx (12.5f);
            groupButtons[size_t (i)]->onClick = [this, i] { group = i; refresh(); };
            addChildComponent (*groupButtons[size_t (i)]);
        }
        channelView.setViewedComponent (&channelHolder, false);
        Dine::nativeScrolling (channelView);
        channelView.setScrollBarsShown (true, false);
        addChildComponent (channelView);

        allButton.setFontPx (12.0f);
        allButton.onClick = [this]
        {
            const int inputs = int (controller.getSession().inputs.size());
            const bool everything = ! picked.empty() && int (picked.size()) == inputs;
            picked.clear();
            if (! everything) for (int i = 0; i < inputs; ++i) picked.insert (i);
            refresh();
        };
        addChildComponent (allButton);

        start.setCaps (true);
        start.setFontPx (13.0f);
        start.onClick = [this] { begin(); };
        cancel.setFontPx (13.0f);
        cancel.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (start);
        addAndMakeVisible (cancel);
        setInterceptsMouseClicks (true, true);
    }

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;

    // The snapshot tool and the reachability test. Nothing about the mix happens here: it
    // picks a scope the way a click would, so the shots are of the real screens.
    void chooseForSnapshot (int which, int whichGroup)
    {
        if (whichGroup >= 0 && whichGroup < kGroupBuses) group = whichGroup;
        if (which == 2 && picked.empty())
            for (int i = 0; i < juce::jmin (3, int (channelRows.size())); ++i) picked.insert (i);
        setScope (Scope (juce::jlimit (0, 2, which)));
    }

    // Opened fresh every time: a scope is a decision about this tune, not a setting that
    // sticks. Whatever the workspace has picked out arrives ticked, because "tune this one"
    // is the request somebody has usually already made with the pointer.
    void open (int selectedStrip)
    {
        rebuildChannels();
        picked.clear();
        if (selectedStrip >= 0 && selectedStrip < int (controller.getSession().inputs.size())) picked.insert (selectedStrip);
        group = -1;
        for (int i = 0; i < kGroupBuses; ++i)
            if (controller.getGraph().busUsed[size_t (groupBus (i))]) { group = i; break; }
        setScope (Scope::Mix);
        setVisible (true);
        toFront (true);
    }

    static constexpr int kCardW = 620;

    // The card is as tall as the question being asked. "The whole mix" needs no list at all
    // and a card with a hole in it looks like something failed to load; a group needs a row or
    // two of names; the channels need a list worth scrolling.
    int bodyHeight() const
    {
        if (scope == Scope::Group)
        {
            int used = 0;
            for (int i = 0; i < kGroupBuses; ++i) if (controller.getGraph().busUsed[size_t (groupBus (i))]) ++used;
            const int perRow = juce::jlimit (1, 4, used);
            const int rows = juce::jmax (1, (juce::jmax (1, used) + perRow - 1) / perRow);
            return 14 + 12 + rows * Dine::Metric::button + (rows - 1) * 10 + 14;
        }
        if (scope == Scope::Channels) return 14 + 12 + kListH + 14;
        return 0;
    }

    int cardHeight() const
    {
        return kPadY * 2 + 14 + 8 + 28 + 16 + Dine::Metric::button + 20
             + bodyHeight() + kSentenceH + 12 + Dine::Metric::button;
    }

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (kCardW, getWidth() - 40);
        return juce::Rectangle<int> (w, juce::jmin (cardHeight(), getHeight() - 20)).withCentre (getLocalBounds().getCentre());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::desk.withAlpha (0.88f));
        auto card = sheetBounds();
        Dine::drawSheet (g, card.toFloat(), 14.0f);

        auto r = card.reduced (kPadX, kPadY);
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (12.0f, 0.10f));
        Dine::drawText (g, "TUNE", r.removeFromTop (14), juce::Justification::centredLeft);
        r.removeFromTop (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (22.0f));
        Dine::drawText (g, "What should DINE tune?", r.removeFromTop (28), juce::Justification::centredLeft);
        r.removeFromTop (16);
        Dine::drawSegmentTrack (g, r.removeFromTop (Dine::Metric::button));
        r.removeFromTop (20);

        if (scope == Scope::Group)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::caps (11.0f, 0.08f));
            Dine::drawText (g, group < 0 ? "NO GROUPS YET" : "GROUPS ON THIS CONSOLE",
                            r.removeFromTop (14), juce::Justification::centredLeft);
        }
        else if (scope == Scope::Channels)
        {
            auto row = r.removeFromTop (14);
            g.setColour (Dine::ink4);
            g.setFont (Dine::caps (11.0f, 0.08f));
            Dine::drawText (g, picked.empty() ? "CHANNELS" : "CHANNELS  " + juce::String (Glyph::dot()) + "  "
                                                             + juce::String (int (picked.size())) + " PICKED",
                            row.withTrimmedRight (allButton.getWidth() + 10), juce::Justification::centredLeft);
        }

        // The sentence, along the foot above the buttons: what one press is about to do.
        auto foot = card.reduced (kPadX, kPadY);
        auto words = foot.removeFromBottom (Dine::Metric::button + 12 + kSentenceH).removeFromTop (kSentenceH);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, sentence(), words, juce::Justification::topLeft, 3, 1.0f);
    }

    void resized() override
    {
        auto card = sheetBounds();
        auto r = card.reduced (kPadX, kPadY);
        r.removeFromTop (14 + 8 + 28 + 16);

        {
            auto tabRow = r.removeFromTop (Dine::Metric::button).reduced (2);
            const int cell = (tabRow.getWidth() - 4) / 3;
            for (int i = 0; i < 3; ++i)
            {
                tabs[size_t (i)]->setBounds (tabRow.removeFromLeft (i == 2 ? tabRow.getWidth() : cell));
                if (i < 2) tabRow.removeFromLeft (2);
            }
        }
        r.removeFromTop (20);

        auto body = r.withTrimmedBottom (Dine::Metric::button + 12 + kSentenceH + 14);
        if (scope == Scope::Group)
        {
            body.removeFromTop (14 + 12);
            int used = 0;
            for (int i = 0; i < kGroupBuses; ++i) if (controller.getGraph().busUsed[size_t (groupBus (i))]) ++used;
            const int perRow = juce::jlimit (1, 4, used);
            const int cell = (body.getWidth() - 10 * (perRow - 1)) / perRow;
            auto row = body.removeFromTop (Dine::Metric::button);
            int placed = 0;
            for (int i = 0; i < kGroupBuses; ++i)
            {
                if (! controller.getGraph().busUsed[size_t (groupBus (i))]) continue;
                if (placed > 0 && placed % perRow == 0)
                {
                    body.removeFromTop (10);
                    row = body.removeFromTop (Dine::Metric::button);
                }
                groupButtons[size_t (i)]->setBounds (row.removeFromLeft (cell));
                row.removeFromLeft (10);
                ++placed;
            }
        }
        else if (scope == Scope::Channels)
        {
            auto cap = body.removeFromTop (14);
            allButton.setBounds (cap.removeFromRight (juce::jmax (90, allButton.idealWidth())).expanded (0, 6));
            body.removeFromTop (12);
            channelView.setBounds (body);
            layoutChannels();
        }

        auto buttons = card.reduced (kPadX, kPadY).removeFromBottom (Dine::Metric::button);
        start.setBounds (buttons.removeFromRight (juce::jmax (150, start.idealWidth())));
        buttons.removeFromRight (10);
        cancel.setBounds (buttons.removeFromRight (juce::jmax (90, cancel.idealWidth())));
    }

private:
    static constexpr int kPadX = 30, kPadY = 26;
    static constexpr int kRowH = 30;
    static constexpr int kSentenceH = 40;
    static constexpr int kListH = 8 * kRowH;     // eight channels before it scrolls

    void setScope (Scope s)
    {
        scope = s;
        for (int i = 0; i < 3; ++i) tabs[size_t (i)]->setToggleState (int (s) == i, juce::dontSendNotification);
        for (int i = 0; i < kGroupBuses; ++i)
            groupButtons[size_t (i)]->setVisible (s == Scope::Group && controller.getGraph().busUsed[size_t (groupBus (i))]);
        channelView.setVisible (s == Scope::Channels);
        allButton.setVisible (s == Scope::Channels);
        resized();
        refresh();
    }

    void refresh()
    {
        for (int i = 0; i < kGroupBuses; ++i)
            groupButtons[size_t (i)]->setToggleState (i == group, juce::dontSendNotification);
        for (size_t i = 0; i < channelRows.size(); ++i)
            channelRows[i]->setPicked (picked.count (int (i)) > 0);
        allButton.setButtonText (! picked.empty() && int (picked.size()) == int (controller.getSession().inputs.size())
                                     ? "Select none" : "Select all");
        const bool ready = scope == Scope::Mix
                        || (scope == Scope::Group && group >= 0)
                        || (scope == Scope::Channels && ! picked.empty());
        start.setEnabled (ready);
        start.setButtonText (scope == Scope::Group && group >= 0 ? "Tune " + groupName (group)
                           : scope == Scope::Channels && picked.size() == 1 ? juce::String ("Tune this channel")
                           : scope == Scope::Channels && picked.size() > 1 ? "Tune " + juce::String (int (picked.size())) + " channels"
                                                                           : juce::String ("Tune the mix"));
        repaint();
    }

    void rebuildChannels()
    {
        channelRows.clear();
        channelHolder.removeAllChildren();
        const auto& session = controller.getSession();
        const auto& graph = controller.getGraph();
        for (int i = 0; i < int (session.inputs.size()); ++i)
        {
            const auto bus = i < graph.numStrips() ? graph.strips[size_t (i)].bus : MixBus::Music;
            auto row = std::make_unique<Row> (juce::String (session.inputs[size_t (i)].name), i, bus);
            row->onClick = [this, i]
            {
                if (picked.count (i) > 0) picked.erase (i); else picked.insert (i);
                refresh();
            };
            channelHolder.addAndMakeVisible (*row);
            channelRows.push_back (std::move (row));
        }
        layoutChannels();
    }

    void layoutChannels()
    {
        const int w = juce::jmax (40, channelView.getWidth() - (channelView.isVerticalScrollBarShown() ? 10 : 0));
        channelHolder.setSize (w, juce::jmax (1, int (channelRows.size()) * kRowH));
        for (size_t i = 0; i < channelRows.size(); ++i)
            channelRows[i]->setBounds (0, int (i) * kRowH, w, kRowH);
    }

    juce::String sentence() const
    {
        switch (scope)
        {
            case Scope::Group:
                if (group < 0) return "Nothing is assigned to a group yet, so there is nothing to tune one at a time. Assign the inputs first.";
                return "DINE listens to the whole band and sets " + groupName (group)
                     + " alone - its channels and its own group chain. Every other group, and the master, stay exactly where they are.";
            case Scope::Channels:
                if (picked.empty())
                    return "Pick the channels to tune. DINE still listens to the whole band, so they are decided in the mix rather than on their own.";
                if (picked.size() == 1)
                    return "DINE listens for " + juce::String (int (MixController::channelListen().seconds))
                         + " seconds and sets that one channel: its chain, its gain, its level and its sends. Nothing else moves.";
                return "DINE listens to the whole band and sets those " + juce::String (int (picked.size()))
                     + " channels. The groups, the master and every other channel stay where they are.";
            case Scope::Mix:
            default:
                return "DINE listens to the band for 30 seconds and builds the whole mix from what it measures - "
                       "every channel, every group and the master.";
        }
    }

    void begin()
    {
        const auto chosen = scope;
        const int chosenGroup = group;
        std::vector<int> strips (picked.begin(), picked.end());
        if (onClose) onClose();
        switch (chosen)
        {
            case Scope::Group:
                if (chosenGroup < 0) return;
                // The picker counts in the console's order; the engine counts in the enum's.
                // `MixBus (chosenGroup)` tuned whichever bus happened to be stored at that
                // index, which is not the one whose name is written on the chip.
                controller.startTuneBus (groupBus (chosenGroup));
                if (onToast) onToast ("Listening for " + groupName (chosenGroup)
                                      + " alone. Every other group, and the master, stay where they are.");
                break;
            case Scope::Channels:
                controller.startTuneStrips (strips);
                if (onToast && strips.size() > 1)
                    onToast ("Listening for those " + juce::String (int (strips.size()))
                             + " channels. The groups, the master and every other channel stay where they are.");
                break;
            case Scope::Mix:
            default:
                controller.startTuneMix();
                break;
        }
    }

    MixController& controller;
    Scope scope = Scope::Mix;
    int group = -1;
    std::set<int> picked;
    std::array<std::unique_ptr<DineButton>, 3> tabs;
    std::array<std::unique_ptr<DineButton>, size_t (kGroupBuses)> groupButtons;
    std::vector<std::unique_ptr<Row>> channelRows;
    juce::Viewport channelView;
    juce::Component channelHolder;
    DineButton allButton { "Select all", DineButton::Style::Ghost };
    DineButton start { "Tune the mix", DineButton::Style::Filled };
    DineButton cancel { "Cancel", DineButton::Style::Ghost };
};

// ------------------------------------------------------------------ ListenSheet
// A card in the middle of the workspace while DINE listens: what it is doing, how far it
// is, what it can hear, and the one way out.
class MixPage::ListenSheet : public juce::Component
{
public:
    ListenSheet (MixController& c, const std::function<float (int)>& arrivingAt) : controller (c), arriving (arrivingAt)
    {
        addAndMakeVisible (cancel);
        cancel.setFontPx (13.0f);
        cancel.setPadX (16);
        cancel.onClick = [this] { controller.abortTuneMix(); };
        setInterceptsMouseClicks (true, true);
    }

    void tick()
    {
        const auto state = controller.getTuneLive().getState();
        if (state == lastLiveState) return;
        lastLiveState = state;
        stepStartedMs = juce::Time::getMillisecondCounter();
    }

    int stepSeconds() const
    {
        if (stepStartedMs == 0) return 0;
        return int ((juce::Time::getMillisecondCounter() - stepStartedMs) / 1000);
    }

    static constexpr int kCardW = 720, kCardH = 440, kRingW = 168;

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (kCardW, getWidth() - 40);
        return juce::Rectangle<int> (w, juce::jmin (kCardH, getHeight() - 20)).withCentre (getLocalBounds().getCentre());
    }

    // The design's listening sheet (`09 - TUNE MIX is listening`, 75:12415): a title and one
    // line saying what to do, the run's real steps as lamps, a bar with the seconds under it,
    // then a line per group saying what has been heard - because a volunteer standing at the
    // desk needs to know that the pastor's microphone has not made a sound yet.
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::desk.withAlpha (0.88f));
        auto card = sheetBounds();
        Dine::drawSheet (g, card.toFloat(), Dine::Radius::card);

        const bool waiting = controller.isWaitingForBand();
        const bool planning = controller.getStage() == MixController::Stage::Planning;
        const float progress = planning ? 1.0f : controller.getListenProgress();
        const bool live = controller.isTuningLive();
        const auto liveState = controller.getTuneLive().getState();
        const bool capturing = liveState == TuneLiveCoordinator::State::CapturingInitial
                            || liveState == TuneLiveCoordinator::State::CapturingVerify;
        const bool working = (live && ! capturing) || planning;
        const juce::String verb = tuneVerb (controller, live);

        auto r = card.reduced (30, 26);
        // THE RING (v4): the seconds left, counting down; a sweep while it works things out.
        {
            auto ringArea = r.removeFromLeft (kRingW).removeFromTop (kRingW).reduced (8);
            r.removeFromLeft (22);
            const auto ring = ringArea.toFloat().reduced (4.0f);
            g.setColour (Dine::control);
            g.drawEllipse (ring, 5.0f);
            const float phase = float (juce::Time::getMillisecondCounter() % 1400u) / 1400.0f;
            juce::Path arc;
            const float start = -juce::MathConstants<float>::halfPi;
            if (working || waiting)
                arc.addCentredArc (ring.getCentreX(), ring.getCentreY(), ring.getWidth() / 2, ring.getHeight() / 2, 0.0f,
                                   start + phase * juce::MathConstants<float>::twoPi,
                                   start + phase * juce::MathConstants<float>::twoPi + 1.2f, true);
            else
                arc.addCentredArc (ring.getCentreX(), ring.getCentreY(), ring.getWidth() / 2, ring.getHeight() / 2, 0.0f,
                                   start, start + juce::MathConstants<float>::twoPi * (1.0f - progress), true);
            g.setColour (waiting ? Dine::ink3 : Dine::accent);
            g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float seconds = controller.getListenSeconds();
            const int left = juce::jmax (0, int (std::ceil (seconds * (1.0f - progress))));
            auto inner = ringArea.withSizeKeepingCentre (ringArea.getWidth() - 30, 64);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (waiting ? 15.0f : 40.0f, 600));
            Dine::drawText (g, waiting ? juce::String ("Waiting") : working ? juce::String (juce::jmin (99, int (std::round (progress * 100.0f)))) + "%"
                                                                       : juce::String (left),
                            inner.removeFromTop (46), juce::Justification::centred, false);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 500));
            Dine::drawText (g, waiting ? juce::String ("for the band") : working ? juce::String ("through") : juce::String ("seconds left"),
                            inner, juce::Justification::centred, false);
        }

        g.setColour (Dine::ink);
        g.setFont (Dine::text (22.0f, 600));
        Dine::drawText (g, verb + (working ? " is working" : waiting ? " is waiting" : " is listening"),
                        r.removeFromTop (28), juce::Justification::centredLeft, true);
        r.removeFromTop (2);

        juce::String hearing;
        if (live)
        {
            hearing = juce::String (controller.getTuneLiveStatus());
            if (capturing) hearing += " Keep the band playing.";
        }
        else if (waiting) hearing = "Have the band play a song the way they normally would. DINE starts as soon as it hears them.";
        else if (planning) hearing = "Comparing what it heard against " + juce::String (styleProfileName (controller.getSession().profile))
                                   + ", then checking its own work.";
        else hearing = "Keep playing. DINE hears every input at once and builds the whole mix.";
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, hearing, r.removeFromTop (36), juce::Justification::topLeft, 2);
        r.removeFromTop (16);

        // The steps: the run's real ones, a lamp each, lit as it passes them.
        {
            auto steps = r.removeFromTop (16);
            const auto font = Dine::text (12.0f, 500);
            // EIGHT STEPS IN ONE ROW. At 22 pt between them the last two ran off the card and
            // the seventh was drawn as "Veri..." - a step nobody can read is not a step. The
            // spacing is what is left over once every word has the room it needs.
            const auto spacing = [&] (const juce::StringArray& labels)
            {
                int words = 0;
                for (const auto& l : labels) words += 14 + Dine::textWidth (font, l);
                return juce::jlimit (8, 22, (steps.getWidth() - words) / juce::jmax (1, labels.size() - 1));
            };
            auto lamp = [&g] (juce::Rectangle<int>& row, const juce::String& label, juce::Colour c, const juce::Font& f, int gap)
            {
                const int w = Dine::textWidth (f, label);
                if (row.getWidth() < 14 + w) { row = row.withWidth (0); return; }
                g.setColour (c);
                g.fillEllipse (row.removeFromLeft (7).withSizeKeepingCentre (6, 6).toFloat());
                row.removeFromLeft (7);
                g.setFont (f);
                Dine::drawText (g, label, row.removeFromLeft (w), juce::Justification::centredLeft);
                row.removeFromLeft (gap);
            };
            if (live)
            {
                juce::StringArray labels;
                for (const auto& step : kLiveSteps) labels.add (Dine::sectionCase (step.label));
                const int gap = spacing (labels);
                const int at = liveStepFor (liveState);
                for (int i = 0; i < kNumLiveSteps; ++i)
                    lamp (steps, labels[i], at > i ? Dine::accent : at == i ? Dine::ink : Dine::ink4, font, gap);
            }
            else
            {
                const juce::StringArray labels { "Listen", "Analyse", "Decide" };
                const int gap = spacing (labels);
                const int at = planning ? 2 : waiting ? -1 : 0;
                for (int i = 0; i < 3; ++i)
                    lamp (steps, labels[i], at > i ? Dine::accent : at == i ? Dine::accent : Dine::ink4, font, gap);
            }
        }
        r.removeFromTop (18);

        // WHAT IT HEARS, input by input (v4): the silent ones first, each with why, then the
        // ones heard - under the ring, across the card, as many as there is room for.
        auto list = card.reduced (30, 26).withTrimmedTop (kRingW + 6);
        list.removeFromBottom (Dine::Metric::button + 10);
        if (controller.isPrepared())
        {
            const auto& gph = controller.getGraph();
            const auto& kept = controller.getBase();
            bool bandHeard = false;
            for (int i = 0; i < gph.numStrips(); ++i)
                if (roleFamily (gph.strips[size_t (i)].role) != RoleFamily::Speech && controller.stripHeard (i)) bandHeard = true;
            struct Line { juce::String name, state; bool heard; juce::Colour tint; };
            std::vector<Line> silent, heard;
            for (int i = 0; i < gph.numStrips(); ++i)
            {
                const auto& st = gph.strips[size_t (i)];
                Line l { juce::String (st.name), {}, controller.stripHeard (i), Dine::busTint (st.bus) };
                if (l.heard) { l.state = "heard"; heard.push_back (l); continue; }
                const bool muted = i < kept.numStrips && kept.strips[size_t (i)].mute;
                const float level = arriving ? arriving (st.inputA) : 0.0f;
                l.state = muted ? juce::String ("silent ") + Glyph::dash() + " muted"
                        : roleFamily (st.role) == RoleFamily::Speech && bandHeard ? juce::String ("silent ") + Glyph::dash() + " expected during the music"
                        : level < -70.0f && progress > 0.25f ? juce::String ("silent ") + Glyph::dash() + " nothing arriving"
                        : juce::String ("not yet");
                silent.push_back (l);
            }
            std::vector<Line> lines (silent);
            lines.insert (lines.end(), heard.begin(), heard.end());
            const int cols = 2, rowH = 22, gap = 24;
            const int rows = juce::jmax (0, list.getHeight() / rowH);
            const int capacity = rows * cols;
            const int colW = (list.getWidth() - gap) / cols;
            const bool more = int (lines.size()) > capacity;
            const int shown = more ? juce::jmax (0, capacity - 1) : int (lines.size());
            const auto nameFont = Dine::text (12.5f, 600), stateFont = Dine::text (12.0f);
            for (int k = 0; k < shown; ++k)
            {
                auto cell = juce::Rectangle<int> (list.getX() + (k / juce::jmax (1, rows)) * (colW + gap),
                                                  list.getY() + (k % juce::jmax (1, rows)) * rowH, colW, rowH);
                const auto& l = lines[size_t (k)];
                g.setColour (l.heard ? Dine::accent : l.state == "not yet" ? Dine::ink4 : Dine::warn);
                g.fillEllipse (cell.removeFromLeft (7).withSizeKeepingCentre (6, 6).toFloat());
                cell.removeFromLeft (9);
                const int nw = juce::jmin (cell.getWidth() / 2, Dine::textWidth (nameFont, l.name) + 2);
                g.setColour (Dine::ink);
                g.setFont (nameFont);
                Dine::drawFittedText (g, l.name, cell.removeFromLeft (nw), juce::Justification::centredLeft, 1, 0.8f);
                cell.removeFromLeft (8);
                g.setColour (l.heard ? Dine::ink3 : Dine::ink2);
                g.setFont (stateFont);
                Dine::drawFittedText (g, l.state, cell, juce::Justification::centredLeft, 1, 0.8f);
            }
            if (more)
            {
                const int k = shown;
                auto cell = juce::Rectangle<int> (list.getX() + (k / juce::jmax (1, rows)) * (colW + gap),
                                                  list.getY() + (k % juce::jmax (1, rows)) * rowH, colW, rowH);
                g.setColour (Dine::ink3);
                g.setFont (stateFont);
                Dine::drawText (g, "and " + juce::String (int (lines.size()) - shown) + " more", cell.withTrimmedLeft (16), juce::Justification::centredLeft, false);
            }
        }
    }

    void resized() override
    {
        auto card = sheetBounds();
        const int w = juce::jmax (120, cancel.idealWidth());
        cancel.setBounds (card.reduced (30, 26).removeFromBottom (Dine::Metric::button).removeFromRight (w));
        cancel.setButtonText (controller.isTuningLive() ? "Stop" : "Stop listening");
    }

private:
    MixController& controller;
    const std::function<float (int)>& arriving;     // MixPage::inputArriving: dBFS at a device input
    DineButton cancel { "Stop listening", DineButton::Style::Standard };
    TuneLiveCoordinator::State lastLiveState = TuneLiveCoordinator::State::Idle;
    juce::uint32 stepStartedMs = 0;
};

// ------------------------------------------------------------------ ResultSheet
// What the run built: a title, one line per decision with the sentence that explains it,
// BEFORE / AFTER, TRY ANOTHER MIX, REVERT and KEEP.
class MixPage::ResultSheet : public juce::Component
{
public:
    ResultSheet (MixController& c, MixPage& p) : controller (c), page (p)
    {
        for (auto* b : { &before, &after, &keep, &revert, &another, &review, &closeButton }) addAndMakeVisible (*b);
        // KEEP SOME: one chip per group this proposal touches. Switching a group off takes
        // its part straight back out of what AFTER is playing, so the decision is made by
        // listening rather than by reading a list.
        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            auto chip = std::make_unique<DineButton> (juce::String (mixBusName (MixBus (b))), DineButton::Style::Toggle);
            chip->setFontPx (11.0f);
            chip->setPadX (11);
            chip->setTooltip (juce::String (mixBusName (MixBus (b))) + ": switch it off and this proposal's changes to it are left out. "
                              "AFTER plays exactly what KEEP will apply.");
            chip->onClick = [this, b]
            {
                picked[size_t (b)] = ! picked[size_t (b)];
                applySelection();
            };
            addChildComponent (*chip);
            chips[size_t (b)] = std::move (chip);
        }
        before.setCaps (true); after.setCaps (true); keep.setCaps (true); revert.setCaps (true);
        before.setClickingTogglesState (false); after.setClickingTogglesState (false);
        for (auto* b : { &before, &after, &keep, &revert, &another }) { b->setFontPx (12.5f); b->setPadX (14); }
        keep.setPadX (18);
        // KEEP says "Keep these" once a group is switched off. The sheet is measured with the
        // longer label from the start, so picking a chip never makes the whole card change size.
        keep.setButtonText ("Keep these");
        keepWideW = juce::jmax (74, keep.idealWidth());
        keep.setButtonText ("Keep");
        before.onClick = [this] { controller.setCompare (MixController::Compare::Before); refresh(); if (page.onToast) page.onToast ("Playing BEFORE - to the room and the stream too. Nothing is kept until KEEP."); };
        after.onClick  = [this] { controller.setCompare (MixController::Compare::After); refresh(); if (page.onToast) page.onToast ("Playing AFTER - to the room and the stream too. Nothing is kept until KEEP."); };
        keep.onClick   = [this] { controller.keepPlan(); if (page.onToast) page.onToast ("Kept. Every value it set is marked TUNED BY DINE and can be reverted stage by stage."); };
        revert.onClick = [this] { controller.revertPlan(); if (page.onToast) page.onToast ("Reverted to the mix you had before this run."); };
        another.onClick = [this] { controller.tryAnotherMix(); };
        review.onClick = [this] { if (page.onOpenAdvanced) page.onOpenAdvanced(); };
        closeButton.onClick = [this]
        {
            controller.revertPlan();
            if (page.onToast) page.onToast ("Closed without keeping: the mix is as it was. TUNE MIX again to propose it again.");
        };
        review.setFontPx (12.5f);
        // The cross, not the word: a 28 pt cell has never had room for CLOSE, and a button
        // that says "Clo..." says nothing. The sentence lives in the tooltip.
        closeButton.setIcon (Dine::Icon::Close);
        closeButton.setTooltip ("Close without keeping: the mix goes back to what it was before this run.");
        setInterceptsMouseClicks (true, true);
    }

    void refresh()
    {
        const bool showingAfter = controller.getCompare() == MixController::Compare::After;
        before.setStyle (showingAfter ? DineButton::Style::Standard : DineButton::Style::Filled);
        after.setStyle (showingAfter ? DineButton::Style::Filled : DineButton::Style::Standard);
        another.setEnabled (controller.canTryAnotherMix());
        rebuildChips();
        resized();
        repaint();
    }

    // Which groups this proposal actually changes. Asked of the planner itself - a group is
    // touched when keeping it alone would change something - so the chips can never offer a
    // group whose changes are nothing, or hide one whose changes are real.
    void rebuildChips()
    {
        const auto* plan = controller.getPlan();
        const int stamp = plan == nullptr ? -1
                        : controller.getTuneCount() * 1000003 + plan->parametersChanged * 101 + plan->fadersChanged * 7 + plan->gainsChanged;
        if (stamp == builtFor) { paintChips(); return; }
        builtFor = stamp;
        groupsTouched = 0;
        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            bool touched = false;
            if (plan != nullptr && plan->valid)
            {
                const auto one = MixPlanner::restrictTo (*plan, MixPlanner::PlanSelection::group (controller.getGraph(), MixBus (b)),
                                                         controller.getGraph(), controller.getSession().profile);
                touched = ! one.noChangeRequired;
            }
            touchedBus[size_t (b)] = touched;
            picked[size_t (b)] = true;         // a new proposal arrives whole; switching a group off is the user's move
            if (touched) ++groupsTouched;
        }
        paintChips();
    }

    void paintChips()
    {
        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            chips[size_t (b)]->setVisible (showChips() && touchedBus[size_t (b)]);
            chips[size_t (b)]->setToggleState (picked[size_t (b)], juce::dontSendNotification);
        }
        const bool all = everythingPicked();
        keep.setButtonText (all ? "Keep" : "Keep these");
        keep.setEnabled (all || anyPicked());
    }

    // One group is not a choice, so the row only appears when there is something to choose between.
    bool showChips() const noexcept { return groupsTouched > 1; }
    bool everythingPicked() const noexcept
    {
        for (int b = 0; b < int (MixBus::Count); ++b) if (touchedBus[size_t (b)] && ! picked[size_t (b)]) return false;
        return true;
    }
    bool anyPicked() const noexcept
    {
        for (int b = 0; b < int (MixBus::Count); ++b) if (touchedBus[size_t (b)] && picked[size_t (b)]) return true;
        return false;
    }

    void applySelection()
    {
        if (everythingPicked()) controller.clearPlanSelection();
        else
        {
            MixPlanner::PlanSelection sel;
            for (int b = 0; b < int (MixBus::Count); ++b)
            {
                if (! picked[size_t (b)]) continue;
                const auto one = MixPlanner::PlanSelection::group (controller.getGraph(), MixBus (b));
                for (size_t i = 0; i < sel.strips.size(); ++i) sel.strips[i] = sel.strips[i] || one.strips[i];
                sel.buses[size_t (b)] = true;
            }
            controller.setPlanSelection (sel);
        }
        // Switching a group off is a decision made by ear: it goes straight onto AFTER.
        if (controller.getCompare() != MixController::Compare::After) controller.setCompare (MixController::Compare::After);
        paintChips();
        repaint();
        if (page.onToast)
            page.onToast (everythingPicked() ? juce::String ("Hearing the whole proposal again.")
                                             : "AFTER is playing only what is switched on. KEEP applies exactly that.");
    }

    // The design's `List Row` (61:9171): a lamp, what it did, why it did it, and the value at
    // the right. No box - rows are separated by space and a hairline.
    struct Bullet { juce::String what, why, value; bool done = true; };

    static constexpr int kPadX = 30, kPadY = 26;
    static constexpr int kSheetW = 620, kValueW = 92;

    // EVERY WORD IN THE FOOTER IS A VERB, and a verb cut to "Revie..." is not a word. So the
    // row is measured and the sheet is at least as wide as the row it has to carry; only a
    // window too narrow for that makes the row give anything up, and what it gives up is the
    // Inspector link's sentence rather than any letter of it.
    int footerWidth() const
    {
        return juce::jmax (74, before.idealWidth()) + 4 + juce::jmax (74, after.idealWidth())
             + 10 + review.idealWidth() + 16
             + juce::jmax (116, another.idealWidth()) + 8
             + juce::jmax (82, revert.idealWidth()) + 8
             + keepWideW;
    }

    int sheetWidth() const { return juce::jmin (juce::jmax (kSheetW, footerWidth() + kPadX * 2), getWidth() - 80); }

    // A VALUE THAT IS CUT IS A WRONG VALUE. "EQ 2.8 kHz - -2.5 dB - Q 1.2" in a 92 pt column
    // reads as an EQ at 2.8 kHz and nothing else, so the column is as wide as the widest
    // value in this proposal - bounded, because the sentence beside it has to stay readable.
    int valueWidth() const { return layoutFor().valueW; }

    static int linesNeeded (const juce::Font& font, const juce::String& text, int width)
    {
        if (text.isEmpty() || width < 40) return 1;
        juce::AttributedString attributed;
        attributed.setText (text);
        attributed.setFont (font);
        attributed.setJustification (juce::Justification::topLeft);
        juce::TextLayout layout;
        layout.createLayout (attributed, float (width));
        return juce::jlimit (1, 5, layout.getNumLines());     // counted, not estimated from a height
    }

    // THE CARD IS LAID OUT ONCE per proposal and width, not on every paint: each sentence's
    // line count is a text layout, and paint, resized and the sheet's own size all asked for
    // every one of them again - three times a second while the card was up, and again on
    // every chip press. That was the lag.
    struct Laid
    {
        int stamp = -2, width = -1;
        std::vector<Bullet> bullets;
        std::vector<int> whatLines, whyLines, heights;
        int valueW = kValueW, listH = 0;
    };
    mutable Laid laid;

    int contentStamp() const
    {
        const auto* plan = controller.getPlan();
        if (plan == nullptr) return -1;
        const bool live = controller.getTuneLive().getState() == TuneLiveCoordinator::State::Ready;
        return controller.getTuneCount() * 1000003 + plan->parametersChanged * 101 + plan->fadersChanged * 7
             + plan->gainsChanged + (live ? 500009 : 0) + int (controller.getTuneLive().getReview().size()) * 13;
    }

    const Laid& layoutFor() const
    {
        const int stamp = contentStamp();
        const int w = getWidth();
        if (laid.stamp == stamp && laid.width == w) return laid;
        laid = {};
        laid.stamp = stamp;
        laid.width = w;
        laid.bullets = bullets();
        for (const auto& b : laid.bullets)
            if (b.value.isNotEmpty()) laid.valueW = juce::jmax (laid.valueW, Dine::textWidth (Dine::mono (11.0f, 500), b.value));
        laid.valueW = juce::jmin (laid.valueW, 230);
        const int avail = juce::jmax (80, sheetWidth() - kPadX * 2 - laid.valueW - 18 - 17);
        for (const auto& b : laid.bullets)
        {
            const int whatLines = linesNeeded (Dine::text (13.0f, 600), b.what, avail);
            const int whyLines = b.why.isEmpty() ? 0 : linesNeeded (Dine::text (12.0f), b.why, avail);
            const int h = kRowPad + whatLines * 18 + (whyLines > 0 ? 2 + whyLines * 16 : 0) + kRowPad;
            laid.whatLines.push_back (whatLines);
            laid.whyLines.push_back (whyLines);
            laid.heights.push_back (h);
            laid.listH += h;
        }
        return laid;
    }

    int listHeight() const { return layoutFor().listH; }

    static constexpr int kChipH = 26;
    static constexpr int kRowPad = 11;
    // The head: the title, the line saying what it ran on, the line saying what is on air - each
    // with room of its own - then the KEEP SOME row, set apart from the sentences above it.
    static constexpr int kTitleH = 28, kLineH = 18, kAfterTitle = 6, kBetweenLines = 4, kBeforeChips = 14, kAfterChips = 10;
    int headHeight() const { return kTitleH + kAfterTitle + kLineH + kBetweenLines + kLineH; }
    int chipRowHeight() const { return showChips() ? kBeforeChips + kChipH + kAfterChips : 8; }

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = sheetWidth();
        const int content = kPadY + headHeight() + chipRowHeight() + listHeight() + 18 + Dine::Metric::button + kPadY;
        const int h = juce::jlimit (240, juce::jmax (240, getHeight() - 40), content);
        return juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
    }

    std::vector<Bullet> bullets() const
    {
        std::vector<Bullet> out;
        const auto* plan = controller.getPlan();
        if (plan == nullptr) return out;
        if (controller.getTuneLive().getState() == TuneLiveCoordinator::State::Ready)
        {
            using Kind = TuneLiveCoordinator::ReviewLine::Kind;
            for (const auto& line : controller.getTuneLive().getReview())
            {
                juce::String why (line.why);
                if (line.kind == Kind::NotPossible) why = why.isEmpty() ? "DINE cannot do this, so it did not." : why;
                if (line.kind == Kind::Refused && why.isEmpty()) why = "DINE declined this change.";
                out.push_back ({ juce::String (line.what), why, {},
                                 line.kind != Kind::NotPossible && line.kind != Kind::Refused });
                if (out.size() >= 6) return out;
            }
            if (! out.empty()) return out;
        }
        for (const auto& n : plan->notes)
        {
            out.push_back ({ juce::String (n), {}, {}, true });
            if (out.size() >= 2) break;
        }
        for (const auto& rel : plan->relationships)
        {
            if (rel.changes.empty() && rel.kind != Recommendation::Kind::MixGain) continue;
            out.push_back ({ juce::String (rel.what), juce::String (rel.why), formatRecommendationValues (rel), true });
            if (out.size() >= 6) break;
        }
        return out;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::desk.withAlpha (0.88f));
        auto card = sheetBounds();
        Dine::drawSheet (g, card.toFloat(), 14.0f);

        const auto* plan = controller.getPlan();
        if (plan == nullptr) return;

        auto r = card.reduced (kPadX, kPadY);
        auto head = r.removeFromTop (kTitleH);
        head.removeFromRight (closeButton.getWidth() + 10);
        const bool live = controller.getTuneLive().getState() == TuneLiveCoordinator::State::Ready;
        g.setColour (Dine::ink);
        g.setFont (Dine::text (22.0f, 600));
        Dine::drawText (g, tuneVerb (controller, live) + " is ready", head, juce::Justification::centredLeft, true);
        r.removeFromTop (kAfterTitle);
        // WHAT IT RAN ON. A card that says "is ready" without saying what it is a card about
        // is the reason the scopes were invisible in the first place - so every one of them
        // names itself here, the whole mix included.
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, "On " + juce::String (controller.getLastTuneScope()) + ". "
                                 + (plan->noChangeRequired ? juce::String (plan->headline)
                                        : "Heard " + juce::String (plan->stripsHeard) + " inputs, proposed "
                                              + juce::String (plan->parametersChanged) + " settings and "
                                              + juce::String (plan->fadersChanged) + " levels. " + juce::String (plan->headline)),
                              r.removeFromTop (kLineH), juce::Justification::topLeft, 1);
        r.removeFromTop (kBetweenLines);
        // WHAT IS AUDITIONED IS ON AIR. BEFORE and AFTER are what the room and the stream hear,
        // not a private listen, and the card says so where the eye already is.
        g.setColour (Dine::warn);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawFittedText (g, "The room and the stream hear BEFORE and AFTER as you switch. Nothing is kept until KEEP.",
                              r.removeFromTop (kLineH), juce::Justification::centredLeft, 1);

        if (showChips())
        {
            r.removeFromTop (kBeforeChips);
            auto row = r.removeFromTop (kChipH);
            g.setColour (everythingPicked() ? Dine::ink3 : Dine::accent);
            g.setFont (Dine::text (12.0f, 500));
            Dine::drawText (g, everythingPicked() ? "Keep" : "Keeping", row.removeFromLeft (kKeepLabelW), juce::Justification::centredLeft);
            r.removeFromTop (kAfterChips);
        }
        else r.removeFromTop (8);

        r.removeFromBottom (Dine::Metric::button + 18);
        const auto& L = layoutFor();
        for (size_t k = 0; k < L.bullets.size(); ++k)
        {
            const auto& b = L.bullets[k];
            const int h = L.heights[k];
            if (r.getHeight() < h) break;
            auto row = r.removeFromTop (h);
            Dine::drawRule (g, row.withHeight (1), Dine::hairSoft);
            row = row.reduced (0, kRowPad);

            auto lamp = row.removeFromLeft (7).withSizeKeepingCentre (6, 6).withY (row.getY() + 6);
            g.setColour (b.done ? Dine::accent : Dine::warn);
            g.fillEllipse (lamp.toFloat());
            row.removeFromLeft (10);

            auto value = row.removeFromRight (valueWidth());
            row.removeFromRight (18);
            if (b.value.isNotEmpty())
            {
                g.setColour (Dine::ink);
                g.setFont (Dine::mono (11.0f, 500));
                Dine::drawText (g, b.value, value.withHeight (18), juce::Justification::centredRight, true);
            }
            else if (! b.done)
            {
                Dine::drawStatusChip (g, value.removeFromRight (74).withHeight (17).toFloat(), "NOT DONE", Dine::warn);
            }

            const int whatLines = L.whatLines[k];
            g.setColour (b.done ? Dine::ink : Dine::ink3);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawFittedText (g, b.what, row.removeFromTop (whatLines * 18), juce::Justification::topLeft, whatLines, 1.0f);
            if (b.why.isNotEmpty())
            {
                row.removeFromTop (2);
                g.setColour (Dine::ink3);
                g.setFont (Dine::text (12.0f));
                Dine::drawFittedText (g, b.why, row, juce::Justification::topLeft, juce::jmax (1, L.whyLines[k]), 1.0f);
            }
        }
    }

    void resized() override
    {
        auto card = sheetBounds();
        auto r = card.reduced (kPadX, kPadY);
        closeButton.setBounds (r.removeFromTop (kTitleH).removeFromRight (28).withSizeKeepingCentre (28, 28));

        // The design's `Sheet Footer` (65:9354): BEFORE / AFTER on the left, the actions on
        // the right with the default - Keep - last, the way macOS orders them.
        auto row = r.removeFromBottom (Dine::Metric::button);
        before.setBounds (row.removeFromLeft (juce::jmax (74, before.idealWidth())));
        row.removeFromLeft (4);
        after.setBounds (row.removeFromLeft (juce::jmax (74, after.idealWidth())));
        keep.setBounds (row.removeFromRight (keepWideW));
        row.removeFromRight (8);
        revert.setBounds (row.removeFromRight (juce::jmax (82, revert.idealWidth())));
        row.removeFromRight (8);
        another.setBounds (row.removeFromRight (juce::jmax (116, another.idealWidth())));
        row.removeFromRight (16);
        // The one thing in the row that can give ground. It says the whole sentence when the
        // row has space for it, the short form when it does not, and steps out altogether
        // when even that would be cut - the Inspector is in the sidebar either way.
        row.removeFromLeft (10);
        review.setButtonText ("Review in the Inspector");
        if (review.idealWidth() > row.getWidth()) review.setButtonText ("Inspector");
        review.setVisible (review.idealWidth() <= row.getWidth());
        if (review.isVisible()) review.setBounds (row.removeFromLeft (review.idealWidth()));
        r.removeFromBottom (18);
        r.removeFromTop (kAfterTitle + kLineH + kBetweenLines + kLineH);

        if (showChips())
        {
            r.removeFromTop (kBeforeChips);
            auto chipRow = r.removeFromTop (kChipH);
            chipRow.removeFromLeft (kKeepLabelW);
            for (int b = 0; b < int (MixBus::Count); ++b)
            {
                if (! chips[size_t (b)]->isVisible()) continue;
                chips[size_t (b)]->setBounds (chipRow.removeFromLeft (juce::jmax (58, chips[size_t (b)]->idealWidth())).withHeight (kChipH));
                chipRow.removeFromLeft (7);
            }
        }
    }

private:
    static constexpr int kKeepLabelW = 66;

    MixController& controller;
    MixPage& page;
    std::array<std::unique_ptr<DineButton>, size_t (MixBus::Count)> chips;
    std::array<bool, size_t (MixBus::Count)> picked { };
    std::array<bool, size_t (MixBus::Count)> touchedBus { };
    int groupsTouched = 0;
    int builtFor = -1;
    int keepWideW = 74;                 // KEEP's width with its longest label (see the constructor)
    DineButton before { "Before", DineButton::Style::Standard }, after { "After", DineButton::Style::Filled };
    DineButton keep { "Keep", DineButton::Style::Filled }, revert { "Revert", DineButton::Style::Standard };
    DineButton another { "Try another mix", DineButton::Style::Standard };
    DineButton review { "Review in the Inspector", DineButton::Style::Ghost };
    DineButton closeButton { juce::String(), DineButton::Style::Ghost };
};

// ------------------------------------------------------------------ MixPage
MixPage::MixPage (MixController& c) : controller (c)
{
    for (int i = 0; i < kAllTiles; ++i)
    {
        groups[size_t (i)] = std::make_unique<GroupTile> (controller, i);
        if (i == kGroupBuses) groups[size_t (i)]->onOpen = [this] { showEffects (! effectsOpen); };
        if (i == 0)
        {
            backToGroups.setTooltip ("The groups again. The effects keep the levels you gave them.");
            backToGroups.onClick = [this] { showEffects (false); };
            addChildComponent (backToGroups);
        }
        if (i < kGroupBuses)
            groups[size_t (i)]->onTune = [this, i]
            {
                controller.startTuneBus (groupBus (i));
                if (onToast) onToast ("Listening to " + groupName (i) + " alone. Every other group, and the master, stay where they are.");
            };
        addAndMakeVisible (*groups[size_t (i)]);
    }
    {
        const auto set = [this] (MixMacro m, float v) { controller.setMacro (m, v); };
        const juce::String times = juce::String::fromUTF8 ("\xC3\x97");
        pads[0] = std::make_unique<MacroPad> ("BODY " + times + " VOICE", MixMacro::Bass, MixMacro::Vocals,
                                              MacroPad::Corners { "AIRY", "PRESENT", "DARK", "THICK" },
                                              std::vector<MacroPad::Snap> { { "Speech", 30.0f, 78.0f }, { "Choir", 68.0f, 58.0f }, { "Plan", 50.0f, 50.0f } }, set);
        pads[1] = std::make_unique<MacroPad> ("DRIVE " + times + " ROOM", MixMacro::Space, MixMacro::Drums,
                                              MacroPad::Corners { "DRY HIT", "BIG HIT", "FLAT", "WASH" },
                                              std::vector<MacroPad::Snap> { { "Tight", 22.0f, 74.0f }, { "Room", 76.0f, 62.0f }, { "Plan", 50.0f, 50.0f } }, set);
        for (auto& p : pads) { p->onActiveChanged = [this] { updateSide(); }; addAndMakeVisible (*p); }
        ribbon = std::make_unique<MacroRibbon> (MixMacro::Energy, [this] (float v) { controller.setMacro (MixMacro::Energy, v); });
        addAndMakeVisible (*ribbon);
    }
    railView.setViewedComponent (&railHolder, false);
    Dine::nativeScrolling (railView);
    railView.setScrollBarsShown (true, false);
    addAndMakeVisible (railView);

    railTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Left, "Inputs");
    railTab->onClick = [this] { setRailShown (! railShown); };
    addAndMakeVisible (*railTab);

    side = std::make_unique<SidePanel> (*this);
    sideView.setViewedComponent (side.get(), false);
    Dine::nativeScrolling (sideView);
    sideView.setScrollBarsShown (true, false);
    addAndMakeVisible (sideView);
    // THE PANEL IS NAMED FOR WHAT IT IS, not for its last section. Folded, the gutter is the
    // only thing left of TUNE MIX, RE-TUNE LIVE, MATCH TO REFERENCE and the mix history - and
    // it used to be labelled MIX HEALTH, so a workspace that had folded it looked like it had
    // lost the whole-mix tune and kept only the one on each group tile.
    sideTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Right, "Tune");
    sideTab->onClick = [this] { setSideShown (! sideShown); };
    addAndMakeVisible (*sideTab);

    scopeSheet = std::make_unique<ScopeSheet> (controller);
    listenSheet = std::make_unique<ListenSheet> (controller, inputArriving);
    resultSheet = std::make_unique<ResultSheet> (controller, *this);
    referenceSheet = std::make_unique<ReferenceSheet> (controller);
    for (auto* b : { &tuneButton, &liveTuneButton, &referenceButton, &checkButton, &chatButton,
                     &undoButton, &redoButton, &historyButton, &advancedButton, &aimButton })
        side->addAndMakeVisible (*b);

    // WHAT TO TUNE, as a segment rather than a question asked after the press.
    {
        const char* names[3] = { "Whole mix", "One group", "Channels" };
        for (int i = 0; i < 3; ++i)
        {
            scopeTabs[size_t (i)] = std::make_unique<DineButton> (names[i], DineButton::Style::Segment);
            scopeTabs[size_t (i)]->setFontPx (11.5f);
            scopeTabs[size_t (i)]->onClick = [this, i]
            {
                wantedScope = i;
                for (int j = 0; j < 3; ++j) scopeTabs[size_t (j)]->setToggleState (j == i, juce::dontSendNotification);
                layoutSide();
                side->repaint();
            };
            side->addAndMakeVisible (*scopeTabs[size_t (i)]);
        }
        scopeTabs[0]->setToggleState (true, juce::dontSendNotification);
    }
    checkButton.setFontPx (12.5f);
    checkButton.onClick = [this] { if (onOpenCheck) onOpenCheck(); };
    checkButton.setTooltip ("Every assigned input, what is arriving on it and one word about it. Reading only: "
                            "nothing there changes the mix.");
    aimButton.setFontPx (12.5f);
    aimButton.setIcon (Dine::Icon::Purpose);
    aimButton.setTooltip ("The favourite mix the next TUNE MIX is fitted to: not its fader positions, but how things "
                          "sat against each other when it sounded right.");
    aimButton.onClick = [this] { if (onOpenFavourites) onOpenFavourites(); };
    addAndMakeVisible (resetMacrosButton);
    addChildComponent (*referenceSheet);
    addChildComponent (*resultSheet);
    addChildComponent (*listenSheet);
    addChildComponent (*scopeSheet);

    scopeSheet->onClose = [this] { scopeSheet->setVisible (false); refreshTuneButton(); repaint(); };
    scopeSheet->onToast = [this] (const juce::String& t) { if (onToast) onToast (t); };

    referenceSheet->onClose = [this] { referenceSheet->setVisible (false); refreshTuneButton(); repaint(); };
    referenceSheet->onToast = [this] (const juce::String& t) { if (onToast) onToast (t); };

    tuneButton.setCaps (true);
    tuneButton.setFontPx (13.0f);
    tuneButton.onClick = [this] { pressTune(); };
    tuneButton.setTooltip ("Listen to the band and build DINE's mix from what it measures. Deterministic: the same "
                           "listen always gives the same mix, and nothing leaves this machine.");
    liveTuneButton.setCaps (true);
    liveTuneButton.setFontPx (13.0f);
    liveTuneButton.onClick = [this] { pressLiveTune(); };
    liveTuneButton.setTooltip ("The same listen, with a mix engineer's reasoning on top: DINE builds its mix, works out "
                               "what this band still needs, applies only what it can do safely, then listens again to "
                               "check. You can compare, review every change and revert.");
    referenceButton.setFontPx (12.5f);
    referenceButton.onClick = [this] { openReference(); };
    referenceButton.setTooltip ("Aim the mix at a finished recording: DINE matches the master's tone, image and density "
                                "to it. How loud the stream is delivered, and who is loud in the mix, are not copied.");
    chatButton.setFontPx (12.5f);
    chatButton.onClick = [this] { if (onOpenChat) onOpenChat(); };
    chatButton.setTooltip ("Ask for a change in plain words. DINE says what it intends to do before anything is yours.");
    undoButton.setQuiet (true);
    redoButton.setQuiet (true);
    historyButton.setQuiet (true);
    historyButton.setFontPx (12.5f);
    historyButton.setTooltip ("Every mix this session has had, by time and by name: a tune, a scene, a morning of "
                              "mixing. Going back to one keeps where you are now, so it is never a one-way door.");
    historyButton.onClick = [this] { if (onOpenHistory) onOpenHistory(); };
    undoButton.setFontPx (12.5f);
    redoButton.setFontPx (12.5f);
    undoButton.setTooltip ("Step back a whole mix");
    redoButton.setTooltip ("Step forward a whole mix");
    undoButton.onClick = [this]
    {
        if (! controller.canUndoMix()) { if (onToast) onToast ("There is no earlier mix to step back to in this session."); return; }
        controller.undoMix();
        if (onToast) onToast ("Stepped back a whole mix. Every value it set went with it.");
    };
    redoButton.onClick = [this]
    {
        if (! controller.canRedoMix()) { if (onToast) onToast ("This is the newest mix in this session."); return; }
        controller.redoMix();
        if (onToast) onToast ("Stepped forward a whole mix.");
    };
    advancedButton.setFontPx (12.5f);
    advancedButton.onClick = [this] { if (onOpenAdvanced) onOpenAdvanced(); };
    resetMacrosButton.setFontPx (13.0f);
    resetMacrosButton.setTooltip ("Both pads and the ENERGY ribbon back to the plan - exactly what TUNE MIX built.");
    resetMacrosButton.onClick = [this] { centreMacroPads(); };

    // ---- MASTER: the target, the lift and the sound
    addAndMakeVisible (loudnessTargetButton);
    addAndMakeVisible (raiseButton);
    addAndMakeVisible (voicingButton);
    loudnessTargetButton.setTooltip ("How loud the finished mix should be. YouTube, Facebook and Spotify normalise to about -14 LUFS; "
                                     "a television broadcast to -23. TUNE MIX fits the whole gain structure to it, and Raise loudness gets "
                                     "the master there without re-tuning.");
    raiseButton.setTooltip ("Raises the master to the loudness target in one move, under the master limiter, so it cannot clip. "
                            "Undo with Undo mix.");
    voicingButton.setTooltip ("Who the mix is for. A voicing sits on the master's tone on top of the kept mix and never changes it: "
                              "switch back to \"as tuned\" and it is exactly what TUNE MIX built.");
    loudnessTargetButton.onClick = [this]
    {
        juce::PopupMenu m;
        const auto current = controller.getDelivery();
        const auto purposeTarget = Profiles::targets (controller.getSession().profile, masterRoleFor (controller.getSession().purpose));
        m.addItem (1, juce::String (deliveryLoudnessName (DeliveryLoudness::FromPurpose)) + "   (" + juce::String (purposeTarget.targetLufs, 0) + " LUFS)",
                   true, current == DeliveryLoudness::FromPurpose);
        m.addSeparator();
        for (int i = 1; i < int (DeliveryLoudness::Count); ++i)
        {
            const auto d = DeliveryLoudness (i);
            m.addItem (i + 1, juce::String (deliveryLoudnessName (d)) + "   " + juce::String (deliveryLoudnessLufs (d), 0) + " LUFS", true, current == d);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loudnessTargetButton).withMinimumWidth (300),
                         [this] (int id) { if (id > 0) { controller.setDelivery (DeliveryLoudness (id - 1)); refreshMaster(); resized(); repaint(); } });
    };
    raiseButton.onClick = [this]
    {
        const auto said = controller.raiseLoudnessToTarget();
        if (onToast) onToast (juce::String (said));
        refreshMaster(); repaint();
    };
    voicingButton.onClick = [this]
    {
        juce::PopupMenu m;
        const auto current = controller.getVoicing();
        for (int i = 0; i < int (MasterVoicing::Count); ++i)
        {
            const auto v = MasterVoicing (i);
            m.addItem (i + 1, juce::String (masterVoicingName (v)) + "   " + Glyph::dot() + "   " + juce::String (masterVoicingHint (v)), true, current == v);
            if (i == 0) m.addSeparator();
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (voicingButton).withMinimumWidth (300),
                         [this] (int id)
                         {
                             if (id <= 0) return;
                             controller.setVoicing (MasterVoicing (id - 1));
                             if (onToast) onToast (MasterVoicing (id - 1) == MasterVoicing::Neutral ? juce::String ("Master back to exactly what TUNE MIX built.")
                                                                                                    : "Master voiced for " + juce::String (masterVoicingName (MasterVoicing (id - 1))).toLowerCase() + ". The kept mix is untouched.");
                             refreshMaster(); resized(); repaint();
                         });
    };
    refreshMaster();
    setOpaque (true);
    refresh();
}

MixPage::~MixPage() = default;

void MixPage::setRailAvailable (bool available)
{
    if (available == railAvailable) return;
    railAvailable = available;
    railTab->setVisible (available);
    railView.setVisible (available && railOut());
    resized();
    repaint();
}

void MixPage::setRailShown (bool shown)
{
    if (shown == railShown) return;
    railShown = shown;
    railTab->setCollapsed (! shown);
    railView.setVisible (railAvailable);
    railSlide.setOpen (shown);           // lays the page out on every step, and once when it lands
}

void MixPage::setSideShown (bool shown)
{
    if (shown == sideShown) return;
    sideShown = shown;
    sideTab->setCollapsed (! shown);
    sideView.setVisible (true);
    sideSlide.setOpen (shown);
    // Folding this one away takes the verb the whole workspace is named after off the screen,
    // and the keyboard can do it by accident. Say where it went and how to get it back.
    if (! shown && onToast)
        onToast ("TUNE MIX and the rest of the verbs are folded away. The tab down the right-hand edge brings them back.");
}

void MixPage::openReference()
{
    if (listenSheet->isVisible() || resultSheet->isVisible()) return;
    if (referenceSheet->isVisible() && ! referenceSheet->isMeasuring()) { referenceSheet->setVisible (false); repaint(); return; }
    referenceSheet->setVisible (true);
    referenceSheet->toFront (false);
    referenceSheet->refresh();
    resized();
}

// TUNE always asked the same question and never asked it out loud. Now it does: the scope
// picker opens, and the listen starts from there. Pressing it while DINE is already
// listening still means stop, because that is the only thing it can mean.
void MixPage::pressTune()
{
    if (controller.isListening() || controller.isTuningLive()) { controller.abortTuneMix(); refreshTuneButton(); return; }
    if (scopeSheet->isVisible()) { scopeSheet->setVisible (false); refreshTuneButton(); repaint(); return; }
    referenceSheet->setVisible (false);
    // The scope is already chosen, on the panel. The whole mix needs nothing more said about
    // it, so it starts; one group and some channels still have to be told *which*, and that is
    // what the picker is for. Either way nothing is committed - the listen ends on BEFORE /
    // AFTER, and KEEP is the only thing that moves the mix.
    if (wantedScope == 0)
    {
        controller.startTuneMix();
        refreshTuneButton();
        repaint();
        return;
    }
    scopeSheet->open (selectedRow);
    scopeSheet->chooseForSnapshot (wantedScope, -1);
    refreshTuneButton();
    repaint();
}

bool MixPage::isScopeSheetOpen() const { return scopeSheet != nullptr && scopeSheet->isVisible(); }

void MixPage::closeScopeSheet()
{
    if (scopeSheet == nullptr || ! scopeSheet->isVisible()) return;
    scopeSheet->setVisible (false);
    refreshTuneButton();
    repaint();
}

void MixPage::setScopeForSnapshot (int scope, int group)
{
    // What the segment on the panel does, without a mouse: the scope the next TUNE MIX will
    // act on, and - while the picker is open - which tab it is showing.
    wantedScope = juce::jlimit (0, 2, scope);
    for (int i = 0; i < 3; ++i)
        if (scopeTabs[size_t (i)] != nullptr)
            scopeTabs[size_t (i)]->setToggleState (i == wantedScope, juce::dontSendNotification);
    layoutSide();
    if (side != nullptr) side->repaint();
    if (scopeSheet == nullptr || ! scopeSheet->isVisible()) return;
    scopeSheet->chooseForSnapshot (scope, group);
}

void MixPage::pressLiveTune()
{
    if (controller.isTuningLive() || controller.isListening()) { controller.abortTuneMix(); refreshTuneButton(); return; }
    controller.startTuneLiveMix();
    refreshTuneButton();
}

void MixPage::setMacroValue (MixMacro m, float v)
{
    controller.setMacro (m, v);
    syncMacros();
    updateSide();
}

void MixPage::centreMacroPads()
{
    controller.resetMacros();
    for (auto& p : pads) p->settleTo (50.0f, 50.0f);
    ribbon->settleTo (50.0f);
    updateSide();
}

void MixPage::syncMacros()
{
    const auto& m = controller.getMacros();
    pads[0]->setValues (m.get (MixMacro::Bass), m.get (MixMacro::Vocals));
    pads[1]->setValues (m.get (MixMacro::Space), m.get (MixMacro::Drums));
    ribbon->setValue (m.get (MixMacro::Energy));
    const auto range = controller.macroRange();
    const juce::String why (liveSafe::macroLimitReason (controller.getLiveSafePolicy()));
    for (auto& p : pads) p->setLimits (range.lo, range.hi, why);
    ribbon->setLimits (range.lo, range.hi, why);
}

// The card on the right says what the pad you are holding does, or which pad is off the
// plan; its body is the two macros' own sentences.
void MixPage::updateSide()
{
    juce::String heading, body;
    const MacroPad* about = nullptr;
    for (auto& p : pads) if (p->isActive()) { about = p.get(); heading = "HOLDING  " + juce::String (Glyph::dot()) + "  " + p->getTitle(); }
    if (about == nullptr)
    {
        const bool a = pads[0]->isOffCentre(), b = pads[1]->isOffCentre();
        if (a && b)  { about = pads[0].get(); heading = "BOTH PADS MOVED"; }
        else if (a)  { about = pads[0].get(); heading = pads[0]->getTitle() + " MOVED"; }
        else if (b)  { about = pads[1].get(); heading = pads[1]->getTitle() + " MOVED"; }
    }
    if (about != nullptr)
        body = juce::String (MixMacros::tooltip (about->getAcross())) + "  " + MixMacros::tooltip (about->getUp());
    else
    {
        heading = "BOTH PADS AT THE PLAN";
        body = "The dashed ring at the centre of each pad is what TUNE MIX built. Press anywhere on a pad to lean the mix "
               "away from it - across for one control, up and down for the other - and double-click to come back.";
    }
    const auto key = heading + "|" + body;
    if (key == painted.padCard) return;
    painted.padCard = key;
    side->setCard (heading, body);
    layoutSide();
    side->repaint();
}

void MixPage::layoutSide()
{
    const auto view = sideView.getBounds();
    if (view.isEmpty()) return;
    const int need = side->layoutFor (view.getWidth(), false);
    const int w = view.getWidth() - (need > view.getHeight() ? 10 : 0);
    side->setSize (w, juce::jmax (need, view.getHeight()));
    side->resized();
}

void MixPage::refreshTuneButton()
{
    const auto stage = controller.getStage();
    const bool live = controller.isTuningLive();
    const bool busy = live || stage == MixController::Stage::Listening || stage == MixController::Stage::Planning;
    const bool ready = controller.isPrepared() && controller.getEngine().getNumStrips() > 0;

    tuneButton.setButtonText (busy && ! live ? "Cancel" : controller.getTuneCount() > 0 ? "RE-TUNE" : "TUNE MIX");
    tuneButton.setEnabled (ready && ! live && stage != MixController::Stage::Planning);
    liveTuneButton.setButtonText (live ? "Stop" : controller.getTuneCount() > 0 ? "RE-TUNE LIVE" : "TUNE LIVE MIX");
    liveTuneButton.setEnabled (ready && (live || stage != MixController::Stage::Listening) && stage != MixController::Stage::Planning);
    referenceButton.setButtonText (controller.hasReference() ? "Matched " + juce::String (Glyph::check())
                                                             : juce::String ("Match to reference"));
    referenceButton.setEnabled (ready && ! busy);
    chatButton.setEnabled (ready);
    checkButton.setEnabled (ready);
    for (auto& tab : scopeTabs) if (tab != nullptr) tab->setEnabled (ready && ! busy);

    // AIM AT: the favourite this tune is fitted to, by name.
    {
        const int n = controller.numFavourites();
        juce::String at;
        if (controller.hasReference())
            for (int i = 0; i < n; ++i)
                if (juce::String (controller.getFavourite (i).name) == juce::String (controller.getReference().name))
                    at = juce::String (controller.getFavourite (i).name);
        aimButton.setButtonText (at.isNotEmpty() ? at
                               : n > 0 ? juce::String ("Pick a favourite mix")
                                       : juce::String ("Nothing marked yet"));
        aimButton.setEnabled (n > 0);
    }
    resized();
}

void MixPage::rebuildRail()
{
    inputRows.clear();
    railHolder.removeAllChildren();
    if (! controller.isPrepared()) { builtRailFor = -1; return; }
    const auto& graph = controller.getGraph();
    // v4: the inputs by family, in the console's order, each family under its caption.
    // `inputRows` stays indexed by strip; `railOrder` is the order they are laid out in.
    railOrder.clear();
    for (int b = 0; b < int (MixBus::Master); ++b)
        for (int i = 0; i < graph.numStrips(); ++i)
            if (graph.strips[size_t (i)].bus == mixBusInDisplayOrder (b)) railOrder.push_back (i);
    for (int i = 0; i < graph.numStrips(); ++i)
        if (std::find (railOrder.begin(), railOrder.end(), i) == railOrder.end()) railOrder.push_back (i);
    for (int i = 0; i < graph.numStrips(); ++i)
    {
        const auto& s = graph.strips[size_t (i)];
        auto row = std::make_unique<InputRow> (s.name, s.role, s.inputA + 1, s.icon);
        row->tint = Dine::busTint (s.bus);
        {
            const auto first = std::find_if (railOrder.begin(), railOrder.end(),
                                              [&] (int k) { return graph.strips[size_t (k)].bus == s.bus; });
            if (first != railOrder.end() && *first == i)
            {
                const juce::String raw (mixBusName (s.bus));
                row->caption = raw.length() <= 3 ? raw.toUpperCase() : raw.substring (0, 1).toUpperCase() + raw.substring (1).toLowerCase();
            }
        }
        row->onTune = [this, i] { selectRow (i); if (onTuneStrip) onTuneStrip (i); };
        row->onSelect = [this, i] { selectRow (i); };
        row->onFocus = [this, in = s.input] { controller.setFocusInput (in); rebuildRail(); };
        row->onHeightChanged = [this] { resized(); };
        railHolder.addAndMakeVisible (*row);
        inputRows.push_back (std::move (row));
    }
    builtRailFor = graph.numStrips();
    if (selectedRow >= builtRailFor) selectedRow = -1;
    rebuildVoices();
    resized();
}

// WHAT EACH VOICE IS DOING. One row per microphone somebody talks or sings into, built from
// the controller's own list so a channel that is not a voice is never offered a job.
void MixPage::rebuildVoices()
{
    voiceRows.clear();
    if (! controller.isPrepared()) { builtVoicesFor = -1; return; }
    const auto& graph = controller.getGraph();
    for (int i = 0; i < graph.numStrips(); ++i)
    {
        // A strip is where an input sits on the console; the role belongs to the input, and
        // the two numbers differ as soon as an input before it is switched off.
        const auto& st = graph.strips[size_t (i)];
        const int in = st.input;
        if (! controller.isVoiceChannel (in)) continue;
        auto row = std::make_unique<VoiceRow> (st.name, Dine::busTint (st.bus));
        // SPEAKING keeps what kind of speaking microphone this is: a lapel that was singing
        // and goes back to speaking is a lapel again, not a generic speech channel.
        row->speaking.onClick = [this, in]
        {
            if (controller.setInputRole (in, controller.roleForJob (in, ChannelRole::Speech)) && onGraphChanged) onGraphChanged();
            rebuildVoices();
            layoutSide();
        };
        row->singing.onClick = [this, in]
        {
            // Singing, and which kind of singing it was: a backing voice that spoke for a
            // minute is a backing voice again (MixController::roleForSinging), never the lead.
            if (controller.setInputRole (in, controller.roleForSinging (in)) && onGraphChanged) onGraphChanged();
            rebuildVoices();
            layoutSide();
        };
        row->setJob (roleFamily (st.role) == RoleFamily::Speech);
        side->addAndMakeVisible (*row);
        voiceRows.push_back (std::move (row));
    }
    builtVoicesFor = graph.numStrips();
    layoutSide();
}

void MixPage::selectRow (int strip)
{
    selectedRow = strip;
    if (onSelectStrip) onSelectStrip (strip);
}

void MixPage::refresh()
{
    const auto& engine = controller.getEngine();
    const auto& graph = engine.getGraph();
    const bool listening = controller.isListening();
    const auto& kept = controller.getBase();
    for (int i = 0; i < kGroupBuses; ++i)
    {
        const MixBus bus = groupBus (i);
        const bool used = controller.isPrepared() && engine.isBusUsed (bus);
        const auto& m = engine.getBus (bus).getOutputMeter();
        groups[size_t (i)]->set (used, used ? controller.busPeakDb (bus) : -120.0f, used ? m.getMaxRmsDb() : -120.0f, used && DineMeter::isClip (controller.busPeakDb (bus)),
                                 controller.isGroupMuted (bus), kept.buses[size_t (bus)].faderDb,
                                 ! used ? 0 : listening ? (controller.busHeard (bus) ? 2 : 1) : 0);
    }
    {
        int returns = 0; float peak = -120.0f, rms = -120.0f; bool clip = false;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (controller.isPrepared() && engine.isFxUsed (FxSlot (f)))
            {
                ++returns;
                const auto& m = engine.getFx (FxSlot (f)).getOutputMeter();
                peak = juce::jmax (peak, controller.fxPeakDb (FxSlot (f))); rms = juce::jmax (rms, m.getMaxRmsDb()); clip = clip || DineMeter::isClip (controller.fxPeakDb (FxSlot (f)));
            }
        groups[size_t (kGroupBuses)]->set (returns > 0, peak, rms, clip, kept.fxMute, kept.fxReturnDb, 0);
        groups[size_t (kGroupBuses)]->setOpen (effectsOpen);
        for (int t = kGroupTiles; t < kAllTiles; ++t)
        {
            const auto slot = returnSlot (t);
            const bool used = controller.isPrepared() && engine.isFxUsed (slot);
            const auto& m = engine.getFx (slot).getOutputMeter();
            const auto& fp = kept.fx[size_t (slot)];
            groups[size_t (t)]->set (used, used ? controller.fxPeakDb (slot) : -120.0f, used ? m.getMaxRmsDb() : -120.0f, used && DineMeter::isClip (controller.fxPeakDb (slot)),
                                     fp.mute, juce::jmax (fp.returnDb, -60.0f), 0);
        }
        if (effectsOpen && returns == 0) showEffects (false);     // the session lost its effects: nothing to open
    }

    // ---- the input rail: the faint / muted marks, re-read a few times a second
    if (controller.isPrepared() && graph.numStrips() != builtRailFor) rebuildRail();
    if (controller.isPrepared() && (adviceTicks++ % 5) == 0)
    {
        const auto* plan = controller.getPlan();
        for (int i = 0; i < int (inputRows.size()) && i < graph.numStrips(); ++i)
        {
            const bool faint = plan != nullptr && i < int (plan->strips.size()) && plan->strips[size_t (i)].faint;
            // The gain verdict as the Mixer's chip says it, when the desk has something to do.
            juce::String chip;
            const auto advice = controller.getInputAdvice (i);
            if (advice.needsAttention())
            {
                using Level = MixController::InputAdvice::Level;
                const int move = juce::roundToInt (advice.consoleMoveDb);
                const juce::String word = advice.level == Level::Clipping ? "Clipping" : advice.level == Level::Digital ? "Digital"
                                        : advice.level == Level::Hot ? "Hot" : advice.level == Level::NotHeard ? "Not heard" : "Low";
                chip = move == 0 ? word : word + " " + (move > 0 ? juce::String ("+") : Glyph::minus()) + juce::String (std::abs (move));
            }
            inputRows[size_t (i)]->set (i < kept.numStrips && kept.strips[size_t (i)].mute, faint, i == selectedRow,
                                        graph.strips[size_t (i)].input == controller.getFocusInput(), chip);
        }
    }

    health = controller.getMixHealthPercent();
    status = controller.getStatusText();
    const auto stage = controller.getStage();
    const bool mixTune = ! controller.isTuningChannel();
    const bool live = controller.isTuningLive();
    const bool listenOn = mixTune && (live || stage == MixController::Stage::Listening || stage == MixController::Stage::Planning);
    const bool preview = mixTune && ! live && stage == MixController::Stage::Preview && controller.hasPlan();
    if ((listenOn || preview) && referenceSheet->isVisible() && ! referenceSheet->isMeasuring()) referenceSheet->setVisible (false);
    if (referenceSheet->isVisible() || referenceSheet->isMeasuring()) referenceSheet->refresh();
    if (listenSheet->isVisible() != listenOn) { listenSheet->setVisible (listenOn); if (listenOn) listenSheet->toFront (false); }
    if (resultSheet->isVisible() != preview) { resultSheet->setVisible (preview); if (preview) { resultSheet->toFront (false); resized(); } }
    if (listenOn) { listenSheet->tick(); listenSheet->resized(); listenSheet->repaint(); }
    if (preview && (adviceTicks % 10) == 0) resultSheet->refresh();
    if (stage != lastStage || live != lastLiveRun) { refreshTuneButton(); lastStage = stage; lastLiveRun = live; }
    syncMacros();
    updateSide();
    if ((adviceTicks % 6) == 0) refreshMaster();

    juce::String notes;
    for (const auto& n : controller.getMixHealthNotes()) notes << juce::String (n) << "|";
    const PageLook now { status, notes, health, stage, controller.getTuneCount(), controller.hasReference(),
                         controller.canUndoMix(), controller.canRedoMix(),
                         controller.hasReference() ? juce::String (controller.getReference().name) : juce::String(), masterNote,
                         painted.padCard };
    if (now != painted)
    {
        painted = now;
        undoButton.setEnabled (now.canUndo);
        redoButton.setEnabled (now.canRedo);
        historyButton.setEnabled (! controller.getCheckpoints().empty());
        layoutSide();
        side->repaint();
        repaint();
    }
}

void MixPage::refreshMaster()
{
    const auto lufs = [] (float v) { return juce::String (std::round (v * 10.0f) / 10.0f, 1) + " LUFS"; };
    const auto delivery = controller.getDelivery();
    const float target = controller.getMasterLoudness().targetLufs;
    const auto deliveryName = delivery == DeliveryLoudness::FromPurpose ? juce::String ("From the purpose")
                                                                       : juce::String (deliveryLoudnessName (delivery));
    loudnessTargetButton.setValue (deliveryName + "  " + Glyph::dot() + "  " + lufs (target));
    // A narrow window drops the name of the target rather than half of it: the number is
    // the part that has to be right.
    loudnessTargetButton.setBriefValue (lufs (target));
    const auto voicingName = controller.getVoicing() == MasterVoicing::Neutral ? juce::String ("as tuned")
                                                                              : juce::String (masterVoicingName (controller.getVoicing()));
    voicingButton.setValue ("Sound: " + voicingName);
    voicingButton.setBriefValue (voicingName);
    const auto move = controller.previewLoudnessMove();
    raisePossible = move.possible;
    raiseButton.setEnabled (move.possible && ! controller.isLiveSafe());
    // The band shows one readout - where the master is and where it is going - and the whole
    // sentence (what one press would do, or why it cannot) lives on the button it is about.
    juce::String sentence;
    if (move.possible)
        sentence = "Now " + lufs (move.fromLufs) + ", aiming at " + lufs (move.targetLufs) + ": "
                 + (move.moveDb > 0 ? "+" : "") + juce::String (move.moveDb, 1) + " dB on the master, under the limiter so it cannot clip.";
    else
        sentence = controller.isLiveSafe() ? juce::String ("LIVE SAFE is on: the master stays where it is until it is off.")
                                           : juce::String (move.why);
    raiseButton.setTooltip ("Raises the master to the loudness target in one move, under the master limiter, so it cannot clip.  " + sentence);
    const auto loud = controller.getMasterLoudness();
    const bool known = loud.known && loud.integratedLufs > -100.0f;
    masterNote = known ? "Now " + juce::String (loud.integratedLufs, 1) + ", aiming at " + juce::String (loud.targetLufs, 1) : juce::String();

    // The three numbers on the master card.
    masterLevelText  = dbText (controller.getBase().buses[size_t (MixBus::Master)].faderDb);
    masterLoudText   = known ? juce::String (loud.integratedLufs, 1) + " now  " + Glyph::dot() + "  "
                                   + juce::String (loud.targetLufs, 0) + " target"
                             : juce::String ("not measured yet");
    // The narrowest form of each number: the cell is labelled, so the unit and the target it
    // is being read against are already on the card.
    masterLoudBrief  = known ? juce::String (loud.integratedLufs, 1) : juce::String ("not yet");
    masterPeakText   = loud.truePeakDb <= -100.0f ? juce::String (Glyph::dash())
                                                  : juce::String (loud.truePeakDb, 1) + " dBTP";
    masterPeakBrief  = loud.truePeakDb <= -100.0f ? juce::String (Glyph::dash())
                                                  : juce::String (loud.truePeakDb, 1);
    masterOnTarget   = ! known || loud.onTarget();
    masterPeakOver   = loud.truePeakDb > loud.ceilingDb + 0.1f;
}

MixPage::Layout MixPage::layout() const
{
    Layout l;
    auto b = getLocalBounds();
    l.rail = b.removeFromLeft (railWidth());
    l.railTab = railOut() ? l.rail.removeFromRight (0) : l.rail;
    l.side = b.removeFromRight (sideWidth());
    l.sideTab = sideOut() ? l.side.withHeight (36).removeFromRight (30) : l.side;
    auto main = b.reduced (24, 22);

    // The middle column never scrolls. The groups take what is left after the master row and
    // the pads, down to a floor of 150; when even that does not fit, the pads drop their snap
    // rows first. What is spare goes to the pads (up to their 214) before the groups.
    // A heading is 20 and its gap 10; the master card is 66 under its own heading.
    const int fixed = (20 + 10) + 24 + (20 + 10 + kMasterCardH) + 26 + (20 + 12);
    // The groups are one row of columns now, so the block is one tile tall rather than nine.
    const int groupsFloor = 168;
    const int groupsWant = GroupTile::height;
    l.compact = main.getHeight() < fixed + MacroPad::heightFor (MacroPad::kMinPad, false) + groupsFloor;
    // The groups take what they need first - they are the mix - and the pads take the rest,
    // down to their floor. Nothing here may add up to more than the column: the ENERGY ribbon
    // is the last thing in it, and a ribbon half under the chain foot is a ribbon nobody can use.
    const int groupsH = juce::jlimit (groupsFloor, groupsWant,
                                      main.getHeight() - fixed - MacroPad::heightFor (MacroPad::kMinPad, l.compact));
    const int padCap = juce::jmax (MacroPad::kMinPad, juce::jmin (MacroPad::kMaxPad, (main.getWidth() - 12) / 2));
    l.padSize = MacroPad::kMinPad;
    for (int size = padCap; size > MacroPad::kMinPad; size -= 2)
        if (fixed + groupsH + MacroPad::heightFor (size, l.compact) <= main.getHeight()) { l.padSize = size; break; }
    const int padsH = MacroPad::heightFor (l.padSize, l.compact);

    l.groupsCaption = main.removeFromTop (20);
    main.removeFromTop (10);
    l.groups = main.removeFromTop (groupsH);
    main.removeFromTop (24);
    // The master: a heading, then one card with what it is set to, how loud it is and the lift.
    l.master = main.removeFromTop (20 + 10 + kMasterCardH);
    main.removeFromTop (26);
    l.macrosCaption = main.removeFromTop (20);
    main.removeFromTop (12);
    // BODY x VOICE, DRIVE x ROOM and ENERGY sit side by side, the way the design sets them:
    // three controls in a row rather than two and one underneath.
    auto padRow = main.removeFromTop (padsH);
    const int compW = juce::jmin (l.padSize + 40, (padRow.getWidth() - 2 * 24) / 3);
    const int block = juce::jmin (padRow.getWidth(), compW * 3 + 2 * 24);
    padRow = padRow.withSizeKeepingCentre (block, padRow.getHeight());
    l.pads[0] = padRow.removeFromLeft (compW);
    padRow.removeFromLeft (24);
    l.pads[1] = padRow.removeFromLeft (compW);
    padRow.removeFromLeft (24);
    // The ribbon is one row high; it sits at the top of its column, level with the pads' words.
    l.ribbon = padRow.withHeight (MacroRibbon::kStackedHeight + 22).withTrimmedTop (22);
    return l;
}

void MixPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto l = layout();

    // ---- the right panel's ground and its edge (the panel itself paints its words)
    g.setColour (Dine::sidebar);
    g.fillRect (l.side.getUnion (l.sideTab));
    g.setColour (Dine::hair);
    g.fillRect (l.side.getX(), l.side.getY(), 1, l.side.getHeight());

    // v4: a 15 pt heading, and beside it a quiet sentence saying what the section is.
    auto heading = [&g] (juce::Rectangle<int> r, const juce::String& text, const juce::String& sub = {})
    {
        const auto font = Dine::text (15.0f, 700);
        const int w = juce::jmin (r.getWidth(), Dine::textWidth (font, text));
        g.setColour (Dine::ink);
        g.setFont (font);
        Dine::drawText (g, text, r.removeFromLeft (w), juce::Justification::centredLeft, true);
        r.removeFromLeft (10);
        if (sub.isNotEmpty() && Dine::textWidth (Dine::text (12.0f), sub) <= r.getWidth())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawText (g, sub, r, juce::Justification::centredLeft);
        }
    };
    heading (l.groupsCaption.withTrimmedRight (effectsOpen ? backToGroups.getWidth() + 12 : 0),
             effectsOpen ? "Effects" : "Groups",
             effectsOpen ? "Each effect return on its own fader" : "The group buses and the effects returns, in console order");
    heading (l.macrosCaption.withTrimmedRight (resetMacrosButton.getWidth() + 12), "Shape the mix",
             "The centre is the plan. Drag anywhere; double-click to go back to it.");

    // ---- the master: one card, the way the design draws it - what it is set to, how loud it
    // actually is against the target it was given, and what the true peak reached.
    {
        auto r = l.master;
        heading (r.removeFromTop (20), "Master");
        r.removeFromTop (10);
        auto card = r.removeFromTop (kMasterCardH);
        Dine::fillRounded (g, card.toFloat(), Dine::card, Dine::Radius::card);
        auto inner = card.reduced (20, 14);
        inner.removeFromRight (raiseButton.isVisible() ? raiseButton.getWidth() + 20 : 0);
        // THREE CELLS, OR IT IS NOT THREE NUMBERS. They used to be given 110 pt each whether
        // the card had 330 pt or not, so a 1180 pt window pushed True peak off the card and
        // cut the loudness in half. They share what there is, and each number says itself
        // the short way when its own cell is too small for the long way.
        const int cellW = juce::jmax (72, inner.getWidth() / 3);
        auto cell = [&] (juce::Rectangle<int> area, const juce::String& label, const juce::String& value,
                         const juce::String& brief, juce::Colour ink)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 500));
            // "True peak" says "Peak" in a cell too narrow for both words.
            const bool roomy = Dine::textWidth (Dine::text (11.0f, 500), label) <= area.getWidth();
            Dine::drawText (g, roomy || label != "True peak" ? label : juce::String ("Peak"), area.removeFromTop (16),
                            juce::Justification::centredLeft, true);
            area.removeFromTop (2);
            g.setColour (ink);
            const auto valueFont = Dine::mono (12.0f, 500);
            g.setFont (valueFont);
            // A reading squeezes rather than loses a digit: "-10." is a different number.
            Dine::drawFittedText (g, brief.isNotEmpty() && Dine::textWidth (valueFont, value) > area.getWidth() ? brief : value,
                                  area, juce::Justification::centredLeft, 1, 0.75f);
        };
        cell (inner.removeFromLeft (juce::jmin (cellW, inner.getWidth())), "Level", masterLevelText, {}, Dine::ink);
        cell (inner.removeFromLeft (juce::jmin (cellW, inner.getWidth())), "Loudness", masterLoudText, masterLoudBrief,
              masterOnTarget ? Dine::ink : Dine::warn);
        cell (inner, "True peak", masterPeakText, masterPeakBrief, masterPeakOver ? Dine::crit : Dine::ink);
    }

    // ---- the input rail
    if (! railAvailable) return;
    g.setColour (Dine::rail);
    g.fillRect (l.rail.getUnion (l.railTab));
    g.setColour (Dine::hair);
    g.fillRect (l.rail.getUnion (l.railTab).removeFromRight (1));   // the seam against the middle
    if (! railOut()) return;
    // Anchored to the edge it slides from: the contents keep their width while the rail moves.
    const auto railFull = l.rail.withX (l.rail.getRight() - Dine::Metric::tuneRail).withWidth (Dine::Metric::tuneRail);
    auto head = railFull.withHeight (46).reduced (16, 0).withTrimmedTop (14);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (17.0f, 600));
    Dine::drawText (g, "Inputs", head.withTrimmedRight (20), juce::Justification::topLeft, true);

    if (inputRows.empty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, "Assign inputs to see them here.", railFull.reduced (14, 50).removeFromTop (40), juce::Justification::topLeft, 2);
    }

    int faintCount = 0;
    juce::String faintNames;
    for (const auto& r : inputRows)
        if (r->faint) { ++faintCount; faintNames += (faintNames.isEmpty() ? "" : ", ") + r->name; }
    if (faintCount > 0)
    {
        auto box = railFull.reduced (10, 0).removeFromBottom (92).withTrimmedBottom (10);
        Dine::fillRounded (g, box.toFloat(), Dine::mix (Dine::warn, 0.14f), Dine::Radius::control);
        auto r = box.reduced (12, 10);
        g.setColour (Dine::warn);
        g.setFont (Dine::text (12.0f, 600));
        Dine::drawText (g, faintCount == 1 ? "Check this input" : "Check these inputs", r.removeFromTop (16), juce::Justification::centredLeft);
        r.removeFromTop (4);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        Dine::drawFittedText (g, faintNames + " never rose above a whisper. Left where it is - a faint input is usually a mic that is off.",
                          r, juce::Justification::topLeft, 3);
    }
}

void MixPage::showEffects (bool open)
{
    if (open == effectsOpen) return;
    effectsOpen = open;
    groups[size_t (kGroupBuses)]->setOpen (open);
    resized();
    repaint();
}

void MixPage::resized()
{
    const auto l = layout();

    // ---- the right panel
    sideTab->setBounds (l.sideTab);
    sideView.setVisible (sideOut());
    sideView.setBounds (sideOut() ? l.side.withTrimmedTop (36).withWidth (kSideW) : juce::Rectangle<int>());
    layoutSide();

    // The groups are rows, one under the next, the way the design reads them.
    {
        // One column per group, left to right in console order. They share the width, down to
        // the width below which a fader stops being a fader - past that the row scrolls off
        // rather than squeezing, and the window is too narrow for TUNE anyway.
        auto groupRow = l.groups;
        const int gap = 8;
        // Which tiles are on the row: the groups and the FX tile, or - opened out - every effect
        // the session uses, then the FX tile again, which still rides them all and closes the row.
        std::vector<int> row;
        if (effectsOpen)
        {
            for (int t = kGroupTiles; t < kAllTiles; ++t)
                if (controller.isPrepared() && controller.getEngine().isFxUsed (returnSlot (t))) row.push_back (t);
            row.push_back (kGroupBuses);
        }
        else
            for (int t = 0; t < kGroupTiles; ++t) row.push_back (t);
        const int n = int (row.size());
        // The groups' own width, open or not: an effect is a fader the same size as a group.
        const int w = juce::jmax (GroupTile::minWidth,
                                  juce::jmin (GroupTile::width, (groupRow.getWidth() - gap * (kGroupTiles - 1)) / kGroupTiles));
        // A TILE IS WHOLE OR IT IS NOT THERE. `removeFromLeft` past the end hands back
        // whatever is left of the row, which is how the console used to end in a 30 pt
        // sliver of a group - its name an ellipsis, its level "+0...", its verb "T...".
        int fits = n;
        while (fits > 1 && fits * w + gap * (fits - 1) > groupRow.getWidth()) --fits;
        for (auto& tile : groups) tile->setVisible (false);
        for (int k = 0; k < fits; ++k)
        {
            auto& tile = groups[size_t (row[size_t (k)])];
            tile->setVisible (true);
            tile->setBounds (groupRow.removeFromLeft (w));
            groupRow.removeFromLeft (gap);
        }
        {
            const int bw = backToGroups.idealWidth() + 8;
            backToGroups.setBounds (l.groupsCaption.withTrimmedLeft (l.groupsCaption.getWidth() - bw)
                                        .withSizeKeepingCentre (bw, juce::jmin (l.groupsCaption.getHeight() + 6, Dine::Metric::button)));
            backToGroups.setVisible (effectsOpen);
        }
    }

    {
        // The card carries the three numbers; the target, the lift and the sound sit in its
        // right-hand end, where the design puts Raise.
        auto card = l.master.withTrimmedTop (20 + 10).reduced (20, 14);
        const int rw = juce::jmax (70, raiseButton.idealWidth());
        raiseButton.setBounds (card.removeFromRight (rw).withSizeKeepingCentre (rw, Dine::Metric::button));
        // The target and the voicing are pickers that belong with it but not on it: they sit
        // in the heading row, right-aligned, so the card stays three numbers and one verb.
        auto head = juce::Rectangle<int> (l.master).removeFromTop (20);
        const int vw = juce::jmin (head.getWidth() / 3, juce::jmax (140, voicingButton.idealWidth()));
        voicingButton.setBounds (head.removeFromRight (vw).withSizeKeepingCentre (vw, Dine::Metric::control));
        head.removeFromRight (8);
        const int tw = juce::jmin (head.getWidth() / 2, juce::jmax (150, loudnessTargetButton.idealWidth()));
        loudnessTargetButton.setBounds (head.removeFromRight (tw).withSizeKeepingCentre (tw, Dine::Metric::control));
    }

    {
        // "Centre both pads" sits at the right of the "Shape the mix" heading (v4).
        const int w = juce::jmax (90, resetMacrosButton.idealWidth());
        resetMacrosButton.setBounds (l.macrosCaption.withTrimmedLeft (l.macrosCaption.getWidth() - w).withSizeKeepingCentre (w, 22));
        for (size_t i = 0; i < pads.size(); ++i)
        {
            pads[i]->setCompact (l.compact);
            pads[i]->setPadSize (l.padSize);
            pads[i]->setBounds (l.pads[i]);
        }
        ribbon->setStacked (true);
        ribbon->setBounds (l.ribbon);
    }

    // ---- rail
    if (railOut()) railTab->setBounds (l.rail.withHeight (46).removeFromRight (30));
    else railTab->setBounds (l.railTab);
    int faintCount = 0;
    for (const auto& r : inputRows) if (r->faint) ++faintCount;
    auto rail = l.rail.withTrimmedTop (46);
    rail = rail.withX (rail.getRight() - Dine::Metric::tuneRail).withWidth (Dine::Metric::tuneRail);
    if (faintCount > 0) rail.removeFromBottom (92);
    railView.setVisible (railAvailable && railOut());
    railView.setBounds (rail);
    int total = 0;
    for (const auto& r : inputRows) total += r->wantedHeight();
    railHolder.setSize (rail.getWidth() - (total > rail.getHeight() ? 10 : 0), juce::jmax (total, rail.getHeight()));
    int y = 0;
    for (const int k : railOrder)
    {
        if (k < 0 || k >= int (inputRows.size())) continue;
        auto& r = inputRows[size_t (k)];
        const int h = r->wantedHeight();
        r->setBounds (0, y, railHolder.getWidth(), h);
        y += h;
    }

    scopeSheet->setBounds (getLocalBounds());
    listenSheet->setBounds (getLocalBounds());
    resultSheet->setBounds (getLocalBounds());
    referenceSheet->setBounds (getLocalBounds());
}

} // namespace livemix
