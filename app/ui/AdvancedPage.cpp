#include "AdvancedPage.h"
#include "Core/DbUtils.h"

namespace livemix
{

namespace
{
    constexpr int kListWidth = 262;

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    // 1-based device channel(s) the strip is patched to ("in 4", "in 9/10").
    juce::String deviceInLabel (const StripRoute& s)
    {
        if (s.inputA < 0) return {};
        if (s.inputB >= 0)
            return "in " + juce::String (s.inputA + 1) + "/" + juce::String (s.inputB + 1);
        return "in " + juce::String (s.inputA + 1);
    }

    juce::Colour busTint (MixBus b) noexcept
    {
        switch (b)
        {
            case MixBus::Drums:  return Dine::warn;
            case MixBus::Bass:   return Dine::accent;
            case MixBus::Music:  return juce::Colour (0xff8fa2d8);
            case MixBus::Vocals: return Dine::ok;
            case MixBus::Master: return Dine::ink;
            default:             return Dine::ink2;
        }
    }

    // "DRUMS" -> "Drums": capitals are kept for the product verbs only.
    juce::String sentenceCase (const juce::String& s)
    {
        return s.substring (0, 1).toUpperCase() + s.substring (1).toLowerCase();
    }

    const char* sendLabel (FxSlot f) noexcept
    {
        switch (f)
        {
            case FxSlot::VocalPlate: return "Plate";
            case FxSlot::VocalDelay: return "Delay";
            case FxSlot::BgvHall:    return "Hall";
            case FxSlot::SnarePlate: return "Snare plate";
            case FxSlot::DrumRoom:   return "Drum room";
            case FxSlot::Count:
            default:                 return "?";
        }
    }
}

// ------------------------------------------------------------------ SectionHeader
class AdvancedPage::SectionHeader : public juce::Component
{
public:
    SectionHeader (const juce::String& title, const juce::String& count, MixBus bus)
        : label (title), countText (count), tint (busTint (bus)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (7, 0);
        auto dot = r.removeFromLeft (7);
        g.setColour (tint.withAlpha (0.9f));
        g.fillEllipse (dot.withSizeKeepingCentre (6, 6).toFloat());
        r.removeFromLeft (7);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        g.drawText (label, r.removeFromLeft (r.getWidth() - 60), juce::Justification::centredLeft);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.5f));
        g.drawText (countText, r, juce::Justification::centredRight);
    }

private:
    juce::String label, countText;
    juce::Colour tint;
};

// ------------------------------------------------------------------ Row
class AdvancedPage::Row : public juce::Button
{
public:
    enum class Kind { Channel, Bus, Master };

    Row (const juce::String& title, Kind k, MixBus busFamily, ChannelRole role, int stripIndex = -1)
        : juce::Button (title), name (title), kind (k), bus (busFamily), strip (stripIndex),
          icon (k == Kind::Channel ? Dine::iconForRole (role) : Dine::Icon::Bus), meter (DineMeter::Style::Bar)
    {
        addAndMakeVisible (meter);
        meter.setInterceptsMouseClicks (false, false);
    }

    void set (float peakDb, float rmsDb, bool clipped, const juce::String& levelText, bool muted, bool soloed = false)
    {
        meter.setLevels (peakDb, rmsDb, clipped);
        level = levelText;
        mute = muted;
        solo = soloed;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().toFloat();
        if (on)        Dine::fillRounded (g, b, Dine::accent.withAlpha (0.18f), Dine::Radius::control);
        else if (over) Dine::fillRounded (g, b, juce::Colours::white.withAlpha (0.06f), Dine::Radius::control);

        auto r = getLocalBounds().reduced (7, 0);
        Dine::drawIcon (g, icon, r.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                        mute ? Dine::crit : solo ? Dine::accent : kind == Kind::Channel ? (on ? Dine::accent : Dine::glyph) : busTint (bus));
        r.removeFromLeft (8);

        g.setColour (mute ? Dine::crit : solo ? Dine::accent : Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        g.drawText (mute ? juce::String ("mute") : (solo ? juce::String ("solo") : level), r.removeFromRight (42), juce::Justification::centredRight);
        r.removeFromRight (6 + 34);   // the meter lives here

        g.setColour (mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (12.5f, kind == Kind::Channel ? (on ? 600 : 400) : 600));
        g.drawText (name, r, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (7, 0);
        r.removeFromRight (42 + 6);
        meter.setBounds (r.removeFromRight (34).withSizeKeepingCentre (34, 4));
    }

    juce::String name, level;
    Kind kind;
    MixBus bus;
    int strip = -1;
    bool mute = false, solo = false;
    Dine::Icon icon;
    DineMeter meter;
};

// ------------------------------------------------------------------ Detail
class AdvancedPage::Detail : public juce::Component
{
public:
    explicit Detail (MixController& c) : controller (c), meter (DineMeter::Style::Segments)
    {
        auto setupSlider = [] (juce::Slider& s, double lo, double hi, double step, double def, bool vertical)
        {
            s.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (lo, hi, step);
            s.setDoubleClickReturnValue (true, def);
        };
        setupSlider (gain, -24.0, 24.0, 0.5, 0.0, false);
        setupSlider (fader, -60.0, 12.0, 0.5, 0.0, false);
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
        muteButton.setFontPx (12.5f);
        muteButton.onClick = [this]
        {
            if (! sel.isBus)
                controller.setStripMute (sel.strip, ! controller.getKept().strips[size_t (sel.strip)].mute);
        };
        soloButton.setClickingTogglesState (false);
        soloButton.setFontPx (12.5f);
        soloButton.onClick = [this]
        {
            if (sel.isBus)
            {
                if (sel.bus != MixBus::Master)
                    controller.setBusSolo (sel.bus, ! controller.getKept().buses[size_t (sel.bus)].solo);
            }
            else
                controller.setStripSolo (sel.strip, ! controller.getKept().strips[size_t (sel.strip)].solo);
        };

        addAndMakeVisible (gain);
        addAndMakeVisible (fader);
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);
        addAndMakeVisible (meter);
        for (auto& s : sends) addChildComponent (s);

        reportView.setViewedComponent (&report, false);
        reportView.setScrollBarsShown (true, false);
        addAndMakeVisible (reportView);
    }

    void show (const Selection& s)
    {
        sel = s;
        rebuildReport();
        refresh();
        resized();
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
            setVis (soloButton, sel.bus != MixBus::Master);
            if (sel.bus != MixBus::Master)
            {
                const bool on = kept.buses[size_t (sel.bus)].solo;
                soloButton.setButtonText (on ? "Unsolo" : "Solo");
                soloButton.setStyle (on ? DineButton::Style::Filled : DineButton::Style::Standard);
            }
            for (auto& s : sends) setVis (s, false);
            if (controller.isPrepared())
            {
                // Non-consuming read: the channel list owns consumeMaxPeakDb each tick.
                const auto& m = controller.getEngine().getBus (sel.bus).getOutputMeter();
                meter.setLevels (m.getMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped());
                peakDb = meter.getPeakDb();
            }
        }
        else if (sel.strip >= 0 && sel.strip < kept.numStrips)
        {
            const auto& st = kept.strips[size_t (sel.strip)];
            setVis (gain, true);
            setVis (muteButton, true);
            setVis (soloButton, true);
            gain.setValue (st.inputGainDb, juce::dontSendNotification);
            fader.setValue (st.faderDb, juce::dontSendNotification);
            muteButton.setButtonText (st.mute ? "Unmute" : "Mute");
            muteButton.setStyle (st.mute ? DineButton::Style::Filled : DineButton::Style::Standard);
            soloButton.setButtonText (st.solo ? "Unsolo" : "Solo");
            soloButton.setStyle (st.solo ? DineButton::Style::Filled : DineButton::Style::Standard);
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
                peakDb = meter.getPeakDb();
            }
        }
        updating = false;
        if (layoutChanged) resized();
        repaint();
    }

    // ---- the WHAT / WHY list, in its own scrolling holder
    class Report : public juce::Component
    {
    public:
        struct Item { juce::String what, why, value; };
        std::vector<Item> items;
        juce::String empty;

        int layoutFor (int width)
        {
            const auto whyFont = Dine::text (12.5f);
            const auto valueFont = Dine::mono (11.5f);
            int y = 0;
            heights.clear();
            for (const auto& it : items)
            {
                const float w = juce::GlyphArrangement::getStringWidth (whyFont, it.why);
                const int lines = juce::jlimit (1, 5, int (std::ceil (w / juce::jmax (60.0f, float (width) - kValueW - 16))));
                const float vw = juce::GlyphArrangement::getStringWidth (valueFont, it.value);
                const int vlines = juce::jlimit (1, 4, int (std::ceil (vw / juce::jmax (40.0f, float (kValueW) - 6))));
                const int h = juce::jmax (18 + lines * 17, 18 + vlines * 15) + 12;
                heights.push_back (h);
                y += h;
            }
            return juce::jmax (y, 40);
        }

        void paint (juce::Graphics& g) override
        {
            if (items.empty())
            {
                g.setColour (Dine::ink3);
                g.setFont (Dine::text (12.5f));
                g.drawFittedText (empty, getLocalBounds().removeFromTop (40), juce::Justification::topLeft, 2);
                return;
            }
            auto r = getLocalBounds();
            for (size_t i = 0; i < items.size() && i < heights.size(); ++i)
            {
                auto row = r.removeFromTop (heights[i]);
                if (i > 0) Dine::drawRule (g, row.withHeight (1), Dine::hairSoft);
                row = row.reduced (0, 8);
                auto value = row.removeFromRight (kValueW);
                g.setColour (Dine::ink3);
                g.setFont (Dine::mono (11.5f));
                g.drawFittedText (items[i].value, value, juce::Justification::topRight, 4);
                row.removeFromRight (14);
                g.setColour (Dine::ink);
                g.setFont (Dine::text (13.0f, 600));
                g.drawText (items[i].what, row.removeFromTop (17), juce::Justification::topLeft, true);
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (12.5f));
                g.drawFittedText (items[i].why, row, juce::Justification::topLeft, 4);
            }
        }

        static constexpr int kValueW = 132;

    private:
        std::vector<int> heights;
    };

    void rebuildReport()
    {
        report.items.clear();
        report.empty = "No TUNE MIX yet. This channel runs on the profile's baseline for its source.";
        const auto* plan = controller.getPlan();
        auto add = [&] (const Recommendation& r)
        {
            report.items.push_back ({ juce::String (r.what), juce::String (r.why), formatRecommendationValues (r) });
        };
        if (sel.isBus)
        {
            if (plan != nullptr && plan->buses[size_t (sel.bus)].tune.valid)
            {
                headline = juce::String (plan->buses[size_t (sel.bus)].tune.headline);
                for (const auto& r : plan->buses[size_t (sel.bus)].tune.report.items) add (r);
            }
            else { headline = {}; report.empty = "No TUNE MIX yet for this bus. It runs on the profile's baseline."; }
        }
        else if (plan != nullptr && sel.strip >= 0 && sel.strip < int (plan->strips.size()))
        {
            const auto& sp = plan->strips[size_t (sel.strip)];
            headline = juce::String (sp.tune.headline);
            for (const auto& r : sp.mixItems) add (r);
            for (const auto& r : sp.tune.report.items) add (r);
        }
        else headline = {};
        resized();
    }

    // The block under the header: the meter column beside the level card, and the
    // sends (or the master's loudness) under it when there is anything to show.
    bool hasExtraCard() const
    {
        return (sel.isBus && sel.bus == MixBus::Master) || (! sel.isBus && anySendVisible());
    }

    int controlsHeight() const
    {
        const bool extra = hasExtraCard();
        if (! extra) return kMinControlsH;   // the meter column always keeps its height
        int n = 0;
        for (const auto& s : sends) if (s.isVisible()) ++n;
        return juce::jmax (kMinControlsH, kLevelH + 14 + (sel.isBus ? 110 : 16 + 8 + n * kSendH + 13));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& graph = controller.getGraph();
        juce::String kicker, title, sub;
        bool muted = false;
        if (sel.isBus)
        {
            kicker = sel.bus == MixBus::Master ? "Output" : "Group bus";
            title = sel.bus == MixBus::Master ? juce::String ("Master") : sentenceCase (mixBusName (sel.bus)) + " bus";
            if (sel.bus == MixBus::Master)
                sub = juce::String (mixPurposeName (controller.getSession().purpose)) + "  " + Glyph::dot() + "  stereo output";
            else
                sub = juce::String (graph.stripsOnBus (sel.bus)) + " inputs  " + Glyph::dot() + "  stereo  "
                      + Glyph::dot() + "  feeds the master";
        }
        else if (sel.strip >= 0 && sel.strip < graph.numStrips())
        {
            const auto& s = graph.strips[size_t (sel.strip)];
            kicker = deviceInLabel (s) + "  " + Glyph::dot() + "  " + juce::String (channelRoleName (s.role));
            title = s.name;
            sub = juce::String (s.numChannels() == 2 ? "stereo" : "mono") + "  " + Glyph::dot() + "  feeds "
                  + sentenceCase (mixBusName (s.bus));
            const auto& kept = controller.getBase();
            muted = sel.strip < kept.numStrips && kept.strips[size_t (sel.strip)].mute;
        }

        auto area = getLocalBounds();
        auto head = area.removeFromTop (kHeaderH);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText (kicker, head.removeFromTop (15), juce::Justification::topLeft, true);
        if (muted)
        {
            const float w = Dine::pillWidth ("Muted", true);
            auto chip = head.removeFromRight (int (w)).removeFromTop (22).toFloat();
            Dine::drawPill (g, chip, "Muted", Dine::crit, Dine::Icon::Dash);
        }
        g.setColour (Dine::ink);
        g.setFont (Dine::text (21.0f, 600));
        g.drawText (title, head.removeFromTop (26), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.5f));
        g.drawText (sub, head, juce::Justification::topLeft, true);

        area.removeFromTop (16);

        // ---- the meter column
        auto controls = area.removeFromTop (controlsHeight());
        auto meterCard = controls.removeFromLeft (92);
        Dine::drawCard (g, meterCard.toFloat());
        auto mc = meterCard.reduced (10, 11);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        g.drawText ("Meter", mc.removeFromTop (14), juce::Justification::centred);
        auto readout = mc.removeFromBottom (30);
        g.setColour (peakDb >= -1.0f ? Dine::crit : Dine::ink);
        g.setFont (Dine::mono (13.0f, 500));
        g.drawText (peakDb <= -119.0f ? Glyph::dash() : db1 (peakDb), readout.removeFromTop (16), juce::Justification::centred);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f));
        g.drawText ("dBFS", readout, juce::Justification::centred);
        controls.removeFromLeft (14);

        // ---- gain and fader
        auto level = controls.removeFromTop (hasExtraCard() ? kLevelH : controls.getHeight());
        Dine::drawCard (g, level.toFloat());
        auto lr = level.reduced (16, 14);
        if (! hasExtraCard()) lr = lr.withSizeKeepingCentre (lr.getWidth(), juce::jmin (lr.getHeight(), 62));
        auto faderCol = lr.removeFromRight (196);
        if (! sel.isBus)
        {
            auto gainCol = lr;
            gainCol.removeFromRight (20);
            auto top = gainCol.removeFromTop (16);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawText ("Input gain", top.removeFromLeft (90), juce::Justification::centredLeft);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (13.5f, 500));
            g.drawText (db1 (float (gain.getValue())) + " dB", top, juce::Justification::centredRight);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f));
            g.drawText ("A digital preamp: it sets what the chain receives.", gainCol.removeFromBottom (14), juce::Justification::centredLeft, true);
            g.setColour (Dine::hair);
            g.fillRect (float (faderCol.getX()) - 10.0f, float (level.getY()) + 12.0f, 0.5f, float (level.getHeight()) - 24.0f);
        }
        else
        {
            auto text = lr;
            text.removeFromRight (20);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawText (sel.bus == MixBus::Master ? "Broadcast bus" : "Group bus", text.removeFromTop (16), juce::Justification::topLeft);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (12.5f));
            g.drawFittedText (sel.bus == MixBus::Master
                                  ? "Four buses sum here, plus the FX returns. The only things set on the way out are the level and the ceiling."
                                  : "Every input on this group arrives here, then this one level feeds the master.",
                              text, juce::Justification::topLeft, 3);
        }
        {
            auto top = faderCol.removeFromTop (16);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawText (sel.isBus ? "Bus level" : "Level", top.removeFromLeft (80), juce::Justification::centredLeft);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (17.0f, 500));
            g.drawText (db1 (float (fader.getValue())) + " dB", top, juce::Justification::centredRight);
            auto scale = faderCol.removeFromBottom (14);
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (10.5f));
            g.drawText (Glyph::minus() + juce::String ("60"), scale, juce::Justification::centredLeft);
            g.drawText ("0", scale.withWidth (int (scale.getWidth() * 60.0f / 72.0f)), juce::Justification::centredRight);
            g.drawText ("+12", scale, juce::Justification::centredRight);
        }

        controls.removeFromTop (14);

        // ---- sends, or the master's loudness
        auto extra = controls;
        if (sel.isBus && sel.bus == MixBus::Master)
        {
            Dine::drawCard (g, extra.toFloat());
            auto r = extra.reduced (16, 14);
            auto top = r.removeFromTop (16);
            Dine::drawIcon (g, Dine::Icon::Target, top.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), Dine::glyph);
            top.removeFromLeft (8);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (12.5f, 600));
            g.drawText ("Loudness", top.removeFromLeft (90), juce::Justification::centredLeft);

            const auto& loud = controller.getEngine().getBus (MixBus::Master).getLoudness();
            const float st = loud.getShortTermLufs();
            const float tp = loud.getTruePeakDb();
            r.removeFromTop (10);
            auto figures = r.removeFromTop (28);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (26.0f, 500));
            const juce::String lufs = st > -100.0f ? juce::String (st, 1) : Glyph::dash();
            const int lw = Dine::textWidth (Dine::mono (26.0f, 500), lufs) + 10;
            g.drawText (lufs, figures.removeFromLeft (lw), juce::Justification::centredLeft);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawText ("LUFS short term", figures.removeFromLeft (110), juce::Justification::centredLeft);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (17.0f, 500));
            const juce::String peak = st > -100.0f ? juce::String (tp, 1) : Glyph::dash();
            const int pw = Dine::textWidth (Dine::mono (17.0f, 500), peak) + 10;
            g.drawText (peak, figures.removeFromLeft (pw), juce::Justification::centredLeft);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawText ("dBTP peak", figures, juce::Justification::centredLeft);
            r.removeFromTop (6);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            g.drawFittedText (juce::String (mixPurposeName (controller.getSession().purpose))
                                  + " sets the target loudness and the ceiling; the limiter only catches the peaks above it.",
                              r, juce::Justification::topLeft, 2);
        }
        else if (! sel.isBus && anySendVisible())
        {
            Dine::drawCard (g, extra.toFloat());
            auto r = extra.reduced (16, 13);
            auto top = r.removeFromTop (16);
            Dine::drawIcon (g, Dine::Icon::Fx, top.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), Dine::glyph);
            top.removeFromLeft (8);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (12.5f, 600));
            g.drawText ("Sends", top, juce::Justification::centredLeft);
            r.removeFromTop (8);
            for (int f = 0; f < int (FxSlot::Count); ++f)
            {
                if (! sends[size_t (f)].isVisible()) continue;
                auto row = r.removeFromTop (kSendH);
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (12.0f));
                g.drawText (sendLabel (FxSlot (f)), row.removeFromLeft (84), juce::Justification::centredLeft);
                g.setColour (Dine::ink);
                g.setFont (Dine::mono (12.0f));
                g.drawText (db1 (float (sends[size_t (f)].getValue())) + " dB", row.removeFromRight (62), juce::Justification::centredRight);
            }
        }

        // ---- what TUNE MIX decided
        area.removeFromTop (14);
        Dine::drawCard (g, area.toFloat());
        auto r = area.reduced (16, 14);
        auto top = r.removeFromTop (16);
        Dine::drawIcon (g, Dine::Icon::Waveform, top.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), Dine::accent);
        top.removeFromLeft (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f, 600));
        g.drawText ("What TUNE MIX decided, and why", top.removeFromLeft (230), juce::Justification::centredLeft);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        g.drawText (headline, top, juce::Justification::centredRight, true);
    }

    bool anySendVisible() const
    {
        for (const auto& s : sends) if (s.isVisible()) return true;
        return false;
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (kHeaderH + 16);
        auto controls = area.removeFromTop (controlsHeight());
        auto meterCard = controls.removeFromLeft (92);
        auto mc = meterCard.reduced (10, 11);
        mc.removeFromTop (14 + 6);
        mc.removeFromBottom (30);
        meter.setBounds (mc.withSizeKeepingCentre (24, mc.getHeight()));
        controls.removeFromLeft (14);

        auto level = controls.removeFromTop (hasExtraCard() ? kLevelH : controls.getHeight());
        auto lr = level.reduced (16, 14);
        if (! hasExtraCard()) lr = lr.withSizeKeepingCentre (lr.getWidth(), juce::jmin (lr.getHeight(), 62));
        auto faderCol = lr.removeFromRight (196);
        faderCol.removeFromTop (16 + 4);
        faderCol.removeFromBottom (14);
        fader.setBounds (faderCol.withHeight (24));
        if (! sel.isBus)
        {
            auto gainCol = lr;
            gainCol.removeFromRight (20);
            gainCol.removeFromTop (16 + 4);
            gainCol.removeFromBottom (14);
            auto row = gainCol.withHeight (24);
            muteButton.setBounds (row.removeFromRight (juce::jmax (72, muteButton.idealWidth())).withHeight (Dine::Metric::control).withY (row.getY() + 1));
            row.removeFromRight (8);
            soloButton.setBounds (row.removeFromRight (juce::jmax (64, soloButton.idealWidth())).withHeight (Dine::Metric::control).withY (row.getY() + 1));
            row.removeFromRight (12);
            gain.setBounds (row);
        }
        else if (soloButton.isVisible())
        {
            auto gainCol = lr;
            gainCol.removeFromRight (20);
            gainCol.removeFromTop (16 + 4);
            gainCol.removeFromBottom (14);
            auto row = gainCol.withHeight (24);
            soloButton.setBounds (row.removeFromRight (juce::jmax (64, soloButton.idealWidth())).withHeight (Dine::Metric::control).withY (row.getY() + 1));
        }

        controls.removeFromTop (14);
        auto extra = controls;
        if (! sel.isBus)
        {
            auto r = extra.reduced (16, 13);
            r.removeFromTop (16 + 8);
            for (auto& s : sends)
            {
                if (! s.isVisible()) continue;
                auto row = r.removeFromTop (kSendH);
                row.removeFromLeft (84);
                row.removeFromRight (62 + 8);
                s.setBounds (row.withSizeKeepingCentre (row.getWidth(), 20));
            }
        }

        area.removeFromTop (14);
        auto reportArea = area.reduced (16, 14);
        reportArea.removeFromTop (16 + 8);
        reportView.setBounds (reportArea);
        const int h = report.layoutFor (reportArea.getWidth() - 12);
        report.setSize (reportArea.getWidth() - (h > reportArea.getHeight() ? 10 : 0), juce::jmax (h, reportArea.getHeight()));
    }

    static constexpr int kHeaderH = 60, kMinControlsH = 196, kLevelH = 88, kSendH = 26;

private:
    MixController& controller;
    Selection sel;
    bool updating = false;
    float peakDb = -120.0f;
    juce::String headline;
    juce::Slider gain, fader;
    std::array<juce::Slider, int (FxSlot::Count)> sends;

    DineButton muteButton { "Mute", DineButton::Style::Standard };
    DineButton soloButton { "Solo", DineButton::Style::Standard };
    DineMeter meter;
    juce::Viewport reportView;
    Report report;
};

// ------------------------------------------------------------------ AdvancedPage
AdvancedPage::AdvancedPage (MixController& c) : controller (c)
{
    viewport.setViewedComponent (&listHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    detail = std::make_unique<Detail> (controller);
    addAndMakeVisible (*detail);
    rebuild();
}

AdvancedPage::~AdvancedPage() = default;

void AdvancedPage::rebuild()
{
    listItems.clear();
    rows.clear();
    listHolder.removeAllChildren();

    const auto& graph = controller.getGraph();
    auto addHeader = [&] (const juce::String& title, const juce::String& count, MixBus bus)
    {
        auto h = std::make_unique<SectionHeader> (title, count, bus);
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

        addHeader (sentenceCase (mixBusName (bus)), juce::String (n) + " ch", bus);

        for (int i = 0; i < graph.numStrips(); ++i)
        {
            const auto& s = graph.strips[size_t (i)];
            if (s.bus != bus) continue;
            auto row = std::make_unique<Row> (s.name, Row::Kind::Channel, s.bus, s.role, i);
            row->setClickingTogglesState (false);
            row->onClick = [this, i] { select (i); };
            addRow (std::move (row));
        }

        if (controller.isPrepared() && controller.getEngine().isBusUsed (bus))
        {
            auto row = std::make_unique<Row> (sentenceCase (mixBusName (bus)) + " bus", Row::Kind::Bus, bus, ChannelRole::KickIn);
            row->setClickingTogglesState (false);
            row->onClick = [this, bus] { selectBus (bus); };
            addRow (std::move (row));
        }
    }

    if (controller.isPrepared() && controller.getEngine().isBusUsed (MixBus::Master))
    {
        addHeader ("Output", "1", MixBus::Master);
        auto row = std::make_unique<Row> ("Master", Row::Kind::Master, MixBus::Master, ChannelRole::KickIn);
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
            r->set (m.consumeMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped(), db1 (st.faderDb), st.mute, st.solo);
        }
        else
        {
            const auto& m = engine.getBus (r->bus).getOutputMeter();
            r->set (m.consumeMaxPeakDb(), m.getMaxRmsDb(), m.hasClipped(),
                    db1 (kept.buses[size_t (r->bus)].faderDb), kept.buses[size_t (r->bus)].mute,
                    kept.buses[size_t (r->bus)].solo);
        }
    }
    detail->refresh();
}

void AdvancedPage::paint (juce::Graphics& g)
{
    auto list = getLocalBounds().removeFromLeft (kListWidth);
    g.setColour (Dine::rail);
    g.fillRect (list);
    g.setColour (Dine::hair);
    g.fillRect (float (list.getRight()) - 0.5f, 0.0f, 0.5f, float (getHeight()));
}

void AdvancedPage::resized()
{
    auto area = getLocalBounds();
    auto list = area.removeFromLeft (kListWidth);
    viewport.setBounds (list.reduced (8, 0).withTrimmedTop (10).withTrimmedBottom (10));

    const int rowH = 30, headerH = 26, sectionGap = 8;
    int total = 0;
    bool firstHeader = true;
    for (auto& item : listItems)
    {
        if (dynamic_cast<SectionHeader*> (item.get()) != nullptr)
        {
            total += (firstHeader ? 0 : sectionGap) + headerH;
            firstHeader = false;
        }
        else total += rowH + 1;
    }

    listHolder.setSize (viewport.getWidth() - (total > viewport.getHeight() ? 10 : 0), juce::jmax (total, viewport.getHeight()));
    int y = 0;
    firstHeader = true;
    for (auto& item : listItems)
    {
        if (dynamic_cast<SectionHeader*> (item.get()) != nullptr)
        {
            if (! firstHeader) y += sectionGap;
            firstHeader = false;
            item->setBounds (0, y, listHolder.getWidth(), headerH);
            y += headerH;
        }
        else
        {
            item->setBounds (0, y, listHolder.getWidth(), rowH);
            y += rowH + 1;
        }
    }

    detail->setBounds (area.reduced (24, 22));
}

} // namespace livemix
