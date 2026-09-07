#include "AdvancedPage.h"
#include "UI/LiveMixLookAndFeel.h"
#include "Core/DbUtils.h"

namespace livemix
{

namespace
{
    juce::String db1 (float v) { return (v >= 0.0f ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (std::fabs (v), 1) + " dB"; }
}

// ------------------------------------------------------------------ Row
class AdvancedPage::Row : public juce::Button
{
public:
    Row (const juce::String& title, const juce::String& sub, bool bus) : juce::Button (title), name (title), subtitle (sub), isBus (bus), meter (MeterComponent::Orientation::Horizontal)
    {
        addAndMakeVisible (meter);
        meter.setInterceptsMouseClicks (false, false);
    }
    void set (float peakDb, float rmsDb, bool clipped, const juce::String& levelText, bool muted, bool heard)
    {
        meter.setLevels (peakDb, rmsDb, clipped);
        level = levelText; mute = muted; wasHeard = heard;
        repaint();
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        LiveMixLookAndFeel::drawSurface (g, b, on ? Tokens::accentDim.withAlpha (0.3f) : (over ? Tokens::raised : isBus ? Tokens::inset : Tokens::panel), on ? Tokens::accentStroke : Tokens::hairRow, Tokens::Radius::control);
        auto r = getLocalBounds().reduced (12, 6);
        auto top = r.removeFromTop (18);
        g.setColour (mute ? Tokens::textDim : Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (13.5f, isBus ? 700 : 600, 0.03f));
        g.drawText (name, top.removeFromLeft (top.getWidth() / 2), juce::Justification::centredLeft);
        g.setColour (Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::mono (11.5f));
        g.drawText (mute ? "MUTED" : level, top, juce::Justification::centredRight);
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::body (11.0f));
        g.drawText (subtitle, r.removeFromTop (14), juce::Justification::centredLeft);
    }
    void resized() override { meter.setBounds (getLocalBounds().reduced (12, 6).removeFromBottom (6)); }
    juce::String name, subtitle, level;
    bool isBus, mute = false, wasHeard = true;
    MeterComponent meter;
};

// ------------------------------------------------------------------ Detail
class AdvancedPage::Detail : public juce::Component
{
public:
    explicit Detail (MixController& c) : controller (c)
    {
        auto setupSlider = [] (juce::Slider& s, double lo, double hi, double step, double def)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (lo, hi, step);
            s.setDoubleClickReturnValue (true, def);
        };
        setupSlider (gain, -24.0, 24.0, 0.5, 0.0);
        setupSlider (fader, -60.0, 12.0, 0.5, 0.0);
        for (auto& s : sends) setupSlider (s, -60.0, 6.0, 0.5, -60.0);
        gain.onValueChange = [this] { if (! updating && ! sel.isBus) controller.setStripInputGain (sel.strip, float (gain.getValue())); };
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (sel.isBus) controller.setBusFader (sel.bus, float (fader.getValue()));
            else controller.setStripFader (sel.strip, float (fader.getValue()));
        };
        for (int f = 0; f < int (FxSlot::Count); ++f)
            sends[size_t (f)].onValueChange = [this, f] { if (! updating && ! sel.isBus) controller.setStripSend (sel.strip, FxSlot (f), float (sends[size_t (f)].getValue())); };
        muteButton.setClickingTogglesState (false);
        muteButton.onClick = [this] { if (! sel.isBus) controller.setStripMute (sel.strip, ! controller.getKept().strips[size_t (sel.strip)].mute); };
        addAndMakeVisible (gain); addAndMakeVisible (fader); addAndMakeVisible (muteButton);
        for (auto& s : sends) addChildComponent (s);
        report.setMultiLine (true, true);
        report.setReadOnly (true);
        report.setScrollbarsShown (true);
        report.setCaretVisible (false);
        report.setFont (LiveMixLookAndFeel::body (12.5f));
        report.setColour (juce::TextEditor::backgroundColourId, Tokens::inset);
        report.setColour (juce::TextEditor::textColourId, Tokens::textMid);
        report.setColour (juce::TextEditor::outlineColourId, Tokens::hair);
        report.setColour (juce::TextEditor::focusedOutlineColourId, Tokens::hair);
        addAndMakeVisible (report);
    }

    void show (const Selection& s) { sel = s; rebuildReport(); resized(); refresh(); }

    void refresh()
    {
        updating = true;
        const auto& kept = controller.getBase();
        if (sel.isBus)
        {
            fader.setValue (kept.buses[size_t (sel.bus)].faderDb, juce::dontSendNotification);
            gain.setVisible (false); muteButton.setVisible (false);
            for (auto& s : sends) s.setVisible (false);
        }
        else if (sel.strip >= 0 && sel.strip < kept.numStrips)
        {
            const auto& st = kept.strips[size_t (sel.strip)];
            gain.setVisible (true); muteButton.setVisible (true);
            gain.setValue (st.inputGainDb, juce::dontSendNotification);
            fader.setValue (st.faderDb, juce::dontSendNotification);
            muteButton.setButtonText (st.mute ? "MUTED" : "MUTE");
            muteButton.setStyle (st.mute ? FlatButton::Style::Accent : FlatButton::Style::Outline);
            const auto& graph = controller.getGraph();
            for (int f = 0; f < int (FxSlot::Count); ++f)
            {
                const bool has = graph.fxUsed[size_t (f)] && st.sendDb[size_t (f)] > kSilenceDb;
                sends[size_t (f)].setVisible (has);
                if (has) sends[size_t (f)].setValue (st.sendDb[size_t (f)], juce::dontSendNotification);
            }
        }
        updating = false;
        repaint();
    }

    void rebuildReport()
    {
        juce::String text;
        const auto* plan = controller.getPlan();
        auto add = [&] (const Recommendation& r)
        {
            text += juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) + r.what + "\n   " + r.why + "\n\n";
        };
        if (sel.isBus)
        {
            if (plan != nullptr && plan->buses[size_t (sel.bus)].tune.valid)
            {
                text += juce::String (plan->buses[size_t (sel.bus)].tune.headline) + "\n\n";
                for (const auto& r : plan->buses[size_t (sel.bus)].tune.report.items) add (r);
            }
            else text = "No Tune Mix yet for this bus. It runs on the profile's baseline.";
        }
        else if (plan != nullptr && sel.strip >= 0 && sel.strip < int (plan->strips.size()))
        {
            const auto& sp = plan->strips[size_t (sel.strip)];
            text += juce::String (sp.tune.headline) + "\n\n";
            for (const auto& r : sp.mixItems) add (r);
            for (const auto& r : sp.tune.report.items) add (r);
        }
        else text = "No Tune Mix yet. This input runs on the profile's baseline for its source.";
        report.setText (text.trim(), false);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        LiveMixLookAndFeel::drawSurface (g, b, Tokens::panel, Tokens::hair, Tokens::Radius::card);
        auto r = getLocalBounds().reduced (18, 14);
        const auto& graph = controller.getGraph();
        juce::String title, sub;
        if (sel.isBus) { title = mixBusName (sel.bus); sub = sel.bus == MixBus::Master ? juce::String (mixPurposeName (controller.getSession().purpose)) + " output" : "bus"; }
        else if (sel.strip >= 0 && sel.strip < graph.numStrips())
        {
            const auto& s = graph.strips[size_t (sel.strip)];
            title = s.name; sub = juce::String (channelRoleName (s.role)) + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + (s.numChannels() == 2 ? "stereo" : "mono") + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + mixBusName (s.bus);
        }
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (20.0f, 700, 0.03f));
        g.drawText (title.toUpperCase(), r.removeFromTop (26), juce::Justification::centredLeft);
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::body (12.0f));
        g.drawText (sub, r.removeFromTop (16), juce::Justification::centredLeft);
        r.removeFromTop (12);
        g.setColour (Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::mono (11.5f));
        if (! sel.isBus)
        {
            auto row = r.removeFromTop (24);
            g.drawText ("INPUT GAIN", row.removeFromLeft (90), juce::Justification::centredLeft);
            g.drawText (db1 (float (gain.getValue())), row.removeFromRight (70), juce::Justification::centredRight);
        }
        auto row = r.removeFromTop (24);
        g.drawText ("LEVEL", row.removeFromLeft (90), juce::Justification::centredLeft);
        g.drawText (db1 (float (fader.getValue())), row.removeFromRight (70), juce::Justification::centredRight);
        if (! sel.isBus)
            for (int f = 0; f < int (FxSlot::Count); ++f)
            {
                if (! sends[size_t (f)].isVisible()) continue;
                auto sr = r.removeFromTop (24);
                g.drawText (juce::String (fxSlotName (FxSlot (f))).toUpperCase(), sr.removeFromLeft (110), juce::Justification::centredLeft);
                g.drawText (db1 (float (sends[size_t (f)].getValue())), sr.removeFromRight (70), juce::Justification::centredRight);
            }
        if (sel.isBus && sel.bus == MixBus::Master)
        {
            const auto& loud = controller.getEngine().getBus (MixBus::Master).getLoudness();
            auto lr = r.removeFromTop (24);
            g.drawText ("LOUDNESS", lr.removeFromLeft (90), juce::Justification::centredLeft);
            const float st = loud.getShortTermLufs();
            g.drawText (st > -100.0f ? juce::String (st, 1) + " LUFS  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  peak " + juce::String (loud.getTruePeakDb(), 1) + " dBTP" : "waiting for signal", lr, juce::Justification::centredRight);
        }
        r.removeFromTop (8);
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::condensed (11.0f, 600, 0.08f));
        g.drawText ("WHAT TUNE MIX DECIDED, AND WHY", r.removeFromTop (16), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 14);
        r.removeFromTop (26 + 16 + 12);
        auto slot = [&] (juce::Slider& s) { auto row = r.removeFromTop (24); row.removeFromLeft (90); row.removeFromRight (74); s.setBounds (row); };
        if (! sel.isBus) slot (gain);
        {
            auto row = r.removeFromTop (24);
            row.removeFromLeft (90);
            row.removeFromRight (74);
            if (! sel.isBus) { muteButton.setBounds (row.removeFromRight (70)); row.removeFromRight (8); }
            fader.setBounds (row);
        }
        if (! sel.isBus)
            for (auto& s : sends) if (s.isVisible()) { auto row = r.removeFromTop (24); row.removeFromLeft (110); row.removeFromRight (74); s.setBounds (row); }
        if (sel.isBus && sel.bus == MixBus::Master) r.removeFromTop (24);
        r.removeFromTop (8 + 16 + 6);
        report.setBounds (r);
    }

private:
    MixController& controller;
    Selection sel;
    bool updating = false;
    juce::Slider gain, fader;
    std::array<juce::Slider, int (FxSlot::Count)> sends;
    FlatButton muteButton { "MUTE", FlatButton::Style::Outline };
    juce::TextEditor report;
};

// ------------------------------------------------------------------ AdvancedPage
AdvancedPage::AdvancedPage (MixController& c) : controller (c)
{
    viewport.setViewedComponent (&listHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    detail = std::make_unique<Detail> (controller);
    addAndMakeVisible (*detail);
    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };
    rebuild();
}

AdvancedPage::~AdvancedPage() = default;

void AdvancedPage::rebuild()
{
    rows.clear();
    listHolder.removeAllChildren();
    const auto& graph = controller.getGraph();
    for (int i = 0; i < graph.numStrips(); ++i)
    {
        const auto& s = graph.strips[size_t (i)];
        auto row = std::make_unique<Row> (s.name, juce::String (channelRoleName (s.role)) + "  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + "  " + mixBusName (s.bus), false);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { select (i); };
        listHolder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        if (! controller.isPrepared() || ! controller.getEngine().isBusUsed (MixBus (b))) continue;
        auto row = std::make_unique<Row> (mixBusName (MixBus (b)), MixBus (b) == MixBus::Master ? "output" : juce::String (graph.stripsOnBus (MixBus (b))) + " inputs  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + "  MASTER", true);
        row->setClickingTogglesState (false);
        row->onClick = [this, b] { selectBus (MixBus (b)); };
        listHolder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    builtForStrips = graph.numStrips();
    if (! selection.isBus && (selection.strip < 0 || selection.strip >= graph.numStrips())) { if (graph.numStrips() > 0) select (0); else selectBus (MixBus::Master); }
    else if (selection.isBus) selectBus (selection.bus);
    else select (selection.strip);
    resized();
}

void AdvancedPage::select (int strip)
{
    selection.isBus = false;
    selection.strip = strip;
    for (int i = 0; i < int (rows.size()); ++i) rows[size_t (i)]->setToggleState (! rows[size_t (i)]->isBus && i == strip, juce::dontSendNotification);
    detail->show (selection);
}

void AdvancedPage::selectBus (MixBus bus)
{
    selection.isBus = true;
    selection.bus = bus;
    for (auto& r : rows) r->setToggleState (r->isBus && r->name == mixBusName (bus), juce::dontSendNotification);
    detail->show (selection);
}

void AdvancedPage::refresh()
{
    if (! controller.isPrepared()) return;
    const auto& engine = controller.getEngine();
    const auto& graph = engine.getGraph();
    if (graph.numStrips() != builtForStrips) rebuild();
    const auto& kept = controller.getBase();
    int r = 0;
    for (int i = 0; i < graph.numStrips() && r < int (rows.size()); ++i, ++r)
    {
        const auto& m = engine.getStrip (i).getOutputMeter();
        const auto& st = kept.strips[size_t (i)];
        rows[size_t (r)]->set (m.consumeMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped(), db1 (st.faderDb) + (st.inputGainDb != 0.0f ? "  gain " + db1 (st.inputGainDb) : juce::String()), st.mute, true);
    }
    for (int b = 0; b < int (MixBus::Count) && r < int (rows.size()); ++b)
    {
        if (! engine.isBusUsed (MixBus (b))) continue;
        const auto& m = engine.getBus (MixBus (b)).getOutputMeter();
        rows[size_t (r)]->set (m.consumeMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped(), db1 (kept.buses[size_t (b)].faderDb), kept.buses[size_t (b)].mute, true);
        ++r;
    }
    detail->refresh();
}

void AdvancedPage::paint (juce::Graphics& g)
{
    g.fillAll (Tokens::ground);
    auto area = getLocalBounds().reduced (AppStyle::kMargin);
    auto header = area.removeFromTop (40);
    g.setColour (Tokens::textHi);
    g.setFont (LiveMixLookAndFeel::condensed (22.0f, 700, 0.03f));
    g.drawText ("ADVANCED", header.removeFromLeft (200), juce::Justification::centredLeft);
    g.setColour (Tokens::textLow);
    g.setFont (LiveMixLookAndFeel::body (12.5f));
    g.drawText ("Every input and bus. Simple and Advanced share one state: nothing here changes the sound until you move it.", header, juce::Justification::centredLeft);
}

void AdvancedPage::resized()
{
    auto area = getLocalBounds().reduced (AppStyle::kMargin);
    auto header = area.removeFromTop (40);
    backButton.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, 32));
    area.removeFromTop (12);
    auto left = area.removeFromLeft (juce::jmin (360, area.getWidth() / 2));
    area.removeFromLeft (16);
    viewport.setBounds (left);
    const int rowH = 52, gap = 6;
    const int total = int (rows.size()) * (rowH + gap);
    listHolder.setSize (left.getWidth() - (total > left.getHeight() ? 12 : 0), total);
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), rowH); y += rowH + gap; }
    detail->setBounds (area);
}

} // namespace livemix
