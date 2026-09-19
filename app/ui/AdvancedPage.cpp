#include "AdvancedPage.h"
#include "Core/DbUtils.h"
#include "DSP/ChannelProcessor.h"

namespace livemix
{

namespace
{
    constexpr int kHeadH  = 108;   // the channel header
    constexpr int kFootH  = 72;    // the rail's engine footer
    constexpr int kPadX   = 22;

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::String panText (float pan)
    {
        if (std::fabs (pan) < 0.005f) return "Centre";
        const int amount = int (std::round (std::fabs (pan) * 100.0f));
        return (pan < 0.0f ? "L" : "R") + juce::String (amount);
    }

    // 1-based device channel(s) the strip is patched to ("in 4", "in 9/10").
    juce::String deviceInLabel (const StripRoute& s)
    {
        if (s.inputA < 0) return {};
        if (s.inputB >= 0)
            return "in " + juce::String (s.inputA + 1) + "/" + juce::String (s.inputB + 1);
        return "in " + juce::String (s.inputA + 1);
    }

    // The rail is read against a dark ground, so the master is drawn in full ink here; every
    // group keeps the colour it has everywhere else.
    juce::Colour busTint (MixBus b) noexcept
    {
        return b == MixBus::Master ? Dine::ink : Dine::busTint (b);
    }

    // "DRUMS" -> "Drums": capitals are kept for the product verbs and the small labels.
    juce::String sentenceCase (const juce::String& s)
    {
        return s.substring (0, 1).toUpperCase() + s.substring (1).toLowerCase();
    }

    juce::Font capsFont (float px, int weight = 600)
    {
        return Dine::caps (px, 0.08f, weight);
    }

    void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                   juce::Colour c, float px = 9.5f, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (c);
        g.setFont (capsFont (px, 600));
        g.drawText (text, r, j, true);
    }
}

// ------------------------------------------------------------------ SectionHeader
class AdvancedPage::SectionHeader : public juce::Component
{
public:
    SectionHeader (const juce::String& title, const juce::String& count, MixBus bus)
        : label (title.toUpperCase()), countText (count), tint (busTint (bus)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (12, 0);
        auto dot = r.removeFromLeft (6);
        g.setColour (tint.withAlpha (0.9f));
        g.fillEllipse (dot.withSizeKeepingCentre (5, 5).toFloat());
        r.removeFromLeft (8);
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (9.5f, 0.12f));
        g.drawText (label, r.removeFromLeft (r.getWidth() - 30), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
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

    Row (const juce::String& title, Kind k, MixBus busFamily, int stripIndex = -1)
        : juce::Button (title), name (title), kind (k), bus (busFamily), strip (stripIndex) {}

    void set (float peakDb, const juce::String& levelText, bool muted, bool soloed)
    {
        level = juce::jmax (peakDb, level - 1.6f);   // fall back smoothly instead of flickering
        text = levelText;
        mute = muted;
        solo = soloed;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        const auto tint = busTint (bus);
        auto b = getLocalBounds();
        if (on)        { g.setColour (Dine::card); g.fillRect (b); }
        else if (over) { g.setColour (Dine::tile); g.fillRect (b); }

        auto r = b.reduced (12, 0);
        auto dot = r.removeFromLeft (6).withSizeKeepingCentre (6, 6);
        g.setColour (mute ? Dine::warn : solo ? Dine::accent : tint);
        g.fillEllipse (dot.toFloat());
        r.removeFromLeft (8);

        auto bar = r.removeFromRight (26).withSizeKeepingCentre (26, 4);
        Dine::fillRounded (g, bar.toFloat(), Dine::hair, 2.0f);
        const float n = mute ? 0.0f : DineMeter::norm (level);
        if (n > 0.001f) Dine::fillRounded (g, bar.toFloat().withWidth (juce::jmax (2.0f, bar.getWidth() * n)), level > -6.0f ? Dine::hot : Dine::accent, 2.0f);
        r.removeFromRight (10);

        g.setColour (mute ? Dine::ink4 : on ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (12.5f, on ? 600 : 400));
        g.drawText (name, r, juce::Justification::centredLeft, true);
        if (mute)
        {
            g.setColour (Dine::ink4);
            g.fillRect (r.getX(), r.getCentreY(), juce::jmin (r.getWidth(), Dine::textWidth (Dine::text (12.5f), name)), 1);
        }
    }

    juce::String name, text;
    Kind kind;
    MixBus bus;
    int strip = -1;
    bool mute = false, solo = false;
    float level = -120.0f;
};

// ------------------------------------------------------------------ Head
// The channel itself: what it is, the meters either side of its chain, its level, and
// the keys. buildHead is the one place these sit, so paint and layout cannot disagree.
class AdvancedPage::Head : public juce::Component
{
public:
    explicit Head (MixController& c) : controller (c)
    {
        auto setup = [] (juce::Slider& s, double lo, double hi, double def)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            Dine::dragOnly (s);
            s.setRange (lo, hi, 0.5);
            s.setDoubleClickReturnValue (true, def);
        };
        setup (gain, -24.0, 24.0, 0.0);
        setup (fader, -60.0, 12.0, 0.0);
        gain.onValueChange = [this]
        {
            if (! updating && ! sel.isBus) controller.setStripInputGain (sel.strip, float (gain.getValue()));
            repaint();
        };
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (sel.isBus) controller.setBusFader (sel.bus, float (fader.getValue()));
            else           controller.setStripFader (sel.strip, float (fader.getValue()));
            repaint();
        };
        pan.setTooltip ("Balance. Double-click for the centre.");
        pan.onChange = [this] (float v) { if (! updating && ! sel.isBus) controller.setStripPan (sel.strip, v); repaint(); };

        for (auto* b : { &muteButton, &soloButton })
        {
            b->setClickingTogglesState (false);
            b->setCaps (true);
            b->setFontPx (11.0f);
            b->setPadX (6);
            addAndMakeVisible (*b);
        }
        muteButton.onClick = [this]
        {
            if (! sel.isBus) controller.setStripMute (sel.strip, ! controller.getKept().strips[size_t (sel.strip)].mute);
        };
        soloButton.onClick = [this]
        {
            if (sel.isBus)
            {
                if (sel.bus != MixBus::Master)
                    controller.setBusSolo (sel.bus, ! controller.getKept().buses[size_t (sel.bus)].solo);
            }
            else controller.setStripSolo (sel.strip, ! controller.getKept().strips[size_t (sel.strip)].solo);
        };

        addAndMakeVisible (gain);
        addAndMakeVisible (fader);
        addChildComponent (pan);
    }

    void show (const Selection& s)
    {
        sel = s;
        const bool strip = ! sel.isBus;
        gain.setVisible (strip);
        pan.setVisible (strip);
        muteButton.setVisible (strip);
        soloButton.setVisible (strip || sel.bus != MixBus::Master);
        refresh();
        resized();
    }

    void refresh()
    {
        const auto& kept = controller.getBase();
        updating = true;
        if (controller.isBypassed() != bypassed)
        {
            bypassed = controller.isBypassed();
            gain.setEnabled (! bypassed);
            fader.setEnabled (! bypassed);
            pan.setEnabled (! bypassed);
        }
        if (sel.isBus)
        {
            fader.setValue (kept.buses[size_t (sel.bus)].faderDb, juce::dontSendNotification);
            if (sel.bus != MixBus::Master)
            {
                const bool on = kept.buses[size_t (sel.bus)].solo;
                soloButton.setTint (Dine::keySolo);
                soloButton.setStyle (on ? DineButton::Style::Filled : DineButton::Style::Standard);
            }
        }
        else if (sel.strip >= 0 && sel.strip < kept.numStrips)
        {
            const auto& st = kept.strips[size_t (sel.strip)];
            gain.setValue (st.inputGainDb, juce::dontSendNotification);
            fader.setValue (st.faderDb, juce::dontSendNotification);
            pan.setValue (st.pan);
            muteButton.setTint (Dine::keyMute);
            soloButton.setTint (Dine::keySolo);
            muteButton.setStyle (st.mute ? DineButton::Style::Filled : DineButton::Style::Standard);
            soloButton.setStyle (st.solo ? DineButton::Style::Filled : DineButton::Style::Standard);
        }
        updating = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& graph = controller.getGraph();
        auto lay = layout();

        juce::String title, sub, provenance;
        juce::Colour tint = busTint (sel.isBus ? sel.bus : MixBus::Master);
        const auto* plan = controller.getPlan();
        if (sel.isBus)
        {
            title = sel.bus == MixBus::Master ? juce::String ("Master") : sentenceCase (mixBusName (sel.bus)) + " bus";
            sub = sel.bus == MixBus::Master
                      ? juce::String (mixPurposeName (controller.getSession().purpose)) + "  " + Glyph::dot() + "  stereo output"
                      : juce::String (graph.stripsOnBus (sel.bus)) + " inputs  " + Glyph::dot() + "  feeds MASTER";
            provenance = plan != nullptr ? "TUNED BY DLIVE" : "BASELINE";
        }
        else if (sel.strip >= 0 && sel.strip < graph.numStrips())
        {
            const auto& s = graph.strips[size_t (sel.strip)];
            tint = busTint (s.bus);
            title = s.name;
            sub = juce::String (channelRoleName (s.role)) + "  " + Glyph::dot() + "  " + deviceInLabel (s) + "  " + Glyph::dot()
                  + "  feeds " + juce::String (mixBusName (s.bus)).toUpperCase();
            provenance = plan == nullptr ? "BASELINE" : "TUNED BY DLIVE";
        }

        Dine::fillRounded (g, lay.colour.toFloat(), tint, Dine::Radius::control);
        auto text = lay.text;
        g.setColour (Dine::ink);
        g.setFont (Dine::text (19.0f, 600));
        g.drawText (title, text.removeFromTop (24), juce::Justification::centredLeft, true);
        text.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawText (sub + "  " + Glyph::dot() + "  " + provenance, text.removeFromTop (16), juce::Justification::topLeft, true);

        if (lay.showMeters)
        {
            const auto* proc = processor();
            paintMeter (g, lay.meterIn, "IN", proc != nullptr ? &proc->getInputMeter() : nullptr, tint);
            paintMeter (g, lay.meterOut, "OUT", proc != nullptr ? &proc->getOutputMeter() : nullptr, tint);
        }

        if (lay.showGain && ! sel.isBus)
        {
            auto col = lay.gain;
            auto top = col.removeFromTop (14);
            drawCaps (g, "INPUT GAIN", top.removeFromLeft (Dine::textWidth (capsFont (9.5f), "INPUT GAIN")), Dine::ink4);
            g.setColour (Dine::ink2);
            g.setFont (Dine::mono (12.0f, 500));
            g.drawText (db1 (float (gain.getValue())) + " dB", top, juce::Justification::centredRight);
            col.removeFromTop (26);
            auto panRow = col.removeFromTop (18);
            drawCaps (g, "PAN", panRow.removeFromLeft (30), Dine::ink4);
            g.setColour (Dine::ink2);
            g.setFont (Dine::mono (10.5f));
            g.drawText (panText (float (pan.getValue())), panRow.removeFromRight (46), juce::Justification::centredRight);
        }

        {
            auto col = lay.level;
            auto top = col.removeFromTop (16);
            const juce::String levelLabel = sel.isBus ? "BUS LEVEL" : "LEVEL";
            drawCaps (g, levelLabel, top.removeFromLeft (Dine::textWidth (capsFont (9.5f), levelLabel)), Dine::ink4);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (16.0f, 500));
            g.drawText (db1 (float (fader.getValue())) + " dB", top, juce::Justification::centredRight);
            col.removeFromTop (24);
            auto scale = col.removeFromTop (12);
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (9.5f));
            g.drawText (Glyph::minus() + juce::String ("60"), scale, juce::Justification::centredLeft);
            g.drawText ("0", scale.withWidth (int (scale.getWidth() * 60.0f / 72.0f)), juce::Justification::centredRight);
            g.drawText ("+12", scale, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto lay = layout();
        auto gainCol = lay.gain;
        gainCol.removeFromTop (14);
        gain.setBounds (gainCol.removeFromTop (24));
        gainCol.removeFromTop (2);
        auto panRow = gainCol.removeFromTop (18);
        panRow.removeFromLeft (30);
        panRow.removeFromRight (46);
        pan.setBounds (panRow.withSizeKeepingCentre (juce::jmax (20, panRow.getWidth()), 16));

        auto levelCol = lay.level;
        levelCol.removeFromTop (16);
        fader.setBounds (levelCol.removeFromTop (24));

        auto keys = lay.keys;
        if (muteButton.isVisible())
        {
            muteButton.setBounds (keys.removeFromLeft (48).withSizeKeepingCentre (48, 44));
            keys.removeFromLeft (8);
        }
        if (soloButton.isVisible()) soloButton.setBounds (keys.removeFromLeft (48).withSizeKeepingCentre (48, 44));
    }

private:
    struct Lay
    {
        juce::Rectangle<int> colour, text, meterIn, meterOut, gain, level, keys;
        int rule = 0;
        bool showMeters = true, showGain = true;
    };

    // Everything in the header, positioned once. A narrow window loses the meters before
    // it loses a control.
    Lay layout() const
    {
        Lay l;
        auto r = getLocalBounds().reduced (kPadX, 0);
        const int keysW = (muteButton.isVisible() ? 56 : 0) + (soloButton.isVisible() ? 48 : 0);
        l.showGain = ! sel.isBus && r.getWidth() > 700;
        l.showMeters = r.getWidth() > (l.showGain ? 860 : 700);

        l.colour = r.removeFromLeft (32).withSizeKeepingCentre (32, 32);
        r.removeFromLeft (16);
        const int textW = juce::jmin (300, r.getWidth() / 3);
        l.text = r.removeFromLeft (textW).withSizeKeepingCentre (textW, 44);
        if (l.showMeters)
        {
            r.removeFromLeft (20);
            l.rule = r.getX();
            r.removeFromLeft (4);
            l.meterIn = r.removeFromLeft (78).withSizeKeepingCentre (78, 48);
            r.removeFromLeft (12);
            l.meterOut = r.removeFromLeft (78).withSizeKeepingCentre (78, 48);
        }
        l.keys = r.removeFromRight (juce::jmax (0, keysW)).withSizeKeepingCentre (juce::jmax (0, keysW), 44);
        if (keysW > 0) r.removeFromRight (16);
        const int levelW = juce::jmin (216, juce::jmax (140, r.getWidth() / 2));
        l.level = r.removeFromRight (levelW).withSizeKeepingCentre (levelW, 52);
        if (l.showGain)
        {
            r.removeFromRight (18);
            const int gainW = juce::jlimit (0, 160, r.getWidth());
            l.gain = r.removeFromRight (gainW).withSizeKeepingCentre (gainW, 58);
        }
        return l;
    }

    const ChannelProcessor* processor() const
    {
        if (! controller.isPrepared()) return nullptr;
        if (sel.isBus) return &controller.getEngine().getBus (sel.bus);
        if (sel.strip >= 0 && sel.strip < controller.getGraph().numStrips())
            return &controller.getEngine().getStrip (sel.strip);
        return nullptr;
    }

    // Non-consuming read: the rail owns consumeMaxPeakDb each tick.
    void paintMeter (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label,
                     const LevelMeter* meter, juce::Colour) const
    {
        auto top = r.removeFromTop (14);
        const float peak = meter != nullptr ? meter->getMaxPeakDb() : -120.0f;
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        g.drawText (label + " " + (peak <= -119.0f ? juce::String (Glyph::dash()) : db1 (peak)), top, juce::Justification::centredLeft);
        r.removeFromTop (6);
        const int channels = meter != nullptr ? juce::jlimit (1, 2, meter->getNumChannels()) : 1;
        for (int c = 0; c < channels; ++c)
        {
            auto bar = r.removeFromTop (5);
            r.removeFromTop (3);
            const float db = meter != nullptr ? meter->getPeakDb (c) : -120.0f;
            Dine::fillMeter (g, bar.toFloat(), DineMeter::norm (db), false, false, 2.0f);
        }
    }

    MixController& controller;
    Selection sel;
    bool updating = false, bypassed = false;
    juce::Slider gain, fader;
    PanBar pan;
    DineButton muteButton { "MUTE", DineButton::Style::Standard };
    DineButton soloButton { "SOLO", DineButton::Style::Standard };
};

// ------------------------------------------------------------------ Trail
// What DINE did to this channel: the headline, then one line per stage - what it is set
// to, where the setting came from, and the sentence that explains it. Click a line to
// open that stage.
class AdvancedPage::Trail : public juce::Component
{
public:
    explicit Trail (MixController& c) : controller (c), list (*this)
    {
        tuneChannel.setCaps (true);
        tuneChannel.setFontPx (11.5f);
        tuneChannel.setIcon (Dine::Icon::Waveform);
        tuneChannel.setTooltip ("Listen to this channel on its own and tune it. Nothing else in the mix moves.");
        tuneChannel.onClick = [this] { if (onTuneChannel) onTuneChannel(); };
        addChildComponent (tuneChannel);

        retune.setCaps (true);
        retune.setFontPx (11.5f);
        retune.setTooltip ("Listen to the whole band again and rebuild the mix.");
        retune.onClick = [this] { if (onRetune) onRetune(); };
        addAndMakeVisible (retune);

        revertAll.setCaps (true);
        revertAll.setFontPx (11.5f);
        revertAll.setTooltip ("Put this channel back the way TUNE MIX set it - every stage, its level and its sends.");
        revertAll.onClick = [this] { if (onRevertAll) onRevertAll(); };
        addAndMakeVisible (revertAll);

        view.setViewedComponent (&list, false);
        Dine::nativeScrolling (view);
        view.setScrollBarsShown (true, false);
        addAndMakeVisible (view);
    }

    std::function<void (int)> onPick;
    std::function<void()> onRetune, onRevertAll, onTuneChannel;

    // `channel` is false for a bus: a bus is not a source, so there is nothing to listen to
    // on its own - it is tuned by what feeds it.
    void setChannel (const juce::String& headlineText, const juce::String& sentenceText, bool masterSelected, bool channel)
    {
        headline = headlineText;
        sentence = sentenceText;
        isMaster = masterSelected;
        if (tuneChannel.isVisible() != channel) tuneChannel.setVisible (channel);
        resized();
        repaint();
    }

    // Gain staging is the first move in a mix, so it is the first thing this column says:
    // what reached the converter, what DLIVE did digitally, and what the console preamp
    // should still do. Buses have no input, so they get no card.
    void setGain (const MixController::InputAdvice& a)
    {
        if (a.known == advice.known && a.level == advice.level
            && std::fabs (a.capturePeakDb - advice.capturePeakDb) < 0.05f
            && std::fabs (a.digitalGainDb - advice.digitalGainDb) < 0.05f
            && a.headline == advice.headline) return;
        advice = a;
        resized();
        repaint();
    }

    void setStages (const std::vector<ChainEditor::StageView>& v, int selectedStage)
    {
        if (v.size() == list.views.size() && selectedStage == list.selected)
        {
            bool same = true;
            for (size_t i = 0; i < v.size() && same; ++i)
                same = v[i].label == list.views[i].label && v[i].value == list.views[i].value
                       && v[i].on == list.views[i].on && v[i].edited == list.views[i].edited
                       && v[i].why == list.views[i].why;
            if (same) return;
        }
        list.views = v;
        list.selected = selectedStage;
        list.setSize (view.getWidth(), list.heightFor (view.getWidth()));
        list.repaint();
    }

    // The foot follows the channel's own meter, so the headroom is the one the engineer
    // is looking at.
    void refresh (float channelPeakDb)
    {
        if (std::fabs (channelPeakDb - peak) < 0.05f) return;
        peak = channelPeakDb;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto head = area.removeFromTop (34).reduced (18, 0).withTrimmedTop (14);
        Dine::drawSection (g, head.withTrimmedRight (40), "WHAT DLIVE DID");
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        g.drawText (controller.getTuneCount() > 0 ? "TUNE " + juce::String (controller.getTuneCount()) : juce::String ("no tune yet"),
                    head.withTrimmedRight (26), juce::Justification::centredRight);

        auto top = area.removeFromTop (sentenceHeight()).reduced (18, 0);
        top.removeFromTop (8);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawFittedText (sentence, top.removeFromTop (juce::jmax (0, top.getHeight() - (tuneChannel.isVisible() ? 82 : 44))),
                          juce::Justification::topLeft, 5, 1.0f);

        if (const int gh = gainHeight(); gh > 0)
            paintGain (g, area.removeFromTop (gh).reduced (18, 0).withTrimmedTop (4).withTrimmedBottom (8));

        auto foot = getLocalBounds().removeFromBottom (kFooterH);
        paintFoot (g, foot.reduced (18, 0).withTrimmedTop (10));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (34);
        auto top = area.removeFromTop (sentenceHeight()).reduced (18, 0);
        auto buttons = top.removeFromBottom (36).withHeight (Dine::Metric::button);
        retune.setBounds (buttons.removeFromLeft ((buttons.getWidth() - 8) / 2));
        buttons.removeFromLeft (8);
        revertAll.setBounds (buttons);
        // TUNE CHANNEL sits above the pair, full width: this column is about one channel,
        // so its own verb comes first and RE-TUNE stays what it is - the whole mix.
        if (tuneChannel.isVisible())
            tuneChannel.setBounds (top.removeFromBottom (Dine::Metric::button + 8).withHeight (Dine::Metric::button));

        area.removeFromTop (gainHeight());
        area.removeFromBottom (kFooterH);
        view.setBounds (area);
        list.setSize (view.getWidth(), list.heightFor (view.getWidth()));
    }

    static constexpr int kFooterH = 62;

private:
    int sentenceHeight() const { return tuneChannel.isVisible() ? 132 : 96; }

    int gainHeight() const
    {
        if (! advice.known) return 0;
        return advice.needsAttention() ? 122 : 62;
    }

    static juce::Colour gainColour (MixController::InputAdvice::Level level)
    {
        using Level = MixController::InputAdvice::Level;
        switch (level)
        {
            case Level::Clipping:
            case Level::Faint:    return Dine::crit;
            case Level::Low:
            case Level::Hot:
            case Level::Digital:  return Dine::warn;
            case Level::NotHeard: return Dine::ink4;
            default:              return Dine::ok;
        }
    }

    void paintGain (juce::Graphics& g, juce::Rectangle<int> r) const
    {
        const auto colour = gainColour (advice.level);
        Dine::fillRounded (g, r.toFloat(), Dine::mix (colour, 0.16f), Dine::Radius::control);
        r = r.reduced (12, 12);
        g.setColour (colour);
        g.setFont (Dine::caps (11.0f, 0.08f, 500));
        g.drawText (juce::String (advice.headline).toUpperCase(), r.removeFromTop (14), juce::Justification::centredLeft, true);
        r.removeFromTop (6);
        auto figures = r.removeFromTop (13);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        if (std::fabs (advice.digitalGainDb) >= 0.05f)
            g.drawText ("DLIVE " + db1 (advice.digitalGainDb), figures.removeFromRight (66), juce::Justification::centredRight);
        g.drawText (advice.capturePeakDb <= -119.0f ? juce::String ("no signal")
                                                    : juce::String (advice.capturePeakDb, 1) + " dBFS in",
                    figures, juce::Justification::centredLeft, true);
        if (! advice.needsAttention()) return;
        r.removeFromTop (5);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f));
        g.drawFittedText (juce::String (advice.detail), r, juce::Justification::topLeft, 4, 1.0f);
    }

    void paintFoot (juce::Graphics& g, juce::Rectangle<int> r)
    {
        if (isMaster && controller.isPrepared())
        {
            const auto& loud = controller.getEngine().getBus (MixBus::Master).getLoudness();
            const float st = loud.getShortTermLufs();
            const float tp = loud.getTruePeakDb();
            drawCaps (g, "LOUDNESS", r.removeFromTop (14), Dine::ink4);
            auto figures = r.removeFromTop (26);
            const auto bigFont = Dine::mono (20.0f, 500);
            const juce::String lufs = st > -100.0f ? juce::String (st, 1) : Glyph::dash();
            g.setColour (Dine::ink);
            g.setFont (bigFont);
            g.drawText (lufs, figures.removeFromLeft (Dine::textWidth (bigFont, lufs) + 8), juce::Justification::centredLeft);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f));
            g.drawText ("LUFS short term", figures, juce::Justification::centredLeft, true);
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (11.0f));
            g.drawText (st > -100.0f ? juce::String (tp, 1) + " dBTP" : juce::String(), r.removeFromTop (14),
                        juce::Justification::topLeft);
            return;
        }

        // How far the channel is from the ceiling right now: the number an engineer
        // glances at before touching anything.
        const float head = juce::jlimit (0.0f, 24.0f, -peak);
        auto top = r.removeFromTop (16);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        g.drawText ("Headroom", top.removeFromLeft (top.getWidth() - 80), juce::Justification::centredLeft);
        g.setColour (Dine::ink);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (peak <= -119.0f ? Glyph::dash() : juce::String (head, 1) + " dB", top, juce::Justification::centredRight);
        r.removeFromTop (6);
        auto bar = r.removeFromTop (6);
        Dine::drawWell (g, bar.toFloat(), 2.0f);
        if (peak > -119.0f)
        {
            g.setColour (head < 3.0f ? Dine::crit : head < 6.0f ? Dine::warn : Dine::accent);
            g.fillRect (bar.toFloat().withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, head / 12.0f)));
        }
        r.removeFromTop (6);
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.0f));
        g.drawText (headline, r.removeFromTop (14), juce::Justification::topLeft, true);
    }

    // The list itself: one row per stage, painted in one place and hit-tested the same way.
    class List : public juce::Component
    {
    public:
        explicit List (Trail& owner) : trail (owner) {}

        int heightFor (int width)
        {
            heights.clear();
            int y = 0;
            const auto whyFont = Dine::text (12.0f);
            for (const auto& v : views)
            {
                const float w = juce::GlyphArrangement::getStringWidth (whyFont, v.why);
                const int lines = juce::jlimit (1, 4, int (std::ceil (w / juce::jmax (60.0f, float (width) - 36 - 24.0f))));
                const int h = 12 + 16 + 5 + 14 + 5 + lines * 16 + 12 + 3;
                heights.push_back (h);
                y += h;
            }
            return juce::jmax (y, 1);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (18, 0);
            for (size_t i = 0; i < views.size() && i < heights.size(); ++i)
            {
                const auto& v = views[i];
                auto row = r.removeFromTop (heights[i]).withTrimmedBottom (3);
                Dine::fillRounded (g, row.toFloat(), Dine::item, Dine::Radius::control);
                if (int (i) == selected) Dine::hairlineRounded (g, row.toFloat(), Dine::accent.withAlpha (0.7f), Dine::Radius::control);
                auto body = row.reduced (12, 12);

                auto top = body.removeFromTop (16);
                const auto tag = v.edited ? juce::String ("EDITED") : v.on ? juce::String ("TUNED") : juce::String ("NOT USED");
                const auto tint = v.edited ? Dine::monitor : v.on ? Dine::accent : Dine::ink4;
                const int tagW = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), tag) + 14;
                Dine::drawStatusChip (g, top.removeFromRight (tagW).toFloat(), tag, tint);
                top.removeFromRight (8);
                g.setColour (v.on ? Dine::ink : Dine::ink3);
                g.setFont (Dine::text (13.0f));
                g.drawText (v.label.substring (0, 1) + v.label.substring (1).toLowerCase(), top, juce::Justification::centredLeft, true);

                body.removeFromTop (5);
                g.setColour (Dine::ink2);
                g.setFont (Dine::mono (11.0f, 500));
                g.drawText (v.value, body.removeFromTop (14), juce::Justification::centredLeft, true);
                body.removeFromTop (5);
                g.setColour (Dine::ink3);
                g.setFont (Dine::text (12.0f));
                g.drawFittedText (v.why, body, juce::Justification::topLeft, 4, 1.0f);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown() || ! trail.onPick) return;
            int y = 0;
            for (size_t i = 0; i < heights.size(); ++i)
            {
                if (e.y >= y && e.y < y + heights[i]) { trail.onPick (int (i)); return; }
                y += heights[i];
            }
        }

        void mouseMove (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::vector<ChainEditor::StageView> views;
        int selected = 0;

    private:
        Trail& trail;
        std::vector<int> heights;
    };

    MixController& controller;
    juce::String headline, sentence;
    MixController::InputAdvice advice;
    float peak = -120.0f;
    bool isMaster = false;
    DineButton tuneChannel { "Tune channel", DineButton::Style::Standard };
    DineButton retune { "Re-tune", DineButton::Style::Filled };
    DineButton revertAll { "Revert", DineButton::Style::Standard };
    juce::Viewport view;
    List list;
};

// ------------------------------------------------------------------ AdvancedPage
AdvancedPage::AdvancedPage (MixController& c) : controller (c)
{
    viewport.setViewedComponent (&listHolder, false);
    Dine::nativeScrolling (viewport);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    head = std::make_unique<Head> (controller);
    addAndMakeVisible (*head);

    chain = std::make_unique<ChainEditor> (controller);
    addAndMakeVisible (*chain);

    path = std::make_unique<SignalPath> (*chain);
    addAndMakeVisible (*path);

    trail = std::make_unique<Trail> (controller);
    trail->onPick = [this] (int stage) { chain->selectStage (stage); };
    trail->onRetune = [this] { if (onRetune) onRetune(); };
    trail->onTuneChannel = [this] { if (onTuneChannel && ! selection.isBus && selection.strip >= 0) onTuneChannel (selection.strip); };
    trail->onRevertAll = [this]
    {
        // Everything DINE set, back the way it set it - the faders included.
        const auto* plan = controller.getPlan();
        if (plan == nullptr) return;
        if (selection.isBus)
        {
            controller.setBusChannel (selection.bus, plan->proposed.buses[size_t (selection.bus)].channel);
            controller.setBusFader (selection.bus, plan->proposed.buses[size_t (selection.bus)].faderDb);
        }
        else if (selection.strip >= 0 && selection.strip < plan->proposed.numStrips)
        {
            const auto& p = plan->proposed.strips[size_t (selection.strip)];
            controller.setStripChannel (selection.strip, p.channel);
            controller.setStripInputGain (selection.strip, p.inputGainDb);
            controller.setStripFader (selection.strip, p.faderDb);
            controller.setStripPan (selection.strip, p.pan);
            for (int f = 0; f < int (FxSlot::Count); ++f)
                controller.setStripSend (selection.strip, FxSlot (f), p.sendDb[size_t (f)]);
        }
        refresh();
    };
    addAndMakeVisible (*trail);

    railTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Left, "Channels");
    railTab->onClick = [this] { setRailShown (! railShown); };
    addAndMakeVisible (*railTab);

    trailTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Right, "What DLIVE did");
    trailTab->onClick = [this] { setTrailShown (! trailShown); };
    addAndMakeVisible (*trailTab);

    chain->onStageChanged = [this] { path->refresh(); refresh(); };
    rebuild();
}

// A folded panel keeps only its gutter: the handle stays where it was, so the width
// comes back with one click and the channel never moves out from under the pointer.
void AdvancedPage::setRailAvailable (bool available)
{
    if (available == railAvailable) return;
    railAvailable = available;
    railTab->setVisible (available);
    resized();
    repaint();
}

void AdvancedPage::setRailShown (bool shown)
{
    if (shown == railShown) return;
    railShown = shown;
    railTab->setCollapsed (! shown);
    viewport.setVisible (shown);
    resized();
    repaint();
}

void AdvancedPage::setTrailShown (bool shown)
{
    if (shown == trailShown) return;
    trailShown = shown;
    trailTab->setCollapsed (! shown);
    trail->setVisible (shown);
    resized();
    repaint();
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

    // Each family: its channels, then its bus - so the group master sits under the
    // channels that feed it, the way a console is patched. MASTER closes the list.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        const int n = graph.stripsOnBus (bus);
        if (n == 0) continue;

        addHeader (sentenceCase (mixBusName (bus)), juce::String (n), bus);

        for (int i = 0; i < graph.numStrips(); ++i)
        {
            const auto& s = graph.strips[size_t (i)];
            if (s.bus != bus) continue;
            auto row = std::make_unique<Row> (s.name, Row::Kind::Channel, s.bus, i);
            row->setClickingTogglesState (false);
            row->onClick = [this, i] { select (i); };
            addRow (std::move (row));
        }

        if (controller.isPrepared() && controller.getEngine().isBusUsed (bus))
        {
            auto row = std::make_unique<Row> (sentenceCase (mixBusName (bus)) + " bus", Row::Kind::Bus, bus);
            row->setClickingTogglesState (false);
            row->onClick = [this, bus] { selectBus (bus); };
            addRow (std::move (row));
        }
    }

    if (controller.isPrepared() && controller.getEngine().isBusUsed (MixBus::Master))
    {
        addHeader ("Output", "1", MixBus::Master);
        auto row = std::make_unique<Row> ("Master", Row::Kind::Master, MixBus::Master);
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
    chain->showStrip (strip);
    showSelection();
}

void AdvancedPage::selectBus (MixBus bus)
{
    selection.isBus = true;
    selection.bus = bus;
    for (auto* r : rows)
        r->setToggleState (r->kind != Row::Kind::Channel && r->bus == bus, juce::dontSendNotification);
    chain->showBus (bus);
    showSelection();
}

void AdvancedPage::selectStage (int index)
{
    chain->selectStage (index);
    path->refresh();
    refresh();
}

void AdvancedPage::showSelection()
{
    head->show (selection);

    // The headline and the sentence: what the last TUNE MIX said about this channel.
    juce::String headline, sentence;
    const auto* plan = controller.getPlan();
    if (plan == nullptr)
    {
        headline = "No TUNE MIX yet";
        sentence = "This channel runs on the profile's baseline for its source. TUNE MIX listens, then sets "
                   "every stage from what it hears.";
    }
    else
    {
        if (selection.isBus) headline = juce::String (plan->buses[size_t (selection.bus)].tune.headline);
        else if (selection.strip >= 0 && selection.strip < int (plan->strips.size()))
            headline = juce::String (plan->strips[size_t (selection.strip)].tune.headline);
        if (headline.isEmpty()) headline = juce::String (plan->headline);
        sentence = juce::String (mixPurposeName (controller.getSession().purpose)) + " " + Glyph::dot() + " "
                   + juce::String (styleProfileName (controller.getSession().profile))
                   + ". Move anything and it is kept - the next TUNE MIX replaces it the same way it replaces a fader.";
    }
    trail->setChannel (headline, sentence, selection.isBus && selection.bus == MixBus::Master, ! selection.isBus && selection.strip >= 0);
    trail->setGain (selection.isBus ? MixController::InputAdvice {} : controller.getInputAdvice (selection.strip));
    trail->setStages (chain->stageViews(), chain->selectedStage());
    path->refresh();
    resized();
    repaint();
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
            r->set (m.consumeMaxPeakDb(), db1 (st.faderDb), st.mute, st.solo);
        }
        else
        {
            const auto& m = engine.getBus (r->bus).getOutputMeter();
            const auto& b = kept.buses[size_t (r->bus)];
            r->set (m.consumeMaxPeakDb(), db1 (b.faderDb), b.mute, b.solo);
        }
    }

    float peak = -120.0f;
    if (selection.isBus) peak = engine.getBus (selection.bus).getOutputMeter().getMaxPeakDb();
    else if (selection.strip >= 0 && selection.strip < graph.numStrips())
        peak = engine.getStrip (selection.strip).getOutputMeter().getMaxPeakDb();

    head->refresh();
    chain->refresh();
    path->refresh();
    trail->setGain (selection.isBus ? MixController::InputAdvice {} : controller.getInputAdvice (selection.strip));
    trail->setStages (chain->stageViews(), chain->selectedStage());
    trail->refresh (peak);

    // The rail rows, the head, the chain and the trail are all components that repaint
    // themselves when their own reading changes. What this page paints is the rail's chrome
    // and the workspace's two bands - and repainting the whole Inspector thirty times a
    // second for that is the single most expensive thing the app was doing: on a 48-channel
    // console one full repaint of this page costs more than a 30 Hz frame has
    // (dlive_ui_snapshots --frames). So it only happens when something it draws has changed.
    const InspectorLook now { selection.isBus, selection.bus, selection.strip, int (rows.size()),
                              controller.isBypassed(), controller.isPrepared(), railShown, trailShown,
                              controller.getSampleRate(), controller.getBlockSize() };
    if (now != painted) { painted = now; repaint(); }
}

void AdvancedPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    auto area = getLocalBounds();
    auto rail = area.removeFromLeft (railWidth());
    auto trailArea = area.removeFromRight (trailWidth());

    g.setColour (Dine::rail);
    g.fillRect (rail);
    if (trailShown) { g.setColour (Dine::window); g.fillRect (trailArea); }
    else            { g.setColour (Dine::menubar); g.fillRect (trailArea); }
    g.setColour (Dine::hair);
    g.fillRect (rail.withLeft (rail.getRight() - 1));          // the seams either side of the middle column
    g.fillRect (trailArea.withWidth (1));

    if (! railShown) return paintWorkspaceBands (g, area);

    // The rail's own head and foot: what this is, and what the engine is doing.
    auto railHead = rail.removeFromTop (34).reduced (14, 0).withTrimmedTop (12);
    Dine::drawSection (g, railHead.withTrimmedRight (30), "CHANNELS");

    auto foot = rail.removeFromBottom (kFootH).reduced (14, 0).withTrimmedTop (12);
    const double sr = controller.getSampleRate();
    const int block = controller.getBlockSize();
    auto line = [&] (const juce::String& k, const juce::String& v)
    {
        auto row = foot.removeFromTop (16);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        g.drawText (k, row, juce::Justification::centredLeft);
        g.drawText (v, row, juce::Justification::centredRight);
    };
    line ("Rate", controller.isPrepared() ? juce::String (sr / 1000.0, 1) + " kHz" : Glyph::dash());
    line ("Buffer", controller.isPrepared() ? juce::String (block) + " smp" : Glyph::dash());
    line ("Latency", controller.isPrepared() ? juce::String (block / juce::jmax (1.0, sr) * 1000.0, 1) + " ms" : Glyph::dash());

    paintWorkspaceBands (g, area);
}

// The two bands the workspace is built from: the channel head, then the signal path.
void AdvancedPage::paintWorkspaceBands (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // The stage device sits in its own tile under the path; the head and the path are on the ground.
    auto workspace = area;
    workspace.removeFromTop (kHeadH + SignalPath::height + 20);
    Dine::fillRounded (g, workspace.reduced (kPadX, 4).withTrimmedBottom (10).toFloat(), Dine::tile, Dine::Radius::card);
}

void AdvancedPage::resized()
{
    auto area = getLocalBounds();
    auto rail = area.removeFromLeft (railWidth());
    auto trailArea = area.removeFromRight (trailWidth());

    if (railShown) railTab->setBounds (rail.withHeight (34).removeFromRight (30)); else railTab->setBounds (rail);
    if (trailShown) trailTab->setBounds (trailArea.withHeight (34).removeFromRight (30)); else trailTab->setBounds (trailArea);

    rail.removeFromTop (34);
    rail.removeFromBottom (kFootH);
    if (railShown) viewport.setBounds (rail.withTrimmedTop (4));

    const int rowH = 26, headerH = 26, sectionGap = 6;
    int total = 0;
    bool firstHeader = true;
    for (auto& item : listItems)
    {
        if (dynamic_cast<SectionHeader*> (item.get()) != nullptr)
        {
            total += (firstHeader ? 0 : sectionGap) + headerH;
            firstHeader = false;
        }
        else total += rowH;
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
            y += rowH;
        }
    }

    if (trailShown) trail->setBounds (trailArea);

    head->setBounds (area.removeFromTop (kHeadH));
    auto pathArea = area.removeFromTop (SignalPath::height + 20).reduced (kPadX, 10);
    path->setBounds (pathArea);
    chain->setBounds (area.reduced (kPadX, 4).withTrimmedBottom (10).reduced (18, 14));
}

} // namespace livemix
