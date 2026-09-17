#include "LivePage.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

namespace
{
    // The band along the foot of LIVE: the lock on the left, the engineer's own listen on the
    // right. Tall enough for three sentences of what LIVE SAFE actually refuses, because
    // "this is locked" without saying what is locked is the problem it exists to solve.
    constexpr int kFootHeight = 114;
}

// One group: a name, a meter, a fader big enough to find without looking, and the two
// keys that can change what the room hears. Whatever state the group is in - muted,
// soloed - the tile says so in colour and in words: during a service nobody should have
// to work out why a group has gone quiet.
class LivePage::GroupFader : public juce::Component
{
public:
    // A tile stands for a group bus, or for the effects returns taken together: a console gives
    // the returns a fader too, and during a sermon "take the reverb out" has to be one press.
    // There is nothing to solo a return against, so the FX tile offers MUTE and no more.
    enum class Kind { Bus, Fx };

    GroupFader (MixController& c, MixBus b, Kind k = Kind::Bus) : controller (c), bus (b), kind (k)
    {
        addAndMakeVisible (meter);
        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        Dine::dragOnly (fader);
        fader.setRange (-60.0, 12.0, 0.1);   // the same throw as the mixer's bus faders
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.getProperties().set ("dineFader", true);
        fader.setTooltip (isFx() ? "Level for every effect return together. Double-click for 0.0 dB, which is what TUNE MIX set."
                                 : "Level for the whole group. Double-click for 0.0 dB.");
        fader.onValueChange = [this]
        {
            if (isFx()) controller.setFxReturn (float (fader.getValue()));
            else        controller.setBusFader (bus, float (fader.getValue()));
        };

        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        mute.setTooltip (isFx() ? "Mute the effects: the reverbs and delays leave the mix, the sources stay."
                                : "Mute this group.");
        solo.setTooltip ("Solo: hear this group alone.");
        solo.setVisible (! isFx());
        mute.onClick = [this]
        {
            if (isFx()) controller.setFxMute (! controller.getBase().fxMute);
            else        controller.setBusMute (bus, ! controller.getBase().buses[size_t (bus)].mute);
        };
        solo.onClick = [this] { controller.setBusSolo (bus, ! controller.getBase().buses[size_t (bus)].solo); };
    }

    void refresh()
    {
        if (isFx()) { refreshFx(); return; }
        const auto& b = controller.getBase().buses[size_t (bus)];
        if (! fader.isMouseButtonDown()) fader.setValue (b.faderDb, juce::dontSendNotification);
        levelText = juce::String (b.faderDb >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (b.faderDb), 1);

        float peak = -120.0f;
        if (controller.isPrepared())
            peak = controller.getEngine().getBus (bus).getOutputMeter().consumeMaxPeakDb();
        meter.setLevels (peak, peak, peak > -0.2f);
        meter.setMuted (b.mute);
        peakDb = meter.getPeakDb();
        peakText = peakDb <= -60.0f ? Glyph::dash() : juce::String (peakDb, 1);

        if (b.mute != muted || b.solo != soloed)
        {
            muted = b.mute;
            soloed = b.solo;
            mute.setOn (muted);
            mute.setLetter (muted ? "MUTED" : "MUTE");
            solo.setOn (soloed);
        }
        repaint();
    }

    // The returns have no bus of their own in the engine - they sum straight into the master -
    // so the tile reads the loudest of the returns that are actually in use, the same way the
    // TUNE workspace's FX meter does.
    void refreshFx()
    {
        const auto& p = controller.getBase();
        if (! fader.isMouseButtonDown()) fader.setValue (p.fxReturnDb, juce::dontSendNotification);
        levelText = juce::String (p.fxReturnDb >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (p.fxReturnDb), 1);

        float peak = -120.0f;
        int returns = 0;
        if (controller.isPrepared())
        {
            const auto& engine = controller.getEngine();
            for (int f = 0; f < int (FxSlot::Count); ++f)
                if (engine.isFxUsed (FxSlot (f)))
                {
                    ++returns;
                    peak = juce::jmax (peak, engine.getFx (FxSlot (f)).getOutputMeter().consumeMaxPeakDb());
                }
        }
        used = returns > 0;
        meter.setLevels (peak, peak, peak > -0.2f);
        meter.setMuted (p.fxMute || ! used);
        peakDb = meter.getPeakDb();
        peakText = peakDb <= -60.0f ? Glyph::dash() : juce::String (peakDb, 1);

        if (p.fxMute != muted)
        {
            muted = p.fxMute;
            mute.setOn (muted);
            mute.setLetter (muted ? "MUTED" : "MUTE");
        }
        fader.setEnabled (used);
        mute.setEnabled (used);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto ground = Dine::card;
        if (muted)       ground = ground.overlaidWith (Dine::keyMute.withAlpha (0.10f)).darker (0.18f);
        else if (soloed) ground = ground.overlaidWith (Dine::accent.withAlpha (0.10f));
        Dine::drawCard (g, r, ground, muted ? Dine::keyMute.withAlpha (0.45f)
                                            : soloed ? Dine::accent.withAlpha (0.5f) : Dine::hair);

        // the group's colour along the top edge, the way the console strip carries it
        {
            juce::Path cap;
            cap.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), 8.0f,
                                     Dine::Radius::card, Dine::Radius::card, true, true, false, false);
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, getWidth(), 3));
            g.setColour (tint().withAlpha (muted ? 0.30f : 0.85f));
            g.fillPath (cap);
            g.restoreState();
        }

        auto inner = getLocalBounds().reduced (12, 0);
        auto head = inner.removeFromTop (34).withTrimmedTop (10);
        g.setColour (muted ? Dine::keyMute : Dine::ink);
        g.setFont (Dine::text (13.0f, 700).withExtraKerningFactor (0.06f));
        g.drawText (isFx() ? "FX" : juce::String (mixBusName (bus)).toUpperCase(), head, juce::Justification::centred, true);
        Dine::drawRule (g, inner.withHeight (1), Dine::hairSoft);

        // the readouts under the throw: what it is set to, and what is coming through
        auto feet = getLocalBounds().reduced (12, 0).withTrimmedBottom (kMuteH + 16);
        auto row = feet.removeFromBottom (16);
        g.setColour (muted ? Dine::ink4 : Dine::ink);
        const auto levelFont = Dine::mono (12.0f, 600);
        g.setFont (levelFont);
        // The unit gives way before the number does: a narrow tile still reads its level.
        auto levelCell = row.removeFromLeft (row.getWidth() / 2);
        const juce::String withUnit = levelText + " dB";
        g.drawText (Dine::textWidth (levelFont, withUnit) <= levelCell.getWidth() ? withUnit : levelText,
                    levelCell, juce::Justification::centredLeft);
        g.setColour (muted || peakText == Glyph::dash() ? Dine::ink4 : Dine::levelColour (peakDb));
        g.setFont (Dine::mono (11.0f));
        g.drawText (peakText, row, juce::Justification::centredRight);

        if (isFx() && ! used)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (10.0f, 700).withExtraKerningFactor (0.10f));
            g.drawText ("NONE IN THIS MIX", feet.removeFromBottom (14), juce::Justification::centred, false);
        }
        else if (muted)
        {
            g.setColour (Dine::keyMute.withAlpha (0.85f));
            g.setFont (Dine::text (10.0f, 700).withExtraKerningFactor (0.10f));
            g.drawText ("NOT HEARD", feet.removeFromBottom (14), juce::Justification::centred, false);
        }
        else if (soloed)
        {
            g.setColour (Dine::accent);
            g.setFont (Dine::text (10.0f, 700).withExtraKerningFactor (0.10f));
            g.drawText ("SOLO", feet.removeFromBottom (14), juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        r.removeFromTop (26);
        auto keys = r.removeFromBottom (kMuteH);
        if (isFx())
        {
            mute.setBounds (keys);          // nothing to solo a return against
        }
        else
        {
            // MUTE is the key that gets reached for, so it takes the room that is left - but
            // SOLO is given the width its word actually needs first, because a key that reads
            // "SOL" is a key nobody trusts at arm's length.
            const int ideal = Dine::textWidth (Dine::text (11.5f, 600), "SOLO") + 16;
            const int soloW = juce::jlimit (34, juce::jmax (34, (keys.getWidth() - 8) / 2), ideal);
            solo.setBounds (keys.removeFromRight (soloW));
            keys.removeFromRight (8);
            mute.setBounds (keys);
        }
        r.removeFromBottom (16 + 14 + 8);          // the readouts and the state word

        // The throw and its meter are one object, centred in the tile: a group fader is
        // meant to be found with a glance and moved with one hand.
        auto body = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 46 + 10 + 18), r.getHeight());
        fader.setBounds (body.removeFromLeft (46));
        body.removeFromLeft (10);
        meter.setBounds (body.removeFromLeft (18));
    }

private:
    bool isFx() const noexcept { return kind == Kind::Fx; }
    juce::Colour tint() const { return isFx() ? Dine::ink2 : Dine::busTint (bus); }

    static constexpr int kMuteH = 30;

    MixController& controller;
    MixBus bus;
    Kind kind = Kind::Bus;
    bool used = true;                 // FX: whether this session has any returns at all
    DineMeter meter { DineMeter::Style::Segments };
    juce::Slider fader;
    DineKey mute { "MUTE", Dine::keyMute };
    DineKey solo { "SOLO", Dine::keySolo };
    juce::String levelText { "+0.0" }, peakText { Glyph::dash() };
    float peakDb = -120.0f;
    bool muted = false, soloed = false;
};

// The transport's Record, sized for the room rather than for a toolbar: on LIVE the
// operator is standing, the screen is across the desk, and this is the button that has to
// be found without looking. It carries the record dot and the only red on the page.
class LivePage::RecordKey : public juce::Button
{
public:
    RecordKey() : juce::Button ("Record")
    {
        setTooltip ("Record: captures every track with its R key on (R). LIVE SAFE never locks the transport.");
        setClickingTogglesState (false);
    }

    void setRecording (bool r) { if (r != recording) { recording = r; repaint(); } }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const auto ground = recording ? Dine::crit.withAlpha (down ? 1.0f : over ? 0.95f : 0.88f)
                                      : Dine::fill.overlaidWith (Dine::crit.withAlpha (over ? 0.22f : 0.12f));
        Dine::fillRounded (g, r, ground, Dine::Radius::control);
        Dine::hairlineRounded (g, r, recording ? Dine::crit : Dine::crit.withAlpha (0.45f), Dine::Radius::control);

        auto inner = getLocalBounds().reduced (14, 0);
        const float d = 11.0f;
        auto dot = juce::Rectangle<float> (d, d).withCentre ({ float (inner.getX()) + d * 0.5f, r.getCentreY() });
        g.setColour (recording ? Dine::onAccent : Dine::crit);
        if (recording) g.fillRect (dot.reduced (1.5f));      // a square: pressing it stops
        else           g.fillEllipse (dot);

        g.setColour (recording ? Dine::onAccent : Dine::ink);
        g.setFont (Dine::text (13.0f, 600).withExtraKerningFactor (0.06f));
        g.drawText (recording ? "STOP RECORDING" : "RECORD",
                    inner.withTrimmedLeft (int (d) + 10), juce::Justification::centredLeft, false);
    }

    int idealWidth() const
    {
        return 14 * 2 + 11 + 10 + Dine::textWidth (Dine::text (13.0f, 600).withExtraKerningFactor (0.06f),
                                                   "STOP RECORDING");
    }

private:
    bool recording = false;
};

LivePage::LivePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    for (int i = 0; i < int (MixBus::Count); ++i)
    {
        faders[size_t (i)] = std::make_unique<GroupFader> (controller, MixBus (i));
        addAndMakeVisible (*faders[size_t (i)]);
    }
    // ... and the effects returns as one more tile, after the master.
    faders[size_t (MixBus::Count)] = std::make_unique<GroupFader> (controller, MixBus::Master, GroupFader::Kind::Fx);
    addAndMakeVisible (*faders[size_t (MixBus::Count)]);
    recordButton = std::make_unique<RecordKey>();
    addAndMakeVisible (*recordButton);
    recordButton->onClick = [this] { if (onToggleRecord) onToggleRecord(); };

    addAndMakeVisible (liveSafeButton);
    liveSafeButton.setCaps (true);
    liveSafeButton.setClickingTogglesState (false);
    liveSafeButton.setTooltip (juce::String ("Lock the mix for the service. ") + liveSafe::lockedSummary() + " "
                               + liveSafe::allowedSummary());
    liveSafeButton.onClick = [this]
    {
        auto& daw = services.daw();
        // One switch, both halves: the timeline's lock and the mix's policy (DawEngine::setLiveSafe).
        daw.setLiveSafe (! daw.isLiveSafe());
        services.saveSession();
        if (onLiveSafeChanged) onLiveSafeChanged();
        resized();
        repaint();
    };

    // ---- the engineer's own listen ----
    addAndMakeVisible (soloModeButton);
    soloModeButton.setTooltip ("Normally solo goes to your headphones only - the room and the stream never hear "
                               "it, so you can listen to any channel during a service. The other setting mutes "
                               "everything else for everybody, which is only for mixing a recording. "
                               "(Engineers: monitor solo / solo in place.)");
    soloModeButton.onClick = [this]
    {
        juce::PopupMenu m;
        const auto mode = controller.getMonitor().mode;
        m.addSectionHeader ("When I press S...");
        m.addItem (1, "...only I hear it, in my headphones", true, mode == SoloMode::Monitor);
        m.addItem (2, "...everyone hears it on its own  (recording only)", true, mode == SoloMode::InPlace);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soloModeButton).withMinimumWidth (330),
                         [this] (int id)
                         {
                             if (id <= 0) return;
                             controller.setSoloMode (id == 2 ? SoloMode::InPlace : SoloMode::Monitor);
                             refreshMonitor();
                             repaint();
                         });
    };

    addAndMakeVisible (soloPointButton);
    soloPointButton.setTooltip ("\"In the mix\" hears the channel where it sits - panned, and silent if it is "
                                "muted. \"On its own\" hears it as it arrives, whatever its fader and mute are "
                                "doing, which is how you find a problem. (Engineers: AFL / PFL.)");
    soloPointButton.onClick = [this]
    {
        controller.setSoloPoint (controller.getMonitor().point == SoloPoint::PFL ? SoloPoint::AFL : SoloPoint::PFL);
        refreshMonitor();
    };

    addAndMakeVisible (dimButton);
    dimButton.setTooltip ("Drop your headphones to talk to someone, without losing the level you had set.");
    dimButton.onClick = [this] { controller.setMonitorDim (! controller.getMonitor().dim); refreshMonitor(); };

    addAndMakeVisible (clearSoloButton);
    clearSoloButton.setTooltip ("Stop listening to everything you have soloed, all at once.");
    clearSoloButton.onClick = [this] { controller.clearSolos(); refreshMonitor(); repaint(); };

    addAndMakeVisible (monitorLevel);
    monitorLevel.setRange (-40.0, 12.0, 0.5);
    monitorLevel.setValue (0.0, juce::dontSendNotification);
    monitorLevel.setTooltip ("How loud your headphones are. Nothing to do with the mix anyone else hears.");
    Dine::dragOnly (monitorLevel);
    monitorLevel.onValueChange = [this] { controller.setMonitorGain (float (monitorLevel.getValue())); };

    refreshMonitor();
}

// The monitor controls say what they are doing at a glance: an engineer glancing at this
// during a service needs "is anything soloed, and can I hear it" answered without reading.
void LivePage::refreshMonitor()
{
    const auto& m = controller.getMonitor();
    const bool inPlace = m.mode == SoloMode::InPlace;
    soloModeButton.setValue (inPlace ? "Everyone hears it" : "Only I hear it");
    soloPointButton.setButtonText (m.point == SoloPoint::PFL ? "On its own" : "In the mix");
    soloPointButton.setStyle (DineButton::Style::Toggle);
    soloPointButton.setToggleState (m.point == SoloPoint::PFL, juce::dontSendNotification);
    dimButton.setStyle (DineButton::Style::Toggle);
    dimButton.setToggleState (m.dim, juce::dontSendNotification);
    soloCount = controller.numSoloed();
    clearSoloButton.setEnabled (soloCount > 0);
    clearSoloButton.setStyle (DineButton::Style::Toggle);
    clearSoloButton.setToggleState (soloCount > 0, juce::dontSendNotification);
    monitorRouted = controller.hasMonitorOutput();
    if (std::fabs (monitorLevel.getValue() - double (m.gainDb)) > 0.01)
        monitorLevel.setValue (m.gainDb, juce::dontSendNotification);
    repaint();
}

LivePage::~LivePage() = default;

void LivePage::rebuild()
{
    for (auto& f : faders) if (f != nullptr) f->refresh();
    repaint();
}

void LivePage::refresh()
{
    for (auto& f : faders) if (f != nullptr) f->refresh();

    health = controller.getMixHealthPercent();
    if (controller.isPrepared())
    {
        const float masterPeak = controller.getEngine().getBus (MixBus::Master).getOutputMeter().getMaxPeakDb();
        headroomDb = -masterPeak;
    }

    const auto& transport = services.daw().getTransport();
    auto& daw = services.daw();
    const bool recording = daw.isRecording();
    clock = Transport::formatTime (transport.getPositionSeconds());
    state = recording                 ? "RECORDING"
          : transport.isPlaying()     ? "PLAYING"
          : services.isAudioRunning() ? "READY" : "NO DEVICE";

    if (recording != recordingOn) { recordingOn = recording; recordButton->setRecording (recording); resized(); }
    recordButton->setEnabled (services.isAudioRunning());

    updateDiskNote();

    // LIVE SAFE is a lock: it has to read as on or off from the back of the room, so the
    // button fills and says which it is, rather than relying on a toggle nobody can see.
    const bool safe = services.daw().getProject().liveSafe;
    if (safe != liveSafeOn)
    {
        liveSafeOn = safe;
        liveSafeButton.setStyle (safe ? DineButton::Style::Filled : DineButton::Style::Standard);
        liveSafeButton.setButtonText (safe ? "LIVE SAFE ON" : "LIVE SAFE");
        liveSafeButton.setIcon (safe ? Dine::Icon::Check : Dine::Icon::None);
        resized();
    }
    // Solo can be pressed from four other places, so this page follows rather than owns it.
    if (soloCount != controller.numSoloed() || monitorRouted != controller.hasMonitorOutput()) refreshMonitor();

    // The tiles repaint themselves; the page around them is cards of text, and text laid out
    // thirty times a second for a clock that reads the same is the kind of waste that makes an
    // app feel heavier than it is. So the page only repaints when what it says has changed.
    juce::String notes;
    for (const auto& n : controller.getMixHealthNotes()) notes << juce::String (n) << "|";
    const Look now { clock, state, stateNote, services.currentOutputDevice(), notes,
                     health, int (std::lround (headroomDb * 10.0f)),
                     recordingOn, liveSafeOn, services.isAudioRunning(),
                     soloCount, services.xrunCount(), controller.getTuneCount(),
                     monitorRouted, controller.getMonitor().mode == SoloMode::InPlace };
    if (now != look) { look = now; repaint(); }
}

// What the STATE card says under its word. While a take is running this is the one number
// an operator watches: how long it has been recording, and how long the disk will last.
// Before it starts, it says whether anything is armed and what that would cost.
void LivePage::updateDiskNote()
{
    auto& daw = services.daw();
    const bool recording = daw.isRecording();

    if (--diskTicks <= 0)                       // ~ every 2 s at 30 Hz, never per frame
    {
        diskTicks = 60;
        secondsFree = daw.getRecordingSecondsFree();
    }

    auto span = [] (double seconds) -> juce::String
    {
        const int total = int (seconds);
        if (total >= 24 * 3600) return "a day or more";      // past this the number is noise, not information
        if (total >= 2 * 3600)  return juce::String (total / 3600) + " h";
        if (total >= 3600)      return "1 h " + juce::String ((total / 60) % 60) + " m";
        return juce::String (juce::jmax (0, total / 60)) + " m";
    };

    const int armed = daw.getProject().numArmed();
    const juce::String room = secondsFree > 0.0 ? span (secondsFree) + " of room left" : juce::String();
    const bool tight = secondsFree > 0.0 && secondsFree < 15.0 * 60.0;

    stateNoteColour = tight ? Dine::crit : Dine::ink3;
    if (recording)
    {
        stateNote = "TAKE " + Transport::formatTime (daw.getRecordingSeconds()).dropLastCharacters (4)
                  + (room.isEmpty() ? juce::String() : "   " + Glyph::dot() + "   " + room);
    }
    else if (armed == 0)
    {
        stateNote = "No tracks are set to record. Press the red R on the ones you want, on the Tracks page.";
        stateNoteColour = Dine::warn;
    }
    else
    {
        stateNote = juce::String (armed) + (armed == 1 ? " track set to record" : " tracks set to record")
                  + (room.isEmpty() ? juce::String() : "   " + Glyph::dot() + "   " + room);
    }
}

juce::Rectangle<int> LivePage::body() const
{
    return getLocalBounds().reduced (Dine::Metric::padX, Dine::Metric::padY);
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    auto r = body();

    // ---- title
    auto head = r.removeFromTop (58);
    head.removeFromRight (recordButton->getWidth() + 20);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 700));
    g.drawText (services.currentSessionName().isEmpty() ? "Untitled" : services.currentSessionName(),
                head.removeFromTop (28), juce::Justification::centredLeft, true);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    g.drawText (juce::String (mixPurposeName (controller.getSession().purpose)) + "   " + Glyph::dot() + "   "
                    + juce::String (styleProfileName (controller.getSession().profile)),
                head, juce::Justification::topLeft, true);

    r.removeFromTop (12);

    // ---- the four things that matter during a service
    auto strip = r.removeFromTop (86);
    const int cardWidth = (strip.getWidth() - 3 * 12) / 4;

    auto card = [&] (juce::Rectangle<int> area, const juce::String& caption, const juce::String& value,
                     juce::Colour colour, const juce::String& note, juce::Colour noteColour = Dine::ink3)
    {
        Dine::drawCard (g, area.toFloat());
        auto inner = area.reduced (16, 13);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        g.drawText (caption, inner.removeFromTop (14), juce::Justification::topLeft);
        g.setColour (colour);
        g.setFont (Dine::mono (24.0f, 500));
        g.drawText (value, inner.removeFromTop (30), juce::Justification::topLeft, true);
        g.setColour (noteColour);
        g.setFont (Dine::text (11.0f));
        g.drawText (note, inner, juce::Justification::topLeft, true);
    };

    const bool recording = services.daw().isRecording();
    card (strip.removeFromLeft (cardWidth), "STATE", state,
          recording ? Dine::crit : (services.isAudioRunning() ? Dine::ok : Dine::ink4),
          stateNote, stateNoteColour);
    strip.removeFromLeft (12);
    card (strip.removeFromLeft (cardWidth), "OUTPUT", services.isAudioRunning() ? "ACTIVE" : "OFF",
          services.isAudioRunning() ? Dine::ok : Dine::ink4,
          services.currentOutputDevice().isEmpty() ? "No output chosen" : services.currentOutputDevice());
    strip.removeFromLeft (12);
    card (strip.removeFromLeft (cardWidth), "MIX HEALTH", health > 0 ? juce::String (health) + "%" : Glyph::dash(),
          health >= 80 ? Dine::ok : health >= 50 ? Dine::warn : Dine::ink4,
          controller.getTuneCount() > 0 ? "Tuned" : "Not tuned yet");
    strip.removeFromLeft (12);
    card (strip, "HEADROOM", juce::String (headroomDb, 1) + " dB",
          headroomDb < 0.5f ? Dine::crit : headroomDb < 3.0f ? Dine::warn : Dine::ok,
          services.xrunCount() > 0 ? juce::String (services.xrunCount()) + " drops" : "No drops");

    r.removeFromTop (16);
    r.removeFromBottom (kFootHeight);    // the LIVE SAFE and MONITOR cards, laid out in resized()

    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 600));
    g.drawText ("GROUPS", r.removeFromTop (18), juce::Justification::topLeft);

    // Under the groups: what an operator would want to know without opening anything.
    r.removeFromTop (juce::jlimit (240, 460, r.getHeight() - 152) + 20);
    if (r.getHeight() > 60)
    {
        auto card = r.removeFromTop (juce::jmin (r.getHeight(), 132));
        Dine::drawCard (g, card.toFloat());
        auto inner = card.reduced (18, 14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        g.drawText ("WHAT TO WATCH", inner.removeFromTop (16), juce::Justification::topLeft);
        inner.removeFromTop (4);

        const auto notes = controller.getMixHealthNotes();
        if (notes.empty())
        {
            g.setColour (Dine::ok);
            g.setFont (Dine::text (12.5f));
            g.drawText (controller.getTuneCount() > 0 ? "Every input is heard and at a healthy level."
                                                      : "Nothing is wrong. Run TUNE MIX when the band is playing.",
                        inner.removeFromTop (18), juce::Justification::topLeft, true);
        }
        else
        {
            g.setFont (Dine::text (12.5f));
            for (const auto& note : notes)
            {
                if (inner.getHeight() < 18) break;
                g.setColour (Dine::ink2);
                g.drawText (Glyph::dot() + juce::String ("  ") + juce::String (note),
                            inner.removeFromTop (18), juce::Justification::topLeft, true);
            }
        }
    }

    // ---- the lock and the engineer's listen, side by side along the foot
    //
    // Both answer a question an operator asks mid-service without wanting to read anything:
    // "is this locked, and what does that stop" on the left, "is anything soloed, and can I
    // hear it" on the right. Neither is a tooltip: what LIVE SAFE actually refuses is printed.
    {
        auto foot = body().removeFromBottom (kFootHeight);
        auto left = foot.removeFromLeft (foot.getWidth() * 3 / 5);
        foot.removeFromLeft (12);
        auto right = foot;

        // ---- LIVE SAFE
        Dine::drawCard (g, left.toFloat(), liveSafeOn ? Dine::card.brighter (0.04f) : Dine::card);
        if (liveSafeOn) Dine::hairlineRounded (g, left.toFloat(), Dine::warn.withAlpha (0.35f), Dine::Radius::card);
        auto inner = left.reduced (16, 12);
        inner.removeFromLeft (liveSafeButton.getWidth() + 16);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f, 700).withExtraKerningFactor (0.06f));
        g.drawText (liveSafeOn ? "LOCKED FOR THE SERVICE" : "NOT LOCKED", inner.removeFromTop (14),
                    juce::Justification::topLeft);
        inner.removeFromTop (2);
        g.setColour (liveSafeOn ? Dine::warn : Dine::ink2);
        g.setFont (Dine::text (11.5f));
        g.drawText (liveSafeOn ? juce::String ("Refused: ") + liveSafe::lockedSummary()
                               : juce::String ("Lock the mix before the service starts, so nothing can change it by accident."),
                    inner.removeFromTop (16), juce::Justification::topLeft, true);
        g.setColour (Dine::ink3);
        g.drawText (liveSafeOn ? juce::String ("Still working: ") + liveSafe::allowedSummary()
                               : juce::String ("Faders, mutes, solo, the monitor, the transport and recording always keep working."),
                    inner.removeFromTop (16), juce::Justification::topLeft, true);
        if (liveSafeOn)
        {
            const auto& policy = controller.getLiveSafePolicy();
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.0f));
            g.drawText ("Limited: a channel fader moves at most " + juce::String (int (policy.maxFaderStepDb))
                            + " dB at a time, the master " + juce::String (int (policy.maxMasterStepDb)) + " dB.",
                        inner.removeFromTop (15), juce::Justification::topLeft, true);
        }

        // ---- MONITOR
        Dine::drawCard (g, right.toFloat());
        auto m = right.reduced (16, 12);
        auto caption = m.removeFromTop (14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f, 700).withExtraKerningFactor (0.06f));
        g.drawText ("WHAT I HEAR", caption.removeFromLeft (110), juce::Justification::topLeft);
        if (soloCount > 0)
        {
            g.setColour (Dine::accent);
            g.drawText (juce::String (soloCount) + (soloCount == 1 ? " CHANNEL SOLOED" : " CHANNELS SOLOED"),
                        caption, juce::Justification::topLeft);
        }
        // The two rows of controls are laid out in resized(); the sentence goes under them.
        m.removeFromTop (2 + Dine::Metric::control + 6 + Dine::Metric::control + 4);

        const bool inPlace = controller.getMonitor().mode == SoloMode::InPlace;
        juce::String note;
        juce::Colour noteColour = Dine::ink3;
        if (inPlace)
        {
            note = "Careful: pressing S is heard by the room and the stream too.";
            noteColour = Dine::warn;
        }
        else if (! monitorRouted)
        {
            note = "Solo has nowhere to go yet. Pick the device you listen on in Outputs.";
            noteColour = Dine::warn;
        }
        else
        {
            note = "Press S on any channel to hear it. Only you hear it.";
            noteColour = Dine::ok;
        }
        g.setColour (noteColour);
        g.setFont (Dine::text (11.0f));
        g.drawText (note, m.removeFromTop (16), juce::Justification::topLeft, true);
    }
}

void LivePage::resized()
{
    auto r = body();
    {
        // Record sits on the title line, at the far edge: the biggest, reddest thing on the page.
        auto head = r.removeFromTop (58);
        const int w = juce::jmax (170, recordButton->idealWidth());
        recordButton->setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, 38));
    }
    r.removeFromTop (12 + 86 + 16 + 18);
    auto foot = r.removeFromBottom (kFootHeight);
    auto left = foot.removeFromLeft (foot.getWidth() * 3 / 5);
    foot.removeFromLeft (12);
    auto right = foot;

    {
        auto cell = left.reduced (16, 12);
        const int w = juce::jmax (140, liveSafeButton.idealWidth());
        liveSafeButton.setBounds (cell.removeFromLeft (w).withSizeKeepingCentre (w, 32));
    }
    {
        auto cell = right.reduced (16, 12);
        cell.removeFromTop (14 + 2);
        auto row = cell.removeFromTop (Dine::Metric::control);
        soloModeButton.setBounds (row.removeFromLeft (juce::jmin (150, row.getWidth() / 2)));
        row.removeFromLeft (6);
        soloPointButton.setBounds (row.removeFromLeft (52));
        row.removeFromLeft (6);
        dimButton.setBounds (row.removeFromLeft (juce::jmax (48, dimButton.idealWidth())));

        cell.removeFromTop (6);
        auto row2 = cell.removeFromTop (Dine::Metric::control);
        clearSoloButton.setBounds (row2.removeFromLeft (juce::jmax (92, clearSoloButton.idealWidth())));
        row2.removeFromLeft (10);
        monitorLevel.setBounds (row2);
    }

    const int count = int (faders.size());
    const int gap = 14;
    const int width = juce::jmax (60, (r.getWidth() - gap * (count - 1)) / count);
    // The tiles take the room that is left, down to the "what to watch" card: a fader with
    // nothing under it is wasted height, and one squeezed into 200 px cannot be trusted.
    auto row = r.withHeight (juce::jlimit (240, 460, r.getHeight() - 152));
    for (int i = 0; i < count; ++i)
    {
        faders[size_t (i)]->setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    }
}

} // namespace livemix
