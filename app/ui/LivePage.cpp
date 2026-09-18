#include "LivePage.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

namespace
{
    constexpr int kGroupBuses = int (MixBus::Master);
    constexpr int kTiles = kGroupBuses + 1;
    constexpr int kPadX = 24, kPadY = 22, kGap = 20;
    constexpr int kStatusH = 100;
    constexpr int kSafeW = 330;

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
        fader.setSliderStyle (juce::Slider::LinearHorizontal);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        Dine::dragOnly (fader);
        fader.setRange (-60.0, 12.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.getProperties().set ("dineFader", true);
        fader.setTooltip (isFx() ? "Level for every effect return together. Double-click for 0.0 dB, which is what TUNE MIX set."
                                 : "Level for the whole group. Double-click for 0.0 dB.");
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (isFx()) controller.setFxReturn (float (fader.getValue()));
            else        controller.setBusFader (MixBus (group), float (fader.getValue()));
            repaint (readout);
        };

        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        mute.setTooltip (isFx() ? "Mute the effects: the reverbs and delays leave the mix, the sources stay."
                                : "Muted: the whole group is not heard");
        solo.setTooltip (isFx() ? "The FX returns have nothing to solo against: they only carry what other channels send them."
                                : "Soloed: this group and nothing else");
        solo.setEnabled (! isFx());
        mute.onClick = [this]
        {
            if (isFx()) controller.setFxMute (! controller.getBase().fxMute);
            else        controller.setBusMute (MixBus (group), ! controller.getBase().buses[size_t (group)].mute);
        };
        solo.onClick = [this] { if (! isFx()) controller.setBusSolo (MixBus (group), ! controller.getBase().buses[size_t (group)].solo); };
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
            const auto& b = p.buses[size_t (group)];
            faderDb = b.faderDb; m = b.mute; s = b.solo;
            isUsed = controller.isPrepared() && controller.getEngine().isBusUsed (MixBus (group));
            if (isUsed) peak = controller.getEngine().getBus (MixBus (group)).getOutputMeter().consumeMaxPeakDb();
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
        auto chip = head.removeFromRight (chipW).withSizeKeepingCentre (chipW, 18).toFloat();
        Dine::fillRounded (g, chip, muted ? Dine::keyMute : soloed ? Dine::accent : Dine::control, Dine::Radius::chip);
        g.setColour (muted || soloed ? Dine::onAccent : Dine::ink3);
        g.setFont (chipFont);
        g.drawText (state, chip, juce::Justification::centred);
        g.setColour (used ? tint() : Dine::ink4);
        g.setFont (Dine::caps (13.0f, 0.06f));
        g.drawText (name(), head, juce::Justification::centredLeft, true);

        g.setColour (! used ? Dine::ink4 : Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        g.drawText (used ? dbText (float (fader.getValue())) : Glyph::dash(), readout, juce::Justification::centredRight);
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
        mute.setBounds (keys.removeFromLeft ((keys.getWidth() - 6) / 2));
        keys.removeFromLeft (6);
        solo.setBounds (keys);
    }

private:
    bool isFx() const noexcept { return group >= kGroupBuses; }
    juce::Colour tint() const { return isFx() ? Dine::ink2 : Dine::busTint (MixBus (group)); }
    juce::String name() const { return isFx() ? "FX RETURNS" : juce::String (mixBusName (MixBus (group))).toUpperCase(); }

    MixController& controller;
    int group;
    bool used = true, muted = false, soloed = false, updating = false;
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
        g.drawText (recording ? "STOP" : "RECORD", inner.withTrimmedLeft (int (d) + 8), juce::Justification::centredLeft, false);
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
    liveSafeButton.onClick = [this]
    {
        auto& daw = services.daw();
        daw.setLiveSafe (! daw.isLiveSafe());
        services.saveSession();
        if (onLiveSafeChanged) onLiveSafeChanged();
        if (onToast) onToast (daw.isLiveSafe() ? "LIVE SAFE on. The sound is locked: re-routes and re-tunes are blocked."
                                               : "LIVE SAFE off. Re-routes and re-tunes are allowed again.");
        refresh();
        repaint();
    };

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
    repaint (layout().monitor);
}

void LivePage::rebuild()
{
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
                     : ! next.routed ? "Solo has nowhere to go yet. Pick the device you listen on in Outputs."
                     : next.soloCount > 0 ? juce::String (next.soloCount) + (next.soloCount == 1 ? " channel soloed. Only you hear it." : " channels soloed. Only you hear it.")
                                          : "Press S on any channel to hear it. Only you hear it.";
    if (next.soloCount != look.soloCount || next.inPlace != look.inPlace) refreshMonitor();
    liveSafeButton.setButtonText (next.safe ? "LIVE SAFE ON" : "LIVE SAFE OFF");
    liveSafeButton.setStyle (next.safe ? DineButton::Style::Filled : DineButton::Style::Standard);
    if (next != look) { look = next; repaint(); }
}

LivePage::Layout LivePage::layout() const
{
    Layout l;
    auto r = getLocalBounds().reduced (kPadX, kPadY);
    l.status = r.removeFromTop (kStatusH);
    r.removeFromTop (kGap);
    l.tiles = r.removeFromTop (juce::jmin (200, juce::jmax (180, r.getHeight() - kGap - 190)));
    r.removeFromTop (kGap);
    auto lower = r.withHeight (juce::jlimit (190, 260, r.getHeight()));
    l.safe = lower.removeFromRight (kSafeW);
    lower.removeFromRight (kGap);
    l.monitor = lower;
    return l;
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto l = layout();

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
            g.drawText (label, inner.removeFromTop (14), juce::Justification::topLeft);
            inner.removeFromTop (6);
            g.setColour (valueInk);
            g.setFont (monoValue ? Dine::mono (19.0f, 500) : Dine::text (19.0f, 600));
            g.drawText (value, inner.removeFromTop (24), juce::Justification::topLeft, true);
            inner.removeFromTop (4);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            g.drawFittedText (note, inner, juce::Justification::topLeft, 2, 1.0f);
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

    // ---- the engineer's listen
    {
        Dine::fillRounded (g, l.monitor.toFloat(), Dine::tile, Dine::Radius::card);
        auto inner = l.monitor.reduced (18, 18);
        Dine::drawSection (g, inner.removeFromTop (14), "ENGINEER MONITORING  " + juce::String (Glyph::dot()) + "  THE ROOM AND THE STREAM DO NOT HEAR THIS");
        auto levelRow = inner.withTrimmedTop (14 + Dine::Metric::control + 18).withHeight (Dine::Metric::control);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawText ("Monitor level", levelRow.removeFromLeft (110), juce::Justification::centredLeft);
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (12.0f, 500));
        g.drawText (dbText (look.monitorDb), levelRow.removeFromRight (58), juce::Justification::centredRight);
        auto note = inner.withTrimmedTop (14 + Dine::Metric::control + 18 + Dine::Metric::control + 12).withHeight (18);
        g.setColour (look.inPlace || ! look.routed ? Dine::warn : look.soloCount > 0 ? Dine::accent : Dine::ink3);
        g.setFont (Dine::text (12.5f));
        g.drawText (look.monitorNote, note, juce::Justification::centredLeft, true);
    }

    // ---- LIVE SAFE: what it locks, blocks and allows, printed
    {
        Dine::fillRounded (g, l.safe.toFloat(), Dine::tile, Dine::Radius::card);
        auto inner = l.safe.reduced (18, 18);
        inner.removeFromTop (44 + 8);
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
            if (inner.getHeight() < 30) break;
            const int h = juce::jmin (inner.getHeight(), 13 + 16 + 5 + 34 + 13);
            auto box = inner.removeFromTop (h);
            Dine::fillRounded (g, box.toFloat(), Dine::item, Dine::Radius::control);
            auto t = box.reduced (12, 12);
            g.setColour (rule.amber ? Dine::warn : Dine::ink);
            g.setFont (Dine::text (13.0f));
            g.drawText (rule.what, t.removeFromTop (16), juce::Justification::topLeft);
            t.removeFromTop (4);
            g.setColour (Dine::ink3);
            g.setFont (Dine::text (12.0f));
            g.drawFittedText (rule.why, t, juce::Justification::topLeft, 3, 1.0f);
            inner.removeFromTop (3);
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
        auto inner = l.monitor.reduced (18, 18);
        inner.removeFromTop (14);
        auto chipRow = inner.removeFromTop (Dine::Metric::control);
        for (auto& c : chips)
        {
            const int w = juce::jmax (44, c->idealWidth());
            if (w > chipRow.getWidth()) break;
            c->setBounds (chipRow.removeFromLeft (w));
            chipRow.removeFromLeft (8);
        }
        inner.removeFromTop (18);
        auto levelRow = inner.removeFromTop (Dine::Metric::control);
        levelRow.removeFromLeft (110);
        levelRow.removeFromRight (58 + 14);
        monitorLevel.setBounds (levelRow);
    }
    {
        auto inner = l.safe.reduced (18, 18);
        liveSafeButton.setBounds (inner.removeFromTop (44));
    }
}

} // namespace livemix
