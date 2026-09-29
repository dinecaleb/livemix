#include "LivePage.h"
#include "native/MixHistory.h"
#include "OutputsSheet.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kGroupBuses = int (MixBus::Master);
    constexpr int kFxTile = kGroupBuses, kMasterTile = kGroupBuses + 1;
    constexpr int kTiles = kGroupBuses + 2;
    constexpr int kPadX = 24, kPadY = 22, kGap = 20;
    constexpr int kStatusH = 100;
    constexpr int kSafeW = 470;
    constexpr int kScenesH = 100;
    constexpr int kMonitorH = 150;
    constexpr int kMonitorW = 560;
    // The monitor card's rows, measured once: a gap under its heading, the chips, a gap, the
    // level, a gap, the sentence that says where solo is going.
    constexpr int kMonCaptionGap = 12, kMonRowGap = 20, kMonNoteGap = 16, kMonNote = 18;

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
class LivePage::GroupTile : public juce::Component
{
public:
    GroupTile (MixController& c, int index) : controller (c), group (index)
    {
        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
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
        for (auto* b : { &mute, &solo }) { b->setFontPx (11.0f); b->setPadX (4); b->setCaps (true); }
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

    // The design's LIVE tile (`06 - Live`, 71:12363): the group's colour across the top, the
    // name and what its fader is set to, a standing fader with its meter beside it, and M and
    // S along the foot. A muted tile says NOT HEARD in words, and its meter keeps moving in
    // grey - "nothing is there" and "it is there but not heard" are different problems.
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        Dine::fillRounded (g, r.toFloat(), muted ? Dine::refuse : soloed ? Dine::soloGround : Dine::card, Dine::Radius::card);
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path round; round.addRoundedRectangle (r.toFloat(), Dine::Radius::card);
            g.reduceClipRegion (round);
            g.setColour (used ? tint() : Dine::ink4);
            g.fillRect (r.removeFromTop (3));
        }

        auto inner = getLocalBounds().reduced (14, 14);
        auto head = inner.removeFromTop (20);
        g.setColour (! used ? Dine::ink4 : muted ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, name(), head, juce::Justification::centredLeft, true);

        auto sub = inner.removeFromTop (16);
        const juce::String state = ! used ? "Off" : muted ? "NOT HEARD" : soloed ? "SOLO" : juce::String();
        if (state.isNotEmpty())
        {
            g.setColour (muted ? Dine::keyMute : soloed ? Dine::accent : Dine::ink4);
            g.setFont (Dine::caps (10.0f, 0.04f, 600));
            Dine::drawText (g, state, sub, juce::Justification::centredLeft, true);
        }
        else if (isMaster())
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (11.0f, 500));
            Dine::drawText (g, loudness + " LUFS", sub, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (Dine::ink3);
            g.setFont (Dine::mono (11.0f, 500));
            Dine::drawText (g, dbText (float (fader.getValue())), sub, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (14, 14);
        inner.removeFromTop (20 + 16 + 10);
        auto keys = inner.removeFromBottom (20);
        inner.removeFromBottom (12);

        // the throw: the fader on the left, the meter beside it, both standing
        auto throwArea = inner;
        const int faderW = 22, meterW = 6;
        auto pair = throwArea.withSizeKeepingCentre (faderW + 10 + meterW, throwArea.getHeight());
        fader.setBounds (pair.removeFromLeft (faderW));
        pair.removeFromLeft (10);
        meter.setBounds (pair);

        if (isMaster()) { mute.setBounds (keys.removeFromRight (30)); solo.setBounds (0, 0, 0, 0); return; }
        solo.setBounds (keys.removeFromRight (30));
        keys.removeFromRight (4);
        mute.setBounds (keys.removeFromRight (30));
    }

private:
    bool isFx() const noexcept { return group == kFxTile; }
    bool isMaster() const noexcept { return group == kMasterTile; }
    // A tile's position is the console's order, not the enum's: LEAD sits with the voices.
    MixBus bus() const noexcept { return isMaster() ? MixBus::Master : mixBusInDisplayOrder (group); }
    juce::Colour tint() const { return isFx() ? Dine::ink2 : isMaster() ? Dine::ink : Dine::busTint (bus()); }
    juce::String name() const
    {
        if (isFx()) return "FX returns";
        if (isMaster()) return "Master";
        const juce::String raw (mixBusName (bus()));
        return raw.length() <= 3 ? raw.toUpperCase() : raw.substring (0, 1).toUpperCase() + raw.substring (1).toLowerCase();
    }

    MixController& controller;
    int group;
    bool used = true, muted = false, soloed = false, updating = false;
    juce::String loudness;
    juce::Rectangle<int> readout;
    DineMeter meter { DineMeter::Style::Bar };
    juce::Slider fader;
    DineButton mute { "M", DineButton::Style::Standard };
    DineButton solo { "S", DineButton::Style::Standard };
};

// A scene, as a card (design `06 - Live`, 71:12363): its name, when it was kept, and KEEP
// along its foot. Pressing the card brings that whole mix back in one press; pressing KEEP
// puts the mix that is running now under that name. LIVE SAFE never locks either.
class LivePage::SceneCard : public juce::Button
{
public:
    explicit SceneCard (const juce::String& n) : juce::Button (n), name (n) { setClickingTogglesState (false); }

    void setScene (const juce::String& n, bool isKept, const juce::String& whenText)
    {
        if (n == name && isKept == kept && whenText == when) return;
        name = n; kept = isKept; when = whenText;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, down ? Dine::selected : over ? Dine::raised : Dine::card, Dine::Radius::card);
        if (kept) Dine::hairlineRounded (g, r.reduced (0.5f), Dine::hair, Dine::Radius::card);
        auto inner = getLocalBounds().reduced (14, 12);
        g.setColour (kept ? Dine::ink : Dine::ink3);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, name, inner.removeFromTop (18), juce::Justification::centredLeft, true);
        inner.removeFromTop (2);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, kept ? (when.isEmpty() ? juce::String ("Kept") : "Kept " + when) : juce::String ("Empty"),
                        inner.removeFromTop (16), juce::Justification::centredLeft, true);
    }

private:
    juce::String name, when;
    bool kept = false;
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

    addAndMakeVisible (autopilotButton);
    autopilotButton.setCaps (true);
    autopilotButton.setFontPx (11.0f);
    autopilotButton.setClickingTogglesState (false);
    autopilotButton.setTooltip ("Hold the mix you set. Autopilot moves group faders only, slowly, inside a few dB of the mix "
                                "it was engaged on, and says why every time. Touch a fader and that group is yours again.");
    autopilotButton.onClick = [this]
    {
        const bool wanted = ! controller.isAutopilotOn();
        if (! controller.setAutopilot (wanted)) return;      // the controller has already said why
        refresh();
        repaint();
    };

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
        scenePads[size_t (i)] = std::make_unique<SceneCard> (juce::String (defaultSceneName (i)));
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

// The sidebar's SCENES row: come to LIVE and say where they are, for a moment.
void LivePage::focusScenes()
{
    sceneFlash = 45;
    repaint();
}

void LivePage::refresh()
{
    refreshScenes();                 // the pads follow the controller: a scene kept from anywhere shows here

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
    liveSafeButton.setButtonText ("LIVE SAFE");
    liveSafeButton.setIcon (Dine::Icon::Lock);
    liveSafeButton.setStyle (next.safe ? DineButton::Style::Filled : DineButton::Style::Standard);
    liveSafeButton.setTint (Dine::warn);

    {
        const auto& ap = controller.getAutopilot();
        autopilotButton.setStyle (ap.on ? DineButton::Style::Filled : DineButton::Style::Standard);
        autopilotButton.setTint (Dine::monitor);
        if (ap.on && autopilotSince.isEmpty()) autopilotSince = juce::Time::getCurrentTime().toString (false, true, false, true);
        if (! ap.on) autopilotSince = {};
        if (ap.on != autopilotWasOn) { autopilotWasOn = ap.on; repaint(); }
    }
    if (next != look) { look = next; repaint(); }
}

LivePage::Layout LivePage::layout() const
{
    Layout l;
    auto r = getLocalBounds().reduced (kPadX, kPadY);
    l.status = r.removeFromTop (kStatusH);
    r.removeFromTop (kGap + 8);
    l.groupsCaption = r.removeFromTop (20);
    r.removeFromTop (10);
    l.tiles = r.removeFromTop (juce::jmin (260, juce::jmax (170, r.getHeight() - kGap - 20 - 10 - kScenesH - kGap - 190)));
    r.removeFromTop (kGap + 8);
    l.scenesCaption = r.removeFromTop (20);
    r.removeFromTop (10);
    {
        // The scenes and what the engineer hears share a band: the scenes take the left, the
        // monitor card the right, the way the design sets them. The card is the taller of the
        // two, so the band is its height and the scene cards sit at the top of it.
        auto band = r.removeFromTop (kMonitorH);
        l.monitor = band.removeFromRight (juce::jmin (kMonitorW, band.getWidth() / 2));
        band.removeFromRight (kGap);
        l.scenes = band.withHeight (kScenesH);
        r.removeFromTop (0);
    }
    r.removeFromTop (kGap + 8);
    auto lower = r.withHeight (juce::jlimit (170, 230, r.getHeight()));
    l.autopilot = lower.removeFromRight (juce::jmin (kSafeW, lower.getWidth() / 2));
    lower.removeFromRight (kGap);
    l.safe = lower;
    return l;
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto l = layout();

    // The mark the sidebar's SCENES row leaves: a ring round the scenes for a second, so an
    // eye that came looking for them finds them without anything changing.
    if (sceneFlash > 0)
    {
        Dine::hairlineRounded (g, l.scenes.expanded (6, 6).toFloat(),
                               Dine::accent.withAlpha (juce::jmin (1.0f, float (sceneFlash) / 30.0f)), Dine::Radius::card);
        --sceneFlash;
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
            auto cap = inner.removeFromTop (14);
            g.setColour (valueInk);
            g.fillEllipse (cap.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            cap.removeFromLeft (8);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (11.0f, 500));
            Dine::drawText (g, label, cap, juce::Justification::centredLeft, true);
            inner.removeFromTop (6);
            g.setColour (valueInk);
            g.setFont (monoValue ? Dine::mono (21.0f, 500) : Dine::text (22.0f, 600));
            Dine::drawText (g, value, inner.removeFromTop (28), juce::Justification::topLeft, true);
            inner.removeFromTop (2);
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
        card (row, "Master headroom", look.headroom, look.headroomNote, Dine::card,
              headroomDb < 0.5f ? Dine::crit : headroomDb < 3.0f ? Dine::warn : Dine::ink, true);
    }

    auto heading = [&g] (juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        Dine::drawText (g, text, r, juce::Justification::centredLeft, true);
    };
    heading (l.groupsCaption, "Groups");
    heading (l.scenesCaption, "Scenes");

    // ---- the engineer's listen: a card of its own, because none of it reaches the room
    {
        Dine::fillRounded (g, l.monitor.toFloat(), Dine::control, Dine::Radius::card);
        auto inner = l.monitor.reduced (18, 16);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (17.0f, 600));
        Dine::drawText (g, "What I hear", inner.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, "Press S on any channel to hear it. Only you hear it.", inner.removeFromTop (16),
                        juce::Justification::centredLeft, true);
        inner.removeFromTop (kMonCaptionGap);
        auto levelRow = inner.withTrimmedTop (Dine::Metric::control + kMonRowGap).withHeight (Dine::Metric::control);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, "Monitor level", levelRow.removeFromLeft (94), juce::Justification::centredLeft);
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, dbText (look.monitorDb), levelRow.removeFromRight (52), juce::Justification::centredRight);
        auto note = inner.withTrimmedTop (Dine::Metric::control + kMonRowGap + Dine::Metric::control + kMonNoteGap).withHeight (kMonNote);
        g.setColour (look.inPlace || ! look.routed ? Dine::warn : look.soloCount > 0 ? Dine::accent : Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, look.monitorNote, note, juce::Justification::centredLeft, true);
    }

    // ---- LIVE SAFE: what it locks, blocks and allows, in three columns and in words
    {
        Dine::fillRounded (g, l.safe.toFloat(), look.safe ? Dine::refuse : Dine::card, Dine::Radius::card);
        if (look.safe) Dine::hairlineRounded (g, l.safe.toFloat().reduced (0.5f), Dine::warn, Dine::Radius::card);
        auto inner = l.safe.reduced (18, 16);
        auto head = inner.removeFromTop (Dine::Metric::button);
        head.removeFromLeft (liveSafeButton.getWidth() + 14);
        g.setColour (look.safe ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (15.0f, 600));
        Dine::drawText (g, look.safe ? "On. Lock the sound for the service."
                                     : "Off. Every setting is editable, including the ones that restart the engine.",
                        head, juce::Justification::centredLeft, true);
        inner.removeFromTop (14);
        inner.removeFromBottom (Dine::Metric::button + 10);

        struct Rule { juce::String what, why; juce::Colour tint; };
        std::vector<Rule> rules;
        if (look.safe)
        {
            const auto& policy = controller.getLiveSafePolicy();
            rules = { { "Locked", "The audio device, the routing, the input patch and opening a session. Changing any of "
                                  "them interrupts the audio mid-service.", Dine::warn },
                      { "Blocked", "TUNE MIX, TUNE LIVE MIX, TUNE CHANNEL and MATCH TO REFERENCE. They re-tune channels "
                                   "that are on air.", Dine::warn },
                      { "Allowed", "Faders (" + juce::String (int (policy.maxFaderStepDb)) + " dB at a time, the master "
                                       + juce::String (int (policy.maxMasterStepDb)) + "), mutes, solos, the monitor, "
                                       "markers and recording. Everything you touch during the service.", Dine::ok } };
        }
        else
        {
            rules = { { "Nothing is locked", "Every setting is editable, including the ones that restart the audio engine.", Dine::ink3 },
                      { "Recording is unaffected", "Takes keep running while you change things.", Dine::ink3 },
                      { "Turn it on before the doors open", "One press, here or in the toolbar.", Dine::ink3 } };
        }
        const int colW = (inner.getWidth() - 2 * 16) / 3;
        for (size_t i = 0; i < rules.size(); ++i)
        {
            auto colArea = juce::Rectangle<int> (inner.getX() + int (i) * (colW + 16), inner.getY(), colW, inner.getHeight());
            g.setColour (rules[i].tint);
            g.setFont (Dine::text (12.0f, 600));
            Dine::drawText (g, rules[i].what, colArea.removeFromTop (16), juce::Justification::topLeft, true);
            colArea.removeFromTop (5);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (11.5f));
            Dine::drawFittedText (g, rules[i].why, colArea, juce::Justification::topLeft, 6, 1.0f);
        }
    }

    // ---- AUTOPILOT: the second thing in DLIVE allowed to move a level by itself, so while it
    // is on the page says so, says what it has had to move, and carries the press that stops it.
    {
        const auto& ap = controller.getAutopilot();
        Dine::fillRounded (g, l.autopilot.toFloat(), ap.on ? Dine::editGround : Dine::card, Dine::Radius::card);
        if (ap.on) Dine::hairlineRounded (g, l.autopilot.toFloat().reduced (0.5f), Dine::monitor.withAlpha (0.5f), Dine::Radius::card);
        auto inner = l.autopilot.reduced (18, 16);
        auto head = inner.removeFromTop (Dine::Metric::button);
        head.removeFromLeft (autopilotButton.getWidth() + 14);
        g.setColour (ap.on ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (15.0f, 600));
        Dine::drawText (g, ap.on ? (autopilotSince.isNotEmpty() ? "Watching since " + autopilotSince + "  " + Glyph::dot() + "  "
                                                                  + (ap.groupsCorrected > 0 ? "holding the mix" : "the mix is healthy")
                                                                : juce::String ("Watching the mix"))
                                 : juce::String ("Off. The mix is yours alone."),
                        head, juce::Justification::centredLeft, true);
        inner.removeFromTop (12);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        const int noteH = juce::jmin (inner.getHeight(), 52);
        Dine::drawFittedText (g, ap.on ? "If the mix stays healthy it does nothing. It moves group faders only, slowly, "
                                         "within a few dB of the mix it was engaged on. Touch a fader and it is yours again."
                                       : "Engage it and DLIVE holds the mix you set: group faders only, inside a few dB of "
                                         "where you engaged it, and every move says why.",
                              inner.removeFromTop (noteH), juce::Justification::topLeft, 3, 1.0f);
        inner.removeFromTop (8);
        if (ap.on && ! ap.lastWhat.empty())
        {
            auto line = inner.removeFromTop (18);
            g.setColour (Dine::accent);
            g.setFont (Dine::mono (10.5f, 500));
            const int timeW = Dine::textWidth (Dine::mono (10.5f, 500), autopilotSince);
            Dine::drawText (g, autopilotSince, line.removeFromLeft (timeW), juce::Justification::centredLeft);
            line.removeFromLeft (10);
            g.setColour (Dine::ink2);
            g.setFont (Dine::text (12.0f));
            Dine::drawText (g, juce::String (ap.lastWhat) + (ap.lastWhy.empty() ? juce::String()
                                                                                : " " + juce::String (Glyph::dash()) + " " + juce::String (ap.lastWhy)),
                            line, juce::Justification::centredLeft, true);
        }
    }
}

void LivePage::resized()
{
    const auto l = layout();
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
        // Four scene cards, side by side: the name and when it was kept on the card, KEEP along
        // its foot (design `06 - Live`, 71:12363).
        auto row = l.scenes;
        const int gap = 12;
        const int cell = (row.getWidth() - gap * 3) / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto c = row.removeFromLeft (cell);
            row.removeFromLeft (gap);
            scenePads[size_t (i)]->setBounds (c);
            auto foot = c.reduced (14, 12).removeFromBottom (Dine::Metric::control);
            const int keepW = juce::jmax (52, sceneKeeps[size_t (i)]->idealWidth());
            sceneKeeps[size_t (i)]->setBounds (foot.removeFromLeft (keepW));
        }
    }
    {
        auto inner = l.monitor.reduced (18, 16);
        inner.removeFromTop (22 + 16 + kMonCaptionGap);
        auto chipRow = inner.removeFromTop (Dine::Metric::control);
        bool room = true;
        for (auto& c : chips)
        {
            const int w = juce::jmax (44, c->idealWidth());
            // A chip with no room left is taken off the row, not left lying where the last
            // layout put it - which is how DIM and CLEAR SOLO came to be drawn over each other.
            room = room && w <= chipRow.getWidth();
            c->setVisible (room);
            if (! room) continue;
            c->setBounds (chipRow.removeFromLeft (w));
            chipRow.removeFromLeft (8);
        }
        // The solo device picker takes the right end of the chip row, when there is a row's worth left for it.
        const int pickW = juce::jmin (300, juce::jmax (150, soloDevice.idealWidth()));
        soloDevice.setVisible (chipRow.getWidth() >= 150);
        soloDevice.setBounds (chipRow.removeFromRight (juce::jmin (pickW, chipRow.getWidth())));
        inner.removeFromTop (kMonRowGap);
        auto levelRow = inner.removeFromTop (Dine::Metric::control);
        levelRow.removeFromLeft (94);
        levelRow.removeFromRight (52 + 12);
        monitorLevel.setBounds (levelRow);
    }
    {
        auto inner = l.safe.reduced (18, 16);
        const int w = juce::jmax (110, liveSafeButton.idealWidth());
        liveSafeButton.setBounds (inner.removeFromTop (Dine::Metric::button).removeFromLeft (w));
        // Under the rules, where an engineer already is when something has gone wrong: the
        // list of every mix this session has had, with the time and the name of each.
        const int hw = juce::jmax (110, historyButton.idealWidth());
        historyButton.setBounds (inner.removeFromBottom (Dine::Metric::button).removeFromLeft (hw));
    }
    {
        auto inner = l.autopilot.reduced (18, 16);
        const int w = juce::jmax (110, autopilotButton.idealWidth());
        autopilotButton.setBounds (inner.removeFromTop (Dine::Metric::button).removeFromLeft (w));
    }
}

} // namespace livemix

namespace livemix
{

// The pads read the controller: the name, and lit while that scene is the one kept.
void LivePage::refreshScenes()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto& scene = controller.getScene (i);
        auto& pad = *scenePads[size_t (i)];
        juce::String when;
        if (scene.kept && scene.whenMs > 0) when = juce::Time (scene.whenMs).toString (false, true, false, true);
        pad.setScene (juce::String (scene.name.empty() ? defaultSceneName (i) : scene.name.c_str()), scene.kept, when);
        pad.setTooltip (scene.kept ? "Bring the " + juce::String (scene.name) + " mix back - every fader, chain and macro - in one press. UNDO takes it back."
                                   : "Nothing is kept here yet. Set the mix, then KEEP.");
    }
}

} // namespace livemix
