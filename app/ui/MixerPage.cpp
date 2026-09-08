#include "MixerPage.h"
#include "Core/DbUtils.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kHeaderH   = 66;
    constexpr int kPadX      = 24;
    constexpr int kPadY      = 14;

    constexpr int kStripGap  = 5;
    constexpr int kGroupGap  = 16;
    constexpr int kBandH     = 20;      // the group band across the top of the bank
    constexpr int kBandGap   = 5;

    constexpr int kRowH      = 46;      // list view
    constexpr int kRowGap    = 3;
    constexpr int kSectionH  = 24;
    constexpr int kMaxStripH = 720;     // a fader is a fader, not a wall: taller than this reads as a mistake

    int columnWidthFor (MixerPage::Size s) noexcept
    {
        return s == MixerPage::Size::Narrow ? 58 : s == MixerPage::Size::Wide ? 116 : 84;
    }

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::Colour busTint (MixBus b) noexcept
    {
        switch (b)
        {
            case MixBus::Drums:  return Dine::warn;
            case MixBus::Bass:   return Dine::accent;
            case MixBus::Music:  return juce::Colour (0xff8fa2d8);
            case MixBus::Vocals: return Dine::ok;
            case MixBus::Master: return juce::Colour (0xffc8ccd4);
            case MixBus::Count:  break;
        }
        return Dine::ink2;
    }

    juce::String sentenceCase (const juce::String& s)
    {
        return s.substring (0, 1).toUpperCase() + s.substring (1).toLowerCase();
    }

    juce::String panText (float pan)
    {
        if (std::fabs (pan) < 0.005f) return "C";
        const int amount = int (std::round (std::fabs (pan) * 100.0f));
        return (pan < 0.0f ? "L" : "R") + juce::String (amount);
    }

    // The meter's scale, in dB. Ticks everywhere, numbers where there is room.
    struct ScaleMark { float db; bool numbered; };
    const ScaleMark kScale[] = { { 0.0f, true }, { -6.0f, true }, { -12.0f, true }, { -18.0f, false },
                                 { -24.0f, true }, { -30.0f, false }, { -36.0f, true }, { -48.0f, true },
                                 { -60.0f, false } };
}

// ------------------------------------------------------------------ PanBar
// A small balance control: drag left / right, double-click to centre. Small enough to
// live on one line of a strip, and it reads as "centre" at a glance.
class PanBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (float)> onChange;

    void setValue (float v) { if (std::fabs (v - value) > 0.0005f) { value = v; repaint(); } }
    float getValue() const noexcept { return value; }
    void setTint (juce::Colour c) { tint = c; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (float (getWidth()), 5.0f);
        Dine::drawWell (g, r, 2.5f);
        const float centre = r.getCentreX();
        const float x = centre + value * (r.getWidth() * 0.5f - 3.0f);
        g.setColour (tint.withAlpha (0.85f));
        const float lo = juce::jmin (centre, x), hi = juce::jmax (centre, x);
        if (hi - lo > 0.5f) g.fillRect (juce::Rectangle<float> (lo, r.getY(), hi - lo, r.getHeight()));
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (centre - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
        auto knob = juce::Rectangle<float> (x - 3.5f, r.getCentreY() - 5.0f, 7.0f, 10.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (knob.translated (0.0f, 1.0f), 2.0f);
        g.setColour (juce::Colour (0xffe8eaee));
        g.fillRoundedRectangle (knob, 2.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override { value = 0.0f; repaint(); if (onChange) onChange (value); }

private:
    void drag (const juce::MouseEvent& e)
    {
        const float half = juce::jmax (1.0f, float (getWidth()) * 0.5f - 3.0f);
        const float v = juce::jlimit (-1.0f, 1.0f, (float (e.position.x) - float (getWidth()) * 0.5f) / half);
        setValue (std::fabs (v) < 0.06f ? 0.0f : v);      // a detent at the centre
        if (onChange) onChange (value);
    }

    float value = 0.0f;
    juce::Colour tint { Dine::accent };
};

// ------------------------------------------------------------------ Strip
// One source, group bus or the master. The same component in both views: STRIPS lays it
// out as a column, LIST as a row, so a mute is a mute wherever you press it.
class MixerPage::Strip : public juce::Component
{
public:
    enum class Kind { Channel, Bus, Master };
    enum class Layout { Column, Row };

    Strip (MixController& c, AppServices& s, Kind k, MixBus busFamily, ChannelRole r, int stripIndex,
           const juce::String& title, const juce::String& sourceText)
        : controller (c), services (s), kind (k), bus (busFamily), role (r), strip (stripIndex),
          name (title), source (sourceText),
          icon (k == Kind::Channel ? Dine::iconForRole (r) : Dine::Icon::Bus),
          meter (DineMeter::Style::Segments),
          muteButton ("M", DineButton::Style::Standard),
          soloButton ("S", DineButton::Style::Standard),
          armButton ("R", DineButton::Style::Standard),
          monitorButton ("A", DineButton::Style::Standard)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 12.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);          // the useful half of the throw gets the room
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.getProperties().set ("dineFader", true);
        fader.setTooltip ("Level. Double-click for 0.0 dB.");
        fader.onValueChange = [this]
        {
            if (updating) return;
            const float db = float (fader.getValue());
            if (kind == Kind::Channel) controller.setStripFader (strip, db);
            else controller.setBusFader (bus, db);
            levelText = db1 (db);
            repaint();
        };

        pan.setTint (busTint (bus));
        pan.setTooltip ("Balance. Double-click for the centre.");
        pan.onChange = [this] (float v)
        {
            if (updating || kind != Kind::Channel) return;
            controller.setStripPan (strip, v);
            repaint();
        };
        pan.setVisible (kind == Kind::Channel);

        auto setupKey = [] (DineButton& b)
        {
            b.setFontPx (11.0f);
            b.setPadX (3);
            b.setClickingTogglesState (false);
        };
        setupKey (muteButton);
        setupKey (soloButton);
        setupKey (armButton);
        setupKey (monitorButton);
        muteButton.setVisible (kind != Kind::Master);
        soloButton.setVisible (kind != Kind::Master);
        armButton.setVisible (kind == Kind::Channel);
        monitorButton.setVisible (kind == Kind::Channel);
        muteButton.setTooltip ("Mute.");
        soloButton.setTooltip ("Solo: hear this alone.");
        armButton.setTooltip ("Record arm: this track is captured, raw, when recording starts.");
        monitorButton.setTooltip ("Monitoring. Auto: you hear the input unless the timeline is playing this track back. Input: always. Off: never.");

        armButton.onClick = [this]
        {
            auto& project = services.daw().getProject();
            if (strip < 0 || strip >= int (project.tracks.size())) return;
            project.tracks[size_t (strip)].armed = ! project.tracks[size_t (strip)].armed;
            services.daw().refresh();
            services.saveSession();
            refresh();
        };
        monitorButton.onClick = [this]
        {
            auto& project = services.daw().getProject();
            if (strip < 0 || strip >= int (project.tracks.size())) return;
            auto& mode = project.tracks[size_t (strip)].monitor;
            mode = MonitorMode ((int (mode) + 1) % int (MonitorMode::Count));
            services.daw().refresh();
            services.saveSession();
            refresh();
        };
        muteButton.onClick = [this]
        {
            if (kind == Kind::Channel) controller.setStripMute (strip, ! controller.getBase().strips[size_t (strip)].mute);
            else if (kind == Kind::Bus) controller.setBusMute (bus, ! controller.getBase().buses[size_t (bus)].mute);
        };
        soloButton.onClick = [this]
        {
            if (kind == Kind::Channel) controller.setStripSolo (strip, ! controller.getBase().strips[size_t (strip)].solo);
            else if (kind == Kind::Bus) controller.setBusSolo (bus, ! controller.getBase().buses[size_t (bus)].solo);
        };

        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        addAndMakeVisible (pan);
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);
        addAndMakeVisible (armButton);
        addAndMakeVisible (monitorButton);
    }

    void setOpenHandler (std::function<void()> h) { open = std::move (h); }

    Kind getKind() const noexcept { return kind; }
    MixBus getBus() const noexcept { return bus; }

    void setLayout (Layout l, Size s)
    {
        layout = l;
        size = s;
        fader.setSliderStyle (l == Layout::Column ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
        pan.setVisible (kind == Kind::Channel && (l == Layout::Row || s != Size::Narrow));
        resized();
        repaint();
    }

    int columnWidth() const noexcept
    {
        const int w = columnWidthFor (size);
        return kind == Kind::Master ? w + 12 : kind == Kind::Bus ? w + 4 : w;
    }

    void refresh()
    {
        if (! controller.isPrepared()) return;
        const auto& state = controller.getBase();
        updating = true;

        float faderDb = 0.0f, panValue = 0.0f;
        bool muted = false, soloed = false;
        float peak = -120.0f, hold = -120.0f;
        bool clipped = false;

        if (kind == Kind::Channel && strip >= 0 && strip < state.numStrips)
        {
            const auto& st = state.strips[size_t (strip)];
            faderDb = st.faderDb;
            panValue = st.pan;
            muted = st.mute;
            soloed = st.solo;
            const auto& m = controller.getEngine().getStrip (strip).getOutputMeter();
            peak = m.consumeMaxPeakDb();
            hold = m.getMaxRmsDb();
            clipped = m.hasClipped();
        }
        else
        {
            faderDb = state.buses[size_t (bus)].faderDb;
            muted = state.buses[size_t (bus)].mute;
            soloed = state.buses[size_t (bus)].solo;
            const auto& m = controller.getEngine().getBus (bus).getOutputMeter();
            peak = m.consumeMaxPeakDb();
            hold = m.getMaxRmsDb();
            clipped = m.hasClipped();
        }

        fader.setValue (faderDb, juce::dontSendNotification);
        pan.setValue (panValue);
        meter.setLevels (peak, hold, clipped);
        peakDb = meter.getPeakDb();
        levelText = db1 (faderDb);
        peakText = peakDb <= -60.0f ? Glyph::dash() : db1 (peakDb);
        mute = muted;
        solo = soloed;
        if (bypassed != controller.isBypassed())
        {
            bypassed = controller.isBypassed();
            fader.setEnabled (! bypassed);
            pan.setEnabled (! bypassed);
        }
        muteButton.setStyle (mute ? DineButton::Style::Filled : DineButton::Style::Standard);
        soloButton.setStyle (solo ? DineButton::Style::Filled : DineButton::Style::Standard);
        if (kind == Kind::Channel)
        {
            const auto& project = services.daw().getProject();
            const bool isArmed = strip >= 0 && strip < int (project.tracks.size()) && project.tracks[size_t (strip)].armed;
            const auto mode = strip >= 0 && strip < int (project.tracks.size()) ? project.tracks[size_t (strip)].monitor
                                                                                : MonitorMode::Auto;
            armed = isArmed;
            armButton.setStyle (isArmed ? DineButton::Style::Filled : DineButton::Style::Standard);
            monitorButton.setButtonText (mode == MonitorMode::Off ? Glyph::dash() : mode == MonitorMode::Input ? "I" : "A");
            monitorButton.setStyle (mode == MonitorMode::Input ? DineButton::Style::Filled : DineButton::Style::Standard);
        }
        updating = false;
        repaint();
    }

    // -------------------------------------------------------------- painting
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto cardFill = kind == Kind::Master ? Dine::card.brighter (0.04f)
                        : kind == Kind::Bus  ? Dine::card.brighter (0.015f)
                                             : Dine::card;
        if (mute) cardFill = Dine::card.darker (0.28f);
        else if (solo) cardFill = cardFill.overlaidWith (Dine::accent.withAlpha (0.10f));
        Dine::drawCard (g, r, cardFill, solo ? Dine::accent.withAlpha (0.5f) : Dine::hair);

        if (layout == Layout::Column) paintColumn (g);
        else                          paintRow (g);
    }

    void paintColumn (juce::Graphics& g)
    {
        const auto tint = busTint (bus);
        const bool narrow = size == Size::Narrow;

        // The bus's colour along the top edge, so a strip always says which group it is in.
        {
            juce::Path top;
            auto r = getLocalBounds().toFloat();
            top.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), 8.0f,
                                     Dine::Radius::card, Dine::Radius::card, true, true, false, false);
            g.setColour (tint.withAlpha (mute ? 0.28f : 0.75f));
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, getWidth(), 3));
            g.fillPath (top);
            g.restoreState();
        }

        auto inner = getLocalBounds().reduced (narrow ? 5 : 7, 0).withTrimmedTop (7).withTrimmedBottom (8);

        // ---- name and source
        auto head = inner.removeFromTop (narrow ? 26 : 32);
        g.setColour (mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (narrow ? 10.5f : 11.5f, 600));
        g.drawFittedText (name, head.removeFromTop (narrow ? 24 : 19), juce::Justification::centredTop, narrow ? 2 : 1, 0.85f);
        if (! narrow)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (9.5f));
            g.drawFittedText (source, head, juce::Justification::centredTop, 1, 0.72f);
        }

        // ---- the peak reading, in the level's own colour
        auto peakRow = inner.removeFromTop (14);
        g.setColour (mute ? Dine::ink4 : peakText == Glyph::dash() ? Dine::ink4 : Dine::levelColour (peakDb));
        g.setFont (Dine::mono (10.0f, 500));
        g.drawText (peakText, peakRow, juce::Justification::centred);
        inner.removeFromTop (4);

        // ---- foot: pan, keys, level
        auto foot = inner.removeFromBottom (footHeight());
        auto body = inner;

        // the meter's scale, between the fader and the meter
        if (! narrow)
        {
            auto scale = body.withTrimmedLeft (body.getWidth() - meterWidth() - scaleWidth()).withWidth (scaleWidth());
            auto meterArea = meter.getBounds();
            g.setFont (Dine::mono (8.5f));
            for (const auto& m : kScale)
            {
                const float y = float (meterArea.getBottom()) - float (meterArea.getHeight()) * DineMeter::norm (m.db);
                g.setColour (juce::Colours::white.withAlpha (m.numbered ? 0.16f : 0.09f));
                g.fillRect (float (scale.getRight()) - (m.numbered ? 5.0f : 3.0f), y - 0.5f, m.numbered ? 5.0f : 3.0f, 1.0f);
                if (m.numbered)
                {
                    g.setColour (Dine::ink4);
                    g.drawText (juce::String (int (-m.db)), scale.getX(), juce::roundToInt (y) - 5,
                                scale.getWidth() - 6, 11, juce::Justification::centredRight);
                }
            }
        }

        // The unity mark beside the fader throw, so 0 dB is findable without reading numbers.
        {
            const auto fb = fader.getBounds();
            const float t = float (fader.valueToProportionOfLength (0.0));
            const float y = float (fb.getY()) + float (fb.getHeight()) * (1.0f - t);
            g.setColour (juce::Colours::white.withAlpha (0.20f));
            g.fillRect (float (fb.getX()), y - 0.5f, 4.0f, 1.0f);
        }

        juce::ignoreUnused (foot, tint);

        // ---- the level under the keys
        auto value = getLocalBounds().withTrimmedBottom (6).removeFromBottom (15);
        g.setColour (mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (narrow ? 9.5f : 10.5f, 500));
        g.drawText (levelText + (narrow ? "" : " dB"), value, juce::Justification::centred);

        if (kind == Kind::Channel && pan.isVisible())
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (8.5f));
            g.drawText (panText (pan.getValue()), pan.getBounds().withY (pan.getBounds().getY() - 11).withHeight (10),
                        juce::Justification::centred);
        }
    }

    void paintRow (juce::Graphics& g)
    {
        const auto tint = busTint (bus);
        auto r = getLocalBounds();

        g.setColour (tint.withAlpha (mute ? 0.3f : 0.9f));
        g.fillRoundedRectangle (juce::Rectangle<float> (float (r.getX()) + 1.0f, float (r.getY()) + 6.0f,
                                                        3.0f, float (r.getHeight()) - 12.0f), 1.5f);

        auto label = juce::Rectangle<int> (r.getX() + 10, r.getY(), nameColumnWidth(), r.getHeight()).reduced (0, 6);
        Dine::drawIcon (g, icon, label.removeFromLeft (16).toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                        mute ? Dine::ink4 : kind == Kind::Channel ? Dine::glyph : tint);
        label.removeFromLeft (8);
        auto line = label.removeFromTop (17);
        g.setColour (mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (12.5f, kind == Kind::Channel ? 500 : 600));
        g.drawText (name, line, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f));
        g.drawText (source, label, juce::Justification::topLeft, true);

        // peak beside the meter, level beside the fader
        auto m = meter.getBounds();
        g.setColour (mute ? Dine::ink4 : peakText == Glyph::dash() ? Dine::ink4 : Dine::levelColour (peakDb));
        g.setFont (Dine::mono (10.0f, 500));
        g.drawText (peakText, m.getX(), m.getBottom() + 1, m.getWidth(), 12, juce::Justification::centredLeft);

        g.setColour (mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (levelText + " dB", valueRect.withTrimmedRight (4), juce::Justification::centredRight);

        if (pan.isVisible())
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (9.0f));
            g.drawText (panText (pan.getValue()), pan.getBounds().withY (pan.getBounds().getBottom() - 1).withHeight (11),
                        juce::Justification::centred);
        }
    }

    // -------------------------------------------------------------- layout
    int keysHeight() const noexcept
    {
        if (kind == Kind::Master) return 0;
        return kind == Kind::Channel ? 2 * 19 + 4 : 19;
    }

    int footHeight() const noexcept
    {
        const int panRow = pan.isVisible() ? 22 : 0;
        return panRow + keysHeight() + 6 + 15;   // pan, keys, gap, the level readout
    }

    int meterWidth() const noexcept { return size == Size::Narrow ? 8 : size == Size::Wide ? 12 : 10; }
    int scaleWidth() const noexcept { return size == Size::Narrow ? 0 : size == Size::Wide ? 26 : 22; }
    int nameColumnWidth() const noexcept { return 168; }

    void resized() override
    {
        if (layout == Layout::Column) layoutColumn();
        else                          layoutRow();
    }

    void layoutColumn()
    {
        const bool narrow = size == Size::Narrow;
        auto inner = getLocalBounds().reduced (narrow ? 5 : 7, 0).withTrimmedTop (7).withTrimmedBottom (8);
        inner.removeFromTop (narrow ? 26 : 32);      // name
        inner.removeFromTop (14 + 4);                // peak

        auto foot = inner.removeFromBottom (footHeight());
        foot.removeFromBottom (15 + 6);              // the level readout
        if (pan.isVisible())
        {
            auto panRow = foot.removeFromBottom (22).withTrimmedTop (10);
            pan.setBounds (panRow.reduced (2, 0));
        }
        if (keysHeight() > 0)
        {
            auto keys = foot.removeFromBottom (keysHeight());
            const int gap = narrow ? 2 : 3;
            if (kind == Kind::Channel)
            {
                auto top = keys.removeFromTop (19);
                keys.removeFromTop (4);
                const int w = juce::jmax (16, (top.getWidth() - gap) / 2);
                armButton.setBounds (top.removeFromLeft (w));
                monitorButton.setBounds (top.removeFromRight (w));
                soloButton.setBounds (keys.removeFromLeft (w));
                muteButton.setBounds (keys.removeFromRight (w));
            }
            else
            {
                const int w = juce::jmax (16, (keys.getWidth() - gap) / 2);
                soloButton.setBounds (keys.removeFromLeft (w));
                muteButton.setBounds (keys.removeFromRight (w));
            }
        }

        auto body = inner.withTrimmedBottom (4);
        meter.setBounds (body.removeFromRight (meterWidth()));
        body.removeFromRight (scaleWidth());
        fader.setBounds (body.withTrimmedRight (narrow ? 0 : 2));
    }

    void layoutRow()
    {
        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromLeft (nameColumnWidth() + 4);

        // Every row reserves the same key column, so the meters and faders line up down the
        // page whether the row is a source or a group.
        {
            const int w = 24;
            auto keys = r.removeFromLeft (4 * w + 9).withSizeKeepingCentre (4 * w + 9, 22);
            if (kind == Kind::Channel)
            {
                armButton.setBounds (keys.removeFromLeft (w)); keys.removeFromLeft (3);
                monitorButton.setBounds (keys.removeFromLeft (w)); keys.removeFromLeft (3);
            }
            else keys.removeFromLeft (2 * w + 6);
            if (kind != Kind::Master)
            {
                muteButton.setBounds (keys.removeFromLeft (w)); keys.removeFromLeft (3);
                soloButton.setBounds (keys.removeFromLeft (w));
            }
        }
        r.removeFromLeft (14);

        valueRect = r.removeFromRight (72);
        r.removeFromRight (10);
        if (pan.isVisible())
        {
            pan.setBounds (r.removeFromRight (62).withSizeKeepingCentre (58, 20).translated (0, -5));
            r.removeFromRight (14);
        }
        else r.removeFromRight (76);

        auto meterArea = r.removeFromLeft (juce::jmax (90, (r.getWidth() * 2) / 5)).withTrimmedBottom (12);
        meter.setBounds (meterArea.withSizeKeepingCentre (meterArea.getWidth(), 8).translated (0, 3));
        r.removeFromLeft (16);
        fader.setBounds (r.withSizeKeepingCentre (juce::jmax (80, r.getWidth()), 22));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this && open && ! e.mouseWasDraggedSinceMouseDown()) open();
    }

    MixController& controller;
    AppServices& services;
    Kind kind;
    MixBus bus;
    ChannelRole role;
    int strip = -1;
    juce::String name, source, levelText { "+0.0" }, peakText;
    float peakDb = -120.0f;
    juce::Rectangle<int> valueRect;
    bool mute = false, solo = false, armed = false, bypassed = false, updating = false;
    Layout layout = Layout::Column;
    Size size = Size::Normal;
    Dine::Icon icon;
    DineMeter meter;
    juce::Slider fader;
    PanBar pan;
    DineButton muteButton, soloButton, armButton, monitorButton;
    std::function<void()> open;
};

// ------------------------------------------------------------------ Bank
// The scrolled surface. It paints the group banding (a coloured band over each family of
// strips, a section rule in the list) so a 24-input console still reads as four groups.
class MixerPage::Bank : public juce::Component
{
public:
    struct Band { juce::Rectangle<int> bounds; juce::String name; juce::Colour tint; bool vertical = false; };

    void setBands (std::vector<Band> b) { bands = std::move (b); repaint(); }

    void paint (juce::Graphics& g) override
    {
        for (const auto& b : bands)
        {
            if (b.vertical)      // list view: a section rule with the group's name
            {
                auto r = b.bounds;
                g.setColour (b.tint.withAlpha (0.9f));
                g.setFont (Dine::text (10.5f, 700).withExtraKerningFactor (0.08f));
                const int w = Dine::textWidth (Dine::text (10.5f, 700), b.name.toUpperCase()) + 6;
                g.drawText (b.name.toUpperCase(), r.removeFromLeft (juce::jmin (w, r.getWidth())),
                            juce::Justification::centredLeft);
                Dine::drawRule (g, r.withSizeKeepingCentre (r.getWidth(), 1).withTrimmedLeft (8), Dine::hairSoft);
            }
            else                 // strips view: a band across the top of the family
            {
                Dine::fillRounded (g, b.bounds.toFloat(), b.tint.withAlpha (0.16f), 5.0f);
                g.setColour (b.tint.withAlpha (0.95f));
                g.setFont (Dine::text (10.0f, 700).withExtraKerningFactor (0.08f));
                g.drawText (b.name.toUpperCase(), b.bounds, juce::Justification::centred, true);
            }
        }
    }

private:
    std::vector<Band> bands;
};

// ------------------------------------------------------------------ MixerPage
MixerPage::MixerPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    bank = std::make_unique<Bank>();
    viewport.setViewedComponent (bank.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    const char* viewNames[2] = { "Strips", "List" };
    for (int i = 0; i < 2; ++i)
    {
        viewTabs[size_t (i)] = std::make_unique<DineButton> (viewNames[i], DineButton::Style::Segment);
        viewTabs[size_t (i)]->setFontPx (11.5f);
        viewTabs[size_t (i)]->setPadX (10);
        viewTabs[size_t (i)]->setClickingTogglesState (false);
        viewTabs[size_t (i)]->onClick = [this, i] { setView (View (i)); };
        addAndMakeVisible (*viewTabs[size_t (i)]);
    }
    viewTabs[0]->setTooltip ("The classic console: one vertical strip per source.");
    viewTabs[1]->setTooltip ("One row per source, so every name and level lines up down the page.");

    const char* sizeNames[3] = { "S", "M", "L" };
    for (int i = 0; i < 3; ++i)
    {
        sizeTabs[size_t (i)] = std::make_unique<DineButton> (sizeNames[i], DineButton::Style::Segment);
        sizeTabs[size_t (i)]->setFontPx (11.5f);
        sizeTabs[size_t (i)]->setPadX (8);
        sizeTabs[size_t (i)]->setClickingTogglesState (false);
        sizeTabs[size_t (i)]->onClick = [this, i] { setStripSize (Size (i)); };
        addAndMakeVisible (*sizeTabs[size_t (i)]);
    }
    sizeTabs[0]->setTooltip ("Narrow strips: the whole band on one screen.");
    sizeTabs[1]->setTooltip ("Normal strips.");
    sizeTabs[2]->setTooltip ("Wide strips: every label in full.");

    const char* showNames[3] = { "All", "Inputs", "Groups" };
    for (int i = 0; i < 3; ++i)
    {
        showTabs[size_t (i)] = std::make_unique<DineButton> (showNames[i], DineButton::Style::Segment);
        showTabs[size_t (i)]->setFontPx (11.5f);
        showTabs[size_t (i)]->setPadX (9);
        showTabs[size_t (i)]->setClickingTogglesState (false);
        showTabs[size_t (i)]->onClick = [this, i] { setShow (Show (i)); };
        addAndMakeVisible (*showTabs[size_t (i)]);
    }
    showTabs[1]->setTooltip ("Only the sources.");
    showTabs[2]->setTooltip ("Only the group buses and the master.");

    clearSolos.setFontPx (11.5f);
    clearSolos.setPadX (10);
    clearSolos.setTooltip ("Every solo off.");
    clearSolos.onClick = [this]
    {
        controller.clearSolos();
        if (onToast) onToast ("Solos cleared.");
        refresh();
    };
    addChildComponent (clearSolos);

    windowButton.setFontPx (11.5f);
    windowButton.setPadX (10);
    windowButton.setTooltip ("Put the mixer on a second screen and keep the timeline in front of you.");
    windowButton.onClick = [this] { if (onOpenWindow) onOpenWindow(); };
    addAndMakeVisible (windowButton);

    rebuild();
}

MixerPage::~MixerPage() = default;

void MixerPage::setWindowButtonVisible (bool v)
{
    windowButtonWanted = v;
    windowButton.setVisible (v);
    resized();
}

void MixerPage::setView (View v)
{
    if (view == v) return;
    view = v;
    viewport.setScrollBarsShown (v == View::List, v == View::Strips);
    for (auto& s : strips) s->setLayout (v == View::Strips ? Strip::Layout::Column : Strip::Layout::Row, stripSize);
    viewport.setViewPosition (0, 0);
    updateControls();
    resized();
}

void MixerPage::setStripSize (Size s)
{
    if (stripSize == s) return;
    stripSize = s;
    for (auto& st : strips) st->setLayout (view == View::Strips ? Strip::Layout::Column : Strip::Layout::Row, s);
    updateControls();
    resized();
}

void MixerPage::setShow (Show s)
{
    if (show == s) return;
    show = s;
    updateControls();
    resized();
}

bool MixerPage::visibleInFilter (const Strip& s) const
{
    if (show == Show::All) return true;
    const bool isChannel = s.getKind() == Strip::Kind::Channel;
    return show == Show::Inputs ? isChannel : ! isChannel;
}

void MixerPage::rebuild()
{
    strips.clear();
    bank->removeAllChildren();

    const auto& graph = controller.getGraph();

    auto add = [&] (std::unique_ptr<Strip> s)
    {
        bank->addAndMakeVisible (*s);
        s->setLayout (view == View::Strips ? Strip::Layout::Column : Strip::Layout::Row, stripSize);
        strips.push_back (std::move (s));
    };

    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        if (graph.stripsOnBus (bus) == 0) continue;

        for (int i = 0; i < graph.numStrips(); ++i)
        {
            const auto& r = graph.strips[size_t (i)];
            if (r.bus != bus) continue;
            juce::String source = juce::String (channelRoleName (r.role));
            source += "  " + Glyph::dot() + "  In " + juce::String (r.inputA + 1);
            if (r.inputB >= 0) source += "-" + juce::String (r.inputB + 1);
            auto s = std::make_unique<Strip> (controller, services, Strip::Kind::Channel, bus, r.role, i,
                                              juce::String (r.name), source);
            s->setOpenHandler ([this, i] { if (onOpenStrip) onOpenStrip (i); });
            add (std::move (s));
        }

        if (controller.isPrepared() && controller.getEngine().isBusUsed (bus))
        {
            auto s = std::make_unique<Strip> (controller, services, Strip::Kind::Bus, bus, ChannelRole::KickIn, -1,
                                              sentenceCase (mixBusName (bus)),
                                              juce::String (graph.stripsOnBus (bus))
                                                  + (graph.stripsOnBus (bus) == 1 ? " source" : " sources"));
            s->setOpenHandler ([this, bus] { if (onOpenBus) onOpenBus (bus); });
            add (std::move (s));
        }
    }

    if (controller.isPrepared() && controller.getEngine().isBusUsed (MixBus::Master))
    {
        auto s = std::make_unique<Strip> (controller, services, Strip::Kind::Master, MixBus::Master, ChannelRole::KickIn,
                                          -1, "Master", "Everything, out");
        s->setOpenHandler ([this] { if (onOpenBus) onOpenBus (MixBus::Master); });
        add (std::move (s));
    }

    builtForStrips = graph.numStrips();
    updateControls();
    resized();
}

void MixerPage::refresh()
{
    if (! controller.isPrepared()) return;
    if (controller.getGraph().numStrips() != builtForStrips) { rebuild(); return; }
    for (auto& s : strips) s->refresh();
    updateControls();
}

void MixerPage::updateControls()
{
    for (int i = 0; i < 2; ++i) viewTabs[size_t (i)]->setToggleState (int (view) == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
    {
        sizeTabs[size_t (i)]->setToggleState (int (stripSize) == i, juce::dontSendNotification);
        sizeTabs[size_t (i)]->setVisible (view == View::Strips);
        showTabs[size_t (i)]->setToggleState (int (show) == i, juce::dontSendNotification);
    }

    bool anySolo = false;
    if (controller.isPrepared())
    {
        const auto& p = controller.getBase();
        for (int i = 0; i < p.numStrips && ! anySolo; ++i) anySolo = p.strips[size_t (i)].solo;
        for (int b = 0; b < int (MixBus::Count) && ! anySolo; ++b) anySolo = p.buses[size_t (b)].solo;
    }
    if (anySolo != clearSolos.isVisible())
    {
        clearSolos.setVisible (anySolo);
        resized();
    }
    windowButton.setVisible (windowButtonWanted);
}

// -------------------------------------------------------------------- paint
void MixerPage::paint (juce::Graphics& g)
{
    auto head = getLocalBounds().removeFromTop (kHeaderH).reduced (kPadX, 0).withTrimmedTop (12);
    auto title = head.removeFromTop (25);
    title = title.removeFromLeft (juce::jmax (200, title.getWidth() / 3));
    g.setColour (Dine::ink);
    g.setFont (Dine::text (21.0f, 600));
    g.drawText ("Mixer", title, juce::Justification::centredLeft);

    g.setFont (Dine::text (12.0f));
    if (controller.isBypassed())
    {
        g.setColour (Dine::warn);
        g.drawText ("BYPASS is on " + Glyph::dash() + " you are hearing the inputs as they arrive, not the mix.",
                    head.removeFromTop (16), juce::Justification::topLeft, true);
    }
    else
    {
        g.setColour (Dine::ink2);
        g.drawText (juce::String (strips.size() > 0 ? juce::String (int (controller.getGraph().numStrips())) + " sources, "
                                                    : juce::String())
                        + "the groups and the master " + Glyph::dash() + " drag a fader to set the level, click a strip to open it.",
                    head.removeFromTop (16), juce::Justification::topLeft, true);
    }

    // the segmented controls sit on a quiet track, the way the workspace tabs do
    auto trackFor = [&g] (juce::Component* first, juce::Component* last)
    {
        if (first == nullptr || ! first->isVisible()) return;
        auto r = first->getBounds().getUnion (last->getBounds());
        Dine::fillRounded (g, r.expanded (2, 2).toFloat(), juce::Colours::white.withAlpha (0.07f), 7.0f);
    };
    trackFor (viewTabs[0].get(), viewTabs[1].get());
    if (view == View::Strips) trackFor (sizeTabs[0].get(), sizeTabs[2].get());
    trackFor (showTabs[0].get(), showTabs[2].get());

    if (strips.empty())
    {
        auto empty = getLocalBounds().withTrimmedTop (kHeaderH).reduced (kPadX, kPadY);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawFittedText ("Assign inputs first. The mixer fills with a strip for each one, then the group buses and master.",
                          empty.removeFromTop (60), juce::Justification::topLeft, 3);
    }
}

// ------------------------------------------------------------------- layout
void MixerPage::resized()
{
    // ---- header controls, laid out from the right
    auto head = getLocalBounds().removeFromTop (kHeaderH).reduced (kPadX, 0);
    auto row = head.withTrimmedTop (14).withHeight (Dine::Metric::control);
    auto controls = row;

    if (windowButton.isVisible())
    {
        const int w = juce::jmax (120, windowButton.idealWidth());
        windowButton.setBounds (controls.removeFromRight (w));
        controls.removeFromRight (10);
    }
    if (clearSolos.isVisible())
    {
        const int w = juce::jmax (90, clearSolos.idealWidth());
        clearSolos.setBounds (controls.removeFromRight (w));
        controls.removeFromRight (10);
    }
    {
        int widths[3], total = 0;
        for (int i = 0; i < 3; ++i) { widths[i] = juce::jmax (52, showTabs[size_t (i)]->idealWidth() + 8); total += widths[i]; }
        auto seg = controls.removeFromRight (total);
        for (int i = 0; i < 3; ++i) showTabs[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
        controls.removeFromRight (12);
    }
    if (view == View::Strips)
    {
        int widths[3], total = 0;
        for (int i = 0; i < 3; ++i) { widths[i] = 30; total += widths[i]; }
        auto seg = controls.removeFromRight (total);
        for (int i = 0; i < 3; ++i) sizeTabs[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
        controls.removeFromRight (12);
    }
    {
        int widths[2], total = 0;
        for (int i = 0; i < 2; ++i) { widths[i] = juce::jmax (58, viewTabs[size_t (i)]->idealWidth() + 8); total += widths[i]; }
        auto seg = controls.removeFromRight (total);
        for (int i = 0; i < 2; ++i) viewTabs[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
    }

    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderH).reduced (kPadX, kPadY));

    if (view == View::Strips) layoutStrips();
    else                      layoutList();
}

void MixerPage::layoutStrips()
{
    std::vector<Bank::Band> bands;
    const int h = juce::jmax (180, viewport.getMaximumVisibleHeight());
    const int top = kBandH + kBandGap;
    const int stripH = juce::jmin (h - top, kMaxStripH);

    int x = 0;
    for (int b = 0; b <= int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        const int groupStart = x;
        bool any = false;
        for (auto& s : strips)
        {
            if (s->getBus() != bus) continue;
            const bool wanted = visibleInFilter (*s);
            s->setVisible (wanted);
            if (! wanted) continue;
            s->setBounds (x, top, s->columnWidth(), stripH);
            x += s->columnWidth() + kStripGap;
            any = true;
        }
        if (! any) continue;
        x -= kStripGap;
        bands.push_back ({ juce::Rectangle<int> (groupStart, 0, x - groupStart, kBandH),
                           sentenceCase (mixBusName (bus)), busTint (bus), false });
        x += kGroupGap;
    }
    if (x > 0) x -= kGroupGap;

    bank->setBands (std::move (bands));
    bank->setSize (juce::jmax (x, viewport.getWidth()), h);
}

void MixerPage::layoutList()
{
    std::vector<Bank::Band> bands;
    const int w = juce::jmax (560, viewport.getMaximumVisibleWidth());

    int y = 2;
    for (int b = 0; b <= int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        std::vector<Strip*> group;
        for (auto& s : strips)
        {
            if (s->getBus() != bus) continue;
            const bool wanted = visibleInFilter (*s);
            s->setVisible (wanted);
            if (wanted) group.push_back (s.get());
        }
        if (group.empty()) continue;

        bands.push_back ({ juce::Rectangle<int> (2, y, w - 4, kSectionH),
                           sentenceCase (mixBusName (bus)), busTint (bus), true });
        y += kSectionH;
        for (auto* s : group)
        {
            s->setBounds (0, y, w, kRowH);
            y += kRowH + kRowGap;
        }
        y += 8;
    }

    bank->setBands (std::move (bands));
    bank->setSize (w, juce::jmax (y + 4, viewport.getMaximumVisibleHeight()));
}

} // namespace livemix
