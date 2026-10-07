#include "LivePage.h"
#include "Profiles/MixProfileData.h"
#include "native/MixHistory.h"
#include "OutputsSheet.h"
#include "UI/Widgets.h"
#include <algorithm>
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kGroupBuses = int (MixBus::Master);
    constexpr int kFxTile = kGroupBuses;
    constexpr int kTiles = kGroupBuses + 1;
    // Behind the FX strip, one strip per effect return: the row opens out to them (Each effect)
    // and closes again (Back to groups). Never on the row with the groups, so nothing narrows.
    constexpr int kAllTiles = kTiles + int (FxSlot::Count);

    // The frame's measures (`06b - Live - decluttered`, 161:18761).
    constexpr int kPadX = 24, kPadTop = 20, kPadBottom = 24, kGap = 16;
    constexpr int kHealthH = 44;
    constexpr int kRailW = 352;
    constexpr int kCardPadX = 16, kCardPadY = 14, kCardGap = 10;
    constexpr int kHeadH = 18;            // a card's header line
    constexpr int kSegmentH = 24;         // a segment inside its track: 4 + 16 + 4
    constexpr int kLevelW = 72;           // the monitor level's line

    juce::String dbText (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::String span (double seconds)
    {
        const int total = int (seconds);
        if (total >= 24 * 3600) return "a day or more";
        if (total >= 3600)      return juce::String (total / 3600) + " h " + juce::String ((total / 60) % 60) + " m";
        return juce::String (juce::jmax (0, total / 60)) + " m";
    }

    juce::String clockTime (long long ms)
    {
        return juce::Time (ms).toString (false, true, false, true);
    }

    // How many lines a sentence wraps to at a width, word by word - how tall a card has to be
    // to hold it. Measured in resized(), never in paint.
    int wrapLines (const juce::Font& font, const juce::String& text, int width)
    {
        if (width <= 0 || text.isEmpty()) return 1;
        const int space = Dine::textWidth (font, " ");
        int lines = 1, run = 0;
        for (const auto& word : juce::StringArray::fromTokens (text, " ", ""))
        {
            const int w = Dine::textWidth (font, word);
            if (run > 0 && run + space + w > width) { ++lines; run = w; }
            else run += (run > 0 ? space : 0) + w;
        }
        return lines;
    }

    juce::Font calloutFont()  { return Dine::text (12.0f); }
    juce::Font noteFont()     { return Dine::text (11.0f, 500); }
}

// One group, standing: its colour across the top, its name and where its fader is, a tall
// fader with its meter beside it, and M and S at the foot. A muted strip says "Not heard" in
// words and its meter keeps moving in grey - "nothing is there" and "it is there but not
// heard" are different problems.
class LivePage::GroupTile : public juce::Component
{
public:
    // `input`: the tile is one input's strip (All and Alerts), `index` its console strip.
    GroupTile (MixController& c, int index, bool input = false) : controller (c), group (index), inputStrip (input)
    {
        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        Dine::dragOnly (fader);
        fader.setRange (-60.0, 12.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.getProperties().set ("dineFader", true);
        fader.getProperties().set ("dineFaderCap", 40);     // the design's 26 x 40 cap
        fader.setTooltip (isInput() ? "This input's fader. Double-click for 0.0 dB."
                          : isFx() ? "Level for every effect return together. Double-click for 0.0 dB, which is what TUNE MIX set."
                          : isReturn() ? "This effect's own level, on top of what TUNE MIX set for it. Double-click for 0.0 dB."
                                       : "Level for the whole group. Double-click for 0.0 dB.");
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (isInput())       controller.setStripFader (group, float (fader.getValue()));
            else if (isFx())     controller.setFxReturn (float (fader.getValue()));
            else if (isReturn()) controller.setFxSlotReturn (slot(), float (fader.getValue()));
            else                 controller.setBusFader (bus(), float (fader.getValue()));
            repaint();
        };

        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        mute.setTooltip (isInput() ? "Mute: this input is not heard"
                         : isFx() ? "Mute the effects: the reverbs and delays leave the mix, the sources stay."
                         : isReturn() ? "Mute this effect. The others stay as they are."
                                      : "Mute: the whole group is not heard");
        solo.setTooltip (isInput() ? "Solo this input. Only you hear it."
                         : isFx() ? "Solo just the reverbs and delays, so you hear what the sends are adding. Only you hear it."
                         : isReturn() ? "Solo this effect on its own. Only you hear it."
                                      : "Solo this group. Only you hear it.");
        mute.onClick = [this]
        {
            if (isInput())       controller.setStripMute (group, ! controller.getBase().strips[size_t (group)].mute);
            else if (isFx())     controller.setFxMute (! controller.getBase().fxMute);
            else if (isReturn()) controller.setFxSlotMute (slot(), ! controller.getBase().fx[size_t (slot())].mute);
            else                 controller.setGroupMuted (bus(), ! controller.isGroupMuted (bus()));
        };
        solo.onClick = [this]
        {
            if (isInput())       controller.setStripSolo (group, ! controller.getBase().strips[size_t (group)].solo);
            else if (isFx())     controller.setFxSoloAll (! controller.anyFxSolo());
            else if (isReturn()) controller.setFxSolo (slot(), ! controller.getBase().fx[size_t (slot())].solo);
            else                 controller.setBusSolo (bus(), ! controller.getBase().buses[size_t (bus())].solo);
        };
    }

    void refresh()
    {
        const auto& p = controller.getBase();
        float faderDb = 0.0f, peak = -120.0f;
        bool m = false, s = false, isUsed = true;
        if (isInput())
        {
            isUsed = controller.isPrepared() && group < controller.getEngine().getNumStrips() && group < p.numStrips;
            if (isUsed)
            {
                const auto& st = p.strips[size_t (group)];
                faderDb = juce::jmax (st.faderDb, -60.0f); m = st.mute; s = st.solo;
                peak = controller.stripPeakDb (group);
            }
        }
        else if (isFx())
        {
            faderDb = p.fxReturnDb;
            m = p.fxMute;
            s = controller.anyFxSolo();
            int returns = 0;
            if (controller.isPrepared())
            {
                const auto& engine = controller.getEngine();
                for (int f = 0; f < int (FxSlot::Count); ++f)
                    if (engine.isFxUsed (FxSlot (f))) { ++returns; peak = juce::jmax (peak, controller.fxPeakDb (FxSlot (f))); }
            }
            isUsed = returns > 0;
        }
        else if (isReturn())
        {
            const auto& fp = p.fx[size_t (slot())];
            faderDb = juce::jmax (fp.returnDb, -60.0f); m = fp.mute; s = fp.solo;
            isUsed = controller.isPrepared() && controller.getEngine().isFxUsed (slot());
            if (isUsed) peak = controller.fxPeakDb (slot());
        }
        else
        {
            const auto& b = p.buses[size_t (bus())];
            faderDb = b.faderDb; m = controller.isGroupMuted (bus()); s = b.solo;
            isUsed = controller.isPrepared() && controller.getEngine().isBusUsed (bus());
            if (isUsed) peak = controller.busPeakDb (bus());
        }
        updating = true;
        if (! fader.isMouseButtonDown() && std::fabs (faderDb - float (fader.getValue())) > 0.01f)
        {
            fader.setValue (faderDb, juce::dontSendNotification);
            repaint();
        }
        updating = false;
        meter.setLevels (peak, peak, peak > -0.2f);
        meter.setMuted (m || ! isUsed);
        if (m != muted || s != soloed || isUsed != used)
        {
            muted = m; soloed = s; used = isUsed;
            mute.setOn (muted);
            solo.setOn (soloed);
            fader.setEnabled (used);
            mute.setEnabled (used);
            solo.setEnabled (used);
            repaint();
        }
    }

    // The FX strip's name opens the row out to the effects, as the button by the heading does.
    std::function<void()> onOpen;
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (isFx() && used && onOpen && ! e.mouseWasDraggedSinceMouseDown() && e.y < 16 + 18 + 4 + 14) onOpen();
    }

    void lookAndFeelChanged() override
    {
        mute.setTint (Dine::keyMute);
        solo.setTint (Dine::keySolo);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        const auto ground = muted ? Dine::refuse : soloed ? Dine::soloGround : Dine::tile;
        Dine::fillRounded (g, r.toFloat(), ground, 8.0f);
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path round; round.addRoundedRectangle (r.toFloat(), 8.0f);
            g.reduceClipRegion (round);
            g.setColour (used ? tint() : Dine::ink4);
            g.fillRect (r.removeFromTop (2));
        }

        auto inner = getLocalBounds().reduced (kInsetX, 0).withTrimmedTop (16);
        g.setColour (! used ? Dine::ink4 : muted ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawFittedText (g, name(), inner.removeFromTop (18), juce::Justification::centred, 1, 0.7f);
        inner.removeFromTop (4);

        auto sub = inner.removeFromTop (14);
        const juce::String state = ! used ? "Off" : muted ? "Not heard" : soloed ? "Solo" : juce::String();
        if (state.isNotEmpty())
        {
            g.setColour (! used ? Dine::ink4 : muted ? Dine::keyMute : Dine::accent);
            g.setFont (Dine::text (11.0f, 600));
            Dine::drawText (g, state, sub, juce::Justification::centred, true);
        }
        else
        {
            g.setColour (Dine::ink2);
            g.setFont (Dine::mono (11.0f, 500));
            Dine::drawText (g, dbText (float (fader.getValue())) + " dB", sub, juce::Justification::centred, true);
        }

    }

    // Unity: the one mark the fader is read against, across the slot. Over the slot rather than
    // under it (the slider paints the slot), and left out while the cap is sitting on it.
    void paintOverChildren (juce::Graphics& g) override
    {
        if (fader.getHeight() <= 0) return;
        const float unity = float (fader.getPositionOfValue (0.0));
        if (std::fabs (unity - float (fader.getPositionOfValue (fader.getValue()))) < 21.0f) return;
        g.setColour (Dine::panMark);
        g.fillRect (juce::Rectangle<float> (float (fader.getX() + 7), float (fader.getY()) + unity, 12.0f, 1.0f));
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (kInsetX, 0);
        inner.removeFromTop (16 + 18 + 4 + 14 + 4 + 16);      // pad, name, gap, level, gap, the throw's own top
        inner.removeFromBottom (14);
        auto keys = inner.removeFromBottom (20);
        inner.removeFromBottom (4 + 12);

        const int faderW = 26, meterW = 6, between = juce::jmin (20, juce::jmax (8, inner.getWidth() - faderW - meterW - 8));
        auto pair = inner.withSizeKeepingCentre (faderW + between + meterW, inner.getHeight());
        fader.setBounds (pair.removeFromLeft (faderW));
        pair.removeFromLeft (between);
        meter.setBounds (pair.reduced (0, 20));               // the meter spans the travel, not the cap

        auto k = keys.withSizeKeepingCentre (28 + 4 + 28, 20);
        mute.setBounds (k.removeFromLeft (28));
        k.removeFromLeft (4);
        solo.setBounds (k);
    }

private:
    static constexpr int kInsetX = 8;
    bool isInput() const noexcept { return inputStrip; }
    bool isFx() const noexcept { return ! inputStrip && group == kFxTile; }
    bool isReturn() const noexcept { return ! inputStrip && group >= kTiles; }
    FxSlot slot() const noexcept { return FxSlot (group - kTiles); }
    // A strip's position is the console's order, not the enum's: LEAD sits with the voices.
    MixBus bus() const noexcept { return mixBusInDisplayOrder (group); }
    juce::Colour tint() const
    {
        if (isInput())
            return group < controller.getGraph().numStrips() ? Dine::busTint (controller.getGraph().strips[size_t (group)].bus) : Dine::ink4;
        return isFx() ? Dine::busAmbience : isReturn() ? Dine::keyFx : Dine::busTint (bus());
    }
    juce::String name() const
    {
        if (isInput())
            return group < controller.getGraph().numStrips() ? juce::String (controller.getGraph().strips[size_t (group)].name) : juce::String();
        if (isFx()) return "FX returns";
        if (isReturn()) return slot() == FxSlot::BgvHall ? juce::String ("BGV Hall") : juce::String (fxSlotName (slot()));
        const juce::String raw (mixBusName (bus()));
        return raw.length() <= 3 ? raw.toUpperCase() : raw.substring (0, 1).toUpperCase() + raw.substring (1).toLowerCase();
    }

    MixController& controller;
    int group;
    bool inputStrip = false;
    bool used = true, muted = false, soloed = false, updating = false;
    DineMeter meter { DineMeter::Style::Bar };
    juce::Slider fader;
    DineKey mute { "M", Dine::keyMute };
    DineKey solo { "S", Dine::keySolo };
};

// The small words at the right of a card's header - "What's locked ›" - that go somewhere.
class LivePage::Link : public juce::Button
{
public:
    Link() : juce::Button ("link") { setWantsKeyboardFocus (false); setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void set (const juce::String& t, juce::Colour c)
    {
        if (t == text && c == colour) return;
        text = t; colour = c;
        repaint();
    }
    int idealWidth() const { return Dine::textWidth (noteFont(), text) + 2; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        g.setColour (over || down ? colour.brighter (0.25f) : colour);
        g.setFont (noteFont());
        Dine::drawText (g, text, getLocalBounds(), juce::Justification::centredRight, false);
    }

private:
    juce::String text;
    juce::Colour colour { Dine::ink2 };
};

// The monitor level: a 3 pt line, the part that is on in ink. How loud the engineer's own
// headphones are and nothing to do with the mix anyone else hears.
class LivePage::LevelLine : public juce::Slider
{
public:
    LevelLine()
    {
        setSliderStyle (juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRange (-40.0, 12.0, 0.5);
        setSkewFactorFromMidPoint (-8.0);
        setDoubleClickReturnValue (true, 0.0);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto line = getLocalBounds().toFloat().withSizeKeepingCentre (float (getWidth()), 3.0f);
        Dine::fillRounded (g, line, Dine::control, 1.5f);
        const float on = float (valueToProportionOfLength (getValue()));
        Dine::fillRounded (g, line.withWidth (juce::jmax (3.0f, line.getWidth() * on)),
                           isMouseOverOrDragging() ? Dine::ink2 : Dine::ink3, 1.5f);
    }
};

// What LIVE SAFE locks, blocks and still allows, one press from the card that says it is on.
class LivePage::SafeDetail : public juce::Component
{
public:
    explicit SafeDetail (const LiveSafePolicy& policy)
    {
        rules = { { "Locked", "The audio device, the routing, the input patch and opening a session. Changing any of "
                              "them interrupts the audio mid-service.", Dine::warn },
                  { "Blocked", "TUNE MIX, TUNE LIVE MIX, TUNE CHANNEL and MATCH TO REFERENCE. They re-tune channels "
                               "that are on air.", Dine::warn },
                  { "Allowed", "Faders (" + juce::String (int (policy.maxFaderStepDb)) + " dB at a time, the master "
                                   + juce::String (int (policy.maxMasterStepDb)) + "), mutes, solos, the monitor, "
                                   "markers and recording.", Dine::ok } };
        int h = 16;
        for (const auto& r : rules) h += 16 + 4 + 16 * wrapLines (calloutFont(), r.why, kW - 32) + 12;
        setSize (kW, h + 4);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (16);
        for (const auto& rule : rules)
        {
            g.setColour (rule.tint);
            g.setFont (Dine::text (12.0f, 600));
            Dine::drawText (g, rule.what, r.removeFromTop (16), juce::Justification::centredLeft, true);
            r.removeFromTop (4);
            const int lines = wrapLines (calloutFont(), rule.why, r.getWidth());
            g.setColour (Dine::ink2);
            g.setFont (calloutFont());
            Dine::drawFittedText (g, rule.why, r.removeFromTop (16 * lines), juce::Justification::topLeft, lines, 1.0f);
            r.removeFromTop (12);
        }
    }

private:
    static constexpr int kW = 340;
    struct Rule { juce::String what, why; juce::Colour tint; };
    std::vector<Rule> rules;
};

// THE SETLIST on the rail: a row per cue - its number, its name, Now or Next - the one on now
// on a lit plane. A long setlist shows the rows around Now. A click goes to that cue.
class LivePage::CueList : public juce::Component
{
public:
    static constexpr int kRowH = 28;
    std::function<void (int)> onPick;

    void set (const Setlist& s)
    {
        if (s == list) return;
        list = s;
        repaint();
    }
    int rowsWanted() const noexcept { return int (list.cues.size()); }
    int firstShown() const noexcept
    {
        // Now and Next always; the cue before Now as well once there are three rows.
        const int rows = juce::jmax (1, getHeight() / kRowH), n = int (list.cues.size());
        return juce::jlimit (0, juce::jmax (0, n - rows), rows >= 3 ? list.current - 1 : list.current);
    }

    void paint (juce::Graphics& g) override
    {
        const int rows = getHeight() / kRowH, n = int (list.cues.size()), first = firstShown();
        const int next = list.next();
        const auto numFont = Dine::mono (11.0f, 500), nameFont = Dine::text (13.0f, 500), tagFont = Dine::text (11.0f, 600);
        const int numW = Dine::textWidth (numFont, "00") + 4;
        for (int k = 0; k < rows && first + k < n; ++k)
        {
            const int i = first + k;
            auto row = juce::Rectangle<int> (0, k * kRowH, getWidth(), kRowH);
            if (i == list.current) Dine::fillRounded (g, row.toFloat(), Dine::controlOn, 6.0f);
            else if (i == hover) Dine::fillRounded (g, row.toFloat(), Dine::control, 6.0f);
            auto inner = row.reduced (8, 0);
            g.setColour (Dine::ink3);
            g.setFont (numFont);
            Dine::drawText (g, juce::String (i + 1), inner.removeFromLeft (numW), juce::Justification::centredLeft, false);
            const juce::String tag = i == list.current ? "Now" : i == next ? "Next" : juce::String();
            if (tag.isNotEmpty())
            {
                g.setColour (i == list.current ? Dine::accent : Dine::ink3);
                g.setFont (tagFont);
                Dine::drawText (g, tag, inner.removeFromRight (Dine::textWidth (tagFont, tag) + 2), juce::Justification::centredRight, false);
                inner.removeFromRight (8);
            }
            g.setColour (i == list.current ? Dine::ink : Dine::ink2);
            g.setFont (nameFont);
            Dine::drawFittedText (g, juce::String (list.cues[size_t (i)].name), inner, juce::Justification::centredLeft, 1, 0.85f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (rowAt (e.y)); }
    void mouseExit (const juce::MouseEvent&) override  { setHover (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.y);
        if (i >= 0 && ! e.mouseWasDraggedSinceMouseDown() && onPick) onPick (i);
    }

private:
    int rowAt (int y) const noexcept
    {
        const int i = firstShown() + y / kRowH;
        return y >= 0 && i < int (list.cues.size()) && y / kRowH < getHeight() / kRowH ? i : -1;
    }
    void setHover (int i) { if (i != hover) { hover = i; repaint(); } }
    Setlist list;
    int hover = -1;
};

LivePage::LivePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    for (int i = 0; i < kAllTiles; ++i)
    {
        tiles[size_t (i)] = std::make_unique<GroupTile> (controller, i);
        addChildComponent (*tiles[size_t (i)]);
    }
    tiles[size_t (kFxTile)]->onOpen = [this] { showEffects (true); };

    // ---- what the strips show: This cue / Groups / All / Alerts, with their counts
    const char* viewNames[4] = { "This cue", "Groups", "All", "Alerts" };
    const char* viewTips[4] = {
        "The groups that are on right now, in this cue.",
        "Every group, and the effects together on one strip.",
        "Every input on its own strip, then each effect on its own fader.",
        "Only the inputs whose level the desk should still move." };
    for (int i = 0; i < 4; ++i)
    {
        auto tab = std::make_unique<DineButton> (viewNames[i], DineButton::Style::Segment);
        tab->setFontPx (12.0f);
        tab->setPadX (10);
        tab->setClickingTogglesState (false);
        tab->setTooltip (viewTips[i]);
        tab->onClick = [this, i] { setView (View (i)); };
        addAndMakeVisible (*tab);
        viewTabs[size_t (i)] = std::move (tab);
    }
    effectsOff.setFontPx (12.0f);
    effectsOff.setPadX (10);
    effectsOff.setClickingTogglesState (false);
    effectsOff.setTooltip ("Take every reverb and delay out of the mix at once, without touching their levels. Press again to bring them back.");
    effectsOff.onClick = [this] { controller.setFxMute (! controller.getBase().fxMute); refresh(); };
    addAndMakeVisible (effectsOff);

    scroller.setViewedComponent (&scrollHolder, false);
    scroller.setScrollBarsShown (false, true);
    scroller.setScrollBarThickness (8);
    addChildComponent (scroller);

    // ---- the setlist: Up next and its GO, the list, and Edit
    cueList = std::make_unique<CueList>();
    cueList->onPick = [this] (int i) { goToCue (i); };
    addAndMakeVisible (*cueList);
    goButton.setFontPx (13.0f);
    goButton.setTooltip ("Go to the next cue: its scene comes back, as a scene does. Space does the same on this page.");
    goButton.onClick = [this] { goToNextCue(); };
    addChildComponent (goButton);
    editSetlist.setFontPx (11.0f);
    editSetlist.setPadX (8);
    editSetlist.setTooltip ("Add, rename, reorder and delete cues, and say who is on in each.");
    editSetlist.onClick = [this] { if (onEditSetlist) onEditSetlist (look.setlist.current); };
    addAndMakeVisible (editSetlist);
    clearCueButton.setFontPx (11.5f);
    clearCueButton.setPadX (8);
    clearCueButton.setTooltip ("Clear the cue: whoever it muted comes back, its Softer / Up front are taken back, and no cue is on.");
    clearCueButton.onClick = [this] { controller.clearCue(); refresh(); };
    addChildComponent (clearCueButton);

    // ---- scenes: pick one; a kept one comes straight back, KEEP writes the mix into the one picked
    for (int i = 0; i < 4; ++i)
    {
        auto seg = std::make_unique<DineButton> (juce::String (defaultSceneName (i)).toUpperCase(), DineButton::Style::Toggle);
        seg->setFontPx (12.0f);
        seg->setPadX (8);
        seg->setClickingTogglesState (false);
        seg->onClick = [this, i]
        {
            sceneSlot = i;
            const auto& scene = controller.getScene (i);
            if (scene.kept) controller.recallScene (i);
            else if (onToast) onToast ("Nothing is kept under " + juce::String (scene.name.empty() ? defaultSceneName (i) : scene.name.c_str())
                                       + " yet. Set the mix, then press Keep.");
            refreshScenes();
        };
        addAndMakeVisible (*seg);
        sceneSegments[size_t (i)] = std::move (seg);
    }
    addAndMakeVisible (keepButton);
    keepButton.setFontPx (11.0f);
    keepButton.onClick = [this]
    {
        if (sceneSlot < 0) return;
        controller.keepScene (sceneSlot);
        refreshScenes();
        if (onToast) onToast ("Kept. " + juce::String (controller.getScene (sceneSlot).name) + " brings this mix back in one press.");
    };
    renameScene.setFontPx (11.0f);
    renameScene.setTooltip ("Give a scene a name of its own: \"Choir\" instead of Custom. A cue that starts from it follows.");
    renameScene.onClick = [this]
    {
        juce::PopupMenu m;
        for (int i = 0; i < 4; ++i) m.addItem (1 + i, "Rename " + juce::String (controller.getScene (i).name) + juce::String (Glyph::ellip()));
        juce::Component::SafePointer<LivePage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&renameScene), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            const int slot = r - 1;
            const juce::String was (safe->controller.getScene (slot).name);
            Dine::askForName ("Rename the " + was + " scene", "What should it be called? Cues that bring it back follow the new name.",
                              was, "Rename", [safe, slot] (const juce::String& name)
            {
                if (safe == nullptr) return;
                safe->controller.renameScene (slot, name.toStdString());
                safe->services.touchSession();
                safe->refreshScenes();
                safe->refresh();
            });
        });
    };
    addAndMakeVisible (renameScene);
    refreshScenes();

    checkLink = std::make_unique<Link>();
    checkLink->set ("Check inputs " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")), Dine::ink2);
    checkLink->setTooltip ("Every input, its level and one word about it.");
    checkLink->onClick = [this] { if (onOpenCheck) onOpenCheck(); };
    addChildComponent (*checkLink);

    safeLink = std::make_unique<Link>();
    addAndMakeVisible (*safeLink);
    safeLink->onClick = [this]
    {
        auto& daw = services.daw();
        if (! daw.isLiveSafe())
        {
            daw.setLiveSafe (true);
            if (onLiveSafeChanged) onLiveSafeChanged();
            if (onToast) onToast ("LIVE SAFE on. The sound is locked: re-routes and re-tunes are blocked.");
            refresh();
            return;
        }
        juce::CallOutBox::launchAsynchronously (std::make_unique<SafeDetail> (controller.getLiveSafePolicy()),
                                                safeLink->getScreenBounds(), nullptr);
    };

    autopilotLink = std::make_unique<Link>();
    addAndMakeVisible (*autopilotLink);
    autopilotLink->setTooltip ("Hold the mix you set. Autopilot moves group faders only, slowly, inside a few dB of the mix "
                               "it was engaged on, and says why every time. Touch a fader and that group is yours again.");
    autopilotLink->onClick = [this]
    {
        if (controller.isAutopilotOn()) return;             // the toolbar's AUTOPILOT is the one press that stops it
        if (! controller.setAutopilot (true)) return;       // the controller has already said why
        refresh();
    };

    // ---- the speaking mics
    priorityLink = std::make_unique<Link>();
    addAndMakeVisible (*priorityLink);
    priorityLink->setTooltip ("While somebody is speaking, the band (drums, bass, music) steps back a few dB and comes back when they stop. "
                              "The voices, the room and your own listen never move.");
    priorityLink->onClick = [this] { controller.setSpeechPriority (! controller.getSpeechPriority()); refresh(); };
    shareLink = std::make_unique<Link>();
    addAndMakeVisible (*shareLink);
    shareLink->setTooltip ("For a podcast table, a panel or an interview: the speaking mic in use is open and the others step back, "
                           "so the stream hears one mic's worth of room noise and bleed. Your pre-fade listen is never changed.");
    shareLink->onClick = [this] { controller.setAutoMix (! controller.getAutoMix()); refresh(); };

    // ---- the engineer's own listen
    const char* labels[4] = { "MONITOR SOLO", "SOLO IN PLACE", "AFL", "PFL" };
    const char* tips[4] = {
        "Solo goes to your headphones only: the room and the stream never hear it. This is the normal setting.",
        "Solo mutes everything else for everybody. Right for mixing a recording, never for a service.",
        "After-fade listen: you hear the channel where it sits in the mix - panned, and silent if it is muted.",
        "Pre-fade listen: you hear the channel as it arrives, whatever its fader and mute are doing." };
    for (int i = 0; i < 4; ++i)
    {
        modes[size_t (i)] = std::make_unique<DineButton> (labels[i], DineButton::Style::Toggle);
        modes[size_t (i)]->setFontPx (12.0f);
        modes[size_t (i)]->setPadX (8);
        modes[size_t (i)]->setClickingTogglesState (false);
        modes[size_t (i)]->setTooltip (tips[i]);
        addAndMakeVisible (*modes[size_t (i)]);
    }
    modes[0]->onClick = [this] { controller.setSoloMode (SoloMode::Monitor); refreshMonitor(); };
    // The controller says what happened - that solo is now heard by everyone, or that LIVE SAFE
    // refused it - so the page adds nothing that could contradict it.
    modes[1]->onClick = [this] { controller.setSoloMode (SoloMode::InPlace); refreshMonitor(); };
    modes[2]->onClick = [this] { controller.setSoloPoint (SoloPoint::AFL); refreshMonitor(); };
    modes[3]->onClick = [this] { controller.setSoloPoint (SoloPoint::PFL); refreshMonitor(); };

    addAndMakeVisible (clearSolo);
    clearSolo.setCaps (true);
    clearSolo.setFontPx (11.0f);
    clearSolo.setTooltip ("Stop listening to everything you have soloed, all at once.");
    clearSolo.onClick = [this] { controller.clearSolos(); refreshMonitor(); if (onToast) onToast ("Solo cleared."); };

    addAndMakeVisible (monitorDim);
    monitorDim.setCaps (true);
    monitorDim.setFontPx (10.0f);
    monitorDim.setPadX (6);
    monitorDim.setClickingTogglesState (false);
    monitorDim.setTooltip ("Drop your headphones to talk to someone, without losing the level you had set.");
    monitorDim.onClick = [this] { controller.setMonitorDim (! controller.getMonitor().dim); refreshMonitor(); };

    // Where solo goes. The same choice as Outputs > Solo, here because this card is where
    // "solo has nowhere to go yet" is read, and the fix belongs one click away from it.
    addAndMakeVisible (soloDevice);
    soloDevice.setFlat (true);
    soloDevice.setTooltip ("The device only you listen on - headphones, a second interface, or outputs 3-4 of the "
                           "broadcast device. Solo a channel and it comes out here; the room and the stream never hear it.");
    soloDevice.onClick = [this]
    {
        OutputsSheet::showSoloDeviceMenu (controller, services, soloDevice, [this] (const juce::String& message)
        {
            if (onToast && message.isNotEmpty()) onToast (message);
            refreshMonitor();
        });
    };

    monitorLevel = std::make_unique<LevelLine>();
    addAndMakeVisible (*monitorLevel);
    monitorLevel->onValueChange = [this]
    {
        controller.setMonitorGain (float (monitorLevel->getValue()));
        monitorLevel->setTooltip ("Your headphones: " + dbText (float (monitorLevel->getValue())) + " dB. Nothing to do with the mix "
                                  "anyone else hears. Double-click for 0.0 dB.");
    };

    setOpaque (true);
    refreshMonitor();
}

LivePage::~LivePage() = default;

void LivePage::refreshMonitor()
{
    const auto& m = controller.getMonitor();
    const bool inPlace = m.mode == SoloMode::InPlace;
    modes[0]->setToggleState (! inPlace, juce::dontSendNotification);
    modes[1]->setToggleState (inPlace, juce::dontSendNotification);
    modes[2]->setToggleState (m.point == SoloPoint::AFL, juce::dontSendNotification);
    modes[3]->setToggleState (m.point == SoloPoint::PFL, juce::dontSendNotification);
    monitorDim.setToggleState (m.dim, juce::dontSendNotification);
    clearSolo.setEnabled (controller.numSoloed() > 0);
    if (std::fabs (monitorLevel->getValue() - double (m.gainDb)) > 0.01)
        monitorLevel->setValue (m.gainDb, juce::dontSendNotification);
    monitorLevel->setTooltip ("Your headphones: " + dbText (m.gainDb) + " dB. Nothing to do with the mix anyone else hears. "
                              "Double-click for 0.0 dB.");
    const auto device = services.soloOutputDevice();
    const auto choice = OutputsSheet::soloChoiceLabel (controller, services, "Choose headphones");
    soloDevice.setValue (choice);
    soloDevice.setBriefValue (device.isEmpty() ? (choice == "Choose headphones" ? juce::String ("Pick") : juce::String ("Here")) : device);
    soloDevice.setEnabled (services.isAudioRunning());
}

void LivePage::rebuild()
{
    refreshScenes();
    rebuildInputTiles();
    for (auto& t : tiles) if (t != nullptr) t->refresh();
    for (auto& t : inputTiles) t->refresh();
    refreshMonitor();
    resized();
    repaint();
}

// One strip per input on the console, made again only when the number of strips changes.
void LivePage::rebuildInputTiles()
{
    const int n = controller.isBuilt() ? controller.getGraph().numStrips() : 0;
    if (int (inputTiles.size()) == n) return;
    inputTiles.clear();
    for (int i = 0; i < n; ++i)
    {
        inputTiles.push_back (std::make_unique<GroupTile> (controller, i, true));
        scrollHolder.addChildComponent (*inputTiles.back());
    }
}

// What each view puts on the row: group tiles by their index, inputs as 1000 + their strip.
std::vector<int> LivePage::tilesFor (View v) const
{
    std::vector<int> out;
    const auto& p = controller.getBase();
    const bool prepared = controller.isPrepared();
    switch (v)
    {
        case View::ThisCue:
            // What is on right now: the groups in use and not muted, and the effects if they are heard.
            for (int t = 0; t < kGroupBuses; ++t)
            {
                const auto b = mixBusInDisplayOrder (t);
                if (prepared && controller.getEngine().isBusUsed (b) && ! controller.isGroupMuted (b)) out.push_back (t);
            }
            if (anyEffects() && ! p.fxMute) out.push_back (kFxTile);
            break;
        case View::Groups:
            for (int t = 0; t < kTiles; ++t) out.push_back (t);
            break;
        case View::All:
            for (int i = 0; i < int (inputTiles.size()); ++i) out.push_back (1000 + i);
            for (int t = kTiles; t < kAllTiles; ++t)
                if (prepared && controller.getEngine().isFxUsed (FxSlot (t - kTiles))) out.push_back (t);
            if (anyEffects()) out.push_back (kFxTile);
            break;
        case View::Alerts:
            for (const auto& s : look.attentionStrips)
                if (s.getIntValue() < int (inputTiles.size())) out.push_back (1000 + s.getIntValue());
            break;
    }
    return out;
}

void LivePage::setView (View v)
{
    if (v == view) return;
    view = v;
    refreshViewTabs();
    resized();
    repaint();
}

void LivePage::refreshViewTabs()
{
    const char* names[4] = { "This cue", "Groups", "All", "Alerts" };
    for (int i = 0; i < 4; ++i)
    {
        auto& tab = *viewTabs[size_t (i)];
        const int n = look.counts[size_t (i)];
        const juce::String text = juce::String (names[i]) + (n > 0 ? "  " + juce::String (n) : juce::String());
        if (tab.getButtonText() != text) tab.setButtonText (text);
        tab.setToggleState (int (view) == i, juce::dontSendNotification);
    }
}

void LivePage::goToNextCue()
{
    controller.goToNextCue();
    refresh();
}

void LivePage::goToCue (int index)
{
    controller.goToCue (index);
    refresh();
}

void LivePage::focusSetlist()
{
    setlistFlash = 45;
    repaint();
}

void LivePage::updateDiskNote()
{
    if (--diskTicks <= 0)
    {
        diskTicks = 60;
        secondsFree = services.daw().getRecordingSecondsFree();
    }
}

// The sidebar's SCENES row: come to LIVE and say where they are, for a moment.
void LivePage::focusScenes()
{
    sceneFlash = 45;
    repaint();
}

void LivePage::refresh()
{
    refreshScenes();                 // the picker follows the controller: a scene kept from anywhere shows here

    rebuildInputTiles();
    for (auto& t : tiles) if (t != nullptr && t->isVisible()) t->refresh();
    for (auto& t : inputTiles) if (t->isVisible()) t->refresh();
    updateDiskNote();

    auto& daw = services.daw();
    const bool recording = daw.isRecording();
    const bool running = services.isAudioRunning();

    Look next;
    next.isRecording = recording;
    next.running = running;
    next.safe = daw.getProject().liveSafe;
    const int armed = daw.getProject().numArmed();
    if (recording)
    {
        next.recording = juce::String (armed) + (armed == 1 ? " track " : " tracks ") + Glyph::dot() + " "
                       + Transport::formatTime (daw.getRecordingSeconds()).dropLastCharacters (4);
        next.recordingNote = secondsFree > 0.0 ? span (secondsFree) + " left" : juce::String();
    }
    else
    {
        next.recording = "Stopped";
        next.recordingNote = armed == 0 ? juce::String ("none set to record")
                                        : juce::String (armed) + " set to record";
    }
    next.output = ! running ? juce::String ("Off")
                : services.outputDisplayName().isEmpty() ? juce::String ("No output") : services.outputDisplayName();

    // Clipping: the inputs the last listen found clipping at the preamp, named. The advice
    // builds sentences, so it is re-read twice a second rather than every frame.
    if ((++adviceTicks % 15) == 1 || look.clipping.isEmpty())
    {
        int clipping = 0;
        juce::String names;
        if (controller.isPrepared())
            for (int i = 0; i < controller.getEngine().getNumStrips(); ++i)
            {
                const auto a = controller.getInputAdvice (i);
                if (a.level != MixController::InputAdvice::Level::Clipping) continue;
                if (clipping < 2) names += (clipping == 0 ? "" : ", ") + juce::String (controller.getGraph().strips[size_t (i)].name);
                ++clipping;
            }
        // v4's "Needs attention": every input the desk should still move, said as a sentence.
        attentionNow.clear();
        attentionStripsNow.clear();
        if (controller.isPrepared())
            for (int i = 0; i < controller.getEngine().getNumStrips() && i < controller.getGraph().numStrips(); ++i)
            {
                const auto a = controller.getInputAdvice (i);
                if (! a.needsAttention()) continue;
                using Level = MixController::InputAdvice::Level;
                const juce::String what = a.level == Level::Clipping ? "is clipping" : a.level == Level::Hot ? "is hot"
                                        : a.level == Level::Digital ? "is only loud because DINE raised it"
                                        : a.level == Level::NotHeard ? "was not heard" : "is too quiet";
                const bool crit = a.level == Level::Clipping || a.level == Level::NotHeard;
                attentionNow.add (juce::String (controller.getGraph().strips[size_t (i)].name) + " " + what + "\t"
                                  + juce::String (a.detail) + "\t" + (crit ? "c" : "w"));
                attentionStripsNow.add (juce::String (i));
            }
        clipText = clipping == 0 ? "None" : juce::String (clipping) + (clipping == 1 ? " input" : " inputs");
        clipNote = clipping == 0 ? juce::String() : names + " " + Glyph::dash() + " fix it at the console";
        anyClipping = clipping > 0;
    }
    next.anyClip = anyClipping;
    next.attention = attentionNow;
    next.attentionStrips = attentionStripsNow;

    // THE SETLIST, and what each view would hold.
    next.setlist = controller.getSetlist();
    for (const auto& cue : next.setlist.cues)
    {
        next.cueScenes.add (juce::String (cueKindName (cue.kind)));
        next.cueLouder.add (juce::String (controller.cueNamesAt (cue, CueLevel::UpFront)));
        next.cueSofter.add (juce::String (controller.cueNamesAt (cue, CueLevel::Softer)));
    }
    {
        int on = 0, used = 0;
        for (int t = 0; t < kGroupBuses; ++t)
        {
            const auto b = mixBusInDisplayOrder (t);
            if (! controller.isPrepared() || ! controller.getEngine().isBusUsed (b)) continue;
            ++used;
            if (! controller.isGroupMuted (b)) ++on;
        }
        next.counts = { on, used, int (inputTiles.size()), attentionNow.size() };
    }
    cueList->set (next.setlist);
    effectsOff.setVisible (anyEffects());
    effectsOff.setToggleState (controller.getBase().fxMute, juce::dontSendNotification);
    next.clipping = clipText;
    next.clippingNote = clipNote;

    const auto loud = controller.getMasterLoudness();
    next.outputNote = loud.known && loud.integratedLufs > -100.0f ? juce::String (loud.integratedLufs, 1).replace ("-", Glyph::minus()) + " LUFS"
                                                                  : juce::String();
    if (controller.isPrepared()) next.headroomDb = -controller.getEngine().getBus (MixBus::Master).getOutputMeter().getMaxPeakDb();
    next.headroom = controller.isPrepared() ? juce::String (juce::jlimit (0.0f, 60.0f, next.headroomDb), 1) + " dB" : Glyph::dash();
    next.headroomNote = loud.known && loud.truePeakDb > -100.0f ? "TP " + juce::String (loud.truePeakDb, 1).replace ("-", Glyph::minus()) + " dBTP"
                                                                : juce::String();

    next.soloCount = controller.numSoloed();
    next.inPlace = controller.getMonitor().mode == SoloMode::InPlace;
    next.routed = controller.hasMonitorOutput();
    next.monitorNote = next.inPlace ? "Careful: pressing S is heard by the room and the stream too."
                     : ! next.routed ? "Solo has nowhere to go yet. Pick where you listen, below."
                     : next.soloCount > 0 ? juce::String (next.soloCount) + (next.soloCount == 1 ? " soloed. Only you hear it." : " soloed. Only you hear it.")
                                          : "Press S on a group. Only you hear it.";
    if (next.soloCount != look.soloCount || next.inPlace != look.inPlace || next.routed != look.routed) refreshMonitor();

    {
        const auto& ap = controller.getAutopilot();
        next.autopilotOn = ap.on;
        next.priorityOn = controller.getSpeechPriority();
        next.shareOn = controller.getAutoMix();
        int speaking = 0;
        for (const auto& s : controller.getGraph().strips) if (s.bus == MixBus::Speech) ++speaking;
        next.speakingMics = speaking;
        next.autopilotMoved = ap.groupsCorrected > 0;
        if (ap.on && autopilotSince.isEmpty()) autopilotSince = clockTime (juce::Time::currentTimeMillis());
        if (! ap.on) autopilotSince = {};
        // What it has done since it was engaged, newest first, in the words the Mix history has:
        // its own entries, back as far as the "Before Autopilot" mark it left when it came on.
        const auto& history = controller.getCheckpoints();
        if (history.size() != checkpointsSeen || ap.on != look.autopilotOn)
        {
            checkpointsSeen = history.size();
            autopilotLog.clear();
            if (ap.on)
                for (auto it = history.rbegin(); it != history.rend() && autopilotLog.size() < 2; ++it)
                {
                    const juce::String what (it->what);
                    if (what == "Before Autopilot") break;
                    if (! what.startsWith ("Autopilot: ")) continue;
                    auto line = what.fromFirstOccurrenceOf ("Autopilot: ", false, false).trimCharactersAtEnd (".");
                    line = line.replaceFirstOccurrenceOf (". ", " " + Glyph::dot() + " ").replace ("-", Glyph::minus());
                    // The group as the strips name it: "Lead", not "LEAD".
                    const auto group = line.upToFirstOccurrenceOf (" ", false, false);
                    if (group.length() > 3)
                        line = group.substring (0, 1) + group.substring (1).toLowerCase() + line.fromFirstOccurrenceOf (" ", true, false);
                    autopilotLog.add (clockTime (it->whenMs) + "\t" + line);
                }
        }
        next.autopilotLog = autopilotLog;
        next.autopilotSince = autopilotSince;
    }

    safeLink->set (next.safe ? juce::String (juce::CharPointer_UTF8 ("What\xe2\x80\x99s locked \xe2\x80\xba"))
                             : juce::String (juce::CharPointer_UTF8 ("Turn on \xe2\x80\xba")),
                   next.safe ? Dine::warn : Dine::ink2);
    safeLink->setTooltip (next.safe ? "What LIVE SAFE locks, what it blocks and what still works."
                                    : juce::String ("Lock the sound for the service. ") + liveSafe::lockedSummary() + " " + liveSafe::allowedSummary());
    autopilotLink->set (next.autopilotOn ? (next.autopilotMoved ? "Holding since " : "Healthy since ") + autopilotSince
                                         : juce::String (juce::CharPointer_UTF8 ("Turn on \xe2\x80\xba")),
                        next.autopilotOn ? Dine::ink3 : Dine::ink2);
    autopilotLink->setMouseCursor (next.autopilotOn ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    {
        const juce::String on (juce::CharPointer_UTF8 ("Turn on \xe2\x80\xba"));
        priorityLink->set (next.priorityOn ? juce::String ("Turn off") : on, next.priorityOn ? Dine::monitor : Dine::ink2);
        shareLink->set (next.shareOn ? juce::String ("Turn off") : on, next.shareOn ? Dine::monitor : Dine::ink2);
    }

    if (next != look)
    {
        const bool reflow = next.safe != look.safe || next.autopilotOn != look.autopilotOn
                         || next.setlist != look.setlist || next.cueScenes != look.cueScenes || next.counts != look.counts
                         || next.cueLouder != look.cueLouder || next.cueSofter != look.cueSofter
                         || next.attentionStrips != look.attentionStrips
                         || next.priorityOn != look.priorityOn || next.shareOn != look.shareOn || next.speakingMics != look.speakingMics
                         || next.autopilotLog != look.autopilotLog || next.monitorNote != look.monitorNote
                         || next.attention != look.attention;
        look = next;
        refreshViewTabs();
        if (reflow) resized();
        repaint();
    }
    // The marks the sidebar's rows leave fade over a second and a half, painting only themselves.
    if (sceneFlash > 0) repaint (lay.sceneTrack.expanded (6));
    if (setlistFlash > 0) repaint (lay.setlist.expanded (2));
}

juce::String LivePage::safeText() const
{
    return look.safe ? "Routing, device and re-tuning are locked. Faders, mutes, solos and recording still work."
                     : "Nothing is locked. Turn it on before the doors open: routing, device and re-tuning lock, "
                       "and faders, mutes, solos and recording keep working.";
}

juce::String LivePage::priorityText() const
{
    const auto depth = juce::String (MixProfile::speechPriority (controller.getSession().profile).depthDb, 0);
    return look.priorityOn ? "On: the band steps back " + depth + " dB while somebody speaks."
                           : "The band steps back " + depth + " dB while somebody speaks.";
}

juce::String LivePage::shareText() const
{
    const auto depth = juce::String (MixProfile::autoMix (controller.getSession().profile).depthDb, 0);
    if (look.shareOn && look.speakingMics < 2) return "On, waiting for a second speaking mic.";
    return look.shareOn ? "On: the mic in use is open, the others " + depth + " dB back."
                        : "The mic in use opens, the others step back " + depth + " dB.";
}

juce::String LivePage::autopilotText() const
{
    const auto limit = juce::String (controller.getAutopilotLimits().maxTotalDb, 0);
    return look.autopilotOn ? juce::String (juce::CharPointer_UTF8 ("Group faders only, \xc2\xb1")) + limit + " dB max. Touch a fader to take over."
                            : juce::String (juce::CharPointer_UTF8 ("Off. Engage it and DINE holds the mix you set: group faders only, "
                                                                     "inside \xc2\xb1")) + limit + " dB of where you engaged it, and every move says why.";
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto& l = lay;

    // ---- the health strip: four readings, one line
    {
        Dine::fillRounded (g, l.health.toFloat(), Dine::card, 8.0f);
        auto row = l.health.reduced (16, 0);
        struct Stat { juce::String caption, value, note; juce::Colour dot; bool monoNote; };
        const Stat stats[4] = {
            { "Recording", look.recording, look.recordingNote, look.isRecording ? Dine::keyRec : Dine::ink4, false },
            { "On air", look.output, look.outputNote, look.running ? Dine::ok : Dine::ink4, true },
            { "Clipping", look.clipping, look.clippingNote, look.anyClip ? Dine::crit : Dine::ok, false },
            { "Headroom", look.headroom, look.headroomNote,
              look.headroomDb < 0.5f ? Dine::crit : look.headroomDb < 3.0f ? Dine::warn : Dine::ok, true } };
        const auto capFont = Dine::text (11.0f, 500), valueFont = Dine::text (13.0f, 600);
        for (int i = 0; i < 4 && row.getWidth() > 0; ++i)
        {
            if (i > 0)
            {
                g.setColour (Dine::control);
                g.fillRect (juce::Rectangle<int> (row.getX() + 10, row.getCentreY() - 10, 1, 20));
                row.removeFromLeft (21);
            }
            const auto& s = stats[i];
            g.setColour (s.dot);
            g.fillEllipse (row.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
            row.removeFromLeft (8);
            g.setColour (Dine::ink3);
            g.setFont (capFont);
            const int capW = Dine::textWidth (capFont, s.caption);
            Dine::drawText (g, s.caption, row.removeFromLeft (capW), juce::Justification::centredLeft, false);
            row.removeFromLeft (8);
            g.setColour (Dine::ink);
            g.setFont (valueFont);
            const int valueW = juce::jmin (row.getWidth(), Dine::textWidth (valueFont, s.value));
            Dine::drawText (g, s.value, row.removeFromLeft (valueW), juce::Justification::centredLeft, true);
            // The note is a second reading, not a name: on a narrow window it is left out
            // whole rather than cut to "TP -1..." - the value beside it still says the thing.
            const auto noteF = s.monoNote ? Dine::mono (11.0f, 500) : capFont;
            const int noteW = s.note.isEmpty() ? 0 : Dine::textWidth (noteF, s.note);
            if (noteW > 0 && noteW + 8 <= row.getWidth())
            {
                row.removeFromLeft (8);
                g.setColour (s.monoNote ? Dine::ink2 : Dine::ink3);
                g.setFont (noteF);
                Dine::drawText (g, s.note, row.removeFromLeft (noteW), juce::Justification::centredLeft, false);
            }
        }
    }

    // ---- the cue that is on: a lamp, its name, "Cue 3 of 10 - Band"; the views; the scenes
    {
        const auto& sl = look.setlist;
        const bool onCue = sl.current >= 0 && sl.current < int (sl.cues.size());
        auto head = l.groupsHeader;
        g.setColour (onCue ? Dine::accent : Dine::ink4);
        g.fillEllipse (head.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        head.removeFromLeft (10);
        const juce::String title = onCue ? juce::String (sl.cues[size_t (sl.current)].name) : juce::String ("Live");
        const juce::String meta = onCue ? "Cue " + juce::String (sl.current + 1) + " of " + juce::String (sl.cues.size()) + " " + Glyph::dot() + " "
                                              + look.cueScenes[sl.current]
                                : sl.cues.empty() ? juce::String ("No cues yet")
                                                  : juce::String (sl.cues.size()) + (sl.cues.size() == 1 ? " cue" : " cues") + ", none on yet";
        const auto titleFont = Dine::text (17.0f, 600), metaFont = Dine::text (11.0f, 500);
        const int metaW = Dine::textWidth (metaFont, meta);
        const int titleW = juce::jmin (Dine::textWidth (titleFont, title) + 2, head.getWidth() - (metaW + 10 <= head.getWidth() / 2 ? metaW + 10 : 0));
        g.setColour (Dine::ink);
        g.setFont (titleFont);
        Dine::drawFittedText (g, title, head.removeFromLeft (titleW), juce::Justification::centredLeft, 1, 0.8f);
        // The meta is a second reading: left out whole on a narrow window, never cut.
        if (metaW + 10 <= head.getWidth())
        {
            head.removeFromLeft (10);
            g.setColour (Dine::ink3);
            g.setFont (metaFont);
            Dine::drawText (g, meta, head.removeFromLeft (metaW), juce::Justification::centredLeft, false);
        }
        Dine::drawSegmentTrack (g, l.viewTrack);
    }
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 500));
    Dine::drawText (g, "Scene", l.sceneCaption, juce::Justification::centredLeft, false);
    Dine::fillRounded (g, l.sceneTrack.toFloat(), Dine::control, 7.0f);
    if (! l.empty.isEmpty())
    {
        g.setColour (Dine::ink3);
        g.setFont (calloutFont());
        Dine::drawFittedText (g, view == View::Alerts ? "Nothing needs attention. Every input arrives at a level DINE can work with."
                                                      : "There is nothing to show here yet. Assign the inputs, and every one gets a strip.",
                              l.empty.withSizeKeepingCentre (juce::jmin (l.empty.getWidth(), 420), 40), juce::Justification::centred, 2, 1.0f);
    }
    // The mark the sidebar's SCENES row leaves: a ring round the picker for a second, so an
    // eye that came looking for the scenes finds them without anything changing.
    if (sceneFlash > 0)
    {
        Dine::hairlineRounded (g, l.sceneTrack.expanded (4, 4).toFloat(),
                               Dine::accent.withAlpha (juce::jmin (1.0f, float (sceneFlash) / 30.0f)), 9.0f);
        --sceneFlash;
    }

    // ---- a card on the rail: a dot, a title and the words under it
    auto header = [&g] (juce::Rectangle<int> card, juce::Colour dot, const juce::String& title)
    {
        auto head = card.reduced (kCardPadX, kCardPadY).removeFromTop (kHeadH);
        g.setColour (dot);
        g.fillEllipse (head.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        head.removeFromLeft (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, title, head, juce::Justification::centredLeft, true);
    };
    auto body = [] (juce::Rectangle<int> card) { return card.reduced (kCardPadX, kCardPadY).withTrimmedTop (kHeadH + kCardGap); };

    // UP NEXT: the cue Space goes to, what to bring up and take down, and its GO.
    if (! l.upNext.isEmpty())
    {
        const auto& sl = look.setlist;
        const int next = sl.next();
        Dine::fillRounded (g, l.upNext.toFloat(), Dine::card, 8.0f);
        auto r = l.upNext.reduced (kCardPadX, kCardPadY);
        g.setColour (Dine::ink3);
        g.setFont (noteFont());
        Dine::drawText (g, "Up next", r.removeFromTop (14), juce::Justification::centredLeft, false);
        r.removeFromTop (4);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        Dine::drawFittedText (g, next >= 0 ? juce::String (sl.cues[size_t (next)].name) : juce::String ("The last cue is on"),
                              r.removeFromTop (22), juce::Justification::centredLeft, 1, 0.8f);
        r.removeFromTop (2);
        g.setColour (Dine::ink3);
        g.setFont (noteFont());
        const juce::String meta = next >= 0 ? "Cue " + juce::String (next + 1) + " " + Glyph::dot() + " " + look.cueScenes[next]
                                                + " " + Glyph::dot() + " Space"
                                            : juce::String ("That was the last cue. Edit adds more.");
        Dine::drawFittedText (g, meta, r.removeFromTop (14), juce::Justification::centredLeft, 1, 0.9f);
        if (next >= 0)
        {
            const auto& cue = sl.cues[size_t (next)];
            juce::ignoreUnused (cue);
            for (const auto& note : { std::pair<const char*, juce::String> { "Louder", look.cueLouder[next] }, { "Softer", look.cueSofter[next] } })
            {
                if (note.second.isEmpty()) continue;
                r.removeFromTop (6);
                auto line = r.removeFromTop (14);
                g.setColour (Dine::ink3);
                g.setFont (calloutFont());
                Dine::drawText (g, note.first, line.removeFromLeft (Dine::textWidth (calloutFont(), note.first) + 12), juce::Justification::centredLeft, false);
                g.setColour (Dine::ink);
                Dine::drawFittedText (g, note.second, line, juce::Justification::centredRight, 1, 0.85f);
            }
        }
    }

    // THE SETLIST: the header, "3 of 10" and Edit; the rows are the CueList's.
    if (! l.setlist.isEmpty())
    {
        const auto& sl = look.setlist;
        Dine::fillRounded (g, l.setlist.toFloat(), Dine::card, 8.0f);
        if (setlistFlash > 0)
        {
            Dine::hairlineRounded (g, l.setlist.toFloat().reduced (0.5f), Dine::accent.withAlpha (juce::jmin (1.0f, float (setlistFlash) / 30.0f)), 8.0f);
            --setlistFlash;
        }
        auto head = l.setlist.reduced (kCardPadX, kCardPadY).removeFromTop (kHeadH);
        head.removeFromRight (editSetlist.getWidth() + 8 + (clearCueButton.isVisible() ? clearCueButton.getWidth() + 6 : 0));
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, "Cues", head, juce::Justification::centredLeft, false);
        if (! sl.cues.empty())
        {
            const juce::String count = sl.current >= 0 ? juce::String (sl.current + 1) + " of " + juce::String (sl.cues.size())
                                                       : juce::String (sl.cues.size()) + (sl.cues.size() == 1 ? " cue" : " cues");
            g.setColour (Dine::ink3);
            g.setFont (noteFont());
            Dine::drawText (g, count, head, juce::Justification::centredRight, false);
        }
        else
        {
            auto text = l.setlist.reduced (kCardPadX, kCardPadY).withTrimmedTop (kHeadH + 8);
            g.setColour (Dine::ink2);
            g.setFont (calloutFont());
            Dine::drawFittedText (g, "No cues yet. Add the songs and moments of the service in order, and Space goes from one to the next.",
                                  text, juce::Justification::topLeft, juce::jmax (1, text.getHeight() / 16), 1.0f);
        }
    }

    // LIVE SAFE: amber on its own ground while the sound is locked; a quiet card while it is not.
    if (! l.safe.isEmpty())
    {
        Dine::fillRounded (g, l.safe.toFloat(), look.safe ? Dine::refuse : Dine::card, 8.0f);
        if (look.safe) Dine::hairlineRounded (g, l.safe.toFloat().reduced (0.5f), Dine::warn, 8.0f);
        header (l.safe, look.safe ? Dine::warn : Dine::ink4, look.safe ? "Live safe is on" : "Live safe is off");
        if (! l.safeCompact)
        {
            auto text = body (l.safe);
            g.setColour (Dine::ink2);
            g.setFont (calloutFont());
            Dine::drawFittedText (g, safeText(), text, juce::Justification::topLeft, juce::jmax (1, text.getHeight() / 16), 1.0f);
        }
    }

    // AUTOPILOT: the second thing allowed to move a level by itself, so while it is on the card
    // says so, says what it has had to move and why, and says where its fence is.
    if (! l.autopilot.isEmpty())
    {
        Dine::fillRounded (g, l.autopilot.toFloat(), look.autopilotOn ? Dine::editGround : Dine::card, 8.0f);
        if (look.autopilotOn) Dine::hairlineRounded (g, l.autopilot.toFloat().reduced (0.5f), Dine::monitor, 8.0f);
        header (l.autopilot, look.autopilotOn ? Dine::monitor : Dine::ink4, "Autopilot");
        auto text = body (l.autopilot);
        if (l.autopilotCompact) {}
        else if (look.autopilotOn)
        {
            const auto timeFont = Dine::mono (11.0f, 500);
            const int timeW = Dine::textWidth (timeFont, "00:00") + 4;
            if (look.autopilotLog.isEmpty())
            {
                g.setColour (Dine::ink3);
                g.setFont (calloutFont());
                Dine::drawText (g, "Nothing to do: the mix is inside tolerance.", text.removeFromTop (16), juce::Justification::centredLeft, true);
                text.removeFromTop (6);
            }
            for (const auto& entry : look.autopilotLog)
            {
                const auto when = entry.upToFirstOccurrenceOf ("\t", false, false);
                const auto what = entry.fromFirstOccurrenceOf ("\t", false, false);
                const int lines = wrapLines (calloutFont(), what, text.getWidth() - timeW - 10);
                auto line = text.removeFromTop (16 * lines);
                g.setColour (Dine::monitor);
                g.setFont (timeFont);
                Dine::drawText (g, when, line.removeFromLeft (timeW).removeFromTop (16), juce::Justification::centredLeft, false);
                line.removeFromLeft (10);
                g.setColour (Dine::ink);
                g.setFont (calloutFont());
                Dine::drawFittedText (g, what, line, juce::Justification::topLeft, lines, 1.0f);
                text.removeFromTop (6);
            }
            text.removeFromTop (4);
            g.setColour (Dine::ink3);
            g.setFont (noteFont());
            Dine::drawFittedText (g, autopilotText(), text, juce::Justification::topLeft, juce::jmax (1, text.getHeight() / 14), 1.0f);
        }
        else
        {
            g.setColour (Dine::ink2);
            g.setFont (calloutFont());
            Dine::drawFittedText (g, autopilotText(), text, juce::Justification::topLeft, juce::jmax (1, text.getHeight() / 16), 1.0f);
        }
    }

    // NEEDS ATTENTION: a lamp, what is wrong in a sentence, and what to do about it.
    if (! l.attention.isEmpty())
    {
        Dine::fillRounded (g, l.attention.toFloat(), Dine::card, 8.0f);
        header (l.attention, Dine::warn, "Needs attention");
        auto text = body (l.attention);
        for (int i = 0; i < attentionShown && i < look.attention.size(); ++i)
        {
            const auto& a = look.attention[i];
            const auto what = a.upToFirstOccurrenceOf ("\t", false, false);
            const auto rest = a.fromFirstOccurrenceOf ("\t", false, false);
            const auto detail = rest.upToFirstOccurrenceOf ("\t", false, false);
            const bool crit = rest.endsWith ("c");
            // The whole sentence or none of it: a cut instruction is worse than the name alone,
            // and "Check inputs" says the rest.
            const int full = wrapLines (noteFont(), detail, text.getWidth() - 14);
            const int lines = full <= attentionLines ? full : 0;
            auto row = text.removeFromTop (18 + 14 * lines);
            text.removeFromTop (10);
            g.setColour (crit ? Dine::crit : Dine::warn);
            g.fillEllipse (row.withWidth (7).withHeight (18).withSizeKeepingCentre (7, 7).toFloat());
            row.removeFromLeft (14);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (12.5f, 600));
            Dine::drawFittedText (g, what, row.removeFromTop (18), juce::Justification::centredLeft, 1, 0.85f);
            if (lines > 0)
            {
                g.setColour (Dine::ink3);
                g.setFont (noteFont());
                Dine::drawFittedText (g, detail, row, juce::Justification::topLeft, lines, 1.0f);
            }
        }
    }

    // SPEAKING MICS: two rows, each a name, a line saying what it does, and its switch.
    if (! l.speaking.isEmpty())
    {
        const bool any = look.priorityOn || look.shareOn;
        Dine::fillRounded (g, l.speaking.toFloat(), any ? Dine::editGround : Dine::card, 8.0f);
        if (any) Dine::hairlineRounded (g, l.speaking.toFloat().reduced (0.5f), Dine::monitor, 8.0f);
        header (l.speaking, any ? Dine::monitor : Dine::ink4, "Speaking mics");
        auto row = [&g] (juce::Rectangle<int> r, const juce::String& name, const juce::String& what, bool on)
        {
            g.setColour (on ? Dine::ink : Dine::ink2);
            g.setFont (Dine::text (13.0f, 600));
            Dine::drawText (g, name, r.removeFromTop (18), juce::Justification::centredLeft, true);
            if (r.getHeight() < 14) return;                 // compact: the name and its switch only
            g.setColour (Dine::ink3);
            g.setFont (noteFont());
            Dine::drawFittedText (g, what, r, juce::Justification::topLeft, juce::jmax (1, r.getHeight() / 14), 1.0f);
        };
        row (l.priorityRow, "Speech priority", priorityText(), look.priorityOn);
        row (l.shareRow, "Share the mics", shareText(), look.shareOn);
    }

    // WHAT I HEAR: the engineer's own listen, on a card of its own because none of it reaches the room.
    {
        Dine::fillRounded (g, l.monitor.toFloat(), Dine::card, 8.0f);
        auto inner = l.monitor.reduced (kCardPadX, kCardPadY);
        auto titleBlock = inner.withTrimmedRight (clearSolo.getWidth() + 8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, "What I hear", titleBlock.removeFromTop (18), juce::Justification::centredLeft, true);
        titleBlock.removeFromTop (2);
        const int lines = wrapLines (noteFont(), look.monitorNote, titleBlock.getWidth());
        g.setColour (look.inPlace || ! look.routed ? Dine::warn : look.soloCount > 0 ? Dine::accent : Dine::ink3);
        g.setFont (noteFont());
        Dine::drawFittedText (g, look.monitorNote, titleBlock.removeFromTop (14 * lines), juce::Justification::topLeft, lines, 1.0f);

        for (const auto& track : { l.modesA, l.modesB })
            Dine::fillRounded (g, track.toFloat(), Dine::control, 7.0f);
        Dine::drawIcon (g, Dine::Icon::Headphones, l.output.withWidth (16).withSizeKeepingCentre (16, 16).toFloat(),
                        look.routed ? Dine::ink2 : Dine::warn);
    }
}

bool LivePage::anyEffects() const
{
    if (! controller.isPrepared()) return false;
    for (int f = 0; f < int (FxSlot::Count); ++f)
        if (controller.getEngine().isFxUsed (FxSlot (f))) return true;
    return false;
}

void LivePage::showEffects (bool open)
{
    if (open && ! anyEffects()) return;
    setView (open ? View::All : View::Groups);
    // Each effect is at the end of All: scroll there, so "the effects" is what is in view.
    if (open) scroller.setViewPositionProportionately (1.0, 0.0);
}

void LivePage::resized()
{
    auto& l = lay;
    auto r = getLocalBounds().withTrimmedLeft (kPadX).withTrimmedRight (kPadX).withTrimmedTop (kPadTop).withTrimmedBottom (kPadBottom);
    l.health = r.removeFromTop (kHealthH);
    r.removeFromTop (kGap);

    const int railW = juce::jmin (kRailW, juce::jmax (280, r.getWidth() / 3));
    auto rail = r.removeFromRight (railW);
    r.removeFromRight (kGap);

    // ---- the cue that is on, the views, Effects off; then the scenes; then the strips
    {
        auto head = r.removeFromTop (Dine::Metric::button);
        if (effectsOff.isVisible())
        {
            const int w = juce::jmax (84, effectsOff.idealWidth());
            effectsOff.setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, kSegmentH + 2));
            head.removeFromRight (12);
        }
        int widths[4] {}, total = 0;
        for (int i = 0; i < 4; ++i) { widths[i] = juce::jmax (48, viewTabs[size_t (i)]->idealWidth()); total += widths[i]; }
        const int trackW = total + 2 * 3 + 4;
        // The views sit after the cue's name; a long name squeezes, the views never do.
        const int titleRoom = juce::jmax (120, head.getWidth() - trackW - 16);
        l.groupsHeader = head.removeFromLeft (titleRoom);

        head.removeFromLeft (16);
        l.viewTrack = head.removeFromLeft (trackW).withSizeKeepingCentre (trackW, kSegmentH + 4);
        auto seg = l.viewTrack.reduced (2);
        for (int i = 0; i < 4; ++i)
        {
            viewTabs[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
            seg.removeFromLeft (2);
        }

        r.removeFromTop (10);
        auto scenesRow = r.removeFromTop (kSegmentH + 4);
        l.sceneCaption = scenesRow.removeFromLeft (Dine::textWidth (Dine::text (11.0f, 500), "Scene") + 2);
        scenesRow.removeFromLeft (10);
        int sw[4] {}, sTotal = 0;
        for (int i = 0; i < 4; ++i) { sw[i] = juce::jmax (44, sceneSegments[size_t (i)]->idealWidth()); sTotal += sw[i]; }
        l.sceneTrack = scenesRow.removeFromLeft (sTotal + 2 * 3 + 4);
        auto ss = l.sceneTrack.reduced (2);
        for (int i = 0; i < 4; ++i)
        {
            sceneSegments[size_t (i)]->setBounds (ss.removeFromLeft (sw[i]));
            ss.removeFromLeft (2);
        }
        scenesRow.removeFromLeft (10);
        const int keepW = juce::jmax (52, keepButton.idealWidth());
        keepButton.setBounds (scenesRow.removeFromLeft (keepW).withSizeKeepingCentre (keepW, kSegmentH + 2));
        scenesRow.removeFromLeft (6);
        const int renW = renameScene.idealWidth() + 4;
        renameScene.setBounds (scenesRow.removeFromLeft (renW).withSizeKeepingCentre (renW, kSegmentH + 2));

        r.removeFromTop (12);
        l.strips = r;
        l.empty = {};

        shownTiles = tilesFor (view);
        for (auto& t : tiles) t->setVisible (false);
        for (auto& t : inputTiles) t->setVisible (false);
        const int gap = 8;
        if (view == View::ThisCue || view == View::Groups)
        {
            scroller.setVisible (false);
            // A group's own width whatever the view, so a strip is the same size in each.
            auto row = l.strips;
            const int w = (row.getWidth() - gap * (kTiles - 1)) / kTiles;
            for (size_t k = 0; k < shownTiles.size(); ++k)
            {
                auto& t = tiles[size_t (shownTiles[k])];
                t->setVisible (true);
                const bool last = view == View::Groups && k + 1 == shownTiles.size();
                t->setBounds (last ? row : row.removeFromLeft (w));
                row.removeFromLeft (gap);
            }
        }
        else
        {
            // One strip per input (and each effect, on All), the width a v4 strip is, in a row
            // that scrolls sideways when there are more than fit.
            constexpr int kInputW = 84;
            scroller.setVisible (! shownTiles.empty());
            scroller.setBounds (l.strips);
            const int h = l.strips.getHeight() - (int (shownTiles.size()) * (kInputW + gap) > l.strips.getWidth() ? 12 : 0);
            scrollHolder.setSize (juce::jmax (l.strips.getWidth(), int (shownTiles.size()) * (kInputW + gap) - gap), h);
            int x = 0;
            for (const int id : shownTiles)
            {
                GroupTile* t = id >= 1000 ? inputTiles[size_t (id - 1000)].get() : tiles[size_t (id)].get();
                if (id < 1000 && t->getParentComponent() != &scrollHolder) scrollHolder.addChildComponent (*t);
                t->setVisible (true);
                t->setBounds (x, 0, kInputW, h);
                x += kInputW + gap;
            }
            if (shownTiles.empty()) l.empty = l.strips;
        }
        // A group tile lives on the page unless All has borrowed it for the scroller.
        for (int t = 0; t < kAllTiles; ++t)
        {
            const bool inScroller = view == View::All && std::find (shownTiles.begin(), shownTiles.end(), t) != shownTiles.end();
            auto* parent = inScroller ? (juce::Component*) &scrollHolder : (juce::Component*) this;
            if (tiles[size_t (t)]->getParentComponent() != parent) parent->addChildComponent (*tiles[size_t (t)]);
        }
    }

    // ---- the rail: Up next and the setlist at the top, LIVE SAFE and Autopilot, what needs
    // attention and the speaking mics as room allows, what I hear at the foot
    {
        const int textW = railW - 2 * kCardPadX;

        const int clearW = juce::jmax (84, clearSolo.idealWidth());
        const int noteLines = wrapLines (noteFont(), look.monitorNote.isEmpty() ? juce::String ("Press S on a group. Only you hear it.") : look.monitorNote,
                                         textW - clearW - 8);
        const int titleH = juce::jmax (Dine::Metric::button, 18 + 2 + 14 * noteLines);
        // The two tracks share a row when the rail is wide enough for both, and stack when it is not.
        const int modesW = modes[0]->idealWidth() + modes[1]->idealWidth() + 6 + 8 + modes[2]->idealWidth() + modes[3]->idealWidth() + 6;
        const bool stacked = modesW > textW;
        const int modesH = stacked ? 2 * (kSegmentH + 4) + 8 : kSegmentH + 4;
        const int monitorH = kCardPadY + titleH + kCardGap + modesH + kCardGap + 24 + kCardPadY;
        l.monitor = rail.removeFromBottom (monitorH);
        rail.removeFromBottom (12);

        // UP NEXT: what Space goes to, its notes, and its GO.
        const auto& sl = look.setlist;
        const int next = sl.cues.empty() ? -1 : sl.next();
        l.upNext = {};
        goButton.setVisible (false);
        if (! sl.cues.empty())
        {
            int h = kCardPadY + 14 + 4 + 22 + 2 + 14;
            if (next >= 0)
            {
                const auto& cue = sl.cues[size_t (next)];
                juce::ignoreUnused (cue);
                h += (look.cueLouder[next].isEmpty() ? 0 : 20) + (look.cueSofter[next].isEmpty() ? 0 : 20) + 10 + 36;
            }
            h += kCardPadY;
            l.upNext = rail.removeFromTop (h);
            rail.removeFromTop (12);
            if (next >= 0)
            {
                goButton.setButtonText ("Go to " + juce::String (sl.cues[size_t (next)].name));
                goButton.setVisible (true);
                goButton.setBounds (l.upNext.reduced (kCardPadX, kCardPadY).removeFromBottom (36));
            }
        }

        // THE SETLIST: its rows, as many as there is room for once LIVE SAFE and Autopilot have
        // their header lines (they are a press away on the toolbar too).
        const int compactCard = kCardPadY + kHeadH + kCardPadY;
        const int safeFull = kCardPadY + kHeadH + kCardGap + 16 * wrapLines (calloutFont(), safeText(), textW) + kCardPadY;
        int apBody = 0;
        if (look.autopilotOn)
        {
            const int timeW = Dine::textWidth (Dine::mono (11.0f, 500), "00:00") + 4;
            if (look.autopilotLog.isEmpty()) apBody += 16 + 6;
            for (const auto& entry : look.autopilotLog)
                apBody += 16 * wrapLines (calloutFont(), entry.fromFirstOccurrenceOf ("\t", false, false), textW - timeW - 10) + 6;
            apBody += 4 + 14 * wrapLines (noteFont(), autopilotText(), textW);
        }
        else apBody = 16 * wrapLines (calloutFont(), autopilotText(), textW);
        const int apFull = kCardPadY + kHeadH + kCardGap + apBody + kCardPadY;

        const int listHead = kCardPadY + kHeadH + 8;
        const int emptyLines = sl.cues.empty() ? wrapLines (calloutFont(), "No cues yet. Add the songs and moments of the service in order, "
                                                                             "and Space goes from one to the next.", textW) : 0;
        // Now and Next at least; more as the rail allows.
        const int minRows = sl.cues.empty() ? 0 : juce::jmin (int (sl.cues.size()), 2);
        const int setlistMin = listHead + (sl.cues.empty() ? 16 * emptyLines : minRows * CueList::kRowH) + kCardPadY;
        // Room is kept for LIVE SAFE's and Autopilot's header lines only when they can have it.
        const bool cardsFit = rail.getHeight() - setlistMin - 12 >= 2 * (compactCard + 12);
        const int reserved = cardsFit ? 2 * (compactCard + 12) : 0;
        int setlistH = setlistMin;
        if (! sl.cues.empty())
        {
            const int roomForRows = (rail.getHeight() - reserved - listHead - kCardPadY) / CueList::kRowH;
            setlistH = listHead + juce::jlimit (minRows, int (sl.cues.size()), juce::jmin (roomForRows, 8)) * CueList::kRowH + kCardPadY;
        }
        l.setlist = rail.removeFromTop (juce::jmin (setlistH, juce::jmax (0, rail.getHeight())));
        rail.removeFromTop (12);
        cueList->setVisible (! sl.cues.empty());
        cueList->setBounds (l.setlist.reduced (kCardPadX - 8, kCardPadY).withTrimmedTop (kHeadH + 8));
        editSetlist.setButtonText (sl.cues.empty() ? "Add cues" : "Edit");
        {
            auto headRow = l.setlist.reduced (kCardPadX, kCardPadY).removeFromTop (kHeadH);
            const int w = editSetlist.idealWidth() + 4;
            editSetlist.setBounds (headRow.removeFromRight (w).expanded (0, 3));
            // Clear cue beside Edit, while one is on (the toolbar's CUE pill clears it too).
            clearCueButton.setVisible (controller.isCueActive() && ! l.setlist.isEmpty());
            if (clearCueButton.isVisible())
            {
                headRow.removeFromRight (6);
                const int cw = clearCueButton.idealWidth() + 4;
                clearCueButton.setBounds (headRow.removeFromRight (cw).expanded (0, 3));
            }
        }

        // LIVE SAFE and Autopilot: whole when there is room, their header lines when not.
        // A card with no room for even its header line is left out whole - the toolbar has
        // LIVE SAFE and Auto - never drawn as a sliver.
        l.safeCompact = safeFull + 12 + apFull > rail.getHeight();
        const int safeH = l.safeCompact ? compactCard : safeFull;
        l.safe = safeH <= rail.getHeight() ? rail.removeFromTop (safeH) : juce::Rectangle<int>();
        if (! l.safe.isEmpty()) rail.removeFromTop (12);
        l.autopilotCompact = apFull > rail.getHeight();
        const int apH = l.autopilotCompact ? compactCard : apFull;
        l.autopilot = apH <= rail.getHeight() ? rail.removeFromTop (apH) : juce::Rectangle<int>();
        if (! l.autopilot.isEmpty()) rail.removeFromTop (12);
        safeLink->setVisible (! l.safe.isEmpty());
        autopilotLink->setVisible (! l.autopilot.isEmpty());

        // NEEDS ATTENTION, in what is left: as many of the inputs the desk should move as fit,
        // each its name and what to do (the whole sentence when it fits, never a cut one).
        {
            l.attention = {};
            if (! look.attention.isEmpty())
            {
                attentionLines = rail.getHeight() > 260 ? 4 : 2;
                int h = kCardPadY + kHeadH + kCardGap + kCardPadY;
                int fits = 0;
                for (const auto& a : look.attention)
                {
                    const auto detail = a.fromFirstOccurrenceOf ("\t", false, false).upToFirstOccurrenceOf ("\t", false, false);
                    const int full = wrapLines (noteFont(), detail, textW - 14);
                    const int rowH = 18 + 14 * (full <= attentionLines ? full : 0) + 10;
                    if (h + rowH > rail.getHeight()) break;
                    h += rowH;
                    ++fits;
                }
                if (fits > 0) { l.attention = rail.removeFromTop (h); rail.removeFromTop (12); }
                attentionShown = fits;
            }
            checkLink->setVisible (! l.attention.isEmpty());
        }

        // SPEAKING MICS: two rows of a name and one line each, or the names and their switches
        // alone when the rail is short, or not at all (the Mix menu has both).
        {
            const int rowTextW = textW - 90;
            const int pFull = 18 + 2 + 14 * wrapLines (noteFont(), priorityText(), rowTextW);
            const int sFull = 18 + 2 + 14 * wrapLines (noteFont(), shareText(), rowTextW);
            const int full = kCardPadY + kHeadH + kCardGap + pFull + 10 + sFull + kCardPadY;
            const bool compact = full > rail.getHeight();
            const int p = compact ? 18 : pFull, s = compact ? 18 : sFull;
            const int wanted = kCardPadY + kHeadH + kCardGap + p + (compact ? 6 : 10) + s + kCardPadY;
            const bool fits = wanted <= rail.getHeight();
            priorityLink->setVisible (fits);
            shareLink->setVisible (fits);
            l.speaking = fits ? rail.removeFromTop (wanted) : juce::Rectangle<int>();
            auto inner = l.speaking.reduced (kCardPadX, kCardPadY).withTrimmedTop (kHeadH + kCardGap);
            l.priorityRow = inner.removeFromTop (p).withTrimmedRight (90);
            inner.removeFromTop (compact ? 6 : 10);
            l.shareRow = inner.removeFromTop (s).withTrimmedRight (90);
        }

        auto inner = l.monitor.reduced (kCardPadX, kCardPadY);
        auto titleRow = inner.removeFromTop (titleH);
        clearSolo.setBounds (titleRow.removeFromRight (clearW).removeFromTop (Dine::Metric::button));
        inner.removeFromTop (kCardGap);

        auto modesRow = inner.removeFromTop (modesH);
        auto track = [&modesRow, stacked] (DineButton& a, DineButton& b)
        {
            const int wa = a.idealWidth(), wb = b.idealWidth();
            auto line = stacked ? modesRow.removeFromTop (kSegmentH + 4) : modesRow;
            if (stacked) modesRow.removeFromTop (8);
            auto t = line.removeFromLeft (juce::jmin (line.getWidth(), wa + wb + 6));
            if (! stacked) modesRow.removeFromLeft (wa + wb + 6 + 8);
            auto seg = t.reduced (2);
            a.setBounds (seg.removeFromLeft (juce::jmin (wa, seg.getWidth())));
            seg.removeFromLeft (2);
            b.setBounds (seg);
            return t;
        };
        l.modesA = track (*modes[0], *modes[1]);
        l.modesB = track (*modes[2], *modes[3]);
        inner.removeFromTop (kCardGap);

        l.output = inner.removeFromTop (24);
        auto out = l.output;
        out.removeFromLeft (16 + 6);
        monitorLevel->setBounds (out.removeFromRight (kLevelW).withSizeKeepingCentre (kLevelW, 16));
        out.removeFromRight (8);
        const int dimW = juce::jmax (34, monitorDim.idealWidth());
        monitorDim.setBounds (out.removeFromRight (dimW).withSizeKeepingCentre (dimW, 20));
        out.removeFromRight (8);
        soloDevice.setBounds (out.withWidth (juce::jmin (out.getWidth(), soloDevice.idealWidth())));
    }

    // the links at the right of the two cards' headers
    auto linkArea = [] (juce::Rectangle<int> card, int w) { return card.reduced (kCardPadX, kCardPadY).removeFromTop (kHeadH).removeFromRight (w); };
    safeLink->setBounds (linkArea (l.safe, juce::jmin (180, safeLink->idealWidth())));
    if (checkLink->isVisible()) checkLink->setBounds (linkArea (l.attention, juce::jmin (140, checkLink->idealWidth())));
    autopilotLink->setBounds (linkArea (l.autopilot, juce::jmin (200, autopilotLink->idealWidth())));
    auto rowLink = [&l] (juce::Rectangle<int> row, int w)
    {
        return juce::Rectangle<int> (l.speaking.getRight() - kCardPadX - w, row.getY(), w, 18);
    };
    priorityLink->setBounds (rowLink (l.priorityRow, juce::jmin (84, priorityLink->idealWidth())));
    shareLink->setBounds (rowLink (l.shareRow, juce::jmin (84, shareLink->idealWidth())));
}

} // namespace livemix

namespace livemix
{

// The picker reads the controller: every scene's name, and KEEP only once one is picked.
void LivePage::refreshScenes()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto& scene = controller.getScene (i);
        auto& seg = *sceneSegments[size_t (i)];
        const juce::String name (scene.name.empty() ? defaultSceneName (i) : scene.name.c_str());
        if (seg.getButtonText() != name.toUpperCase())
        {
            seg.setButtonText (name.toUpperCase());
            if (monitorLevel != nullptr) resized();          // a renamed scene is a wider segment
        }
        seg.setToggleState (i == sceneSlot, juce::dontSendNotification);
        juce::String when;
        if (scene.kept && scene.whenMs > 0) when = clockTime (scene.whenMs);
        seg.setTooltip (scene.kept ? "Bring the " + name + " mix back - every fader, chain and macro - in one press"
                                         + (when.isEmpty() ? juce::String() : " (kept " + when + ")") + ". UNDO takes it back."
                                   : "Nothing is kept under " + name + " yet. Pick it, set the mix, then press Keep.");
    }
    keepButton.setEnabled (sceneSlot >= 0);
    keepButton.setTooltip (sceneSlot >= 0 ? "Keep the mix as it is now under " + juce::String (controller.getScene (sceneSlot).name) + "."
                                          : juce::String ("Pick a scene first, then Keep puts the mix as it is now under its name."));
}

} // namespace livemix
