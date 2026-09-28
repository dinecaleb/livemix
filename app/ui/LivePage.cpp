#include "LivePage.h"

#include "native/MixHistory.h"
#include "OutputsSheet.h"
#include "UI/Widgets.h"
#include <cmath>
#include <limits>

namespace livemix
{

namespace
{
    constexpr int kGroupBuses = int (MixBus::Master);
    constexpr int kFxTile = kGroupBuses, kMasterTile = kGroupBuses + 1;
    constexpr int kTiles = kGroupBuses + 2;
    constexpr int kPadX = 24, kPadY = 22, kGap = 20;
    constexpr int kHeadH = 44;      // the page bar: ON AIR, and what part of the service this is
    constexpr int kStatusH = 114;   // tall enough for BROADCAST's number, its three readings and its target bar
    constexpr int kSafeW = 330;
    constexpr int kScenesH = 78;
    constexpr int kMacrosH = 96;
    // The monitor card's rows, measured once: the caption, a gap, the chips, a gap, the level, a gap, the sentence.
    constexpr int kMonCaption = 14, kMonCaptionGap = 12, kMonRowGap = 20, kMonNoteGap = 16, kMonNote = 18;

    juce::String dbText (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::String span (double seconds)
    {
        const int total = int (seconds);
        if (total >= 24 * 3600) return "a day or more";
        if (total >= 3600)      return juce::String (total / 3600) + " h " + juce::String ((total / 60) % 60) + " m";
        return juce::String (juce::jmax (0, total / 60)) + " m";
    }
}

// One group: its name in its colour, a state chip, a meter, the fader under it and its
// level, and the two keys that can change what the room hears.
// ------------------------------------------------------------------- MacroKnob
// One of the five macros, as the design draws it: a 270 degree arc, the number in the middle
// and the plain word under it. Turning one is a whole-mix move that TUNE MIX already decided
// the shape of - the macro leans the mix away from the plan, it does not replace it - so the
// centre of the arc is 50 and double-clicking comes back to it.
class LivePage::MacroKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    MacroKnob (MixMacro m, const juce::String& caption, std::function<void (float)> onSet)
        : macro (m), label (caption), set (std::move (onSet))
    {
        setTooltip (label + ": a whole-mix move. 50 is the mix TUNE MIX built; double-click comes back to it.");
        // A stroked arc is path rendering, and five of them on every full repaint of the
        // workspace is a page switch nobody asked to pay for. The knob only changes when its
        // value does, so it is cached and a repaint of the page is a blit.
        setBufferedToImage (true);
    }

    void setValue (float v)
    {
        if (std::fabs (v - value) < 0.05f) return;
        value = v;
        repaint();
    }
    float getValue() const noexcept { return value; }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto cap = area.removeFromBottom (14);
        auto dial = area.reduced (4).withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()) - 8,
                                                            juce::jmin (area.getWidth(), area.getHeight()) - 8);
        const auto centre = dial.toFloat().getCentre();
        const float radius = dial.getWidth() * 0.5f;
        constexpr float kSweep = 2.356194f;            // 135 degrees each side of the top
        const float angle = (value / 50.0f - 1.0f) * kSweep;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, -kSweep, kSweep, true);
        g.setColour (Dine::control);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (std::fabs (value - 50.0f) > 0.5f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f,
                               juce::jmin (0.0f, angle), juce::jmax (0.0f, angle), true);
            g.setColour (Dine::accent);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (std::fabs (value - 50.0f) > 0.5f ? Dine::ink : Dine::ink2);
        g.setFont (Dine::Type::monoValue());
        Dine::drawText (g, juce::String (juce::roundToInt (value)), dial, juce::Justification::centred);

        g.setColour (Dine::ink4);
        g.setFont (Dine::Type::labelSection());
        Dine::drawText (g, label, cap, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override { dragFrom = value; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        const float v = juce::jlimit (0.0f, 100.0f, dragFrom - float (e.getDistanceFromDragStartY()) * 0.5f);
        setValue (v);
        if (set) set (v);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setValue (50.0f);
        if (set) set (50.0f);
    }

private:
    MixMacro macro;
    juce::String label;
    std::function<void (float)> set;
    float value = 50.0f, dragFrom = 50.0f;
};

class LivePage::GroupTile : public juce::Component
{
public:
    GroupTile (MixController& c, int index) : controller (c), group (index)
    {
        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearHorizontal);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        Dine::dragOnly (fader);
        fader.setRange (-60.0, 12.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.getProperties().set ("dineFader", true);
        fader.setTooltip (isFx() ? "Level for every effect return together. Double-click for 0.0 dB, which is what TUNE MIX set."
                        : isMaster() ? "The master fader: everything the room and the stream hear. The readout beside the meter is the "
                                       "mix's integrated loudness. Double-click for 0.0 dB."
                                     : "Level for the whole group. Double-click for 0.0 dB.");
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (isFx()) controller.setFxReturn (float (fader.getValue()));
            else        controller.setBusFader (bus(), float (fader.getValue()));
            repaint (readout);
        };

        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        mute.setTooltip (isFx() ? "Mute the effects: the reverbs and delays leave the mix, the sources stay."
                       : isMaster() ? "Mute the master: nothing reaches the room or the stream until it is off."
                                    : "Muted: the whole group is not heard");
        // The master has nothing to solo against: its key is the loudness readout instead (painted).
        solo.setVisible (! isMaster());
        solo.setTooltip (isFx() ? "Soloed: just the reverbs and delays, so you hear what the sends are adding. Only you hear it."
                                : "Soloed: this group and nothing else");
        mute.onClick = [this]
        {
            if (isFx()) controller.setFxMute (! controller.getBase().fxMute);
            else        controller.setBusMute (bus(), ! controller.getBase().buses[size_t (bus())].mute);
        };
        solo.onClick = [this]
        {
            if (isFx()) controller.setFxSoloAll (! controller.anyFxSolo());
            else if (! isMaster()) controller.setBusSolo (bus(), ! controller.getBase().buses[size_t (bus())].solo);
        };
        for (auto* b : { &mute, &solo }) { b->setFontPx (10.0f); b->setPadX (4); b->setCaps (true); }
    }

    void refresh()
    {
        const auto& p = controller.getBase();
        float faderDb = 0.0f, peak = -120.0f;
        bool m = false, s = false, isUsed = true;
        if (isFx())
        {
            faderDb = p.fxReturnDb;
            m = p.fxMute;
            s = controller.anyFxSolo();
            int returns = 0;
            if (controller.isPrepared())
            {
                const auto& engine = controller.getEngine();
                for (int f = 0; f < int (FxSlot::Count); ++f)
                    if (engine.isFxUsed (FxSlot (f))) { ++returns; peak = juce::jmax (peak, engine.getFx (FxSlot (f)).getOutputMeter().consumeMaxPeakDb()); }
            }
            isUsed = returns > 0;
        }
        else
        {
            const auto& b = p.buses[size_t (bus())];
            faderDb = b.faderDb; m = b.mute; s = b.solo;
            isUsed = controller.isPrepared() && (isMaster() || controller.getEngine().isBusUsed (bus()));
            if (isUsed) peak = controller.getEngine().getBus (bus()).getOutputMeter().consumeMaxPeakDb();
        }
        if (isMaster())
        {
            // The master's readout beside its mute: where it is against the delivery target.
            const auto loud = controller.getMasterLoudness();
            const juce::String next = loud.known && loud.integratedLufs > -100.0f ? juce::String (loud.integratedLufs, 1) : juce::String (Glyph::dash());
            if (next != loudness) { loudness = next; repaint(); }
        }
        updating = true;
        if (! fader.isMouseButtonDown() && std::fabs (faderDb - float (fader.getValue())) > 0.01f)
        {
            fader.setValue (faderDb, juce::dontSendNotification);
            repaint (readout);
        }
        updating = false;
        meter.setLevels (peak, peak, peak > -0.2f);
        meter.setMuted (m || ! isUsed);
        if (m != muted || s != soloed || isUsed != used)
        {
            muted = m; soloed = s; used = isUsed;
            mute.setStyle (muted ? DineButton::Style::Filled : DineButton::Style::Standard);
            mute.setTint (Dine::keyMute);
            solo.setStyle (soloed ? DineButton::Style::Filled : DineButton::Style::Standard);
            solo.setTint (Dine::keySolo);
            fader.setEnabled (used);
            mute.setEnabled (used);
            solo.setEnabled (used);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        Dine::fillRounded (g, r.toFloat(), muted ? Dine::refuse : soloed ? Dine::soloGround : Dine::card, Dine::Radius::card);
        auto inner = r.reduced (16, 16);
        auto head = inner.removeFromTop (18);
        const juce::String state = ! used ? "OFF" : muted ? "NOT HEARD" : soloed ? "SOLO" : "ON";
        const auto chipFont = Dine::caps (10.0f, 0.08f, 500);
        const int chipW = Dine::textWidth (chipFont, state) + 14;
        // "ON" is the resting state and says little; on a narrow tile it gives its room to the name.
        // OFF, NOT HEARD and SOLO are news and always show.
        if ((state != "ON" || head.getWidth() >= 150) && ! (isMaster() && state == "ON"))
        {
            auto chip = head.removeFromRight (chipW).withSizeKeepingCentre (chipW, 18).toFloat();
            Dine::fillRounded (g, chip, muted ? Dine::keyMute : soloed ? Dine::accent : Dine::control, Dine::Radius::chip);
            g.setColour (muted || soloed ? Dine::onAccent : Dine::ink3);
            g.setFont (chipFont);
            Dine::drawText (g, state, chip, juce::Justification::centred);
            head.removeFromRight (6);
        }
        g.setColour (used ? tint() : Dine::ink4);
        g.setFont (Dine::caps (13.0f, 0.06f));
        Dine::drawText (g, name(), head, juce::Justification::centredLeft, true);

        if (isMaster())
        {
            // The master's level is its loudness: the integrated LUFS where the groups show their fader.
            g.setColour (Dine::ink4);
            g.setFont (Dine::caps (9.0f, 0.08f, 500));
            Dine::drawText (g, "LUFS", readout.translated (0, -13), juce::Justification::centredRight);
            g.setColour (Dine::ink2);
            g.setFont (Dine::mono (12.0f, 500));
            Dine::drawText (g, loudness, readout, juce::Justification::centredRight);
            return;
        }
        g.setColour (! used ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        Dine::drawText (g, used ? dbText (float (fader.getValue())) : Glyph::dash(), readout, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (16, 16);
        inner.removeFromTop (18 + 14);
        auto meterRow = inner.removeFromTop (juce::jmin (64, juce::jmax (30, inner.getHeight() - 14 - 14 - 10 - 30)));
        readout = meterRow.removeFromRight (44).withTrimmedTop (meterRow.getHeight() - 16);
        meterRow.removeFromRight (5);
        meter.setBounds (meterRow);
        inner.removeFromTop (10);
        fader.setBounds (inner.removeFromTop (14));
        inner.removeFromTop (10);
        auto keys = inner.removeFromTop (28);
        if (isMaster()) { mute.setBounds (keys); return; }     // nothing to solo against: MUTE has the row
        mute.setBounds (keys.removeFromLeft ((keys.getWidth() - 6) / 2));
        keys.removeFromLeft (6);
        solo.setBounds (keys);
    }

private:
    bool isFx() const noexcept { return group == kFxTile; }
    bool isMaster() const noexcept { return group == kMasterTile; }
    MixBus bus() const noexcept { return isMaster() ? MixBus::Master : MixBus (group); }
    juce::Colour tint() const { return isFx() ? Dine::ink2 : isMaster() ? Dine::ink : Dine::busTint (bus()); }
    juce::String name() const { return isFx() ? "FX RETURNS" : isMaster() ? "MASTER" : juce::String (mixBusName (bus())).toUpperCase(); }

    MixController& controller;
    int group;
    bool used = true, muted = false, soloed = false, updating = false;
    juce::String loudness;
    juce::Rectangle<int> readout;
    DineMeter meter { DineMeter::Style::Bar };
    juce::Slider fader;
    DineButton mute { "MUTE", DineButton::Style::Standard };
    DineButton solo { "SOLO", DineButton::Style::Standard };
};

// The transport's Record, on the Recording card: the one button that has to be found from
// across the desk. It carries the record dot and the only red on the page.
class LivePage::RecordKey : public juce::Button
{
public:
    RecordKey() : juce::Button ("Record")
    {
        setTooltip ("Record: captures every track with its R key on (R). LIVE SAFE never locks the transport.");
        setClickingTogglesState (false);
        setWantsKeyboardFocus (false);
    }

    void setRecording (bool r) { if (r != recording) { recording = r; repaint(); } }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, recording ? Dine::crit.withAlpha (down ? 1.0f : over ? 0.95f : 0.88f)
                                           : (down ? Dine::controlOn : over ? Dine::controlHot : Dine::control), Dine::Radius::control);
        auto inner = getLocalBounds().reduced (12, 0);
        const float d = 10.0f;
        auto dot = juce::Rectangle<float> (d, d).withCentre ({ float (inner.getX()) + d * 0.5f, r.getCentreY() });
        g.setColour (recording ? Dine::onAccent : Dine::crit);
        if (recording) g.fillRect (dot.reduced (1.5f));
        else           g.fillEllipse (dot);
        g.setColour (recording ? Dine::onAccent : Dine::ink);
        g.setFont (Dine::caps (11.0f, 0.06f));
        Dine::drawText (g, recording ? "STOP" : "RECORD", inner.withTrimmedLeft (int (d) + 8), juce::Justification::centredLeft, false);
    }

    int idealWidth() const { return 12 * 2 + 10 + 8 + Dine::textWidth (Dine::caps (11.0f, 0.06f), "RECORD"); }

private:
    bool recording = false;
};

LivePage::LivePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    for (int i = 0; i < kTiles; ++i)
    {
        tiles[size_t (i)] = std::make_unique<GroupTile> (controller, i);
        addAndMakeVisible (*tiles[size_t (i)]);
    }
    recordButton = std::make_unique<RecordKey>();
    addAndMakeVisible (*recordButton);
    recordButton->onClick = [this] { if (onToggleRecord) onToggleRecord(); };

    addAndMakeVisible (liveSafeButton);
    liveSafeButton.setButtonText ("LIVE SAFE OFF");
    liveSafeButton.setStyle (DineButton::Style::Standard);
    liveSafeButton.setCaps (true);
    liveSafeButton.setFontPx (13.0f);
    liveSafeButton.setClickingTogglesState (false);
    liveSafeButton.setTooltip (juce::String ("Lock the sound for the service. ") + liveSafe::lockedSummary() + " "
                               + liveSafe::allowedSummary());
    addAndMakeVisible (historyButton);
    historyButton.setFontPx (12.5f);
    historyButton.setQuiet (true);
    historyButton.setTooltip ("Every mix this session has had, by time and by name. Going back to one keeps where "
                              "you are now, so it is never a one-way door.");
    historyButton.onClick = [this] { if (onOpenHistory) onOpenHistory(); };

    liveSafeButton.onClick = [this]
    {
        auto& daw = services.daw();
        daw.setLiveSafe (! daw.isLiveSafe());
        if (onLiveSafeChanged) onLiveSafeChanged();
        if (onToast) onToast (daw.isLiveSafe() ? "LIVE SAFE on. The sound is locked: re-routes and re-tunes are blocked."
                                               : "LIVE SAFE off. Re-routes and re-tunes are allowed again.");
        refresh();
        repaint();
    };

    // ---- scenes: one press brings a whole mix back
    for (int i = 0; i < 4; ++i)
    {
        scenePads[size_t (i)] = std::make_unique<DineButton> (juce::String (defaultSceneName (i)), DineButton::Style::Toggle);
        scenePads[size_t (i)]->setFontPx (13.0f);
        scenePads[size_t (i)]->setClickingTogglesState (false);
        scenePads[size_t (i)]->setTooltip ("Bring this whole mix back - every fader, chain and macro - in one press. UNDO takes it back. LIVE SAFE never locks it.");
        scenePads[size_t (i)]->onClick = [this, i] { controller.recallScene (i); refreshScenes(); };
        addAndMakeVisible (*scenePads[size_t (i)]);
        sceneKeeps[size_t (i)] = std::make_unique<DineButton> ("Keep", DineButton::Style::Ghost);
        sceneKeeps[size_t (i)]->setFontPx (11.0f);
        sceneKeeps[size_t (i)]->setCaps (true);
        sceneKeeps[size_t (i)]->setTooltip ("Keep the mix as it is now under this name.");
        sceneKeeps[size_t (i)]->onClick = [this, i] { controller.keepScene (i); refreshScenes(); };
        addAndMakeVisible (*sceneKeeps[size_t (i)]);
    }
    refreshScenes();

    // ---- MACROS - WHOLE MIX: the same five MixMacro values the TUNE pads move, one knob
    // each, because during a service the question is "a bit more voice" and not "where on
    // the pad". Moving one here and moving it there are the same move.
    {
        const char* macroNames[int (MixMacro::Count)] = { "VOCALS", "DRUMS", "BASS", "SPACE", "ENERGY" };
        for (int i = 0; i < int (MixMacro::Count); ++i)
        {
            const auto m = MixMacro (i);
            macroKnobs[size_t (i)] = std::make_unique<MacroKnob> (m, macroNames[i],
                                                                  [this, m] (float v) { controller.setMacro (m, v); });
            addAndMakeVisible (*macroKnobs[size_t (i)]);
        }
        refreshMacros();
    }

    // ---- the engineer's own listen: six chips and a level
    const char* labels[6] = { "MONITOR SOLO", "SOLO IN PLACE", "AFL", "PFL", "DIM", "CLEAR SOLO" };
    const char* tips[6] = {
        "Solo goes to your headphones only: the room and the stream never hear it. This is the normal setting.",
        "Solo mutes everything else for everybody. Right for mixing a recording, never for a service.",
        "After-fade listen: you hear the channel where it sits in the mix - panned, and silent if it is muted.",
        "Pre-fade listen: you hear the channel as it arrives, whatever its fader and mute are doing.",
        "Drop your headphones to talk to someone, without losing the level you had set.",
        "Stop listening to everything you have soloed, all at once." };
    for (int i = 0; i < 6; ++i)
    {
        chips[size_t (i)] = std::make_unique<DineButton> (labels[i], DineButton::Style::Toggle);
        chips[size_t (i)]->setFontPx (11.0f);
        chips[size_t (i)]->setCaps (true);
        chips[size_t (i)]->setPadX (11);
        chips[size_t (i)]->setClickingTogglesState (false);
        chips[size_t (i)]->setTooltip (tips[i]);
        addAndMakeVisible (*chips[size_t (i)]);
    }
    chips[0]->onClick = [this] { controller.setSoloMode (SoloMode::Monitor); refreshMonitor(); };
    chips[1]->onClick = [this] { controller.setSoloMode (SoloMode::InPlace); refreshMonitor();
                                 if (onToast) onToast ("Solo in place: pressing S is heard by the room and the stream too. Use it for a recording, not a service."); };
    chips[2]->onClick = [this] { controller.setSoloPoint (SoloPoint::AFL); refreshMonitor(); };
    chips[3]->onClick = [this] { controller.setSoloPoint (SoloPoint::PFL); refreshMonitor(); };
    chips[4]->onClick = [this] { controller.setMonitorDim (! controller.getMonitor().dim); refreshMonitor(); };
    chips[5]->onClick = [this] { controller.clearSolos(); refreshMonitor(); if (onToast) onToast ("Solo cleared."); };

    // Where solo goes. The same choice as Outputs > Solo, here because this card is where the
    // sentence "solo has nowhere to go yet" is read, and the fix should be one click away from it.
    addAndMakeVisible (soloDevice);
    soloDevice.setTooltip ("The device only you listen on - headphones, a second interface, or outputs 3-4 of the "
                           "broadcast device. Solo a channel and it comes out here; the room and the stream never hear it.");
    soloDevice.onClick = [this]
    {
        OutputsSheet::showSoloDeviceMenu (services, soloDevice, [this] (const juce::String& message)
        {
            if (onToast) onToast (message);
            refreshMonitor();
        });
    };

    addAndMakeVisible (monitorLevel);
    monitorLevel.setRange (-40.0, 12.0, 0.5);
    monitorLevel.setValue (0.0, juce::dontSendNotification);
    monitorLevel.setTooltip ("How loud your headphones are. Nothing to do with the mix anyone else hears.");
    Dine::dragOnly (monitorLevel);
    monitorLevel.onValueChange = [this] { controller.setMonitorGain (float (monitorLevel.getValue())); repaint (layout().monitor); };

    setOpaque (true);
    refreshMonitor();
}

LivePage::~LivePage() = default;

void LivePage::refreshMonitor()
{
    const auto& m = controller.getMonitor();
    const bool inPlace = m.mode == SoloMode::InPlace;
    chips[0]->setToggleState (! inPlace, juce::dontSendNotification);
    chips[1]->setToggleState (inPlace, juce::dontSendNotification);
    chips[2]->setToggleState (m.point == SoloPoint::AFL, juce::dontSendNotification);
    chips[3]->setToggleState (m.point == SoloPoint::PFL, juce::dontSendNotification);
    chips[4]->setToggleState (m.dim, juce::dontSendNotification);
    chips[5]->setEnabled (controller.numSoloed() > 0);
    if (std::fabs (monitorLevel.getValue() - double (m.gainDb)) > 0.01)
        monitorLevel.setValue (m.gainDb, juce::dontSendNotification);
    const auto device = services.soloOutputDevice();
    soloDevice.setValue (device.isEmpty() ? juce::String ("Solo goes nowhere yet") : "Solo: " + device);
    soloDevice.setEnabled (services.isAudioRunning());
    repaint (layout().monitor);
}

void LivePage::rebuild()
{
    refreshScenes();
    for (auto& t : tiles) if (t != nullptr) t->refresh();
    refreshMonitor();
    repaint();
}

void LivePage::updateDiskNote()
{
    if (--diskTicks <= 0)
    {
        diskTicks = 60;
        secondsFree = services.daw().getRecordingSecondsFree();
    }
}

void LivePage::refresh()
{
    refreshScenes();                 // the pads follow the controller: a scene kept from anywhere shows here
    refreshMacros();                 // ... and so do the five macros, moved from TUNE or from here

    for (auto& t : tiles) if (t != nullptr) t->refresh();
    updateDiskNote();

    auto& daw = services.daw();
    const bool recording = daw.isRecording();
    const bool running = services.isAudioRunning();
    recordButton->setRecording (recording);
    recordButton->setEnabled (running);

    Look next;
    next.isRecording = recording;
    next.running = running;
    next.safe = daw.getProject().liveSafe;
    const int armed = daw.getProject().numArmed();
    const juce::String room = secondsFree > 0.0 ? span (secondsFree) + " left on the disk" : juce::String();
    if (recording)
    {
        next.recording = "Take " + Glyph::dot() + " " + Transport::formatTime (daw.getRecordingSeconds()).dropLastCharacters (4);
        next.recordingNote = room.isEmpty() ? juce::String (armed) + " tracks being written" : room;
    }
    else
    {
        next.recording = "Not recording";
        next.recordingNote = armed == 0 ? "No tracks are set to record - press the red R on the ones you want"
                                        : juce::String (armed) + (armed == 1 ? " track set to record" : " tracks set to record")
                                              + (room.isEmpty() ? juce::String() : "  " + Glyph::dot() + "  " + room);
    }
    next.output = running ? "On air" : "Off";
    next.outputNote = ! running ? juce::String ("No audio device open")
                     : services.outputDisplayName().isEmpty() ? juce::String ("No output chosen") : services.outputDisplayName();

    // Clipping: the inputs the last listen found clipping at the preamp, named. The advice
    // builds sentences, so it is re-read twice a second rather than every frame.
    if ((++adviceTicks % 15) == 1 || look.clipping.isEmpty())
    {
        int clipping = 0;
        juce::String names;
        if (controller.isPrepared())
            for (int i = 0; i < controller.getEngine().getNumStrips(); ++i)
            {
                const auto a = controller.getInputAdvice (i);
                if (a.level != MixController::InputAdvice::Level::Clipping) continue;
                if (clipping < 2) names += (clipping == 0 ? "" : ", ") + juce::String (controller.getGraph().strips[size_t (i)].name);
                ++clipping;
            }
        clipText = clipping == 0 ? "None" : juce::String (clipping) + (clipping == 1 ? " input" : " inputs");
        clipNote = clipping == 0 ? "Nothing is clipping at the preamp" : names + " " + Glyph::dash() + " fix it at the console";
        anyClipping = clipping > 0;
    }
    next.anyClip = anyClipping;
    next.clipping = clipText;
    next.clippingNote = clipNote;

    const auto loud = controller.getMasterLoudness();
    if (controller.isPrepared()) headroomDb = -controller.getEngine().getBus (MixBus::Master).getOutputMeter().getMaxPeakDb();
    next.headroom = controller.isPrepared() ? juce::String (juce::jlimit (0.0f, 60.0f, headroomDb), 1) + " dB" : Glyph::dash();
    // The sentence only changes when the playhead crosses a marker, so it is rebuilt then and
    // not thirty times a second: scanning the list and building two strings every tick is the
    // kind of cost that does not show up until somebody has a service's worth of markers.
    {
        const auto& project = services.daw().getProject();
        const auto now = services.daw().getTransport().getPosition();
        const bool crossed = now < markerFrom || now >= markerUntil;
        if (crossed || int (project.markers.size()) != markerCount)
        {
            markerCount = int (project.markers.size());
            markerText = markerLine();
            markerFrom = 0;
            markerUntil = std::numeric_limits<juce::int64>::max();
            for (const auto& m : project.markers)
            {
                if (m.position <= now) markerFrom = juce::jmax (markerFrom, m.position);
                else                   markerUntil = juce::jmin (markerUntil, m.position);
            }
        }
        next.marker = markerText;
    }
    next.headroomNote = loud.known && loud.integratedLufs > -100.0f
                            ? juce::String (loud.integratedLufs, 1) + " LUFS integrated, target " + juce::String (loud.targetLufs, 0)
                            : "No loudness reading yet";

    next.soloCount = controller.numSoloed();
    next.inPlace = controller.getMonitor().mode == SoloMode::InPlace;
    next.routed = controller.hasMonitorOutput();
    next.monitorDb = float (monitorLevel.getValue());
    next.monitorNote = next.inPlace ? "Careful: pressing S is heard by the room and the stream too."
                     : ! next.routed ? "Solo has nowhere to go yet. Pick the device you listen on, at the right of the row above."
                     : next.soloCount > 0 ? juce::String (next.soloCount) + (next.soloCount == 1 ? " channel soloed. Only you hear it." : " channels soloed. Only you hear it.")
                                          : "Press S on any channel to hear it. Only you hear it.";
    if (next.soloCount != look.soloCount || next.inPlace != look.inPlace) refreshMonitor();
    historyButton.setEnabled (! controller.getCheckpoints().empty());
    liveSafeButton.setButtonText (next.safe ? "LIVE SAFE ON" : "LIVE SAFE OFF");
    liveSafeButton.setStyle (next.safe ? DineButton::Style::Filled : DineButton::Style::Standard);
    if (next != look) { look = next; repaint(); }
}

LivePage::Layout LivePage::layout() const
{
    Layout l;
    auto page = getLocalBounds();
    l.head = page.removeFromTop (kHeadH);
    auto r = page.reduced (kPadX, kPadY - 8);
    l.status = r.removeFromTop (kStatusH);
    r.removeFromTop (kGap);
    // What the bands cost, so the group tiles can give up their spare height to the macros
    // rather than the macros being dropped while the tiles keep it. The macros are still the
    // first thing to go when even the tiles' floor will not fit.
    constexpr int kTilesFloor = 160, kLowerFloor = 190;
    const bool macrosFit = r.getHeight() >= kTilesFloor + kGap + kScenesH + kGap + kMacrosH + kGap + kLowerFloor;
    const int below = kGap + kScenesH + kGap + (macrosFit ? kMacrosH + kGap : 0) + kLowerFloor;
    l.tiles = r.removeFromTop (juce::jlimit (kTilesFloor, 200, r.getHeight() - below));
    r.removeFromTop (kGap);
    l.scenes = r.removeFromTop (kScenesH);
    r.removeFromTop (kGap);
    if (macrosFit)
    {
        l.macros = r.removeFromTop (kMacrosH);
        r.removeFromTop (kGap);
    }
    auto lower = r.withHeight (juce::jlimit (190, 260, r.getHeight()));
    l.safe = lower.removeFromRight (kSafeW);
    lower.removeFromRight (kGap);
    // The monitor card is as tall as what it holds (caption, chips, level, sentence); LIVE SAFE keeps the band's height for its rules.
    l.monitor = lower.withHeight (juce::jmin (lower.getHeight(), 18 + kMonCaption + kMonCaptionGap + Dine::Metric::control + kMonRowGap
                                                                + Dine::Metric::control + kMonNoteGap + kMonNote + 18));
    return l;
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto l = layout();

    // ---- the page bar: what this workspace is, whether it is going out, and what part of
    // the service this is. The marker is the one the playhead has most recently passed, and
    // the next one is where the service is going - both read off the timeline.
    {
        auto head = l.head.reduced (kPadX, 0);
        g.setColour (Dine::hair);
        g.fillRect (l.head.getX(), l.head.getBottom() - 1, l.head.getWidth(), 1);
        g.setColour (Dine::ink);
        g.setFont (Dine::Type::headingPage());
        head.removeFromLeft (60);
        Dine::drawText (g, "Live", l.head.reduced (kPadX, 0).withWidth (60), juce::Justification::centredLeft);

        // ON AIR: lit whenever the engine is running and the broadcast is not muted.
        const bool onAir = look.running && ! controller.isBroadcastMuted();
        auto pill = head.removeFromLeft (juce::jmin (150, head.getWidth())).withSizeKeepingCentre (
            juce::jmin (150, head.getWidth()), 24);
        if (onAir)
        {
            Dine::fillRounded (g, pill.toFloat(), Dine::crit.withAlpha (0.18f), Dine::Radius::chip);
            g.setColour (Dine::crit);
            g.fillEllipse (float (pill.getX() + 12), float (pill.getCentreY() - 3), 6.0f, 6.0f);
            g.setFont (Dine::Type::labelControl());
            Dine::drawText (g, "ON AIR", pill.withTrimmedLeft (24), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::Type::labelControl());
            Dine::drawText (g, controller.isBroadcastMuted() ? "BROADCAST MUTED" : "NOT RUNNING", pill, juce::Justification::centredLeft, true);
        }

        head.removeFromLeft (14);
        if (head.getWidth() > 120)
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::Type::bodySmall());
            Dine::drawText (g, look.marker, head, juce::Justification::centredLeft, true);
        }
    }

    // ---- the four things that matter during a service
    {
        auto row = l.status;
        const int w = (row.getWidth() - 3 * 12) / 4;
        auto card = [&] (juce::Rectangle<int> area, const juce::String& label, const juce::String& value, const juce::String& note,
                         juce::Colour ground, juce::Colour valueInk, bool monoValue, int trimRight = 0)
        {
            Dine::fillRounded (g, area.toFloat(), ground, Dine::Radius::card);
            auto inner = area.reduced (16, 14);
            inner.removeFromRight (trimRight);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawText (g, label, inner.removeFromTop (14), juce::Justification::topLeft);
            inner.removeFromTop (6);
            g.setColour (valueInk);
            g.setFont (monoValue ? Dine::mono (19.0f, 500) : Dine::text (19.0f, 600));
            Dine::drawText (g, value, inner.removeFromTop (24), juce::Justification::topLeft, true);
            inner.removeFromTop (4);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawFittedText (g, note, inner, juce::Justification::topLeft, 2, 1.0f);
        };
        card (row.removeFromLeft (w), "Recording", look.recording, look.recordingNote,
              look.isRecording ? Dine::recGround : Dine::card, look.isRecording ? Dine::crit : Dine::ink3, true,
              recordButton->getWidth() + 10);
        row.removeFromLeft (12);
        card (row.removeFromLeft (w), "Output", look.output, look.outputNote, Dine::card, look.running ? Dine::ink : Dine::ink3, false);
        row.removeFromLeft (12);
        card (row.removeFromLeft (w), "Clipping", look.clipping, look.clippingNote,
              look.anyClip ? Dine::recGround : Dine::card, look.anyClip ? Dine::crit : Dine::ink, false);
        row.removeFromLeft (12);
        // ---- BROADCAST: what is actually leaving, big enough to read from the back of a
        // booth, with the three numbers that decide whether it is right underneath it.
        {
            auto broadcast = row;
            Dine::fillRounded (g, broadcast.toFloat(), Dine::card, Dine::Radius::card);
            auto inner = broadcast.reduced (16, 14);

            const auto loud = controller.getMasterLoudness();
            const bool known = loud.known && loud.shortTermLufs > -100.0f;

            auto head = inner.removeFromTop (12);
            g.setColour (Dine::ink4);
            g.setFont (Dine::Type::labelSection());
            Dine::drawText (g, "BROADCAST  " + juce::String (Glyph::dot()) + "  " + look.output.toUpperCase(),
                        head, juce::Justification::centredLeft, true);
            inner.removeFromTop (4);

            // the number
            auto big = inner.removeFromTop (30);
            const auto tint = ! known ? Dine::ink3 : loud.onTarget() ? Dine::accent : Dine::warn;
            g.setColour (tint);
            g.setFont (Dine::mono (26.0f, 500));
            // A fixed box rather than a measured one: the face is monospaced, so "-14.3" is
            // always the same width, and measuring a string on every paint is how a page ends
            // up spending its frame on glyph layout.
            const juce::String shortTerm = known ? juce::String (loud.shortTermLufs, 1) : juce::String (Glyph::dash());
            Dine::drawText (g, shortTerm, big.removeFromLeft (96), juce::Justification::centredLeft);
            g.setColour (Dine::ink3);
            g.setFont (Dine::Type::caption());
            Dine::drawText (g, "LUFS short-term", big, juce::Justification::centredLeft, true);
            inner.removeFromTop (6);

            // the three that decide whether it is right. Loudness range (LRA) is not measured,
            // so the third is how far from target this mix actually is - which is the number
            // somebody is looking for anyway.
            // Three readings need about 90 px each to be readable. On the smallest window they
            // do not have it, so the third gives way rather than all three colliding - the
            // target bar underneath already says what "against target" says.
            auto cols = inner.removeFromTop (28);
            const int columns = cols.getWidth() >= 270 ? 3 : 2;
            const int cw = cols.getWidth() / columns;
            auto small = [&] (juce::Rectangle<int> c, const juce::String& k, const juce::String& v, juce::Colour ink)
            {
                if (c.isEmpty()) return;
                g.setColour (Dine::ink4);
                g.setFont (Dine::Type::caption());
                Dine::drawText (g, k, c.removeFromTop (13), juce::Justification::topLeft, true);
                g.setColour (ink);
                g.setFont (Dine::Type::monoValue());
                Dine::drawText (g, v, c, juce::Justification::topLeft, true);
            };
            const bool integrated = loud.known && loud.integratedLufs > -100.0f;
            small (cols.removeFromLeft (cw), "Integrated",
                   integrated ? juce::String (loud.integratedLufs, 1) : juce::String (Glyph::dash()), Dine::ink);
            small (cols.removeFromLeft (cw), "True peak",
                   loud.truePeakDb > -100.0f ? juce::String (loud.truePeakDb, 1) + " dBTP" : juce::String (Glyph::dash()),
                   loud.truePeakDb > loud.ceilingDb ? Dine::crit : Dine::ink);
            if (columns < 3) cols = {};
            small (cols, "Against target",
                   integrated ? juce::String (loud.deltaLu() >= 0.0f ? "+" : "") + juce::String (loud.deltaLu(), 1) + " LU"
                              : juce::String (Glyph::dash()),
                   ! integrated ? Dine::ink3 : loud.onTarget() ? Dine::ink : Dine::warn);

            // the target bar: where the mix sits against what it is aiming at, +/- 6 LU across
            if (inner.getHeight() >= 4)
            {
                auto track = inner.removeFromTop (juce::jmin (5, inner.getHeight())).toFloat();
                Dine::fillRounded (g, track, Dine::well, 2.5f);
                if (integrated)
                {
                    const float t = juce::jlimit (0.0f, 1.0f, 0.5f + loud.deltaLu() / 12.0f);
                    Dine::fillRounded (g, track.withWidth (track.getWidth() * t), tint, 2.5f);
                }
                g.setColour (Dine::edge);
                g.fillRect (track.getCentreX() - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);
            }
        }
    }

    // ---- scenes
    {
        Dine::fillRounded (g, l.scenes.toFloat(), Dine::tile, Dine::Radius::card);
        auto inner = l.scenes.reduced (18, 14);
        Dine::drawSection (g, inner.removeFromTop (kMonCaption), "SCENES  " + juce::String (Glyph::dot()) + "  KEEP THE MIX FOR EACH PART OF THE SERVICE, BRING IT BACK IN ONE PRESS");
    }

    // ---- MACROS - WHOLE MIX
    if (! l.macros.isEmpty())
    {
        Dine::fillRounded (g, l.macros.toFloat(), Dine::tile, Dine::Radius::card);
        auto inner = l.macros.reduced (18, 10);
        Dine::drawSection (g, inner.removeFromTop (kMonCaption),
                           "MACROS  " + juce::String (Glyph::dot()) + "  WHOLE MIX");
        if (services.daw().isLiveSafe())
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::Type::caption());
            Dine::drawText (g, "LIVE SAFE", l.macros.reduced (18, 10).removeFromTop (kMonCaption),
                        juce::Justification::centredRight, true);
        }
    }

    // ---- the engineer's listen
    {
        Dine::fillRounded (g, l.monitor.toFloat(), Dine::tile, Dine::Radius::card);
        auto inner = l.monitor.reduced (18, 18);
        Dine::drawSection (g, inner.removeFromTop (kMonCaption), "ENGINEER MONITORING  " + juce::String (Glyph::dot()) + "  THE ROOM AND THE STREAM DO NOT HEAR THIS");
        // The same rows resized() gives the chips and the slider: the caption is already taken off `inner`.
        inner.removeFromTop (kMonCaptionGap);
        auto levelRow = inner.withTrimmedTop (Dine::Metric::control + kMonRowGap).withHeight (Dine::Metric::control);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, "Monitor level", levelRow.removeFromLeft (110), juce::Justification::centredLeft);
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        Dine::drawText (g, dbText (look.monitorDb), levelRow.removeFromRight (58), juce::Justification::centredRight);
        auto note = inner.withTrimmedTop (Dine::Metric::control + kMonRowGap + Dine::Metric::control + kMonNoteGap).withHeight (kMonNote);
        g.setColour (look.inPlace || ! look.routed ? Dine::warn : look.soloCount > 0 ? Dine::accent : Dine::ink3);
        g.setFont (Dine::text (12.5f));
        Dine::drawText (g, look.monitorNote, note, juce::Justification::centredLeft, true);
    }

    // ---- LIVE SAFE: what it locks, blocks and allows, printed
    {
        Dine::fillRounded (g, l.safe.toFloat(), Dine::tile, Dine::Radius::card);
        auto inner = l.safe.reduced (18, 18);
        inner.removeFromTop (44 + 10 + Dine::Metric::button + 10);   // the lock, then the way back
        struct Rule { juce::String what, why; bool amber; };
        std::vector<Rule> rules;
        if (look.safe)
        {
            const auto& policy = controller.getLiveSafePolicy();
            rules = { { "Locked", "The audio device, the routing, the input patch and opening a session. Changing any of them interrupts the audio mid-service.", true },
                      { "Blocked", "TUNE MIX, TUNE LIVE MIX, TUNE CHANNEL and MATCH TO REFERENCE. They re-tune channels that are on air.", true },
                      { "Allowed", "Faders (" + juce::String (int (policy.maxFaderStepDb)) + " dB at a time, the master " + juce::String (int (policy.maxMasterStepDb))
                                       + "), mutes, solos, the monitor, markers and recording. Everything you touch during the service.", false } };
        }
        else
        {
            rules = { { "LIVE SAFE is off", "Every setting is editable, including the ones that restart the audio engine.", false },
                      { "Recording is unaffected", "Takes keep running while you change things.", false },
                      { "Turn it on before the doors open", "One click, here or in the toolbar.", false } };
        }
        for (const auto& rule : rules)
        {
            // A rule is drawn whole or not at all: half a sentence in a box is worse than one
            // rule fewer, and the ones that matter most are first.
            if (inner.getHeight() < 62) break;
            const int h = juce::jmin (inner.getHeight(), 13 + 16 + 5 + 34 + 13);
            auto box = inner.removeFromTop (h);
            Dine::fillRounded (g, box.toFloat(), Dine::item, Dine::Radius::control);
            auto t = box.reduced (12, 12);
            g.setColour (rule.amber ? Dine::warn : Dine::ink);
            g.setFont (Dine::text (13.0f));
            Dine::drawText (g, rule.what, t.removeFromTop (16), juce::Justification::topLeft);
            t.removeFromTop (4);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            Dine::drawFittedText (g, rule.why, t, juce::Justification::topLeft, 3, 1.0f);
            inner.removeFromTop (3);
        }
    }
}

void LivePage::resized()
{
    const auto l = layout();

    // MACROS - WHOLE MIX: five knobs in a row, centred in their card.
    {
        const bool shown = ! l.macros.isEmpty();
        auto row = l.macros.reduced (18, 14);
        const int n = int (MixMacro::Count);
        const int w = juce::jmin (108, row.getWidth() / juce::jmax (1, n));
        auto block = row.withSizeKeepingCentre (w * n, row.getHeight());
        for (int i = 0; i < n; ++i)
        {
            auto& k = macroKnobs[size_t (i)];
            if (k == nullptr) continue;
            k->setVisible (shown);
            if (shown) k->setBounds (block.removeFromLeft (w));
        }
    }
    {
        auto first = l.status.withWidth ((l.status.getWidth() - 36) / 4).reduced (16, 14);
        const int w = juce::jmax (96, recordButton->idealWidth());
        recordButton->setBounds (first.removeFromRight (w).withSizeKeepingCentre (w, 30));
    }
    {
        auto row = l.tiles;
        const int gap = 12;
        const int w = (row.getWidth() - gap * (kTiles - 1)) / kTiles;
        for (int i = 0; i < kTiles; ++i)
        {
            tiles[size_t (i)]->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    }
    {
        auto inner = l.scenes.reduced (18, 14);
        inner.removeFromTop (kMonCaption + 8);
        auto row = inner.removeFromTop (Dine::Metric::control);
        const int gap = 10;
        const int cell = (row.getWidth() - gap * 3) / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto c = row.removeFromLeft (cell);
            row.removeFromLeft (gap);
            const int keepW = juce::jmax (52, sceneKeeps[size_t (i)]->idealWidth());
            sceneKeeps[size_t (i)]->setBounds (c.removeFromRight (keepW));
            c.removeFromRight (6);
            scenePads[size_t (i)]->setBounds (c);
        }
    }
    {
        auto inner = l.monitor.reduced (18, 18);
        inner.removeFromTop (kMonCaption + kMonCaptionGap);
        auto chipRow = inner.removeFromTop (Dine::Metric::control);
        for (auto& c : chips)
        {
            const int w = juce::jmax (44, c->idealWidth());
            if (w > chipRow.getWidth()) break;
            c->setBounds (chipRow.removeFromLeft (w));
            chipRow.removeFromLeft (8);
        }
        // The solo device picker takes the right end of the chip row, when there is a row's worth left for it.
        const int pickW = juce::jmin (300, juce::jmax (150, soloDevice.idealWidth()));
        soloDevice.setVisible (chipRow.getWidth() >= 150);
        soloDevice.setBounds (chipRow.removeFromRight (juce::jmin (pickW, chipRow.getWidth())));
        inner.removeFromTop (kMonRowGap);
        auto levelRow = inner.removeFromTop (Dine::Metric::control);
        levelRow.removeFromLeft (110);
        levelRow.removeFromRight (58 + 14);
        monitorLevel.setBounds (levelRow);
    }
    {
        auto inner = l.safe.reduced (18, 18);
        liveSafeButton.setBounds (inner.removeFromTop (44));
        // Under the lock, where an engineer already is when something has gone wrong: the
        // list of every mix this session has had, with the time and the name of each.
        inner.removeFromTop (10);
        historyButton.setBounds (inner.removeFromTop (Dine::Metric::button));
    }
}

} // namespace livemix

namespace livemix
{

// The pads read the controller: the name, and lit while that scene is the one kept.
// WHAT PART OF THE SERVICE THIS IS.
//
// The marker the playhead has most recently passed, and the next one after it: "Now: Sermon -
// next marker 'Altar call' at 01:25". Read straight off the timeline, so it is the same list
// TRACKS shows and there is nothing to keep in step.
juce::String LivePage::markerLine() const
{
    const auto& project = services.daw().getProject();
    if (project.markers.empty()) return "No markers on the timeline yet.";

    const auto now = services.daw().getTransport().getPosition();
    const Marker* current = nullptr;
    const Marker* next = nullptr;
    for (const auto& m : project.markers)
    {
        if (m.position <= now && (current == nullptr || m.position > current->position)) current = &m;
        if (m.position > now && (next == nullptr || m.position < next->position)) next = &m;
    }

    const double sr = juce::jmax (1.0, project.sampleRate);
    auto clock = [sr] (juce::int64 pos)
    {
        const int total = int (double (pos) / sr);
        return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    };

    juce::String s = current != nullptr ? "Now: " + current->name : juce::String ("Before the first marker");
    if (next != nullptr) s += "   " + juce::String (Glyph::dot()) + "   next marker \"" + next->name + "\" at " + clock (next->position);
    return s;
}

void LivePage::refreshMacros()
{
    const auto& v = controller.getMacros();
    const bool safe = services.daw().isLiveSafe();
    for (int i = 0; i < int (MixMacro::Count); ++i)
        if (macroKnobs[size_t (i)] != nullptr)
        {
            macroKnobs[size_t (i)]->setValue (v.get (MixMacro (i)));
            // LIVE SAFE fences the macros here the same way it fences the pads on TUNE.
            macroKnobs[size_t (i)]->setEnabled (! safe);
        }
}

void LivePage::refreshScenes()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto& scene = controller.getScene (i);
        auto& pad = *scenePads[size_t (i)];
        const juce::String text = juce::String (scene.name.empty() ? defaultSceneName (i) : scene.name.c_str()) + (scene.kept ? "" : "  " + juce::String (Glyph::dash()));
        if (pad.getButtonText() != text) pad.setButtonText (text);
        pad.setToggleState (scene.kept, juce::dontSendNotification);
        pad.setTooltip (scene.kept ? "Bring the " + juce::String (scene.name) + " mix back - every fader, chain and macro - in one press. UNDO takes it back."
                                   : "Nothing is kept here yet. Set the mix, then KEEP.");
    }
}

} // namespace livemix
