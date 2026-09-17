#include "MixerPage.h"
#include "Core/DbUtils.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kHeaderH   = 38;      // the sub-toolbar: what this is, then the controls
    constexpr int kPadX      = 18;
    constexpr int kPadY      = 12;

    constexpr int kStripGap  = 0;       // a console has no gaps: a hairline separates the columns
    constexpr int kGroupGap  = 10;
    constexpr int kBandH     = 4;       // the group's colour, as a bar over the family
    constexpr int kBandGap   = 6;

    constexpr int kRowH      = 46;      // list view
    constexpr int kRowGap    = 3;
    constexpr int kSectionH  = 24;
    constexpr int kMaxStripH = 900;     // a fader is a fader, not a wall: taller than this reads as a mistake

    int columnWidthFor (MixerPage::Size s) noexcept
    {
        return s == MixerPage::Size::Narrow ? 58 : s == MixerPage::Size::Wide ? 116 : 92;
    }

    // Gain staging, as a chip on the strip: the colour says how bad it is, the words say
    // what to do at the desk. Shared by both layouts so a strip and a row never disagree.
    juce::Colour gainAdviceColour (MixController::InputAdvice::Level level) noexcept
    {
        using Level = MixController::InputAdvice::Level;
        switch (level)
        {
            case Level::Clipping:
            case Level::Faint:    return Dine::crit;
            case Level::Low:
            case Level::Hot:      return Dine::warn;
            case Level::Digital:  return Dine::warn;
            case Level::NotHeard: return Dine::ink4;
            default:              return Dine::ok;
        }
    }

    juce::String gainAdviceChip (const MixController::InputAdvice& a, bool narrow)
    {
        using Level = MixController::InputAdvice::Level;
        const juce::String move = juce::String (juce::roundToInt (std::fabs (a.consoleMoveDb)));
        switch (a.level)
        {
            case Level::Clipping: return narrow ? "CLIP" : "CLIPPING";
            case Level::Faint:    return narrow ? "CHECK" : "CHECK MIC";
            case Level::Low:      return (narrow ? "GAIN +" : "PREAMP +") + move;
            case Level::Hot:      return (narrow ? "GAIN " : "PREAMP ") + Glyph::minus() + move;
            case Level::Digital:  return (narrow ? "GAIN " : "PREAMP ")
                                         + (a.consoleMoveDb > 0.0f ? juce::String ("+") : Glyph::minus()) + move;
            case Level::NotHeard: return narrow ? "QUIET" : "NOT HEARD";
            default:              return {};
        }
    }

    // The send slots, abbreviated the way a console labels them.
    const char* shortFxName (FxSlot s) noexcept
    {
        switch (s)
        {
            case FxSlot::VocalPlate:  return "PLT";
            case FxSlot::VocalDelay:  return "DLY";
            case FxSlot::BgvHall:     return "HALL";
            case FxSlot::SnarePlate:  return "SNR";
            case FxSlot::DrumRoom:    return "ROOM";
            case FxSlot::Count:       break;
        }
        return "FX";
    }

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::Colour busTint (MixBus b) noexcept { return Dine::busTint (b); }

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

}

// ------------------------------------------------------------------ Strip
// One source, group bus or the master. The same component in both views: STRIPS lays it
// out as a column, LIST as a row, so a mute is a mute wherever you press it.
class MixerPage::Strip : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    enum class Kind { Channel, Bus, Master };
    enum class Layout { Column, Row };

    Strip (MixController& c, AppServices& s, Kind k, MixBus busFamily, ChannelRole r, int stripIndex,
           const juce::String& title, const juce::String& sourceText, const std::string& iconKey = {})
        : controller (c), services (s), kind (k), bus (busFamily), role (r), strip (stripIndex),
          name (title), source (sourceText),
          icon (k == Kind::Channel ? Dine::iconFor (iconKey, r) : Dine::Icon::Bus),
          meter (DineMeter::Style::Segments),
          muteButton ("M", Dine::keyMute),
          soloButton ("S", Dine::keySolo),
          armButton ("R", Dine::keyRec),
          monitorButton ("A", Dine::keyMon)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        Dine::dragOnly (fader);            // a swipe across the bank scrolls it, it never moves a fader
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

        muteButton.setVisible (kind != Kind::Master);
        soloButton.setVisible (kind != Kind::Master);
        armButton.setVisible (kind == Kind::Channel);
        monitorButton.setVisible (kind == Kind::Channel);
        muteButton.setTooltip ("Mute.");
        soloButton.setTooltip ("Solo: hear this alone.");
        armButton.setTooltip ("Set this source to record: when you press Record it is captured, raw, on its own track. Engineers call this arming.");
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

        setTooltip (name + "  " + Glyph::dot() + "  " + source);

        numberText = kind == Kind::Channel ? juce::String (stripIndex + 1).paddedLeft ('0', 2) : juce::String();
        outText = kind == Kind::Channel ? juce::String (mixBusName (bus)).toUpperCase() : "MASTER";
        stereo = true;
        if (kind == Kind::Channel && stripIndex >= 0 && stripIndex < controller.getGraph().numStrips())
            stereo = controller.getGraph().strips[size_t (stripIndex)].inputB >= 0;

        // A column is a flat, opaque surface, and a console is swiped sideways constantly:
        // twenty-four columns of hand-drawn text cannot be redrawn on every frame of a
        // scroll. Cached as an image, scrolling the bank is a blit, and the only thing ever
        // redrawn is the part of a strip that actually moved.
        setOpaque (true);
        setBufferedToImage (true);

        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        addAndMakeVisible (pan);
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);
        addAndMakeVisible (armButton);
        addAndMakeVisible (monitorButton);
    }

    void setOpenHandler (std::function<void()> h) { open = std::move (h); }
    void setSelectHandler (std::function<void()> h) { select = std::move (h); }
    void setTuneHandler (std::function<void()> h) { tune = std::move (h); }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    bool isSelected() const noexcept { return selected; }

    Kind getKind() const noexcept { return kind; }
    MixBus getBus() const noexcept { return bus; }
    int getStripIndex() const noexcept { return strip; }

    void setLayout (Layout l, Size s)
    {
        layout = l;
        size = s;
        // A row is a rounded card on the page's ground, so it is not opaque; a column is.
        setOpaque (l == Layout::Column);
        setBufferedToImage (true);
        shown = false;
        fader.setSliderStyle (l == Layout::Column ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
        pan.setVisible (kind == Kind::Channel && (l == Layout::Row || s != Size::Narrow));
        resized();
        repaint();
    }

    void setSendsVisible (bool v)
    {
        if (v == showSends) return;
        showSends = v;
        resized();
        repaint();
    }

    int columnWidth() const noexcept
    {
        const int w = columnWidthFor (size);
        return kind == Kind::Master ? w + (size == Size::Narrow ? 34 : 44) : kind == Kind::Bus ? w + 4 : w;
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

        if (kind == Kind::Channel)
        {
            const auto next = controller.getInputAdvice (strip);
            if (next.needsAttention() != advice.needsAttention()) needsLayout = true;
            advice = next;
        }

        fader.setValue (faderDb, juce::dontSendNotification);
        pan.setValue (panValue);
        meter.setLevels (peak, hold, clipped);
        meter.setMuted (muted);
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
        muteButton.setOn (mute);
        soloButton.setOn (solo);
        if (kind == Kind::Channel)
        {
            const auto& project = services.daw().getProject();
            const bool isArmed = strip >= 0 && strip < int (project.tracks.size()) && project.tracks[size_t (strip)].armed;
            const auto mode = strip >= 0 && strip < int (project.tracks.size()) ? project.tracks[size_t (strip)].monitor
                                                                                : MonitorMode::Auto;
            armed = isArmed;
            armButton.setOn (isArmed);
            monitorButton.setLetter (mode == MonitorMode::Off ? Glyph::dash() : mode == MonitorMode::Input ? "I" : "A");
            monitorButton.setOn (mode == MonitorMode::Input);
        }

        if (kind == Kind::Master && controller.isPrepared())
        {
            // One place answers "is this loud enough, and is it safe": the controller's own
            // reading, so the mixer, LIVE, the Inspector and an export can never disagree.
            const auto m = controller.getMasterLoudness();
            shortTermLufs = m.shortTermLufs;
            integratedText = m.integratedLufs <= -60.0f ? Glyph::dash() : db1 (m.integratedLufs);
            shortTermText = m.shortTermLufs <= -60.0f ? Glyph::dash() : db1 (m.shortTermLufs);
            truePeakText = m.truePeakDb <= -60.0f ? Glyph::dash() : db1 (m.truePeakDb);
            // How hard the master limiter is working. A mix that only reaches its target
            // because the limiter is holding 6 dB down is not finished, it is squashed - and
            // until this was on the console there was no way to see it.
            limiterGrDb = m.limiterReductionDb;
            grText = m.limiterReductionDb < 0.1f ? Glyph::dash() : db1 (-m.limiterReductionDb);
            targetLufs = m.targetLufs;
            truePeakOver = m.truePeakDb > m.ceilingDb + 0.1f;
        }

        // What the audio actually meets, and where it goes: read once per tick from the same
        // parameters the engine was given, so the strip, the foot and the Inspector agree.
        const auto& channel = kind == Kind::Channel && strip >= 0 && strip < state.numStrips
                                  ? state.strips[size_t (strip)].channel
                                  : state.buses[size_t (bus)].channel;
        auto nextInserts = activeChainStages (channel, kind == Kind::Master, stereo);
        if (int (nextInserts.size()) != int (insertList.size())) needsLayout = true;
        insertList = std::move (nextInserts);

        sendList.clear();
        if (kind == Kind::Channel && strip >= 0 && strip < state.numStrips)
        {
            const auto& graph = controller.getGraph();
            const auto& sends = state.strips[size_t (strip)].sendDb;
            for (int f = 0; f < int (FxSlot::Count) && int (sendList.size()) < maxSends(); ++f)
                if (graph.fxUsed[size_t (f)] && sends[size_t (f)] > kSilenceDb)
                    sendList.push_back ({ juce::String (shortFxName (FxSlot (f))), sends[size_t (f)] });
        }
        if (int (sendList.size()) != lastSendCount) { lastSendCount = int (sendList.size()); needsLayout = true; }

        updating = false;
        if (needsLayout) { needsLayout = false; resized(); shown = false; }
        showLook (currentLook());
    }

    // -------------------------------------------------------------- repainting
    // The page refreshes every strip thirty times a second; a console of twenty-four
    // columns cannot be *redrawn* thirty times a second. The meters, faders and keys are
    // components and repaint themselves; everything the strip draws by hand is remembered
    // here, so the live numbers repaint their own row and the column as a whole is only
    // redrawn when the mix actually changes under it.
    struct Look
    {
        juce::String level, peak, balance, integrated, shortTerm, truePeak, advice;
        juce::Colour adviceTint { juce::Colours::transparentBlack };
        std::vector<juce::String> inserts, sends;
        float peakDb = -120.0f, shortTermLufs = -70.0f;
        bool mute = false, solo = false, selected = false, bypassed = false;
    };

    Look currentLook() const
    {
        Look l;
        l.level = levelText;
        l.peak = peakText;
        l.peakDb = peakDb;
        l.balance = kind == Kind::Channel ? panText (pan.getValue()) : Glyph::dash();
        l.integrated = integratedText;
        l.shortTerm = shortTermText;
        l.truePeak = truePeakText;
        l.shortTermLufs = shortTermLufs;
        if (advice.needsAttention())
        {
            l.advice = gainAdviceChip (advice, size == Size::Narrow);
            l.adviceTint = gainAdviceColour (advice.level);
        }
        l.inserts.reserve (insertList.size());
        for (const auto& i : insertList) l.inserts.push_back (i.label);
        l.sends.reserve (sendList.size());
        for (const auto& sv : sendList) l.sends.push_back (sv.label + db1 (sv.db));
        l.mute = mute; l.solo = solo; l.selected = selected; l.bypassed = bypassed;
        return l;
    }

    void showLook (Look next)
    {
        const bool body = next.mute != look.mute || next.solo != look.solo
                       || next.selected != look.selected || next.bypassed != look.bypassed
                       || next.advice != look.advice || next.adviceTint != look.adviceTint
                       || next.inserts != look.inserts || next.sends != look.sends;
        const bool levels = next.level != look.level || next.peak != look.peak
                         || std::fabs (next.peakDb - look.peakDb) > 0.001f;
        const bool balance = next.balance != look.balance;
        const bool loudness = next.integrated != look.integrated || next.shortTerm != look.shortTerm
                           || next.truePeak != look.truePeak
                           || std::fabs (next.shortTermLufs - look.shortTermLufs) > 0.001f;
        look = std::move (next);

        if (body || ! shown) { shown = true; repaint(); return; }

        if (layout == Layout::Column)
        {
            if (levels)                      repaint (col.level);
            if (balance && col.hasPan)       repaint (col.panLabel);
            if (loudness && col.hasLoudness) repaint (col.loudness);
        }
        else
        {
            if (levels)
            {
                repaint (valueRect);
                repaint (meter.getBounds().withY (meter.getBottom() + 1).withHeight (12));
            }
            if (balance && pan.isVisible())
                repaint (pan.getBounds().withY (pan.getBounds().getBottom() - 1).withHeight (11));
        }
    }

    // -------------------------------------------------------------- painting
    // The bank is one console surface, not a row of cards: a column is flat, with its
    // group's colour along the top edge and a hairline down its right, so twenty of them
    // read as one desk. A strip says what state it is in with its own ground - a muted
    // strip goes dark, a soloed one lifts, the picked-out one brightens.
    void paint (juce::Graphics& g) override
    {
        if (layout == Layout::Column) { paintColumnGround (g); paintColumn (g); }
        else                          { paintRowGround (g); paintRow (g); }
    }

    void paintColumnGround (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        // Three planes of black, a few levels apart: the channels, then the groups, then
        // the master. The separation is carried by the hairline between columns and by the
        // group's colour along the top, not by boxing each strip in its own grey.
        juce::Colour ground = kind == Kind::Master ? Dine::raised
                            : kind == Kind::Bus    ? Dine::card
                                                   : Dine::console;
        if (mute)      ground = ground.darker (0.55f);
        else if (solo) ground = ground.overlaidWith (Dine::accent.withAlpha (0.07f));
        if (selected)  ground = ground.brighter (0.09f);
        g.setColour (ground);
        g.fillRect (r);

        g.setColour (busTint (bus).withAlpha (mute ? 0.30f : 0.85f));
        g.fillRect (r.withHeight (2.0f));
        g.setColour (Dine::hair);
        g.fillRect (r.getRight() - 0.5f, 2.0f, 0.5f, r.getHeight() - 2.0f);
    }

    void paintRowGround (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto cardFill = kind == Kind::Master ? Dine::card.brighter (0.04f)
                        : kind == Kind::Bus  ? Dine::card.brighter (0.015f)
                                             : Dine::card;
        if (mute) cardFill = Dine::card.darker (0.28f);
        else if (solo) cardFill = cardFill.overlaidWith (Dine::accent.withAlpha (0.10f));
        if (selected) cardFill = cardFill.brighter (0.07f);
        Dine::drawCard (g, r, cardFill, solo ? Dine::accent.withAlpha (0.5f)
                                             : selected ? Dine::hairStrong : Dine::hair);
        if (selected) Dine::hairlineRounded (g, r.reduced (1.0f), Dine::hair, Dine::Radius::card - 1.0f);
    }

    void paintColumn (juce::Graphics& g)
    {
        const bool narrow = size == Size::Narrow;

        // ---- the number and the name, over the rule that closes the head
        {
            auto head = col.name.reduced (narrow ? 5 : 7, 0);
            if (! narrow && numberText.isNotEmpty())
            {
                g.setColour (selected ? Dine::accent.withAlpha (0.75f) : Dine::ink4);
                g.setFont (Dine::mono (9.5f));
                g.drawText (numberText, head.removeFromLeft (15), juce::Justification::centredLeft);
                head.removeFromLeft (3);
            }
            const bool twoLine = kind == Kind::Master && ! narrow;
            g.setColour (mute ? Dine::keyMute.withAlpha (0.80f) : selected ? Dine::accent : Dine::ink);
            g.setFont (Dine::text (narrow ? 10.5f : 11.5f, kind == Kind::Channel ? 600 : 700));
            g.drawFittedText (name, twoLine ? head.removeFromTop (17) : head,
                              narrow ? juce::Justification::centred : juce::Justification::centredLeft,
                              narrow ? 2 : 1, 0.82f);
            if (twoLine)
            {
                g.setColour (Dine::ink4);
                g.setFont (Dine::text (9.0f, 600).withExtraKerningFactor (0.06f));
                g.drawText (source.toUpperCase(), head, juce::Justification::centredLeft, true);
            }
            auto rule = col.name.withY (col.name.getBottom() - 2).withHeight (2);
            if (selected)
            {
                g.setColour (Dine::accent);
                g.fillRect (rule.reduced (narrow ? 5 : 7, 0));
            }
            else Dine::drawRule (g, rule.withHeight (1).withY (col.name.getBottom() - 1), Dine::hairSoft);
        }

        // ---- what the last listen made of this input's level. The row is always here, so
        //      the sections below it line up across the bank; it only speaks when it must.
        if (col.hasGain && advice.needsAttention())
        {
            const auto colour = gainAdviceColour (advice.level);
            Dine::fillRounded (g, col.gain.toFloat(), colour.withAlpha (0.16f), 3.0f);
            Dine::hairlineRounded (g, col.gain.toFloat(), colour.withAlpha (0.5f), 3.0f);
            g.setColour (colour);
            g.setFont (Dine::text (narrow ? 8.0f : 8.5f, 700).withExtraKerningFactor (0.06f));
            g.drawText (gainAdviceChip (advice, narrow), col.gain, juce::Justification::centred, true);
        }

        // ---- the master's loudness: what the broadcast is actually reading
        if (col.hasLoudness)
        {
            auto block = col.loudness.reduced (10, 0);
            auto top = block.removeFromTop (17);
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (8.5f, 600).withExtraKerningFactor (0.09f));
            g.drawText ("LUFS-I", top.removeFromLeft (44), juce::Justification::centredLeft);
            g.setColour (Dine::ink);
            g.setFont (Dine::mono (14.0f));
            g.drawText (integratedText, top, juce::Justification::centredRight);

            auto bar = block.removeFromTop (4);
            Dine::drawWell (g, bar.toFloat(), 2.0f);
            const float t = juce::jlimit (0.0f, 1.0f, (shortTermLufs + 40.0f) / 40.0f);
            if (t > 0.001f)
            {
                g.setColour (Dine::accent);
                g.fillRoundedRectangle (bar.toFloat().withWidth (juce::jmax (2.0f, float (bar.getWidth()) * t)), 2.0f);
            }
            // The target the planner actually fitted the mix to - the session's own delivery
            // loudness when it has one - so "how close am I" is one glance and never a guess
            // about which standard this session is aiming at.
            const float target = juce::jlimit (0.0f, 1.0f, (targetLufs + 40.0f) / 40.0f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillRect (float (bar.getX()) + float (bar.getWidth()) * target, float (bar.getY()) - 2.0f, 1.0f,
                        float (bar.getHeight()) + 4.0f);

            block.removeFromTop (3);
            auto feet = block.removeFromTop (12);
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (9.0f));
            g.drawText ("SHORT " + shortTermText, feet.removeFromLeft (feet.getWidth() * 3 / 5),
                        juce::Justification::centredLeft, false);
            g.setColour (truePeakOver ? Dine::crit : Dine::ink3);
            g.drawText ("TP " + truePeakText, feet, juce::Justification::centredRight, false);

            // Master gain reduction, under the loudness it paid for.
            auto grRow = block.removeFromTop (12);
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (8.5f, 600).withExtraKerningFactor (0.09f));
            g.drawText ("LIMITER", grRow.removeFromLeft (44), juce::Justification::centredLeft);
            g.setColour (limiterGrDb > 3.0f ? Dine::warn : (limiterGrDb > 0.1f ? Dine::ink2 : Dine::ink4));
            g.setFont (Dine::mono (9.5f));
            g.drawText (grText, grRow, juce::Justification::centredRight, false);
            Dine::drawRule (g, col.loudness.withY (col.loudness.getBottom()).withHeight (1), Dine::hairSoft);
        }

        // ---- INSERTS: the stages that are doing something, in chain order. The slots are
        //      fixed, so an empty one holds its place and the console reads straight across.
        if (col.hasInserts)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (8.5f, 600).withExtraKerningFactor (0.09f));
            g.drawText ("INSERTS", col.insertsLabel.withTrimmedLeft (1), juce::Justification::centredLeft);
            for (size_t i = 0; i < col.insertRows.size(); ++i)
            {
                auto row = col.insertRows[i];
                const bool used = i < insertList.size();
                // A used slot is a lit plane with one small lime lamp on it. Thirteen strips
                // of three inserts is thirty-nine of these, so the colour is spent on the
                // lamp and nothing else - the label stays ink.
                Dine::fillRounded (g, row.toFloat(), used ? juce::Colours::white.withAlpha (mute ? 0.03f : 0.055f)
                                                          : juce::Colours::white.withAlpha (0.02f), 3.0f);
                auto inner = row.reduced (4, 0);
                auto dot = inner.removeFromLeft (4).withSizeKeepingCentre (4, 4);
                g.setColour (used ? Dine::accent.withAlpha (mute ? 0.45f : 1.0f) : juce::Colours::white.withAlpha (0.12f));
                g.fillEllipse (dot.toFloat());
                inner.removeFromLeft (4);
                g.setColour (used ? (mute ? Dine::ink4 : Dine::ink2) : Dine::ink4.withAlpha (0.7f));
                g.setFont (Dine::text (9.5f, used ? 700 : 500).withExtraKerningFactor (0.02f));
                g.drawText (used ? insertList[i].label : Glyph::dash(), inner, juce::Justification::centredLeft, true);
            }
        }

        // ---- SENDS: how much of this goes to each return, in the same fixed slots
        if (col.hasSends)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (8.5f, 600).withExtraKerningFactor (0.09f));
            g.drawText ("SENDS", col.sendsLabel.withTrimmedLeft (1), juce::Justification::centredLeft);
            for (size_t i = 0; i < col.sendRows.size(); ++i)
            {
                auto row = col.sendRows[i];
                const bool used = i < sendList.size();
                g.setColour (used ? Dine::ink3 : Dine::ink4.withAlpha (0.7f));
                g.setFont (Dine::text (9.0f, 600).withExtraKerningFactor (0.05f));
                g.drawText (used ? sendList[i].label : Glyph::dash(), row.removeFromLeft (34),
                            juce::Justification::centredLeft);
                if (! used) continue;
                auto value = row.removeFromRight (30);
                auto bar = row.withTrimmedRight (4).withSizeKeepingCentre (row.getWidth() - 4, 3);
                Dine::drawWell (g, bar.toFloat(), 1.5f);
                const float t = juce::jlimit (0.0f, 1.0f, (sendList[i].db + 40.0f) / 46.0f);
                if (t > 0.001f)
                {
                    g.setColour (Dine::accent.withAlpha (mute ? 0.35f : 0.8f));
                    g.fillRect (bar.toFloat().withWidth (juce::jmax (1.5f, float (bar.getWidth()) * t)));
                }
                g.setColour (Dine::ink3);
                g.setFont (Dine::mono (9.0f));
                g.drawText (db1 (sendList[i].db), value, juce::Justification::centredRight);
            }
        }

        // ---- PAN, over the bar itself
        if (col.hasPan)
        {
            auto label = col.panLabel;
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (8.5f, 600).withExtraKerningFactor (0.09f));
            g.drawText ("PAN", label.removeFromLeft (30), juce::Justification::centredLeft);
            g.setColour (kind != Kind::Channel ? Dine::ink4.withAlpha (0.7f) : mute ? Dine::ink4 : Dine::ink2);
            g.setFont (Dine::mono (9.5f));
            g.drawText (kind == Kind::Channel ? panText (pan.getValue()) : Glyph::dash(), label,
                        juce::Justification::centredRight);
        }

        // ---- the fader well, and the one mark a console is read against: 0 dB, straight
        //      across the fader and the meter, at the same height in every strip.
        {
            const float t = float (fader.valueToProportionOfLength (0.0));
            const float y = float (col.fader.getY()) + float (col.fader.getHeight()) * (1.0f - t);
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.fillRect (float (col.fader.getX()), y - 0.5f,
                        float (col.meter.getRight() - col.fader.getX()), 1.0f);
        }

        // ---- the level, with the peak reading beside it in the level's own colour
        {
            auto row = col.level;
            g.setColour (mute ? Dine::ink4 : Dine::ink);
            g.setFont (Dine::mono (narrow ? 10.0f : 11.0f, 600));
            g.drawText (levelText, narrow ? row : row.removeFromLeft (row.getWidth() * 3 / 5),
                        narrow ? juce::Justification::centred : juce::Justification::centredLeft);
            if (! narrow)
            {
                g.setColour (mute || peakText == Glyph::dash() ? Dine::ink4 : Dine::levelColour (peakDb));
                g.setFont (Dine::mono (9.5f));
                g.drawText (peakText, row, juce::Justification::centredRight);
            }
        }

        // ---- where this strip goes
        if (col.hasOut)
        {
            Dine::fillRounded (g, col.out.toFloat(), juce::Colours::white.withAlpha (0.045f), 3.0f);
            g.setColour (mute ? Dine::ink4 : Dine::ink3);
            g.setFont (Dine::text (9.0f, 600).withExtraKerningFactor (0.06f));
            g.drawText (outText, col.out.reduced (5, 0), juce::Justification::centred, true);
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
        g.setColour (mute ? Dine::keyMute.withAlpha (0.80f) : Dine::ink);
        g.setFont (Dine::text (12.5f, kind == Kind::Channel ? 500 : 600));
        g.drawText (name, line, juce::Justification::centredLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f));
        g.drawText (source, label, juce::Justification::topLeft, true);

        if (advice.needsAttention())
        {
            const auto colour = gainAdviceColour (advice.level);
            auto chip = juce::Rectangle<int> (line.getRight() - 62, line.getCentreY() - 6, 62, 13);
            Dine::fillRounded (g, chip.toFloat(), colour.withAlpha (0.16f), 3.0f);
            Dine::hairlineRounded (g, chip.toFloat(), colour.withAlpha (0.5f), 3.0f);
            g.setColour (colour);
            g.setFont (Dine::text (8.5f, 700).withExtraKerningFactor (0.06f));
            g.drawText (gainAdviceChip (advice, false), chip, juce::Justification::centred, true);
        }

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
    int meterWidth() const noexcept { return size == Size::Narrow ? 9 : size == Size::Wide ? 18 : 14; }
    int nameColumnWidth() const noexcept { return 168; }
    int maxSends() const noexcept { return size == Size::Wide ? 3 : 2; }
    int insertSlots() const noexcept { return size == Size::Narrow ? 0 : size == Size::Wide ? 4 : 3; }

    void resized() override
    {
        if (layout == Layout::Column) layoutColumn();
        else                          layoutRow();
    }

    // One place decides where every section of a column is, so painting and hit-testing can
    // never disagree - and every strip is built on the same grid, with fixed slots for the
    // inserts and the sends, so INSERTS, SENDS, PAN and the faders line up straight across
    // the console even when one source has less to show than its neighbour. A short strip
    // drops whole sections, in a fixed order, rather than squeezing the fader: the throw is
    // the one thing a console cannot afford to lose.
    void buildColumn()
    {
        col = Col {};
        const bool narrow = size == Size::Narrow;
        const int pad = narrow ? 5 : 7;
        auto r = getLocalBounds().withTrimmedTop (4);

        const bool onGrid = kind != Kind::Master && ! narrow;   // channels and buses share one grid
        const int inserts = narrow ? 0 : insertSlots();
        const int sends = (showSends && onGrid) ? maxSends() : 0;

        const int nameH = narrow ? 26 : kind == Kind::Master ? 38 : 28;
        // Gain staging is reserved on every strip that can have it, narrow included: the
        // row holds its place so the sections below line up, and it only speaks when it must.
        const int gainH = kind == Kind::Master ? 0 : narrow ? 14 : 16;
        // The master's loudness block: LUFS-I, the bar against the target, SHORT / TP, and the
        // limiter's own gain reduction underneath - a mix that only reaches its target because
        // the limiter is holding 6 dB down is not finished, it is squashed, and that has to be
        // visible on the console rather than only in the Inspector.
        const int loudH = (kind == Kind::Master && ! narrow) ? 58 : 0;
        const int insertsH = inserts > 0 ? 12 + inserts * 16 + 4 : 0;
        const int sendsH = sends > 0 ? 12 + sends * 13 + 4 : 0;
        const int panH = onGrid ? 24 : 0;
        const int levelH = 16;
        const int keysH = kind == Kind::Master ? 0 : kind == Kind::Channel ? (narrow ? 40 : 22) : 22;
        const int outH = (! narrow && kind != Kind::Master) ? 22 : 0;
        const int bodyMin = 100;

        bool keepSends = sends > 0, keepInserts = inserts > 0, keepOut = outH > 0,
             keepPan = panH > 0, keepGain = gainH > 0;
        int fixed = nameH + gainH + loudH + insertsH + sendsH + panH + levelH + keysH + outH;
        auto shrink = [&] (bool& flag, int h) { if (flag && r.getHeight() - fixed < bodyMin) { flag = false; fixed -= h; } };
        shrink (keepSends, sendsH);
        shrink (keepInserts, insertsH);
        shrink (keepOut, outH);
        shrink (keepPan, panH);
        shrink (keepGain, gainH);

        col.name = r.removeFromTop (nameH);
        // Gain staging comes before everything else in a mix, so an input that still wants a
        // preamp move says so right under its name - never squeezed out by inserts or sends.
        if (keepGain)
        {
            col.gain = r.removeFromTop (gainH).reduced (pad, 0).withTrimmedBottom (3);
            col.hasGain = true;
        }
        if (loudH > 0)
        {
            col.loudness = r.removeFromTop (loudH).withTrimmedTop (6).withTrimmedBottom (5);
            col.hasLoudness = true;
        }
        if (keepInserts)
        {
            auto block = r.removeFromTop (insertsH).reduced (pad - 1, 0).withTrimmedTop (1).withTrimmedBottom (4);
            col.insertsLabel = block.removeFromTop (12);
            for (int i = 0; i < inserts; ++i) col.insertRows.push_back (block.removeFromTop (16).withTrimmedBottom (2));
            col.hasInserts = true;
        }
        if (keepSends)
        {
            auto block = r.removeFromTop (sendsH).reduced (pad - 1, 0).withTrimmedTop (1).withTrimmedBottom (4);
            col.sendsLabel = block.removeFromTop (12);
            for (int i = 0; i < sends; ++i) col.sendRows.push_back (block.removeFromTop (13));
            col.hasSends = true;
        }
        if (keepPan)
        {
            // Pan sits above the fader, the way a console column reads: what is set,
            // then how much of it you hear.
            auto block = r.removeFromTop (panH).reduced (pad, 0).withTrimmedBottom (4);
            col.panLabel = block.removeFromTop (12);
            col.panBar = block.removeFromTop (8);
            col.hasPan = true;
        }

        if (keepOut)
        {
            col.out = r.removeFromBottom (outH).reduced (pad, 0).withTrimmedTop (3).withTrimmedBottom (6);
            col.hasOut = true;
        }
        if (keysH > 0) col.keys = r.removeFromBottom (keysH).reduced (pad, 0).withTrimmedBottom (4);
        col.level = r.removeFromBottom (levelH).reduced (pad, 0);

        // The body is the strip: the fader takes what is left, the meter stands beside it.
        auto body = r.reduced (pad, 0).withTrimmedTop (8).withTrimmedBottom (6);
        col.meter = body.removeFromRight (meterWidth());
        body.removeFromRight (narrow ? 3 : 5);
        col.fader = body;
    }

    void layoutColumn()
    {
        buildColumn();
        meter.setBounds (col.meter);
        fader.setBounds (col.fader);
        pan.setVisible (col.hasPan && kind == Kind::Channel);
        if (pan.isVisible()) pan.setBounds (col.panBar);

        if (col.keys.isEmpty())
        {
            muteButton.setVisible (false);
            soloButton.setVisible (false);
            armButton.setVisible (false);
            monitorButton.setVisible (false);
            return;
        }

        muteButton.setVisible (kind != Kind::Master);
        soloButton.setVisible (kind != Kind::Master);
        armButton.setVisible (kind == Kind::Channel);
        monitorButton.setVisible (kind == Kind::Channel);

        auto keys = col.keys;
        const int gap = 3;
        auto share = [gap] (juce::Rectangle<int>& row, int of, int index)
        {
            const int w = juce::jmax (14, (row.getWidth() - gap * (of - 1)) / of);
            auto cell = row.removeFromLeft (index == of - 1 ? row.getWidth() : w);
            row.removeFromLeft (gap);
            return cell;
        };

        if (kind != Kind::Channel)
        {
            auto row = keys.withHeight (juce::jmin (20, keys.getHeight()));
            muteButton.setBounds (share (row, 2, 0));
            soloButton.setBounds (share (row, 2, 1));
            return;
        }

        if (size == Size::Narrow)
        {
            auto top = keys.removeFromTop (18);
            keys.removeFromTop (4);
            auto bottom = keys.removeFromTop (18);
            muteButton.setBounds (share (top, 2, 0));
            soloButton.setBounds (share (top, 2, 1));
            armButton.setBounds (share (bottom, 2, 0));
            monitorButton.setBounds (share (bottom, 2, 1));
            return;
        }

        auto row = keys.withHeight (juce::jmin (20, keys.getHeight()));
        muteButton.setBounds (share (row, 4, 0));
        soloButton.setBounds (share (row, 4, 1));
        armButton.setBounds (share (row, 4, 2));
        monitorButton.setBounds (share (row, 4, 3));
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
        meter.setBounds (meterArea.withSizeKeepingCentre (meterArea.getWidth(), 6).translated (0, 3));
        r.removeFromLeft (16);
        fader.setBounds (r.withSizeKeepingCentre (juce::jmax (80, r.getWidth()), 22));
    }

    // A click picks the strip out - the foot of the page then reads its chain; a double-click
    // opens it in the Inspector, the same as double-clicking a track header on the timeline.
    // Right-click is where one channel's own actions live: TUNE CHANNEL first, because
    // tuning a source is what you come to a strip to do.
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this || e.mouseWasDraggedSinceMouseDown()) return;
        if (e.mods.isPopupMenu()) { if (select) select(); showMenu(); return; }
        if (e.getNumberOfClicks() >= 2) { if (open) open(); }
        else if (select) select();
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (numberText.isEmpty() ? name : numberText + "  " + name);
        m.addItem (1, "TUNE CHANNEL", tune != nullptr);
        m.addSeparator();
        m.addItem (2, "Open in the Inspector", open != nullptr);
        juce::Component::SafePointer<Strip> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (200),
                         [safe] (int chosen)
                         {
                             if (safe == nullptr || chosen <= 0) return;
                             if (chosen == 1) { if (safe->tune) safe->tune(); }
                             else if (safe->open) safe->open();
                         });
    }

    // Where every section of the column sits, built once per resize.
    struct Col
    {
        juce::Rectangle<int> name, gain, loudness, insertsLabel, sendsLabel, panLabel, panBar,
                             fader, meter, level, keys, out;
        std::vector<juce::Rectangle<int>> insertRows, sendRows;
        bool hasLoudness = false, hasInserts = false, hasSends = false, hasPan = false,
             hasOut = false, hasGain = false;
    };
    struct SendView { juce::String label; float db = kSilenceDb; };

    MixController& controller;
    AppServices& services;
    Kind kind;
    MixBus bus;
    ChannelRole role;
    int strip = -1;
    juce::String name, source, levelText { "+0.0" }, peakText, numberText, outText;
    juce::String integratedText { Glyph::dash() }, shortTermText { Glyph::dash() }, truePeakText { Glyph::dash() };
    // What the master's loudness is being read against, and what it cost: the target this
    // session aims at, the limiter's current reduction, and whether the true peak is over.
    juce::String grText { Glyph::dash() };
    float limiterGrDb = 0.0f, targetLufs = -23.0f;
    bool truePeakOver = false;
    float peakDb = -120.0f, shortTermLufs = -70.0f;
    juce::Rectangle<int> valueRect;
    Col col;
    std::vector<ChainStage> insertList;
    std::vector<SendView> sendList;
    MixController::InputAdvice advice;      // what the last listen said about this input's level
    Look look;                              // what is on screen, so only what moved is redrawn
    bool shown = false;                     // has this strip been drawn at least once in this layout
    bool stereo = false, showSends = true, needsLayout = false;
    int lastSendCount = 0;
    bool mute = false, solo = false, armed = false, bypassed = false, updating = false, selected = false;
    Layout layout = Layout::Column;
    Size size = Size::Normal;
    Dine::Icon icon;
    DineMeter meter;
    juce::Slider fader;
    PanBar pan;
    DineKey muteButton, soloButton, armButton, monitorButton;
    std::function<void()> open, select, tune;
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
            else                 // strips view: the family's colour, drawn over its columns
            {
                Dine::fillRounded (g, b.bounds.toFloat(), b.tint.withAlpha (0.75f), 2.0f);
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
    Dine::nativeScrolling (viewport);      // a swipe crosses the console at the speed of the fingers
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

    sendsButton.setFontPx (11.5f);
    sendsButton.setPadX (9);
    sendsButton.setTooltip ("Show how much of each source goes to the reverbs and delays.");
    sendsButton.onClick = [this] { setSendsVisible (! showSends); };
    addAndMakeVisible (sendsButton);

    chainStrip.setEmpty ("Click a strip to read its chain here. Double-click it to open the Inspector.");
    chainStrip.onOpen = [this]
    {
        if (selected >= 0) { if (onOpenStrip) onOpenStrip (selected); }
        else if (selectedBus != MixBus::Count) { if (onOpenBus) onOpenBus (selectedBus); }
    };
    addAndMakeVisible (chainStrip);

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

void MixerPage::setSendsVisible (bool v)
{
    if (showSends == v) return;
    showSends = v;
    for (auto& st : strips) st->setSendsVisible (v);
    updateControls();
}

void MixerPage::selectStrip (int strip)
{
    selected = strip;
    selectedBus = MixBus::Count;
    updateChainStrip();
}

void MixerPage::selectBus (MixBus b)
{
    selected = -1;
    selectedBus = b;
    updateChainStrip();
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
    removeChildComponent (masterStrip());
    strips.clear();
    bank->removeAllChildren();

    const auto& graph = controller.getGraph();

    auto add = [&] (std::unique_ptr<Strip> s)
    {
        s->setSendsVisible (showSends);
        s->setLayout (view == View::Strips ? Strip::Layout::Column : Strip::Layout::Row, stripSize);
        bank->addAndMakeVisible (*s);
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
                                              juce::String (r.name), source, r.icon);
            s->setOpenHandler ([this, i] { if (onOpenStrip) onOpenStrip (i); });
            s->setSelectHandler ([this, i] { selectStrip (i); });
            s->setTuneHandler ([this, i] { if (onTuneStrip) onTuneStrip (i); });
            add (std::move (s));
        }

        if (controller.isPrepared() && controller.getEngine().isBusUsed (bus))
        {
            auto s = std::make_unique<Strip> (controller, services, Strip::Kind::Bus, bus, ChannelRole::KickIn, -1,
                                              sentenceCase (mixBusName (bus)),
                                              juce::String (graph.stripsOnBus (bus))
                                                  + (graph.stripsOnBus (bus) == 1 ? " source" : " sources"));
            s->setOpenHandler ([this, bus] { if (onOpenBus) onOpenBus (bus); });
            s->setSelectHandler ([this, bus] { selectBus (bus); });
            add (std::move (s));
        }
    }

    if (controller.isPrepared() && controller.getEngine().isBusUsed (MixBus::Master))
    {
        // The master says where it actually leaves the Mac, so the pinned column is never a guess.
        const auto& main = controller.getOutputFeeds().feeds[0];
        const juce::String out = main.routed()
            ? juce::String (main.mono ? "Mono out " : "Stereo out ") + juce::String (main.left + 1)
                  + (main.mono || main.right < 0 ? juce::String() : "-" + juce::String (main.right + 1))
            : juce::String ("Not routed");
        auto s = std::make_unique<Strip> (controller, services, Strip::Kind::Master, MixBus::Master, ChannelRole::KickIn,
                                          -1, "Master", out);
        s->setOpenHandler ([this] { if (onOpenBus) onOpenBus (MixBus::Master); });
        s->setSelectHandler ([this] { selectBus (MixBus::Master); });
        add (std::move (s));
    }

    builtForStrips = graph.numStrips();
    if (selected >= graph.numStrips()) { selected = -1; selectedBus = MixBus::Count; }
    updateChainStrip();
    updateControls();
    resized();
}

void MixerPage::refresh()
{
    if (! controller.isPrepared()) return;
    if (controller.getGraph().numStrips() != builtForStrips) { rebuild(); return; }
    for (auto& s : strips) s->refresh();
    updateChainStrip();
    updateControls();
}

// The foot of the page: the picked-out strip's chain, stage by stage, and the session's clock.
void MixerPage::updateChainStrip()
{
    if (! controller.isPrepared())
    {
        chainStrip.setEmpty ("Set the device up first.");
        return;
    }

    const auto& state = controller.getBase();
    if (selected >= 0 && selected < state.numStrips)
    {
        const auto& r = controller.getGraph().strips[size_t (selected)];
        chainStrip.setSource (juce::String (r.name), busTint (r.bus), state.strips[size_t (selected)].channel,
                              false, r.inputB >= 0);
    }
    else if (selectedBus != MixBus::Count)
    {
        chainStrip.setSource (sentenceCase (mixBusName (selectedBus)), busTint (selectedBus),
                              state.buses[size_t (selectedBus)].channel, selectedBus == MixBus::Master, true);
    }
    else
    {
        chainStrip.setEmpty ("Click a strip to read its chain here. Double-click it to open the Inspector.");
    }

    for (int i = 0; i < int (strips.size()); ++i)
        strips[size_t (i)]->setSelected (selected >= 0 ? strips[size_t (i)]->getStripIndex() == selected
                                                       : selectedBus != MixBus::Count
                                                         && strips[size_t (i)]->getKind() != Strip::Kind::Channel
                                                         && strips[size_t (i)]->getBus() == selectedBus);

    // The right-hand note is what the broadcast is reading, so the console has the number
    // the planner fits the mix to in front of it at all times.
    juce::String note;
    if (controller.isBypassed()) note = "BYPASS";
    else if (controller.getEngine().isBusUsed (MixBus::Master))
    {
        const float lufs = controller.getEngine().getBus (MixBus::Master).getLoudness().getShortTermLufs();
        note = "LUFS " + (lufs <= -60.0f ? Glyph::dash() : db1 (lufs));
    }
    chainStrip.setNote (note);
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
    // A view toggle is a selection, never a primary action: it lights as a chosen segment
    // rather than filling with the lime, which belongs to what the mix is doing.
    sendsButton.setStyle (showSends ? DineButton::Style::Segment : DineButton::Style::Standard);
    sendsButton.setToggleState (showSends, juce::dontSendNotification);
    if (sendsButton.isVisible() != (view == View::Strips))
    {
        sendsButton.setVisible (view == View::Strips);
        resized();
    }
}

// -------------------------------------------------------------------- paint
void MixerPage::paint (juce::Graphics& g)
{
    // The sub-toolbar: what this is and how much of it there is, then the controls.
    auto head = getLocalBounds().removeFromTop (kHeaderH);
    g.setColour (Dine::toolbar);
    g.fillRect (head);
    Dine::drawRule (g, head.removeFromBottom (1), Dine::hairSoft);

    // The controls own the right of the sub-toolbar. What is written to their left takes only
    // the room they leave it and gives way in order - the hint first, then the count and the
    // rule that closes it, then the name - so nothing is ever drawn under a button, at any
    // width the console can be opened at (its own window goes down to 720).
    auto row = head.reduced (kPadX, 0);
    if (viewTabs[0] != nullptr)
        row = row.withRight (juce::jmin (row.getRight(), viewTabs[0]->getX() - 16));

    const auto titleFont = Dine::text (11.0f, 700).withExtraKerningFactor (0.08f);
    if (row.getWidth() >= 48)
    {
        g.setColour (Dine::ink3);
        g.setFont (titleFont);
        g.drawText ("MIXER", row.removeFromLeft (48), juce::Justification::centredLeft);
    }

    if (controller.isPrepared())
    {
        const int sources = controller.getGraph().numStrips();
        int groups = 0;
        for (int b = 0; b < int (MixBus::Master); ++b)
            if (controller.getEngine().isBusUsed (MixBus (b))) ++groups;
        juce::String counts = juce::String (sources) + " CH  " + Glyph::dot() + "  " + juce::String (groups)
                              + " BUS  " + Glyph::dot() + "  1 MASTER";
        const auto countFont = Dine::mono (11.0f);
        const int countsW = Dine::textWidth (countFont, counts);
        if (row.getWidth() >= countsW + 20)          // the count, then the rule that closes it
        {
            g.setColour (Dine::ink3);
            g.setFont (countFont);
            g.drawText (counts, row.removeFromLeft (countsW), juce::Justification::centredLeft);
            row.removeFromLeft (10);
            g.setColour (Dine::hair);
            g.fillRect (float (row.getX()), float (row.getCentreY()) - 9.0f, 0.5f, 18.0f);
            row.removeFromLeft (10);
        }
    }

    const juce::String hint = controller.isBypassed()
        ? "BYPASS is on " + Glyph::dash() + " you are hearing the inputs as they arrive, not the mix."
        : "Drag a fader or a balance to set it. Click a strip to read its chain, double-click to open it.";
    const auto hintFont = Dine::text (11.5f);
    if (Dine::textWidth (hintFont, hint) <= row.getWidth())
    {
        g.setColour (controller.isBypassed() ? Dine::warn : Dine::ink4);
        g.setFont (hintFont);
        g.drawText (hint, row, juce::Justification::centredLeft);
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

    if (auto* master = masterStrip(); master != nullptr && master->getParentComponent() == this && master->isVisible())
    {
        auto edge = master->getBounds().withWidth (18).translated (-18, 0).toFloat();
        juce::ColourGradient shade (juce::Colours::black.withAlpha (0.0f), edge.getX(), edge.getY(),
                                    juce::Colours::black.withAlpha (0.34f), edge.getRight(), edge.getY(), false);
        g.setGradientFill (shade);
        g.fillRect (edge);

        // the pinned column keeps its bar, so it reads as part of the same console
        auto band = master->getBounds().withHeight (kBandH).translated (0, -(kBandH + kBandGap));
        Dine::fillRounded (g, band.toFloat(), busTint (MixBus::Master).withAlpha (0.75f), 2.0f);
    }

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
    auto controls = head.withSizeKeepingCentre (head.getWidth(), Dine::Metric::control);

    if (windowButton.isVisible())
    {
        const int w = juce::jmax (120, windowButton.idealWidth());
        windowButton.setBounds (controls.removeFromRight (w));
        controls.removeFromRight (8);
    }
    if (clearSolos.isVisible())
    {
        const int w = juce::jmax (90, clearSolos.idealWidth());
        clearSolos.setBounds (controls.removeFromRight (w));
        controls.removeFromRight (8);
    }
    if (sendsButton.isVisible())
    {
        sendsButton.setBounds (controls.removeFromRight (juce::jmax (58, sendsButton.idealWidth())));
        controls.removeFromRight (10);
    }
    {
        int widths[3], total = 0;
        for (int i = 0; i < 3; ++i) { widths[i] = juce::jmax (52, showTabs[size_t (i)]->idealWidth() + 8); total += widths[i]; }
        auto seg = controls.removeFromRight (total);
        for (int i = 0; i < 3; ++i) showTabs[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
        controls.removeFromRight (10);
    }
    if (view == View::Strips)
    {
        auto seg = controls.removeFromRight (90);
        for (int i = 0; i < 3; ++i) sizeTabs[size_t (i)]->setBounds (seg.removeFromLeft (30));
        controls.removeFromRight (10);
    }
    {
        int widths[2], total = 0;
        for (int i = 0; i < 2; ++i) { widths[i] = juce::jmax (58, viewTabs[size_t (i)]->idealWidth() + 8); total += widths[i]; }
        auto seg = controls.removeFromRight (total);
        for (int i = 0; i < 2; ++i) viewTabs[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
    }

    chainStrip.setBounds (getLocalBounds().removeFromBottom (ChainStrip::height));

    if (view == View::Strips) layoutStrips();
    else                      layoutList();

    // What this page paints itself - the sub-toolbar's name, count and hint, the quiet
    // tracks under the segments, the shade beside the pinned master - is all positioned
    // from where those controls ended up. Moving a child only invalidates the child's own
    // old and new bounds, so switching LIST to STRIPS (which slides the segments 158 px to
    // the left) would otherwise leave the hint drawn for the old layout standing, with the
    // buttons landing on top of it. The page is laid out rarely; redraw it whole.
    repaint();
}

// The master stands still while the bank scrolls: it is the one strip you always want in
// front of you, so it sits beside the scrolling area rather than inside it.
MixerPage::Strip* MixerPage::masterStrip() const
{
    for (auto& s : strips)
        if (s->getKind() == Strip::Kind::Master) return s.get();
    return nullptr;
}

void MixerPage::layoutStrips()
{
    auto area = getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedBottom (ChainStrip::height).reduced (kPadX, kPadY);
    const int top = kBandH + kBandGap;
    const int h = juce::jmax (180, area.getHeight());
    const int stripH = juce::jmin (h - top, kMaxStripH);

    auto* master = masterStrip();
    if (master != nullptr && visibleInFilter (*master))
    {
        if (master->getParentComponent() != this) addAndMakeVisible (*master);
        master->setVisible (true);
        auto column = area.removeFromRight (master->columnWidth());
        master->setBounds (column.withTop (area.getY() + top).withHeight (stripH));
        area.removeFromRight (kGroupGap);
    }
    else if (master != nullptr)
    {
        master->setVisible (false);
    }
    viewport.setBounds (area);

    std::vector<Bank::Band> bands;
    int x = 0;
    for (int b = 0; b <= int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        const int groupStart = x;
        bool any = false;
        for (auto& s : strips)
        {
            if (s->getBus() != bus || s.get() == master) continue;
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
    if (auto* master = masterStrip(); master != nullptr && master->getParentComponent() != bank.get())
        bank->addAndMakeVisible (*master);
    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderH)
                            .withTrimmedBottom (ChainStrip::height).reduced (kPadX, kPadY));

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
