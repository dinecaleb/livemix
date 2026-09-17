#include "MixPage.h"
#include "ReferenceSheet.h"
#include "Core/DbUtils.h"

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
    constexpr int kMacroRowH = 40;

    juce::String dbText (float db)
    {
        if (db <= -119.0f) return Glyph::dash();
        return (db >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (db), 1);
    }
}

// ------------------------------------------------------------------ GroupTile
// One group bus: its name in its colour, a fader and a meter side by side, and its level.
class MixPage::GroupTile : public juce::Component
{
public:
    GroupTile (MixController& c, int index) : controller (c), group (index)
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
        bool body = used != isUsed || heard != heardState || muted != isMuted;
        used = isUsed; heard = heardState; muted = isMuted;
        meter.setLevels (peakDb, holdDb, clipped);
        meter.setMuted (isMuted || ! used);
        if (std::fabs (faderDb - float (fader.getValue())) > 0.01f) { fader.setValue (faderDb, juce::dontSendNotification); repaint (readout); }
        fader.setEnabled (used);
        updating = false;
        if (body) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::tile, Dine::Radius::card);
        auto r = getLocalBounds().reduced (10, 14);
        g.setColour (used ? groupColour (group) : Dine::ink4);
        g.setFont (Dine::caps (11.0f, 0.06f, 500));
        g.drawText (groupName (group), r.removeFromTop (14), juce::Justification::centred);
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
        g.drawText (! used ? "not in this mix" : muted ? "NOT HEARD" : dbText (float (fader.getValue())), readout, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 14);
        r.removeFromTop (14 + 10);
        readout = r.removeFromBottom (14);
        r.removeFromBottom (10);
        auto pair = r.withSizeKeepingCentre (20 + 6 + 7, r.getHeight());
        fader.setBounds (pair.removeFromLeft (20));
        pair.removeFromLeft (6);
        meter.setBounds (pair);
    }

private:
    bool isFx() const noexcept { return group >= kGroupBuses; }

    MixController& controller;
    int group;
    bool used = false, muted = false, updating = false;
    int heard = 0;
    juce::Rectangle<int> readout;
    DineMeter meter { DineMeter::Style::Bar };
    juce::Slider fader;
};

// ------------------------------------------------------------------ MacroSlider
class MixPage::MacroSlider : public juce::Component
{
public:
    MacroSlider (MixMacro m, std::function<void (float)> onChange) : macro (m), changed (std::move (onChange))
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        Dine::dragOnly (slider);
        slider.setRange (0.0, 100.0, 1.0);
        slider.setValue (50.0, juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, 50.0);
        slider.setTooltip (juce::String (MixMacros::tooltip (m)) + "  " + Glyph::dot() + "  "
                           + MixMacros::lowLabel (m) + " at 0, " + MixMacros::highLabel (m) + " at 100, the plan at 50.");
        slider.onValueChange = [this] { if (changed) changed (float (slider.getValue())); repaint (top); };
        DineLookAndFeel::setBipolar (slider, true);
        addAndMakeVisible (slider);
    }
    void setValue (float v)
    {
        if (std::fabs (v - float (slider.getValue())) < 0.01f) return;
        slider.setValue (v, juce::dontSendNotification);
        repaint (top);
    }

    void paint (juce::Graphics& g) override
    {
        const juce::String label = juce::String (MixMacros::name (macro));
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 500));
        g.drawText (label.substring (0, 1) + label.substring (1).toLowerCase(), top, juce::Justification::centredLeft);
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        g.drawText (juce::String (int (slider.getValue())), top, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        top = r.removeFromTop (18);
        r.removeFromTop (7);
        slider.setBounds (r.removeFromTop (14));
    }

private:
    MixMacro macro;
    juce::Slider slider;
    juce::Rectangle<int> top;
    std::function<void (float)> changed;
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
        setTooltip ("Click to pick " + name + " out. TUNE CHANNEL listens to it on its own - nothing else in the mix moves.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onTune, onSelect;

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition())) return;
        if (e.getPosition().x >= verbRect.getX() - 6) { if (onTune) onTune(); }
        else if (onSelect) onSelect();
    }

    void set (bool isMuted, bool isFaint, bool isSelected)
    {
        if (muted != isMuted || faint != isFaint || selected != isSelected)
        {
            muted = isMuted; faint = isFaint; selected = isSelected; repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        if (selected)   g.fillAll (Dine::card);
        else if (hover) g.fillAll (Dine::tile);
        juce::ignoreUnused (b);

        auto r = getLocalBounds().reduced (14, 0);
        const auto verbFont = Dine::caps (10.0f, 0.06f);
        const int verbW = Dine::textWidth (verbFont, "TUNE CHANNEL");
        verbRect = r.removeFromRight (verbW);
        g.setColour (Dine::accent);
        g.setFont (verbFont);
        g.drawText ("TUNE CHANNEL", verbRect, juce::Justification::centredRight);
        r.removeFromRight (8);

        if (muted || faint)
        {
            g.setColour (muted ? Dine::warn : Dine::crit);
            g.fillEllipse (r.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            r.removeFromLeft (6);
        }
        g.setColour (selected ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (12.5f, selected ? 600 : 400));
        g.drawText (name, r, juce::Justification::centredLeft, true);
    }

    juce::String name;
    Dine::Icon icon;
    int num = 0;
    bool muted = false, faint = false, hover = false, selected = false;
    juce::Rectangle<int> verbRect;
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
        g.drawText (verb + (working ? " IS WORKING" : waiting ? " IS WAITING" : " IS LISTENING"), r.removeFromTop (14), juce::Justification::centred);
        r.removeFromTop (12);

        // The number: seconds left in a listen, per cent through the work.
        const float seconds = controller.isTuningChannel() ? MixController::channelListen().seconds : 30.0f;
        const juce::String big = waiting ? juce::String (Glyph::dash())
                               : working ? juce::String (juce::jmin (99, int (std::round (progress * 100.0f)))) + "%"
                                         : juce::String (juce::jmax (0, int (std::ceil (seconds * (1.0f - progress))))) + " s";
        g.setColour (Dine::ink);
        g.setFont (Dine::mono (52.0f, 500));
        g.drawText (big, r.removeFromTop (58), juce::Justification::centred);
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
        g.drawFittedText (hearing, r.removeFromTop (40), juce::Justification::centredTop, 2);
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
                g.drawText (label, row.removeFromLeft (w), juce::Justification::centred);
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
                g.drawText (labels[i], row.removeFromLeft (84), juce::Justification::centred);
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
        closeButton.onClick = [this] { controller.revertPlan(); };
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
        resized();
        repaint();
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

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (900, getWidth() - 80);
        const int content = kPadY + 28 + 20 + Dine::Metric::button + 10 + listHeight() + kPadY;
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
        g.drawText (title, head.removeFromLeft (Dine::textWidth (titleFont, title)), juce::Justification::centredLeft);
        head.removeFromLeft (14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawText (plan->noChangeRequired ? juce::String (plan->headline)
                                           : "Heard " + juce::String (plan->stripsHeard) + " inputs, proposed "
                                                 + juce::String (plan->parametersChanged) + " settings and "
                                                 + juce::String (plan->fadersChanged) + " levels. " + juce::String (plan->headline),
                    head, juce::Justification::centredLeft, true);

        r.removeFromTop (20 + Dine::Metric::button + 10);
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
            g.drawFittedText (b.what, left, juce::Justification::topLeft, 5, 1.0f);
            row.removeFromLeft (18);
            if (! b.done)
            {
                auto tag = row.removeFromRight (90);
                Dine::drawStatusChip (g, tag.withHeight (17).toFloat(), "NOT DONE", Dine::warn);
                row.removeFromRight (12);
            }
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (13.0f));
            g.drawFittedText (b.why.isEmpty() ? juce::String ("Applied.") : b.why, row, juce::Justification::topLeft, 5, 1.0f);
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
    }

private:
    MixController& controller;
    MixPage& page;
    DineButton before { "Before", DineButton::Style::Standard }, after { "After", DineButton::Style::Filled };
    DineButton keep { "Keep", DineButton::Style::Filled }, revert { "Revert", DineButton::Style::Standard };
    DineButton another { "Try another mix", DineButton::Style::Standard };
    DineButton review { "Review in the Inspector", DineButton::Style::Ghost };
    DineButton closeButton { "Close", DineButton::Style::Ghost };
};

// ------------------------------------------------------------------ MixPage
MixPage::MixPage (MixController& c) : controller (c)
{
    for (int i = 0; i < kGroupTiles; ++i) { groups[size_t (i)] = std::make_unique<GroupTile> (controller, i); addAndMakeVisible (*groups[size_t (i)]); }
    for (int i = 0; i < int (MixMacro::Count); ++i)
    {
        const auto m = MixMacro (i);
        macros[size_t (i)] = std::make_unique<MacroSlider> (m, [this, m] (float v) { controller.setMacro (m, v); });
        addAndMakeVisible (*macros[size_t (i)]);
    }
    railView.setViewedComponent (&railHolder, false);
    Dine::nativeScrolling (railView);
    railView.setScrollBarsShown (true, false);
    addAndMakeVisible (railView);

    railTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Left, "Inputs");
    railTab->onClick = [this] { setRailShown (! railShown); };
    addAndMakeVisible (*railTab);

    listenSheet = std::make_unique<ListenSheet> (controller);
    resultSheet = std::make_unique<ResultSheet> (controller, *this);
    referenceSheet = std::make_unique<ReferenceSheet> (controller);
    for (auto* b : { &tuneButton, &liveTuneButton, &referenceButton, &chatButton, &undoButton, &redoButton, &advancedButton, &resetMacrosButton })
        addAndMakeVisible (*b);
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
    resetMacrosButton.onClick = [this] { controller.resetMacros(); for (int i = 0; i < int (MixMacro::Count); ++i) macros[size_t (i)]->setValue (50.0f); };
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
    macros[size_t (m)]->setValue (v);
    controller.setMacro (m, v);
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
            inputRows[size_t (i)]->set (i < kept.numStrips && kept.strips[size_t (i)].mute, faint, i == selectedRow);
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
    for (int i = 0; i < int (MixMacro::Count); ++i) macros[size_t (i)]->setValue (controller.getMacros().get (MixMacro (i)));

    juce::String notes;
    for (const auto& n : controller.getMixHealthNotes()) notes << juce::String (n) << "|";
    const PageLook now { status, notes, health, stage, controller.getTuneCount(), controller.hasReference(),
                         controller.canUndoMix(), controller.canRedoMix(),
                         controller.hasReference() ? juce::String (controller.getReference().name) : juce::String() };
    if (now != painted)
    {
        painted = now;
        undoButton.setEnabled (now.canUndo);
        redoButton.setEnabled (now.canRedo);
        repaint();
    }
}

MixPage::Layout MixPage::layout() const
{
    Layout l;
    auto b = getLocalBounds();
    l.rail = b.removeFromLeft (railWidth());
    l.railTab = railShown ? l.rail.removeFromRight (0) : l.rail;
    auto main = b.reduced (24, 22);
    // MIX HEALTH beside the 230 px column of verbs
    auto top = main.removeFromTop (juce::jmin (230, juce::jmax (150, main.getHeight() / 3)));
    l.actions = top.removeFromRight (230);
    top.removeFromRight (22);
    l.health = top;
    main.removeFromTop (22);
    // The macros are as tall as they are; the groups take what is left, down to a floor.
    const int macrosH = 12 + 12 + 18 + 7 + 14;
    l.macros = main.removeFromBottom (macrosH);
    main.removeFromBottom (22);
    l.groupsCaption = main.removeFromTop (12);
    main.removeFromTop (12);
    l.groups = main.withHeight (juce::jlimit (150, kGroupsH + 40, main.getHeight()));
    return l;
}

void MixPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto l = layout();

    // ---- MIX HEALTH: the caption and the sentences under it
    {
        auto r = l.health;
        auto cap = r.removeFromTop (14);
        Dine::drawSection (g, cap, health > 0 ? "MIX HEALTH  " + juce::String (Glyph::dot()) + "  " + juce::String (health) + "%" : "MIX HEALTH");
        r.removeFromTop (12);
        const auto notes = controller.getMixHealthNotes();
        const auto font = Dine::text (13.0f);
        int shown = 0;
        for (const auto& n : notes)
        {
            if (shown >= 3 || r.getHeight() < 20) break;
            const juce::String text (n);
            const auto lower = text.toLowerCase();
            const bool bad = lower.contains ("clipping") || lower.contains ("barely") || lower.contains ("not heard");
            const bool watch = lower.contains ("preamp") || lower.contains ("digital");
            g.setColour (bad ? Dine::crit : watch ? Dine::warn : Dine::ink2);
            g.setFont (font);
            juce::AttributedString a; a.setText (text); a.setFont (font);
            juce::TextLayout tl; tl.createLayout (a, float (juce::jmin (620, r.getWidth())));
            const int h = juce::jmin (r.getHeight(), int (std::ceil (tl.getHeight())) + 2);
            g.drawFittedText (text, r.removeFromTop (h).withWidth (juce::jmin (620, r.getWidth())), juce::Justification::topLeft, 4, 1.0f);
            r.removeFromTop (10);
            ++shown;
        }
        if (r.getHeight() >= 18)
        {
            g.setColour (Dine::ink3);
            g.setFont (font);
            g.drawFittedText (status, r.withWidth (juce::jmin (620, r.getWidth())), juce::Justification::topLeft, 2, 1.0f);
        }
    }

    // ---- the stamp under the verbs
    {
        auto stamp = l.actions.withTrimmedTop (44 + 9 + 44 + 9 + Dine::Metric::button + 9 + Dine::Metric::button + 9).withHeight (16);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (12.5f));
        juce::String text = controller.getTuneCount() > 0 ? "Tuned " + juce::String (controller.getTuneCount()) + (controller.getTuneCount() == 1 ? " time" : " times")
                                                          + " this session" : juce::String ("Not tuned yet");
        if (controller.hasReference()) text += "  " + Glyph::dot() + "  aimed at " + juce::String (controller.getReference().name);
        g.drawText (text, stamp, juce::Justification::centredLeft, true);
    }

    Dine::drawSection (g, l.groupsCaption, "GROUPS");

    // ---- MACROS
    {
        auto r = l.macros;
        auto cap = r.removeFromTop (12);
        Dine::drawSection (g, cap, "MACROS  " + juce::String (Glyph::dot()) + "  50 IS THE PLAN");
    }

    // ---- the input rail
    if (! railAvailable) return;
    g.setColour (Dine::rail);
    g.fillRect (l.rail.getUnion (l.railTab));
    if (! railShown) return;
    auto head = l.rail.withHeight (36).reduced (14, 0).withTrimmedTop (12);
    Dine::drawSection (g, head.withTrimmedRight (20), "INPUTS");

    if (inputRows.empty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        g.drawFittedText ("Assign inputs to see them here.", l.rail.reduced (14, 50).removeFromTop (40), juce::Justification::topLeft, 2);
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
        g.drawText (faintCount == 1 ? "Check this input" : "Check these inputs", r.removeFromTop (16), juce::Justification::centredLeft);
        r.removeFromTop (4);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawFittedText (faintNames + " never rose above a whisper. Left where it is - a faint input is usually a mic that is off.",
                          r, juce::Justification::topLeft, 3);
    }
}

void MixPage::resized()
{
    const auto l = layout();

    auto col = l.actions;
    tuneButton.setBounds (col.removeFromTop (44));
    col.removeFromTop (9);
    liveTuneButton.setBounds (col.removeFromTop (44));
    col.removeFromTop (9);
    {
        auto row = col.removeFromTop (Dine::Metric::button);
        referenceButton.setBounds (row.removeFromLeft ((row.getWidth() - 9) / 2));
        row.removeFromLeft (9);
        chatButton.setBounds (row);
    }
    col.removeFromTop (9);
    {
        auto row = col.removeFromTop (Dine::Metric::button);
        undoButton.setBounds (row.removeFromLeft ((row.getWidth() - 9) / 2));
        row.removeFromLeft (9);
        redoButton.setBounds (row);
    }
    col.removeFromTop (9 + 16 + 6);
    advancedButton.setBounds (col.removeFromTop (Dine::Metric::control).withWidth (juce::jmax (120, advancedButton.idealWidth())));

    auto groupRow = l.groups;
    const int gap = 10;
    const int w = (groupRow.getWidth() - gap * (kGroupTiles - 1)) / kGroupTiles;
    for (auto& t : groups) { t->setBounds (groupRow.removeFromLeft (w)); groupRow.removeFromLeft (gap); }

    auto macroArea = l.macros;
    auto cap = macroArea.removeFromTop (12);
    resetMacrosButton.setBounds (cap.removeFromRight (juce::jmax (90, resetMacrosButton.idealWidth())).withSizeKeepingCentre (juce::jmax (90, resetMacrosButton.idealWidth()), 22));
    macroArea.removeFromTop (12);
    auto row = macroArea.removeFromTop (kMacroRowH);
    const int mgap = 14;
    const int mw = (row.getWidth() - mgap * (int (MixMacro::Count) - 1)) / int (MixMacro::Count);
    for (auto& m : macros) { m->setBounds (row.removeFromLeft (mw)); row.removeFromLeft (mgap); }

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
