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
    // The design's Inspector (`07 - Inspector - Sample`, 73:10195): a 180 pt channel rail, the
    // stage editor beside it, and the 280 pt column of what DINE did. Inside the editor the
    // name sits at y = 16, the signal path at 80 and the stage card at 132.
    constexpr int kHeadH   = 80;   // the channel's name, and Simple / Advanced / RE-TUNE beside it
    constexpr int kPathTop = 80;   // where the signal path's row begins
    constexpr int kCardTop = 132;  // ... and the stage card under it
    constexpr int kPadX    = 24;   // the stage editor's own gutter
    constexpr int kRailPadX = 16;  // the channel rail's

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

namespace
{
    // A group as people say it: "Drums bus", and "BGV bus" - an initialism stays one.
    juce::String busWord (MixBus b)
    {
        const juce::String raw (mixBusName (b));
        return raw.length() <= 3 ? raw.toUpperCase() : raw.substring (0, 1).toUpperCase() + raw.substring (1).toLowerCase();
    }
}

// ------------------------------------------------------------------ SectionHeader
// The rail groups a console's channels under their bus, which the design's sixteen-input
// frame had no need of and a real service does: a caption, quiet, with the group's colour.
class AdvancedPage::SectionHeader : public juce::Component
{
public:
    SectionHeader (const juce::String& title, const juce::String& count, MixBus bus)
        : label (title), countText (count), tint (busTint (bus)) {}

    void paint (juce::Graphics& g) override
    {
        // v4: the family's name in its own colour, 11 pt semibold; "BGV" stays an initialism.
        auto r = getLocalBounds().reduced (kRailPadX, 0);
        const juce::String word = label.length() <= 3 ? label.toUpperCase()
                                                      : label.substring (0, 1).toUpperCase() + label.substring (1).toLowerCase();
        g.setColour (tint);
        g.setFont (Dine::text (11.0f, 600));
        Dine::drawText (g, word, r.removeFromLeft (r.getWidth() - 26), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        Dine::drawText (g, countText, r, juce::Justification::centredRight);
    }

private:
    juce::String label, countText;
    juce::Colour tint;
};

// ------------------------------------------------------------------ Row
// One channel in the rail, the way the design draws it: a 28 pt row inside a 32 pt pitch,
// a dot in the source's colour and its name. The chosen one is a lit plane.
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
        if (muted == mute && soloed == solo) return;
        mute = muted;
        solo = soloed;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        const auto tint = busTint (bus);
        auto b = getLocalBounds().reduced (6, 0);
        if (on)        Dine::fillRounded (g, b.toFloat(), Dine::selected, Dine::Radius::control);
        else if (over) Dine::fillRounded (g, b.toFloat(), Dine::item, Dine::Radius::control);

        auto r = b.reduced (10, 0);
        auto dot = r.removeFromLeft (7).withSizeKeepingCentre (7, 7);
        g.setColour (mute ? Dine::keyMute : solo ? Dine::keySolo : tint);
        g.fillEllipse (dot.toFloat());
        r.removeFromLeft (9);

        g.setColour (mute ? Dine::ink4 : on ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (13.0f, on ? 600 : 500));
        Dine::drawText (g, name, r, juce::Justification::centredLeft, true);
        if (mute)
            g.fillRect (r.getX(), r.getCentreY(), juce::jmin (r.getWidth(), Dine::textWidth (Dine::text (13.0f), name)), 1);
    }

    juce::String name, text;
    Kind kind;
    MixBus bus;
    int strip = -1;
    bool mute = false, solo = false;
    float level = -120.0f;
};

// ------------------------------------------------------------------ SimplePanel
// THE CHANNEL IN FIVE PLAIN WORDS (design: `28 - Inspector - Simple view`, 89:25394).
//
// The same five controls the plug-in's Simple view has, for whatever this channel is - a voice
// gets WARMTH, CLARITY, SMOOTH, STEADY and CLEAN-UP; a drum gets PUNCH, BODY, ATTACK, TONE and
// BLEED - and under them what DINE did to this channel, in sentences, its level, and the two
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
        // THE LEVEL IS A KNOB, NOT A BAR LYING DOWN. A number an engineer turns is turned;
        // the one fader in the product is the one on the mixer strip, standing up.
        level.setRange (-60.0, 12.0, 0.1, -12.0);
        level.setDefaultValue (0.0);
        level.setDial (kLevelDial);
        level.setCaption ("Level");
        level.setFormat ([] (double v) { return db1 (float (v)) + " dB"; });
        level.setTooltip ("How loud this channel is in the mix. Double-click for 0.0 dB.");
        level.onChange = [this] (double v)
        {
            if (updating || strip < 0) return;
            controller.setStripFader (strip, float (v));
            repaint();
        };
        addAndMakeVisible (level);

        tuneButton.setCaps (true);
        tuneButton.setFontPx (12.0f);
        tuneButton.setTooltip ("DINE listens to this channel on its own and sets its chain. Nothing else in the mix moves.");
        tuneButton.onClick = [this] { if (onTune && strip >= 0) onTune (strip); };
        addAndMakeVisible (tuneButton);

        // SPEAKING / SINGING: the same microphone, and the only thing that has to change between
        // the two is the reverb and the delay. One press here, one press on the strip, the send
        // levels kept either way - it is never a re-route.
        addChildComponent (voiceTrack);
        for (auto* b : { &speakingTab, &singingTab })
        {
            b->setFontPx (12.0f);
            voiceTrack.addAndMakeVisible (*b);
        }
        speakingTab.setTooltip ("Dry, for preaching: the reverb and the delay are off on this microphone.");
        singingTab.setTooltip ("In the effects, for singing. Your send levels are kept either way.");
        speakingTab.onClick = [this] { if (strip >= 0) controller.setStripEffects (strip, false); };
        singingTab.onClick  = [this] { if (strip >= 0) controller.setStripEffects (strip, true); };

        putBack.setCaps (true);
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
        voiceTrack.setVisible (controller.stripCanHaveEffects (strip));
        syncKnobs();
        resized();
        repaint();
    }

    void refresh()
    {
        if (strip < 0 || ! controller.isPrepared() || strip >= controller.getBase().numStrips) return;
        updating = true;
        const float db = controller.getBase().strips[size_t (strip)].faderDb;
        if (std::fabs (db - float (level.getValue())) > 0.01f) { level.setValue (db); repaint(); }
        updating = false;
        if (voiceTrack.isVisible())
        {
            const bool singing = controller.stripEffectsOn (strip);
            speakingTab.setToggleState (! singing, juce::dontSendNotification);
            singingTab.setToggleState (singing, juce::dontSendNotification);
        }
        const auto when = putBackLabel();
        if (when != putBack.getButtonText()) { putBack.setButtonText (when); resized(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto lay = layout();
        Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::card, Dine::Radius::card);

        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        Dine::drawText (g, personal() ? "How they sound" : "How it sounds", lay.title,
                    juce::Justification::centredLeft, true);

        Dine::drawRule (g, lay.rule, Dine::hair);

        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, "What DINE did", lay.didHead, juce::Justification::centredLeft, true);

        // The sentences TUNE already wrote for this channel, as a list a volunteer can read.
        const auto notes = sentences();
        auto rows = lay.bullets;
        for (const auto& note : notes)
        {
            if (rows.getHeight() < kBulletH) break;
            auto row = rows.removeFromTop (kBulletH);
            g.setColour (Dine::accent);
            g.fillEllipse (row.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            row.removeFromLeft (10);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (13.0f));
            Dine::drawText (g, note, row, juce::Justification::centredLeft, true);
        }
        if (notes.isEmpty())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (13.0f));
            Dine::drawText (g, "Nothing yet. TUNE CHANNEL listens to this one source and sets its chain.",
                        rows.removeFromTop (kBulletH), juce::Justification::centredLeft, true);
        }

        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.5f));
        Dine::drawText (g, "The engineer's words " + juce::String (Glyph::dash()) + " gate, compressor, de-esser "
                            + Glyph::dash() + " are in Advanced.",
                    lay.note, juce::Justification::centredLeft, true);
    }

    // What this panel needs to say everything it has. The card is sized to it, so a channel
    // with four sentences and one with none both look finished instead of leaving half a
    // window of empty card under the last line. The sum is layout()'s own steps.
    int wantedHeight() const
    {
        const int lines = juce::jmax (1, sentences().size());
        return 20 + 22 + 22 + kKnobBlockH + 20 + 1 + 16 + 18 + 10 + lines * kBulletH
                  + 24 + DineKnob::cellHeight (kLevelDial) + 18 + Dine::Metric::button + 16 + 16 + 20;
    }

    void resized() override
    {
        auto lay = layout();
        // Five controls across the whole card, evenly: the design sets them at a 141 pt pitch
        // in a 723 pt card, which is the width shared out rather than a fixed cell.
        auto row = lay.knobs;
        const int each = juce::jmax (96, row.getWidth() / 5);
        for (int i = 0; i < 5; ++i)
            knobs[size_t (i)]->setBounds (row.getX() + i * each, row.getY(), each, row.getHeight());

        if (voiceTrack.isVisible())
        {
            const int w = juce::jmax (64, juce::jmax (speakingTab.idealWidth(), singingTab.idealWidth()) + 10) * 2 + 6;
            voiceTrack.setBounds (lay.title.withWidth (lay.title.getWidth()).removeFromRight (w)
                                           .withSizeKeepingCentre (w, Dine::Metric::control));
            auto track = voiceTrack.getLocalBounds().reduced (2, 2);
            speakingTab.setBounds (track.removeFromLeft (track.getWidth() / 2));
            track.removeFromLeft (2);
            singingTab.setBounds (track);
        }

        level.setBounds (lay.level);
        auto buttons = lay.buttons;
        tuneButton.setBounds (buttons.removeFromLeft (juce::jmax (116, tuneButton.idealWidth())));
        buttons.removeFromLeft (20);
        putBack.setBounds (buttons.removeFromLeft (juce::jmax (110, putBack.idealWidth())));
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

            // THE RING IS DRAWN FROM THE KNOB, NOT FROM A NUMBER. A 3 pt stroke reads as a
            // weight on the 48 pt knob it was written for and as a hairline on this one, which
            // is twice the size - so a Simple view, the one a volunteer uses, had the thinnest
            // ribbons in the product. It is a proportion of the radius, and the pointer keeps
            // clear of its inner edge.
            const float ring = juce::jmax (4.0f, radius * 0.17f);
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
            g.setColour (Dine::control);
            g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
            g.setColour (isEnabled() ? Dine::accent : Dine::ink4);
            g.strokePath (arc, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto dot = centre.getPointOnCircumference (radius - ring - 3.5f, angle);
            g.setColour (isEnabled() ? Dine::ink : Dine::ink4);
            g.fillEllipse (juce::Rectangle<float> (5.5f, 5.5f).withCentre (dot));

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
    static constexpr int kLevelDial = 56;   // the level: a rotary of its own, under the sentences
    static constexpr int kKnobBlockH = kKnobSize + 6 + 16 + 14;
    static constexpr int kBulletH = 30;

    // Everything on the panel, positioned once, so paint and layout cannot disagree. The
    // design's `Simple` frame (89:25705) measures from the top and lets the panel run on:
    // a channel with four sentences is taller than one with none, and the level and the two
    // verbs follow the sentences rather than sitting at the foot of whatever height it has.
    struct Lay
    {
        juce::Rectangle<int> title, knobs, rule, didHead, bullets, level, buttons, note;
    };

    Lay layout() const
    {
        Lay l;
        auto r = getLocalBounds().reduced (24, 20);
        l.title = r.removeFromTop (22);
        r.removeFromTop (22);
        l.knobs = r.removeFromTop (kKnobBlockH);
        r.removeFromTop (20);
        l.rule = r.removeFromTop (1);
        r.removeFromTop (16);
        l.didHead = r.removeFromTop (18);
        r.removeFromTop (10);
        const int lines = juce::jmax (1, sentences().size());
        l.bullets = r.removeFromTop (juce::jmin (r.getHeight(), lines * kBulletH));
        r.removeFromTop (24);
        l.level = r.removeFromTop (DineKnob::cellHeight (kLevelDial))
                   .removeFromLeft (juce::jmax (96, level.cellWidth()));
        r.removeFromTop (18);
        l.buttons = r.removeFromTop (Dine::Metric::button);
        r.removeFromTop (16);
        l.note = r.removeFromTop (16);
        return l;
    }

    // A microphone somebody sings or speaks into is a person; a keyboard is not.
    bool personal() const noexcept
    {
        switch (roleFamily (role))
        {
            case RoleFamily::LeadVocal:
            case RoleFamily::BackingVocal:
            case RoleFamily::Choir:
            case RoleFamily::Speech:   return true;
            default:                   return false;
        }
    }

    // PUT BACK carries the clock of what it puts the channel back to, the way the design writes it.
    juce::String putBackLabel() const
    {
        if (strip < 0) return "PUT IT BACK";
        const auto& records = controller.getStripHistory (strip);
        for (auto it = records.rbegin(); it != records.rend(); ++it)
            if (juce::String (it->what).startsWith ("TUNE") && it->whenMs > 0)
                return "PUT BACK " + juce::Time (juce::int64 (it->whenMs)).formatted ("%l:%M %p").trim();
        return "PUT IT BACK";
    }

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
    DineKnob level;
    DineButton tuneButton { "TUNE CHANNEL", DineButton::Style::Filled };
    DineButton putBack { "PUT IT BACK", DineButton::Style::Standard };
    DineSegmentRow voiceTrack;
    DineButton speakingTab { "Speaking", DineButton::Style::Segment };
    DineButton singingTab  { "Singing",  DineButton::Style::Segment };
    bool updating = false;
};

// ------------------------------------------------------------------ Trail
// WHAT DINE DID, to this channel.
//
// The design's right-hand column (`74:11725`): one record per thing that ever set this
// channel, newest first - when it happened, what did it, the sentence that says what it
// was, and the one control a record carries, which is the way back to it. A tune and a
// hand edit are the same kind of record on purpose: the trail's job is to make a hand edit
// as visible, and as undoable, as a TUNE MIX.
class AdvancedPage::Trail : public juce::Component
{
public:
    explicit Trail (MixController& c) : controller (c), list (*this)
    {
        view.setViewedComponent (&list, false);
        Dine::nativeScrolling (view);
        view.setScrollBarsShown (true, false);
        addAndMakeVisible (view);
    }

    std::function<void (int)> onRestore;       // put record `index` back on this channel

    // Scrolls the column to the top: the newest record is the one the channel is on now.
    void showHistory() { view.setViewPosition (0, 0); }

    void setHistory (const std::vector<HistoryView>& v)
    {
        if (v == list.history) return;
        list.history = v;
        list.setSize (view.getWidth(), list.heightFor (view.getWidth()));
        list.repaint();
        repaint();
    }

    void setChannel (const juce::String& emptyLine)
    {
        if (emptyLine == empty) return;
        empty = emptyLine;
        view.setViewPosition (0, 0);
        repaint();
    }

    // Gain staging is the first move in a mix, and a preamp that is wrong is the one thing
    // no amount of tuning can put right - so an input that needs attention says so here,
    // above the records. An input that is fine says nothing: this column is a history.
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

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto head = area.removeFromTop (kHeadH).reduced (kPad, 0).withTrimmedTop (16);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, "What DINE did", head.removeFromTop (18), juce::Justification::topLeft, true);

        if (const int gh = gainHeight(); gh > 0)
            paintGain (g, area.removeFromTop (gh).reduced (10, 0).withTrimmedBottom (8));

        if (! list.history.empty()) return;
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, empty, area.reduced (kPad, 0).withTrimmedTop (4).withHeight (80),
                          juce::Justification::topLeft, 5, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (kHeadH);
        area.removeFromTop (gainHeight());
        view.setBounds (area);
        list.setSize (view.getWidth(), list.heightFor (view.getWidth()));
    }

    static constexpr int kHeadH = 44, kPad = 16;

private:
    int gainHeight() const { return advice.known && advice.needsAttention() ? 104 : 0; }

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
        Dine::fillRounded (g, r.toFloat(), Dine::mix (colour, 0.16f, Dine::rail), Dine::Radius::control);
        r = r.reduced (12, 12);
        g.setColour (colour);
        g.setFont (Dine::text (12.0f, 600));
        Dine::drawText (g, juce::String (advice.headline), r.removeFromTop (16), juce::Justification::centredLeft, true);
        r.removeFromTop (4);
        auto figures = r.removeFromTop (14);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        if (std::fabs (advice.digitalGainDb) >= 0.05f)
            Dine::drawText (g, "DINE " + db1 (advice.digitalGainDb), figures.removeFromRight (66), juce::Justification::centredRight);
        Dine::drawText (g, advice.capturePeakDb <= -119.0f ? juce::String ("no signal")
                                                           : juce::String (advice.capturePeakDb, 1) + " dBFS in",
                    figures, juce::Justification::centredLeft, true);
        r.removeFromTop (4);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawFittedText (g, juce::String (advice.detail), r, juce::Justification::topLeft, 3, 1.0f);
    }

    // The records themselves. One shape, painted in one place and hit-tested the same way.
    class List : public juce::Component
    {
    public:
        explicit List (Trail& owner) : trail (owner) {}

        int heightFor (int width)
        {
            heights.clear();
            int y = 0;
            const auto summaryFont = Dine::text (12.0f);
            for (const auto& v : history)
            {
                int h = kBase;
                if (v.summary.isNotEmpty())
                {
                    const float w = juce::GlyphArrangement::getStringWidth (summaryFont, v.summary);
                    h += juce::jlimit (1, 2, int (std::ceil (w / juce::jmax (60.0f, float (width) - 2 * kPad - 24.0f)))) * 16 + 4;
                }
                heights.push_back (h);
                y += h + kGap;
            }
            return juce::jmax (y, 1);
        }

        void paint (juce::Graphics& g) override
        {
            chips.clear();
            auto r = getLocalBounds().reduced (10, 0);
            for (size_t i = 0; i < history.size() && i < heights.size(); ++i)
            {
                const auto& v = history[i];
                auto row = r.removeFromTop (heights[i]);
                r.removeFromTop (kGap);
                Dine::fillRounded (g, row.toFloat(), Dine::item, Dine::Radius::control);
                auto body = row.reduced (12, 10);

                // When, and what did it: a tune says so in the accent, a hand edit quietly.
                auto top = body.removeFromTop (14);
                const bool tuned = v.what.startsWith ("TUNE");
                const auto whatFont = Dine::text (11.0f, 500);
                g.setColour (tuned ? Dine::accent : Dine::ink3);
                g.setFont (whatFont);
                Dine::drawText (g, tuned ? v.what : juce::String ("Edited"),
                            top.removeFromRight (juce::jmin (top.getWidth() / 2,
                                                             Dine::textWidth (whatFont, tuned ? v.what : juce::String ("Edited")) + 2)),
                            juce::Justification::centredRight);
                g.setColour (Dine::ink4);
                g.setFont (Dine::mono (11.0f));
                Dine::drawText (g, v.when, top, juce::Justification::centredLeft, true);

                body.removeFromTop (4);
                g.setColour (Dine::ink);
                g.setFont (Dine::text (13.0f, 500));
                Dine::drawText (g, headlineFor (v), body.removeFromTop (18), juce::Justification::centredLeft, true);

                if (v.summary.isNotEmpty())
                {
                    body.removeFromTop (4);
                    g.setColour (Dine::ink3);
                    g.setFont (Dine::text (12.0f));
                    auto lines = body.removeFromTop (body.getHeight() - Dine::Metric::button - 10);
                    Dine::drawFittedText (g, v.summary, lines, juce::Justification::topLeft, 2, 1.0f);
                }

                auto chip = body.removeFromBottom (Dine::Metric::button)
                                .removeFromLeft (juce::jmin (body.getWidth(), kChipW));
                Dine::drawStandard (g, chip.toFloat(), Dine::Radius::control, hover == int (i), false);
                g.setColour (Dine::ink2);
                g.setFont (Dine::caps (11.0f, 0.06f, 600));
                Dine::drawText (g, "PUT BACK", chip, juce::Justification::centred);
                chips.push_back (chip);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown() || ! trail.onRestore) return;
            for (size_t i = 0; i < chips.size(); ++i)
                if (chips[i].contains (e.getPosition())) { trail.onRestore (int (i)); return; }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int over = -1;
            for (size_t i = 0; i < chips.size(); ++i) if (chips[i].contains (e.getPosition())) over = int (i);
            if (over == hover) return;
            hover = over;
            setMouseCursor (over >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }

        void mouseExit (const juce::MouseEvent&) override { if (hover >= 0) { hover = -1; repaint(); } }

        std::vector<HistoryView> history;        // newest first

        static constexpr int kBase = 92, kGap = 8, kPad = 12, kChipW = 83;

    private:
        // The sentence a record leads with: what it was, in the trail's words.
        static juce::String headlineFor (const HistoryView& v)
        {
            if (v.what.startsWith ("TUNE")) return "Set by " + v.what;
            if (v.what.startsWith ("Put back")) return v.what;
            if (v.what.startsWith ("Mix Buddy")) return v.what;
            return "Hand-edited";
        }

        Trail& trail;
        std::vector<int> heights;
        std::vector<juce::Rectangle<int>> chips;  // where each row's PUT BACK was painted, for the click
        int hover = -1;
    };

    MixController& controller;
    MixController::InputAdvice advice;
    juce::String empty;
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
    tuneChannelButton.setCaps (true);
    tuneChannelButton.setFontPx (11.0f);
    tuneChannelButton.setTooltip ("DINE listens to this channel and sets its chain and level. A channel linked to its other "
                                  "half is tuned with it. Nothing else in the mix moves.");
    tuneChannelButton.onClick = [this]
    {
        if (onTuneChannel && ! selection.isBus && selection.strip >= 0) onTuneChannel (selection.strip);
    };
    addChildComponent (tuneChannelButton);

    // The two keys that change what the room hears, on the channel that is open.
    for (auto* b : { &muteButton, &soloButton })
    {
        b->setCaps (true);
        b->setFontPx (11.0f);
        b->setPadX (8);
        addChildComponent (*b);
    }
    muteButton.setTint (Dine::keyMute);
    soloButton.setTint (Dine::keySolo);
    muteButton.setTooltip ("Muted: the signal arrives and is not heard. Recording is unaffected.");
    soloButton.setTooltip ("Soloed: this and nothing else. Solo never changes what the room hears.");
    muteButton.onClick = [this]
    {
        if (selection.isBus)
        {
            if (selection.bus != MixBus::Master)
                controller.setBusMute (selection.bus, ! controller.getBase().buses[size_t (selection.bus)].mute);
        }
        else if (selection.strip >= 0)
            controller.setStripMute (selection.strip, ! controller.getBase().strips[size_t (selection.strip)].mute);
        refresh();
    };
    soloButton.onClick = [this]
    {
        if (selection.isBus)
        {
            if (selection.bus != MixBus::Master)
                controller.setBusSolo (selection.bus, ! controller.getBase().buses[size_t (selection.bus)].solo);
        }
        else if (selection.strip >= 0)
            controller.setStripSolo (selection.strip, ! controller.getBase().strips[size_t (selection.strip)].solo);
        refresh();
    };

    trail = std::make_unique<Trail> (controller);
    trail->onRestore = [this] (int record)
    {
        if (selection.isBus || selection.strip < 0) return;
        // The list is newest first; the controller keeps its records oldest first.
        const int n = int (controller.getStripHistory (selection.strip).size());
        if (controller.restoreStripTune (selection.strip, n - 1 - record)) refresh();
    };
    addAndMakeVisible (*trail);

    railTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Left, "Channels");
    railTab->onClick = [this] { setRailShown (! railShown); };
    addAndMakeVisible (*railTab);

    trailTab = std::make_unique<DinePanelTab> (DinePanelTab::Side::Right, "What DINE did");
    trailTab->onClick = [this] { setTrailShown (! trailShown); };
    addAndMakeVisible (*trailTab);

    chain->onStageChanged = [this] { path->refresh(); refresh(); };
    chain->onImportSample = [this] (RoleFamily family) { if (onImportSample) onImportSample (family); };
    chain->drumKitName = [this] { return drumKitName ? drumKitName() : juce::String(); };
    chain->onDrumKit = [this] (juce::Component& anchor) { if (onDrumKit) onDrumKit (anchor); };
    rebuild();
}

// A folded panel keeps only its gutter: the handle stays where it was, so the width
// comes back with one click and the channel never moves out from under the pointer.
void AdvancedPage::setSampleLibrary (std::function<const SampleLibrary*()> f)
{
    chain->sampleLibrary = std::move (f);
}

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
    if (rebuilding) return;
    const juce::ScopedValueSetter<bool> guard (rebuilding, true);
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
            auto row = std::make_unique<Row> (busWord (bus) + " bus", Row::Kind::Bus, bus);
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

int AdvancedPage::numStages() const { return chain->numStages(); }

juce::String AdvancedPage::stageName (int index) const
{
    const auto& views = chain->stageViews();
    if (index < 0 || index >= int (views.size())) return {};
    return views[size_t (index)].label.toLowerCase().replace(" ", "-");
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
    tuneChannelButton.setVisible (! on && ! selection.isBus && selection.strip >= 0);
    resized();
    repaint();
}

// MUTE and SOLO, as the mix has them. The master has nothing to solo against, so it is the
// one selection that carries neither: muting the master is what DIM and MUTE in the toolbar
// are for, and they say so from every workspace.
void AdvancedPage::refreshKeys()
{
    const bool master = selection.isBus && selection.bus == MixBus::Master;
    const bool wanted = ! master && (selection.isBus || selection.strip >= 0);
    // Whether the keys are wanted is decided here; whether they fit is the layout's call.
    if (headKeysWanted != wanted)
    {
        headKeysWanted = wanted;
        resized();
    }
    if (! wanted) return;
    const auto& kept = controller.getBase();
    bool muted = false, soloed = false;
    if (selection.isBus)
    {
        muted = kept.buses[size_t (selection.bus)].mute;
        soloed = kept.buses[size_t (selection.bus)].solo;
    }
    else if (selection.strip < kept.numStrips)
    {
        muted = kept.strips[size_t (selection.strip)].mute;
        soloed = kept.strips[size_t (selection.strip)].solo;
    }
    muteButton.setStyle (muted ? DineButton::Style::Filled : DineButton::Style::Standard);
    soloButton.setStyle (soloed ? DineButton::Style::Filled : DineButton::Style::Standard);
    muteButton.setEnabled (! controller.isBypassed());
    soloButton.setEnabled (! controller.isBypassed());
}

void AdvancedPage::showSelection()
{
    refreshKeys();
    {
        const bool want = ! simpleView && ! selection.isBus && selection.strip >= 0;
        if (tuneChannelButton.isVisible() != want) { tuneChannelButton.setVisible (want); resized(); }
    }
    simple->setStrip (selection.isBus ? -1 : selection.strip);
    simple->setVisible (simpleView && ! selection.isBus && selection.strip >= 0);
    path->setVisible (! simple->isVisible());
    chain->setVisible (! simple->isVisible());

    trail->setChannel (controller.getPlan() == nullptr
                           ? "Nothing yet. This channel runs on the profile's baseline for its source; TUNE MIX "
                             "listens, then sets every stage from what it hears."
                           : "Nothing on this channel yet. Every tune and every hand edit lands here, with the way "
                             "back to the one before it.");
    trail->setGain (selection.isBus ? MixController::InputAdvice {} : controller.getInputAdvice (selection.strip));
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
    // Rebuilt against the same list rebuild() reads - the controller's. The engine's catches up
    // with an import a moment later (a large folder, the device re-opening), and comparing
    // against it here while rebuild() counted the controller's turned the two into a loop:
    // rebuild -> select -> the chain rebuilds -> refresh -> rebuild, until the stack ran out.
    if (controller.getGraph().numStrips() != builtForStrips && ! rebuilding) rebuild();
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

    chain->refresh();
    path->refresh();
    refreshKeys();
    trail->setGain (selection.isBus ? MixController::InputAdvice {} : controller.getInputAdvice (selection.strip));
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
    juce::ignoreUnused (peak);

    // The rail rows, the head, the chain and the trail are all components that repaint
    // themselves when their own reading changes. What this page paints is the rail's chrome
    // and the workspace's two bands - and repainting the whole Inspector thirty times a
    // second for that is the single most expensive thing the app was doing: on a 48-channel
    // console one full repaint of this page costs more than a 30 Hz frame has
    // (dine_ui_snapshots --frames). So it only happens when something it draws has changed.
    const InspectorLook now { selection.isBus, selection.bus, selection.strip, int (rows.size()),
                              controller.isBypassed(), controller.isPrepared(), railShown, trailShown,
                              controller.getTuneCount() };
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
    g.fillRect (trailArea);
    g.setColour (Dine::hair);
    g.fillRect (rail.withLeft (rail.getRight() - 1));          // the seams either side of the middle column
    g.fillRect (trailArea.withWidth (1));

    if (railShown)
    {
        auto railHead = rail.removeFromTop (kRailHeadH).reduced (kRailPadX, 0).withTrimmedTop (16);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, "Channels", railHead.withTrimmedRight (30).removeFromTop (18), juce::Justification::topLeft, true);
    }

    paintHead (g, area.removeFromTop (kHeadH));
}

// THE CHANNEL'S OWN NAME, and the two things that are about how you are looking at it.
//
// The design's head is a name, one line of provenance and nothing else: the level, the pan,
// the keys and the meters are a console's job and they are on MIXER, on the strip and on the
// chain foot, which are never more than one press away. An Inspector that repeats them is an
// Inspector with less room for the thing it is actually for.
void AdvancedPage::paintHead (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto& graph = controller.getGraph();
    auto r = area.reduced (kPadX, 0);
    r.setRight (juce::jmin (r.getRight(), headControlsLeft - 12));

    juce::String title, sub;
    juce::Colour tint = busTint (selection.isBus ? selection.bus : MixBus::Master);
    if (selection.isBus)
    {
        const bool master = selection.bus == MixBus::Master;
        title = master ? juce::String ("Master") : busWord (selection.bus) + " bus";
        sub = master ? juce::String ("MASTER  ") + Glyph::dot() + "  Master bus"
                     : juce::String (mixBusName (selection.bus)).toUpperCase() + "  " + Glyph::dot() + "  "
                           + juce::String (graph.stripsOnBus (selection.bus)) + " inputs";
    }
    else if (selection.strip >= 0 && selection.strip < graph.numStrips())
    {
        const auto& s = graph.strips[size_t (selection.strip)];
        tint = busTint (s.bus);
        title = s.name;
        sub = deviceInLabel (s).toUpperCase() + "  " + Glyph::dot() + "  " + sentenceCase (mixBusName (s.bus));
    }
    // On a narrow window the line gives up its last part first (when it was tuned), then the
    // group, rather than be cut off in the middle of a word.
    const auto subFont = Dine::text (12.0f);
    const int subRoom = r.getWidth() - 12;
    const juce::String withTuned = sub + "  " + juce::String (Glyph::dot()) + "  " + tunedLabel();
    if (Dine::textWidth (subFont, withTuned) <= subRoom) sub = withTuned;
    else if (Dine::textWidth (subFont, sub) > subRoom) sub = sub.upToFirstOccurrenceOf (juce::String ("  ") + Glyph::dot(), false, false);

    // The colour bar beside the name: which group this channel belongs to, said once.
    Dine::fillRounded (g, r.removeFromLeft (3).withTrimmedTop (20).withHeight (40).toFloat(), tint, 1.5f);
    r.removeFromLeft (9);
    auto text = r.removeFromTop (66).withTrimmedTop (16);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, title, text.removeFromTop (28), juce::Justification::topLeft, true);
    text.removeFromTop (2);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
    Dine::drawText (g, sub, text.removeFromTop (16), juce::Justification::topLeft, true);
}

// "tuned 8:27 PM", or what the channel is running on when nothing has tuned it yet.
juce::String AdvancedPage::tunedLabel() const
{
    if (! selection.isBus && selection.strip >= 0)
    {
        const auto& records = controller.getStripHistory (selection.strip);
        for (auto it = records.rbegin(); it != records.rend(); ++it)
            if (juce::String (it->what).startsWith ("TUNE") && it->whenMs > 0)
                return "tuned " + juce::Time (juce::int64 (it->whenMs)).formatted ("%l:%M %p").trim();
    }
    return controller.getPlan() != nullptr ? juce::String ("tuned by DINE") : juce::String ("the baseline");
}

void AdvancedPage::resized()
{
    auto area = getLocalBounds();
    auto rail = area.removeFromLeft (railWidth());
    auto trailArea = area.removeFromRight (trailWidth());

    if (railShown) railTab->setBounds (rail.withHeight (kRailHeadH).removeFromRight (30)); else railTab->setBounds (rail);
    if (trailShown) trailTab->setBounds (trailArea.withHeight (Trail::kHeadH).removeFromRight (30)); else trailTab->setBounds (trailArea);

    rail.removeFromTop (kRailHeadH);
    if (railShown) viewport.setBounds (rail);

    const int rowH = 32, headerH = 28, sectionGap = 6;
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
    total += 12;
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
            // A 28 pt row inside a 32 pt pitch, exactly as the design measures the rail.
            item->setBounds (0, y + 2, listHolder.getWidth(), rowH - 4);
            y += rowH;
        }
    }

    if (trailShown) trail->setBounds (trailArea);

    // SIMPLE / ADVANCED and RE-TUNE sit level with the channel's name, at the right.
    {
        auto row = area.withTrimmedTop (24).withHeight (Dine::Metric::control).reduced (kPadX, 0);
        // What the row holds, by priority: the channel's name keeps kNameMin, RE-TUNE and the
        // view tabs always fit, TUNE CHANNEL says the verb alone (TUNE) when the row is short,
        // and MUTE / SOLO step out of the head last - they are on every strip anyway - rather
        // than land on the name, which is what a narrow window used to do.
        constexpr int kNameMin = 180;
        const int rw = juce::jmax (76, retuneButton.idealWidth());
        const int tabs = juce::jmax (56, juce::jmax (simpleTab.idealWidth(), advancedTab.idealWidth()) + 10) * 2 + 6;
        const int kw = juce::jmax (52, juce::jmax (muteButton.idealWidth(), soloButton.idealWidth()));
        const bool keysWanted = headKeysWanted;
        const int keys = keysWanted ? 20 + 6 + 2 * kw : 0;
        tuneChannelButton.setButtonText ("TUNE CHANNEL");
        auto tuneW = [&] { return tuneChannelButton.isVisible() ? juce::jmax (tuneChannelButton.getButtonText() == "TUNE" ? 64 : 112,
                                                                              tuneChannelButton.idealWidth()) + 10 : 0; };
        if (row.getWidth() - (rw + 10 + tuneW() + tabs + keys) < kNameMin) tuneChannelButton.setButtonText ("TUNE");
        const bool keysFit = row.getWidth() - (rw + 10 + tuneW() + tabs + keys) >= kNameMin;

        retuneButton.setBounds (row.removeFromRight (rw));
        row.removeFromRight (10);
        if (tuneChannelButton.isVisible())
        {
            tuneChannelButton.setBounds (row.removeFromRight (tuneW() - 10));
            row.removeFromRight (10);
        }
        viewTrack.setBounds (row.removeFromRight (tabs));
        auto track = viewTrack.getLocalBounds().reduced (2, 2);
        simpleTab.setBounds (track.removeFromLeft (track.getWidth() / 2));
        track.removeFromLeft (2);
        advancedTab.setBounds (track);
        muteButton.setVisible (keysWanted && keysFit);
        soloButton.setVisible (keysWanted && keysFit);
        if (keysWanted && keysFit)
        {
            row.removeFromRight (20);
            soloButton.setBounds (row.removeFromRight (kw));
            row.removeFromRight (6);
            muteButton.setBounds (row.removeFromRight (kw));
        }
        headControlsLeft = row.getRight();
    }

    if (simpleView && simple->isVisible())
    {
        path->setBounds (0, 0, 0, 0);
        chain->setBounds (0, 0, 0, 0);
        auto card = area.withTrimmedTop (84).reduced (kPadX, 0).withTrimmedBottom (24);
        // The panel is sized to what it says. A card that runs to the foot of a tall window
        // with its last line a third of the way down does not look finished.
        simple->setBounds (card.withHeight (juce::jmin (card.getHeight(), simple->wantedHeight())));
        return;
    }
    const auto pathRow = area.withTrimmedTop (kPathTop).reduced (kPadX, 0);
    const int pathH = path->wantedHeight (pathRow.getWidth());
    path->setBounds (pathRow.withHeight (pathH));
    chain->setBounds (area.withTrimmedTop (kPathTop + pathH + (kCardTop - kPathTop - SignalPath::height)).reduced (kPadX, 0).withTrimmedBottom (24));
}

} // namespace livemix
