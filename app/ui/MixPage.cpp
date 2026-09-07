#include "MixPage.h"
#include "UI/LiveMixLookAndFeel.h"
#include "Core/DbUtils.h"

namespace livemix
{

namespace
{
    const char* kGroupNames[5] = { "DRUMS", "BASS", "MUSIC", "VOCALS", "FX" };
    const MixBus kGroupBus[5] = { MixBus::Drums, MixBus::Bass, MixBus::Music, MixBus::Vocals, MixBus::Master /* FX: returns */ };
}

// ------------------------------------------------------------------ GroupTile
class MixPage::GroupTile : public juce::Component
{
public:
    explicit GroupTile (int index) : group (index), meter (MeterComponent::Orientation::Horizontal) { addAndMakeVisible (meter); }

    void set (bool isUsed, int stripCount, float peakDb, float holdDb, bool clipped, int heardState /*0 none, 1 listening no, 2 heard*/)
    {
        used = isUsed; strips = stripCount; heard = heardState;
        meter.setLevels (peakDb, holdDb, clipped);
        meter.setVisible (used);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        LiveMixLookAndFeel::drawElevated (g, b, used ? Tokens::raised : Tokens::inset, used ? Tokens::hair2 : Tokens::hair, Tokens::Radius::card);
        auto r = getLocalBounds().reduced (16, 14);
        g.setColour (used ? Tokens::textHi : Tokens::textDim);
        g.setFont (LiveMixLookAndFeel::condensed (14.0f, 700, 0.08f));
        auto top = r.removeFromTop (20);
        g.drawText (kGroupNames[group], top, juce::Justification::centredLeft);
        if (heard == 2) LiveMixLookAndFeel::drawIcon (g, LiveMixLookAndFeel::Icon::Check, top.removeFromRight (16).toFloat(), Tokens::okText);
        else if (heard == 1) { g.setColour (Tokens::textDim); g.fillEllipse (top.removeFromRight (16).withSizeKeepingCentre (6, 6).toFloat()); }
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::body (12.0f));
        g.drawText (! used ? "not in this mix" : group == 4 ? juce::String (strips) + (strips == 1 ? " return" : " returns") : juce::String (strips) + (strips == 1 ? " input" : " inputs"),
                    r.removeFromTop (16), juce::Justification::centredLeft);
    }
    void resized() override { meter.setBounds (getLocalBounds().reduced (14, 12).removeFromBottom (8)); }

private:
    int group;
    bool used = false;
    int strips = 0;
    int heard = 0;
    MeterComponent meter;
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
        addAndMakeVisible (slider);
    }
    void setValue (float v) { slider.setValue (v, juce::dontSendNotification); repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (18);
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (13.0f, 700, 0.06f));
        g.drawText (MixMacros::name (macro), top, juce::Justification::centredLeft);
        const float v = float (slider.getValue());
        g.setColour (v == 50.0f ? Tokens::textDim : Tokens::accentText);
        g.setFont (LiveMixLookAndFeel::mono (11.0f));
        g.drawText (v == 50.0f ? juce::String ("as tuned") : (v < 50.0f ? juce::String (MixMacros::lowLabel (macro)) : juce::String (MixMacros::highLabel (macro))) + " " + juce::String (int (std::round (std::fabs (v - 50.0f) * 2.0f))) + "%",
                    top, juce::Justification::centredRight);
        auto labels = r.removeFromBottom (16);
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::body (11.5f));
        g.drawText (MixMacros::lowLabel (macro), labels, juce::Justification::centredLeft);
        g.drawText (MixMacros::highLabel (macro), labels, juce::Justification::centredRight);
    }
    void resized() override { auto r = getLocalBounds(); r.removeFromTop (18); r.removeFromBottom (14); slider.setBounds (r); }

private:
    MixMacro macro;
    juce::Slider slider;
    std::function<void (float)> changed;
};

// ------------------------------------------------------------------ ListenOverlay
class MixPage::ListenOverlay : public juce::Component
{
public:
    explicit ListenOverlay (MixController& c) : controller (c)
    {
        addAndMakeVisible (cancel);
        cancel.onClick = [this] { controller.abortTuneMix(); };
        setInterceptsMouseClicks (true, true);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Tokens::ground.withAlpha (0.86f));
        auto card = getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), 330);
        LiveMixLookAndFeel::drawSurface (g, card.toFloat(), Tokens::raised, Tokens::hairStrong, Tokens::Radius::card);
        auto r = card.reduced (28, 24);
        const bool waiting = controller.isWaitingForBand();
        const bool planning = controller.getStage() == MixController::Stage::Planning;
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (26.0f, 700, 0.04f));
        g.drawText (planning ? "BUILDING THE MIX..." : waiting ? "WAITING FOR THE BAND" : "LISTENING...", r.removeFromTop (34), juce::Justification::centredLeft);
        g.setColour (Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::body (13.5f));
        g.drawFittedText (waiting ? "Have the band play a song the way they normally would. DINELIVE starts as soon as it hears them."
                                  : "Keep playing. Every input is measured at once, then the mix is built around the lead vocal.", r.removeFromTop (40), juce::Justification::topLeft, 2);
        r.removeFromTop (10);
        auto bar = r.removeFromTop (8);
        LiveMixLookAndFeel::fillSurface (g, bar.toFloat(), Tokens::track, 4.0f);
        const float p = planning ? 1.0f : controller.getListenProgress();
        if (p > 0.0f) LiveMixLookAndFeel::fillSurface (g, bar.withWidth (juce::jmax (8, int (p * bar.getWidth()))).toFloat(), Tokens::accent, 4.0f);
        r.removeFromTop (18);
        for (int i = 0; i < 4; ++i)
        {
            auto row = r.removeFromTop (26);
            const bool used = controller.getEngine().isBusUsed (kGroupBus[i]);
            const bool heard = used && controller.busHeard (kGroupBus[i]);
            g.setColour (! used ? Tokens::textDim : heard ? Tokens::textHi : Tokens::textMid);
            g.setFont (LiveMixLookAndFeel::condensed (14.0f, 600, 0.04f));
            g.drawText (kGroupNames[i], row.removeFromLeft (120), juce::Justification::centredLeft);
            if (! used) { g.setColour (Tokens::textDim); g.setFont (LiveMixLookAndFeel::body (12.0f)); g.drawText ("not in this mix", row, juce::Justification::centredLeft); }
            else if (heard) LiveMixLookAndFeel::drawIcon (g, LiveMixLookAndFeel::Icon::Check, row.removeFromLeft (18).toFloat().reduced (1.0f), Tokens::okText);
            else { g.setColour (Tokens::textDim); g.setFont (LiveMixLookAndFeel::body (12.0f)); g.drawText ("waiting", row, juce::Justification::centredLeft); }
        }
    }
    void resized() override
    {
        auto card = getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), 330);
        cancel.setBounds (card.reduced (28, 24).removeFromBottom (34).removeFromRight (110));
    }
private:
    MixController& controller;
    FlatButton cancel { "CANCEL", FlatButton::Style::Outline };
};

// ------------------------------------------------------------------ PlanCard
class MixPage::PlanCard : public juce::Component
{
public:
    PlanCard (MixController& c, MixPage& p) : controller (c), page (p)
    {
        for (auto* b : { &before, &after, &keep, &revert, &review }) addAndMakeVisible (*b);
        before.setClickingTogglesState (false); after.setClickingTogglesState (false);
        before.onClick = [this] { controller.setCompare (MixController::Compare::Before); refresh(); };
        after.onClick  = [this] { controller.setCompare (MixController::Compare::After); refresh(); };
        keep.onClick   = [this] { controller.keepPlan(); if (page.onToast) page.onToast ("Kept. This is your mix now; RE-TUNE any time the band changes."); };
        revert.onClick = [this] { controller.revertPlan(); if (page.onToast) page.onToast ("Reverted to the previous mix."); };
        review.onClick = [this] { if (page.onOpenAdvanced) page.onOpenAdvanced(); };
    }
    void refresh()
    {
        const bool showingAfter = controller.getCompare() == MixController::Compare::After;
        before.setToggleState (! showingAfter, juce::dontSendNotification);
        after.setToggleState (showingAfter, juce::dontSendNotification);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        LiveMixLookAndFeel::drawElevated (g, b, Tokens::raised, Tokens::accentDim, Tokens::Radius::card);
        const auto* plan = controller.getPlan();
        if (plan == nullptr) return;
        auto r = getLocalBounds().reduced (20, 16);
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (20.0f, 700, 0.03f));
        g.drawText (plan->headline, r.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::body (13.0f));
        juce::String notes;
        for (const auto& n : plan->notes) notes += juce::String (n) + "  ";
        g.drawFittedText (notes.trim(), r.removeFromTop (40), juce::Justification::topLeft, 2);
        int shown = 0;
        for (const auto& rel : plan->relationships)
        {
            if (rel.changes.empty() && rel.kind != Recommendation::Kind::MixGain) continue;
            if (shown++ >= 3) break;
            auto line = r.removeFromTop (18);
            g.setColour (Tokens::accentText);
            g.setFont (LiveMixLookAndFeel::body (12.5f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) + rel.what, line, juce::Justification::centredLeft);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16).removeFromBottom (34);
        keep.setBounds (r.removeFromRight (110)); r.removeFromRight (8);
        revert.setBounds (r.removeFromRight (100)); r.removeFromRight (8);
        review.setBounds (r.removeFromRight (100));
        auto seg = r.removeFromLeft (200);
        before.setBounds (seg.removeFromLeft (100));
        after.setBounds (seg);
    }
private:
    MixController& controller;
    MixPage& page;
    FlatButton before { "BEFORE", FlatButton::Style::Segment }, after { "AFTER", FlatButton::Style::Segment };
    FlatButton keep { "KEEP", FlatButton::Style::Solid }, revert { "REVERT", FlatButton::Style::Outline }, review { "REVIEW", FlatButton::Style::Ghost };
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
    overlay = std::make_unique<ListenOverlay> (controller);
    addChildComponent (*overlay);
    card = std::make_unique<PlanCard> (controller, *this);
    addChildComponent (*card);
    addAndMakeVisible (tuneButton);
    addAndMakeVisible (advancedButton);
    addAndMakeVisible (resetMacrosButton);
    tuneButton.setFontPx (15.0f);
    tuneButton.onClick = [this] { pressTune(); };
    advancedButton.onClick = [this] { if (onOpenAdvanced) onOpenAdvanced(); };
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
    tuneButton.setButtonText (busy ? "CANCEL" : controller.getTuneCount() > 0 ? "RE-TUNE" : "TUNE MIX");
    tuneButton.setStyle (busy ? FlatButton::Style::Outline : FlatButton::Style::Solid);
    tuneButton.setEnabled (controller.isPrepared() && controller.getEngine().getNumStrips() > 0 && stage != MixController::Stage::Planning);
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
    health = controller.getMixHealthPercent();
    status = controller.getStatusText();
    const auto stage = controller.getStage();
    overlay->setVisible (stage == MixController::Stage::Listening || stage == MixController::Stage::Planning);
    if (overlay->isVisible()) overlay->repaint();
    const bool preview = stage == MixController::Stage::Preview && controller.hasPlan();
    if (card->isVisible() != preview) { card->setVisible (preview); resized(); }
    if (preview) card->refresh();
    if (stage != lastStage) { refreshTuneButton(); lastStage = stage; }
    for (int i = 0; i < int (MixMacro::Count); ++i) macros[size_t (i)]->setValue (controller.getMacros().get (MixMacro (i)));
    repaint();
}

void MixPage::paint (juce::Graphics& g)
{
    // Ambient is drawn by MainView; keep this transparent-ish with a soft local wash.
    auto area = AppStyle::contentArea (getLocalBounds());
    auto header = area.removeFromTop (88);

    g.setColour (Tokens::textLow);
    g.setFont (LiveMixLookAndFeel::condensed (11.5f, 600, 0.12f));
    g.drawText (juce::String (styleProfileName (controller.getSession().profile)).toUpperCase()
                    + "   " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "   "
                    + juce::String (mixPurposeName (controller.getSession().purpose)).toUpperCase(),
                header.removeFromTop (18), juce::Justification::centredLeft);

    auto healthRow = header.removeFromTop (44);
    g.setColour (Tokens::textHi);
    g.setFont (LiveMixLookAndFeel::condensed (34.0f, 700, 0.01f));
    const juce::String healthText = health > 0 ? "Mix health  " + juce::String (health) + "%"
                                               : "Mix health  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
    g.drawText (healthText, healthRow, juce::Justification::centredLeft);
    if (controller.getStage() == MixController::Stage::Mixed)
    {
        const juce::String ready = "READY";
        const float w = LiveMixLookAndFeel::chipWidth (ready, 11.0f, true);
        auto chip = healthRow.removeFromRight (int (w)).withSizeKeepingCentre (int (w), 26).toFloat();
        auto icon = LiveMixLookAndFeel::Icon::Check;
        LiveMixLookAndFeel::drawChip (g, chip, ready, Tokens::okText, Tokens::okDeep, Tokens::okDeep.withAlpha (0.28f), 11.0f, &icon);
    }
    g.setColour (Tokens::textMid);
    g.setFont (LiveMixLookAndFeel::body (14.0f));
    g.drawText (status, header, juce::Justification::centredLeft);

    auto rest = area;
    rest.removeFromTop (12 + 104 + 28);
    if (! card->isVisible())
    {
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::condensed (11.5f, 600, 0.12f));
        rest.removeFromTop (60 + 28);
        g.drawText ("SHAPE THE MIX", rest.removeFromTop (18), juce::Justification::centredLeft);
    }
}

void MixPage::resized()
{
    auto area = AppStyle::contentArea (getLocalBounds());
    area.removeFromTop (88 + 12);
    auto row = area.removeFromTop (104);
    const int gap = 12;
    const int w = (row.getWidth() - gap * 4) / 5;
    for (auto& t : groups) { t->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
    area.removeFromTop (28);
    if (card->isVisible())
    {
        card->setBounds (area.removeFromTop (196));
        area.removeFromTop (16);
    }
    else
    {
        auto actions = area.removeFromTop (60);
        tuneButton.setBounds (actions.removeFromLeft (200).withSizeKeepingCentre (200, 48));
        actions.removeFromLeft (12);
        advancedButton.setBounds (actions.removeFromLeft (130).withSizeKeepingCentre (130, 40));
        area.removeFromTop (24);
    }
    tuneButton.setVisible (! card->isVisible());
    advancedButton.setVisible (! card->isVisible());
    auto caption = area.removeFromTop (18);
    resetMacrosButton.setBounds (caption.removeFromRight (80));
    area.removeFromTop (10);
    const int sliderGap = 6;
    const int sliderH = juce::jlimit (44, 58, (area.getHeight() - sliderGap * (int (MixMacro::Count) - 1)) / int (MixMacro::Count));
    for (auto& m : macros) { m->setBounds (area.removeFromTop (sliderH)); area.removeFromTop (sliderGap); }
    overlay->setBounds (getLocalBounds());
}

} // namespace livemix
