#include "MixerPage.h"
#include "Core/DbUtils.h"
#include "DSP/ChannelParameters.h"
#include "UI/Widgets.h"
#include <cmath>
#include <cstring>
#include <type_traits>

namespace livemix
{

namespace
{
    constexpr int kHeaderH   = Dine::Metric::header;     // the tool row
    constexpr int kPadX      = 18;
    constexpr int kStripGap  = 3;       // between columns
    constexpr int kRowH      = 46;      // list view
    constexpr int kRowGap    = 3;
    constexpr int kListHeadH = 30;
    constexpr int kMasterW   = 150;     // the pinned master column
    constexpr int kMaxStripH = 900;

    int columnWidthFor (MixerPage::Size s) noexcept
    {
        return s == MixerPage::Size::Narrow ? 62 : s == MixerPage::Size::Wide ? 124 : 96;
    }

    // Gain staging, as a chip on the strip: the colour says how bad it is, the word says what.
    juce::Colour gainAdviceColour (MixController::InputAdvice::Level level) noexcept
    {
        using Level = MixController::InputAdvice::Level;
        switch (level)
        {
            case Level::Clipping: return Dine::crit;
            case Level::Faint:
            case Level::Low:
            case Level::Digital:  return Dine::warn;
            case Level::Hot:      return Dine::hot;
            case Level::NotHeard: return Dine::ink3;
            default:              return Dine::ok;
        }
    }

    juce::String gainAdviceChip (const MixController::InputAdvice& a)
    {
        using Level = MixController::InputAdvice::Level;
        switch (a.level)
        {
            case Level::Clipping: return "CLIPPING";
            case Level::Faint:    return "FAINT";
            case Level::Low:      return "LOW";
            case Level::Hot:      return "HOT";
            case Level::Digital:  return "DIGITAL";
            case Level::NotHeard: return "NOT HEARD";
            case Level::Healthy:  return "HEALTHY";
            case Level::Bleed:    return "SPILL";
            default:              return {};
        }
    }

    const char* fxName (FxSlot s) noexcept
    {
        switch (s)
        {
            case FxSlot::VocalPlate:  return "Plate";
            case FxSlot::VocalDelay:  return "Delay";
            case FxSlot::BgvHall:     return "Hall";
            case FxSlot::SnarePlate:  return "Snare";
            case FxSlot::DrumRoom:    return "Room";
            case FxSlot::Count:       break;
        }
        return "FX";
    }

    juce::String db1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::String sentenceCase (const juce::String& s)
    {
        return s.substring (0, 1).toUpperCase() + s.substring (1).toLowerCase();
    }

    juce::String panText (float pan)
    {
        if (std::fabs (pan) < 0.005f) return "C";
        return (pan < 0.0f ? "L" : "R") + juce::String (int (std::round (std::fabs (pan) * 100.0f)));
    }

    // A send in per cent of its throw, the way the design labels one: "Plate 22%".
    int sendPercent (float db) noexcept
    {
        return juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 46.0f) * 100.0f);
    }

    // A cheap fingerprint of the chain, so the inserts are only re-read when it changed.
    juce::uint32 chainHash (const ChannelParameters& p)
    {
        juce::uint32 h = 2166136261u;
        auto mixIn = [&h] (juce::uint32 v) { h ^= v; h *= 16777619u; };
        forEachDspField (const_cast<ChannelParameters&> (p), [&] (auto, auto& v)
        {
            using T = std::decay_t<decltype (v)>;
            if constexpr (std::is_same_v<T, float>) { float f = v; juce::uint32 bits; std::memcpy (&bits, &f, 4); mixIn (bits); }
            else mixIn (juce::uint32 (v));
        });
        return h;
    }

    // The shared geometry of the slot chips: 10 px type, 3 px of padding, 6 px corners.
    constexpr int kSlotH = 22;
}

// ------------------------------------------------------------------ Strip
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
          meter (DineMeter::Style::Bar),
          muteButton ("M", Dine::keyMute),
          soloButton ("S", Dine::keySolo),
          armButton ("R", Dine::keyRec),
          monitorButton ("A", Dine::keyMon)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        Dine::dragOnly (fader);
        fader.setRange (-60.0, 12.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
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
            repaint (layout == Layout::Column ? col.level : valueRect);
        };

        pan.setTooltip ("Balance " + Glyph::dot() + " drag left or right " + Glyph::dot() + " centre is C");
        pan.onChange = [this] (float v)
        {
            if (updating || kind != Kind::Channel) return;
            controller.setStripPan (strip, v);
            repaint (layout == Layout::Column ? col.panRead : panReadRect);
        };
        pan.setVisible (kind == Kind::Channel);

        muteButton.setVisible (kind != Kind::Master);
        soloButton.setVisible (kind != Kind::Master);
        armButton.setVisible (kind == Kind::Channel);
        monitorButton.setVisible (kind == Kind::Channel);
        muteButton.setTooltip (kind == Kind::Bus ? "Muted: the whole group is not heard" : "Muted: signal arrives, it is not heard");
        soloButton.setTooltip (kind == Kind::Bus ? "Soloed: this group and nothing else" : "Soloed: this and nothing else");
        armButton.setTooltip ("Set to record (the engineer's word is arm)");
        monitorButton.setTooltip ("Monitoring " + Glyph::dot() + " click to cycle: input (you hear the live input), auto (input while recording), off (the timeline only)");

        armButton.onClick = [this]
        {
            auto& project = services.daw().getProject();
            if (strip < 0 || strip >= int (project.tracks.size())) return;
            project.tracks[size_t (strip)].armed = ! project.tracks[size_t (strip)].armed;
            services.daw().refresh();
            services.saveSession();
            refresh (true);
        };
        monitorButton.onClick = [this]
        {
            auto& project = services.daw().getProject();
            if (strip < 0 || strip >= int (project.tracks.size())) return;
            auto& mode = project.tracks[size_t (strip)].monitor;
            mode = MonitorMode ((int (mode) + 1) % int (MonitorMode::Count));
            services.daw().refresh();
            services.saveSession();
            refresh (true);
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

        numberText = kind == Kind::Channel ? juce::String (stripIndex + 1).paddedLeft ('0', 2) : kind == Kind::Bus ? "BUS" : juce::String();
        outText = kind == Kind::Channel ? juce::String (mixBusName (bus)).toUpperCase() : kind == Kind::Bus ? "MASTER" : juce::String();
        stereo = true;
        if (kind == Kind::Channel && stripIndex >= 0 && stripIndex < controller.getGraph().numStrips())
            stereo = controller.getGraph().strips[size_t (stripIndex)].inputB >= 0;

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
    void setAssignHandler (std::function<void()> h) { assign = std::move (h); }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    bool isSelected() const noexcept { return selected; }

    Kind getKind() const noexcept { return kind; }
    MixBus getBus() const noexcept { return bus; }
    int getStripIndex() const noexcept { return strip; }

    void setLayout (Layout l, Size s)
    {
        layout = l;
        size = s;
        setOpaque (l == Layout::Column);
        setBufferedToImage (true);
        fader.setSliderStyle (l == Layout::Column ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
        pan.setVisible (kind == Kind::Channel);
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
        return kind == Kind::Master ? kMasterW : columnWidthFor (size);
    }

    // The 30 Hz tick. `adviceDue` is true twice a second (and the tick a tune lands): the
    // gain advice builds sentences and only changes when a plan does.
    void refresh (bool adviceDue)
    {
        if (! controller.isPrepared()) return;
        const auto& state = controller.getBase();
        updating = true;

        float faderDb = 0.0f, panValue = 0.0f;
        bool muted = false, soloed = false;
        float peak = -120.0f, hold = -120.0f;
        bool clipped = false;
        const ChannelParameters* channel = nullptr;

        if (kind == Kind::Channel && strip >= 0 && strip < state.numStrips)
        {
            const auto& st = state.strips[size_t (strip)];
            faderDb = st.faderDb;
            panValue = st.pan;
            muted = st.mute;
            soloed = st.solo;
            channel = &st.channel;
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
            channel = &state.buses[size_t (bus)].channel;
            const auto& m = controller.getEngine().getBus (bus).getOutputMeter();
            peak = m.consumeMaxPeakDb();
            hold = m.getMaxRmsDb();
            clipped = m.hasClipped();
        }

        // ---- every frame: the atomics
        bool body = false, levels = false, balance = false;
        if (std::fabs (faderDb - shownFaderDb) > 0.01f)
        {
            shownFaderDb = faderDb;
            fader.setValue (faderDb, juce::dontSendNotification);
            levelText = db1 (faderDb);
            levels = true;
        }
        if (std::fabs (panValue - pan.getValue()) > 0.001f) { pan.setValue (panValue); balance = true; }
        meter.setLevels (peak, hold, clipped);
        meter.setMuted (muted);
        const float shownPeak = meter.getPeakDb();
        if (std::fabs (shownPeak - peakDb) > 0.15f)
        {
            peakDb = shownPeak;
            peakText = peakDb <= -60.0f ? Glyph::dash() : juce::String (peakDb, 1);
            levels = true;
        }
        if (muted != mute || soloed != solo) { mute = muted; solo = soloed; body = true; }
        muteButton.setOn (mute);
        soloButton.setOn (solo);
        if (bypassed != controller.isBypassed())
        {
            bypassed = controller.isBypassed();
            fader.setEnabled (! bypassed);
            pan.setEnabled (! bypassed);
            body = true;
        }
        if (kind == Kind::Channel)
        {
            const auto& project = services.daw().getProject();
            const bool isArmed = strip >= 0 && strip < int (project.tracks.size()) && project.tracks[size_t (strip)].armed;
            const auto mode = strip >= 0 && strip < int (project.tracks.size()) ? project.tracks[size_t (strip)].monitor
                                                                                : MonitorMode::Auto;
            armButton.setOn (isArmed);
            monitorButton.setLetter (mode == MonitorMode::Input ? "I" : "A");
            monitorButton.setOn (mode != MonitorMode::Off);
        }

        // ---- twice a second: the sentences
        if (kind == Kind::Channel && adviceDue)
        {
            const auto next = controller.getInputAdvice (strip);
            if (next.level != advice.level || next.known != advice.known) body = true;
            advice = next;
        }

        // ---- when the chain changed: the inserts and the sends
        if (channel != nullptr)
        {
            const auto h = chainHash (*channel);
            if (h != chainFingerprint)
            {
                chainFingerprint = h;
                insertList = activeChainStages (*channel, kind == Kind::Master, stereo);
                body = true;
            }
        }
        if (kind == Kind::Channel && strip >= 0 && strip < state.numStrips)
        {
            const auto& sends = state.strips[size_t (strip)].sendDb;
            if (sends != shownSends)
            {
                shownSends = sends;
                sendList.clear();
                const auto& graph = controller.getGraph();
                for (int f = 0; f < int (FxSlot::Count) && int (sendList.size()) < 2; ++f)
                    if (graph.fxUsed[size_t (f)] && sends[size_t (f)] > kSilenceDb)
                        sendList.push_back ({ juce::String (fxName (FxSlot (f))), sends[size_t (f)] });
                body = true;
            }
        }

        if (kind == Kind::Master)
        {
            const auto m = controller.getMasterLoudness();
            juce::String i = m.integratedLufs <= -60.0f ? Glyph::dash() : juce::String (m.integratedLufs, 1);
            juce::String st = m.shortTermLufs <= -60.0f ? Glyph::dash() : juce::String (m.shortTermLufs, 1);
            juce::String tp = m.truePeakDb <= -60.0f ? Glyph::dash() : juce::String (m.truePeakDb, 1);
            juce::String gr = m.limiterReductionDb < 0.1f ? Glyph::dash() : juce::String (-m.limiterReductionDb, 1);
            juce::String target = juce::String (m.targetLufs, 1);
            if (i != integratedText || st != shortTermText || tp != truePeakText || gr != grText || target != targetText)
            {
                integratedText = i; shortTermText = st; truePeakText = tp; grText = gr; targetText = target;
                truePeakOver = m.truePeakDb > m.ceilingDb + 0.1f;
                limiterHot = m.limiterReductionDb > 3.0f;
                repaint (col.loudness);
            }
        }

        updating = false;
        if (body) { repaint(); return; }
        if (layout == Layout::Column)
        {
            if (levels) repaint (col.level);
            if (balance && col.hasPan) repaint (col.panRead);
        }
        else
        {
            if (levels) repaint (valueRect);
            if (balance) repaint (panReadRect);
        }
    }

    // -------------------------------------------------------------- painting
    void paint (juce::Graphics& g) override
    {
        if (layout == Layout::Column) paintColumn (g);
        else                          paintRow (g);
    }

    juce::Colour tint() const noexcept { return kind == Kind::Master ? Dine::ink2 : Dine::busTint (bus); }

    void paintColumn (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        g.setColour (selected ? Dine::card : Dine::console);
        g.fillRect (r);
        if (kind != Kind::Master)
        {
            g.setColour (tint().withAlpha (mute ? 0.4f : 1.0f));
            g.fillRect (r.removeFromTop (selected ? 5 : 3));
        }

        // ---- the number and the name
        if (kind == Kind::Master)
        {
            g.setColour (Dine::ink2);
            g.setFont (Dine::caps (12.0f, 0.08f, 500));
            g.drawText ("MASTER", col.name, juce::Justification::centred);
        }
        else
        {
            const auto numFont = Dine::mono (10.0f, 500);
            const auto nameFont = Dine::text (12.0f, selected ? 600 : 400);
            const int numW = Dine::textWidth (numFont, numberText);
            const int nameW = juce::jmin (col.name.getWidth() - numW - 5, Dine::textWidth (nameFont, name));
            auto head = col.name.withSizeKeepingCentre (numW + 5 + nameW, col.name.getHeight());
            g.setColour (Dine::ink4);
            g.setFont (numFont);
            g.drawText (numberText, head.removeFromLeft (numW), juce::Justification::centredLeft);
            head.removeFromLeft (5);
            g.setColour (mute ? Dine::ink3 : Dine::ink);
            g.setFont (nameFont);
            g.drawText (name, head, juce::Justification::centredLeft, true);
            if (mute)
            {
                g.setColour (Dine::ink3);
                g.fillRect (head.getX(), head.getCentreY(), head.getWidth(), 1);
            }
        }

        // ---- gain staging: every strip that can have it, always in its slot
        if (col.hasGain)
        {
            const juce::String label = advice.known ? gainAdviceChip (advice) : juce::String();
            if (label.isNotEmpty())
                Dine::drawStatusChip (g, col.gain.toFloat(), label, gainAdviceColour (advice.level));
        }

        // ---- the master's loudness
        if (col.hasLoudness)
        {
            auto block = col.loudness;
            auto row = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
            {
                auto line = block.removeFromTop (18);
                g.setColour (Dine::ink3);
                g.setFont (Dine::mono (11.0f, 500));
                g.drawText (k, line, juce::Justification::centredLeft);
                g.setColour (c);
                g.drawText (v, line, juce::Justification::centredRight);
            };
            row ("LUFS-I", integratedText, Dine::ink);
            row ("Short", shortTermText, Dine::ink);
            row ("True pk", truePeakText, truePeakOver ? Dine::crit : Dine::ink);
            row ("Limiter", grText, limiterHot ? Dine::warn : Dine::ink);
            row ("Target", targetText, Dine::ink);
        }

        // ---- INSERTS: the stages that are on, in fixed slots
        auto caption = [&] (juce::Rectangle<int> area, const char* text)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::caps (9.0f, 0.10f));
            g.drawText (text, area, juce::Justification::centredLeft);
        };
        auto slot = [&] (juce::Rectangle<int> area, const juce::String& text, bool used, bool left)
        {
            if (used) Dine::fillRounded (g, area.toFloat(), Dine::selected, Dine::Radius::chip);
            g.setColour (used ? Dine::ink2 : Dine::ink4);
            g.setFont (Dine::text (10.0f));
            g.drawText (used ? text : Glyph::dash(), area.reduced (5, 0),
                        used && left ? juce::Justification::centredLeft : juce::Justification::centred, true);
        };
        if (col.hasInserts)
        {
            caption (col.insertsLabel, "INSERTS");
            for (size_t i = 0; i < col.insertRows.size(); ++i)
                slot (col.insertRows[i], i < insertList.size() ? sentenceCase (insertList[i].label) : juce::String(),
                      i < insertList.size(), true);
        }
        if (col.hasSends)
        {
            caption (col.sendsLabel, "SENDS");
            for (size_t i = 0; i < col.sendRows.size(); ++i)
                slot (col.sendRows[i], i < sendList.size() ? sendList[i].label + " " + juce::String (sendPercent (sendList[i].db)) + "%" : juce::String(),
                      i < sendList.size(), true);
        }
        if (col.hasPan)
        {
            caption (col.panLabel, "PAN");
            const auto text = kind == Kind::Channel ? panText (pan.getValue()) : Glyph::dash();
            g.setColour (text == "C" || kind != Kind::Channel ? Dine::ink4 : Dine::ink2);
            g.setFont (Dine::mono (9.5f, 500));
            g.drawText (text, col.panRead, juce::Justification::centred);
        }

        // ---- the unity line across the fader and the meter
        {
            const float t = float (fader.valueToProportionOfLength (0.0));
            const float y = float (col.fader.getY()) + float (col.fader.getHeight()) * (1.0f - t);
            g.setColour (Dine::edge);
            g.fillRect (float (col.body.getX()), y - 0.5f, float (col.body.getWidth()), 1.0f);
        }

        // ---- the level and the peak
        g.setColour (mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (levelText, col.level, juce::Justification::centred);
        if (col.hasPeak)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (10.0f));
            g.drawText ("pk " + peakText, col.peak, juce::Justification::centred);
        }

        // ---- where this strip goes
        if (col.hasOut)
        {
            g.setColour (tint().withAlpha (mute ? 0.5f : 1.0f));
            g.setFont (Dine::caps (9.0f, 0.08f));
            g.drawText (outText, col.out, juce::Justification::centredRight, true);
        }
    }

    void paintRow (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        Dine::fillRounded (g, r.toFloat(), selected ? Dine::selected : Dine::raised, Dine::Radius::control);
        auto inner = r.reduced (12, 0);

        // the band, the number and the name
        if (kind != Kind::Master)
        {
            g.setColour (tint().withAlpha (mute ? 0.4f : 1.0f));
            g.fillRoundedRectangle (inner.removeFromLeft (3).reduced (0, 8).toFloat(), 1.5f);
        }
        else inner.removeFromLeft (3);
        inner.removeFromLeft (12);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f, 500));
        g.drawText (numberText, inner.removeFromLeft (24), juce::Justification::centredLeft);
        inner.removeFromLeft (12);
        auto nameCell = inner.removeFromLeft (132);
        g.setColour (mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (12.5f, selected ? 600 : 500));
        g.drawText (name, nameCell, juce::Justification::centredLeft, true);
        if (mute)
        {
            const int w = juce::jmin (nameCell.getWidth(), Dine::textWidth (Dine::text (12.5f, 500), name));
            g.setColour (Dine::ink3);
            g.fillRect (nameCell.getX(), nameCell.getCentreY(), w, 1);
        }
        inner.removeFromLeft (12);
        // gain staging
        auto gainCell = inner.removeFromLeft (92);
        if (kind == Kind::Channel && advice.known)
            Dine::drawStatusChip (g, gainCell.withSizeKeepingCentre (gainCell.getWidth(), 17).toFloat(),
                                  gainAdviceChip (advice), gainAdviceColour (advice.level));
        else if (kind == Kind::Master)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (10.5f, 500));
            g.drawText (integratedText + " LUFS", gainCell, juce::Justification::centredLeft);
        }

        // level and pan readouts beside their controls
        g.setColour (mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        g.drawText (levelText, valueRect, juce::Justification::centredRight);
        if (kind == Kind::Channel)
        {
            const auto text = panText (pan.getValue());
            g.setColour (text == "C" ? Dine::ink4 : Dine::ink3);
            g.setFont (Dine::mono (10.0f, 500));
            g.drawText (text, panReadRect, juce::Justification::centredRight);
        }

        // where it goes
        if (outText.isNotEmpty())
        {
            g.setColour (tint().withAlpha (mute ? 0.5f : 1.0f));
            g.setFont (Dine::caps (9.0f, 0.08f));
            g.drawText (outText, r.reduced (12, 0), juce::Justification::centredRight, true);
        }
    }

    // -------------------------------------------------------------- layout
    void resized() override
    {
        if (layout == Layout::Column) layoutColumn();
        else                          layoutRow();
    }

    // One place decides where every section of a column is, on the same grid for every
    // width, so INSERTS, SENDS, PAN and the faders line up straight across the console. A
    // short window drops sections in a fixed order rather than squeezing the fader.
    void buildColumn()
    {
        col = Col {};
        const bool narrow = size == Size::Narrow;
        const int padX = narrow ? 5 : 8;
        auto r = getLocalBounds().withTrimmedTop (kind == Kind::Master ? 16 : 3 + 10).withTrimmedBottom (12).reduced (padX, 0);
        col.body = r;

        const int gap = 7;
        const int nameH = 16;
        const int gainH = kind == Kind::Master ? 0 : 17;
        const int loudH = kind == Kind::Master ? 5 * 18 : 0;
        const int inserts = kind == Kind::Master ? 0 : 3;
        const int sends = kind == Kind::Channel && showSends ? 2 : (kind == Kind::Bus && showSends ? 2 : 0);
        const int insertsH = inserts > 0 ? 11 + 2 + inserts * kSlotH + (inserts - 1) * 2 : 0;
        const int sendsH = sends > 0 ? 11 + 2 + sends * kSlotH + (sends - 1) * 2 : 0;
        const int panH = kind != Kind::Master ? 9 + 4 + 14 + 3 + 12 : 0;
        const int levelH = 14, peakH = kind == Kind::Master ? 0 : 13;
        const int keysH = kind == Kind::Master ? 0 : 2 * 22 + 4;
        const int outH = kind == Kind::Master ? 0 : 12;
        const int bodyMin = 70 + 12;

        bool keepSends = sends > 0, keepInserts = inserts > 0, keepOut = outH > 0, keepPan = panH > 0, keepPeak = peakH > 0;
        auto total = [&]
        {
            int t = nameH + gap + (gainH > 0 ? gainH + gap : 0) + (loudH > 0 ? loudH + gap : 0)
                  + (keepInserts ? insertsH + gap : 0) + (keepSends ? sendsH + gap : 0) + (keepPan ? panH + gap : 0)
                  + levelH + (keepPeak ? 2 + peakH : 0) + (keysH > 0 ? gap + keysH : 0) + (keepOut ? gap + outH : 0);
            return t;
        };
        if (r.getHeight() - total() < bodyMin) keepSends = false;
        if (r.getHeight() - total() < bodyMin) keepOut = false;
        if (r.getHeight() - total() < bodyMin) keepPeak = false;
        if (r.getHeight() - total() < bodyMin) keepInserts = false;
        if (r.getHeight() - total() < bodyMin) keepPan = false;

        col.name = r.removeFromTop (nameH);
        r.removeFromTop (gap);
        if (gainH > 0) { col.gain = r.removeFromTop (gainH); col.hasGain = true; r.removeFromTop (gap); }
        if (loudH > 0) { col.loudness = r.removeFromTop (loudH); col.hasLoudness = true; r.removeFromTop (gap); }
        if (keepInserts)
        {
            auto block = r.removeFromTop (insertsH);
            col.insertsLabel = block.removeFromTop (11);
            block.removeFromTop (2);
            for (int i = 0; i < inserts; ++i) { col.insertRows.push_back (block.removeFromTop (kSlotH)); block.removeFromTop (2); }
            col.hasInserts = true;
            r.removeFromTop (gap);
        }
        if (keepSends)
        {
            auto block = r.removeFromTop (sendsH);
            col.sendsLabel = block.removeFromTop (11);
            block.removeFromTop (2);
            for (int i = 0; i < sends; ++i) { col.sendRows.push_back (block.removeFromTop (kSlotH)); block.removeFromTop (2); }
            col.hasSends = true;
            r.removeFromTop (gap);
        }
        if (keepPan)
        {
            auto block = r.removeFromTop (panH);
            col.panLabel = block.removeFromTop (9);
            block.removeFromTop (4);
            col.panBar = block.removeFromTop (14);
            block.removeFromTop (3);
            col.panRead = block;
            col.hasPan = true;
            r.removeFromTop (gap);
        }

        if (keepOut) { col.out = r.removeFromBottom (outH); col.hasOut = true; r.removeFromBottom (gap); }
        if (keysH > 0) { col.keys = r.removeFromBottom (keysH); r.removeFromBottom (gap); }
        if (keepPeak) { col.peak = r.removeFromBottom (peakH); col.hasPeak = true; r.removeFromBottom (2); }
        col.level = r.removeFromBottom (levelH);

        // the throw: the fader 22 wide and the meter 7 (8 on the master), 6 apart, centred
        auto body = r.withTrimmedTop (6).withTrimmedBottom (6);
        const int faderW = 22, meterW = kind == Kind::Master ? 8 : 7;
        auto pair = body.withSizeKeepingCentre (faderW + 6 + meterW, body.getHeight());
        col.fader = pair.removeFromLeft (faderW);
        pair.removeFromLeft (6);
        col.meter = pair;
        col.body = juce::Rectangle<int> (col.fader.getX(), col.fader.getY(), col.meter.getRight() - col.fader.getX(), col.fader.getHeight());
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
        const int gap = 4;
        const int w = (keys.getWidth() - gap) / 2;
        if (kind == Kind::Channel)
        {
            auto top = keys.removeFromTop (22);
            keys.removeFromTop (gap);
            auto bottom = keys.removeFromTop (22);
            armButton.setBounds (top.removeFromLeft (w));
            monitorButton.setBounds (top.removeFromRight (w));
            muteButton.setBounds (bottom.removeFromLeft (w));
            soloButton.setBounds (bottom.removeFromRight (w));
        }
        else
        {
            auto row = keys.removeFromBottom (22);
            muteButton.setBounds (row.removeFromLeft (w));
            soloButton.setBounds (row.removeFromRight (w));
        }
    }

    void layoutRow()
    {
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromLeft (3 + 12 + 24 + 12 + 132 + 12 + 92 + 12);
        // LEVEL ARRIVING: the meter, lying down
        meter.setBounds (r.removeFromLeft (150).withSizeKeepingCentre (150, 7));
        r.removeFromLeft (12);
        fader.setBounds (r.removeFromLeft (96).withSizeKeepingCentre (96, 16));
        r.removeFromLeft (12);
        valueRect = r.removeFromLeft (58);
        r.removeFromLeft (12);
        auto panCell = r.removeFromLeft (78);
        pan.setVisible (kind == Kind::Channel);
        pan.setBounds (panCell.withSizeKeepingCentre (78, 16));
        r.removeFromLeft (12);
        panReadRect = r.removeFromLeft (32);
        r.removeFromLeft (12);
        auto keys = r.removeFromLeft (104).withSizeKeepingCentre (104, 22);
        const int w = 23;
        if (kind == Kind::Channel)
        {
            armButton.setBounds (keys.removeFromLeft (w)); keys.removeFromLeft (4);
            monitorButton.setBounds (keys.removeFromLeft (w)); keys.removeFromLeft (4);
        }
        else keys.removeFromLeft (2 * (w + 4));
        if (kind != Kind::Master)
        {
            muteButton.setBounds (keys.removeFromLeft (w)); keys.removeFromLeft (4);
            soloButton.setBounds (keys.removeFromLeft (w));
        }
        armButton.setVisible (kind == Kind::Channel);
        monitorButton.setVisible (kind == Kind::Channel);
        muteButton.setVisible (kind != Kind::Master);
        soloButton.setVisible (kind != Kind::Master);
    }

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
        m.addSectionHeader ((kind == Kind::Channel ? "CHANNEL " : "BUS ") + juce::String (Glyph::dot()) + " " + name);
        if (kind == Kind::Channel)
        {
            m.addItem (1, "TUNE CHANNEL", tune != nullptr);
            m.addItem (2, "Open in the Inspector", open != nullptr);
            m.addSeparator();
            m.addItem (3, "Set to record");
            m.addItem (4, "Monitoring: Input");
            m.addItem (5, "Monitoring: Auto");
            m.addItem (6, "Monitoring: Off");
            m.addSeparator();
            m.addItem (7, "Fix the assignments" + juce::String (Glyph::ellip()), assign != nullptr);
        }
        else
        {
            m.addItem (8, "Clear solo on this bus");
            m.addItem (2, "Open in the Inspector", open != nullptr);
        }
        juce::Component::SafePointer<Strip> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (230),
                         [safe] (int chosen)
                         {
                             if (safe == nullptr || chosen <= 0) return;
                             auto& s = *safe;
                             switch (chosen)
                             {
                                 case 1: if (s.tune) s.tune(); break;
                                 case 2: if (s.open) s.open(); break;
                                 case 3: s.armButton.triggerClick(); break;
                                 case 4: case 5: case 6:
                                 {
                                     auto& project = s.services.daw().getProject();
                                     if (s.strip < 0 || s.strip >= int (project.tracks.size())) break;
                                     project.tracks[size_t (s.strip)].monitor = chosen == 4 ? MonitorMode::Input : chosen == 5 ? MonitorMode::Auto : MonitorMode::Off;
                                     s.services.daw().refresh();
                                     s.services.saveSession();
                                     s.refresh (true);
                                     break;
                                 }
                                 case 7: if (s.assign) s.assign(); break;
                                 case 8: if (s.kind == Kind::Bus) s.controller.setBusSolo (s.bus, false); break;
                                 default: break;
                             }
                         });
    }

    struct Col
    {
        juce::Rectangle<int> name, gain, loudness, insertsLabel, sendsLabel, panLabel, panBar, panRead,
                             fader, meter, body, level, peak, keys, out;
        std::vector<juce::Rectangle<int>> insertRows, sendRows;
        bool hasLoudness = false, hasInserts = false, hasSends = false, hasPan = false,
             hasOut = false, hasGain = false, hasPeak = false;
    };
    struct SendView { juce::String label; float db = kSilenceDb; };

    MixController& controller;
    AppServices& services;
    Kind kind;
    MixBus bus;
    ChannelRole role;
    int strip = -1;
    juce::String name, source, levelText { "+0.0" }, peakText { Glyph::dash() }, numberText, outText;
    juce::String integratedText { Glyph::dash() }, shortTermText { Glyph::dash() }, truePeakText { Glyph::dash() },
                 grText { Glyph::dash() }, targetText { "-23.0" };
    bool truePeakOver = false, limiterHot = false;
    float peakDb = -120.0f, shownFaderDb = 1000.0f;
    juce::Rectangle<int> valueRect, panReadRect;
    Col col;
    std::vector<ChainStage> insertList;
    std::vector<SendView> sendList;
    std::array<float, int (FxSlot::Count)> shownSends {};
    juce::uint32 chainFingerprint = 0;
    MixController::InputAdvice advice;
    bool stereo = false, showSends = true;
    bool mute = false, solo = false, bypassed = false, updating = false, selected = false;
    Layout layout = Layout::Column;
    Size size = Size::Normal;
    Dine::Icon icon;
    DineMeter meter;
    juce::Slider fader;
    PanBar pan;
    DineKey muteButton, soloButton, armButton, monitorButton;
    std::function<void()> open, select, tune, assign;
};

// ------------------------------------------------------------------ Bank
// The scrolled surface: the ground the columns sit on. Opaque, so a scroll never asks the
// page underneath to paint. In LIST view it also paints the column headings.
class MixerPage::Bank : public juce::Component
{
public:
    Bank() { setOpaque (true); }
    void setList (bool l) { list = l; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::window);
        if (! list) return;
        auto r = getLocalBounds().withHeight (kListHeadH).reduced (12, 0);
        g.setColour (Dine::ink4);
        g.setFont (Dine::caps (9.5f, 0.12f));
        r.removeFromLeft (3 + 12);
        g.drawText ("#", r.removeFromLeft (24), juce::Justification::centredLeft);
        r.removeFromLeft (12);
        g.drawText ("NAME", r.removeFromLeft (132), juce::Justification::centredLeft);
        r.removeFromLeft (12);
        g.drawText ("GAIN STAGING", r.removeFromLeft (92), juce::Justification::centredLeft);
        r.removeFromLeft (12);
        g.drawText ("LEVEL ARRIVING", r.removeFromLeft (150), juce::Justification::centredLeft);
        r.removeFromLeft (12);
        g.drawText ("FADER", r.removeFromLeft (96), juce::Justification::centredLeft);
        r.removeFromLeft (12);
        g.drawText ("DB", r.removeFromLeft (58), juce::Justification::centredRight);
        r.removeFromLeft (12);
        g.drawText ("PAN", r.removeFromLeft (78), juce::Justification::centred);
        r.removeFromLeft (12 + 32 + 12);
        g.drawText ("R " + Glyph::dot() + " A " + Glyph::dot() + " M " + Glyph::dot() + " S", r.removeFromLeft (104), juce::Justification::centred);
        g.drawText ("BUS", r, juce::Justification::centredRight);
    }

private:
    bool list = false;
};

// ------------------------------------------------------------------ MixerPage
MixerPage::MixerPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    bank = std::make_unique<Bank>();
    viewport.setViewedComponent (bank.get(), false);
    viewport.setScrollBarsShown (false, true);
    viewport.setOpaque (true);
    Dine::nativeScrolling (viewport);
    addAndMakeVisible (viewport);

    auto segment = [] (std::unique_ptr<DineButton>& b, const char* label, int pad)
    {
        b = std::make_unique<DineButton> (label, DineButton::Style::Segment);
        b->setFontPx (12.0f);
        b->setPadX (pad);
        b->setClickingTogglesState (false);
    };
    const char* viewNames[2] = { "Strips", "List" };
    for (int i = 0; i < 2; ++i)
    {
        segment (viewTabs[size_t (i)], viewNames[i], 10);
        viewTabs[size_t (i)]->onClick = [this, i] { setView (View (i)); };
        addAndMakeVisible (*viewTabs[size_t (i)]);
    }
    viewTabs[0]->setTooltip ("The classic console: one vertical strip per source.");
    viewTabs[1]->setTooltip ("One row per source, so every name and level lines up down the page.");

    const char* sizeNames[3] = { "S", "M", "L" };
    for (int i = 0; i < 3; ++i)
    {
        segment (sizeTabs[size_t (i)], sizeNames[i], 8);
        sizeTabs[size_t (i)]->onClick = [this, i] { setStripSize (Size (i)); };
        addAndMakeVisible (*sizeTabs[size_t (i)]);
    }
    sizeTabs[0]->setTooltip ("Narrow strips: the whole band on one screen.");
    sizeTabs[1]->setTooltip ("Normal strips.");
    sizeTabs[2]->setTooltip ("Wide strips: every label in full.");

    const char* showNames[3] = { "All", "Inputs", "Groups" };
    for (int i = 0; i < 3; ++i)
    {
        segment (showTabs[size_t (i)], showNames[i], 10);
        showTabs[size_t (i)]->onClick = [this, i] { setShow (Show (i)); };
        addAndMakeVisible (*showTabs[size_t (i)]);
    }
    showTabs[1]->setTooltip ("Only the sources.");
    showTabs[2]->setTooltip ("Only the group buses and the master.");

    sendsButton.setFontPx (12.0f);
    sendsButton.setPadX (10);
    sendsButton.setTooltip ("Show how much of each source goes to the reverbs and delays.");
    sendsButton.onClick = [this] { setSendsVisible (! showSends); };
    addAndMakeVisible (sendsButton);

    chainStrip.setEmpty ("Click a strip to read its chain here. Double-click it to open the Inspector.");
    chainStrip.onOpen = [this]
    {
        if (selected >= 0) { if (onOpenStrip) onOpenStrip (selected); }
        else if (selectedBusValue != MixBus::Count) { if (onOpenBus) onOpenBus (selectedBusValue); }
    };
    addAndMakeVisible (chainStrip);

    clearSolos.setFontPx (12.0f);
    clearSolos.setPadX (10);
    clearSolos.setTooltip ("Every solo off.");
    clearSolos.onClick = [this]
    {
        controller.clearSolos();
        if (onToast) onToast ("Solo cleared.");
        refresh();
    };
    addAndMakeVisible (clearSolos);

    windowButton.setFontPx (12.0f);
    windowButton.setPadX (11);
    windowButton.setTooltip ("Put the console on a second screen and keep the timeline in front of you.");
    windowButton.onClick = [this] { if (onOpenWindow) onOpenWindow(); };
    addAndMakeVisible (windowButton);

    setOpaque (true);
    rebuild();
}

MixerPage::~MixerPage() = default;

void MixerPage::setWindowButtonVisible (bool v)
{
    windowButtonWanted = v;
    windowButton.setVisible (v);
    resized();
}

void MixerPage::setFootShown (bool v)
{
    if (footShown == v) return;
    footShown = v;
    chainStrip.setVisible (v);
    resized();
}

void MixerPage::setView (View v)
{
    if (view == v) return;
    view = v;
    viewport.setScrollBarsShown (v == View::List, v == View::Strips);
    bank->setList (v == View::List);
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
    selectedBusValue = MixBus::Count;
    updateChainStrip();
}

void MixerPage::selectBus (MixBus b)
{
    selected = -1;
    selectedBusValue = b;
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
            s->setAssignHandler ([this] { if (onOpenAssign) onOpenAssign(); });
            add (std::move (s));
        }

        if (controller.isPrepared() && controller.getEngine().isBusUsed (bus))
        {
            auto s = std::make_unique<Strip> (controller, services, Strip::Kind::Bus, bus, ChannelRole::KickIn, -1,
                                              juce::String (mixBusName (bus)).toUpperCase(),
                                              juce::String (graph.stripsOnBus (bus))
                                                  + (graph.stripsOnBus (bus) == 1 ? " source" : " sources"));
            s->setOpenHandler ([this, bus] { if (onOpenBus) onOpenBus (bus); });
            s->setSelectHandler ([this, bus] { selectBus (bus); });
            add (std::move (s));
        }
    }

    if (controller.isPrepared() && controller.getEngine().isBusUsed (MixBus::Master))
    {
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
    if (selected >= graph.numStrips()) { selected = -1; selectedBusValue = MixBus::Count; }
    adviceForTune = -1;
    updateChainStrip();
    updateControls();
    resized();
}

void MixerPage::refresh()
{
    if (! controller.isPrepared()) return;
    if (controller.getGraph().numStrips() != builtForStrips) { rebuild(); return; }
    const int tunes = controller.getTuneCount();
    const bool adviceDue = (tick++ % 15) == 0 || tunes != adviceForTune;
    adviceForTune = tunes;
    for (auto& s : strips) s->refresh (adviceDue);
    if (footShown && (tick % 3) == 0) updateChainStrip();
    if ((tick % 5) == 0) updateControls();
}

void MixerPage::updateChainStrip()
{
    if (! controller.isPrepared())
    {
        chainStrip.setEmpty ("Set the device up first.");
        return;
    }

    const auto& state = controller.getBase();
    if (footShown)
    {
        if (selected >= 0 && selected < state.numStrips)
        {
            const auto& r = controller.getGraph().strips[size_t (selected)];
            chainStrip.setSource (juce::String (r.name), Dine::busTint (r.bus), state.strips[size_t (selected)].channel,
                                  false, r.inputB >= 0);
        }
        else if (selectedBusValue != MixBus::Count)
        {
            chainStrip.setSource (sentenceCase (mixBusName (selectedBusValue)), Dine::busTint (selectedBusValue),
                                  state.buses[size_t (selectedBusValue)].channel, selectedBusValue == MixBus::Master, true);
        }
        else chainStrip.setEmpty ("Click a strip to read its chain here. Double-click it to open the Inspector.");
        chainStrip.setNote (controller.isBypassed() ? "BYPASS" : juce::String());
    }

    for (auto& s : strips)
        s->setSelected (selected >= 0 ? s->getStripIndex() == selected
                                      : selectedBusValue != MixBus::Count && s->getKind() != Strip::Kind::Channel
                                        && s->getBus() == selectedBusValue);
}

void MixerPage::updateControls()
{
    for (int i = 0; i < 2; ++i) viewTabs[size_t (i)]->setToggleState (int (view) == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
    {
        sizeTabs[size_t (i)]->setToggleState (int (stripSize) == i, juce::dontSendNotification);
        showTabs[size_t (i)]->setToggleState (int (show) == i, juce::dontSendNotification);
    }
    bool anySolo = controller.isPrepared() && controller.numSoloed() > 0;
    clearSolos.setToggleState (anySolo, juce::dontSendNotification);
    clearSolos.setEnabled (anySolo);
    windowButton.setVisible (windowButtonWanted);
    sendsButton.setToggleState (showSends, juce::dontSendNotification);
    const bool sizesWanted = view == View::Strips;
    if (sizeTabs[0]->isVisible() != sizesWanted || sendsButton.isVisible() != sizesWanted)
    {
        for (auto& t : sizeTabs) t->setVisible (sizesWanted);
        sendsButton.setVisible (sizesWanted);
        resized();
    }
}

// -------------------------------------------------------------------- paint
void MixerPage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    Dine::drawHeaderBand (g, getLocalBounds().removeFromTop (kHeaderH));

    auto trackFor = [&g] (juce::Component* first, juce::Component* last)
    {
        if (first == nullptr || ! first->isVisible()) return;
        Dine::drawSegmentTrack (g, first->getBounds().getUnion (last->getBounds()).expanded (2, 2));
    };
    trackFor (viewTabs[0].get(), viewTabs[1].get());
    trackFor (showTabs[0].get(), showTabs[2].get());
    if (view == View::Strips) trackFor (sizeTabs[0].get(), sizeTabs[2].get());

    if (strips.empty())
    {
        auto empty = getLocalBounds().withTrimmedTop (kHeaderH).reduced (kPadX, 20);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawFittedText ("Assign inputs first. The console fills with a strip for each one, then the group buses and the master.",
                          empty.removeFromTop (60), juce::Justification::topLeft, 3);
    }
}

// ------------------------------------------------------------------- layout
void MixerPage::resized()
{
    auto head = getLocalBounds().removeFromTop (kHeaderH).reduced (kPadX, 0);
    auto controls = head.withSizeKeepingCentre (head.getWidth(), Dine::Metric::control);

    // left to right: Strips / List, All / Inputs / Groups, S / M / L, Sends
    auto left = controls;
    auto seg = [&left] (std::unique_ptr<DineButton>* buttons, int n, int minW)
    {
        auto track = left;
        int x = track.getX() + 2;
        for (int i = 0; i < n; ++i)
        {
            const int w = juce::jmax (minW, buttons[i]->idealWidth());
            buttons[i]->setBounds (x, track.getY() + 2, w, track.getHeight() - 4);
            x += w;
        }
        left.removeFromLeft (x + 2 - track.getX() + 10);
    };
    seg (viewTabs.data(), 2, 52);
    seg (showTabs.data(), 3, 48);
    if (view == View::Strips)
    {
        seg (sizeTabs.data(), 3, 28);
        sendsButton.setBounds (left.removeFromLeft (juce::jmax (60, sendsButton.idealWidth())).reduced (0, 1));
        left.removeFromLeft (10);
    }

    auto right = controls;
    if (windowButton.isVisible())
    {
        const int w = juce::jmax (120, windowButton.idealWidth());
        windowButton.setBounds (right.removeFromRight (w).reduced (0, 1));
        right.removeFromRight (10);
    }
    clearSolos.setBounds (right.removeFromRight (juce::jmax (84, clearSolos.idealWidth())).reduced (0, 1));

    chainStrip.setBounds (getLocalBounds().removeFromBottom (footHeight()));

    if (view == View::Strips) layoutStrips();
    else                      layoutList();
    repaint();
}

MixerPage::Strip* MixerPage::masterStrip() const
{
    for (auto& s : strips)
        if (s->getKind() == Strip::Kind::Master) return s.get();
    return nullptr;
}

void MixerPage::layoutStrips()
{
    auto area = getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedBottom (footHeight());
    const int h = juce::jmax (180, area.getHeight());
    const int stripH = juce::jmin (h, kMaxStripH);

    auto* master = masterStrip();
    if (master != nullptr && visibleInFilter (*master))
    {
        if (master->getParentComponent() != this) addAndMakeVisible (*master);
        master->setVisible (true);
        master->setBounds (area.removeFromRight (kMasterW).withHeight (stripH));
    }
    else if (master != nullptr) master->setVisible (false);
    viewport.setBounds (area);

    int x = 0;
    for (int b = 0; b <= int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        for (auto& s : strips)
        {
            if (s->getBus() != bus || s.get() == master) continue;
            const bool wanted = visibleInFilter (*s);
            s->setVisible (wanted);
            if (! wanted) continue;
            s->setBounds (x, 0, s->columnWidth(), stripH);
            x += s->columnWidth() + kStripGap;
        }
    }
    // No trailing gap: a bank that exactly fits must not grow a scrollbar for the gap after its last strip.
    bank->setSize (juce::jmax (x > 0 ? x - kStripGap : 0, viewport.getWidth()), juce::jmax (stripH, viewport.getMaximumVisibleHeight()));
}

void MixerPage::layoutList()
{
    if (auto* master = masterStrip(); master != nullptr && master->getParentComponent() != bank.get())
        bank->addAndMakeVisible (*master);
    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedBottom (footHeight()).reduced (kPadX, 0));

    const int w = juce::jmax (760, viewport.getMaximumVisibleWidth());
    int y = kListHeadH;
    for (int b = 0; b <= int (MixBus::Master); ++b)
    {
        const auto bus = MixBus (b);
        for (auto& s : strips)
        {
            if (s->getBus() != bus) continue;
            const bool wanted = visibleInFilter (*s);
            s->setVisible (wanted);
            if (! wanted) continue;
            s->setBounds (0, y, w, kRowH);
            y += kRowH + kRowGap;
        }
    }
    bank->setSize (w, juce::jmax (y + 8, viewport.getMaximumVisibleHeight()));
}

} // namespace livemix
