#include "MixPage.h"
#include "Core/DbUtils.h"

namespace livemix
{

namespace
{
    const char* kGroupNames[5] = { "Drums", "Bass", "Music", "Vocals", "FX" };
    const MixBus kGroupBus[5] = { MixBus::Drums, MixBus::Bass, MixBus::Music, MixBus::Vocals, MixBus::Master /* FX: returns */ };
    const Dine::Icon kGroupIcons[5] = { Dine::Icon::Drum, Dine::Icon::Guitar, Dine::Icon::Piano, Dine::Icon::Mic, Dine::Icon::Fx };

    juce::Colour groupColour (int i) noexcept
    {
        switch (i)
        {
            case 0:  return Dine::warn;
            case 1:  return Dine::accent;
            case 2:  return juce::Colour (0xff8fa2d8);
            case 3:  return Dine::ok;
            default: return Dine::ink2;
        }
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
                           : group == 4 ? juce::String (strips) + (strips == 1 ? " return" : " returns")
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
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 11);
        if (getHeight() > 150)
        {
            // A tall strip reads like a console meter: the numbers on top, the meter under them.
            r.removeFromTop (16 + 4 + 15 + 26 + 14 + 8);
            meter.setBounds (r.withSizeKeepingCentre (juce::jmin (22, r.getWidth()), r.getHeight()));
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
class MixPage::InputRow : public juce::Component
{
public:
    InputRow (const juce::String& n, ChannelRole role, int number)
        : name (n), icon (Dine::iconForRole (role)), num (number), meter (DineMeter::Style::Bar)
    {
        addAndMakeVisible (meter);
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

        auto r = getLocalBounds().reduced (6, 0);
        Dine::drawIcon (g, icon, r.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                        muted ? Dine::crit : faint ? Dine::warn : Dine::glyph);
        r.removeFromLeft (9);

        const juce::String state = muted ? "Muted" : faint ? "Faint" : dbText (peak);
        const juce::Colour stateColour = muted ? Dine::crit : faint ? Dine::warn : Dine::ink3;
        auto right = r.removeFromRight (muted || faint ? 42 : 34);
        g.setColour (stateColour);
        g.setFont (muted || faint ? Dine::text (10.5f, 600) : Dine::mono (10.5f));
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
    bool muted = false, faint = false;
    DineMeter meter;
};

// ------------------------------------------------------------------ ListenSheet
// A macOS sheet: it drops from under the toolbar while DINELIVE listens.
class MixPage::ListenSheet : public juce::Component
{
public:
    explicit ListenSheet (MixController& c) : controller (c)
    {
        addAndMakeVisible (cancel);
        cancel.onClick = [this] { controller.abortTuneMix(); };
        setInterceptsMouseClicks (true, true);
    }

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (480, getWidth() - 40);
        return juce::Rectangle<int> ((getWidth() - w) / 2, 0, w, 330);
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

        auto r = sheetBounds().reduced (26, 24);
        const bool waiting = controller.isWaitingForBand();
        const bool planning = controller.getStage() == MixController::Stage::Planning;
        const float progress = planning ? 1.0f : controller.getListenProgress();

        // ---- the capture ring
        auto ring = r.removeFromLeft (104).removeFromTop (104).toFloat();
        {
            const float radius = 48.0f, thickness = 5.0f;
            auto centre = ring.getCentre();
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (track, juce::PathStrokeType (thickness));
            if (progress > 0.0f)
            {
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f,
                                   juce::MathConstants<float>::twoPi * juce::jlimit (0.02f, 1.0f, progress), true);
                g.setColour (Dine::accent);
                g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (25.0f, 500));
            g.drawText (waiting ? Glyph::dash() : juce::String (int (std::round (progress * 100.0f))),
                        ring.withTrimmedBottom (16.0f).toNearestInt(), juce::Justification::centred);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (10.0f));
            g.drawText (waiting ? "waiting" : "% listened", ring.removeFromBottom (26.0f).toNearestInt(), juce::Justification::centred);
        }

        auto text = sheetBounds().reduced (26, 24).withTrimmedLeft (104 + 20).removeFromTop (104);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        g.drawText (planning ? "Building the mix" : waiting ? "Waiting for the band" : "Listening", text.removeFromTop (22), juce::Justification::topLeft);
        text.removeFromTop (5);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (waiting ? "Have the band play a song the way they normally would. DINELIVE starts as soon as it hears them."
                                  : "Keep playing. Every input is measured at once, then the mix is built around the lead vocal.",
                          text, juce::Justification::topLeft, 4);

        // ---- one line per group, ticked the frame it is heard
        auto lines = sheetBounds().reduced (26, 24).withTrimmedTop (104 + 18);
        lines = lines.removeFromTop (4 * 32);
        Dine::fillRounded (g, lines.toFloat(), juce::Colours::black.withAlpha (0.24f), 8.0f);
        for (int i = 0; i < 4; ++i)
        {
            auto row = lines.removeFromTop (32).reduced (12, 0);
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

        auto foot = sheetBounds().reduced (26, 24).removeFromBottom (26);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText ("The mix keeps playing while DINELIVE listens.", foot, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto foot = sheetBounds().reduced (26, 24).removeFromBottom (26);
        cancel.setBounds (foot.removeFromRight (juce::jmax (80, cancel.idealWidth())).withHeight (Dine::Metric::button));
    }

private:
    MixController& controller;
    DineButton cancel { "Cancel", DineButton::Style::Standard };
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
        repaint();
    }

    juce::Rectangle<int> sheetBounds() const
    {
        const int w = juce::jmin (600, getWidth() - 40);
        return juce::Rectangle<int> ((getWidth() - w) / 2, 0, w, juce::jmin (getHeight() - 20, 372));
    }

    // The lines the sheet shows: the plan's own notes, then what it decided about
    // how the sources work together.
    std::vector<std::pair<juce::String, juce::String>> bullets() const
    {
        std::vector<std::pair<juce::String, juce::String>> out;
        const auto* plan = controller.getPlan();
        if (plan == nullptr) return out;
        for (const auto& n : plan->notes)
        {
            out.push_back ({ juce::String (n), {} });
            if (out.size() >= 2) break;
        }
        for (const auto& rel : plan->relationships)
        {
            if (rel.changes.empty() && rel.kind != Recommendation::Kind::MixGain) continue;
            out.push_back ({ juce::String (rel.what), juce::String (rel.why) });
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
        auto list = r.removeFromTop (juce::jmax (0, r.getHeight() - 84));
        Dine::fillRounded (g, list.toFloat(), juce::Colours::black.withAlpha (0.20f), 8.0f);
        auto inner = list.reduced (14, 2);
        bool first = true;
        for (const auto& b : bullets())
        {
            const int h = b.second.isEmpty() ? 30 : 56;
            if (inner.getHeight() < h) break;
            auto row = inner.removeFromTop (h);
            if (! first) Dine::drawRule (g, row.withHeight (1), Dine::hairSoft);
            first = false;
            row = row.reduced (0, 7);
            Dine::drawIcon (g, Dine::Icon::Check, row.removeFromLeft (14).toFloat().withSizeKeepingCentre (13.0f, 13.0f).withY (float (row.getY()) + 1.0f), Dine::accent);
            row.removeFromLeft (10);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (13.0f, 600));
            g.drawText (b.first, row.removeFromTop (16), juce::Justification::topLeft, true);
            if (b.second.isNotEmpty())
            {
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (12.5f));
                g.drawFittedText (b.second, row, juce::Justification::topLeft, 2);
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
    for (int i = 0; i < 5; ++i) { groups[size_t (i)] = std::make_unique<GroupTile> (i); addAndMakeVisible (*groups[size_t (i)]); }
    for (int i = 0; i < int (MixMacro::Count); ++i)
    {
        const auto m = MixMacro (i);
        macros[size_t (i)] = std::make_unique<MacroSlider> (m, [this, m] (float v) { controller.setMacro (m, v); });
        addAndMakeVisible (*macros[size_t (i)]);
    }
    railView.setViewedComponent (&railHolder, false);
    railView.setScrollBarsShown (true, false);
    addAndMakeVisible (railView);

    listenSheet = std::make_unique<ListenSheet> (controller);
    resultSheet = std::make_unique<ResultSheet> (controller, *this);
    addAndMakeVisible (tuneButton);
    addAndMakeVisible (advancedButton);
    addAndMakeVisible (resetMacrosButton);
    // The sheets are added last: sibling order is z-order.
    addChildComponent (*resultSheet);
    addChildComponent (*listenSheet);

    tuneButton.setCaps (true);
    tuneButton.setFontPx (19.0f);
    tuneButton.setIcon (Dine::Icon::Waveform);
    tuneButton.onClick = [this] { pressTune(); };
    advancedButton.setIcon (Dine::Icon::List);
    advancedButton.setFontPx (12.5f);
    advancedButton.onClick = [this] { if (onOpenAdvanced) onOpenAdvanced(); };
    resetMacrosButton.setFontPx (11.5f);
    resetMacrosButton.onClick = [this] { controller.resetMacros(); for (int i = 0; i < int (MixMacro::Count); ++i) macros[size_t (i)]->setValue (50.0f); };
    refresh();
}

MixPage::~MixPage() = default;

void MixPage::pressTune()
{
    if (controller.isListening()) { controller.abortTuneMix(); refreshTuneButton(); return; }
    controller.startTuneMix();
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
    const bool busy = stage == MixController::Stage::Listening || stage == MixController::Stage::Planning;
    tuneButton.setButtonText (busy ? "Cancel" : controller.getTuneCount() > 0 ? "Re-tune" : "Tune mix");
    tuneButton.setStyle (busy ? DineButton::Style::Standard : DineButton::Style::Filled);
    tuneButton.setEnabled (controller.isPrepared() && controller.getEngine().getNumStrips() > 0 && stage != MixController::Stage::Planning);
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
        auto row = std::make_unique<InputRow> (s.name, s.role, s.inputA + 1);
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
    for (int i = 0; i < 4; ++i)
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
        groups[4]->set (returns > 0, returns, peak, rms, clip, 0);
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
    const bool listenOn = stage == MixController::Stage::Listening || stage == MixController::Stage::Planning;
    const bool preview = stage == MixController::Stage::Preview && controller.hasPlan();
    if (listenSheet->isVisible() != listenOn) { listenSheet->setVisible (listenOn); if (listenOn) listenSheet->toFront (false); }
    if (resultSheet->isVisible() != preview) { resultSheet->setVisible (preview); if (preview) { resultSheet->toFront (false); resized(); } }
    if (listenOn) listenSheet->repaint();
    if (preview) resultSheet->refresh();
    if (stage != lastStage) { refreshTuneButton(); lastStage = stage; }
    for (int i = 0; i < int (MixMacro::Count); ++i) macros[size_t (i)]->setValue (controller.getMacros().get (MixMacro (i)));
    repaint();
}

MixPage::Layout MixPage::layout() const
{
    Layout l;
    auto b = getLocalBounds();
    l.rail = b.removeFromRight (Dine::Metric::rail);
    auto left = b.reduced (0, 20).withTrimmedLeft (24).withTrimmedRight (22);
    auto top = left.removeFromTop (76);
    l.tune = top.removeFromRight (268);
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
    g.fillRect (l.rail);
    g.setColour (Dine::hair);
    g.fillRect (float (l.rail.getX()), 0.0f, 0.5f, float (getHeight()));
    auto head = l.rail.reduced (12, 0).withY (12).withHeight (16);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (12.0f, 600));
    g.drawText ("Inputs", head.removeFromLeft (44), juce::Justification::centredLeft);
    g.setColour (Dine::ink3);
    g.setFont (Dine::mono (11.0f));
    g.drawText (juce::String (int (inputRows.size())), head, juce::Justification::centredLeft);

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
    auto groupRow = l.groups;
    const int gap = 10;
    const int w = (groupRow.getWidth() - gap * 4) / 5;
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
}

} // namespace livemix
