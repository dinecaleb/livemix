#include "AdvancedPage.h"
#include "Core/DbUtils.h"
#include "DSP/ChannelProcessor.h"
#include "State/ParameterSpecs.h"
#include "Profiles/MacroMapping.h"
#include "Core/ProductDefinition.h"
#include "UI/LiveMixLookAndFeel.h"
#include <ctime>

namespace livemix
{

namespace
{
    constexpr int kHeadH  = 108;   // the channel header
    constexpr int kViewRowH = 38;  // Simple / Advanced and RE-TUNE, under it
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
        Dine::drawText (g, text, r, j, true);
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
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        Dine::drawText (g, Dine::sectionCase (label), r.removeFromLeft (r.getWidth() - 30), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        Dine::drawText (g, countText, r, juce::Justification::centredRight);
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
        Dine::drawText (g, name, r, juce::Justification::centredLeft, true);
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

        for (auto* b : { &muteButton, &soloButton, &effectsButton })
        {
            b->setClickingTogglesState (false);
            b->setCaps (true);
            b->setFontPx (11.0f);
            b->setPadX (6);
            addAndMakeVisible (*b);
        }
        // WHAT THIS MICROPHONE IS DOING RIGHT NOW. The pastor's handheld is the same channel
        // preaching and singing; the reverb and the delay are the only thing that has to change
        // between the two. So it is one press here and one press on the strip - the send levels
        // are kept either way, nothing is re-routed, and the way back is the same press.
        effectsButton.setTooltip ("Effects on this microphone: the reverb and the delay. Off for speaking, on for singing. "
                                  "Your send levels are kept either way, so one press is the way back. This is not what "
                                  "the microphone IS - that is on the strip's own menu, and it rebuilds the routing.");
        effectsButton.onClick = [this]
        {
            if (! sel.isBus) controller.setStripEffects (sel.strip, ! controller.stripEffectsOn (sel.strip));
        };
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
        effectsButton.setVisible (strip && controller.stripCanHaveEffects (sel.strip));
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
            if (effectsButton.isVisible())
            {
                const bool on = controller.stripEffectsOn (sel.strip);
                effectsButton.setTint (Dine::keyFx);
                effectsButton.setButtonText (on ? "EFFECTS ON" : "EFFECTS OFF");
                effectsButton.setStyle (on ? DineButton::Style::Filled : DineButton::Style::Standard);
            }
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
        Dine::drawText (g, title, text.removeFromTop (24), juce::Justification::centredLeft, true);
        text.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, sub + "  " + Glyph::dot() + "  " + provenance, text.removeFromTop (16), juce::Justification::topLeft, true);

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
            Dine::drawText (g, db1 (float (gain.getValue())) + " dB", top, juce::Justification::centredRight);
            col.removeFromTop (26);
            auto panRow = col.removeFromTop (18);
            drawCaps (g, "PAN", panRow.removeFromLeft (30), Dine::ink4);
            g.setColour (Dine::ink2);
            g.setFont (Dine::mono (10.5f));
            Dine::drawText (g, panText (float (pan.getValue())), panRow.removeFromRight (46), juce::Justification::centredRight);
        }

        {
            auto col = lay.level;
            auto top = col.removeFromTop (16);
            // The number is the thing being read, so it takes the width it needs and the
            // caption beside it takes what is left. A caption ellipsised is a caption; a level
            // that reads "+0..." is not a level.
            const juce::String levelLabel = sel.isBus ? "BUS LEVEL" : "LEVEL";
            const juce::String levelValue = db1 (float (fader.getValue())) + " dB";
            const auto valueFont = Dine::mono (16.0f, 500);
            auto valueCell = top.removeFromRight (juce::jmin (top.getWidth(), Dine::textWidth (valueFont, levelValue) + 4));
            drawCaps (g, levelLabel, top.withTrimmedRight (6), Dine::ink4);
            g.setColour (Dine::ink);
            g.setFont (valueFont);
            Dine::drawText (g, levelValue, valueCell, juce::Justification::centredRight);
            col.removeFromTop (24);
            auto scale = col.removeFromTop (12);
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (9.5f));
            Dine::drawText (g, Glyph::minus() + juce::String ("60"), scale, juce::Justification::centredLeft);
            Dine::drawText (g, "0", scale.withWidth (int (scale.getWidth() * 60.0f / 72.0f)), juce::Justification::centredRight);
            Dine::drawText (g, "+12", scale, juce::Justification::centredRight);
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
        if (effectsButton.isVisible())
        {
            effectsButton.setBounds (keys.removeFromLeft (kEffectsW).withSizeKeepingCentre (kEffectsW, 44));
            keys.removeFromLeft (8);
        }
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
        const int keysW = (effectsButton.isVisible() ? kEffectsW + 8 : 0)
                       + (muteButton.isVisible() ? 56 : 0) + (soloButton.isVisible() ? 48 : 0);
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
        Dine::drawText (g, label + " " + (peak <= -119.0f ? juce::String (Glyph::dash()) : db1 (peak)), top, juce::Justification::centredLeft);
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
    DineButton effectsButton { "EFFECTS ON", DineButton::Style::Standard };
    // Wide enough for the longer of the two words it carries, so the button does not change
    // width as it is switched.
    static constexpr int kEffectsW = 108;
};

// ------------------------------------------------------------------ SimplePanel
// THE CHANNEL IN FIVE PLAIN WORDS (design: `28 - Inspector - Simple view`, 89:25394).
//
// The same five controls the plug-in's Simple view has, for whatever this channel is - a voice
// gets WARMTH, CLARITY, SMOOTH, STEADY and CLEAN-UP; a drum gets PUNCH, BODY, ATTACK, TONE and
// BLEED - and under them what DLIVE did to this channel, in sentences, its level, and the two
// verbs that matter: TUNE CHANNEL and PUT BACK.
//
// 50 IS THE PLAN. A knob does not start from the profile's baseline the way the plug-in's
// does: it starts from the channel as TUNE left it, so the centre of every knob is "as tuned"
// and moving one leans that channel away from the plan by a bounded, musical amount - the same
// convention as TUNE's own macro pads. A tune, or a hand edit in Advanced, re-seeds it.
class AdvancedPage::SimplePanel : public juce::Component
{
public:
    explicit SimplePanel (MixController& c) : controller (c)
    {
        for (int i = 0; i < 5; ++i)
        {
            knobs[size_t (i)] = std::make_unique<Knob>();
            knobs[size_t (i)]->onChange = [this, i] (float v) { setMacro (i, v); };
            addAndMakeVisible (*knobs[size_t (i)]);
        }
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        Dine::dragOnly (level);
        level.setRange (-60.0, 12.0, 0.1);
        level.setSkewFactorFromMidPoint (-12.0);
        level.setDoubleClickReturnValue (true, 0.0);
        level.getProperties().set ("dineFader", true);
        level.setTooltip ("How loud this channel is in the mix. Double-click for 0.0 dB.");
        level.onValueChange = [this]
        {
            if (updating || strip < 0) return;
            controller.setStripFader (strip, float (level.getValue()));
            repaint();
        };
        addAndMakeVisible (level);

        tuneButton.setCaps (true);
        tuneButton.setFontPx (12.0f);
        tuneButton.setTooltip ("DLIVE listens to this channel on its own and sets its chain. Nothing else in the mix moves.");
        tuneButton.onClick = [this] { if (onTune && strip >= 0) onTune (strip); };
        addAndMakeVisible (tuneButton);

        putBack.setFontPx (12.0f);
        putBack.setTooltip ("Put this channel back to what TUNE left it at: the five controls go back to the middle.");
        putBack.onClick = [this]
        {
            if (strip < 0) return;
            values = MacroMapping::defaults (product);
            controller.setStripChannel (strip, asTuned);
            syncKnobs();
            repaint();
        };
        addAndMakeVisible (putBack);
        setOpaque (false);
    }

    std::function<void (int strip)> onTune;

    // A new channel, or a chain that changed from outside: the five controls go back to the
    // middle and this becomes what they are measured from.
    void setStrip (int index)
    {
        strip = index;
        reseed();
    }

    void reseed()
    {
        if (strip < 0 || ! controller.isPrepared() || strip >= controller.getBase().numStrips) return;
        const auto& st = controller.getBase().strips[size_t (strip)];
        asTuned = st.channel;
        role = controller.getGraph().strips[size_t (strip)].role;
        product = productOf (role);
        values = MacroMapping::defaults (product);
        syncKnobs();
        repaint();
    }

    void refresh()
    {
        if (strip < 0 || ! controller.isPrepared() || strip >= controller.getBase().numStrips) return;
        updating = true;
        const float db = controller.getBase().strips[size_t (strip)].faderDb;
        if (std::fabs (db - float (level.getValue())) > 0.01f) { level.setValue (db, juce::dontSendNotification); repaint(); }
        updating = false;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        Dine::fillRounded (g, r.toFloat(), Dine::card, Dine::Radius::card);
        auto inner = r.reduced (24, 20);

        const auto& def = productDefinition (product);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        Dine::drawText (g, "How it sounds", inner.removeFromTop (22), juce::Justification::centredLeft, true);
        inner.removeFromTop (kKnobBlockH + 22);

        g.setColour (Dine::hair);
        g.fillRect (inner.removeFromTop (1));
        inner.removeFromTop (18);

        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f, 600));
        Dine::drawText (g, "What DLIVE did", inner.removeFromTop (20), juce::Justification::centredLeft, true);
        inner.removeFromTop (8);

        // The sentences TUNE already wrote for this channel, as a list a volunteer can read.
        const auto notes = sentences();
        for (const auto& note : notes)
        {
            if (inner.getHeight() < 40) break;
            auto row = inner.removeFromTop (20);
            g.setColour (Dine::accent);
            g.fillEllipse (row.removeFromLeft (5).withSizeKeepingCentre (4, 4).toFloat());
            row.removeFromLeft (9);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.5f));
            Dine::drawText (g, note, row, juce::Justification::centredLeft, true);
            inner.removeFromTop (6);
        }
        if (notes.isEmpty())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.5f));
            Dine::drawText (g, "Nothing yet. TUNE CHANNEL listens to this one source and sets its chain.",
                            inner.removeFromTop (20), juce::Justification::centredLeft, true);
        }

        // the level, over its slider
        auto foot = getLocalBounds().reduced (24, 20).removeFromBottom (Dine::Metric::button + 10 + 34);
        auto levelRow = foot.removeFromTop (34);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, "Level", levelRow.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (Dine::ink);
        g.setFont (Dine::mono (12.0f, 500));
        Dine::drawText (g, db1 (float (level.getValue())) + " dB",
                        juce::Rectangle<int> (levelRow).removeFromRight (64), juce::Justification::centredRight);

        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        Dine::drawText (g, "The engineer's words " + juce::String (Glyph::dash()) + " gate, compressor, de-esser " 
                            + Glyph::dash() + " are in Advanced.",
                        getLocalBounds().reduced (24, 20).removeFromBottom (16), juce::Justification::centredLeft, true);
        juce::ignoreUnused (def);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (24, 20);
        inner.removeFromTop (22 + 10);
        auto row = inner.removeFromTop (kKnobBlockH);
        const int each = row.getWidth() / 5;
        for (int i = 0; i < 5; ++i) knobs[size_t (i)]->setBounds (row.removeFromLeft (each));

        auto foot = getLocalBounds().reduced (24, 20).removeFromBottom (Dine::Metric::button + 10 + 34 + 16 + 8);
        foot.removeFromTop (14 + 4);
        level.setBounds (foot.removeFromTop (16).withTrimmedRight (72));
        foot.removeFromTop (10);
        auto buttons = foot.removeFromTop (Dine::Metric::button);
        const int tw = juce::jmax (120, tuneButton.idealWidth());
        tuneButton.setBounds (buttons.removeFromLeft (tw));
        buttons.removeFromLeft (10);
        const int pw = juce::jmax (100, putBack.idealWidth());
        putBack.setBounds (buttons.removeFromLeft (pw));
    }

private:
    // The design's `Knob Large` (112:10018): a 270 degree track, the value arc in the accent,
    // one pointer, and the value under the knob so the arc is never covered by it.
    class Knob : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<void (float)> onChange;

        void set (float v, const juce::String& text, const juce::String& label)
        {
            value = v; readout = text; name = label;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            auto box = r.removeFromTop (kKnobSize).withSizeKeepingCentre (kKnobSize, kKnobSize).toFloat().reduced (3.0f);
            const auto centre = box.getCentre();
            const float radius = box.getWidth() * 0.5f;
            const float start = juce::MathConstants<float>::pi * 1.25f;
            const float end   = juce::MathConstants<float>::pi * 2.75f;
            const float angle = start + juce::jlimit (0.0f, 1.0f, value / 100.0f) * (end - start);

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
            g.setColour (Dine::control);
            g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
            g.setColour (isEnabled() ? Dine::accent : Dine::ink4);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto dot = centre.getPointOnCircumference (radius - 6.0f, angle);
            g.setColour (isEnabled() ? Dine::ink : Dine::ink4);
            g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (dot));

            r.removeFromTop (6);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (13.0f, 500));
            Dine::drawText (g, readout, r.removeFromTop (16), juce::Justification::centred);
            g.setColour (Dine::ink3);
            g.setFont (Dine::caps (10.0f, 0.04f, 600));
            Dine::drawText (g, name, r.removeFromTop (14), juce::Justification::centred, true);
        }

        void mouseDown (const juce::MouseEvent&) override { dragFrom = value; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! isEnabled()) return;
            const float v = juce::jlimit (0.0f, 100.0f, dragFrom - float (e.getDistanceFromDragStartY()) * 0.5f);
            if (std::fabs (v - value) < 0.01f) return;
            value = v;
            repaint();
            if (onChange) onChange (v);
        }
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (! isEnabled()) return;
            value = 50.0f;
            repaint();
            if (onChange) onChange (value);
        }

        float value = 50.0f, dragFrom = 50.0f;
        juce::String readout { "50" }, name;
    };

    static constexpr int kKnobSize = 76;
    static constexpr int kKnobBlockH = kKnobSize + 6 + 16 + 14;

    void setMacro (int index, float v)
    {
        if (strip < 0 || index < 0 || index >= 5) return;
        values.v[size_t (index)] = v;
        controller.setStripChannel (strip, MacroMapping::apply (product, asTuned, values, roleFamily (role)));
        syncKnobs();
    }

    void syncKnobs()
    {
        const auto& def = productDefinition (product);
        for (int i = 0; i < 5; ++i)
        {
            const auto& spec = def.macros[size_t (i)];
            const float v = values.v[size_t (i)];
            const bool unit = spec.maxValue <= 1.001f;
            const float shown = unit ? v * 100.0f : v;
            knobs[size_t (i)]->set (shown, juce::String (juce::roundToInt (shown)), spec.label);
            knobs[size_t (i)]->setTooltip (juce::String (spec.tooltip) + "  The middle is what TUNE left this channel at.");
        }
    }

    juce::StringArray sentences() const
    {
        juce::StringArray out;
        if (strip < 0) return out;
        // What TUNE said about this one channel, in its own words: the WHY of each item it
        // decided, oldest first, capped so the panel stays a summary rather than a report.
        if (const auto* plan = controller.getPlan())
            for (const auto& sp : plan->strips)
            {
                if (sp.strip != strip) continue;
                for (const auto& item : sp.tune.report.items)
                {
                    if (item.why.empty()) continue;
                    out.addIfNotAlreadyThere (juce::String (item.why));
                    if (out.size() >= 5) return out;
                }
                for (const auto& item : sp.mixItems)
                {
                    if (item.why.empty()) continue;
                    out.addIfNotAlreadyThere (juce::String (item.why));
                    if (out.size() >= 5) return out;
                }
            }
        return out;
    }

    MixController& controller;
    int strip = -1;
    ChannelRole role = ChannelRole::LeadVocal;
    Product product = Product::Vocals;
    ChannelParameters asTuned;
    MacroValues values;
    std::array<std::unique_ptr<Knob>, 5> knobs;
    juce::Slider level;
    DineButton tuneButton { "TUNE CHANNEL", DineButton::Style::Filled };
    DineButton putBack { "Put it back", DineButton::Style::Standard };
    bool updating = false;
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
    std::function<void (int)> onRestore;       // HISTORY: put record `index` back on this channel
    std::function<void()> onRetune, onRevertAll, onTuneChannel;

    // Scrolls the column so HISTORY is the first thing in it.
    void showHistory()
    {
        if (list.history.empty()) return;
        view.setViewPosition (0, list.historyTop());
    }

    void setHistory (const std::vector<HistoryView>& v)
    {
        if (v == list.history) return;
        list.history = v;
        list.setSize (view.getWidth(), list.heightFor (view.getWidth()));
        list.repaint();
    }

    // `channel` is false for a bus: a bus is not a source, so there is nothing to listen to
    // on its own - it is tuned by what feeds it.
    void setChannel (const juce::String& headlineText, const juce::String& sentenceText, bool masterSelected, bool channel)
    {
        headline = headlineText;
        sentence = sentenceText;
        isMaster = masterSelected;
        if (tuneChannel.isVisible() != channel) tuneChannel.setVisible (channel);
        view.setViewPosition (0, 0);       // a channel's trail starts at its top
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
        // The caption and the tune count share one row, so the count takes what it actually
        // measures and the caption gets the rest. A fixed 40 px for the count was enough until
        // Text size made the caption wider than what was left.
        const juce::String tunes = controller.getTuneCount() > 0 ? "TUNE " + juce::String (controller.getTuneCount())
                                                                 : juce::String ("no tune yet");
        const auto tunesFont = Dine::mono (10.0f);
        auto countCell = head.removeFromRight (juce::jmin (head.getWidth() / 2, Dine::textWidth (tunesFont, tunes) + 26));
        Dine::drawSection (g, head.withTrimmedRight (10), "WHAT DLIVE DID");
        g.setColour (Dine::ink4);
        g.setFont (tunesFont);
        Dine::drawText (g, tunes, countCell.withTrimmedRight (26), juce::Justification::centredRight);

        auto top = area.removeFromTop (sentenceHeight()).reduced (18, 0);
        top.removeFromTop (8);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.5f));
        Dine::drawFittedText (g, sentence, top.removeFromTop (juce::jmax (0, top.getHeight() - (tuneChannel.isVisible() ? 82 : 44))),
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
        Dine::drawText (g, juce::String (advice.headline).toUpperCase(), r.removeFromTop (14), juce::Justification::centredLeft, true);
        r.removeFromTop (6);
        auto figures = r.removeFromTop (13);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        if (std::fabs (advice.digitalGainDb) >= 0.05f)
            Dine::drawText (g, "DLIVE " + db1 (advice.digitalGainDb), figures.removeFromRight (66), juce::Justification::centredRight);
        Dine::drawText (g, advice.capturePeakDb <= -119.0f ? juce::String ("no signal")
                                                    : juce::String (advice.capturePeakDb, 1) + " dBFS in",
                    figures, juce::Justification::centredLeft, true);
        if (! advice.needsAttention()) return;
        r.removeFromTop (5);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, juce::String (advice.detail), r, juce::Justification::topLeft, 4, 1.0f);
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
            Dine::drawText (g, lufs, figures.removeFromLeft (Dine::textWidth (bigFont, lufs) + 8), juce::Justification::centredLeft);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f));
            Dine::drawText (g, "LUFS short term", figures, juce::Justification::centredLeft, true);
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (11.0f));
            Dine::drawText (g, st > -100.0f ? juce::String (tp, 1) + " dBTP" : juce::String(), r.removeFromTop (14),
                        juce::Justification::topLeft);
            return;
        }

        // How far the channel is from the ceiling right now: the number an engineer
        // glances at before touching anything.
        const float head = juce::jlimit (0.0f, 24.0f, -peak);
        auto top = r.removeFromTop (16);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        Dine::drawText (g, "Headroom", top.removeFromLeft (top.getWidth() - 80), juce::Justification::centredLeft);
        g.setColour (Dine::ink);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, peak <= -119.0f ? Glyph::dash() : juce::String (head, 1) + " dB", top, juce::Justification::centredRight);
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
        Dine::drawText (g, headline, r.removeFromTop (14), juce::Justification::topLeft, true);
    }

    // The list itself: one row per stage, painted in one place and hit-tested the same way.
    class List : public juce::Component
    {
    public:
        explicit List (Trail& owner) : trail (owner) {}

        static constexpr int kHistoryHeadH = 34;

        int historyTop() const
        {
            int y = 0;
            for (int h : heights) y += h;
            return y;
        }

        int heightFor (int width)
        {
            heights.clear();
            historyHeights.clear();
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
            if (! history.empty())
            {
                y += kHistoryHeadH;
                for (const auto& v : history)
                {
                    const int h = 12 + 16 + 5 + 14 + (v.lines.isEmpty() ? 0 : 5 + v.lines.size() * 16) + 12 + 3;
                    historyHeights.push_back (h);
                    y += h;
                }
            }
            return juce::jmax (y, 1);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (18, 0);
            paintStages (g, r);
            paintHistory (g, r);
        }

        // HISTORY: newest first, each row a setting this channel had and a way to have it
        // back. The chip is the only control: the row itself is for reading.
        void paintHistory (juce::Graphics& g, juce::Rectangle<int>& r)
        {
            chips.clear();
            if (history.empty()) return;
            auto head = r.removeFromTop (kHistoryHeadH).withTrimmedTop (14);
            Dine::drawSection (g, head, "HISTORY");
            for (size_t i = 0; i < history.size() && i < historyHeights.size(); ++i)
            {
                const auto& v = history[i];
                auto row = r.removeFromTop (historyHeights[i]).withTrimmedBottom (3);
                Dine::fillRounded (g, row.toFloat(), Dine::item, Dine::Radius::control);
                auto body = row.reduced (12, 12);

                auto top = body.removeFromTop (16);
                const juce::String chipText = "PUT BACK";
                const int chipW = Dine::textWidth (Dine::caps (9.0f, 0.06f, 500), chipText) + 14;
                auto chip = top.removeFromRight (chipW);
                Dine::drawStatusChip (g, chip.toFloat(), chipText, Dine::accent);
                chips.push_back (chip);
                top.removeFromRight (8);
                g.setColour (Dine::ink4);
                g.setFont (Dine::mono (10.0f));
                const int whenW = Dine::textWidth (Dine::mono (10.0f), v.when);
                Dine::drawText (g, v.when, top.removeFromRight (whenW), juce::Justification::centredRight);
                top.removeFromRight (8);
                g.setColour (Dine::ink);
                g.setFont (Dine::text (13.0f));
                Dine::drawText (g, v.what, top, juce::Justification::centredLeft, true);

                body.removeFromTop (5);
                g.setColour (Dine::ink2);
                g.setFont (Dine::mono (11.0f, 500));
                Dine::drawText (g, v.summary, body.removeFromTop (14), juce::Justification::centredLeft, true);
                if (v.lines.isEmpty()) continue;
                body.removeFromTop (5);
                g.setColour (Dine::ink3);
                g.setFont (Dine::text (12.0f));
                for (const auto& line : v.lines)
                    Dine::drawText (g, line, body.removeFromTop (16), juce::Justification::centredLeft, true);
            }
        }

        void paintStages (juce::Graphics& g, juce::Rectangle<int>& r)
        {
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
                Dine::drawText (g, v.label.substring (0, 1) + v.label.substring (1).toLowerCase(), top, juce::Justification::centredLeft, true);

                body.removeFromTop (5);
                g.setColour (Dine::ink2);
                g.setFont (Dine::mono (11.0f, 500));
                Dine::drawText (g, v.value, body.removeFromTop (14), juce::Justification::centredLeft, true);
                body.removeFromTop (5);
                g.setColour (Dine::ink3);
                g.setFont (Dine::text (12.0f));
                Dine::drawFittedText (g, v.why, body, juce::Justification::topLeft, 4, 1.0f);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown()) return;
            for (size_t i = 0; i < chips.size(); ++i)
                if (chips[i].contains (e.getPosition())) { if (trail.onRestore) trail.onRestore (int (i)); return; }
            if (! trail.onPick) return;
            int y = 0;
            for (size_t i = 0; i < heights.size(); ++i)
            {
                if (e.y >= y && e.y < y + heights[i]) { trail.onPick (int (i)); return; }
                y += heights[i];
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int stagesEnd = 0;
            for (int h : heights) stagesEnd += h;
            bool onChip = false;
            for (const auto& c : chips) if (c.contains (e.getPosition())) onChip = true;
            setMouseCursor (e.y < stagesEnd || onChip ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        std::vector<ChainEditor::StageView> views;
        std::vector<HistoryView> history;        // newest first
        int selected = 0;

    private:
        Trail& trail;
        std::vector<int> heights, historyHeights;
        std::vector<juce::Rectangle<int>> chips;  // where each row's PUT BACK was painted, for the click
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

    simple = std::make_unique<SimplePanel> (controller);
    simple->onTune = [this] (int strip) { if (onTuneChannel) onTuneChannel (strip); };
    addChildComponent (*simple);

    // SIMPLE / ADVANCED: a way of looking at the channel, never a mode the mix is in.
    addAndMakeVisible (viewTrack);
    for (auto* b : { &simpleTab, &advancedTab })
    {
        b->setFontPx (11.5f);
        viewTrack.addAndMakeVisible (*b);
    }
    simpleTab.setTooltip ("This channel in five plain words. The middle of each control is what TUNE left it at.");
    advancedTab.setTooltip ("The whole chain, stage by stage, with every number it owns.");
    simpleTab.onClick = [this] { setSimpleView (true); };
    advancedTab.onClick = [this] { setSimpleView (false); };
    advancedTab.setToggleState (true, juce::dontSendNotification);

    retuneButton.setCaps (true);
    retuneButton.setFontPx (11.0f);
    retuneButton.setTooltip ("Listen again and build the mix from what it hears now. Nothing is committed until KEEP.");
    retuneButton.onClick = [this] { if (onRetune) onRetune(); };
    addAndMakeVisible (retuneButton);

    trail = std::make_unique<Trail> (controller);
    trail->onPick = [this] (int stage) { chain->selectStage (stage); };
    trail->onRetune = [this] { if (onRetune) onRetune(); };
    trail->onTuneChannel = [this] { if (onTuneChannel && ! selection.isBus && selection.strip >= 0) onTuneChannel (selection.strip); };
    trail->onRestore = [this] (int record)
    {
        if (selection.isBus || selection.strip < 0) return;
        // The list is newest first; the controller keeps its records oldest first.
        const int n = int (controller.getStripHistory (selection.strip).size());
        if (controller.restoreStripTune (selection.strip, n - 1 - record)) refresh();
    };
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
    chain->onImportSample = [this] (RoleFamily family) { if (onImportSample) onImportSample (family); };
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

void AdvancedPage::revealHistory()
{
    if (! trailShown) setTrailShown (true);
    trail->showHistory();
}

void AdvancedPage::selectStage (int index)
{
    chain->selectStage (index);
    path->refresh();
    refresh();
}

// SIMPLE / ADVANCED. A way of looking at the channel: the sound is identical either way, and
// what one view changes the other shows.
void AdvancedPage::setSimpleView (bool on)
{
    if (on == simpleView) return;
    simpleView = on;
    simpleTab.setToggleState (on, juce::dontSendNotification);
    advancedTab.setToggleState (! on, juce::dontSendNotification);
    // Simple is about one channel: a group bus has no five plain words, so it stays on the chain.
    simple->setVisible (on && ! selection.isBus && selection.strip >= 0);
    path->setVisible (! on);
    chain->setVisible (! on);
    if (simple->isVisible()) simple->reseed();
    resized();
    repaint();
}

void AdvancedPage::showSelection()
{
    head->show (selection);
    simple->setStrip (selection.isBus ? -1 : selection.strip);
    simple->setVisible (simpleView && ! selection.isBus && selection.strip >= 0);
    path->setVisible (! simple->isVisible());
    chain->setVisible (! simple->isVisible());

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
    trail->setHistory (historyViews());
    path->refresh();
    resized();
    repaint();
}

// The channel's history in the words the trail uses: what did it, when, how much, and each
// change as "High-pass 80 Hz -> 100 Hz". Newest first, because the top of the list is what
// the channel is now. Built only when the record count or the newest record changes.
std::vector<AdvancedPage::HistoryView> AdvancedPage::historyViews()
{
    std::vector<HistoryView> out;
    if (selection.isBus || selection.strip < 0) return out;
    const auto& records = controller.getStripHistory (selection.strip);
    if (records.empty()) return out;

    auto clock = [] (long long ms)
    {
        if (ms <= 0) return juce::String();
        const juce::Time t { juce::int64 (ms) };
        const bool today = t.toString (true, false) == juce::Time::getCurrentTime().toString (true, false);
        return today ? t.formatted ("%H:%M") : t.formatted ("%d %b %H:%M");
    };
    auto value = [] (const ParameterSpec* spec, float v)
    {
        if (spec == nullptr) return juce::String (v, 2);
        if (spec->type == ParameterSpec::Type::Bool) return juce::String (v >= 0.5f ? "on" : "off");
        if (spec->type == ParameterSpec::Type::Choice)
        {
            const int i = juce::jlimit (0, int (spec->choices.size()) - 1, int (std::round (v)));
            return spec->choices.empty() ? juce::String (int (v)) : juce::String (spec->choices[size_t (i)]);
        }
        return LiveMixLookAndFeel::formatValue (v, juce::String (spec->unit), spec->minValue, spec->maxValue);
    };
    auto valueOf = [] (const ChannelParameters& p, const std::string& id) -> float
    {
        float found = 0.0f;
        ChannelParameters copy = p;
        forEachDspParameter (copy, [&] (const std::string& fieldId, auto& field) { if (fieldId == id) found = float (field); });
        return found;
    };

    for (auto it = records.rbegin(); it != records.rend(); ++it)
    {
        const auto& r = *it;
        HistoryView v;
        v.what = juce::String (r.what);
        v.when = clock (r.whenMs);

        const auto changes = diffParameters (r.before.channel, r.after.channel);
        juce::StringArray parts;
        if (! changes.empty()) parts.add (juce::String (int (changes.size())) + (changes.size() == 1 ? " setting" : " settings"));
        if (std::fabs (r.after.faderDb - r.before.faderDb) >= 0.05f) parts.add ("level " + db1 (r.after.faderDb) + " dB");
        if (std::fabs (r.after.inputGainDb - r.before.inputGainDb) >= 0.05f) parts.add ("gain " + db1 (r.after.inputGainDb) + " dB");
        if (std::fabs (r.after.pan - r.before.pan) >= 0.005f) parts.add ("pan " + panText (r.after.pan));
        int sends = 0;
        for (size_t f = 0; f < r.after.sendDb.size(); ++f)
            if (std::fabs (r.after.sendDb[f] - r.before.sendDb[f]) >= 0.05f) ++sends;
        if (sends > 0) parts.add (juce::String (sends) + (sends == 1 ? " send" : " sends"));
        v.summary = parts.joinIntoString ("  " + juce::String (Glyph::dot()) + "  ");

        constexpr int kMaxLines = 4;
        for (const auto& c : changes)
        {
            if (v.lines.size() >= kMaxLines) break;
            const auto* spec = findParameterSpec (c.paramId);
            const juce::String name = spec != nullptr ? juce::String (spec->name) : juce::String (c.paramId);
            v.lines.add (name + "  " + value (spec, valueOf (r.before.channel, c.paramId)) + " to " + value (spec, c.value));
        }
        if (int (changes.size()) > kMaxLines)
            v.lines.set (kMaxLines - 1, "and " + juce::String (int (changes.size()) - (kMaxLines - 1)) + " more");
        out.push_back (std::move (v));
    }
    return out;
}

void AdvancedPage::refresh()
{
    if (! controller.isPrepared()) return;
    if (simple != nullptr && simple->isVisible()) simple->refresh();
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
    {
        // The history only ever grows at the end, so the count and the newest record's clock
        // say whether there is anything new to put into words.
        const auto& records = selection.isBus || selection.strip < 0 ? controller.getStripHistory (-1) : controller.getStripHistory (selection.strip);
        const long long newest = records.empty() ? 0 : records.back().whenMs;
        if (int (records.size()) != historyCount || newest != historyNewest || selection.strip != historyStrip)
        {
            historyCount = int (records.size());
            historyNewest = newest;
            historyStrip = selection.strip;
            trail->setHistory (historyViews());
        }
    }
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
        Dine::drawText (g, k, row, juce::Justification::centredLeft);
        Dine::drawText (g, v, row, juce::Justification::centredRight);
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
    workspace.removeFromTop (kHeadH + kViewRowH + SignalPath::height + 20);
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
    {
        // SIMPLE / ADVANCED and RE-TUNE: the design puts them level with the channel's name,
        // which is where the head's own gain, level and keys already are, so they take the row
        // under it - still the first thing at the right of the channel, and over nothing.
        auto row = area.removeFromTop (kViewRowH).reduced (kPadX, 0).withSizeKeepingCentre (
                       area.getWidth() - 2 * kPadX, Dine::Metric::control);
        const int rw = juce::jmax (92, retuneButton.idealWidth());
        retuneButton.setBounds (row.removeFromRight (rw));
        row.removeFromRight (12);
        const int each = 78;
        viewTrack.setBounds (row.removeFromRight (each * 2 + 4));
        auto track = viewTrack.getLocalBounds().reduced (2, 2);
        simpleTab.setBounds (track.removeFromLeft (each));
        advancedTab.setBounds (track);
    }
    if (simpleView)
    {
        path->setBounds (0, 0, 0, 0);
        chain->setBounds (0, 0, 0, 0);
        simple->setBounds (area.reduced (kPadX, 10).withTrimmedBottom (10));
        return;
    }
    auto pathArea = area.removeFromTop (SignalPath::height + 20).reduced (kPadX, 10);
    path->setBounds (pathArea);
    chain->setBounds (area.reduced (kPadX, 4).withTrimmedBottom (10).reduced (18, 14));
}

} // namespace livemix
