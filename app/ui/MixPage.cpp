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

    juce::String groupName (int i)
    {
        return i < kGroupBuses ? juce::String (mixBusName (MixBus (i))).toUpperCase() : juce::String ("FX RETURNS");
    }

    juce::Colour groupColour (int i) noexcept
    {
        return i >= 0 && i < kGroupBuses ? Dine::busTint (MixBus (i)) : Dine::ink2;
    }

    constexpr int kGroupsH = 210;
    constexpr int kRibbonGap = 22;      // the ENERGY ribbon sits clear of the snap rows above it

    juce::String dbText (float db)
    {
        if (db <= -119.0f) return Glyph::dash();
        return (db >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (db), 1);
    }
}

// ------------------------------------------------------------------ GroupTile
// One group bus: its name in its colour, a fader and a meter side by side, and its level.
class MixPage::GroupTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    GroupTile (MixController& c, int index) : controller (c), group (index)
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
                                 : "Level for the whole group. Double-click for 0.0 dB.");
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (isFx()) controller.setFxReturn (float (fader.getValue()));
            else        controller.setBusFader (MixBus (group), float (fader.getValue()));
            repaint (readout);
        };
    }

    void set (bool isUsed, float peakDb, float holdDb, bool clipped, bool isMuted, float faderDb, int heardState)
    {
        updating = true;
        const bool wasUsed = used;
        bool body = used != isUsed || heard != heardState || muted != isMuted;
        used = isUsed; heard = heardState; muted = isMuted;
        if (wasUsed != used) resized();      // the TUNE verb appears with the group
        meter.setLevels (peakDb, holdDb, clipped);
        meter.setMuted (isMuted || ! used);
        if (std::fabs (faderDb - float (fader.getValue())) > 0.01f) { fader.setValue (faderDb, juce::dontSendNotification); repaint (readout); }
        fader.setEnabled (used);
        updating = false;
        if (body) repaint();
    }

    // TUNE <GROUP>, the same verb the input rail carries for one channel: DLIVE listens to
    // the whole console and applies only this group, so the band can be tuned during the
    // song and the pastor during the sermon without either moving the other.
    std::function<void()> onTune;

    void mouseEnter (const juce::MouseEvent&) override { if (! verbRect.isEmpty()) repaint (verbRect); }
    void mouseExit  (const juce::MouseEvent&) override { if (! verbRect.isEmpty()) repaint (verbRect); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! verbRect.contains (e.getPosition())) return;
        if (canTune() && onTune) onTune();
    }

    void paint (juce::Graphics& g) override
    {
        Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::tile, Dine::Radius::card);
        auto r = getLocalBounds().reduced (10, 14);
        g.setColour (used ? groupColour (group) : Dine::ink4);
        g.setFont (Dine::caps (11.0f, 0.06f, 500));
        Dine::drawText (g, groupName (group), r.removeFromTop (14), juce::Justification::centred);
        if (heard == 2)
        {
            g.setColour (Dine::ok);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ float (getWidth() - 14), 14.0f }));
        }
        else if (heard == 1)
        {
            g.setColour (Dine::warn);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ float (getWidth() - 14), 14.0f }));
        }
        g.setColour (! used ? Dine::ink4 : muted ? Dine::warn : Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, ! used ? "not in this mix" : muted ? "NOT HEARD" : dbText (float (fader.getValue())), readout, juce::Justification::centred);

        if (canTune())
        {
            g.setColour (isMouseOver (true) ? Dine::accent : Dine::ink3);
            g.setFont (Dine::caps (9.5f, 0.06f));
            Dine::drawText (g, "TUNE", verbRect, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 14);
        r.removeFromTop (14 + 10);
        verbRect = canTune() ? r.removeFromBottom (13) : juce::Rectangle<int>();
        if (canTune()) r.removeFromBottom (4);
        readout = r.removeFromBottom (14);
        r.removeFromBottom (10);
        auto pair = r.withSizeKeepingCentre (20 + 6 + 7, r.getHeight());
        fader.setBounds (pair.removeFromLeft (20));
        pair.removeFromLeft (6);
        meter.setBounds (pair);
    }

private:
    bool isFx() const noexcept { return group >= kGroupBuses; }
    // The returns are not a group of sources, so there is nothing to listen to and tune.
    bool canTune() const noexcept { return used && ! isFx(); }

    MixController& controller;
    int group;
    bool used = false, muted = false, updating = false;
    int heard = 0;
    juce::Rectangle<int> readout, verbRect;
    DineMeter meter { DineMeter::Style::Bar };
    juce::Slider fader;
};

// ------------------------------------------------------------------ SidePanel
// The right column, in its own scroll: the verbs, a card that says what the pad under the
// pointer does (or which pad is off the plan), then MIX HEALTH wrapped to the panel's width.
// The buttons are the page's; they are laid out here so the stack is measured in one place.
class MixPage::SidePanel : public juce::Component
{
public:
    explicit SidePanel (MixPage& p) : page (p) {}

    static constexpr int kPad = 18;

    // Measures the stack for a width; `place` also positions the buttons. Returns the height needed.
    int layoutFor (int width, bool place)
    {
        const int inner = width - kPad * 2;
        int y = kPad;
        auto put = [&] (juce::Component& c, juce::Rectangle<int> r) { if (place) c.setBounds (r); };
        put (page.tuneButton, { kPad, y, inner, 44 });          y += 44 + 9;
        put (page.liveTuneButton, { kPad, y, inner, 44 });      y += 44 + 9;
        {
            const int half = (inner - 9) / 2;
            put (page.referenceButton, { kPad, y, half, Dine::Metric::button });
            put (page.chatButton, { kPad + half + 9, y, inner - half - 9, Dine::Metric::button });
            y += Dine::Metric::button + 9;
            put (page.undoButton, { kPad, y, half, Dine::Metric::button });
            put (page.redoButton, { kPad + half + 9, y, inner - half - 9, Dine::Metric::button });
            y += Dine::Metric::button + 9;
            put (page.historyButton, { kPad, y, inner, Dine::Metric::button });
            y += Dine::Metric::button + 9;
        }
        stamp = { kPad, y, inner, 16 };                          y += 16 + 4;
        put (page.advancedButton, { kPad, y, juce::jmin (inner, juce::jmax (120, page.advancedButton.idealWidth())), Dine::Metric::control });
        y += Dine::Metric::control + kPad;

        // the pad card
        {
            const int textW = inner - 16 - 14;
            const int bodyH = juce::jmax (16, textHeight (Dine::text (12.5f), body, textW));
            card = { kPad, y, inner, 14 + 14 + 8 + bodyH + 14 };
            y += card.getHeight() + kPad;
        }

        // MIX HEALTH
        healthCap = { kPad, y, inner, 14 };                      y += 14 + 12;
        notes.clear();
        for (const auto& n : page.controller.getMixHealthNotes())
        {
            const juce::String text (n);
            const auto lower = text.toLowerCase();
            const bool bad = lower.contains ("clipping") || lower.contains ("barely") || lower.contains ("not heard");
            const bool watch = lower.contains ("preamp") || lower.contains ("digital");
            const int h = textHeight (Dine::text (13.0f), text, inner);
            notes.push_back ({ text, bad ? Dine::crit : watch ? Dine::warn : Dine::ink2, { kPad, y, inner, h } });
            y += h + 10;
        }
        bool statusIsNote = false;
        for (const auto& n : notes) if (n.text == page.status) statusIsNote = true;
        statusBox = {};
        if (! statusIsNote && page.status.isNotEmpty())
        {
            const int h = textHeight (Dine::text (13.0f), page.status, inner);
            statusBox = { kPad, y, inner, h };
            y += h + 10;
        }
        return y + kPad;
    }

    void setCard (const juce::String& h, const juce::String& b) { heading = h; body = b; }

    void resized() override { layoutFor (getWidth(), true); }

    void paint (juce::Graphics& g) override
    {
        // the stamp under the verbs
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (12.5f));
        const int tunes = page.controller.getTuneCount();
        juce::String text = tunes > 0 ? "Tuned " + juce::String (tunes) + (tunes == 1 ? " time" : " times") + " this session"
                                      : juce::String ("Not tuned yet");
        if (page.controller.hasReference()) text += "  " + Glyph::dot() + "  aimed at " + juce::String (page.controller.getReference().name);
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
            g.setFont (Dine::caps (10.5f, 0.10f));
            Dine::drawText (g, heading, in.removeFromTop (14), juce::Justification::centredLeft, true);
            in.removeFromTop (8);
            drawWrapped (g, body, Dine::text (12.5f), Dine::ink2, in);
        }

        Dine::drawSection (g, healthCap, page.health > 0 ? "MIX HEALTH  " + juce::String (Glyph::dot()) + "  " + juce::String (page.health) + "%"
                                                         : "MIX HEALTH");
        for (const auto& n : notes) drawWrapped (g, n.text, Dine::text (13.0f), n.colour, n.box);
        if (! statusBox.isEmpty()) drawWrapped (g, page.status, Dine::text (13.0f), Dine::ink3, statusBox);
    }

private:
    static int textHeight (const juce::Font& f, const juce::String& s, int width)
    {
        if (s.isEmpty() || width <= 0) return 0;
        juce::AttributedString a; a.setText (s); a.setFont (f);
        juce::TextLayout tl; tl.createLayout (a, float (width));
        return int (std::ceil (tl.getHeight())) + 2;
    }
    static void drawWrapped (juce::Graphics& g, const juce::String& s, const juce::Font& f, juce::Colour c, juce::Rectangle<int> box)
    {
        if (s.isEmpty() || box.isEmpty()) return;
        juce::AttributedString a; a.setText (s); a.setFont (f); a.setColour (c);
        juce::TextLayout tl; tl.createLayout (a, float (box.getWidth()));
        tl.draw (g, box.toFloat());
    }

    struct Note { juce::String text; juce::Colour colour; juce::Rectangle<int> box; };
    MixPage& page;
    juce::String heading, body;
    juce::Rectangle<int> stamp, card, healthCap, statusBox;
    std::vector<Note> notes;
};

// ------------------------------------------------------------------ InputRow (the rail)
// A name and the one verb this rail exists for. Clicking the row picks the channel out
// (the chain along the foot follows it); clicking TUNE CHANNEL listens to it alone.
class MixPage::InputRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    InputRow (const juce::String& n, ChannelRole role, int number, const std::string& iconKey)
        : name (n), icon (Dine::iconFor (iconKey, role)), num (number)
    {
        setTooltip ("Click to pick " + name + " out. TUNE CHANNEL listens to it on its own - nothing else in the mix moves. "
                    "FOCUS makes it the source the whole mix is built around: every level is set against it, and the music "
                    "makes room for it rather than the other way round.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onTune, onSelect, onFocus;

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition())) return;
        if (e.getPosition().x >= verbRect.getX() - 6) { if (onTune) onTune(); }
        else if (! focusRect.isEmpty() && e.getPosition().x >= focusRect.getX() - 4 && e.getPosition().x < focusRect.getRight() + 4)
        { if (onFocus) onFocus(); }
        else if (onSelect) onSelect();
    }

    void set (bool isMuted, bool isFaint, bool isSelected, bool isFocal)
    {
        if (muted != isMuted || faint != isFaint || selected != isSelected || focal != isFocal)
        {
            muted = isMuted; faint = isFaint; selected = isSelected; focal = isFocal; repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (selected)   g.fillAll (Dine::card);
        else if (hover) g.fillAll (Dine::tile);

        auto r = getLocalBounds().reduced (14, 0);
        const auto verbFont = Dine::caps (10.0f, 0.06f);
        const int verbW = Dine::textWidth (verbFont, "TUNE CHANNEL");
        verbRect = r.removeFromRight (verbW);
        g.setColour (Dine::accent);
        g.setFont (verbFont);
        Dine::drawText (g, "TUNE CHANNEL", verbRect, juce::Justification::centredRight);
        r.removeFromRight (8);

        // THE FOCAL SOURCE: what the mix is built around. Shown always once it is set, offered
        // on hover before that, so a rail of thirty inputs is not a row of thirty labels.
        focusRect = {};
        if (focal || hover)
        {
            const auto focusFont = Dine::caps (10.0f, 0.06f);
            focusRect = r.removeFromRight (Dine::textWidth (focusFont, "FOCUS"));
            g.setColour (focal ? Dine::accent : Dine::ink4);
            g.setFont (focusFont);
            Dine::drawText (g, "FOCUS", focusRect, juce::Justification::centredRight);
            r.removeFromRight (10);
        }

        if (muted || faint)
        {
            g.setColour (muted ? Dine::warn : Dine::crit);
            g.fillEllipse (r.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            r.removeFromLeft (6);
        }
        g.setColour (selected ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (12.5f, selected ? 600 : 400));
        Dine::drawText (g, name, r, juce::Justification::centredLeft, true);
    }

    juce::String name;
    Dine::Icon icon;
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

// ------------------------------------------------------------------ ListenSheet
// A card in the middle of the workspace while DLIVE listens: what it is doing, how far it
// is, what it can hear, and the one way out.
class MixPage::ListenSheet : public juce::Component
{
public:
    explicit ListenSheet (MixController& c) : controller (c)
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

    static constexpr int kCardW = 540, kCardH = 318;

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (kCardW, getWidth() - 40);
        return juce::Rectangle<int> (w, juce::jmin (kCardH, getHeight() - 20)).withCentre (getLocalBounds().getCentre());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::desk.withAlpha (0.88f));
        auto card = sheetBounds();
        Dine::drawSheet (g, card.toFloat(), 14.0f);

        const bool waiting = controller.isWaitingForBand();
        const bool planning = controller.getStage() == MixController::Stage::Planning;
        const float progress = planning ? 1.0f : controller.getListenProgress();
        const bool live = controller.isTuningLive();
        const auto liveState = controller.getTuneLive().getState();
        const bool capturing = liveState == TuneLiveCoordinator::State::CapturingInitial
                            || liveState == TuneLiveCoordinator::State::CapturingVerify;
        const bool working = (live && ! capturing) || planning;
        const juce::String verb = live ? "TUNE LIVE MIX" : controller.isTuningChannel() ? "TUNE CHANNEL" : "TUNE MIX";

        auto r = card.reduced (34, 34);
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (12.0f, 0.10f));
        Dine::drawText (g, verb + (working ? " IS WORKING" : waiting ? " IS WAITING" : " IS LISTENING"), r.removeFromTop (14), juce::Justification::centred);
        r.removeFromTop (12);

        // The number: seconds left in a listen, per cent through the work.
        const float seconds = controller.isTuningChannel() ? MixController::channelListen().seconds : 30.0f;
        const juce::String big = waiting ? juce::String (Glyph::dash())
                               : working ? juce::String (juce::jmin (99, int (std::round (progress * 100.0f)))) + "%"
                                         : juce::String (juce::jmax (0, int (std::ceil (seconds * (1.0f - progress))))) + " s";
        g.setColour (Dine::ink);
        g.setFont (Dine::mono (52.0f, 500));
        Dine::drawText (g, big, r.removeFromTop (58), juce::Justification::centred);
        r.removeFromTop (8);

        juce::String hearing;
        if (live)
        {
            hearing = juce::String (controller.getTuneLiveStatus());
            if (capturing) hearing += " Keep the band playing.";
        }
        else if (waiting) hearing = "Have the band play a song the way they normally would. DLIVE starts as soon as it hears them.";
        else if (planning) hearing = "Comparing what it heard against " + juce::String (styleProfileName (controller.getSession().profile)) + ", then checking its own work.";
        else
        {
            juce::StringArray heard;
            for (int i = 0; i < kGroupBuses; ++i)
                if (controller.getEngine().isBusUsed (MixBus (i)) && controller.busHeard (MixBus (i)))
                    heard.add (juce::String (mixBusName (MixBus (i))).toLowerCase());
            hearing = heard.isEmpty() ? "Listening to every input at once. Keep the band playing."
                                      : "Hearing " + heard.joinIntoString (", ") + ". Keep the band playing.";
        }
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (13.0f));
        Dine::drawFittedText (g, hearing, r.removeFromTop (40), juce::Justification::centredTop, 2);
        r.removeFromTop (10);

        auto bar = r.removeFromTop (4);
        Dine::fillRounded (g, bar.toFloat(), Dine::hair, 2.0f);
        const float phase = float (juce::Time::getMillisecondCounter() % 1400u) / 1400.0f;
        if (working && live)
        {
            // a sweep, because a bar frozen at full is indistinguishable from a run that stopped
            const float w = float (bar.getWidth()) * 0.22f;
            const float x = float (bar.getX()) + (float (bar.getWidth()) - w) * phase;
            Dine::fillRounded (g, juce::Rectangle<float> (x, float (bar.getY()), w, float (bar.getHeight())), Dine::accent, 2.0f);
        }
        else if (progress > 0.0f)
            Dine::fillRounded (g, bar.toFloat().withWidth (juce::jmax (4.0f, float (bar.getWidth()) * progress)), Dine::accent, 2.0f);
        r.removeFromTop (18);

        // The steps: the run's real ones, lit as it passes them.
        auto steps = r.removeFromTop (16);
        if (live)
        {
            const int at = liveStepFor (liveState);
            const auto font = Dine::caps (11.0f, 0.08f, 500);
            int total = 0;
            for (int i = 0; i < kNumLiveSteps; ++i) total += Dine::textWidth (font, juce::String (kLiveSteps[i].label).toUpperCase()) + 16;
            auto row = steps.withSizeKeepingCentre (juce::jmin (total, steps.getWidth()), steps.getHeight());
            for (int i = 0; i < kNumLiveSteps; ++i)
            {
                const auto label = juce::String (kLiveSteps[i].label).toUpperCase();
                const int w = Dine::textWidth (font, label) + 16;
                g.setColour (at > i ? Dine::accent : at == i ? Dine::ink : Dine::ink4);
                g.setFont (font);
                Dine::drawText (g, label, row.removeFromLeft (w), juce::Justification::centred);
            }
        }
        else
        {
            const char* labels[4] = { "LISTEN", "ANALYSE", "PLAN", "VERIFY" };
            const int phaseIndex = planning ? 2 : waiting ? -1 : 0;
            const auto font = Dine::caps (11.0f, 0.08f, 500);
            auto row = steps.withSizeKeepingCentre (juce::jmin (steps.getWidth(), 4 * 84), steps.getHeight());
            for (int i = 0; i < 4; ++i)
            {
                g.setColour (i <= phaseIndex ? Dine::accent : Dine::ink4);
                g.setFont (font);
                Dine::drawText (g, labels[i], row.removeFromLeft (84), juce::Justification::centred);
            }
        }
    }

    void resized() override
    {
        auto card = sheetBounds();
        const int w = juce::jmax (120, cancel.idealWidth());
        cancel.setBounds (juce::Rectangle<int> (w, Dine::Metric::button).withCentre ({ card.getCentreX(), card.getBottom() - 34 - 16 }));
        cancel.setButtonText (controller.isTuningLive() ? "Stop" : "Stop listening");
    }

private:
    MixController& controller;
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
        before.onClick = [this] { controller.setCompare (MixController::Compare::Before); refresh(); if (page.onToast) page.onToast ("Auditioning BEFORE. Nothing is committed by listening."); };
        after.onClick  = [this] { controller.setCompare (MixController::Compare::After); refresh(); if (page.onToast) page.onToast ("Auditioning AFTER."); };
        keep.onClick   = [this] { controller.keepPlan(); if (page.onToast) page.onToast ("Kept. Every value it set is marked TUNED BY DLIVE and can be reverted stage by stage."); };
        revert.onClick = [this] { controller.revertPlan(); if (page.onToast) page.onToast ("Reverted to the mix you had before this run."); };
        another.onClick = [this] { controller.tryAnotherMix(); };
        review.onClick = [this] { if (page.onOpenAdvanced) page.onOpenAdvanced(); };
        closeButton.onClick = [this]
        {
            controller.revertPlan();
            if (page.onToast) page.onToast ("Closed without keeping: the mix is as it was. TUNE MIX again to propose it again.");
        };
        review.setFontPx (12.5f);
        closeButton.setFontPx (13.0f);
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

    struct Bullet { juce::String what, why; bool done = true; };

    static constexpr int kPadX = 28, kPadY = 28;

    int bulletWidth() const { return juce::jmin (900, getWidth() - 80) - kPadX * 2 - 150 - 18 - 18; }

    static int linesNeeded (const juce::Font& font, const juce::String& text, int width)
    {
        if (text.isEmpty() || width < 40) return 1;
        juce::AttributedString attributed;
        attributed.setText (text);
        attributed.setFont (font);
        attributed.setJustification (juce::Justification::topLeft);
        juce::TextLayout layout;
        layout.createLayout (attributed, float (width));
        return juce::jlimit (1, 5, int (std::ceil (layout.getHeight() / juce::jmax (1.0f, font.getHeight()) - 0.05f)));
    }

    int bulletHeight (const Bullet& b) const
    {
        const int avail = juce::jmax (80, bulletWidth());
        const int whyLines = b.why.isEmpty() ? 1 : linesNeeded (Dine::text (13.0f), b.why, avail);
        const int whatLines = linesNeeded (Dine::text (13.0f), b.what, 150);
        return 16 + juce::jmax (whatLines * 17 + 2, whyLines * 20) + 16;
    }

    int listHeight() const
    {
        int h = 0;
        for (const auto& b : bullets()) h += bulletHeight (b);
        return h;
    }

    static constexpr int kChipH = 26;
    int chipRowHeight() const { return showChips() ? kChipH + 12 : 0; }

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (900, getWidth() - 80);
        const int content = kPadY + 28 + 20 + Dine::Metric::button + 10 + chipRowHeight() + listHeight() + kPadY;
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
                if (line.kind == Kind::NotPossible) why = why.isEmpty() ? "DLIVE cannot do this, so it did not." : why;
                if (line.kind == Kind::Refused && why.isEmpty()) why = "DLIVE declined this change.";
                out.push_back ({ juce::String (line.what), why,
                                 line.kind != Kind::NotPossible && line.kind != Kind::Refused });
                if (out.size() >= 6) return out;
            }
            if (! out.empty()) return out;
        }
        for (const auto& n : plan->notes)
        {
            out.push_back ({ juce::String (n), {}, true });
            if (out.size() >= 2) break;
        }
        for (const auto& rel : plan->relationships)
        {
            if (rel.changes.empty() && rel.kind != Recommendation::Kind::MixGain) continue;
            out.push_back ({ juce::String (rel.what), juce::String (rel.why), true });
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
        auto head = r.removeFromTop (28);
        head.removeFromRight (closeButton.getWidth() + 10);
        const bool live = controller.getTuneLive().getState() == TuneLiveCoordinator::State::Ready;
        const juce::String title = juce::String (live ? "TUNE LIVE MIX" : controller.isTuningChannel() ? "TUNE CHANNEL" : "TUNE MIX") + " is ready";
        const auto titleFont = Dine::text (22.0f);
        g.setColour (Dine::ink);
        g.setFont (titleFont);
        Dine::drawText (g, title, head.removeFromLeft (Dine::textWidth (titleFont, title)), juce::Justification::centredLeft);
        head.removeFromLeft (14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, plan->noChangeRequired ? juce::String (plan->headline)
                                           : "Heard " + juce::String (plan->stripsHeard) + " inputs, proposed "
                                                 + juce::String (plan->parametersChanged) + " settings and "
                                                 + juce::String (plan->fadersChanged) + " levels. " + juce::String (plan->headline),
                    head, juce::Justification::centredLeft, true);

        r.removeFromTop (20 + Dine::Metric::button + 10);
        if (showChips())
        {
            auto row = r.removeFromTop (kChipH);
            g.setColour (everythingPicked() ? Dine::ink3 : Dine::accent);
            g.setFont (Dine::caps (10.0f, 0.08f));
            Dine::drawText (g, everythingPicked() ? "KEEP" : "KEEPING", row.removeFromLeft (kKeepLabelW), juce::Justification::centredLeft);
            r.removeFromTop (12);
        }
        for (const auto& b : bullets())
        {
            const int h = bulletHeight (b);
            if (r.getHeight() < h) break;
            auto row = r.removeFromTop (h);
            Dine::drawRule (g, row.withHeight (1), Dine::hairSoft);
            row = row.reduced (0, 16);
            auto left = row.removeFromLeft (150);
            g.setColour (b.done ? Dine::ink : Dine::ink3);
            g.setFont (Dine::text (13.0f));
            Dine::drawFittedText (g, b.what, left, juce::Justification::topLeft, 5, 1.0f);
            row.removeFromLeft (18);
            if (! b.done)
            {
                auto tag = row.removeFromRight (90);
                Dine::drawStatusChip (g, tag.withHeight (17).toFloat(), "NOT DONE", Dine::warn);
                row.removeFromRight (12);
            }
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (13.0f));
            Dine::drawFittedText (g, b.why.isEmpty() ? juce::String ("Applied.") : b.why, row, juce::Justification::topLeft, 5, 1.0f);
        }
    }

    void resized() override
    {
        auto card = sheetBounds();
        auto r = card.reduced (kPadX, kPadY);
        closeButton.setBounds (r.removeFromTop (28).removeFromRight (juce::jmax (56, closeButton.idealWidth())));
        r.removeFromTop (20);
        auto row = r.removeFromTop (Dine::Metric::button);
        before.setBounds (row.removeFromLeft (juce::jmax (80, before.idealWidth())));
        row.removeFromLeft (10);
        after.setBounds (row.removeFromLeft (juce::jmax (80, after.idealWidth())));
        row.removeFromLeft (10);
        review.setBounds (row.removeFromLeft (juce::jmax (90, review.idealWidth())));
        keep.setBounds (row.removeFromRight (juce::jmax (80, keep.idealWidth())));
        row.removeFromRight (10);
        revert.setBounds (row.removeFromRight (juce::jmax (90, revert.idealWidth())));
        row.removeFromRight (10);
        another.setBounds (row.removeFromRight (juce::jmax (120, another.idealWidth())));

        if (showChips())
        {
            r.removeFromTop (10);
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
    DineButton before { "Before", DineButton::Style::Standard }, after { "After", DineButton::Style::Filled };
    DineButton keep { "Keep", DineButton::Style::Filled }, revert { "Revert", DineButton::Style::Standard };
    DineButton another { "Try another mix", DineButton::Style::Standard };
    DineButton review { "Review in the Inspector", DineButton::Style::Ghost };
    DineButton closeButton { "Close", DineButton::Style::Ghost };
};

// ------------------------------------------------------------------ MixPage
MixPage::MixPage (MixController& c) : controller (c)
{
    for (int i = 0; i < kGroupTiles; ++i)
    {
        groups[size_t (i)] = std::make_unique<GroupTile> (controller, i);
        if (i < kGroupBuses)
            groups[size_t (i)]->onTune = [this, i]
            {
                controller.startTuneBus (MixBus (i));
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
    sideTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Right, "Mix health");
    sideTab->onClick = [this] { setSideShown (! sideShown); };
    addAndMakeVisible (*sideTab);

    listenSheet = std::make_unique<ListenSheet> (controller);
    resultSheet = std::make_unique<ResultSheet> (controller, *this);
    referenceSheet = std::make_unique<ReferenceSheet> (controller);
    for (auto* b : { &tuneButton, &liveTuneButton, &referenceButton, &chatButton, &undoButton, &redoButton, &historyButton, &advancedButton })
        side->addAndMakeVisible (*b);
    addAndMakeVisible (resetMacrosButton);
    addChildComponent (*referenceSheet);
    addChildComponent (*resultSheet);
    addChildComponent (*listenSheet);

    referenceSheet->onClose = [this] { referenceSheet->setVisible (false); refreshTuneButton(); repaint(); };
    referenceSheet->onToast = [this] (const juce::String& t) { if (onToast) onToast (t); };

    tuneButton.setCaps (true);
    tuneButton.setFontPx (13.0f);
    tuneButton.onClick = [this] { pressTune(); };
    tuneButton.setTooltip ("Listen to the band and build DLIVE's mix from what it measures. Deterministic: the same "
                           "listen always gives the same mix, and nothing leaves this machine.");
    liveTuneButton.setCaps (true);
    liveTuneButton.setFontPx (13.0f);
    liveTuneButton.onClick = [this] { pressLiveTune(); };
    liveTuneButton.setTooltip ("The same listen, with a mix engineer's reasoning on top: DLIVE builds its mix, works out "
                               "what this band still needs, applies only what it can do safely, then listens again to "
                               "check. You can compare, review every change and revert.");
    referenceButton.setFontPx (12.5f);
    referenceButton.onClick = [this] { openReference(); };
    referenceButton.setTooltip ("Aim the mix at a finished recording: DLIVE matches the master's tone, image and density "
                                "to it. How loud the stream is delivered, and who is loud in the mix, are not copied.");
    chatButton.setFontPx (12.5f);
    chatButton.onClick = [this] { if (onOpenChat) onOpenChat(); };
    chatButton.setTooltip ("Ask for a change in plain words. DLIVE says what it intends to do before anything is yours.");
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
    railView.setVisible (available && railShown);
    resized();
    repaint();
}

void MixPage::setRailShown (bool shown)
{
    if (shown == railShown) return;
    railShown = shown;
    railTab->setCollapsed (! shown);
    railView.setVisible (shown);
    resized();
    repaint();
}

void MixPage::setSideShown (bool shown)
{
    if (shown == sideShown) return;
    sideShown = shown;
    sideTab->setCollapsed (! shown);
    sideView.setVisible (shown);
    resized();
    repaint();
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

void MixPage::pressTune()
{
    if (controller.isListening() || controller.isTuningLive()) { controller.abortTuneMix(); refreshTuneButton(); return; }
    controller.startTuneMix();
    refreshTuneButton();
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

    tuneButton.setButtonText (busy && ! live ? "Cancel" : controller.getTuneCount() > 0 ? "Re-tune" : "Tune mix");
    tuneButton.setEnabled (ready && ! live && stage != MixController::Stage::Planning);
    liveTuneButton.setButtonText (live ? "Stop" : controller.getTuneCount() > 0 ? "Re-tune live" : "Tune live mix");
    liveTuneButton.setEnabled (ready && (live || stage != MixController::Stage::Listening) && stage != MixController::Stage::Planning);
    referenceButton.setButtonText (controller.hasReference() ? "Reference " + juce::String (Glyph::check()) : "Reference");
    referenceButton.setEnabled (ready && ! busy);
    chatButton.setEnabled (ready);
    resized();
}

void MixPage::rebuildRail()
{
    inputRows.clear();
    railHolder.removeAllChildren();
    if (! controller.isPrepared()) { builtRailFor = -1; return; }
    const auto& graph = controller.getGraph();
    for (int i = 0; i < graph.numStrips(); ++i)
    {
        const auto& s = graph.strips[size_t (i)];
        auto row = std::make_unique<InputRow> (s.name, s.role, s.inputA + 1, s.icon);
        row->onTune = [this, i] { selectRow (i); if (onTuneStrip) onTuneStrip (i); };
        row->onSelect = [this, i] { selectRow (i); };
        row->onFocus = [this, i] { controller.setFocusInput (i); rebuildRail(); };
        railHolder.addAndMakeVisible (*row);
        inputRows.push_back (std::move (row));
    }
    builtRailFor = graph.numStrips();
    if (selectedRow >= builtRailFor) selectedRow = -1;
    resized();
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
        const MixBus bus = MixBus (i);
        const bool used = controller.isPrepared() && engine.isBusUsed (bus);
        const auto& m = engine.getBus (bus).getOutputMeter();
        groups[size_t (i)]->set (used, used ? m.consumeMaxPeakDb() : -120.0f, used ? m.getMaxRmsDb() : -120.0f, used && m.hasClipped(),
                                 kept.buses[size_t (bus)].mute, kept.buses[size_t (bus)].faderDb,
                                 ! used ? 0 : listening ? (controller.busHeard (bus) ? 2 : 1) : 0);
    }
    {
        int returns = 0; float peak = -120.0f, rms = -120.0f; bool clip = false;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (controller.isPrepared() && engine.isFxUsed (FxSlot (f)))
            {
                ++returns;
                const auto& m = engine.getFx (FxSlot (f)).getOutputMeter();
                peak = juce::jmax (peak, m.consumeMaxPeakDb()); rms = juce::jmax (rms, m.getMaxRmsDb()); clip = clip || m.hasClipped();
            }
        groups[size_t (kGroupBuses)]->set (returns > 0, peak, rms, clip, kept.fxMute, kept.fxReturnDb, 0);
    }

    // ---- the input rail: the faint / muted marks, re-read a few times a second
    if (controller.isPrepared() && graph.numStrips() != builtRailFor) rebuildRail();
    if (controller.isPrepared() && (adviceTicks++ % 5) == 0)
    {
        const auto* plan = controller.getPlan();
        for (int i = 0; i < int (inputRows.size()) && i < graph.numStrips(); ++i)
        {
            const bool faint = plan != nullptr && i < int (plan->strips.size()) && plan->strips[size_t (i)].faint;
            inputRows[size_t (i)]->set (i < kept.numStrips && kept.strips[size_t (i)].mute, faint, i == selectedRow,
                                        i == controller.getFocusInput());
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
    loudnessTargetButton.setValue ((delivery == DeliveryLoudness::FromPurpose ? juce::String ("From the purpose")
                                                                              : juce::String (deliveryLoudnessName (delivery)))
                                   + "  " + Glyph::dot() + "  " + lufs (target));
    voicingButton.setValue (controller.getVoicing() == MasterVoicing::Neutral ? juce::String ("Sound: as tuned")
                                                                              : "Sound: " + juce::String (masterVoicingName (controller.getVoicing())));
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
    masterNote = known ? "NOW  " + juce::String (loud.integratedLufs, 1) + "     TARGET  " + juce::String (loud.targetLufs, 1) : juce::String();
}

MixPage::Layout MixPage::layout() const
{
    Layout l;
    auto b = getLocalBounds();
    l.rail = b.removeFromLeft (railWidth());
    l.railTab = railShown ? l.rail.removeFromRight (0) : l.rail;
    l.side = b.removeFromRight (sideWidth());
    l.sideTab = sideShown ? l.side.withHeight (36).removeFromRight (30) : l.side;
    auto main = b.reduced (24, 22);

    // The middle column never scrolls. The groups take what is left after the master row and
    // the pads, down to a floor of 150; when even that does not fit, the pads drop their snap
    // rows first. What is spare goes to the pads (up to their 214) before the groups.
    const int fixed = (12 + 12) + 20 + (12 + 8 + Dine::Metric::control) + 20 + (12 + 12) + kRibbonGap + MacroRibbon::kHeight;
    const int groupsFloor = 150;
    l.compact = main.getHeight() < fixed + MacroPad::heightFor (MacroPad::kMinPad, false) + groupsFloor;
    const int padCap = juce::jmax (MacroPad::kMinPad, juce::jmin (MacroPad::kMaxPad, (main.getWidth() - 12) / 2));
    l.padSize = MacroPad::kMinPad;
    for (int size = padCap; size > MacroPad::kMinPad; size -= 2)
        if (main.getHeight() - fixed - MacroPad::heightFor (size, l.compact) >= groupsFloor) { l.padSize = size; break; }
    const int padsH = MacroPad::heightFor (l.padSize, l.compact);
    const int groupsH = juce::jlimit (groupsFloor, 320, main.getHeight() - fixed - padsH);

    l.groupsCaption = main.removeFromTop (12);
    main.removeFromTop (12);
    l.groups = main.removeFromTop (groupsH);
    main.removeFromTop (20);
    // MASTER: a caption, then one row of the target, the lift and the sound, with its sentence beside them.
    l.master = main.removeFromTop (12 + 8 + Dine::Metric::control);
    main.removeFromTop (20);
    l.macrosCaption = main.removeFromTop (12);
    main.removeFromTop (12);
    // The two pads and the ribbon are one block, centred in the column rather than parked at its left.
    auto padRow = main.removeFromTop (padsH);
    const int compW = juce::jmin (l.padSize + 40, (padRow.getWidth() - 24) / 2);
    const int block = compW * 2 + 24;
    padRow = padRow.withSizeKeepingCentre (block, padRow.getHeight());
    l.pads[0] = padRow.removeFromLeft (compW);
    padRow.removeFromLeft (24);
    l.pads[1] = padRow.removeFromLeft (compW);
    main.removeFromTop (kRibbonGap);
    l.ribbon = main.removeFromTop (MacroRibbon::kHeight).withSizeKeepingCentre (block, MacroRibbon::kHeight);
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

    Dine::drawSection (g, l.groupsCaption, "GROUPS");

    // ---- MASTER: the sentence beside the controls says what the lift would do, or why not
    {
        auto r = l.master;
        Dine::drawSection (g, r.removeFromTop (12), "MASTER");
        r.removeFromTop (8);
        const int used = loudnessTargetButton.getRight() > 0 ? voicingButton.getRight() - r.getX() : 0;
        auto note = r.withTrimmedLeft (used + 16);
        if (note.getWidth() > 160 && masterNote.isNotEmpty())
        {
            g.setColour (raisePossible ? Dine::ink2 : Dine::ink3);
            g.setFont (Dine::mono (11.5f, 500));
            Dine::drawText (g, masterNote, note, juce::Justification::centredRight, true);
        }
    }

    Dine::drawSection (g, l.macrosCaption, "MACROS");

    // ---- the input rail
    if (! railAvailable) return;
    g.setColour (Dine::rail);
    g.fillRect (l.rail.getUnion (l.railTab));
    g.setColour (Dine::hair);
    g.fillRect (l.rail.getUnion (l.railTab).removeFromRight (1));   // the seam against the middle
    if (! railShown) return;
    auto head = l.rail.withHeight (36).reduced (14, 0).withTrimmedTop (12);
    Dine::drawSection (g, head.withTrimmedRight (20), "INPUTS");

    if (inputRows.empty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, "Assign inputs to see them here.", l.rail.reduced (14, 50).removeFromTop (40), juce::Justification::topLeft, 2);
    }

    int faintCount = 0;
    juce::String faintNames;
    for (const auto& r : inputRows)
        if (r->faint) { ++faintCount; faintNames += (faintNames.isEmpty() ? "" : ", ") + r->name; }
    if (faintCount > 0)
    {
        auto box = l.rail.reduced (10, 0).removeFromBottom (92).withTrimmedBottom (10);
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

void MixPage::resized()
{
    const auto l = layout();

    // ---- the right panel
    sideTab->setBounds (l.sideTab);
    sideView.setBounds (sideShown ? l.side.withTrimmedTop (36) : juce::Rectangle<int>());
    layoutSide();

    auto groupRow = l.groups;
    const int gap = 10;
    const int w = (groupRow.getWidth() - gap * (kGroupTiles - 1)) / kGroupTiles;
    for (auto& t : groups) { t->setBounds (groupRow.removeFromLeft (w)); groupRow.removeFromLeft (gap); }

    {
        auto row = l.master.withTrimmedTop (12 + 8);
        loudnessTargetButton.setBounds (row.removeFromLeft (juce::jmin (row.getWidth() / 3, juce::jmax (150, loudnessTargetButton.idealWidth()))));
        row.removeFromLeft (8);
        raiseButton.setBounds (row.removeFromLeft (juce::jmax (120, raiseButton.idealWidth())));
        row.removeFromLeft (8);
        voicingButton.setBounds (row.removeFromLeft (juce::jmin (row.getWidth() / 2, juce::jmax (140, voicingButton.idealWidth()))));
    }

    {
        const int w = juce::jmax (90, resetMacrosButton.idealWidth());
        resetMacrosButton.setBounds (l.macrosCaption.withTrimmedLeft (l.macrosCaption.getWidth() - w).withSizeKeepingCentre (w, 22));
        for (size_t i = 0; i < pads.size(); ++i)
        {
            pads[i]->setCompact (l.compact);
            pads[i]->setPadSize (l.padSize);
            pads[i]->setBounds (l.pads[i]);
        }
        ribbon->setBounds (l.ribbon);
    }

    // ---- rail
    if (railShown) railTab->setBounds (l.rail.withHeight (36).removeFromRight (30));
    else railTab->setBounds (l.railTab);
    int faintCount = 0;
    for (const auto& r : inputRows) if (r->faint) ++faintCount;
    auto rail = l.rail.withTrimmedTop (36);
    if (faintCount > 0) rail.removeFromBottom (92);
    railView.setBounds (rail);
    const int total = int (inputRows.size()) * 32;
    railHolder.setSize (rail.getWidth() - (total > rail.getHeight() ? 10 : 0), juce::jmax (total, rail.getHeight()));
    int y = 0;
    for (auto& r : inputRows) { r->setBounds (0, y, railHolder.getWidth(), 32); y += 32; }

    listenSheet->setBounds (getLocalBounds());
    resultSheet->setBounds (getLocalBounds());
    referenceSheet->setBounds (getLocalBounds());
}

} // namespace livemix
