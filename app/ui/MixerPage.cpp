#include "MixerPage.h"
#include "Core/DbUtils.h"
#include "DSP/ChannelParameters.h"
#include "UI/Widgets.h"
#include "DSP/EqResponse.h"
#include <cmath>
#include <cstring>
#include <type_traits>

namespace livemix
{

namespace
{
    constexpr int kHeaderH   = 52;                        // v4: the title and the tool row
    constexpr int kPadX      = 18;
    // Between columns. Enough that two strips are two things: at 3 the console read as one
    // wall of faders, and on a busy service the eye has to find a strip before it can move it.
    constexpr int kStripGap  = 8;
    constexpr int kRowH      = 46;      // list view
    constexpr int kRowGap    = 3;
    constexpr int kListHeadH = 30;
    constexpr int kMasterW   = 150;     // the pinned master card
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
        // The v4 strip is 86 pt (docs/design/v4); S and L are either side of it.
        return s == MixerPage::Size::Narrow ? 70 : s == MixerPage::Size::Wide ? 110 : 86;
    }

    // v4's console: each family on a card of its own, the strips edge to edge inside it.
    constexpr int kCardHead  = 28;      // the family's name and "8 + bus", under a 3 pt line
    constexpr int kCardGap   = 10;      // between two family cards
    constexpr int kRailW     = 300;     // the quick inspector down the right

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

    // The v4 chip says the verdict and, where the desk has something to do, by how much:
    // "Healthy", "Clipping -6", "Digital +12", "Low +6". The number is what the preamp should
    // still move (InputAdvice::consoleMoveDb), signed the way the knob turns.
    juce::String gainAdviceChip (const MixController::InputAdvice& a)
    {
        using Level = MixController::InputAdvice::Level;
        juce::String word;
        switch (a.level)
        {
            case Level::Clipping: word = "Clipping"; break;
            case Level::Faint:    word = "Faint"; break;
            case Level::Low:      word = "Low"; break;
            case Level::Hot:      word = "Hot"; break;
            case Level::Digital:  word = "Digital"; break;
            case Level::NotHeard: return "Not heard";
            case Level::Healthy:  return "Healthy";
            case Level::Bleed:    return "Spill";
            default:              return {};
        }
        const int move = juce::roundToInt (a.consoleMoveDb);
        if (move == 0) return word;
        return word + " " + (move > 0 ? juce::String ("+") : Glyph::minus()) + juce::String (std::abs (move));
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
            case FxSlot::BandHall:    return "Band";
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

    // A group's name as people say it: "Drums", "Speech" - and "BGV", an initialism, whole.
    juce::String groupLabel (MixBus b)
    {
        const juce::String raw (mixBusName (b));
        return raw.length() <= 3 ? raw.toUpperCase() : sentenceCase (raw);
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
    // A Return is one effect return - the plate, the delay, the hall - on its own fader, after
    // the groups and before the master, where a console puts its returns.
    enum class Kind { Channel, Bus, Master, Return };
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
        if (kind == Kind::Return) { slot = strip; strip = -1; }
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
            else if (kind == Kind::Return) controller.setFxSlotReturn (FxSlot (slot), db);
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
        muteButton.setTooltip (kind == Kind::Bus ? "Muted: the whole group is not heard"
                               : kind == Kind::Return ? "Muted: this effect is not heard. The others, and what is sent to it, stay as they are"
                                                      : "Muted: signal arrives, it is not heard");
        soloButton.setTooltip (kind == Kind::Bus ? "Soloed: this group and nothing else"
                               : kind == Kind::Return ? "Soloed: this effect on its own, in your listen only"
                                                      : "Soloed: this and nothing else");
        if (kind == Kind::Return)
            fader.setTooltip ("This effect's own level, on top of what TUNE MIX set for it. The FX fader on TUNE and LIVE "
                              "still moves every effect together. Double-click for 0.0 dB.");
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
            else if (kind == Kind::Bus) controller.setGroupMuted (bus, ! controller.isGroupMuted (bus));
            else if (kind == Kind::Return) controller.setFxSlotMute (FxSlot (slot), ! controller.getBase().fx[size_t (slot)].mute);
        };
        soloButton.onClick = [this]
        {
            if (kind == Kind::Channel) controller.setStripSolo (strip, ! controller.getBase().strips[size_t (strip)].solo);
            else if (kind == Kind::Bus) controller.setBusSolo (bus, ! controller.getBase().buses[size_t (bus)].solo);
            else if (kind == Kind::Return) controller.setFxSolo (FxSlot (slot), ! controller.getBase().fx[size_t (slot)].solo);
        };
        fxButton.onClick = [this]
        {
            if (kind == Kind::Channel) controller.setStripEffects (strip, ! controller.stripEffectsOn (strip));
        };

        setTooltip (kind == Kind::Return ? name + "  " + Glyph::dot() + "  double-click to set how it sounds"
                                         : name + "  " + Glyph::dot() + "  " + source);

        numberText = kind == Kind::Channel ? juce::String (stripIndex + 1).paddedLeft ('0', 2)
                   : kind == Kind::Bus ? "BUS" : kind == Kind::Return ? "FX" : juce::String();
        outText = kind == Kind::Channel ? juce::String (mixBusName (bus)).toUpperCase()
                : kind == Kind::Bus || kind == Kind::Return ? "MASTER" : juce::String();
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
        setOpaque (l == Layout::Column && kind != Kind::Master);
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
            peak = controller.stripPeakDb (strip);
            hold = m.getMaxRmsDb();
            clipped = DineMeter::isClip (peak);
            // A stereo strip is metered as a pair (v4), so one side going is seen - each side
            // from its own peak since the last tick, so a short one is not missed.
            if (stereo && m.getNumChannels() > 1)
            {
                const float right = controller.stripPeakDb (strip, 1);
                meterRight.setLevels (juce::jmax (right, -120.0f), m.getRmsDb (1), DineMeter::isClip (right));
                peak = juce::jmax (controller.stripPeakDb (strip, 0), -120.0f);
                hold = m.getRmsDb (0);
                clipped = DineMeter::isClip (peak);
            }
        }
        else if (kind == Kind::Return)
        {
            const auto& fp = state.fx[size_t (slot)];
            faderDb = juce::jmax (fp.returnDb, -60.0f);
            muted = fp.mute;
            soloed = fp.solo;
            const auto& m = controller.getEngine().getFx (FxSlot (slot)).getOutputMeter();
            peak = controller.fxPeakDb (FxSlot (slot));
            hold = m.getMaxRmsDb();
            clipped = DineMeter::isClip (peak);
        }
        else
        {
            faderDb = state.buses[size_t (bus)].faderDb;
            muted = controller.isGroupMuted (bus);
            soloed = state.buses[size_t (bus)].solo;
            channel = &state.buses[size_t (bus)].channel;
            const auto& m = controller.getEngine().getBus (bus).getOutputMeter();
            peak = controller.busPeakDb (bus);
            hold = m.getMaxRmsDb();
            clipped = DineMeter::isClip (peak);
            // The master is metered in stereo, because a broadcast that has gone mono, or one
            // side that has gone, is the one thing a single bar cannot say.
            if (kind == Kind::Master && m.getNumChannels() > 1)
            {
                const float right = controller.busPeakDb (bus, 1);
                meterRight.setLevels (juce::jmax (right, -120.0f), m.getRmsDb (1), DineMeter::isClip (right));
                peak = juce::jmax (controller.busPeakDb (bus, 0), -120.0f);
                hold = m.getRmsDb (0);
                clipped = DineMeter::isClip (peak);
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
        // Why a strip is silent: muted is NOT HEARD; something else soloed is SOLOED OUT (in the
        // engineer's listen only - solo never changes what the room hears).
        {
            bool soloedOut = false;
            if (controller.anySolo() && ! soloed && kind != Kind::Master)
            {
                if (kind == Kind::Channel) soloedOut = ! state.buses[size_t (bus)].solo;
                else if (kind == Kind::Bus)
                {
                    soloedOut = true;
                    for (int i = 0; i < state.numStrips && i < controller.getGraph().numStrips(); ++i)
                        if (controller.getGraph().strips[size_t (i)].bus == bus && state.strips[size_t (i)].solo) soloedOut = false;
                }
                else soloedOut = true;
            }
            const juce::String nextTag = muted ? juce::String ("NOT HEARD") : soloedOut ? juce::String ("SOLOED OUT") : juce::String();
            if (nextTag != tagText) { tagText = nextTag; body = true; }
        }
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

    juce::Colour tint() const noexcept { return kind == Kind::Master ? Dine::ink2 : kind == Kind::Return ? Dine::keyFx : Dine::busTint (bus); }

    // A strip's own plane: the family card's ground lifted by white at .027, more when it is
    // the one picked out. The card is drawn by the bank behind; the strip is opaque on it.
    juce::Colour stripGround() const
    {
        if (kind == Kind::Master) return Dine::raised;
        return Dine::console.overlaidWith (juce::Colours::white.withAlpha (selected ? 0.08f : 0.027f));
    }

    // A slot (v4): 19 tall, 5 pt corners. Used - white at .07 and a lamp; reserved and empty -
    // a dashed outline, so the line across the console never bends.
    // What a used insert or send slot says: a lamp and the words, the first of `forms` that fits
    // at full width. Where none does with the lamp, the lamp gives its room to the words - a
    // slot is read for what it holds, and a letter is never squeezed to make it fit.
    static void drawSlotText (juce::Graphics& g, juce::Rectangle<int> area, std::initializer_list<juce::String> forms)
    {
        const auto font = Dine::text (10.5f);
        const int withLamp = area.getWidth() - 7 - 5 - 5 - 3, without = area.getWidth() - 6 - 3;
        juce::String text = *forms.begin();
        bool lamp = true;
        bool found = false;
        for (const auto& f : forms) if (Dine::textWidth (font, f) <= withLamp) { text = f; found = true; break; }
        if (! found)
        {
            lamp = false;
            for (const auto& f : forms) if (Dine::textWidth (font, f) <= without) { text = f; found = true; break; }
            if (! found) text = *(forms.end() - 1);
        }
        if (lamp)
        {
            area.removeFromLeft (7);
            g.setColour (Dine::accent);
            g.fillEllipse (area.removeFromLeft (5).withSizeKeepingCentre (5, 5).toFloat());
            area.removeFromLeft (5);
        }
        else area.removeFromLeft (6);
        g.setColour (Dine::ink.withAlpha (0.78f));
        g.setFont (font);
        Dine::drawFittedText (g, text, area.withTrimmedRight (3), juce::Justification::centredLeft, 1);
    }

    void drawSlot (juce::Graphics& g, juce::Rectangle<int> row, bool used) const
    {
        if (row.isEmpty()) return;
        const auto box = row.toFloat();
        if (used) Dine::fillRounded (g, box, juce::Colours::white.withAlpha (0.07f), 5.0f);
        else
        {
            juce::Path outline;
            outline.addRoundedRectangle (box.reduced (0.5f), 5.0f);
            juce::Path dashed;
            const float dashes[] = { 3.0f, 2.5f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillPath (dashed);
        }
    }

    void paintColumn (juce::Graphics& g)
    {
        if (kind == Kind::Master)
        {
            Dine::fillRounded (g, getLocalBounds().toFloat(), stripGround(), 12.0f);
            paintMaster (g);
            return;
        }
        g.fillAll (stripGround());

        // ---- the head: the number, then the name, both centred
        {
            g.setColour (Dine::ink3);
            g.setFont (kind == Kind::Channel ? Dine::mono (10.0f) : Dine::text (10.0f));
            Dine::drawText (g, kind == Kind::Channel ? juce::String (strip + 1) : kind == Kind::Bus ? juce::String ("Bus") : juce::String ("Return"),
                            col.number, juce::Justification::centred);
            const int markW = link != 0 ? 14 : 0;
            const auto nameFont = Dine::text (size == Size::Narrow ? 11.5f : 12.0f, 600);
            auto head = col.name.withTrimmedRight (markW);
            g.setColour (mute ? Dine::ink3 : Dine::ink);
            g.setFont (nameFont);
            // A name is what the strip is for: squeezed a little before it is ever cut.
            Dine::drawFittedText (g, name, head, juce::Justification::centred, 1, 0.78f);
            if (mute)
            {
                const int w = juce::jmin (head.getWidth(), Dine::textWidth (nameFont, name));
                g.setColour (Dine::ink3);
                g.fillRect (head.getCentreX() - w / 2, head.getCentreY(), w, 1);
            }
            if (link != 0) Dine::drawLinkGlyph (g, col.name.withTrimmedLeft (col.name.getWidth() - markW).toFloat(), Dine::accent);
        }

        // ---- gain health: always in its slot, so the console's line never bends
        if (col.hasGain)
        {
            const auto text = advice.known ? gainAdviceChip (advice) : juce::String();
            using Level = MixController::InputAdvice::Level;
            const bool bad = advice.known && advice.level != Level::Healthy && advice.level != Level::Unknown;
            const auto tintNow = advice.known ? gainAdviceColour (advice.level) : Dine::ink4;
            Dine::fillRounded (g, col.gain.toFloat(), bad ? tintNow.withAlpha (0.20f) : juce::Colours::white.withAlpha (0.04f), 6.0f);
            if (text.isNotEmpty())
            {
                g.setColour (bad ? tintNow.brighter (0.25f) : Dine::ink3);
                g.setFont (Dine::text (10.0f, 600));
                Dine::drawFittedText (g, text, col.gain.reduced (4, 0), juce::Justification::centred, 1, 0.8f);
            }
        }

        // ---- the inserts
        if (col.hasInserts)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (9.5f));
            Dine::drawText (g, "Inserts", col.insertsLabel, juce::Justification::centredLeft);
            for (size_t i = 0; i < col.insertRows.size(); ++i)
            {
                auto area = col.insertRows[i];
                const bool used = i < insertList.size();
                drawSlot (g, area, used);
                if (! used) continue;
                drawSlotText (g, area, { sentenceCase (insertList[i].label) });
            }
        }

        // ---- the sends: the return and how far down it is sent, "Plate -22"
        if (col.hasSends)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (9.5f));
            Dine::drawText (g, "Sends", col.sendsLabel, juce::Justification::centredLeft);
            for (size_t i = 0; i < col.sendRows.size(); ++i)
            {
                auto area = col.sendRows[i];
                const bool used = i < sendList.size();
                drawSlot (g, area, used);
                if (! used) continue;
                // "Plate -22", or on a strip too narrow for both, the return's name alone.
                drawSlotText (g, area, { sendList[i].label + " " + Glyph::minus() + juce::String (juce::roundToInt (std::fabs (sendList[i].db))),
                                         sendList[i].label });
            }
        }

        // ---- the balance, and what it reads
        if (col.hasPan)
        {
            const auto text = kind == Kind::Channel ? panText (pan.getValue()) : juce::String ("C");
            g.setColour (Dine::ink.withAlpha (0.4f));
            g.setFont (Dine::mono (9.5f));
            Dine::drawText (g, text, col.panRead, juce::Justification::centred);
        }

        // ---- not heard / soloed out: why a strip with signal on it is silent in the room
        if (col.hasTag && tagText.isNotEmpty())
        {
            const auto font = Dine::caps (9.0f, 0.02f, 700);
            const int w = juce::jmin (col.tag.getWidth(), Dine::textWidth (font, tagText) + 10);
            const auto pill = col.tag.withSizeKeepingCentre (w, col.tag.getHeight());
            Dine::fillRounded (g, pill.toFloat(), juce::Colours::white.withAlpha (0.08f), pill.getHeight() * 0.5f);
            g.setColour (tagText == "SOLOED OUT" ? Dine::keySolo : Dine::ink2);
            g.setFont (font);
            Dine::drawFittedText (g, tagText, pill, juce::Justification::centred, 1, 0.8f);
        }

        // ---- the unity line across the fader and the meters
        {
            const float t = float (fader.valueToProportionOfLength (0.0));
            const float y = float (col.fader.getY()) + float (col.fader.getHeight()) * (1.0f - t);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.fillRect (float (col.body.getX()), y - 0.5f, float (col.body.getWidth()), 1.0f);
        }

        // ---- the level and the peak, side by side
        {
            auto row = col.level;
            g.setColour (mute ? Dine::ink3 : Dine::ink);
            g.setFont (Dine::mono (12.5f, 600));
            const int levelW = Dine::textWidth (Dine::mono (12.5f, 600), levelText);
            if (col.hasPeak)
            {
                Dine::drawText (g, levelText, row.removeFromLeft (juce::jmin (row.getWidth(), levelW)), juce::Justification::centredLeft);
                g.setColour (Dine::ink3);
                g.setFont (Dine::mono (9.5f));
                Dine::drawFittedText (g, peakText, row, juce::Justification::centredRight, 1, 0.8f);
            }
            else Dine::drawText (g, levelText, row, juce::Justification::centred);
        }

        // ---- where this strip goes
        if (col.hasOut)
        {
            g.setColour (Dine::ink.withAlpha (mute ? 0.25f : 0.4f));
            g.setFont (Dine::text (10.0f));
            Dine::drawFittedText (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + (outText.length() <= 3 ? outText : sentenceCase (outText)), col.out,
                                  juce::Justification::centred, 1, 0.8f);
        }
    }

    // The master (v4): its own card - "Master", the integrated loudness against the target in
    // a big number, short-term and true peak under it, a bar for how near the target it is,
    // then the fader and its two meters, the level, and where it goes out.
    void paintMaster (juce::Graphics& g)
    {
        Dine::hairlineRounded (g, getLocalBounds().toFloat().reduced (0.5f), juce::Colours::white.withAlpha (0.06f), 11.5f);
        auto r = col.loudness;
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.0f, 600));
        Dine::drawText (g, "Master", r.removeFromTop (18), juce::Justification::centredLeft);
        r.removeFromTop (8);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.0f));
        Dine::drawText (g, "Integrated " + Glyph::dot() + " target " + targetText.upToFirstOccurrenceOf (" ", false, false),
                        r.removeFromTop (13), juce::Justification::centredLeft, true);
        {
            auto big = r.removeFromTop (30);
            const auto bigFont = Dine::mono (26.0f, 600);
            const int w = juce::jmin (big.getWidth(), Dine::textWidth (bigFont, integratedText));
            g.setColour (loudnessOnTarget ? Dine::ink : Dine::warn);
            g.setFont (bigFont);
            Dine::drawText (g, integratedText, big.removeFromLeft (w), juce::Justification::centredLeft);
            big.removeFromLeft (5);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f));
            Dine::drawText (g, "LUFS", big.withTrimmedTop (8), juce::Justification::centredLeft, true);
        }
        r.removeFromTop (6);
        auto pair = r.removeFromTop (28);
        auto cellPair = [&g] (juce::Rectangle<int> c, const juce::String& k, const juce::String& v, juce::Colour ink)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (9.5f));
            Dine::drawText (g, k, c.removeFromTop (12), juce::Justification::centredLeft, true);
            g.setColour (ink);
            g.setFont (Dine::mono (12.0f, 600));
            Dine::drawText (g, v, c, juce::Justification::centredLeft, true);
        };
        const int half = pair.getWidth() / 2;
        cellPair (pair.removeFromLeft (half), "Short-term", shortTermText, Dine::ink);
        cellPair (pair, "True peak", truePeakText, truePeakOver ? Dine::crit : Dine::ink);
        r.removeFromTop (8);
        // How near the target: a bar from -40 LUFS to 0, with the target as a tick.
        {
            auto bar = r.removeFromTop (4).toFloat();
            Dine::fillRounded (g, bar, juce::Colours::white.withAlpha (0.10f), 2.0f);
            const float lufs = integratedText == Glyph::dash() ? -60.0f : integratedText.replaceCharacter (juce::juce_wchar (0x2212), '-').getFloatValue();
            const float p = juce::jlimit (0.0f, 1.0f, (lufs + 40.0f) / 40.0f);
            if (p > 0.0f) Dine::fillRounded (g, bar.withWidth (bar.getWidth() * p), loudnessOnTarget ? Dine::accent : Dine::warn, 2.0f);
            const float tp = juce::jlimit (0.0f, 1.0f, (targetText.getFloatValue() + 40.0f) / 40.0f);
            g.setColour (Dine::ink);
            g.fillRect (bar.getX() + bar.getWidth() * tp - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        }

        // the unity line, the level, and where it goes out
        {
            const float t = float (fader.valueToProportionOfLength (0.0));
            const float y = float (col.fader.getY()) + float (col.fader.getHeight()) * (1.0f - t);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.fillRect (float (col.body.getX()), y - 0.5f, float (col.body.getWidth()), 1.0f);
        }
        g.setColour (Dine::ink);
        g.setFont (Dine::mono (12.5f, 600));
        Dine::drawText (g, levelText + " dB", col.level, juce::Justification::centred);
        if (col.hasOut)
        {
            auto out = col.out.reduced (6, 0);
            Dine::fillRounded (g, out.toFloat(), juce::Colours::white.withAlpha (0.05f), out.getHeight() * 0.5f);
            auto inner = out.reduced (10, 0);
            g.setColour (Dine::ok);
            g.fillEllipse (inner.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            inner.removeFromLeft (6);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (10.5f));
            Dine::drawFittedText (g, source, inner, juce::Justification::centredLeft, 1, 0.8f);
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
    // v4's strip, top to bottom (docs/design/v4, 86 pt): the number, the name, the gain chip,
    // "Inserts" and three slots, "Sends" and two, the pan bar and its reading, a tag line, the
    // throw, the level and the peak, R A / M S, and where it goes. One place decides it, on the
    // same grid for every width, so every section lines up straight across the console; a
    // short window drops sections in a fixed order rather than squeezing the fader.
    void buildColumn()
    {
        col = Col {};
        const bool narrow = size == Size::Narrow;
        const int padX = narrow ? 4 : 6;
        auto r = getLocalBounds().reduced (padX, 0);

        if (kind == Kind::Master)
        {
            r = getLocalBounds().reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (12);
            col.loudness = r.removeFromTop (18 + 8 + 13 + 30 + 6 + 28 + 8 + 4);
            col.hasLoudness = true;
            col.out = r.removeFromBottom (24); col.hasOut = true;
            r.removeFromBottom (10);
            col.level = r.removeFromBottom (16);
            r.removeFromBottom (8);
            r.removeFromTop (18);
            auto body = r;
            const int faderW = 24, meterW = 8;
            auto pair = body.withSizeKeepingCentre (faderW + 26 + meterW * 2 + 3, body.getHeight());
            col.fader = pair.removeFromLeft (faderW);
            col.scale = pair.removeFromLeft (26);
            col.meter = pair.removeFromLeft (meterW);
            pair.removeFromLeft (3);
            col.meterRight = pair.removeFromLeft (meterW);
            col.body = juce::Rectangle<int> (col.fader.getX(), col.fader.getY(), col.meterRight.getRight() - col.fader.getX(), col.fader.getHeight());
            return;
        }

        const int inserts = 3;
        const int sends = showSends ? 2 : 0;
        const int topH = 8 + 11 + 2 + 15;                         // number, name
        const int gainH = 8 + 20;
        const int insertsH = 6 + 11 + 3 + inserts * 22 - 3;
        const int sendsH = sends > 0 ? 6 + 14 + 3 + sends * 22 - 3 : 0;
        const int panH = 9 + 10 + 1 + 11;
        const int tagH = 4 + 14;
        const int bottomH = 16 + 15 + 6 + 2 * 22 + 3 + 6 + 12 + 12;   // level, keys, out
        const int bodyMin = 90;

        bool keepSends = sends > 0, keepInserts = true, keepPan = true, keepTag = true, keepGain = true, keepPeak = ! narrow;
        auto total = [&] { return topH + (keepGain ? gainH : 0) + (keepInserts ? insertsH : 0) + (keepSends ? sendsH : 0)
                                  + (keepPan ? panH : 0) + (keepTag ? tagH : 0) + bottomH + 16; };
        if (r.getHeight() - total() < bodyMin) keepTag = false;
        if (r.getHeight() - total() < bodyMin) keepSends = false;
        if (r.getHeight() - total() < bodyMin) keepInserts = false;
        if (r.getHeight() - total() < bodyMin) keepPan = false;
        if (r.getHeight() - total() < bodyMin) keepGain = false;

        r.removeFromTop (8);
        col.number = r.removeFromTop (11);
        r.removeFromTop (2);
        col.name = r.removeFromTop (15);
        if (keepGain && kind != Kind::Return)
        {
            r.removeFromTop (8);
            col.gain = r.removeFromTop (20);
            col.hasGain = kind == Kind::Channel || kind == Kind::Bus;
        }
        else if (keepGain) r.removeFromTop (28);
        if (keepInserts)
        {
            r.removeFromTop (6);
            col.insertsLabel = r.removeFromTop (11);
            r.removeFromTop (3);
            for (int i = 0; i < inserts; ++i) { col.insertRows.push_back (r.removeFromTop (19)); if (i < inserts - 1) r.removeFromTop (3); }
            col.hasInserts = true;
        }
        if (keepSends)
        {
            r.removeFromTop (6);
            col.sendsLabel = r.removeFromTop (14);
            r.removeFromTop (3);
            for (int i = 0; i < sends; ++i) { col.sendRows.push_back (r.removeFromTop (19)); if (i < sends - 1) r.removeFromTop (3); }
            col.hasSends = true;
        }
        if (keepPan)
        {
            r.removeFromTop (9);
            col.panBar = r.removeFromTop (10).reduced (juce::jmax (0, (r.getWidth() - 65) / 2), 0);
            r.removeFromTop (1);
            col.panRead = r.removeFromTop (11);
            col.hasPan = true;
        }
        if (keepTag) { r.removeFromTop (4); col.tag = r.removeFromTop (14); col.hasTag = true; }

        r.removeFromBottom (12);
        col.out = r.removeFromBottom (12); col.hasOut = true;
        r.removeFromBottom (6);
        col.keys = r.removeFromBottom (2 * 22 + 3);
        r.removeFromBottom (6);
        col.level = r.removeFromBottom (15);
        col.hasPeak = keepPeak;
        r.removeFromBottom (16);

        // the throw: the fader on the left of the meters, the scale between them
        auto body = r.withTrimmedTop (10);
        const int faderW = narrow ? 20 : 24;
        const int meterW = 5;
        const int meters = stereo ? 2 : 1;
        const int meterBlock = meterW * meters + (meters - 1) * 2;
        const int gutter = juce::jlimit (6, 22, r.getWidth() - faderW - meterBlock - 8);
        auto pair = body.withSizeKeepingCentre (faderW + gutter + meterBlock, body.getHeight());
        col.fader = pair.removeFromLeft (faderW);
        col.scale = pair.removeFromLeft (gutter);
        col.meter = pair.removeFromLeft (meterW);
        if (meters > 1) { pair.removeFromLeft (2); col.meterRight = pair.removeFromLeft (meterW); }
        col.body = juce::Rectangle<int> (getLocalBounds().getX() + padX, col.fader.getY(),
                                         getWidth() - 2 * padX, col.fader.getHeight());
    }

    void layoutColumn()
    {
        buildColumn();
        meter.setBounds (col.meter);
        meterRight.setVisible (! col.meterRight.isEmpty());
        if (meterRight.isVisible()) meterRight.setBounds (col.meterRight);
        fader.setBounds (col.fader);
        pan.setStyle (PanBar::Style::Bar);
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
        const int gap = 3;
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
            // FX sits at the right of the "Sends" caption (v4 has no third key row): effects
            // on this microphone are about its sends, and a voice strip keeps its key without
            // every strip on the console growing a row for it.
            if (hasFxKey && ! col.sendsLabel.isEmpty())
                fxButton.setBounds (col.sendsLabel.withTrimmedLeft (col.sendsLabel.getWidth() - 26));
            else fxButton.setVisible (false);
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
        if (kind == Kind::Return)
        {
            // A return is its fader and its two keys - and the sound it makes, one sheet away.
            juce::PopupMenu m;
            m.addSectionHeader ("EFFECT " + juce::String (Glyph::dot()) + " " + name);
            m.addItem (1, "Set how it sounds" + juce::String (Glyph::ellip()), open != nullptr);
            juce::Component::SafePointer<Strip> self (this);
            m.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (this),
                             [self] (int r) { if (self != nullptr && r == 1 && self->open) self->open(); });
            return;
        }
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
        juce::Rectangle<int> number, name, gain, loudness, insertsLabel, sendsLabel, panLabel, panBar, panRead, tag,
                             fader, scale, meter, meterRight, body, level, peak, keys, out;
        std::vector<juce::Rectangle<int>> insertRows, sendRows;
        bool hasLoudness = false, hasInserts = false, hasSends = false, hasPan = false,
             hasOut = false, hasGain = false, hasPeak = false, hasTag = false;
    };
    struct SendView { juce::String label; float db = kSilenceDb; };

    MixController& controller;
    AppServices& services;
    Kind kind;
    MixBus bus;
    ChannelRole role;
    int strip = -1;
    int slot = -1;                 // the FxSlot, on a Return
    juce::String name, source, levelText { "+0.0" }, peakText { Glyph::dash() }, numberText, outText, tagText;
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
    // A family card (v4): the group's own colour as a 3 pt line along the top, its name and
    // "8 + bus" under it, and the strips edge to edge on the card's ground.
    struct Card { juce::Rectangle<int> r; juce::Colour tint; juce::String title, sub; };

    Bank() { setOpaque (true); }
    void setList (bool l) { list = l; repaint(); }
    void setCards (std::vector<Card> c) { cards = std::move (c); repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::window);
        if (! list)
        {
            for (const auto& c : cards)
            {
                const auto r = c.r.toFloat();
                Dine::fillRounded (g, r, Dine::console, 12.0f);
                {
                    juce::Graphics::ScopedSaveState keep (g);
                    juce::Path clip;
                    clip.addRoundedRectangle (r, 12.0f);
                    g.reduceClipRegion (clip);
                    g.setColour (c.tint);
                    g.fillRect (r.withHeight (3.0f));
                }
                Dine::hairlineRounded (g, r.reduced (0.5f), juce::Colours::white.withAlpha (0.06f), 11.5f);
                auto head = c.r.withTrimmedTop (3).withHeight (kCardHead - 3).reduced (10, 0);
                const auto font = Dine::text (11.5f, 600);
                const int w = juce::jmin (head.getWidth(), Dine::textWidth (font, c.title));
                g.setColour (c.tint);
                g.setFont (font);
                Dine::drawText (g, c.title, head.removeFromLeft (w), juce::Justification::centredLeft);
                head.removeFromLeft (6);
                if (Dine::textWidth (Dine::text (10.5f), c.sub) <= head.getWidth())
                {
                    g.setColour (Dine::ink3);
                    g.setFont (Dine::text (10.5f));
                    Dine::drawText (g, c.sub, head, juce::Justification::centredLeft);
                }
            }
            return;
        }
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
    std::vector<Card> cards;
};

// ------------------------------------------------------------------ the quick inspector
// v4's right rail on the console: the picked-out channel or group in a glance - its name and
// where it comes from, who set it and when, its EQ as a curve, its compressor as three dials,
// its sends - and the two ways further: the Inspector, and TUNE CHANNEL. It reads; it never
// edits. Refreshed a few times a second, and only repainted when what it shows has changed.
class MixerPage::QuickInspector : public juce::Component
{
public:
    QuickInspector (MixController& c, AppServices& s) : controller (c), services (s)
    {
        openButton.setFontPx (12.0f);
        openButton.setTooltip ("Every stage of this channel, in full.");
        tuneButton.setCaps (true);
        tuneButton.setFontPx (11.0f);
        tuneButton.setTooltip ("DINE listens to this channel alone and sets its chain. You hear BEFORE and AFTER, then keep it or not.");
        addAndMakeVisible (openButton);
        addAndMakeVisible (tuneButton);
        setOpaque (true);
    }

    DineButton openButton { "Open in Inspector", DineButton::Style::Standard };
    DineButton tuneButton { "TUNE CHANNEL", DineButton::Style::Filled };

    void show (int stripIndex, MixBus busIndex) { strip = stripIndex; bus = busIndex; update(); }

    void update()
    {
        if (! controller.isPrepared()) return;
        const auto& state = controller.getBase();
        const auto& graph = controller.getGraph();
        View next;
        const ChannelParameters* p = nullptr;
        if (strip >= 0 && strip < state.numStrips && strip < graph.numStrips())
        {
            const auto& r = graph.strips[size_t (strip)];
            next.tint = Dine::busTint (r.bus);
            next.title = juce::String (r.name);
            next.sub = "Input " + juce::String (r.inputA + 1) + (r.inputB >= 0 ? "-" + juce::String (r.inputB + 1) : juce::String())
                     + " " + Glyph::dot() + " " + Dine::friendlyRoleName (r.role) + " " + Glyph::dot() + " "
                     + groupLabel (r.bus);
            next.provenance = provenanceFor (strip);
            p = &state.strips[size_t (strip)].channel;
            const auto& sends = state.strips[size_t (strip)].sendDb;
            for (int f = 0; f < int (FxSlot::Count); ++f)
                if (graph.fxUsed[size_t (f)]) next.sends.push_back ({ juce::String (fxName (FxSlot (f))), sends[size_t (f)] });
        }
        else if (bus != MixBus::Count)
        {
            next.tint = Dine::busTint (bus);
            next.title = bus == MixBus::Master ? juce::String ("Master") : groupLabel (bus) + " bus";
            next.sub = bus == MixBus::Master ? juce::String ("Everything the room and the stream hear")
                                             : juce::String (graph.stripsOnBus (bus)) + " inputs " + Glyph::dot() + " into the master";
            p = &state.buses[size_t (bus)].channel;
        }
        else next.title = {};

        if (p != nullptr)
        {
            next.eqOn = p->correctiveEqEnabled || p->toneEqEnabled || p->hpfEnabled || p->lpfEnabled;
            next.compOn = p->compEnabled;
            next.threshold = p->compThresholdDb;
            next.ratio = p->compRatio;
            next.makeup = p->compMakeupDb;
            const double sr = juce::jmax (8000.0, services.sampleRate());
            for (int i = 0; i < kPoints; ++i)
            {
                const float hz = 20.0f * std::pow (10.0f, 3.0f * float (i) / float (kPoints - 1));
                next.curve[size_t (i)] = EqResponse::chainMagnitudeDb (*p, sr, hz);
            }
            auto addBands = [&next] (const auto& bands, bool on)
            {
                if (! on) return;
                for (const auto& b : bands) if (b.enabled) next.bands.push_back ({ b.freqHz, b.gainDb });
            };
            addBands (p->correctiveBands, p->correctiveEqEnabled);
            addBands (p->toneBands, p->toneEqEnabled);
        }
        openButton.setVisible (next.title.isNotEmpty());
        tuneButton.setVisible (strip >= 0 && next.title.isNotEmpty());
        if (next == view) return;
        view = std::move (next);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Dine::rail);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (getLocalBounds().removeFromLeft (1));
        auto r = getLocalBounds().reduced (16, 0).withTrimmedTop (18);
        if (view.title.isEmpty())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.5f));
            Dine::drawFittedText (g, "Click a strip to see it here: its EQ, its compressor and its sends. Double-click it for the Inspector.",
                                  r.removeFromTop (60), juce::Justification::topLeft, 3, 1.0f);
            return;
        }

        // the name, with the group's lamp, and where it comes from
        {
            auto line = r.removeFromTop (24);
            g.setColour (view.tint);
            g.fillRoundedRectangle (line.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat(), 2.5f);
            line.removeFromLeft (10);
            g.setColour (Dine::ink);
            g.setFont (Dine::text (18.0f, 700));
            Dine::drawFittedText (g, view.title, line, juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            Dine::drawFittedText (g, view.sub, r.removeFromTop (16).withTrimmedLeft (20), juce::Justification::centredLeft, 1, 0.8f);
        }
        if (view.provenance.isNotEmpty())
        {
            r.removeFromTop (12);
            auto line = r.removeFromTop (16);
            g.setColour (Dine::ink);
            g.fillEllipse (line.removeFromLeft (6).withSizeKeepingCentre (5, 5).toFloat());
            line.removeFromLeft (8);
            g.setColour (Dine::ink.withAlpha (0.8f));
            g.setFont (Dine::text (11.5f));
            Dine::drawFittedText (g, view.provenance, line, juce::Justification::centredLeft, 1, 0.8f);
        }
        r.removeFromTop (14);

        // EQ: the whole chain's response as a curve, with a dot on every band that is on
        {
            auto card = r.removeFromTop (172);
            drawCard (g, card, "EQ", view.eqOn ? "Clarity" : "Off");
            auto plot = card.reduced (12).withTrimmedTop (24).withTrimmedBottom (14);
            Dine::fillRounded (g, plot.toFloat(), Dine::deep, 6.0f);
            const auto xFor = [plot] (float hz) { return float (plot.getX()) + float (plot.getWidth()) * std::log10 (hz / 20.0f) / 3.0f; };
            const auto yFor = [plot] (float db) { return float (plot.getCentreY()) - juce::jlimit (-18.0f, 18.0f, db) / 18.0f * float (plot.getHeight()) * 0.45f; };
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            for (float hz : { 50.0f, 200.0f, 1000.0f, 5000.0f }) g.fillRect (xFor (hz), float (plot.getY()), 1.0f, float (plot.getHeight()));
            g.fillRect (float (plot.getX()), yFor (0.0f), float (plot.getWidth()), 1.0f);
            juce::Path curve, fill;
            for (int i = 0; i < kPoints; ++i)
            {
                const float x = float (plot.getX()) + float (plot.getWidth()) * float (i) / float (kPoints - 1);
                const float y = yFor (view.curve[size_t (i)]);
                if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, float (plot.getBottom())); }
                else curve.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (float (plot.getRight()), float (plot.getBottom()));
            fill.closeSubPath();
            g.setColour (Dine::accent.withAlpha (view.eqOn ? 0.14f : 0.05f));
            g.fillPath (fill);
            g.setColour (view.eqOn ? Dine::ink : Dine::ink3);
            g.strokePath (curve, juce::PathStrokeType (1.4f));
            g.setColour (Dine::monitor);
            for (const auto& b : view.bands)
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ xFor (juce::jlimit (20.0f, 20000.0f, b.first)), yFor (b.second) }));
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (9.0f));
            auto scale = card.reduced (12).removeFromBottom (12);
            const char* labels[] = { "50", "200", "1k", "5k", "20k" };
            const float freqs[] = { 50.0f, 200.0f, 1000.0f, 5000.0f, 20000.0f };
            for (int i = 0; i < 5; ++i)
            {
                const int x = juce::roundToInt (xFor (freqs[i]));
                Dine::drawText (g, labels[i], juce::Rectangle<int> (juce::jlimit (scale.getX(), scale.getRight() - 24, x - 12), scale.getY(), 24, 12),
                                juce::Justification::centred);
            }
        }
        r.removeFromTop (12);

        // Compressor: three dials, the value under each and what it is under that
        {
            auto card = r.removeFromTop (130);
            drawCard (g, card, "Compressor", view.compOn ? "Steady" : "Off");
            auto row = card.reduced (12).withTrimmedTop (28);
            const int w = row.getWidth() / 3;
            dial (g, row.removeFromLeft (w), juce::jlimit (0.0f, 1.0f, (view.threshold + 60.0f) / 60.0f),
                  db1 (view.threshold) + " dB", "Threshold");
            dial (g, row.removeFromLeft (w), juce::jlimit (0.0f, 1.0f, (view.ratio - 1.0f) / 9.0f),
                  juce::String (view.ratio, 1) + ":1", "Ratio");
            dial (g, row, juce::jlimit (0.0f, 1.0f, view.makeup / 24.0f), db1 (view.makeup) + " dB", "Make-up");
        }

        // Sends: one line per effect return the session has
        if (! view.sends.empty())
        {
            r.removeFromTop (12);
            auto card = r.removeFromTop (44 + 30 * int (view.sends.size()));
            drawCard (g, card, "Sends", {});
            auto rows = card.reduced (12).withTrimmedTop (30);
            for (const auto& sv : view.sends)
            {
                auto line = rows.removeFromTop (30);
                g.setColour (Dine::ink.withAlpha (0.85f));
                g.setFont (Dine::text (12.0f));
                Dine::drawText (g, sv.label, line.removeFromLeft (52), juce::Justification::centredLeft, true);
                auto value = line.removeFromRight (64);
                g.setColour (Dine::ink2);
                g.setFont (Dine::mono (11.0f));
                Dine::drawText (g, sv.db <= kSilenceDb ? juce::String ("Off") : db1 (sv.db) + " dB", value, juce::Justification::centredRight);
                auto bar = line.reduced (6, 0).withSizeKeepingCentre (line.getWidth() - 12, 4).toFloat();
                Dine::fillRounded (g, bar, juce::Colours::white.withAlpha (0.10f), 2.0f);
                const float amount = sv.db <= kSilenceDb ? 0.0f : juce::jlimit (0.0f, 1.0f, float (sendPercent (sv.db)) / 100.0f);
                if (amount > 0.0f) Dine::fillRounded (g, bar.withWidth (bar.getWidth() * amount), Dine::ink.withAlpha (0.85f), 2.0f);
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0).withTrimmedTop (18);
        r.removeFromTop (24 + 16 + (view.provenance.isNotEmpty() ? 28 : 0) + 14 + 172 + 12 + 130
                         + (view.sends.empty() ? 0 : 12 + 44 + 30 * int (view.sends.size())) + 18);
        auto row = r.removeFromTop (32);
        const int w = (row.getWidth() - 8) / 2;
        openButton.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (8);
        tuneButton.setBounds (row);
    }

private:
    static constexpr int kPoints = 64;
    struct SendLine { juce::String label; float db; bool operator== (const SendLine& o) const { return label == o.label && juce::exactlyEqual (db, o.db); } };
    struct View
    {
        juce::String title, sub, provenance;
        juce::Colour tint;
        bool eqOn = false, compOn = false;
        float threshold = 0.0f, ratio = 1.0f, makeup = 0.0f;
        std::array<float, kPoints> curve {};
        std::vector<std::pair<float, float>> bands;
        std::vector<SendLine> sends;
        bool operator== (const View& o) const
        {
            return title == o.title && sub == o.sub && provenance == o.provenance && tint == o.tint && eqOn == o.eqOn
                && compOn == o.compOn && juce::exactlyEqual (threshold, o.threshold) && juce::exactlyEqual (ratio, o.ratio)
                && juce::exactlyEqual (makeup, o.makeup) && curve == o.curve && bands == o.bands && sends == o.sends;
        }
    };

    // "Tuned by DINE at 9:41 - one hand edit": the newest tune on this channel and the edits since.
    juce::String provenanceFor (int s) const
    {
        const auto& records = controller.getStripHistory (s);
        long long tunedAt = 0;
        int edits = 0;
        for (auto it = records.rbegin(); it != records.rend(); ++it)
        {
            if (juce::String (it->what).startsWith ("TUNE")) { tunedAt = it->whenMs; break; }
            ++edits;
        }
        if (tunedAt <= 0 && edits == 0) return "Not tuned yet";
        juce::String text = tunedAt > 0 ? "Tuned by DINE at " + juce::Time (tunedAt).formatted ("%H:%M") : juce::String ("Set by hand");
        if (edits > 0) text += " " + Glyph::dot() + " " + (edits == 1 ? juce::String ("one hand edit") : juce::String (edits) + " hand edits");
        return text;
    }

    static void drawCard (juce::Graphics& g, juce::Rectangle<int> card, const juce::String& title, const juce::String& word)
    {
        Dine::fillRounded (g, card.toFloat(), Dine::card, 12.0f);
        Dine::hairlineRounded (g, card.toFloat().reduced (0.5f), juce::Colours::white.withAlpha (0.06f), 11.5f);
        auto head = card.reduced (12, 0).withTrimmedTop (10).withHeight (18);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f, 600));
        Dine::drawText (g, title, head, juce::Justification::centredLeft);
        if (word.isNotEmpty())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f));
            Dine::drawText (g, word, head, juce::Justification::centredRight);
        }
    }

    static void dial (juce::Graphics& g, juce::Rectangle<int> cell, float amount, const juce::String& value, const juce::String& what)
    {
        auto knob = cell.removeFromTop (44).withSizeKeepingCentre (40, 40).toFloat().reduced (2.0f);
        const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
        juce::Path track, arc;
        track.addCentredArc (knob.getCentreX(), knob.getCentreY(), knob.getWidth() * 0.5f, knob.getHeight() * 0.5f, 0.0f, start, end, true);
        arc.addCentredArc (knob.getCentreX(), knob.getCentreY(), knob.getWidth() * 0.5f, knob.getHeight() * 0.5f, 0.0f, start, start + (end - start) * amount, true);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Dine::ink);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        cell.removeFromTop (4);
        g.setColour (Dine::ink);
        g.setFont (Dine::mono (11.0f, 600));
        Dine::drawFittedText (g, value, cell.removeFromTop (14), juce::Justification::centred, 1, 0.8f);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.0f));
        Dine::drawText (g, what, cell.removeFromTop (13), juce::Justification::centred, true);
    }

    MixController& controller;
    AppServices& services;
    int strip = -1;
    MixBus bus = MixBus::Count;
    View view;
};

// ------------------------------------------------------------------ MixerPage
MixerPage::MixerPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    bank = std::make_unique<Bank>();
    viewport.setViewedComponent (bank.get(), false);
    viewport.setScrollBarsShown (false, true);
    // Not opaque: the scrollbar's strip is painted by nothing, and an opaque viewport over it
    // left whatever the window held there - a white band under the console.
    viewport.setOpaque (false);
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

    const char* showNames[4] = { "All", "Inputs", "Groups", "Effects" };
    for (int i = 0; i < 4; ++i)
    {
        segment (showTabs[size_t (i)], showNames[i], 10);
        showTabs[size_t (i)]->onClick = [this, i] { setShow (Show (i)); };
        addAndMakeVisible (*showTabs[size_t (i)]);
    }
    showTabs[1]->setTooltip ("Only the sources.");
    showTabs[2]->setTooltip ("Only the group buses and the master.");
    showTabs[3]->setTooltip ("Only the effect returns: the reverbs and the delay, each on its own fader.");

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
    windowButton.setTooltip ("Open the console in a new window: put it on a second screen and keep the timeline "
                             "in front of you.");
    windowButton.onClick = [this] { if (onOpenWindow) onOpenWindow(); };
    addAndMakeVisible (windowButton);

    railButton.setFontPx (12.0f);
    railButton.setPadX (10);
    railButton.setIcon (Dine::Icon::Sidebar);
    railButton.setTooltip ("Show or hide the selected channel's details down the right (])");
    railButton.onClick = [this] { setRailShown (! railWanted); };
    addAndMakeVisible (railButton);

    tuneChannelButton.setCaps (true);
    tuneChannelButton.setFontPx (11.0f);
    tuneChannelButton.setTooltip ("Tune the channel you picked: DINE listens to it alone and sets its chain (T).");
    tuneChannelButton.onClick = [this]
    {
        if (selected >= 0) { if (onTuneStrip) onTuneStrip (selected); }
        else if (onToast) onToast ("Pick a channel first: click its strip, then TUNE CHANNEL.");
    };
    addAndMakeVisible (tuneChannelButton);

    rail = std::make_unique<QuickInspector> (controller, services);
    rail->openButton.onClick = [this]
    {
        if (selected >= 0) { if (onOpenStrip) onOpenStrip (selected); }
        else if (selectedBusValue != MixBus::Count && onOpenBus) onOpenBus (selectedBusValue);
    };
    rail->tuneButton.onClick = [this] { if (selected >= 0 && onTuneStrip) onTuneStrip (selected); };
    addAndMakeVisible (*rail);

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

void MixerPage::setRailShown (bool v)
{
    if (railWanted == v) return;
    railWanted = v;
    railButton.setToggleState (v, juce::dontSendNotification);
    railSlide.setOpen (v);          // slides in and out the way the sidebar does
}

// The rail needs a console beside it worth having: three strips and the master at least.
bool MixerPage::railFits() const noexcept
{
    return view == View::Strips && getWidth() >= kRailW + kMasterW + 3 * 86 + 60;
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
    if (v == View::List) bank->setCards ({});
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
    switch (show)
    {
        case Show::Inputs:  return s.getKind() == Strip::Kind::Channel;
        case Show::Groups:  return s.getKind() == Strip::Kind::Bus || s.getKind() == Strip::Kind::Master;
        case Show::Effects: return s.getKind() == Strip::Kind::Return;
        case Show::All:     break;
    }
    return true;
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
                                              groupLabel (bus),
                                              juce::String (graph.stripsOnBus (bus))
                                                  + (graph.stripsOnBus (bus) == 1 ? " source" : " sources"));
            s->setOpenHandler ([this, bus] { if (onOpenBus) onOpenBus (bus); });
            s->setSelectHandler ([this, bus] { selectBus (bus); });
            add (std::move (s));
        }
    }

    // The returns, one strip each, after the last group: a console's returns are their own faders.
    if (controller.isPrepared())
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (controller.getEngine().isFxUsed (FxSlot (f)))
            {
                auto s = std::make_unique<Strip> (controller, services, Strip::Kind::Return, MixBus::Master, ChannelRole::KickIn, f,
                                                  // "Backing Hall" is wider than a column; BGV is what the group is called here
                                                  FxSlot (f) == FxSlot::BgvHall ? juce::String ("BGV Hall") : juce::String (fxSlotName (FxSlot (f))),
                                                  "Effect return");
                s->setOpenHandler ([this, f] { if (onOpenReturn) onOpenReturn (FxSlot (f)); });
                add (std::move (s));
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
    if (rail != nullptr && rail->isVisible() && (tick % 6) == 0) rail->update();
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

    if (rail != nullptr) rail->show (selected, selectedBusValue);
    tuneChannelButton.setEnabled (selected >= 0);
    for (auto& s : strips)
        s->setSelected (selected >= 0 ? s->getStripIndex() == selected
                                      : selectedBusValue != MixBus::Count && s->getKind() != Strip::Kind::Channel
                                        && s->getKind() != Strip::Kind::Return && s->getBus() == selectedBusValue);
}

void MixerPage::updateControls()
{
    for (int i = 0; i < 2; ++i) viewTabs[size_t (i)]->setToggleState (int (view) == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
        sizeTabs[size_t (i)]->setToggleState (int (stripSize) == i, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
        showTabs[size_t (i)]->setToggleState (int (show) == i, juce::dontSendNotification);
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
    // the title, then the segment tracks behind their buttons
    {
        auto head = getLocalBounds().removeFromTop (kHeaderH).reduced (16, 0);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (15.0f, 700));
        Dine::drawText (g, "Mixer", head.removeFromLeft (titleW), juce::Justification::centredLeft);
    }
    auto trackFor = [&g] (juce::Component* first, juce::Component* last)
    {
        if (first == nullptr || ! first->isVisible()) return;
        Dine::drawSegmentTrack (g, first->getBounds().getUnion (last->getBounds()).expanded (2, 2));
    };
    trackFor (viewTabs[0].get(), viewTabs[1].get());
    trackFor (showTabs[0].get(), showTabs[3].get());
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
    // The rail runs the page's full height, beside the title row as well as the console.
    // While it slides the console gives way a step at a time, and the panel itself keeps its
    // full width and moves in from the edge - its contents never re-flow mid-slide.
    const int railW = railFits() ? railSlide.width (0, kRailW) : 0;
    const bool railWas = rail->isVisible();
    rail->setVisible (railW > 0);
    auto page = getLocalBounds();
    if (railW > 0)
    {
        const auto room = page.removeFromRight (railW);
        rail->setBounds (room.withWidth (kRailW));
        if (! railWas || ! railSlide.isMoving()) rail->update();
    }

    auto head = page.removeFromTop (kHeaderH).reduced (16, 0);
    auto controls = head.withSizeKeepingCentre (head.getWidth(), Dine::Metric::control);
    titleW = Dine::textWidth (Dine::text (15.0f, 700), "Mixer") + 4;

    // left to right, in v4's order: the title, how the console is laid out, what is on it,
    // how wide a strip is, and whether the sends have their rows.
    auto left = controls.withTrimmedLeft (titleW + 12);
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
    seg (viewTabs.data(), 2, 46);
    seg (showTabs.data(), 4, 40);
    if (view == View::Strips)
    {
        seg (sizeTabs.data(), 3, 28);
        sendsButton.setBounds (left.removeFromLeft (juce::jmax (60, sendsButton.idealWidth())).reduced (0, 1));
        left.removeFromLeft (10);
    }

    // From the right; a button with no room for its whole word is given no bounds at all,
    // never squeezed (TUNE CHANNEL is also on the strip's menu, on T and in the rail).
    auto right = controls.withLeft (left.getX());
    auto placeRight = [&right] (juce::Component& b, int w, int dy)
    {
        if (right.getWidth() < w) { b.setBounds ({}); return; }
        b.setBounds (right.removeFromRight (w).reduced (0, dy));
        right.removeFromRight (8);
    };
    railButton.setToggleState (railWanted, juce::dontSendNotification);
    placeRight (railButton, juce::jmax (84, railButton.idealWidth()), 1);
    placeRight (tuneChannelButton, juce::jmax (110, tuneChannelButton.idealWidth()), -2);
    if (windowButton.isVisible()) placeRight (windowButton, juce::jmax (96, windowButton.idealWidth()), -2);
    placeRight (clearSolos, juce::jmax (84, clearSolos.idealWidth()), 1);

    chainStrip.setBounds (page.removeFromBottom (footHeight()));
    pageArea = page;

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

int MixerPage::returnStripCount() const
{
    int n = 0;
    for (const auto& s : strips)
        if (s->getKind() == Strip::Kind::Return && s->isVisible()) ++n;
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
    auto area = pageArea.reduced (12, 0).withTrimmedBottom (12);
    const int h = juce::jmax (180, area.getHeight());
    const int stripH = juce::jmin (h, kMaxStripH) - kCardHead - 2;

    auto* master = masterStrip();
    if (master != nullptr && visibleInFilter (*master))
    {
        if (master->getParentComponent() != this) addAndMakeVisible (*master);
        master->setVisible (true);
        master->setBounds (area.removeFromRight (kMasterW).withHeight (stripH + kCardHead + 2));
        area.removeFromRight (kCardGap);
    }
    else if (master != nullptr) master->setVisible (false);

    viewport.setBounds (area);

    // One card per family, in the console's order, the group bus at the end of its family;
    // the effect returns on a card of their own after the last group.
    std::vector<Bank::Card> cards;
    int x = 0;
    auto family = [&] (auto belongs, juce::Colour tint, const juce::String& title)
    {
        int inputs = 0, buses = 0;
        const int start = x;
        for (auto& s : strips)
        {
            if (s.get() == master || ! belongs (*s)) continue;
            if (s->getParentComponent() != bank.get()) bank->addAndMakeVisible (*s);
            const bool wanted = visibleInFilter (*s);
            s->setVisible (wanted);
            if (! wanted) continue;
            if (s->getKind() == Strip::Kind::Bus) ++buses; else ++inputs;
            s->setBounds (x + 1, kCardHead + 1, s->columnWidth(), stripH);
            x += s->columnWidth();
        }
        if (x == start) return;
        cards.push_back ({ juce::Rectangle<int> (start, 0, x - start + 2, stripH + kCardHead + 2), tint, title,
                           inputs > 0 ? juce::String (inputs) + (buses > 0 ? " + bus" : juce::String())
                                      : buses > 0 ? juce::String ("bus") : juce::String() });
        x += 2 + kCardGap;
    };
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const auto bus = mixBusInDisplayOrder (b);
        family ([bus] (const Strip& s) { return s.getBus() == bus && s.getKind() != Strip::Kind::Return; },
                Dine::busTint (bus), groupLabel (bus));
    }
    family ([] (const Strip& s) { return s.getKind() == Strip::Kind::Return; }, Dine::keyFx, "Effects");
    bank->setCards (std::move (cards));
    // No trailing gap: a bank that exactly fits must not grow a scrollbar for the gap after its last card.
    bank->setSize (juce::jmax (x > 0 ? x - kCardGap : 0, viewport.getWidth()), juce::jmax (stripH + kCardHead + 2, viewport.getMaximumVisibleHeight()));
}

void MixerPage::layoutList()
{
    if (auto* master = masterStrip(); master != nullptr && master->getParentComponent() != bank.get())
        bank->addAndMakeVisible (*master);
    for (auto& s : strips)
        if (s->getKind() == Strip::Kind::Bus && s->getParentComponent() != bank.get())
            bank->addAndMakeVisible (*s);
    viewport.setBounds (pageArea.reduced (kPadX, 0));

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
