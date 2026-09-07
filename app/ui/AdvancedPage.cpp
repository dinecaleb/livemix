#include "AdvancedPage.h"
#include "UI/LiveMixLookAndFeel.h"
#include "Core/DbUtils.h"

namespace livemix
{

namespace
{
    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1) + " dB";
    }

    // 1-based device channel(s) the strip is patched to ("in 4", "in 9/10").
    juce::String deviceInLabel (const StripRoute& s)
    {
        if (s.inputA < 0) return {};
        if (s.inputB >= 0)
            return "in " + juce::String (s.inputA + 1) + "/" + juce::String (s.inputB + 1);
        return "in " + juce::String (s.inputA + 1);
    }

    // Family tint for the left rail — muted so the list stays readable, distinct so
    // drums / vocals / buses separate at a glance.
    juce::Colour busTint (MixBus b) noexcept
    {
        switch (b)
        {
            case MixBus::Drums:  return Tokens::warn;
            case MixBus::Bass:   return Tokens::accentText;
            case MixBus::Music:  return Tokens::accentStroke;
            case MixBus::Vocals: return Tokens::okText;
            case MixBus::Master: return Tokens::textHi;
            case MixBus::Count:
            default:             return Tokens::mark;
        }
    }

    const char* kindLabel (bool isBus, MixBus bus) noexcept
    {
        if (! isBus) return "CH";
        return bus == MixBus::Master ? "OUT" : "BUS";
    }

    // Short send labels so they fit the control column without colliding with values.
    const char* sendLabel (FxSlot f) noexcept
    {
        switch (f)
        {
            case FxSlot::VocalPlate: return "PLATE";
            case FxSlot::VocalDelay: return "DELAY";
            case FxSlot::BgvHall:    return "HALL";
            case FxSlot::SnarePlate: return "SNR PLATE";
            case FxSlot::DrumRoom:   return "DRM ROOM";
            case FxSlot::Count:
            default:                 return "?";
        }
    }
}

// ------------------------------------------------------------------ SectionHeader
class AdvancedPage::SectionHeader : public juce::Component
{
public:
    SectionHeader (const juce::String& title, const juce::String& count, MixBus bus, bool busesSection)
        : label (title), countText (count), tint (busTint (bus)), isBuses (busesSection)
    {
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (4, 0);
        auto rail = r.removeFromLeft (3).toFloat().reduced (0.0f, 6.0f);
        g.setColour (tint.withAlpha (isBuses ? 0.85f : 0.55f));
        g.fillRoundedRectangle (rail, 1.5f);
        r.removeFromLeft (8);
        g.setColour (isBuses ? Tokens::textHi : Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::condensed (11.0f, 700, 0.12f));
        g.drawText (label, r.removeFromLeft (r.getWidth() - 70), juce::Justification::centredLeft);
        g.setColour (Tokens::textDim);
        g.setFont (LiveMixLookAndFeel::mono (10.0f));
        g.drawText (countText, r, juce::Justification::centredRight);
    }

private:
    juce::String label, countText;
    juce::Colour tint;
    bool isBuses = false;
};

// ------------------------------------------------------------------ Row
class AdvancedPage::Row : public juce::Button
{
public:
    enum class Kind { Channel, Bus, Master };

    Row (const juce::String& title, const juce::String& sub, Kind k, MixBus busFamily, int stripIndex = -1,
         const juce::String& deviceIn = {})
        : juce::Button (title),
          name (title),
          subtitle (sub),
          inputLabel (deviceIn),
          kind (k),
          bus (busFamily),
          strip (stripIndex),
          meter (MeterComponent::Orientation::Horizontal)
    {
        addAndMakeVisible (meter);
        meter.setInterceptsMouseClicks (false, false);
    }

    void set (float peakDb, float rmsDb, bool clipped, const juce::String& levelText, bool muted)
    {
        meter.setLevels (peakDb, rmsDb, clipped);
        level = levelText;
        mute = muted;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        const bool isChannel = kind == Kind::Channel;
        auto outer = getLocalBounds();
        if (isChannel) outer = outer.withTrimmedLeft (10);
        auto b = outer.toFloat().reduced (0.5f);

        const juce::Colour fill = on ? Tokens::accentDim.withAlpha (0.35f)
                                     : (over ? Tokens::raised
                                             : (isChannel ? Tokens::panel : Tokens::inset));
        const juce::Colour stroke = on ? Tokens::accentStroke
                                       : (isChannel ? Tokens::hairRow : Tokens::hairStrong);
        LiveMixLookAndFeel::drawSurface (g, b, fill, stroke, Tokens::Radius::control);

        // Family colour rail — thicker on buses so they read as group masters.
        auto rail = b.removeFromLeft (isChannel ? 3.0f : 5.0f).reduced (0.0f, 4.0f);
        g.setColour (busTint (bus).withAlpha (on ? 1.0f : (isChannel ? 0.55f : 0.9f)));
        g.fillRoundedRectangle (rail, 1.5f);

        auto r = outer.reduced (isChannel ? 12 : 14, isChannel ? 6 : 8);
        r.removeFromLeft (isChannel ? 4 : 6);

        auto top = r.removeFromTop (isChannel ? 16 : 18);
        const char* chip = kindLabel (kind != Kind::Channel, bus);
        const float chipW = LiveMixLookAndFeel::chipWidth (chip, 9.0f);
        auto chipBounds = top.removeFromLeft (int (chipW)).toFloat().withSizeKeepingCentre (chipW, 16.0f);
        const juce::Colour chipFg = kind == Kind::Master ? Tokens::textHi
                                                        : (kind == Kind::Bus ? busTint (bus) : Tokens::textMid);
        const juce::Colour chipBg = kind == Kind::Channel ? Tokens::inset
                                                         : busTint (bus).withAlpha (0.12f);
        LiveMixLookAndFeel::drawChip (g, chipBounds, chip, chipFg,
                                      kind == Kind::Channel ? Tokens::hair : chipFg.withAlpha (0.45f),
                                      chipBg, 9.0f);
        top.removeFromLeft (6);
        if (isChannel && inputLabel.isNotEmpty())
        {
            const float inW = LiveMixLookAndFeel::chipWidth (inputLabel, 9.0f);
            auto inBounds = top.removeFromLeft (int (inW)).toFloat().withSizeKeepingCentre (inW, 16.0f);
            LiveMixLookAndFeel::drawChip (g, inBounds, inputLabel, Tokens::accentText,
                                          Tokens::accent.withAlpha (0.45f), Tokens::accentDim.withAlpha (0.35f), 9.0f);
            top.removeFromLeft (8);
        }
        else top.removeFromLeft (2);

        g.setColour (mute ? Tokens::textDim : Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (isChannel ? 13.0f : 14.5f, isChannel ? 600 : 700, 0.04f));
        g.drawText (name, top.removeFromLeft (top.getWidth() / 2), juce::Justification::centredLeft);

        g.setColour (mute ? Tokens::critText : Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::mono (11.0f));
        g.drawText (mute ? "MUTED" : level, top, juce::Justification::centredRight);

        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::body (10.5f));
        g.drawText (subtitle, r.removeFromTop (14), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        if (kind == Kind::Channel) bounds = bounds.withTrimmedLeft (10);
        const int insetX = kind == Kind::Channel ? 12 : 14;
        const int insetY = kind == Kind::Channel ? 6 : 8;
        meter.setBounds (bounds.reduced (insetX, insetY).withTrimmedLeft (6).removeFromBottom (6));
    }

    juce::String name, subtitle, inputLabel, level;
    Kind kind;
    MixBus bus;
    int strip = -1;
    bool mute = false;
    MeterComponent meter;
};

// ------------------------------------------------------------------ Detail
class AdvancedPage::Detail : public juce::Component
{
public:
    explicit Detail (MixController& c) : controller (c), meter (MeterComponent::Orientation::Vertical)
    {
        auto setupSlider = [] (juce::Slider& s, double lo, double hi, double step, double def, bool vertical)
        {
            s.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (lo, hi, step);
            s.setDoubleClickReturnValue (true, def);
        };
        setupSlider (gain, -24.0, 24.0, 0.5, 0.0, false);
        setupSlider (fader, -60.0, 12.0, 0.5, 0.0, true);
        for (auto& s : sends) setupSlider (s, -60.0, 6.0, 0.5, -60.0, false);

        gain.onValueChange = [this] { if (! updating && ! sel.isBus) controller.setStripInputGain (sel.strip, float (gain.getValue())); };
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (sel.isBus) controller.setBusFader (sel.bus, float (fader.getValue()));
            else controller.setStripFader (sel.strip, float (fader.getValue()));
            repaint();
        };
        for (int f = 0; f < int (FxSlot::Count); ++f)
            sends[size_t (f)].onValueChange = [this, f]
            {
                if (! updating && ! sel.isBus)
                    controller.setStripSend (sel.strip, FxSlot (f), float (sends[size_t (f)].getValue()));
            };

        muteButton.setClickingTogglesState (false);
        muteButton.onClick = [this]
        {
            if (! sel.isBus)
                controller.setStripMute (sel.strip, ! controller.getKept().strips[size_t (sel.strip)].mute);
        };

        addAndMakeVisible (gain);
        addAndMakeVisible (fader);
        addAndMakeVisible (muteButton);
        addAndMakeVisible (meter);
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

    void show (const Selection& s)
    {
        sel = s;
        rebuildReport();
        refresh(); // sets control visibility first
        resized(); // then lays them out (mute used to land on a send / the report title)
    }

    void refresh()
    {
        updating = true;
        bool layoutChanged = false;
        auto setVis = [&] (juce::Component& c, bool on)
        {
            if (c.isVisible() != on) { c.setVisible (on); layoutChanged = true; }
        };
        const auto& kept = controller.getBase();
        if (sel.isBus)
        {
            fader.setValue (kept.buses[size_t (sel.bus)].faderDb, juce::dontSendNotification);
            setVis (gain, false);
            setVis (muteButton, false);
            for (auto& s : sends) setVis (s, false);
            if (controller.isPrepared())
            {
                // Non-consuming read: the channel list owns consumeMaxPeakDb each tick.
                const auto& m = controller.getEngine().getBus (sel.bus).getOutputMeter();
                meter.setLevels (m.getMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped());
            }
        }
        else if (sel.strip >= 0 && sel.strip < kept.numStrips)
        {
            const auto& st = kept.strips[size_t (sel.strip)];
            setVis (gain, true);
            setVis (muteButton, true);
            gain.setValue (st.inputGainDb, juce::dontSendNotification);
            fader.setValue (st.faderDb, juce::dontSendNotification);
            muteButton.setButtonText (st.mute ? "MUTED" : "MUTE");
            muteButton.setStyle (st.mute ? FlatButton::Style::Accent : FlatButton::Style::Outline);
            const auto& graph = controller.getGraph();
            for (int f = 0; f < int (FxSlot::Count); ++f)
            {
                const bool has = graph.fxUsed[size_t (f)] && st.sendDb[size_t (f)] > kSilenceDb;
                setVis (sends[size_t (f)], has);
                if (has) sends[size_t (f)].setValue (st.sendDb[size_t (f)], juce::dontSendNotification);
            }
            if (controller.isPrepared())
            {
                const auto& m = controller.getEngine().getStrip (sel.strip).getOutputMeter();
                meter.setLevels (m.getMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped());
            }
        }
        updating = false;
        if (layoutChanged) resized();
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
        LiveMixLookAndFeel::drawElevated (g, b, Tokens::panel, Tokens::hair, Tokens::Radius::card);

        auto r = getLocalBounds().reduced (18, 14);
        // Leave room for the console strip on the right.
        r.removeFromRight (92);

        const auto& graph = controller.getGraph();
        juce::String title, sub;
        MixBus family = MixBus::Master;
        const char* chip = "CH";
        if (sel.isBus)
        {
            family = sel.bus;
            chip = kindLabel (true, sel.bus);
            title = mixBusName (sel.bus);
            if (sel.bus == MixBus::Master)
                sub = juce::String (mixPurposeName (controller.getSession().purpose)) + "  " + Glyph::dot() + "  stereo output";
            else
                sub = juce::String ("Group bus") + "  " + Glyph::dot() + "  stereo  " + Glyph::dot() + "  "
                      + juce::String (graph.stripsOnBus (sel.bus)) + " inputs  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + "  MASTER";
        }
        else if (sel.strip >= 0 && sel.strip < graph.numStrips())
        {
            const auto& s = graph.strips[size_t (sel.strip)];
            family = s.bus;
            chip = "CH";
            title = s.name;
            const auto in = deviceInLabel (s);
            sub = (in.isNotEmpty() ? in + "  " + Glyph::dot() + "  " : juce::String())
                  + juce::String (channelRoleName (s.role)) + "  " + Glyph::dot() + "  "
                  + (s.numChannels() == 2 ? "stereo" : "mono") + "  " + Glyph::dot() + "  "
                  + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + "  " + mixBusName (s.bus);
        }

        auto head = r.removeFromTop (28);
        const float chipW = LiveMixLookAndFeel::chipWidth (chip, 10.0f);
        auto chipBounds = head.removeFromLeft (int (chipW)).toFloat().withSizeKeepingCentre (chipW, 18.0f);
        const juce::Colour tint = busTint (family);
        LiveMixLookAndFeel::drawChip (g, chipBounds, chip,
                                      sel.isBus ? tint : Tokens::textMid,
                                      sel.isBus ? tint.withAlpha (0.5f) : Tokens::hair,
                                      sel.isBus ? tint.withAlpha (0.12f) : Tokens::inset, 10.0f);
        head.removeFromLeft (10);
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (20.0f, 700, 0.03f));
        g.drawText (title.toUpperCase(), head, juce::Justification::centredLeft);

        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::body (12.0f));
        g.drawText (sub, r.removeFromTop (16), juce::Justification::centredLeft);
        r.removeFromTop (14);

        // Control labels (sliders sit on top via resized). Mute lives on the gain row
        // so it never lands on a send or the report title when sends appear later.
        g.setColour (Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::mono (11.5f));
        if (! sel.isBus)
        {
            auto row = r.removeFromTop (28);
            g.drawText ("INPUT GAIN", row.removeFromLeft (90), juce::Justification::centredLeft);
            row.removeFromRight (78); // mute button
            g.drawText (db1 (float (gain.getValue())), row.removeFromRight (70), juce::Justification::centredRight);
        }
        if (! sel.isBus)
            for (int f = 0; f < int (FxSlot::Count); ++f)
            {
                if (! sends[size_t (f)].isVisible()) continue;
                auto sr = r.removeFromTop (24);
                g.drawText (sendLabel (FxSlot (f)), sr.removeFromLeft (90), juce::Justification::centredLeft);
                g.drawText (db1 (float (sends[size_t (f)].getValue())), sr.removeFromRight (70), juce::Justification::centredRight);
            }
        if (sel.isBus && sel.bus == MixBus::Master)
        {
            const auto& loud = controller.getEngine().getBus (MixBus::Master).getLoudness();
            auto lr = r.removeFromTop (24);
            g.drawText ("LOUDNESS", lr.removeFromLeft (90), juce::Justification::centredLeft);
            const float st = loud.getShortTermLufs();
            g.drawText (st > -100.0f ? juce::String (st, 1) + " LUFS  " + Glyph::dot() + "  peak " + juce::String (loud.getTruePeakDb(), 1) + " dBTP"
                                     : "waiting for signal",
                        lr, juce::Justification::centredRight);
        }

        r.removeFromTop (10);
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::condensed (11.0f, 600, 0.08f));
        g.drawText ("WHAT TUNE MIX DECIDED, AND WHY", r.removeFromTop (16), juce::Justification::centredLeft);

        // Console strip chrome (meter + fader live in this column).
        auto console = getLocalBounds().reduced (18, 14).removeFromRight (78);
        LiveMixLookAndFeel::drawSurface (g, console.toFloat(), Tokens::inset, Tokens::hairStrong, Tokens::Radius::control);
        auto consoleInner = console.reduced (8, 10);
        g.setColour (Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::condensed (10.0f, 600, 0.1f));
        g.drawText ("LEVEL", consoleInner.removeFromTop (14), juce::Justification::centred);
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::mono (12.0f));
        g.drawText (db1 (float (fader.getValue())), consoleInner.removeFromBottom (18), juce::Justification::centred);
        // Scale marks beside the fader column.
        auto marks = consoleInner;
        marks.removeFromLeft (marks.getWidth() / 2);
        g.setColour (Tokens::textDim);
        g.setFont (LiveMixLookAndFeel::mono (9.0f));
        const float marksH = float (marks.getHeight());
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            // Fader range [-60, +12]: +12 at top, -60 at bottom.
            const float yNorm = (12.0f - db) / (12.0f - (-60.0f));
            const int y = marks.getY() + int (yNorm * marksH);
            g.drawText (db > 0 ? "+" + juce::String (int (db)) : juce::String (int (db)),
                        marks.getX(), y - 6, marks.getWidth(), 12, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (18, 14);
        auto console = bounds.removeFromRight (78);
        bounds.removeFromRight (14);

        auto consoleInner = console.reduced (8, 10);
        consoleInner.removeFromTop (14);
        consoleInner.removeFromBottom (18);
        auto meterCol = consoleInner.removeFromLeft (consoleInner.getWidth() / 2).reduced (2, 0);
        auto faderCol = consoleInner.reduced (2, 0);
        meter.setBounds (meterCol);
        fader.setBounds (faderCol);

        auto r = bounds;
        r.removeFromTop (28 + 16 + 14);

        auto slot = [&] (juce::Slider& s, int labelW, int rightReserve)
        {
            auto row = r.removeFromTop (24);
            row.removeFromLeft (labelW);
            row.removeFromRight (rightReserve);
            s.setBounds (row);
        };
        if (! sel.isBus)
        {
            // Gain + mute share one row (mute on the right).
            auto row = r.removeFromTop (28);
            row.removeFromLeft (90);
            muteButton.setBounds (row.removeFromRight (70).withHeight (26).withY (row.getY() + 1));
            row.removeFromRight (8);
            row.removeFromRight (70); // value readout painted in paint()
            gain.setBounds (row);
        }
        if (! sel.isBus)
            for (auto& s : sends)
                if (s.isVisible()) slot (s, 90, 74);
        if (sel.isBus && sel.bus == MixBus::Master) r.removeFromTop (24);

        r.removeFromTop (10 + 16 + 6);
        report.setBounds (r);
    }

private:
    MixController& controller;
    Selection sel;
    bool updating = false;
    juce::Slider gain, fader;
    std::array<juce::Slider, int (FxSlot::Count)> sends;
    FlatButton muteButton { "MUTE", FlatButton::Style::Outline };
    MeterComponent meter;
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
    listItems.clear();
    rows.clear();
    listHolder.removeAllChildren();

    const auto& graph = controller.getGraph();
    auto addHeader = [&] (const juce::String& title, const juce::String& count, MixBus bus, bool busesSection)
    {
        auto h = std::make_unique<SectionHeader> (title, count, bus, busesSection);
        listHolder.addAndMakeVisible (*h);
        listItems.push_back (std::move (h));
    };
    auto addRow = [&] (std::unique_ptr<Row> row)
    {
        listHolder.addAndMakeVisible (*row);
        rows.push_back (row.get());
        listItems.push_back (std::move (row));
    };

    // Each family: its channels, then its bus — so the group master sits under the
    // channels that feed it, the way a console is patched. MASTER closes the list.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        const int n = graph.stripsOnBus (bus);
        if (n == 0) continue;

        addHeader (juce::String (mixBusName (bus)),
                   juce::String (n) + (n == 1 ? " ch" : " ch"),
                   bus, false);

        for (int i = 0; i < graph.numStrips(); ++i)
        {
            const auto& s = graph.strips[size_t (i)];
            if (s.bus != bus) continue;
            const auto in = deviceInLabel (s);
            auto row = std::make_unique<Row> (s.name,
                                              juce::String (channelRoleName (s.role)) + "  "
                                                  + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + "  "
                                                  + mixBusName (s.bus),
                                              Row::Kind::Channel, s.bus, i, in);
            row->setClickingTogglesState (false);
            row->onClick = [this, i] { select (i); };
            addRow (std::move (row));
        }

        if (controller.isPrepared() && controller.getEngine().isBusUsed (bus))
        {
            auto row = std::make_unique<Row> (mixBusName (bus),
                                              juce::String (n) + " inputs  "
                                                  + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + "  MASTER",
                                              Row::Kind::Bus, bus);
            row->setClickingTogglesState (false);
            row->onClick = [this, bus] { selectBus (bus); };
            addRow (std::move (row));
        }
    }

    if (controller.isPrepared() && controller.getEngine().isBusUsed (MixBus::Master))
    {
        addHeader ("OUTPUT", "1", MixBus::Master, true);
        auto row = std::make_unique<Row> ("MASTER", "mix output", Row::Kind::Master, MixBus::Master);
        row->setClickingTogglesState (false);
        row->onClick = [this] { selectBus (MixBus::Master); };
        addRow (std::move (row));
    }

    builtForStrips = graph.numStrips();
    if (! selection.isBus && (selection.strip < 0 || selection.strip >= graph.numStrips()))
    {
        if (graph.numStrips() > 0) select (0);
        else selectBus (MixBus::Master);
    }
    else if (selection.isBus) selectBus (selection.bus);
    else select (selection.strip);
    resized();
}

void AdvancedPage::select (int strip)
{
    selection.isBus = false;
    selection.strip = strip;
    for (auto* r : rows)
        r->setToggleState (r->kind == Row::Kind::Channel && r->strip == strip, juce::dontSendNotification);
    detail->show (selection);
}

void AdvancedPage::selectBus (MixBus bus)
{
    selection.isBus = true;
    selection.bus = bus;
    for (auto* r : rows)
        r->setToggleState (r->kind != Row::Kind::Channel && r->bus == bus, juce::dontSendNotification);
    detail->show (selection);
}

void AdvancedPage::refresh()
{
    if (! controller.isPrepared()) return;
    const auto& engine = controller.getEngine();
    const auto& graph = engine.getGraph();
    if (graph.numStrips() != builtForStrips) rebuild();
    const auto& kept = controller.getBase();

    for (auto* r : rows)
    {
        if (r->kind == Row::Kind::Channel)
        {
            const auto& m = engine.getStrip (r->strip).getOutputMeter();
            const auto& st = kept.strips[size_t (r->strip)];
            r->set (m.consumeMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped(),
                    db1 (st.faderDb) + (st.inputGainDb != 0.0f ? "  gain " + db1 (st.inputGainDb) : juce::String()),
                    st.mute);
        }
        else
        {
            const auto& m = engine.getBus (r->bus).getOutputMeter();
            r->set (m.consumeMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped(),
                    db1 (kept.buses[size_t (r->bus)].faderDb),
                    kept.buses[size_t (r->bus)].mute);
        }
    }
    detail->refresh();
}

void AdvancedPage::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (AppStyle::kMargin);
    auto header = area.removeFromTop (44);
    g.setColour (Tokens::textHi);
    g.setFont (LiveMixLookAndFeel::condensed (28.0f, 700, 0.02f));
    g.drawText ("Advanced", header.removeFromLeft (180), juce::Justification::centredLeft);
    g.setColour (Tokens::textLow);
    g.setFont (LiveMixLookAndFeel::body (13.0f));
    g.drawText ("Channels feed buses. Buses feed the mix.",
                header, juce::Justification::centredLeft);
}

void AdvancedPage::resized()
{
    auto area = getLocalBounds().reduced (AppStyle::kMargin);
    auto header = area.removeFromTop (40);
    backButton.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, 32));
    area.removeFromTop (12);

    auto left = area.removeFromLeft (juce::jmin (380, area.getWidth() / 2));
    area.removeFromLeft (16);
    viewport.setBounds (left);

    const int channelH = 52, busH = 60, headerH = 28, gap = 4, sectionGap = 10;
    int total = 0;
    for (auto& item : listItems)
    {
        if (dynamic_cast<SectionHeader*> (item.get()) != nullptr)
            total += (total > 0 ? sectionGap : 0) + headerH + gap;
        else if (auto* row = dynamic_cast<Row*> (item.get()))
            total += (row->kind == Row::Kind::Channel ? channelH : busH) + gap;
    }

    listHolder.setSize (left.getWidth() - (total > left.getHeight() ? 12 : 0), juce::jmax (total, left.getHeight()));
    int y = 0;
    bool firstHeader = true;
    for (auto& item : listItems)
    {
        if (dynamic_cast<SectionHeader*> (item.get()) != nullptr)
        {
            if (! firstHeader) y += sectionGap;
            firstHeader = false;
            item->setBounds (0, y, listHolder.getWidth(), headerH);
            y += headerH + gap;
        }
        else if (auto* row = dynamic_cast<Row*> (item.get()))
        {
            const int h = row->kind == Row::Kind::Channel ? channelH : busH;
            item->setBounds (0, y, listHolder.getWidth(), h);
            y += h + gap;
        }
    }
    detail->setBounds (area);
}

} // namespace livemix
