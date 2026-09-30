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
    // Between columns. Enough that two strips are two things: at 3 the console read as one
    // wall of faders, and on a busy service the eye has to find a strip before it can move it.
    constexpr int kStripGap  = 8;
    constexpr int kRowH      = 46;      // list view
    constexpr int kRowGap    = 3;
    constexpr int kListHeadH = 30;
    constexpr int kMasterW   = 150;     // the pinned master column
    constexpr int kMaxStripH = 900;

    // ---- THE LIST'S GRID, in one place.
    // The row, the header above it and the controls on it all read this, so a column cannot
    // be in two places at once. And when the window is narrower than the whole grid the cells
    // are given up in a fixed order - the group, then the balance, then gain staging - rather
    // than every cell shuffling left until the last one is 7 pt wide and its number is "L...".
    struct RowGrid
    {
        juce::Rectangle<int> band, number, name, gain, meter, fader, value, pan, panRead, keys, out;
        bool hasGain = false, hasPan = false, hasOut = false;
    };

    RowGrid rowGrid (juce::Rectangle<int> bounds)
    {
        constexpr int gap = 12, kNum = 24, kName = 132, kGain = 92, kValue = 58, kPan = 78, kPanRead = 32;
        constexpr int kKeys = 104 + 4 + 30, kOut = 92;
        constexpr int kMeterMax = 150, kMeterMin = 80, kFaderMax = 96, kFaderMin = 64, kNameMin = 74;

        RowGrid g;
        auto r = bounds.reduced (12, 0);
        int spare = r.getWidth() - (3 + gap + kNum + gap + kName + gap
                                    + kMeterMin + gap + kFaderMin + gap + kValue + gap + kKeys);
        g.hasGain = spare >= kGain + gap;                  if (g.hasGain) spare -= kGain + gap;
        g.hasPan  = spare >= kPan + gap + kPanRead + gap;  if (g.hasPan)  spare -= kPan + gap + kPanRead + gap;
        g.hasOut  = spare >= kOut + gap;                   if (g.hasOut)  spare -= kOut + gap;

        // Past the last thing that can be dropped, the name gives ground; below that the row
        // simply runs out and the console is telling the truth about being too narrow.
        const int nameW = juce::jlimit (kNameMin, kName, kName + juce::jmin (0, spare));
        const int grow = juce::jmax (0, spare);
        const int meterW = kMeterMin + juce::jmin (kMeterMax - kMeterMin, grow * 3 / 5);
        const int faderW = kFaderMin + juce::jmin (kFaderMax - kFaderMin, grow - (meterW - kMeterMin));

        g.band   = r.removeFromLeft (3);      r.removeFromLeft (gap);
        g.number = r.removeFromLeft (kNum);   r.removeFromLeft (gap);
        g.name   = r.removeFromLeft (nameW);  r.removeFromLeft (gap);
        if (g.hasGain) { g.gain = r.removeFromLeft (kGain); r.removeFromLeft (gap); }
        g.meter  = r.removeFromLeft (meterW); r.removeFromLeft (gap);
        g.fader  = r.removeFromLeft (faderW); r.removeFromLeft (gap);
        g.value  = r.removeFromLeft (kValue); r.removeFromLeft (gap);
        if (g.hasPan)
        {
            g.pan = r.removeFromLeft (kPan);         r.removeFromLeft (gap);
            g.panRead = r.removeFromLeft (kPanRead); r.removeFromLeft (gap);
        }
        g.keys   = r.removeFromLeft (kKeys);
        g.out    = r;
        return g;
    }

    int columnWidthFor (MixerPage::Size s) noexcept
    {
        // The design's strip is 70 pt (`Channel Strip`, 63:9488); S and L are either side of it.
        return s == MixerPage::Size::Narrow ? 58 : s == MixerPage::Size::Wide ? 106 : 74;
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
            case Level::Healthy:  return "OK";
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
    constexpr int kCaptionH = 12;   // the strip head's caption line, above the name
    // Between two rows of the same block (the chain's three, the two sends). Wider than it
    // was, and still well under the gap between one block and the next.
    constexpr int kRowGapIn = 4;
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
          meterRight (DineMeter::Style::Bar),
          muteButton ("M", Dine::keyMute),
          soloButton ("S", Dine::keySolo),
          armButton ("R", Dine::keyRec),
          monitorButton ("A", Dine::keyMon),
          fxButton ("FX", Dine::keyFx)
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
            // A linked fader takes its partners with it; Cmd-drag moves this one alone.
            if (kind == Kind::Channel) controller.setStripFader (strip, db, ! juce::ModifierKeys::getCurrentModifiers().isCommandDown());
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
        // EFFECTS ON THIS MICROPHONE. One press for the pastor who has started singing, and one
        // press back when he goes back to preaching. Only on a voice channel, and only where the
        // session has the returns to send to.
        fxButton.setVisible (kind == Kind::Channel && controller.stripCanHaveEffects (strip));
        fxButton.setTooltip ("Effects on this microphone " + Glyph::dot() + " the reverb and the delay. Off for speaking, "
                             "on for singing. Your levels are kept either way, so one press is the way back.");
        monitorButton.setTooltip ("Monitoring " + Glyph::dot() + " click to cycle: input (you hear the live input), auto (input while recording), off (the timeline only)");

        armButton.onClick = [this]
        {
            auto& project = services.daw().getProject();
            if (strip < 0 || strip >= int (project.tracks.size())) return;
            project.tracks[size_t (strip)].armed = ! project.tracks[size_t (strip)].armed;
            services.daw().refresh();
            services.touchSession();
            refresh (true);
        };
        monitorButton.onClick = [this]
        {
            auto& project = services.daw().getProject();
            if (strip < 0 || strip >= int (project.tracks.size())) return;
            auto& mode = project.tracks[size_t (strip)].monitor;
            mode = MonitorMode ((int (mode) + 1) % int (MonitorMode::Count));
            services.daw().refresh();
            services.touchSession();
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
        fxButton.onClick = [this]
        {
            if (kind == Kind::Channel) controller.setStripEffects (strip, ! controller.stripEffectsOn (strip));
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
        addChildComponent (meterRight);
        addAndMakeVisible (fader);
        addAndMakeVisible (pan);
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);
        addAndMakeVisible (armButton);
        addAndMakeVisible (monitorButton);
        addChildComponent (fxButton);
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
        wantedSize = s;
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
            linkNow = st.linkGroup;
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
            // The master is metered in stereo, because a broadcast that has gone mono, or one
            // side that has gone, is the one thing a single bar cannot say.
            if (kind == Kind::Master && m.getNumChannels() > 1)
            {
                meterRight.setLevels (juce::jmax (m.getPeakDb (1), -120.0f), m.getRmsDb (1), clipped);
                peak = juce::jmax (m.getPeakDb (0), -120.0f);
                hold = m.getRmsDb (0);
            }
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
        if (linkNow != link)
        {
            link = linkNow;
            fader.setTooltip (link != 0 ? "Level. Linked with " + juce::String (controller.linkedNames (strip))
                                              + ": they move and solo together. Cmd-drag to move this one alone. Double-click for 0.0 dB."
                                        : juce::String ("Level. Double-click for 0.0 dB."));
            body = true;
        }
        muteButton.setOn (mute);
        soloButton.setOn (solo);
        if (kind == Kind::Channel) fxButton.setOn (controller.stripEffectsOn (strip));
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
                const bool sample = kind == Kind::Channel && strip >= 0 && strip < controller.getGraph().numStrips()
                                    && hasSampleStage (controller.getGraph().strips[size_t (strip)].role);
                insertList = activeChainStages (*channel, kind == Kind::Master, stereo, sample);
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
            juce::String target = juce::String (m.targetLufs, 0) + " LUFS";
            const bool onTarget = m.known && m.onTarget();
            // Short enough to sit beside LUFS-I in the master column at any console width.
            const juce::String note = ! m.known || m.integratedLufs <= -60.0f ? juce::String ("not measured")
                                    : onTarget ? juce::String ("on target")
                                    : m.integratedLufs > m.targetLufs ? juce::String ("over target")
                                                                      : juce::String ("under target");
            if (i != integratedText || st != shortTermText || tp != truePeakText || gr != grText || target != targetText
                || onTarget != loudnessOnTarget || note != loudnessNote)
            {
                integratedText = i; shortTermText = st; truePeakText = tp; grText = gr; targetText = target;
                loudnessOnTarget = onTarget; loudnessNote = note;
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

    // ONE SLOT, ONE BOX. The gain chip, each insert and each send is a thing of its own,
    // and on a 70 pt column six lines of small type separated by nothing but space read as
    // one loose list. Every slot stands on its own plane, the way a DAW's channel strip
    // draws its inserts - a filled one when there is something in it, a quieter recess when
    // the slot is reserved and empty, so the console's line does not bend either way.
    // (`Channel Strip` 63:9488 says no boxes inside a strip; docs/DESIGN-V3.md says why not.)
    void drawSlot (juce::Graphics& g, juce::Rectangle<int> row, bool used) const
    {
        if (row.isEmpty()) return;
        const juce::Rectangle<int> box (2, row.getY(), getWidth() - 4, row.getHeight());
        Dine::fillRounded (g, box.toFloat(), used ? Dine::item : Dine::inset, Dine::Radius::chip);
    }

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
        else
        {
            // The master is pinned beside the scrolling bank: a seam down its left edge says so.
            g.setColour (Dine::hair);
            g.fillRect (r.removeFromLeft (1));
        }

        // ---- the head: a caption line, then the name
        // Two lines, the same two on every strip, both flush left: what the strip is (the
        // input's number, "Group", "Output") in small quiet type, and under it the name, which
        // is what the strip is for and so gets the whole width. The number used to sit on the
        // name's line and a group carried both a "GROUP" caption and a "BUS" number, so a
        // group's head said the same thing twice and every name started somewhere different.
        {
            const auto caption = col.name.withHeight (kCaptionH);
            const auto nameRow = col.name.withTrimmedTop (kCaptionH);
            const juce::String captionText = kind == Kind::Channel ? numberText
                                           : kind == Kind::Bus     ? juce::String ("Group")
                                                                   : juce::String ("Output");
            g.setColour (Dine::ink4);
            g.setFont (kind == Kind::Channel ? Dine::mono (10.0f, 500) : Dine::text (10.0f, 500));
            Dine::drawText (g, captionText, caption, juce::Justification::centredLeft);

            // A linked strip carries its mark at the right end of the name row.
            const int markW = link != 0 ? 16 : 0;
            const bool strong = kind != Kind::Channel || selected;
            const auto nameFont = Dine::text (size == Size::Narrow ? 12.0f : 13.0f, strong ? 600 : 500);
            auto head = nameRow.withTrimmedRight (markW);
            head = head.withWidth (juce::jmin (head.getWidth(), Dine::textWidth (nameFont, name)));
            g.setColour (mute ? Dine::ink3 : Dine::ink);
            g.setFont (nameFont);
            // A strip is 74 pt wide and a name is what the strip is for: it is squeezed a
            // little before it is ever cut off, the way the group tiles on TUNE squeeze theirs.
            Dine::drawFittedText (g, kind == Kind::Master ? juce::String ("Master") : name, head,
                                  juce::Justification::centredLeft, 1, 0.78f);
            if (mute)
            {
                g.setColour (Dine::ink3);
                g.fillRect (head.getX(), head.getCentreY(), head.getWidth(), 1);
            }
            if (link != 0) Dine::drawLinkGlyph (g, nameRow.withTrimmedLeft (nameRow.getWidth() - markW).toFloat().reduced (2.0f, 0.0f), Dine::accent);
        }

        // ---- gain staging: every strip that can have it, always in its slot
        if (col.hasGain)
        {
            // The chip is always there, because "nothing is said about this input" and "this
            // input is fine" are different things and a slot that comes and goes bends the
            // line the whole console is read down.
            // An unlit lamp and nothing beside it. A dash on every strip of a thirty-two
            // channel console is thirty-two words that say nothing, and they are the reason
            // the head reads as a wall; the slot is still here, so the line across the
            // console does not bend when one input has something to say and the rest do not.
            // The slot's own plane says it is a slot; the lamp and the word's colour still
            // say whether it is quiet or loud.
            drawSlot (g, col.gain, advice.known);
            auto area = col.gain;
            auto lamp = area.removeFromLeft (5).withSizeKeepingCentre (4, 4);
            g.setColour (advice.known ? gainAdviceColour (advice.level) : Dine::ink4.withAlpha (0.45f));
            g.fillEllipse (lamp.toFloat());
            area.removeFromLeft (5);
            if (advice.known)
            {
                g.setFont (Dine::caps (9.5f, 0.04f, 600));
                Dine::drawText (g, gainAdviceChip (advice), area, juce::Justification::centredLeft, true);
            }
        }

        // ---- the master's loudness
        if (col.hasLoudness)
        {
            // How loud the broadcast is, in an inset display the eye finds from across the
            // booth (`Master Column`, 65:9316), then the four numbers under it.
            auto block = col.loudness;
            auto display = block.removeFromTop (56);
            Dine::fillRounded (g, display.toFloat(), Dine::deep, Dine::Radius::well);
            auto inner = display.reduced (10, 6);
            g.setColour (loudnessOnTarget ? Dine::accent : Dine::warn);
            g.setFont (Dine::mono (26.0f, 500));
            Dine::drawText (g, integratedText, inner.removeFromTop (30), juce::Justification::centredLeft, true);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (10.5f, 500));
            Dine::drawText (g, "LUFS-I  " + juce::String (Glyph::dot()) + "  " + loudnessNote, inner,
                            juce::Justification::centredLeft, true);
            block.removeFromTop (10);

            auto row = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
            {
                auto line = block.removeFromTop (21);
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (12.0f, 500));
                Dine::drawText (g, k, line, juce::Justification::centredLeft);
                g.setColour (c);
                g.setFont (Dine::mono (11.0f, 500));
                Dine::drawText (g, v, line, juce::Justification::centredRight);
            };
            row ("Short", shortTermText, Dine::ink);
            row ("True peak", truePeakText, truePeakOver ? Dine::crit : Dine::ink);
            row ("Limiter", grText, limiterHot ? Dine::warn : Dine::ink);
            row ("Target", targetText, Dine::ink);
        }

        // ---- the inserts: a lamp and the stage's name, lit when the stage is on. The slots
        // are fixed, so an empty one holds its place and the sections line up straight across
        // the whole console.
        if (col.hasInserts)
        {
            for (size_t i = 0; i < col.insertRows.size(); ++i)
            {
                auto area = col.insertRows[i];
                const bool used = i < insertList.size();
                drawSlot (g, area, used);
                auto lamp = area.removeFromLeft (5).withSizeKeepingCentre (4, 4);
                g.setColour (used ? Dine::accent : Dine::ink4.withAlpha (0.3f));
                g.fillEllipse (lamp.toFloat());
                if (! used) continue;              // an empty slot is an unlit lamp, not a dash
                area.removeFromLeft (5);
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (10.5f, 500));
                Dine::drawFittedText (g, sentenceCase (insertList[i].label), area, juce::Justification::centredLeft, 1, 0.78f);
            }
        }

        // ---- the sends: the return's name, and a bar for how much of this goes to it
        if (col.hasSends)
        {
            for (size_t i = 0; i < col.sendRows.size(); ++i)
            {
                auto area = col.sendRows[i];
                const bool used = i < sendList.size();
                drawSlot (g, area, used);
                auto bar = area.removeFromRight (juce::jmin (22, area.getWidth() / 3)).withSizeKeepingCentre (
                               juce::jmin (22, col.sendRows[i].getWidth() / 3), 3);
                area.removeFromRight (5);
                // A send that is off is an empty slot, not an empty track: a stub of grey with
                // no name beside it reads as a mark on the strip rather than as nothing.
                if (! used) continue;
                g.setColour (Dine::ink2);
                g.setFont (Dine::text (10.5f, 500));
                Dine::drawFittedText (g, sendList[i].label, area, juce::Justification::centredLeft, 1, 0.78f);
                Dine::fillRounded (g, bar.toFloat(), Dine::control, 1.5f);
                const float amount = juce::jlimit (0.0f, 1.0f, float (sendPercent (sendList[i].db)) / 100.0f);
                if (amount > 0.01f)
                    Dine::fillRounded (g, bar.toFloat().withWidth (bar.getWidth() * amount), Dine::accent, 1.5f);
            }
        }

        // ---- the balance, and what it reads
        if (col.hasPan)
        {
            const auto text = kind == Kind::Channel ? panText (pan.getValue()) : Glyph::dash();
            g.setColour (text == "C" || kind != Kind::Channel ? Dine::ink3 : Dine::ink2);
            g.setFont (Dine::mono (10.0f, 500));
            Dine::drawText (g, text, col.panRead, juce::Justification::centred);
        }

        // ---- the one mark the bank is read against: the unity line, across the fader and the
        // meter at the same height in every strip. Beside it, the design's scale - +6 at the
        // top, then 10, 20, 40 and the floor - drawn once down the gutter between them, quiet
        // enough to be a ruler and not a row of numbers.
        {
            const float t = float (fader.valueToProportionOfLength (0.0));
            const float y = float (col.fader.getY()) + float (col.fader.getHeight()) * (1.0f - t);
            g.setColour (Dine::edge);
            g.fillRect (float (col.body.getX()), y - 0.5f, float (col.body.getWidth()), 1.0f);

            if (col.body.getHeight() > 150 && col.scale.getWidth() >= 14)
            {
                g.setColour (Dine::ink4);
                g.setFont (Dine::mono (9.0f, 500));
                const double marks[5] = { 6.0, -10.0, -20.0, -40.0, -60.0 };
                const char* labels[5] = { "+6", "10", "20", "40", "\xe2\x88\x9e" };
                for (int i = 0; i < 5; ++i)
                {
                    const float p = float (fader.valueToProportionOfLength (marks[i]));
                    const int my = col.body.getY() + juce::roundToInt (float (col.body.getHeight()) * (1.0f - p));
                    Dine::drawText (g, juce::CharPointer_UTF8 (labels[i]),
                                    juce::Rectangle<int> (col.scale.getX(), my - 6, col.scale.getWidth(), 12),
                                    juce::Justification::centred);
                }
            }
        }

        // ---- the level and the peak
        g.setColour (mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, levelText, col.level, juce::Justification::centred);
        if (col.hasPeak)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::mono (10.0f));
            Dine::drawText (g, "pk " + peakText, col.peak, juce::Justification::centred);
        }

        // ---- where this strip goes
        if (col.hasOut)
        {
            g.setColour (tint().withAlpha (mute ? 0.5f : 0.85f));
            g.setFont (Dine::text (10.0f, 500));
            Dine::drawText (g, sentenceCase (outText), col.out, juce::Justification::centred, true);
        }
    }

    void paintRow (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        Dine::fillRounded (g, r.toFloat(), selected ? Dine::selected : Dine::raised, Dine::Radius::control);
        const auto grid = rowGrid (getLocalBounds());

        // the band, the number and the name
        if (kind != Kind::Master)
        {
            g.setColour (tint().withAlpha (mute ? 0.4f : 1.0f));
            g.fillRoundedRectangle (grid.band.reduced (0, 8).toFloat(), 1.5f);
        }
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, numberText, grid.number, juce::Justification::centredLeft);
        auto nameCell = grid.name;
        if (link != 0)
        {
            // The link mark right after the name, where the eye already is.
            const int nameW = juce::jmin (nameCell.getWidth() - 22, Dine::textWidth (Dine::text (12.5f, selected ? 600 : 500), name));
            Dine::drawLinkGlyph (g, nameCell.withTrimmedLeft (nameW + 6).removeFromLeft (16).toFloat(), Dine::accent);
        }
        g.setColour (mute ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (12.5f, selected ? 600 : 500));
        Dine::drawText (g, name, nameCell, juce::Justification::centredLeft, true);
        if (mute)
        {
            const int w = juce::jmin (nameCell.getWidth(), Dine::textWidth (Dine::text (12.5f, 500), name));
            g.setColour (Dine::ink3);
            g.fillRect (nameCell.getX(), nameCell.getCentreY(), w, 1);
        }
        // gain staging
        if (grid.hasGain)
        {
            if (kind == Kind::Channel && advice.known)
                Dine::drawStatusChip (g, grid.gain.withSizeKeepingCentre (grid.gain.getWidth(), 17).toFloat(),
                                      gainAdviceChip (advice), gainAdviceColour (advice.level));
            else if (kind == Kind::Master)
            {
                g.setColour (Dine::ink3);
                g.setFont (Dine::mono (10.5f, 500));
                Dine::drawText (g, integratedText + " LUFS", grid.gain, juce::Justification::centredLeft);
            }
        }

        // level and pan readouts beside their controls
        g.setColour (mute ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        Dine::drawText (g, levelText, grid.value, juce::Justification::centredRight);
        if (kind == Kind::Channel && grid.hasPan)
        {
            const auto text = panText (pan.getValue());
            g.setColour (text == "C" ? Dine::ink4 : Dine::ink3);
            g.setFont (Dine::mono (10.0f, 500));
            Dine::drawText (g, text, grid.panRead, juce::Justification::centredRight);
        }

        // where it goes
        if (outText.isNotEmpty() && grid.hasOut)
        {
            g.setColour (tint().withAlpha (mute ? 0.5f : 0.9f));
            g.setFont (Dine::text (11.0f, 500));
            Dine::drawText (g, sentenceCase (outText), grid.out, juce::Justification::centredRight, true);
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
        auto r = getLocalBounds().withTrimmedTop (3 + 6).withTrimmedBottom (12).reduced (padX, 0);
        col.body = r;

        // The v3 strip (`Channel Strip`, 63:9488): the number and the name, the gain chip,
        // three insert lamps, two send rows, the pan knob and its readout, then the throw.
        // No section captions: the design says INSERTS and SENDS by what they look like, and a
        // caption on a 70 pt column is three quarters of the column.
        // The strip is read as blocks, not as lines: the name, what the preamp is doing, the
        // chain, the sends, the pan, the throw. A 7 pt gap between blocks and 2 between rows
        // made all six of them one stack of small type, so the gap between blocks is wider
        // than the gap inside one and the eye can find the block it wants.
        const int gap = 11;
        const int nameH = kCaptionH + 18;
        const int gainH = kind == Kind::Master ? 0 : 14;
        const int loudH = kind == Kind::Master ? 56 + 10 + 4 * 21 : 0;
        const int inserts = kind == Kind::Master ? 0 : 3;
        const int sends = showSends && kind != Kind::Master ? 2 : 0;
        const int insertsH = inserts > 0 ? inserts * kSlotH + (inserts - 1) * kRowGapIn : 0;
        const int sendsH = sends > 0 ? sends * kSlotH + (sends - 1) * kRowGapIn : 0;
        const int panH = kind != Kind::Master ? 26 + 2 + 12 : 0;
        const int levelH = 14, peakH = kind == Kind::Master ? 0 : 13;
        // A third key row for FX, full width under M and S. It is RESERVED on every strip, not
        // only the ones that show it: this one function is what makes INSERTS, SENDS, PAN and
        // the faders line up straight across the console, and a key block that is a row taller
        // on the voice channels would bend that line.
        const int keysH = kind == Kind::Master ? 0 : 3 * 22 + 2 * 4;
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
            for (int i = 0; i < inserts; ++i) { col.insertRows.push_back (block.removeFromTop (kSlotH)); block.removeFromTop (kRowGapIn); }
            col.hasInserts = true;
            r.removeFromTop (gap);
        }
        if (keepSends)
        {
            auto block = r.removeFromTop (sendsH);
            for (int i = 0; i < sends; ++i) { col.sendRows.push_back (block.removeFromTop (kSlotH)); block.removeFromTop (kRowGapIn); }
            col.hasSends = true;
            r.removeFromTop (gap);
        }
        if (keepPan)
        {
            auto block = r.removeFromTop (panH);
            col.panBar = block.removeFromTop (26).withSizeKeepingCentre (26, 26);
            block.removeFromTop (2);
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
        const int faderW = narrow ? 20 : 24;
        const int meterW = kind == Kind::Master ? 8 : 6;
        // The gutter between them is where the scale is written, so it is a real gap rather
        // than a seam: the numbers are a ruler, and a ruler needs room.
        const int gutter = juce::jlimit (6, 22, r.getWidth() - faderW - meterW - 4);
        const int meterBlock = kind == Kind::Master ? meterW * 2 + 2 : meterW;
        auto pair = body.withSizeKeepingCentre (faderW + gutter + meterBlock, body.getHeight());
        col.fader = pair.removeFromLeft (faderW);
        col.scale = pair.removeFromLeft (gutter);
        col.meter = pair.removeFromLeft (meterW);
        if (kind == Kind::Master) { pair.removeFromLeft (2); col.meterRight = pair.removeFromLeft (meterW); }
        col.body = juce::Rectangle<int> (col.fader.getX(), col.fader.getY(), col.meter.getRight() - col.fader.getX(), col.fader.getHeight());
    }

    void layoutColumn()
    {
        buildColumn();
        meter.setBounds (col.meter);
        meterRight.setVisible (kind == Kind::Master && ! col.meterRight.isEmpty());
        if (meterRight.isVisible()) meterRight.setBounds (col.meterRight);
        fader.setBounds (col.fader);
        pan.setStyle (PanBar::Style::Knob);
        pan.setVisible (col.hasPan && kind == Kind::Channel);
        if (pan.isVisible()) pan.setBounds (col.panBar);

        const bool hasFxKey = kind == Kind::Channel && controller.stripCanHaveEffects (strip);
        if (col.keys.isEmpty())
        {
            muteButton.setVisible (false);
            soloButton.setVisible (false);
            armButton.setVisible (false);
            monitorButton.setVisible (false);
            fxButton.setVisible (false);
            return;
        }
        muteButton.setVisible (kind != Kind::Master);
        soloButton.setVisible (kind != Kind::Master);
        armButton.setVisible (kind == Kind::Channel);
        monitorButton.setVisible (kind == Kind::Channel);
        fxButton.setVisible (hasFxKey);

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
            if (hasFxKey)
            {
                keys.removeFromTop (gap);
                fxButton.setBounds (keys.removeFromTop (22));
            }
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
        const auto grid = rowGrid (getLocalBounds());
        // LEVEL ARRIVING: the meter, lying down
        meter.setBounds (grid.meter.withSizeKeepingCentre (grid.meter.getWidth(), 7));
        fader.setBounds (grid.fader.withSizeKeepingCentre (grid.fader.getWidth(), 16));
        valueRect = grid.value;
        pan.setStyle (PanBar::Style::Bar);
        pan.setVisible (kind == Kind::Channel && grid.hasPan);
        pan.setBounds (grid.pan.withSizeKeepingCentre (grid.pan.getWidth(), 16));
        panReadRect = grid.panRead;
        // The FX cell is reserved on every row whether or not this channel has the key, because
        // a console whose M and S do not line up across the rows is worse than a gap.
        const bool hasFxKey = kind == Kind::Channel && controller.stripCanHaveEffects (strip);
        auto keys = grid.keys.withSizeKeepingCentre (grid.keys.getWidth(), 22);
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
        if (hasFxKey) { keys.removeFromLeft (4); fxButton.setBounds (keys.removeFromLeft (30)); }
        fxButton.setVisible (hasFxKey);
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
            // WHAT THIS MICROPHONE IS DOING. A handheld is the pastor's in the sermon and the
            // worship leader's in the last song, and those are not the same channel. One press
            // moves it to the right group and gives it the profile's starting point for the
            // job - a starting point, not a preset: the next TUNE plans it as what it now is.
            if (controller.isVoiceChannel (controller.inputOfStrip (strip)))
            {
                juce::PopupMenu jobs;
                const auto now = controller.getSession().inputs[size_t (controller.inputOfStrip (strip))].role;
                int id = 200;
                for (const auto& job : MixController::voiceJobs())
                {
                    jobs.addItem (id++, juce::String (job.name), ! controller.isLiveSafe(),
                                  roleFamily (job.role) == roleFamily (now));
                }
                m.addSubMenu ("This microphone is", jobs);
            }
            m.addSeparator();
            m.addItem (3, "Set to record");
            m.addItem (4, "Monitoring: Input");
            m.addItem (5, "Monitoring: Auto");
            m.addItem (6, "Monitoring: Off");
            m.addSeparator();
            // Linked faders: pick the channels this one should move with. A member already in
            // the group is ticked, and choosing it again takes it out.
            const int group = controller.getStripLink (strip);
            juce::PopupMenu link;
            const auto& inputs = controller.getSession().inputs;
            const int count = juce::jmin (controller.getKept().numStrips, int (inputs.size()));
            for (int i = 0; i < count; ++i)
            {
                if (i == strip) continue;
                const int g = controller.getStripLink (i);
                const bool together = group != 0 && g == group;
                link.addItem (300 + i, juce::String (i + 1).paddedLeft ('0', 2) + "  " + juce::String (inputs[size_t (i)].name)
                                           + (g != 0 && ! together ? "   (linked elsewhere)" : juce::String()), true, together);
            }
            m.addSubMenu (group != 0 ? "Linked faders  " + juce::String (Glyph::dot()) + "  " + juce::String (controller.linkedNames (strip))
                                     : juce::String ("Link fader with"), link, count > 1);
            if (group != 0) m.addItem (9, "Unlink this fader");
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
                             if (chosen >= 200 && chosen < 300)
                             {
                                 const auto& jobs = MixController::voiceJobs();
                                 const size_t which = size_t (chosen - 200);
                                 if (which < jobs.size()
                                     && s.controller.setInputRole (s.controller.inputOfStrip (s.strip),
                                                                   s.controller.roleForJob (s.controller.inputOfStrip (s.strip), jobs[which].role)))
                                     s.services.reconfigure();     // the graph changed, exactly as an assignment does
                                 return;
                             }
                             if (chosen >= 300)
                             {
                                 const int other = chosen - 300;
                                 const int group = s.controller.getStripLink (s.strip);
                                 if (group != 0 && s.controller.getStripLink (other) == group) s.controller.unlinkStrip (other);
                                 else s.controller.linkStrips ({ s.strip, other });
                                 return;
                             }
                             switch (chosen)
                             {
                                 case 9: s.controller.unlinkStrip (s.strip); break;
                                 case 1: if (s.tune) s.tune(); break;
                                 case 2: if (s.open) s.open(); break;
                                 case 3: s.armButton.triggerClick(); break;
                                 case 4: case 5: case 6:
                                 {
                                     auto& project = s.services.daw().getProject();
                                     if (s.strip < 0 || s.strip >= int (project.tracks.size())) break;
                                     project.tracks[size_t (s.strip)].monitor = chosen == 4 ? MonitorMode::Input : chosen == 5 ? MonitorMode::Auto : MonitorMode::Off;
                                     s.services.daw().refresh();
                                     s.services.touchSession();
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
                             fader, scale, meter, meterRight, body, level, peak, keys, out;
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
                 grText { Glyph::dash() }, targetText { "-23 LUFS" }, loudnessNote { "not measured yet" };
    bool loudnessOnTarget = false, truePeakOver = false, limiterHot = false;
    int link = 0, linkNow = 0;            // StripParameters::linkGroup, as last drawn / as read this tick
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
    Size wantedSize = Size::Normal;      // what the page asked for
    Dine::Icon icon;
    DineMeter meter, meterRight;
    juce::Slider fader;
    PanBar pan;
    DineKey muteButton, soloButton, armButton, monitorButton, fxButton;
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
        // THE GUTTER BETWEEN STRIPS HAS TO BE DARKER THAN A STRIP, or the space between two
        // columns is not space - it is the same plane with nothing drawn on it. The console
        // stands on the deepest ground in the product; the list does not, because a list's
        // rows are separated by their own hairlines and not by air.
        g.fillAll (list ? Dine::window : Dine::menubar);
        if (! list) return;
        // The same grid the rows are laid out on, so a heading is always over its column.
        const auto grid = rowGrid (getLocalBounds().withHeight (kListHeadH));
        // Sentence case: the only capitals in this product are its verbs.
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, "#", grid.number, juce::Justification::centredLeft);
        Dine::drawText (g, "Name", grid.name, juce::Justification::centredLeft, true);
        if (grid.hasGain) Dine::drawText (g, "Gain staging", grid.gain, juce::Justification::centredLeft, true);
        Dine::drawText (g, "Level arriving", grid.meter, juce::Justification::centredLeft, true);
        Dine::drawText (g, "Fader", grid.fader, juce::Justification::centredLeft, true);
        Dine::drawText (g, "dB", grid.value, juce::Justification::centredRight);
        if (grid.hasPan) Dine::drawText (g, "Balance", grid.pan, juce::Justification::centred, true);
        Dine::drawText (g, "R " + Glyph::dot() + " A " + Glyph::dot() + " M " + Glyph::dot() + " S " + Glyph::dot() + " FX",
                        grid.keys, juce::Justification::centred);
        if (grid.hasOut) Dine::drawText (g, "Group", grid.out, juce::Justification::centredRight, true);
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

    windowButton.setButtonText ({});
    windowButton.setIcon (Dine::Icon::WindowNav);
    windowButton.setPadX (7);
    windowButton.setTooltip ("Open the console in a new window: put it on a second screen and keep the timeline "
                             "in front of you.");
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

    // The console's order, not the enum's: a bank reads DRUMS BASS MUSIC LEAD BGV SPEECH
    // AMBIENCE, which is where an engineer looks for each of them.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const auto bus = mixBusInDisplayOrder (b);
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
                                  false, r.inputB >= 0, hasSampleStage (r.role));
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
    // The console's own ground, behind the bank and the master, so the gutters between the
    // strips read as gutters all the way across the row rather than only inside the bank.
    if (view == View::Strips)
    {
        g.setColour (Dine::menubar);
        g.fillRect (getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedBottom (footHeight()));
    }
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
        Dine::drawFittedText (g, "Assign inputs first. The console fills with a strip for each one, then the group buses and the master.",
                          empty.removeFromTop (60), juce::Justification::topLeft, 3);
    }
}

// ------------------------------------------------------------------- layout
void MixerPage::resized()
{
    auto head = getLocalBounds().removeFromTop (kHeaderH).reduced (kPadX, 0);
    auto controls = head.withSizeKeepingCentre (head.getWidth(), Dine::Metric::control);

    // left to right, in the design's order (`08 - Mixer`, 75:12328): what is on the console,
    // how it is laid out, how wide a strip is, and whether the sends have their rows.
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
    seg (showTabs.data(), 3, 48);
    seg (viewTabs.data(), 2, 52);
    if (view == View::Strips)
    {
        seg (sizeTabs.data(), 3, 28);
        sendsButton.setBounds (left.removeFromLeft (juce::jmax (60, sendsButton.idealWidth())).reduced (0, 1));
        left.removeFromLeft (10);
    }

    auto right = controls;
    if (windowButton.isVisible())
    {
        // An icon at the very right, the way the design puts it: the words are in its tooltip
        // and in the View menu, and the row is worth more than they are.
        windowButton.setBounds (right.removeFromRight (30).reduced (0, 1));
        right.removeFromRight (10);
    }
    clearSolos.setBounds (right.removeFromRight (juce::jmax (84, clearSolos.idealWidth())).reduced (0, 1));

    chainStrip.setBounds (getLocalBounds().removeFromBottom (footHeight()));

    if (view == View::Strips) layoutStrips();
    else                      layoutList();
    repaint();
}

int MixerPage::busStripCount() const
{
    int n = 0;
    for (const auto& s : strips)
        if (s->getKind() == Strip::Kind::Bus && s->isVisible()) ++n;
    return n;
}

MixerPage::Strip* MixerPage::masterStrip() const
{
    for (auto& s : strips)
        if (s->getKind() == Strip::Kind::Master) return s.get();
    return nullptr;
}

// THE GROUPS SIT WHERE THEY ARE PATCHED, and the master is the only fixed column.
//
// The groups were pinned in a rail of their own beside the master for a while. They are not
// any more: a console reads left to right, and a second fixed column at the right turned the
// one thing an engineer scans - the run of strips - into two lists with a seam between them.
// A group bus now sits at the end of the family that feeds it, the way it is patched, and
// scrolls with everything else. The GROUPS filter is what shows them on their own.
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
        const auto bus = b < int (MixBus::Master) ? mixBusInDisplayOrder (b) : MixBus::Master;
        for (auto& s : strips)
        {
            if (s->getBus() != bus || s.get() == master) continue;
            if (s->getParentComponent() != bank.get()) bank->addAndMakeVisible (*s);
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
    for (auto& s : strips)
        if (s->getKind() == Strip::Kind::Bus && s->getParentComponent() != bank.get())
            bank->addAndMakeVisible (*s);
    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedBottom (footHeight()).reduced (kPadX, 0));

    // The narrowest a row can be and still be a row: the grid above gives its cells up in
    // order down to this, and below it the console is honestly too narrow. It used to be 760,
    // which ran the keys and the group off the right-hand edge of a 720 pt window.
    const int w = juce::jmax (540, viewport.getMaximumVisibleWidth());
    int y = kListHeadH;
    for (int b = 0; b <= int (MixBus::Master); ++b)
    {
        const auto bus = b < int (MixBus::Master) ? mixBusInDisplayOrder (b) : MixBus::Master;
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
