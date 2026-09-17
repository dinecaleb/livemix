#include "MixPage.h"
#include "ReferenceSheet.h"
#include "Core/DbUtils.h"

namespace livemix
{

namespace
{
    // TUNE reads the mix as its groups: every group bus in console order, then the FX returns
    // as one more tile. Both counts come from MixBus, so a new group bus - SPEECH, when
    // speaking microphones were taken out of VOCALS - becomes a meter here without being
    // wired in by hand.
    constexpr int kGroupBuses = int (MixBus::Master);      // DRUMS BASS MUSIC VOCALS SPEECH
    constexpr int kGroupTiles = kGroupBuses + 1;           // ... and the returns

    const char* kGroupNames[kGroupTiles] = { "Drums", "Bass", "Music", "Vocals", "Speech", "FX" };
    const MixBus kGroupBus[kGroupBuses] = { MixBus::Drums, MixBus::Bass, MixBus::Music, MixBus::Vocals, MixBus::Speech };
    const Dine::Icon kGroupIcons[kGroupTiles] = { Dine::Icon::Drum, Dine::Icon::Guitar, Dine::Icon::Piano,
                                                  Dine::Icon::Mic, Dine::Icon::Speech, Dine::Icon::Fx };

    juce::Colour groupColour (int i) noexcept
    {
        return i >= 0 && i < kGroupBuses ? Dine::busTint (kGroupBus[i]) : Dine::ink2;
    }

    constexpr int kMacroRowH = 78;   // name, slider, the two end labels

    juce::String dbText (float db)
    {
        if (db <= -119.0f) return Glyph::dash();
        return (db >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (db), 1);
    }
}

// ------------------------------------------------------------------ GroupTile
class MixPage::GroupTile : public juce::Component
{
public:
    explicit GroupTile (int index) : group (index) { addAndMakeVisible (meter); }

    void set (bool isUsed, int stripCount, float peakDb, float holdDb, bool clipped, int heardState /*0 none, 1 listening no, 2 heard*/)
    {
        used = isUsed; strips = stripCount; heard = heardState;
        meter.setLevels (peakDb, holdDb, clipped);
        peak = meter.getPeakDb();
        meter.setVisible (used);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        Dine::drawCard (g, b, used ? Dine::card : Dine::card.withAlpha (0.55f), Dine::hair);

        auto r = getLocalBounds().reduced (12, 11);
        const bool tall = getHeight() > 150;
        if (! tall) r.removeFromRight (16 + 10);   // the meter sits beside the text
        auto top = r.removeFromTop (16);
        Dine::drawIcon (g, kGroupIcons[group], top.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                        used ? groupColour (group) : Dine::ink4);
        top.removeFromLeft (6);
        if (heard == 2)
            Dine::drawIcon (g, Dine::Icon::Check, top.removeFromRight (13).toFloat().withSizeKeepingCentre (13.0f, 13.0f), Dine::ok);
        else if (heard == 1)
        {
            g.setColour (Dine::warn.withAlpha (0.85f));
            g.fillEllipse (top.removeFromRight (13).withSizeKeepingCentre (6, 6).toFloat());
        }
        g.setColour (used ? Dine::ink : Dine::ink4);
        g.setFont (Dine::text (12.5f, 600));
        g.drawText (kGroupNames[group], top, juce::Justification::centredLeft, true);

        r.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        g.drawText (! used ? "not in this mix"
                           : group == kGroupBuses ? juce::String (strips) + (strips == 1 ? " return" : " returns")
                                                  : juce::String (strips) + (strips == 1 ? " input" : " inputs"),
                    r.removeFromTop (15), juce::Justification::topLeft, true);

        if (used)
        {
            g.setColour (peak >= -1.0f ? Dine::crit : Dine::ink);
            g.setFont (Dine::mono (15.0f, 500));
            g.drawText (dbText (peak), r.removeFromTop (juce::jmin (r.getHeight(), 26)).withTrimmedTop (6),
                        juce::Justification::topLeft);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (10.5f));
            g.drawText ("dBFS peak", r.removeFromTop (14), juce::Justification::topLeft);

            // The scale beside the meter. Five group meters can carry the numbers a bank of
            // twenty-four strips cannot: this is where "how loud is that" is actually read.
            if (tall && meter.isVisible())
            {
                const auto m = meter.getBounds();
                const int marks[] = { 0, -6, -12, -24, -36, -48 };
                g.setFont (Dine::mono (9.0f));
                for (int db : marks)
                {
                    const float y = float (m.getBottom()) - float (m.getHeight()) * DineMeter::norm (float (db));
                    g.setColour (juce::Colours::white.withAlpha (0.10f));
                    g.fillRect (float (m.getX()) - 4.0f, y - 0.5f, 4.0f, 1.0f);
                    g.setColour (Dine::ink4);
                    g.drawText (juce::String (-db), m.getX() - 34, juce::roundToInt (y) - 6, 26, 12,
                                juce::Justification::centredRight);
                }
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 11);
        if (getHeight() > 150)
        {
            // A tall tile reads like a console meter: the numbers on top, then a meter wide
            // enough to be read across the room, with its scale beside it.
            r.removeFromTop (16 + 4 + 15 + 26 + 14 + 8);
            const int w = juce::jlimit (22, 56, juce::roundToInt (float (r.getWidth()) * 0.40f));
            meter.setBounds (r.withSizeKeepingCentre (w, r.getHeight()).translated ((r.getWidth() - w) / 6, 0));
        }
        else meter.setBounds (r.removeFromRight (16));
    }

private:
    int group;
    bool used = false;
    int strips = 0, heard = 0;
    float peak = -120.0f;
    DineMeter meter { DineMeter::Style::Segments };
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
        slider.setTooltip (MixMacros::tooltip (m));
        slider.onValueChange = [this] { if (changed) changed (float (slider.getValue())); repaint(); };
        DineLookAndFeel::setBipolar (slider, true);
        addAndMakeVisible (slider);
    }
    void setValue (float v) { slider.setValue (v, juce::dontSendNotification); repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (16);
        const float v = float (slider.getValue());
        const bool centred = v == 50.0f;

        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.0f, 600));
        // Sentence case: the design keeps capitals for the product verbs only.
        const juce::String label = juce::String (MixMacros::name (macro));
        g.drawText (label.substring (0, 1) + label.substring (1).toLowerCase(),
                    top.removeFromLeft (top.getWidth() / 2), juce::Justification::centredLeft);

        g.setColour (centred ? Dine::ink3 : Dine::accent);
        g.setFont (Dine::mono (11.5f));
        g.drawText (centred ? juce::String ("as tuned")
                            : juce::String (v < 50.0f ? MixMacros::lowLabel (macro) : MixMacros::highLabel (macro)) + " "
                                  + juce::String (int (std::round (std::fabs (v - 50.0f) * 2.0f))) + "%",
                    top, juce::Justification::centredRight);

        auto labels = r.removeFromBottom (14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f));
        g.drawText (MixMacros::lowLabel (macro), labels, juce::Justification::centredLeft);
        g.drawText (MixMacros::highLabel (macro), labels, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (16);
        r.removeFromBottom (14);
        slider.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
    }

private:
    MixMacro macro;
    juce::Slider slider;
    std::function<void (float)> changed;
};

// ------------------------------------------------------------------ InputRow (the rail)
class MixPage::InputRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    InputRow (const juce::String& n, ChannelRole role, int number, const std::string& iconKey)
        : name (n), icon (Dine::iconFor (iconKey, role)), num (number), meter (DineMeter::Style::Bar)
    {
        addAndMakeVisible (meter);
        meter.setInterceptsMouseClicks (false, false);
        setTooltip ("Click to tune " + name + " on its own. Nothing else in the mix moves.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    // The rail is the list of everything the mix is made of, so it is also the shortest way
    // to tune one of them: click the input and DLIVE listens to that source alone.
    std::function<void()> onTune;

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition())) return;
        if (onTune) onTune();
    }

    void set (float peakDb, float holdDb, bool clipped, bool isMuted, bool isFaint)
    {
        meter.setLevels (peakDb, holdDb, clipped);
        const float shown = meter.getPeakDb();
        if (muted != isMuted || faint != isFaint || std::abs (shown - peak) > 0.4f)
        {
            muted = isMuted; faint = isFaint; peak = shown; repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        if (muted)      Dine::fillRounded (g, b, Dine::crit.withAlpha (0.10f), Dine::Radius::control);
        else if (faint) Dine::fillRounded (g, b, Dine::warn.withAlpha (0.10f), Dine::Radius::control);
        if (hover)      Dine::fillRounded (g, b, Dine::fillSoft, Dine::Radius::control);

        auto r = getLocalBounds().reduced (6, 0);
        Dine::drawIcon (g, icon, r.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                        muted ? Dine::crit : faint ? Dine::warn : Dine::glyph);
        r.removeFromLeft (9);

        const juce::String state = hover ? "TUNE" : muted ? "Muted" : faint ? "Faint" : dbText (peak);
        const juce::Colour stateColour = hover ? Dine::accent : muted ? Dine::crit : faint ? Dine::warn : Dine::ink3;
        auto right = r.removeFromRight (muted || faint || hover ? 42 : 34);
        g.setColour (stateColour);
        g.setFont (hover ? Dine::text (10.5f, 700).withExtraKerningFactor (0.08f)
                         : muted || faint ? Dine::text (10.5f, 600) : Dine::mono (10.5f));
        g.drawText (state, right, juce::Justification::centredRight);

        auto text = r.withTrimmedRight (6).removeFromTop (getHeight() - 12);
        g.setColour (muted ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (12.5f));
        g.drawText (juce::String (num) + "  " + name, text.withTrimmedTop (5), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 0);
        r.removeFromLeft (15 + 9);
        r.removeFromRight (40 + 6);
        meter.setBounds (r.removeFromBottom (9).withHeight (4));
    }

    juce::String name;
    Dine::Icon icon;
    int num = 0;
    float peak = -120.0f;
    bool muted = false, faint = false, hover = false;
    DineMeter meter;
};

// The steps of a TUNE LIVE MIX run, as the user sees them. One entry per piece of real
// work the state machine actually does - nothing here is a progress bar with no state behind
// it, and a step is only ticked once the machine has genuinely passed it.
namespace
{
    struct LiveStep { const char* label; TuneLiveCoordinator::State from; };

    constexpr LiveStep kLiveSteps[] = {
        { "Listening to the band",              TuneLiveCoordinator::State::CapturingInitial },
        { "Measuring every input",              TuneLiveCoordinator::State::AnalyzingInitial },
        { "Deciding what this mix needs",       TuneLiveCoordinator::State::WaitingForReasoning },
        { "Working out how, with what DLIVE has", TuneLiveCoordinator::State::Resolving },
        { "Checking every change is safe",      TuneLiveCoordinator::State::Validating },
        { "Applying the mix",                   TuneLiveCoordinator::State::Applying },
        { "Listening again to what it did",     TuneLiveCoordinator::State::CapturingVerify },
        { "Making the last corrections",        TuneLiveCoordinator::State::WaitingForRefinement },
    };
    constexpr int kNumLiveSteps = int (sizeof (kLiveSteps) / sizeof (kLiveSteps[0]));

    // Which step the run is on. States that are a continuation of a step (analysing the verify
    // listen, validating a refinement) report the step they belong to, so nothing flickers.
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
// A macOS sheet: it drops from under the toolbar while DLIVE listens.
class MixPage::ListenSheet : public juce::Component
{
public:
    explicit ListenSheet (MixController& c) : controller (c)
    {
        addAndMakeVisible (cancel);
        cancel.onClick = [this] { controller.abortTuneMix(); };
        setInterceptsMouseClicks (true, true);
    }

    // Called with the page's 30 Hz refresh. The sheet has to be able to say how long the
    // step it is on has been going: once the listen is finished the ring has no percentage
    // left to fill, and "working" that never changes is indistinguishable from "hung".
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

    // The card is as tall as what it holds: the head, a line per group bus, then the foot. It is
    // computed rather than typed, because the group buses decide it - when SPEECH was added, a
    // hard-coded height put the fifth line on top of the foot and the Cancel button.
    static constexpr int kPadX    = 26;
    static constexpr int kPadY    = 24;
    static constexpr int kHeadH   = 104;   // the capture ring, and the two lines beside it
    static constexpr int kHeadGap = 18;
    static constexpr int kRowH    = 32;
    static constexpr int kFootGap = 12;
    static constexpr int kFootH   = 26;

    // A live run lists the steps of its workflow instead of the group buses, and there are
    // more of them, so the card is sized from whichever list it is showing. Computed rather
    // than typed for the same reason as before: a hard-coded height puts the last row on top
    // of the foot and the Cancel button the moment a row is added.
    static constexpr int sheetHeight (int rows) noexcept
    {
        return kPadY + kHeadH + kHeadGap + rows * kRowH + kFootGap + kFootH + kPadY;
    }
    int rowCount() const noexcept { return controller.isTuningLive() ? kNumLiveSteps : kGroupBuses; }

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (480, getWidth() - 40);
        return juce::Rectangle<int> ((getWidth() - w) / 2, 0, w, sheetHeight (rowCount()));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0c0d0e).withAlpha (0.55f));

        auto card = sheetBounds().toFloat();
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 16 }).drawForRectangle (g, card.toNearestInt());
        {
            juce::Path p;
            p.addRoundedRectangle (card.getX(), card.getY() - 12.0f, card.getWidth(), card.getHeight() + 12.0f,
                                   Dine::Radius::window, Dine::Radius::window, false, false, true, true);
            g.setColour (Dine::sheet);
            g.fillPath (p);
            g.setColour (Dine::hairStrong);
            g.strokePath (p, juce::PathStrokeType (0.5f));
        }

        auto r = sheetBounds().reduced (kPadX, kPadY);
        const bool waiting = controller.isWaitingForBand();
        const bool planning = controller.getStage() == MixController::Stage::Planning;
        const float progress = planning ? 1.0f : controller.getListenProgress();
        const bool live = controller.isTuningLive();
        const auto liveState = controller.getTuneLive().getState();
        const bool capturing = liveState == TuneLiveCoordinator::State::CapturingInitial
                            || liveState == TuneLiveCoordinator::State::CapturingVerify;
        // A live run spends most of its time off a listen - measuring, reasoning, resolving,
        // checking - and there is no percentage to fill during any of it. A ring frozen at
        // 100 is indistinguishable from a ring that has stopped, so once the listening is
        // done the ring sweeps instead of filling and the number becomes the one thing the
        // user actually wants: how long this step has been going.
        const bool working = live && ! capturing;
        const float phase = float (juce::Time::getMillisecondCounter() % 1400u) / 1400.0f;

        // ---- the capture ring
        auto ring = r.removeFromLeft (kHeadH).removeFromTop (kHeadH).toFloat();   // the head is as tall as the ring
        {
            const float radius = 48.0f, thickness = 5.0f;
            auto centre = ring.getCentre();
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (track, juce::PathStrokeType (thickness));

            const float sweepFrom = working ? juce::MathConstants<float>::twoPi * phase : 0.0f;
            const float sweepTo = working ? sweepFrom + juce::MathConstants<float>::twoPi * 0.22f
                                          : juce::MathConstants<float>::twoPi * juce::jlimit (0.02f, 1.0f, progress);
            if (working || progress > 0.0f)
            {
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, sweepFrom, sweepTo, true);
                g.setColour (Dine::accent);
                g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // The number and its caption both live inside the ring, never over the arc.
            const int seconds = stepSeconds();
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (25.0f, 500));
            g.drawText (working ? juce::String (seconds)
                                : waiting ? Glyph::dash() : juce::String (int (std::round (progress * 100.0f))),
                        ring.withTrimmedBottom (24.0f).toNearestInt(), juce::Justification::centred);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (10.0f));
            g.drawText (working ? (seconds == 1 ? "second" : "seconds")
                                : waiting ? "waiting" : "% listened",
                        ring.withTrimmedTop (ring.getHeight() * 0.5f + 8.0f).withHeight (17.0f).toNearestInt(),
                        juce::Justification::centred);
        }

        auto text = sheetBounds().reduced (kPadX, kPadY).withTrimmedLeft (kHeadH + 20).removeFromTop (kHeadH);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        g.drawText (live ? "Tuning the live mix"
                         : planning ? "Building the mix" : waiting ? "Waiting for the band" : "Listening",
                    text.removeFromTop (22), juce::Justification::topLeft);
        text.removeFromTop (5);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        // What the band is being asked for changes through the run: they play for the two
        // listens, and between them they only have to stay ready. Telling them to keep
        // playing while DLIVE is thinking is asking for something it does not need.
        juce::String liveAsk;
        if (capturing) liveAsk = " Keep the full band playing.";
        else if (liveState != TuneLiveCoordinator::State::WaitingForRefinement
                 && liveState != TuneLiveCoordinator::State::ValidatingRefinement
                 && liveState != TuneLiveCoordinator::State::ApplyingRefinement)
            liveAsk = " Stay ready - DLIVE listens again in a moment.";
        g.drawFittedText (live ? juce::String (controller.getTuneLiveStatus()) + liveAsk
                               : waiting ? "Have the band play a song the way they normally would. DLIVE starts as soon as it hears them."
                                         : "Keep playing. Every input is measured at once, then the mix is built around the lead vocal.",
                          text, juce::Justification::topLeft, 4);

        // ---- what the run is actually doing, one line per real step
        if (live)
        {
            const int at = liveStepFor (controller.getTuneLive().getState());
            auto rows = sheetBounds().reduced (kPadX, kPadY).withTrimmedTop (kHeadH + kHeadGap);
            rows = rows.removeFromTop (kNumLiveSteps * kRowH);
            Dine::fillRounded (g, rows.toFloat(), juce::Colours::black.withAlpha (0.24f), 8.0f);
            for (int i = 0; i < kNumLiveSteps; ++i)
            {
                auto row = rows.removeFromTop (kRowH).reduced (12, 0);
                if (i > 0) Dine::drawRule (g, row.withHeight (1).expanded (12, 0), Dine::hairSoft);
                const bool done = at > i;
                const bool now = at == i;
                // The running step breathes. A cloud model can take twenty seconds to answer,
                // and a row that never changes in that time reads as a run that has stopped.
                const float pulse = now ? 0.55f + 0.45f * (0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi))
                                        : 1.0f;
                Dine::drawIcon (g, done ? Dine::Icon::Check : now ? Dine::Icon::Waveform : Dine::Icon::Target,
                                row.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                                done ? Dine::ok : now ? Dine::accent.withMultipliedAlpha (pulse) : Dine::ink4);
                row.removeFromLeft (9);
                g.setColour (done ? Dine::ink2 : now ? Dine::ink : Dine::ink4);
                g.setFont (Dine::text (12.5f, now ? 600 : 500));
                g.drawText (kLiveSteps[i].label, row.removeFromLeft (row.getWidth() - 52), juce::Justification::centredLeft);
                // How long it has been on this step, on the step itself: the one number that
                // separates "still working" from "stuck".
                if (now && stepSeconds() > 0)
                {
                    g.setColour (Dine::ink3);
                    g.setFont (Dine::mono (11.0f));
                    g.drawText (juce::String (stepSeconds()) + " s", row, juce::Justification::centredRight);
                }
            }
            auto liveFoot = sheetBounds().reduced (kPadX, kPadY).removeFromBottom (kFootH);
            liveFoot.removeFromRight (cancel.getWidth() + 12);      // the sentence never runs under the button
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.5f));
            g.drawText (controller.getTuneLive().getProvider()->sendsDataExternally()
                            ? "Measurements are sent out. No audio ever leaves this machine."
                            : "All of this happens on this machine.",
                        liveFoot, juce::Justification::centredLeft, true);
            return;
        }

        // ---- one line per group, ticked the frame it is heard
        auto lines = sheetBounds().reduced (kPadX, kPadY).withTrimmedTop (kHeadH + kHeadGap);
        lines = lines.removeFromTop (kGroupBuses * kRowH);
        Dine::fillRounded (g, lines.toFloat(), juce::Colours::black.withAlpha (0.24f), 8.0f);
        for (int i = 0; i < kGroupBuses; ++i)
        {
            auto row = lines.removeFromTop (kRowH).reduced (12, 0);
            if (i > 0) Dine::drawRule (g, row.withHeight (1).expanded (12, 0), Dine::hairSoft);
            const bool used = controller.getEngine().isBusUsed (kGroupBus[i]);
            const bool heard = used && controller.busHeard (kGroupBus[i]);
            Dine::drawIcon (g, kGroupIcons[i], row.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                            ! used ? Dine::ink4 : heard ? Dine::ok : Dine::ink3);
            row.removeFromLeft (9);
            g.setColour (! used ? Dine::ink4 : Dine::ink);
            g.setFont (Dine::text (12.5f, 500));
            g.drawText (kGroupNames[i], row.removeFromLeft (row.getWidth() / 2), juce::Justification::centredLeft);
            g.setColour (! used ? Dine::ink4 : heard ? Dine::ok : Dine::ink3);
            g.setFont (Dine::text (12.0f));
            g.drawText (! used ? "not in this mix" : heard ? "Heard" : "waiting", row, juce::Justification::centredRight);
        }

        auto foot = sheetBounds().reduced (kPadX, kPadY).removeFromBottom (kFootH);
        foot.removeFromRight (cancel.getWidth() + 12);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText ("The mix keeps playing while DLIVE listens.", foot, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto foot = sheetBounds().reduced (kPadX, kPadY).removeFromBottom (kFootH);
        cancel.setBounds (foot.removeFromRight (juce::jmax (80, cancel.idealWidth())).withHeight (Dine::Metric::button));
    }

private:
    MixController& controller;
    DineButton cancel { "Cancel", DineButton::Style::Standard };
    TuneLiveCoordinator::State lastLiveState = TuneLiveCoordinator::State::Idle;
    juce::uint32 stepStartedMs = 0;
};

// ------------------------------------------------------------------ ResultSheet
// What TUNE MIX built, with BEFORE / AFTER, KEEP and REVERT.
class MixPage::ResultSheet : public juce::Component
{
public:
    ResultSheet (MixController& c, MixPage& p) : controller (c), page (p)
    {
        for (auto* b : { &before, &after, &keep, &revert, &review }) addAndMakeVisible (*b);
        before.setCaps (true); after.setCaps (true); keep.setCaps (true); revert.setCaps (true);
        before.setClickingTogglesState (false); after.setClickingTogglesState (false);
        before.onClick = [this] { controller.setCompare (MixController::Compare::Before); refresh(); };
        after.onClick  = [this] { controller.setCompare (MixController::Compare::After); refresh(); };
        keep.onClick   = [this] { controller.keepPlan(); if (page.onToast) page.onToast ("Kept. This is your mix now; RE-TUNE any time the band changes."); };
        revert.onClick = [this] { controller.revertPlan(); if (page.onToast) page.onToast ("Reverted to the previous mix."); };
        review.onClick = [this] { if (page.onOpenAdvanced) page.onOpenAdvanced(); };
        setInterceptsMouseClicks (true, true);
    }

    void refresh()
    {
        const bool showingAfter = controller.getCompare() == MixController::Compare::After;
        before.setToggleState (! showingAfter, juce::dontSendNotification);
        after.setToggleState (showingAfter, juce::dontSendNotification);
        resized();      // the card is sized from its bullets, so the buttons follow the content
        repaint();
    }

    // A line, its explanation, and whether it is something DLIVE did. A refusal or a
    // "cannot do this" drawn with a tick would read as a change that was made.
    struct Bullet { juce::String what, why; bool done = true; };

    // A line that explains an engineering decision is a sentence, not a label, so a bullet
    // wraps rather than ending in an ellipsis - and the card is then sized from what it is
    // actually holding, instead of a typed height that leaves a void under three short lines.
    static constexpr int kSideX = 26, kSideY = 22;
    static constexpr int kIconGutter = 24;     // the tick and the gap after it
    static constexpr int kControlsH = 84;      // the A/B row, the rule and the buttons

    int bulletWidth() const { return juce::jmin (600, getWidth() - 40) - kSideX * 2 - 14 * 2 - kIconGutter; }

    // How many lines a string really takes at this width. Measured, not estimated from the
    // unwrapped width: an estimate that is one line short makes drawFittedText squash the
    // text horizontally to fit, which is why a sentence could come out visibly narrower
    // than the one under it.
    static int linesNeeded (const juce::Font& font, const juce::String& text, int width)
    {
        if (text.isEmpty() || width < 40) return 1;
        juce::AttributedString attributed;
        attributed.setText (text);
        attributed.setFont (font);
        attributed.setJustification (juce::Justification::topLeft);
        juce::TextLayout layout;
        layout.createLayout (attributed, float (width));
        return juce::jlimit (1, 4, int (std::ceil (layout.getHeight() / juce::jmax (1.0f, font.getHeight()) - 0.05f)));
    }

    // How tall one bullet needs to be, from how many lines its two strings really take.
    int bulletHeight (const Bullet& b) const
    {
        const int avail = juce::jmax (80, bulletWidth());
        const int whatLines = linesNeeded (Dine::text (13.0f, 600), b.what, avail);
        const int whyLines = b.why.isEmpty() ? 0 : linesNeeded (Dine::text (12.5f), b.why, avail);
        return 14 + whatLines * 17 + (whyLines > 0 ? 3 + whyLines * 16 : 0);
    }

    int listHeight() const
    {
        int h = 4;
        for (const auto& b : bullets()) h += bulletHeight (b);
        return h;
    }

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (600, getWidth() - 40);
        const int content = kSideY + 17 + 8 + 24 + 10 + listHeight() + kControlsH + kSideY;
        const int h = juce::jlimit (280, juce::jmax (280, getHeight() - 20), content);
        return juce::Rectangle<int> ((getWidth() - w) / 2, 0, w, h);
    }

    // The lines the sheet shows: the plan's own notes, then what it decided about
    // how the sources work together.
    std::vector<Bullet> bullets() const
    {
        std::vector<Bullet> out;
        const auto* plan = controller.getPlan();
        if (plan == nullptr) return out;
        // A live run explains itself in its own words: what it decided, what it could only
        // approximate and what it refused to do. That reads better than the plan's notes,
        // and it is the only place the refusals appear.
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
                if (out.size() >= 5) return out;
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
            if (out.size() >= 5) break;
        }
        return out;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0c0d0e).withAlpha (0.55f));
        auto card = sheetBounds().toFloat();
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 16 }).drawForRectangle (g, card.toNearestInt());
        {
            juce::Path p;
            p.addRoundedRectangle (card.getX(), card.getY() - 12.0f, card.getWidth(), card.getHeight() + 12.0f,
                                   Dine::Radius::window, Dine::Radius::window, false, false, true, true);
            g.setColour (Dine::sheet);
            g.fillPath (p);
            g.setColour (Dine::hairStrong);
            g.strokePath (p, juce::PathStrokeType (0.5f));
        }

        const auto* plan = controller.getPlan();
        if (plan == nullptr) return;

        auto r = sheetBounds().reduced (26, 22);
        auto head = r.removeFromTop (17);
        Dine::drawIcon (g, Dine::Icon::Check, head.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f), Dine::accent);
        head.removeFromLeft (8);
        g.setColour (Dine::accent);
        g.setFont (Dine::text (11.5f, 600));
        g.drawText ("The mix is built", head.removeFromLeft (140), juce::Justification::centredLeft);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        g.drawText (juce::String (plan->stripsHeard) + " heard  " + Glyph::dot() + "  "
                        + juce::String (plan->parametersChanged) + " settings  " + Glyph::dot() + "  "
                        + juce::String (plan->fadersChanged) + " levels",
                    head, juce::Justification::centredRight);

        r.removeFromTop (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (19.0f, 600));
        g.drawText (juce::String (plan->headline), r.removeFromTop (24), juce::Justification::centredLeft, true);

        r.removeFromTop (10);
        auto list = r.removeFromTop (juce::jmin (juce::jmax (0, r.getHeight() - kControlsH), listHeight()));
        Dine::fillRounded (g, list.toFloat(), juce::Colours::black.withAlpha (0.20f), 8.0f);
        auto inner = list.reduced (14, 2);
        bool first = true;
        for (const auto& b : bullets())
        {
            const int h = bulletHeight (b);
            if (inner.getHeight() < h) break;
            auto row = inner.removeFromTop (h);
            if (! first) Dine::drawRule (g, row.withHeight (1), Dine::hairSoft);
            first = false;
            row = row.reduced (0, 7);
            Dine::drawIcon (g, b.done ? Dine::Icon::Check : Dine::Icon::Target,
                            row.removeFromLeft (14).toFloat().withSizeKeepingCentre (13.0f, 13.0f).withY (float (row.getY()) + 1.0f),
                            b.done ? Dine::accent : Dine::ink3);
            row.removeFromLeft (10);
            // Drawn at full width - the last argument keeps JUCE from squeezing the glyphs
            // to avoid a wrap. A sentence that wraps reads; a sentence that is squashed to
            // fit one line does not, and it no longer matches the one under it.
            const int avail = juce::jmax (80, row.getWidth());
            const int whatLines = linesNeeded (Dine::text (13.0f, 600), b.what, avail);
            g.setFont (Dine::text (13.0f, 600));
            g.setColour (b.done ? Dine::ink : Dine::ink2);
            g.drawFittedText (b.what, row.removeFromTop (whatLines * 17), juce::Justification::topLeft, whatLines, 1.0f);
            if (b.why.isNotEmpty())
            {
                row.removeFromTop (3);
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (12.5f));
                g.drawFittedText (b.why, row, juce::Justification::topLeft,
                                  linesNeeded (Dine::text (12.5f), b.why, avail), 1.0f);
            }
        }

        // The A/B segment track and its note.
        auto ab = juce::Rectangle<int> (before.getBounds().getX(), before.getBounds().getY(),
                                        before.getWidth() + after.getWidth(), before.getHeight()).expanded (2, 2);
        Dine::fillRounded (g, ab.toFloat(), juce::Colours::white.withAlpha (0.08f), 7.0f);
        auto note = juce::Rectangle<int> (ab.getRight() + 12, ab.getY(), sheetBounds().getRight() - 26 - ab.getRight() - 12, ab.getHeight());
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawText (controller.getCompare() == MixController::Compare::After
                        ? "AFTER is the proposed mix. Both are level-matched."
                        : "BEFORE is the mix as it was. Both are level-matched.",
                    note, juce::Justification::centredLeft, true);

        Dine::drawRule (g, sheetBounds().reduced (26, 0).withY (keep.getBounds().getY() - 14).withHeight (1), Dine::hair);
    }

    void resized() override
    {
        auto r = sheetBounds().reduced (26, 22);
        auto foot = r.removeFromBottom (Dine::Metric::button);
        const int kw = juce::jmax (86, keep.idealWidth());
        keep.setBounds (foot.removeFromRight (kw));
        foot.removeFromRight (8);
        const int rw = juce::jmax (86, revert.idealWidth());
        revert.setBounds (foot.removeFromRight (rw));
        review.setBounds (foot.removeFromLeft (juce::jmax (70, review.idealWidth())));

        auto ab = r.removeFromBottom (Dine::Metric::control + 14).removeFromTop (Dine::Metric::control);
        before.setBounds (ab.removeFromLeft (juce::jmax (84, before.idealWidth())));
        after.setBounds (ab.removeFromLeft (juce::jmax (76, after.idealWidth())));
    }

private:
    MixController& controller;
    MixPage& page;
    DineButton before { "Before", DineButton::Style::Segment }, after { "After", DineButton::Style::Segment };
    DineButton keep { "Keep", DineButton::Style::Filled }, revert { "Revert", DineButton::Style::Standard };
    DineButton review { "Review", DineButton::Style::Ghost };
};

// ------------------------------------------------------------------ MixPage
MixPage::MixPage (MixController& c) : controller (c)
{
    for (int i = 0; i < kGroupTiles; ++i) { groups[size_t (i)] = std::make_unique<GroupTile> (i); addAndMakeVisible (*groups[size_t (i)]); }
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

    railTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Right, "Inputs");
    railTab->onClick = [this] { setRailShown (! railShown); };
    addAndMakeVisible (*railTab);

    listenSheet = std::make_unique<ListenSheet> (controller);
    resultSheet = std::make_unique<ResultSheet> (controller, *this);
    referenceSheet = std::make_unique<ReferenceSheet> (controller);
    addAndMakeVisible (tuneButton);
    addAndMakeVisible (liveTuneButton);
    addAndMakeVisible (referenceButton);
    addAndMakeVisible (advancedButton);
    addAndMakeVisible (resetMacrosButton);
    // The sheets are added last: sibling order is z-order.
    addChildComponent (*referenceSheet);
    addChildComponent (*resultSheet);
    addChildComponent (*listenSheet);

    referenceSheet->onClose = [this] { referenceSheet->setVisible (false); refreshTuneButton(); repaint(); };
    referenceSheet->onToast = [this] (const juce::String& t) { if (onToast) onToast (t); };

    tuneButton.setCaps (true);
    tuneButton.setFontPx (19.0f);
    tuneButton.setIcon (Dine::Icon::Waveform);
    tuneButton.onClick = [this] { pressTune(); };
    tuneButton.setTooltip ("Listen to the band and build DLIVE's mix from what it measures. Deterministic: the same "
                           "listen always gives the same mix, and nothing leaves this machine.");
    liveTuneButton.setCaps (true);
    liveTuneButton.setFontPx (19.0f);
    liveTuneButton.setIcon (Dine::Icon::Waveform);
    liveTuneButton.onClick = [this] { pressLiveTune(); };
    liveTuneButton.setTooltip ("The same listen, with a mix engineer's reasoning on top: DLIVE builds its mix, works out "
                               "what this band still needs, applies only what it can do safely, then listens again to "
                               "check. You can compare, review every change and revert.");
    referenceButton.setFontPx (12.5f);
    referenceButton.onClick = [this] { openReference(); };
    referenceButton.setTooltip ("Aim the mix at a finished recording: DLIVE matches the master's tone, image and density "
                                "to it. How loud the stream is delivered, and who is loud in the mix, are not copied.");
    advancedButton.setIcon (Dine::Icon::List);
    advancedButton.setFontPx (12.5f);
    advancedButton.onClick = [this] { if (onOpenAdvanced) onOpenAdvanced(); };
    resetMacrosButton.setFontPx (11.5f);
    resetMacrosButton.onClick = [this] { controller.resetMacros(); for (int i = 0; i < int (MixMacro::Count); ++i) macros[size_t (i)]->setValue (50.0f); };
    refresh();
}

MixPage::~MixPage() = default;

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
    // A listen or a proposal owns the screen while it is happening: a second sheet over the
    // first is two things asking for the same decision.
    if (listenSheet->isVisible() || resultSheet->isVisible()) return;
    // The button is a toggle: pressing it again puts the sheet away, the way every panel does.
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

    // Both buttons keep their place while a run is going - the one that is not running is
    // disabled, never hidden, so the row does not jump under the pointer mid-press.
    tuneButton.setButtonText (busy && ! live ? "Cancel" : controller.getTuneCount() > 0 ? "Re-tune" : "Tune mix");
    tuneButton.setEnabled (ready && ! live && stage != MixController::Stage::Planning);

    // "RE-TUNE LIVE", not "TUNE LIVE MIX AGAIN": a product verb that does not fit its button
    // is not a product verb, and the row is sized from these labels below.
    liveTuneButton.setButtonText (live ? "Stop" : controller.getTuneCount() > 0 ? "Re-tune live" : "Tune live mix");
    liveTuneButton.setStyle (live ? DineButton::Style::Standard : DineButton::Style::Filled);
    liveTuneButton.setEnabled (ready && (live || stage != MixController::Stage::Listening) && stage != MixController::Stage::Planning);
    // The reference is a target rather than a run, so the button only says whether there is
    // one; pressing it is safe at any time except while a listen owns the screen.
    referenceButton.setIcon (controller.hasReference() ? Dine::Icon::Check : Dine::Icon::Waveform);
    referenceButton.setEnabled (ready && ! busy);
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
        row->onTune = [this, i] { if (onTuneStrip) onTuneStrip (i); };
        railHolder.addAndMakeVisible (*row);
        inputRows.push_back (std::move (row));
    }
    builtRailFor = graph.numStrips();
    resized();
}

void MixPage::refresh()
{
    const auto& engine = controller.getEngine();
    const auto& graph = engine.getGraph();
    const bool listening = controller.isListening();
    for (int i = 0; i < kGroupBuses; ++i)
    {
        const MixBus bus = kGroupBus[i];
        const bool used = controller.isPrepared() && engine.isBusUsed (bus);
        const auto& m = engine.getBus (bus).getOutputMeter();
        groups[size_t (i)]->set (used, graph.stripsOnBus (bus), used ? m.consumeMaxPeakDb() : -120.0f, used ? m.getMaxRmsDb() : -120.0f, used && m.hasClipped(),
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
        groups[size_t (kGroupBuses)]->set (returns > 0, returns, peak, rms, clip, 0);
    }

    // ---- the input rail
    if (controller.isPrepared() && graph.numStrips() != builtRailFor) rebuildRail();
    if (controller.isPrepared())
    {
        const auto& kept = controller.getBase();
        const auto* plan = controller.getPlan();
        for (int i = 0; i < int (inputRows.size()) && i < graph.numStrips(); ++i)
        {
            const auto& m = engine.getStrip (i).getOutputMeter();
            const bool faint = plan != nullptr && i < int (plan->strips.size()) && plan->strips[size_t (i)].faint;
            inputRows[size_t (i)]->set (m.consumeMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped(),
                                        i < kept.numStrips && kept.strips[size_t (i)].mute, faint);
        }
    }

    health = controller.getMixHealthPercent();
    status = controller.getStatusText();
    const auto stage = controller.getStage();
    // TUNE CHANNEL has its own sheet over whatever workspace it was started from, so these
    // two - which are about the whole mix - stay out of its way.
    const bool mixTune = ! controller.isTuningChannel();
    // A live run keeps the progress sheet up for the whole workflow: the deterministic mix is
    // audible underneath it, but KEEP and REVERT are not offered until the run has finished.
    const bool live = controller.isTuningLive();
    const bool listenOn = mixTune && (live || stage == MixController::Stage::Listening || stage == MixController::Stage::Planning);
    const bool preview = mixTune && ! live && stage == MixController::Stage::Preview && controller.hasPlan();
    if ((listenOn || preview) && referenceSheet->isVisible() && ! referenceSheet->isMeasuring()) referenceSheet->setVisible (false);
    if (referenceSheet->isVisible() || referenceSheet->isMeasuring()) referenceSheet->refresh();
    if (listenSheet->isVisible() != listenOn) { listenSheet->setVisible (listenOn); if (listenOn) listenSheet->toFront (false); }
    if (resultSheet->isVisible() != preview) { resultSheet->setVisible (preview); if (preview) { resultSheet->toFront (false); resized(); } }
    // The sheet changes height when it swaps the group list for the live workflow's steps,
    // and that happens without a resize, so the foot is placed again each refresh.
    if (listenOn) { listenSheet->tick(); listenSheet->resized(); listenSheet->repaint(); }
    if (preview) resultSheet->refresh();
    // A live run starts and ends without the stage necessarily moving (it finishes in
    // Preview, where it already was), so the buttons follow the run as well as the stage -
    // otherwise the primary action is still offering "Stop" after the mix is ready.
    if (stage != lastStage || live != lastLiveRun) { refreshTuneButton(); lastStage = stage; lastLiveRun = live; }
    for (int i = 0; i < int (MixMacro::Count); ++i) macros[size_t (i)]->setValue (controller.getMacros().get (MixMacro (i)));

    // The meters and the rail rows are components that repaint themselves; this page's own
    // paint is the headline, the status line and the health number. Redrawing all of that
    // thirty times a second for text that has not changed costs a whole 30 Hz frame on a
    // large console (dlive_ui_snapshots --frames), so it only happens when it says something
    // different.
    const PageLook now { status, health, stage, controller.getTuneCount(), controller.hasReference(),
                         controller.hasReference() ? juce::String (controller.getReference().name) : juce::String() };
    if (now != painted) { painted = now; repaint(); }
}

MixPage::Layout MixPage::layout() const
{
    Layout l;
    auto b = getLocalBounds();
    l.rail = b.removeFromRight (railWidth());
    l.railTab = l.rail.removeFromLeft (Dine::Metric::panelTab);   // the gutter the handle sits in
    auto left = b.reduced (0, 20).withTrimmedLeft (24).withTrimmedRight (22);
    auto top = left.removeFromTop (76);
    // TUNE LIVE MIX is the primary action and sits on the right; the deterministic TUNE MIX
    // keeps its place beside it. Both are sized from their own labels rather than from a
    // typed width, because the label changes with the stage ("Tune live mix" / "Re-tune live"
    // / "Stop") and a truncated product verb reads as a bug.
    const int liveW = juce::jmax (168, liveTuneButton.idealWidth());
    const int tuneW = juce::jmax (124, tuneButton.idealWidth());
    // The reference is a target, not a run, so it sits beside the two verbs as a quiet
    // control rather than as a third thing of the same weight.
    const int refW = juce::jmax (112, referenceButton.idealWidth());
    auto tuneArea = top.removeFromRight (refW + 12 + liveW + 10 + tuneW);
    l.liveTune = tuneArea.removeFromRight (liveW);
    tuneArea.removeFromRight (10);
    l.tune = tuneArea.removeFromRight (tuneW);
    tuneArea.removeFromRight (12);
    l.reference = tuneArea.withSizeKeepingCentre (refW, Dine::Metric::button + 8);
    top.removeFromRight (14);
    l.health = top;
    left.removeFromTop (14);
    // The macros card is as tall as what it holds; the group strips take the spare
    // height, so a taller window buys taller meters rather than empty card.
    const int macrosH = 13 + 17 + 16 + kMacroRowH + 14 + Dine::Metric::control + 13;
    l.groups = left.removeFromTop (juce::jmax (104, left.getHeight() - 14 - macrosH));
    left.removeFromTop (14);
    l.macros = left.removeFromTop (macrosH);
    return l;
}

void MixPage::paint (juce::Graphics& g)
{
    const auto l = layout();

    // ---- mix health
    Dine::drawCard (g, l.health.toFloat());
    {
        auto r = l.health.reduced (16, 14);
        const bool known = health > 0;
        auto figure = r.removeFromLeft (known ? 116 : 96);
        g.setColour (! known ? Dine::ink3 : health >= 85 ? Dine::ok : health >= 60 ? Dine::warn : Dine::crit);
        g.setFont (Dine::mono (34.0f, 500));
        g.drawText (known ? juce::String (health) + "%" : Glyph::dash(), figure.removeFromTop (34), juce::Justification::centredLeft);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f));
        g.drawText ("Mix health", figure, juce::Justification::topLeft);

        // The state chip on the right, then the note fills what is left.
        if (controller.getStage() == MixController::Stage::Mixed || controller.getStage() == MixController::Stage::Ready)
        {
            const bool tuned = controller.getStage() == MixController::Stage::Mixed;
            const juce::String label = tuned ? "Tuned" : "Ready";
            const float w = Dine::pillWidth (label, true);
            auto chip = r.removeFromRight (int (w)).withSizeKeepingCentre (int (w), 22).toFloat();
            Dine::drawPill (g, chip, label, tuned ? Dine::ok : Dine::ink2, tuned ? Dine::Icon::Check : Dine::Icon::Target);
            r.removeFromRight (14);
        }

        // What the mix is aimed at, when it is aimed at a record rather than at the profile.
        // It sits beside the state chip because it is the same kind of fact about the mix.
        if (controller.hasReference())
        {
            const juce::String name (controller.getReference().name);
            const float w = juce::jmin (Dine::pillWidth (name, true), float (r.getWidth()) * 0.42f);
            if (w > 60.0f)
            {
                auto chip = r.removeFromRight (int (w)).withSizeKeepingCentre (int (w), 22).toFloat();
                Dine::drawPill (g, chip, name, Dine::accent, Dine::Icon::Waveform);
                r.removeFromRight (10);
            }
        }

        auto bar = r.removeFromTop (r.getHeight() / 2 + 2).removeFromBottom (5);
        Dine::fillRounded (g, bar.toFloat(), juce::Colours::white.withAlpha (0.09f), 3.0f);
        if (known)
        {
            g.setColour (health >= 85 ? Dine::ok : health >= 60 ? Dine::warn : Dine::crit);
            g.fillRoundedRectangle (bar.toFloat().withWidth (float (bar.getWidth()) * float (health) / 100.0f), 3.0f);
        }
        r.removeFromTop (7);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f));
        g.drawFittedText (status, r, juce::Justification::topLeft, 2);
    }

    // ---- the macros card (the tiles paint themselves)
    Dine::drawCard (g, l.macros.toFloat());
    {
        auto r = l.macros.reduced (16, 13);
        auto head = r.removeFromTop (17);
        Dine::drawIcon (g, Dine::Icon::Sliders, head.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), Dine::glyph);
        head.removeFromLeft (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f, 600));
        const int w = Dine::textWidth (Dine::text (12.5f, 600), "Shape the mix");
        g.drawText ("Shape the mix", head.removeFromLeft (w), juce::Justification::centredLeft);
        head.removeFromLeft (8);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText (Glyph::dash() + juce::String (" centred means \"as tuned\""), head, juce::Justification::centredLeft, true);

        // The footer rule above Open Advanced.
        auto foot = l.macros.reduced (16, 13).removeFromBottom (Dine::Metric::control);
        Dine::drawRule (g, foot.withY (foot.getY() - 11).withHeight (1), Dine::hairSoft);
        auto note = foot.withTrimmedLeft (advancedButton.getWidth() + 9);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText ("Every channel, every bus, and what TUNE MIX decided. Nothing in there is required.",
                    note, juce::Justification::centredLeft, true);
    }

    // ---- the input rail
    g.setColour (Dine::rail);
    g.fillRect (l.rail.getUnion (l.railTab));
    g.setColour (Dine::hair);
    g.fillRect (float (l.railTab.getX()), 0.0f, 0.5f, float (getHeight()));
    if (! railShown) return;
    auto head = l.rail.reduced (12, 0).withY (12).withHeight (16);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (12.0f, 600));
    g.drawText ("Inputs", head.removeFromLeft (44), juce::Justification::centredLeft);
    g.setColour (Dine::ink3);
    g.setFont (Dine::mono (11.0f));
    g.drawText (juce::String (int (inputRows.size())), head.removeFromLeft (24), juce::Justification::centredLeft);
    g.setColour (Dine::ink4);
    g.setFont (Dine::text (10.5f));
    g.drawText ("click one to tune it", head, juce::Justification::centredRight, true);

    if (inputRows.empty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        g.drawFittedText ("Assign inputs to see them here.", l.rail.reduced (14, 40).removeFromTop (40), juce::Justification::centredTop, 2);
    }

    // A faint input is a mic that is off, not a mix problem: say so, and change nothing.
    int faintCount = 0;
    juce::String faintNames;
    for (const auto& r : inputRows)
        if (r->faint) { ++faintCount; faintNames += (faintNames.isEmpty() ? "" : ", ") + r->name; }
    if (faintCount > 0)
    {
        auto box = l.rail.reduced (8, 0).removeFromBottom (86).withTrimmedBottom (8);
        Dine::fillRounded (g, box.toFloat(), Dine::warn.withAlpha (0.10f), Dine::Radius::control);
        Dine::hairlineRounded (g, box.toFloat(), Dine::warn.withAlpha (0.32f), Dine::Radius::control);
        auto r = box.reduced (12, 11);
        auto top = r.removeFromTop (16);
        Dine::drawIcon (g, Dine::Icon::Warn, top.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f), Dine::warn);
        top.removeFromLeft (7);
        g.setColour (Dine::warn);
        g.setFont (Dine::text (12.0f, 600));
        g.drawText (faintCount == 1 ? "Check this input" : "Check these inputs", top, juce::Justification::centredLeft);
        r.removeFromTop (5);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.0f));
        g.drawFittedText (faintNames + (faintCount == 1 ? " never rose above a whisper." : " never rose above a whisper."),
                          r.removeFromTop (17), juce::Justification::topLeft, 1);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawFittedText ("Left where it is. A faint input is usually a mic that is off, not a mix problem.",
                          r, juce::Justification::topLeft, 2);
    }
}

void MixPage::resized()
{
    const auto l = layout();

    tuneButton.setBounds (l.tune);
    liveTuneButton.setBounds (l.liveTune);
    referenceButton.setBounds (l.reference);
    auto groupRow = l.groups;
    const int gap = 10;
    const int w = (groupRow.getWidth() - gap * (kGroupTiles - 1)) / kGroupTiles;
    for (auto& t : groups) { t->setBounds (groupRow.removeFromLeft (w)); groupRow.removeFromLeft (gap); }

    auto card = l.macros.reduced (16, 13);
    auto head = card.removeFromTop (17);
    resetMacrosButton.setBounds (head.removeFromRight (juce::jmax (72, resetMacrosButton.idealWidth())).withSizeKeepingCentre (juce::jmax (72, resetMacrosButton.idealWidth()), 22));
    auto foot = card.removeFromBottom (Dine::Metric::control);
    advancedButton.setBounds (foot.removeFromLeft (juce::jmax (140, advancedButton.idealWidth())));
    card.removeFromBottom (11);
    card.removeFromTop (14);

    auto row = card.removeFromTop (juce::jmin (card.getHeight(), kMacroRowH));
    const int mgap = 16;
    const int mw = (row.getWidth() - mgap * (int (MixMacro::Count) - 1)) / int (MixMacro::Count);
    for (auto& m : macros) { m->setBounds (row.removeFromLeft (mw)); row.removeFromLeft (mgap); }

    // ---- rail
    railTab->setBounds (l.railTab);
    int faintCount = 0;
    for (const auto& r : inputRows) if (r->faint) ++faintCount;
    auto rail = l.rail.reduced (8, 0).withTrimmedTop (34);
    if (faintCount > 0) rail.removeFromBottom (86);
    else rail.removeFromBottom (8);
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
