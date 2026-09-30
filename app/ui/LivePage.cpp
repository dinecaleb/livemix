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
    constexpr int kFxTile = kGroupBuses;
    constexpr int kTiles = kGroupBuses + 1;

    // The frame's measures (`06b - Live - decluttered`, 161:18761).
    constexpr int kPadX = 24, kPadTop = 20, kPadBottom = 24, kGap = 16;
    constexpr int kHealthH = 44;
    constexpr int kRailW = 352;
    constexpr int kCardPadX = 16, kCardPadY = 14, kCardGap = 10;
    constexpr int kHeadH = 18;            // a card's header line
    constexpr int kSegmentH = 24;         // a segment inside its track: 4 + 16 + 4
    constexpr int kLevelW = 72;           // the monitor level's line

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

    juce::String clockTime (long long ms)
    {
        return juce::Time (ms).toString (false, true, false, true);
    }

    // How many lines a sentence wraps to at a width, word by word - how tall a card has to be
    // to hold it. Measured in resized(), never in paint.
    int wrapLines (const juce::Font& font, const juce::String& text, int width)
    {
        if (width <= 0 || text.isEmpty()) return 1;
        const int space = Dine::textWidth (font, " ");
        int lines = 1, run = 0;
        for (const auto& word : juce::StringArray::fromTokens (text, " ", ""))
        {
            const int w = Dine::textWidth (font, word);
            if (run > 0 && run + space + w > width) { ++lines; run = w; }
            else run += (run > 0 ? space : 0) + w;
        }
        return lines;
    }

    juce::Font calloutFont()  { return Dine::text (12.0f); }
    juce::Font noteFont()     { return Dine::text (11.0f, 500); }
}

// One group, standing: its colour across the top, its name and where its fader is, a tall
// fader with its meter beside it, and M and S at the foot. A muted strip says "Not heard" in
// words and its meter keeps moving in grey - "nothing is there" and "it is there but not
// heard" are different problems.
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
        fader.getProperties().set ("dineFaderCap", 40);     // the design's 26 x 40 cap
        fader.setTooltip (isFx() ? "Level for every effect return together. Double-click for 0.0 dB, which is what TUNE MIX set."
                                 : "Level for the whole group. Double-click for 0.0 dB.");
        fader.onValueChange = [this]
        {
            if (updating) return;
            if (isFx()) controller.setFxReturn (float (fader.getValue()));
            else        controller.setBusFader (bus(), float (fader.getValue()));
            repaint();
        };

        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        mute.setTooltip (isFx() ? "Mute the effects: the reverbs and delays leave the mix, the sources stay."
                                : "Mute: the whole group is not heard");
        solo.setTooltip (isFx() ? "Solo just the reverbs and delays, so you hear what the sends are adding. Only you hear it."
                                : "Solo this group. Only you hear it.");
        mute.onClick = [this]
        {
            if (isFx()) controller.setFxMute (! controller.getBase().fxMute);
            else        controller.setBusMute (bus(), ! controller.getBase().buses[size_t (bus())].mute);
        };
        solo.onClick = [this]
        {
            if (isFx()) controller.setFxSoloAll (! controller.anyFxSolo());
            else        controller.setBusSolo (bus(), ! controller.getBase().buses[size_t (bus())].solo);
        };
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
            isUsed = controller.isPrepared() && controller.getEngine().isBusUsed (bus());
            if (isUsed) peak = controller.getEngine().getBus (bus()).getOutputMeter().consumeMaxPeakDb();
        }
        updating = true;
        if (! fader.isMouseButtonDown() && std::fabs (faderDb - float (fader.getValue())) > 0.01f)
        {
            fader.setValue (faderDb, juce::dontSendNotification);
            repaint();
        }
        updating = false;
        meter.setLevels (peak, peak, peak > -0.2f);
        meter.setMuted (m || ! isUsed);
        if (m != muted || s != soloed || isUsed != used)
        {
            muted = m; soloed = s; used = isUsed;
            mute.setOn (muted);
            solo.setOn (soloed);
            fader.setEnabled (used);
            mute.setEnabled (used);
            solo.setEnabled (used);
            repaint();
        }
    }

    void lookAndFeelChanged() override
    {
        mute.setTint (Dine::keyMute);
        solo.setTint (Dine::keySolo);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        const auto ground = muted ? Dine::refuse : soloed ? Dine::soloGround : Dine::tile;
        Dine::fillRounded (g, r.toFloat(), ground, 8.0f);
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path round; round.addRoundedRectangle (r.toFloat(), 8.0f);
            g.reduceClipRegion (round);
            g.setColour (used ? tint() : Dine::ink4);
            g.fillRect (r.removeFromTop (2));
        }

        auto inner = getLocalBounds().reduced (kInsetX, 0).withTrimmedTop (16);
        g.setColour (! used ? Dine::ink4 : muted ? Dine::ink3 : Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawFittedText (g, name(), inner.removeFromTop (18), juce::Justification::centred, 1, 0.7f);
        inner.removeFromTop (4);

        auto sub = inner.removeFromTop (14);
        const juce::String state = ! used ? "Off" : muted ? "Not heard" : soloed ? "Solo" : juce::String();
        if (state.isNotEmpty())
        {
            g.setColour (! used ? Dine::ink4 : muted ? Dine::keyMute : Dine::accent);
            g.setFont (Dine::text (11.0f, 600));
            Dine::drawText (g, state, sub, juce::Justification::centred, true);
        }
        else
        {
            g.setColour (Dine::ink2);
            g.setFont (Dine::mono (11.0f, 500));
            Dine::drawText (g, dbText (float (fader.getValue())) + " dB", sub, juce::Justification::centred, true);
        }

    }

    // Unity: the one mark the fader is read against, across the slot. Over the slot rather than
    // under it (the slider paints the slot), and left out while the cap is sitting on it.
    void paintOverChildren (juce::Graphics& g) override
    {
        if (fader.getHeight() <= 0) return;
        const float unity = float (fader.getPositionOfValue (0.0));
        if (std::fabs (unity - float (fader.getPositionOfValue (fader.getValue()))) < 21.0f) return;
        g.setColour (Dine::panMark);
        g.fillRect (juce::Rectangle<float> (float (fader.getX() + 7), float (fader.getY()) + unity, 12.0f, 1.0f));
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (kInsetX, 0);
        inner.removeFromTop (16 + 18 + 4 + 14 + 4 + 16);      // pad, name, gap, level, gap, the throw's own top
        inner.removeFromBottom (14);
        auto keys = inner.removeFromBottom (20);
        inner.removeFromBottom (4 + 12);

        const int faderW = 26, meterW = 6, between = juce::jmin (20, juce::jmax (8, inner.getWidth() - faderW - meterW - 8));
        auto pair = inner.withSizeKeepingCentre (faderW + between + meterW, inner.getHeight());
        fader.setBounds (pair.removeFromLeft (faderW));
        pair.removeFromLeft (between);
        meter.setBounds (pair.reduced (0, 20));               // the meter spans the travel, not the cap

        auto k = keys.withSizeKeepingCentre (28 + 4 + 28, 20);
        mute.setBounds (k.removeFromLeft (28));
        k.removeFromLeft (4);
        solo.setBounds (k);
    }

private:
    static constexpr int kInsetX = 8;
    bool isFx() const noexcept { return group == kFxTile; }
    // A strip's position is the console's order, not the enum's: LEAD sits with the voices.
    MixBus bus() const noexcept { return mixBusInDisplayOrder (group); }
    juce::Colour tint() const { return isFx() ? Dine::busAmbience : Dine::busTint (bus()); }
    juce::String name() const
    {
        if (isFx()) return "FX returns";
        const juce::String raw (mixBusName (bus()));
        return raw.length() <= 3 ? raw.toUpperCase() : raw.substring (0, 1).toUpperCase() + raw.substring (1).toLowerCase();
    }

    MixController& controller;
    int group;
    bool used = true, muted = false, soloed = false, updating = false;
    DineMeter meter { DineMeter::Style::Bar };
    juce::Slider fader;
    DineKey mute { "M", Dine::keyMute };
    DineKey solo { "S", Dine::keySolo };
};

// The small words at the right of a card's header - "What's locked ›" - that go somewhere.
class LivePage::Link : public juce::Button
{
public:
    Link() : juce::Button ("link") { setWantsKeyboardFocus (false); setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void set (const juce::String& t, juce::Colour c)
    {
        if (t == text && c == colour) return;
        text = t; colour = c;
        repaint();
    }
    int idealWidth() const { return Dine::textWidth (noteFont(), text) + 2; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        g.setColour (over || down ? colour.brighter (0.25f) : colour);
        g.setFont (noteFont());
        Dine::drawText (g, text, getLocalBounds(), juce::Justification::centredRight, false);
    }

private:
    juce::String text;
    juce::Colour colour { Dine::ink2 };
};

// The monitor level: a 3 pt line, the part that is on in ink. How loud the engineer's own
// headphones are and nothing to do with the mix anyone else hears.
class LivePage::LevelLine : public juce::Slider
{
public:
    LevelLine()
    {
        setSliderStyle (juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRange (-40.0, 12.0, 0.5);
        setSkewFactorFromMidPoint (-8.0);
        setDoubleClickReturnValue (true, 0.0);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto line = getLocalBounds().toFloat().withSizeKeepingCentre (float (getWidth()), 3.0f);
        Dine::fillRounded (g, line, Dine::control, 1.5f);
        const float on = float (valueToProportionOfLength (getValue()));
        Dine::fillRounded (g, line.withWidth (juce::jmax (3.0f, line.getWidth() * on)),
                           isMouseOverOrDragging() ? Dine::ink2 : Dine::ink3, 1.5f);
    }
};

// What LIVE SAFE locks, blocks and still allows, one press from the card that says it is on.
class LivePage::SafeDetail : public juce::Component
{
public:
    explicit SafeDetail (const LiveSafePolicy& policy)
    {
        rules = { { "Locked", "The audio device, the routing, the input patch and opening a session. Changing any of "
                              "them interrupts the audio mid-service.", Dine::warn },
                  { "Blocked", "TUNE MIX, TUNE LIVE MIX, TUNE CHANNEL and MATCH TO REFERENCE. They re-tune channels "
                               "that are on air.", Dine::warn },
                  { "Allowed", "Faders (" + juce::String (int (policy.maxFaderStepDb)) + " dB at a time, the master "
                                   + juce::String (int (policy.maxMasterStepDb)) + "), mutes, solos, the monitor, "
                                   "markers and recording.", Dine::ok } };
        int h = 16;
        for (const auto& r : rules) h += 16 + 4 + 16 * wrapLines (calloutFont(), r.why, kW - 32) + 12;
        setSize (kW, h + 4);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (16);
        for (const auto& rule : rules)
        {
            g.setColour (rule.tint);
            g.setFont (Dine::text (12.0f, 600));
            Dine::drawText (g, rule.what, r.removeFromTop (16), juce::Justification::centredLeft, true);
            r.removeFromTop (4);
            const int lines = wrapLines (calloutFont(), rule.why, r.getWidth());
            g.setColour (Dine::ink2);
            g.setFont (calloutFont());
            Dine::drawFittedText (g, rule.why, r.removeFromTop (16 * lines), juce::Justification::topLeft, lines, 1.0f);
            r.removeFromTop (12);
        }
    }

private:
    static constexpr int kW = 340;
    struct Rule { juce::String what, why; juce::Colour tint; };
    std::vector<Rule> rules;
};

LivePage::LivePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    for (int i = 0; i < kTiles; ++i)
    {
        tiles[size_t (i)] = std::make_unique<GroupTile> (controller, i);
        addAndMakeVisible (*tiles[size_t (i)]);
    }

    // ---- scenes: pick one; a kept one comes straight back, KEEP writes the mix into the one picked
    for (int i = 0; i < 4; ++i)
    {
        auto seg = std::make_unique<DineButton> (juce::String (defaultSceneName (i)).toUpperCase(), DineButton::Style::Toggle);
        seg->setFontPx (12.0f);
        seg->setPadX (8);
        seg->setClickingTogglesState (false);
        seg->onClick = [this, i]
        {
            sceneSlot = i;
            const auto& scene = controller.getScene (i);
            if (scene.kept) controller.recallScene (i);
            else if (onToast) onToast ("Nothing is kept under " + juce::String (scene.name.empty() ? defaultSceneName (i) : scene.name.c_str())
                                       + " yet. Set the mix, then press Keep.");
            refreshScenes();
        };
        addAndMakeVisible (*seg);
        sceneSegments[size_t (i)] = std::move (seg);
    }
    addAndMakeVisible (keepButton);
    keepButton.setFontPx (11.0f);
    keepButton.onClick = [this]
    {
        if (sceneSlot < 0) return;
        controller.keepScene (sceneSlot);
        refreshScenes();
        if (onToast) onToast ("Kept. " + juce::String (controller.getScene (sceneSlot).name) + " brings this mix back in one press.");
    };
    refreshScenes();

    safeLink = std::make_unique<Link>();
    addAndMakeVisible (*safeLink);
    safeLink->onClick = [this]
    {
        auto& daw = services.daw();
        if (! daw.isLiveSafe())
        {
            daw.setLiveSafe (true);
            if (onLiveSafeChanged) onLiveSafeChanged();
            if (onToast) onToast ("LIVE SAFE on. The sound is locked: re-routes and re-tunes are blocked.");
            refresh();
            return;
        }
        juce::CallOutBox::launchAsynchronously (std::make_unique<SafeDetail> (controller.getLiveSafePolicy()),
                                                safeLink->getScreenBounds(), nullptr);
    };

    autopilotLink = std::make_unique<Link>();
    addAndMakeVisible (*autopilotLink);
    autopilotLink->setTooltip ("Hold the mix you set. Autopilot moves group faders only, slowly, inside a few dB of the mix "
                               "it was engaged on, and says why every time. Touch a fader and that group is yours again.");
    autopilotLink->onClick = [this]
    {
        if (controller.isAutopilotOn()) return;             // the toolbar's AUTOPILOT is the one press that stops it
        if (! controller.setAutopilot (true)) return;       // the controller has already said why
        refresh();
    };

    // ---- the engineer's own listen
    const char* labels[4] = { "MONITOR SOLO", "SOLO IN PLACE", "AFL", "PFL" };
    const char* tips[4] = {
        "Solo goes to your headphones only: the room and the stream never hear it. This is the normal setting.",
        "Solo mutes everything else for everybody. Right for mixing a recording, never for a service.",
        "After-fade listen: you hear the channel where it sits in the mix - panned, and silent if it is muted.",
        "Pre-fade listen: you hear the channel as it arrives, whatever its fader and mute are doing." };
    for (int i = 0; i < 4; ++i)
    {
        modes[size_t (i)] = std::make_unique<DineButton> (labels[i], DineButton::Style::Toggle);
        modes[size_t (i)]->setFontPx (12.0f);
        modes[size_t (i)]->setPadX (8);
        modes[size_t (i)]->setClickingTogglesState (false);
        modes[size_t (i)]->setTooltip (tips[i]);
        addAndMakeVisible (*modes[size_t (i)]);
    }
    modes[0]->onClick = [this] { controller.setSoloMode (SoloMode::Monitor); refreshMonitor(); };
    modes[1]->onClick = [this] { controller.setSoloMode (SoloMode::InPlace); refreshMonitor();
                                 if (onToast) onToast ("Solo in place: pressing S is heard by the room and the stream too. Use it for a recording, not a service."); };
    modes[2]->onClick = [this] { controller.setSoloPoint (SoloPoint::AFL); refreshMonitor(); };
    modes[3]->onClick = [this] { controller.setSoloPoint (SoloPoint::PFL); refreshMonitor(); };

    addAndMakeVisible (clearSolo);
    clearSolo.setCaps (true);
    clearSolo.setFontPx (11.0f);
    clearSolo.setTooltip ("Stop listening to everything you have soloed, all at once.");
    clearSolo.onClick = [this] { controller.clearSolos(); refreshMonitor(); if (onToast) onToast ("Solo cleared."); };

    addAndMakeVisible (monitorDim);
    monitorDim.setCaps (true);
    monitorDim.setFontPx (10.0f);
    monitorDim.setPadX (6);
    monitorDim.setClickingTogglesState (false);
    monitorDim.setTooltip ("Drop your headphones to talk to someone, without losing the level you had set.");
    monitorDim.onClick = [this] { controller.setMonitorDim (! controller.getMonitor().dim); refreshMonitor(); };

    // Where solo goes. The same choice as Outputs > Solo, here because this card is where
    // "solo has nowhere to go yet" is read, and the fix belongs one click away from it.
    addAndMakeVisible (soloDevice);
    soloDevice.setFlat (true);
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

    monitorLevel = std::make_unique<LevelLine>();
    addAndMakeVisible (*monitorLevel);
    monitorLevel->onValueChange = [this]
    {
        controller.setMonitorGain (float (monitorLevel->getValue()));
        monitorLevel->setTooltip ("Your headphones: " + dbText (float (monitorLevel->getValue())) + " dB. Nothing to do with the mix "
                                  "anyone else hears. Double-click for 0.0 dB.");
    };

    setOpaque (true);
    refreshMonitor();
}

LivePage::~LivePage() = default;

void LivePage::refreshMonitor()
{
    const auto& m = controller.getMonitor();
    const bool inPlace = m.mode == SoloMode::InPlace;
    modes[0]->setToggleState (! inPlace, juce::dontSendNotification);
    modes[1]->setToggleState (inPlace, juce::dontSendNotification);
    modes[2]->setToggleState (m.point == SoloPoint::AFL, juce::dontSendNotification);
    modes[3]->setToggleState (m.point == SoloPoint::PFL, juce::dontSendNotification);
    monitorDim.setToggleState (m.dim, juce::dontSendNotification);
    clearSolo.setEnabled (controller.numSoloed() > 0);
    if (std::fabs (monitorLevel->getValue() - double (m.gainDb)) > 0.01)
        monitorLevel->setValue (m.gainDb, juce::dontSendNotification);
    monitorLevel->setTooltip ("Your headphones: " + dbText (m.gainDb) + " dB. Nothing to do with the mix anyone else hears. "
                              "Double-click for 0.0 dB.");
    const auto device = services.soloOutputDevice();
    soloDevice.setValue (device.isEmpty() ? juce::String ("Choose headphones") : device);
    soloDevice.setBriefValue (device.isEmpty() ? juce::String ("Pick") : device);
    soloDevice.setEnabled (services.isAudioRunning());
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
    refreshScenes();                 // the picker follows the controller: a scene kept from anywhere shows here

    for (auto& t : tiles) if (t != nullptr) t->refresh();
    updateDiskNote();

    auto& daw = services.daw();
    const bool recording = daw.isRecording();
    const bool running = services.isAudioRunning();

    Look next;
    next.isRecording = recording;
    next.running = running;
    next.safe = daw.getProject().liveSafe;
    const int armed = daw.getProject().numArmed();
    if (recording)
    {
        next.recording = juce::String (armed) + (armed == 1 ? " track " : " tracks ") + Glyph::dot() + " "
                       + Transport::formatTime (daw.getRecordingSeconds()).dropLastCharacters (4);
        next.recordingNote = secondsFree > 0.0 ? span (secondsFree) + " left" : juce::String();
    }
    else
    {
        next.recording = "Stopped";
        next.recordingNote = armed == 0 ? juce::String ("none set to record")
                                        : juce::String (armed) + " set to record";
    }
    next.output = ! running ? juce::String ("Off")
                : services.outputDisplayName().isEmpty() ? juce::String ("No output") : services.outputDisplayName();

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
        clipNote = clipping == 0 ? juce::String() : names + " " + Glyph::dash() + " fix it at the console";
        anyClipping = clipping > 0;
    }
    next.anyClip = anyClipping;
    next.clipping = clipText;
    next.clippingNote = clipNote;

    const auto loud = controller.getMasterLoudness();
    next.outputNote = loud.known && loud.integratedLufs > -100.0f ? juce::String (loud.integratedLufs, 1).replace ("-", Glyph::minus()) + " LUFS"
                                                                  : juce::String();
    if (controller.isPrepared()) next.headroomDb = -controller.getEngine().getBus (MixBus::Master).getOutputMeter().getMaxPeakDb();
    next.headroom = controller.isPrepared() ? juce::String (juce::jlimit (0.0f, 60.0f, next.headroomDb), 1) + " dB" : Glyph::dash();
    next.headroomNote = loud.known && loud.truePeakDb > -100.0f ? "TP " + juce::String (loud.truePeakDb, 1).replace ("-", Glyph::minus()) + " dBTP"
                                                                : juce::String();

    next.soloCount = controller.numSoloed();
    next.inPlace = controller.getMonitor().mode == SoloMode::InPlace;
    next.routed = controller.hasMonitorOutput();
    next.monitorNote = next.inPlace ? "Careful: pressing S is heard by the room and the stream too."
                     : ! next.routed ? "Solo has nowhere to go yet. Pick where you listen, below."
                     : next.soloCount > 0 ? juce::String (next.soloCount) + (next.soloCount == 1 ? " soloed. Only you hear it." : " soloed. Only you hear it.")
                                          : "Press S on a group. Only you hear it.";
    if (next.soloCount != look.soloCount || next.inPlace != look.inPlace || next.routed != look.routed) refreshMonitor();

    {
        const auto& ap = controller.getAutopilot();
        next.autopilotOn = ap.on;
        next.autopilotMoved = ap.groupsCorrected > 0;
        if (ap.on && autopilotSince.isEmpty()) autopilotSince = clockTime (juce::Time::currentTimeMillis());
        if (! ap.on) autopilotSince = {};
        // What it has done since it was engaged, newest first, in the words the Mix history has:
        // its own entries, back as far as the "Before Autopilot" mark it left when it came on.
        const auto& history = controller.getCheckpoints();
        if (history.size() != checkpointsSeen || ap.on != look.autopilotOn)
        {
            checkpointsSeen = history.size();
            autopilotLog.clear();
            if (ap.on)
                for (auto it = history.rbegin(); it != history.rend() && autopilotLog.size() < 2; ++it)
                {
                    const juce::String what (it->what);
                    if (what == "Before Autopilot") break;
                    if (! what.startsWith ("Autopilot: ")) continue;
                    auto line = what.fromFirstOccurrenceOf ("Autopilot: ", false, false).trimCharactersAtEnd (".");
                    line = line.replaceFirstOccurrenceOf (". ", " " + Glyph::dot() + " ").replace ("-", Glyph::minus());
                    // The group as the strips name it: "Lead", not "LEAD".
                    const auto group = line.upToFirstOccurrenceOf (" ", false, false);
                    if (group.length() > 3)
                        line = group.substring (0, 1) + group.substring (1).toLowerCase() + line.fromFirstOccurrenceOf (" ", true, false);
                    autopilotLog.add (clockTime (it->whenMs) + "\t" + line);
                }
        }
        next.autopilotLog = autopilotLog;
        next.autopilotSince = autopilotSince;
    }

    safeLink->set (next.safe ? juce::String (juce::CharPointer_UTF8 ("What\xe2\x80\x99s locked \xe2\x80\xba"))
                             : juce::String (juce::CharPointer_UTF8 ("Turn on \xe2\x80\xba")),
                   next.safe ? Dine::warn : Dine::ink2);
    safeLink->setTooltip (next.safe ? "What LIVE SAFE locks, what it blocks and what still works."
                                    : juce::String ("Lock the sound for the service. ") + liveSafe::lockedSummary() + " " + liveSafe::allowedSummary());
    autopilotLink->set (next.autopilotOn ? (next.autopilotMoved ? "Holding since " : "Healthy since ") + autopilotSince
                                         : juce::String (juce::CharPointer_UTF8 ("Turn on \xe2\x80\xba")),
                        next.autopilotOn ? Dine::ink3 : Dine::ink2);
    autopilotLink->setMouseCursor (next.autopilotOn ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);

    if (next != look)
    {
        const bool reflow = next.safe != look.safe || next.autopilotOn != look.autopilotOn
                         || next.autopilotLog != look.autopilotLog || next.monitorNote != look.monitorNote;
        look = next;
        if (reflow) resized();
        repaint();
    }
}

juce::String LivePage::safeText() const
{
    return look.safe ? "Routing, device and re-tuning are locked. Faders, mutes, solos and recording still work."
                     : "Nothing is locked. Turn it on before the doors open: routing, device and re-tuning lock, "
                       "and faders, mutes, solos and recording keep working.";
}

juce::String LivePage::autopilotText() const
{
    const auto limit = juce::String (controller.getAutopilotLimits().maxTotalDb, 0);
    return look.autopilotOn ? juce::String (juce::CharPointer_UTF8 ("Group faders only, \xc2\xb1")) + limit + " dB max. Touch a fader to take over."
                            : juce::String (juce::CharPointer_UTF8 ("Off. Engage it and DLIVE holds the mix you set: group faders only, "
                                                                     "inside \xc2\xb1")) + limit + " dB of where you engaged it, and every move says why.";
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);
    const auto& l = lay;

    // ---- the health strip: four readings, one line
    {
        Dine::fillRounded (g, l.health.toFloat(), Dine::card, 8.0f);
        auto row = l.health.reduced (16, 0);
        struct Stat { juce::String caption, value, note; juce::Colour dot; bool monoNote; };
        const Stat stats[4] = {
            { "Recording", look.recording, look.recordingNote, look.isRecording ? Dine::keyRec : Dine::ink4, false },
            { "On air", look.output, look.outputNote, look.running ? Dine::ok : Dine::ink4, true },
            { "Clipping", look.clipping, look.clippingNote, look.anyClip ? Dine::crit : Dine::ok, false },
            { "Headroom", look.headroom, look.headroomNote,
              look.headroomDb < 0.5f ? Dine::crit : look.headroomDb < 3.0f ? Dine::warn : Dine::ok, true } };
        const auto capFont = Dine::text (11.0f, 500), valueFont = Dine::text (13.0f, 600);
        for (int i = 0; i < 4 && row.getWidth() > 0; ++i)
        {
            if (i > 0)
            {
                g.setColour (Dine::control);
                g.fillRect (juce::Rectangle<int> (row.getX() + 10, row.getCentreY() - 10, 1, 20));
                row.removeFromLeft (21);
            }
            const auto& s = stats[i];
            g.setColour (s.dot);
            g.fillEllipse (row.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
            row.removeFromLeft (8);
            g.setColour (Dine::ink3);
            g.setFont (capFont);
            const int capW = Dine::textWidth (capFont, s.caption);
            Dine::drawText (g, s.caption, row.removeFromLeft (capW), juce::Justification::centredLeft, false);
            row.removeFromLeft (8);
            g.setColour (Dine::ink);
            g.setFont (valueFont);
            const int valueW = juce::jmin (row.getWidth(), Dine::textWidth (valueFont, s.value));
            Dine::drawText (g, s.value, row.removeFromLeft (valueW), juce::Justification::centredLeft, true);
            // The note is a second reading, not a name: on a narrow window it is left out
            // whole rather than cut to "TP -1..." - the value beside it still says the thing.
            const auto noteF = s.monoNote ? Dine::mono (11.0f, 500) : capFont;
            const int noteW = s.note.isEmpty() ? 0 : Dine::textWidth (noteF, s.note);
            if (noteW > 0 && noteW + 8 <= row.getWidth())
            {
                row.removeFromLeft (8);
                g.setColour (s.monoNote ? Dine::ink2 : Dine::ink3);
                g.setFont (noteF);
                Dine::drawText (g, s.note, row.removeFromLeft (noteW), juce::Justification::centredLeft, false);
            }
        }
    }

    // ---- Groups, and the scene picker over them
    g.setColour (Dine::ink);
    g.setFont (Dine::text (17.0f, 600));
    Dine::drawText (g, "Groups", l.groupsHeader, juce::Justification::centredLeft, true);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (11.0f, 500));
    Dine::drawText (g, "Scene", l.sceneCaption, juce::Justification::centredRight, false);
    Dine::fillRounded (g, l.sceneTrack.toFloat(), Dine::control, 7.0f);
    // The mark the sidebar's SCENES row leaves: a ring round the picker for a second, so an
    // eye that came looking for the scenes finds them without anything changing.
    if (sceneFlash > 0)
    {
        Dine::hairlineRounded (g, l.sceneTrack.expanded (4, 4).toFloat(),
                               Dine::accent.withAlpha (juce::jmin (1.0f, float (sceneFlash) / 30.0f)), 9.0f);
        --sceneFlash;
    }

    // ---- a card on the rail: a dot, a title and the words under it
    auto header = [&g] (juce::Rectangle<int> card, juce::Colour dot, const juce::String& title)
    {
        auto head = card.reduced (kCardPadX, kCardPadY).removeFromTop (kHeadH);
        g.setColour (dot);
        g.fillEllipse (head.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        head.removeFromLeft (8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, title, head, juce::Justification::centredLeft, true);
    };
    auto body = [] (juce::Rectangle<int> card) { return card.reduced (kCardPadX, kCardPadY).withTrimmedTop (kHeadH + kCardGap); };

    // LIVE SAFE: amber on its own ground while the sound is locked; a quiet card while it is not.
    {
        Dine::fillRounded (g, l.safe.toFloat(), look.safe ? Dine::refuse : Dine::card, 8.0f);
        if (look.safe) Dine::hairlineRounded (g, l.safe.toFloat().reduced (0.5f), Dine::warn, 8.0f);
        header (l.safe, look.safe ? Dine::warn : Dine::ink4, look.safe ? "Live safe is on" : "Live safe is off");
        auto text = body (l.safe);
        g.setColour (Dine::ink2);
        g.setFont (calloutFont());
        Dine::drawFittedText (g, safeText(), text, juce::Justification::topLeft, juce::jmax (1, text.getHeight() / 16), 1.0f);
    }

    // AUTOPILOT: the second thing allowed to move a level by itself, so while it is on the card
    // says so, says what it has had to move and why, and says where its fence is.
    {
        Dine::fillRounded (g, l.autopilot.toFloat(), look.autopilotOn ? Dine::editGround : Dine::card, 8.0f);
        if (look.autopilotOn) Dine::hairlineRounded (g, l.autopilot.toFloat().reduced (0.5f), Dine::monitor, 8.0f);
        header (l.autopilot, look.autopilotOn ? Dine::monitor : Dine::ink4, "Autopilot");
        auto text = body (l.autopilot);
        if (look.autopilotOn)
        {
            const auto timeFont = Dine::mono (11.0f, 500);
            const int timeW = Dine::textWidth (timeFont, "00:00") + 4;
            if (look.autopilotLog.isEmpty())
            {
                g.setColour (Dine::ink3);
                g.setFont (calloutFont());
                Dine::drawText (g, "Nothing to do: the mix is inside tolerance.", text.removeFromTop (16), juce::Justification::centredLeft, true);
                text.removeFromTop (6);
            }
            for (const auto& entry : look.autopilotLog)
            {
                const auto when = entry.upToFirstOccurrenceOf ("\t", false, false);
                const auto what = entry.fromFirstOccurrenceOf ("\t", false, false);
                const int lines = wrapLines (calloutFont(), what, text.getWidth() - timeW - 10);
                auto line = text.removeFromTop (16 * lines);
                g.setColour (Dine::monitor);
                g.setFont (timeFont);
                Dine::drawText (g, when, line.removeFromLeft (timeW).removeFromTop (16), juce::Justification::centredLeft, false);
                line.removeFromLeft (10);
                g.setColour (Dine::ink);
                g.setFont (calloutFont());
                Dine::drawFittedText (g, what, line, juce::Justification::topLeft, lines, 1.0f);
                text.removeFromTop (6);
            }
            text.removeFromTop (4);
            g.setColour (Dine::ink3);
            g.setFont (noteFont());
            Dine::drawFittedText (g, autopilotText(), text, juce::Justification::topLeft, juce::jmax (1, text.getHeight() / 14), 1.0f);
        }
        else
        {
            g.setColour (Dine::ink2);
            g.setFont (calloutFont());
            Dine::drawFittedText (g, autopilotText(), text, juce::Justification::topLeft, juce::jmax (1, text.getHeight() / 16), 1.0f);
        }
    }

    // WHAT I HEAR: the engineer's own listen, on a card of its own because none of it reaches the room.
    {
        Dine::fillRounded (g, l.monitor.toFloat(), Dine::card, 8.0f);
        auto inner = l.monitor.reduced (kCardPadX, kCardPadY);
        auto titleBlock = inner.withTrimmedRight (clearSolo.getWidth() + 8);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        Dine::drawText (g, "What I hear", titleBlock.removeFromTop (18), juce::Justification::centredLeft, true);
        titleBlock.removeFromTop (2);
        const int lines = wrapLines (noteFont(), look.monitorNote, titleBlock.getWidth());
        g.setColour (look.inPlace || ! look.routed ? Dine::warn : look.soloCount > 0 ? Dine::accent : Dine::ink3);
        g.setFont (noteFont());
        Dine::drawFittedText (g, look.monitorNote, titleBlock.removeFromTop (14 * lines), juce::Justification::topLeft, lines, 1.0f);

        for (const auto& track : { l.modesA, l.modesB })
            Dine::fillRounded (g, track.toFloat(), Dine::control, 7.0f);
        Dine::drawIcon (g, Dine::Icon::Headphones, l.output.withWidth (16).withSizeKeepingCentre (16, 16).toFloat(),
                        look.routed ? Dine::ink2 : Dine::warn);
    }
}

void LivePage::resized()
{
    auto& l = lay;
    auto r = getLocalBounds().withTrimmedLeft (kPadX).withTrimmedRight (kPadX).withTrimmedTop (kPadTop).withTrimmedBottom (kPadBottom);
    l.health = r.removeFromTop (kHealthH);
    r.removeFromTop (kGap);

    const int railW = juce::jmin (kRailW, juce::jmax (280, r.getWidth() / 3));
    auto rail = r.removeFromRight (railW);
    r.removeFromRight (kGap);

    // ---- the groups: a header row, then the strips
    {
        auto head = r.removeFromTop (Dine::Metric::button);
        const int keepW = juce::jmax (52, keepButton.idealWidth());
        keepButton.setBounds (head.removeFromRight (keepW));
        head.removeFromRight (12);
        int widths[4] {}, total = 0;
        for (int i = 0; i < 4; ++i) { widths[i] = juce::jmax (44, sceneSegments[size_t (i)]->idealWidth()); total += widths[i]; }
        l.sceneTrack = head.removeFromRight (total + 2 * 3 + 4).withSizeKeepingCentre (total + 2 * 3 + 4, kSegmentH + 4);
        auto seg = l.sceneTrack.reduced (2);
        for (int i = 0; i < 4; ++i)
        {
            sceneSegments[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
            seg.removeFromLeft (2);
        }
        head.removeFromRight (12);
        l.sceneCaption = head.removeFromRight (Dine::textWidth (Dine::text (11.0f, 500), "Scene") + 2);
        l.groupsHeader = head;
        r.removeFromTop (12);
        l.strips = r;

        auto row = l.strips;
        const int gap = 8;
        const int w = (row.getWidth() - gap * (kTiles - 1)) / kTiles;
        for (int i = 0; i < kTiles; ++i)
        {
            tiles[size_t (i)]->setBounds (i == kTiles - 1 ? row : row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    }

    // ---- the rail: LIVE SAFE and Autopilot at the top, what I hear at the foot
    {
        const int textW = railW - 2 * kCardPadX;
        const int safeH = kCardPadY + kHeadH + kCardGap + 16 * wrapLines (calloutFont(), safeText(), textW) + kCardPadY;
        l.safe = rail.removeFromTop (safeH);
        rail.removeFromTop (12);

        int apBody = 0;
        if (look.autopilotOn)
        {
            const int timeW = Dine::textWidth (Dine::mono (11.0f, 500), "00:00") + 4;
            if (look.autopilotLog.isEmpty()) apBody += 16 + 6;
            for (const auto& entry : look.autopilotLog)
                apBody += 16 * wrapLines (calloutFont(), entry.fromFirstOccurrenceOf ("\t", false, false), textW - timeW - 10) + 6;
            apBody += 4 + 14 * wrapLines (noteFont(), autopilotText(), textW);
        }
        else apBody = 16 * wrapLines (calloutFont(), autopilotText(), textW);
        l.autopilot = rail.removeFromTop (kCardPadY + kHeadH + kCardGap + apBody + kCardPadY);

        const int clearW = juce::jmax (84, clearSolo.idealWidth());
        const int noteLines = wrapLines (noteFont(), look.monitorNote.isEmpty() ? juce::String ("Press S on a group. Only you hear it.") : look.monitorNote,
                                         textW - clearW - 8);
        const int titleH = juce::jmax (Dine::Metric::button, 18 + 2 + 14 * noteLines);
        // The two tracks share a row when the rail is wide enough for both, and stack when it is not.
        const int modesW = modes[0]->idealWidth() + modes[1]->idealWidth() + 6 + 8 + modes[2]->idealWidth() + modes[3]->idealWidth() + 6;
        const bool stacked = modesW > textW;
        const int modesH = stacked ? 2 * (kSegmentH + 4) + 8 : kSegmentH + 4;
        const int monitorH = kCardPadY + titleH + kCardGap + modesH + kCardGap + 24 + kCardPadY;
        l.monitor = rail.removeFromBottom (monitorH);

        auto inner = l.monitor.reduced (kCardPadX, kCardPadY);
        auto titleRow = inner.removeFromTop (titleH);
        clearSolo.setBounds (titleRow.removeFromRight (clearW).removeFromTop (Dine::Metric::button));
        inner.removeFromTop (kCardGap);

        auto modesRow = inner.removeFromTop (modesH);
        auto track = [&modesRow, stacked] (DineButton& a, DineButton& b)
        {
            const int wa = a.idealWidth(), wb = b.idealWidth();
            auto line = stacked ? modesRow.removeFromTop (kSegmentH + 4) : modesRow;
            if (stacked) modesRow.removeFromTop (8);
            auto t = line.removeFromLeft (juce::jmin (line.getWidth(), wa + wb + 6));
            if (! stacked) modesRow.removeFromLeft (wa + wb + 6 + 8);
            auto seg = t.reduced (2);
            a.setBounds (seg.removeFromLeft (juce::jmin (wa, seg.getWidth())));
            seg.removeFromLeft (2);
            b.setBounds (seg);
            return t;
        };
        l.modesA = track (*modes[0], *modes[1]);
        l.modesB = track (*modes[2], *modes[3]);
        inner.removeFromTop (kCardGap);

        l.output = inner.removeFromTop (24);
        auto out = l.output;
        out.removeFromLeft (16 + 6);
        monitorLevel->setBounds (out.removeFromRight (kLevelW).withSizeKeepingCentre (kLevelW, 16));
        out.removeFromRight (8);
        const int dimW = juce::jmax (34, monitorDim.idealWidth());
        monitorDim.setBounds (out.removeFromRight (dimW).withSizeKeepingCentre (dimW, 20));
        out.removeFromRight (8);
        soloDevice.setBounds (out.withWidth (juce::jmin (out.getWidth(), soloDevice.idealWidth())));
    }

    // the links at the right of the two cards' headers
    auto linkArea = [] (juce::Rectangle<int> card, int w) { return card.reduced (kCardPadX, kCardPadY).removeFromTop (kHeadH).removeFromRight (w); };
    safeLink->setBounds (linkArea (l.safe, juce::jmin (180, safeLink->idealWidth())));
    autopilotLink->setBounds (linkArea (l.autopilot, juce::jmin (200, autopilotLink->idealWidth())));
}

} // namespace livemix

namespace livemix
{

// The picker reads the controller: every scene's name, and KEEP only once one is picked.
void LivePage::refreshScenes()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto& scene = controller.getScene (i);
        auto& seg = *sceneSegments[size_t (i)];
        const juce::String name (scene.name.empty() ? defaultSceneName (i) : scene.name.c_str());
        if (seg.getButtonText() != name.toUpperCase())
        {
            seg.setButtonText (name.toUpperCase());
            if (monitorLevel != nullptr) resized();          // a renamed scene is a wider segment
        }
        seg.setToggleState (i == sceneSlot, juce::dontSendNotification);
        juce::String when;
        if (scene.kept && scene.whenMs > 0) when = clockTime (scene.whenMs);
        seg.setTooltip (scene.kept ? "Bring the " + name + " mix back - every fader, chain and macro - in one press"
                                         + (when.isEmpty() ? juce::String() : " (kept " + when + ")") + ". UNDO takes it back."
                                   : "Nothing is kept under " + name + " yet. Pick it, set the mix, then press Keep.");
    }
    keepButton.setEnabled (sceneSlot >= 0);
    keepButton.setTooltip (sceneSlot >= 0 ? "Keep the mix as it is now under " + juce::String (controller.getScene (sceneSlot).name) + "."
                                          : juce::String ("Pick a scene first, then Keep puts the mix as it is now under its name."));
}

} // namespace livemix
