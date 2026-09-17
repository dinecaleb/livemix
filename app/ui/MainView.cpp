#include "MainView.h"
#include "native/InputMapStore.h"
#include "native/MultitrackImport.h"
#include "native/OpenAiMixProvider.h"

namespace livemix
{

// Nothing opens by itself in the headless snapshot tool (MainView::setAutoTutorial).
namespace { bool gAutoTutorial = true; }

// ---------------------------------------------------------------- HUD toast
// A HUD at the bottom of the content, not a banner: nothing in the layout moves.
class MainView::Toast : public juce::Component
{
public:
    void show (const juce::String& t)
    {
        text = t;
        const auto lower = t.toLowerCase();
        icon = lower.contains ("could not") || lower.contains ("not ") || lower.contains ("stopped")
                   ? Dine::Icon::Warn : Dine::Icon::Check;
        colour = icon == Dine::Icon::Warn ? Dine::warn : Dine::ok;
        setVisible (true);
        repaint();
    }
    int idealWidth() const
    {
        return juce::jmin (620, Dine::textWidth (Dine::text (12.5f), text) + 54);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 26, { 0, 10 }).drawForRectangle (g, getLocalBounds());
        Dine::fillRounded (g, r, Dine::popover, Dine::Radius::card);
        Dine::hairlineRounded (g, r, Dine::hairStrong, Dine::Radius::card);
        auto inner = getLocalBounds().reduced (15, 0);
        Dine::drawIcon (g, icon, inner.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), colour);
        inner.removeFromLeft (9);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (12.5f));
        g.drawText (text, inner, juce::Justification::centredLeft, true);
    }
private:
    juce::String text;
    Dine::Icon icon = Dine::Icon::Check;
    juce::Colour colour { Dine::ok };
};

// ---------------------------------------------------------------- session button
// The toolbar's document title: the session's name over what it is set to mix.
class MainView::SessionButton : public juce::Button
{
public:
    SessionButton() : juce::Button ("session") {}
    void set (const juce::String& n, const juce::String& s)
    {
        if (n == name && s == sub) return;
        name = n; sub = s; repaint();
    }
    int idealWidth() const
    {
        return juce::jmin (380, juce::jmax (Dine::textWidth (Dine::text (13.0f, 600), name) + 22,
                                            Dine::textWidth (Dine::text (11.0f), sub)) + 14);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (over || down) Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::fillSoft, Dine::Radius::chip);
        auto r = getLocalBounds().reduced (6, 4);
        auto top = r.removeFromTop (17);
        g.setColour (Dine::ink);
        g.setFont (Dine::text (13.0f, 600));
        const int w = Dine::textWidth (Dine::text (13.0f, 600), name);
        g.drawText (name, top.removeFromLeft (juce::jmin (w, top.getWidth() - 16)), juce::Justification::centredLeft, true);
        top.removeFromLeft (5);
        Dine::drawIcon (g, Dine::Icon::UpDown, top.removeFromLeft (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f), Dine::ink2);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f));
        g.drawText (sub, r, juce::Justification::topLeft, true);
    }
private:
    juce::String name, sub;
};

// ---------------------------------------------------------------- toolbar toggle
// A latching toolbar button that fills in its own colour when it is on, so a state you
// can hear (BYPASS) can never look like a state you cannot.
class MainView::ToolbarToggle : public juce::Button
{
public:
    ToolbarToggle (const juce::String& text, juce::Colour c, Dine::Icon i = Dine::Icon::None)
        : juce::Button (text), tint (c), icon (i)
    {
        setClickingTogglesState (false);
    }

    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    bool isOn() const noexcept { return on; }
    // A state that has two readings says which one it is on the key itself: LIVE SAFE ON is
    // not the same news as LIVE SAFE, and "off" is the reading that gets missed in a booth.
    void setSuffix (const juce::String& s) { if (s != suffix) { suffix = s; repaint(); } }

    juce::String label() const { return suffix.isEmpty() ? getButtonText() : getButtonText() + " " + suffix; }

    int idealWidth() const
    {
        return Dine::textWidth (Dine::text (11.0f, 600).withExtraKerningFactor (0.05f), label())
               + 24 + (icon != Dine::Icon::None ? 18 : 0);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (on)
        {
            Dine::fillRounded (g, r, tint.withAlpha (down ? 0.8f : over ? 1.0f : 0.92f), Dine::Radius::control);
            Dine::hairlineRounded (g, r, juce::Colours::black.withAlpha (0.25f), Dine::Radius::control);
        }
        else Dine::drawStandard (g, r, Dine::Radius::control, over, down);

        const auto fg = on ? Dine::onAccent : Dine::ink3;
        const auto font = Dine::text (11.0f, 600).withExtraKerningFactor (0.05f);
        const int textW = Dine::textWidth (font, label());
        const int iconW = icon != Dine::Icon::None ? 18 : 0;
        auto block = getLocalBounds().withSizeKeepingCentre (textW + iconW, getHeight());
        if (iconW > 0)
        {
            Dine::drawIcon (g, icon, block.removeFromLeft (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f), fg, 1.5f);
            block.removeFromLeft (6);
        }
        g.setColour (fg);
        g.setFont (font);
        g.drawText (label(), block, juce::Justification::centredLeft);
    }

private:
    juce::Colour tint;
    Dine::Icon icon;
    juce::String suffix;
    bool on = false;
};

// The toolbar's panel switch, in the place macOS puts it: the far left, before the
// document. It shows and hides the channel list, and it is the same command as
// View > Channels and Ctrl-Cmd-S.
class MainView::SidebarButton : public juce::Button
{
public:
    SidebarButton() : juce::Button ("Channels") { setWantsKeyboardFocus (false); }

    void setOn (bool o) { if (o != on) { on = o; repaint(); } }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (over || down) Dine::fillRounded (g, r, juce::Colours::white.withAlpha (down ? 0.12f : 0.07f), Dine::Radius::control);
        Dine::drawIcon (g, Dine::Icon::Sidebar, r.withSizeKeepingCentre (17.0f, 17.0f),
                        on ? Dine::ink2 : Dine::ink4, 1.3f);
    }

private:
    bool on = true;
};

// ---------------------------------------------------------------- icon button
// A bare 30 x 26 toolbar glyph (the chat). No label: the toolbar's right-hand cluster is
// already three words wide and a fourth would push the tabs off centre on a 1280 booth screen.
class MainView::IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, Dine::Icon i) : juce::Button (name), icon (i)
    {
        setWantsKeyboardFocus (false);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::drawStandard (g, r, Dine::Radius::control, over, down);
        Dine::drawIcon (g, icon, r.withSizeKeepingCentre (14.0f, 14.0f), over || down ? Dine::ink : Dine::ink3, 1.4f);
    }
private:
    Dine::Icon icon;
};

// ---------------------------------------------------------------- status foot
// Always on screen, on every workspace: the engine, the disk, what is recording, how loud
// the broadcast is, and what the engineer's own ears are on. A booth question that has to
// be answered by walking somewhere is a booth question that gets answered wrong.
//
// It is painted, not built, and the numbers it reads are refreshed at 30 Hz but the *text*
// is compared before a repaint (`Look`), so a still console costs nothing. The disk is a
// syscall, so it is asked once a second rather than thirty times.
class MainView::StatusBar : public juce::Component
{
public:
    StatusBar (MixController& c, AppServices& s) : controller (c), services (s) { setOpaque (true); }

    struct Item { juce::String label, value; juce::Colour dot, ink; };

    void update (bool slow)
    {
        Look next;
        const bool running = services.isAudioRunning();
        const bool lost = ! running && services.deviceStopped();
        next.engine = lost ? "device lost"
                     : ! running ? "not running"
                                 : juce::String (services.sampleRate() / 1000.0, 1) + " kHz  "
                                       + juce::String (services.bufferSize()) + " smp";
        next.engineState = lost ? 2 : ! running ? 1 : 0;
        next.drops = services.xrunCount();

        const auto& daw = services.daw();
        next.recording = daw.isRecording();
        const auto& project = daw.getProject();
        int armed = 0;
        for (const auto& t : project.tracks) if (t.armed) ++armed;
        next.armed = armed;
        next.rec = next.recording ? juce::String (armed) + " tracks"
                                  : armed > 0 ? juce::String (armed) + " to record" : juce::String ("none set");

        if (slow) disk = services.sessionFolder() == juce::File()
                             ? juce::File::getSpecialLocation (juce::File::userMusicDirectory).getBytesFreeOnVolume()
                             : services.sessionFolder().getBytesFreeOnVolume();
        next.disk = disk <= 0 ? juce::String ("--")
                              : juce::String (double (disk) / 1.0e9, disk < 10000000000LL ? 1 : 0) + " GB";
        next.diskLow = disk > 0 && disk < 5000000000LL;

        const auto loud = controller.getMasterLoudness();
        next.loudness = ! loud.known || loud.integratedLufs <= -100.0f
                            ? juce::String (Glyph::dash()) + " LUFS"
                            : juce::String (loud.integratedLufs, 1) + " / " + juce::String (loud.targetLufs, 0) + " LUFS";
        next.loudnessState = ! loud.known || loud.integratedLufs <= -100.0f ? 1 : loud.onTarget() ? 0 : 2;

        next.monitor = controller.hasMonitorOutput() ? juce::String (services.headphonesSummary().isNotEmpty()
                                                                         ? services.headphonesSummary()
                                                                         : juce::String ("headphones"))
                                                     : juce::String ("nowhere");
        next.monitorOn = controller.hasMonitorOutput();
        next.safe = project.liveSafe;

        if (next != look) { look = next; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        Dine::drawStatusBand (g, getLocalBounds());
        auto r = getLocalBounds().reduced (12, 0);

        const juce::Colour engineDot = look.engineState == 2 ? Dine::crit
                                     : look.engineState == 1 ? Dine::ink4
                                     : look.drops > 0        ? Dine::warn : Dine::ok;
        cell (g, r, "ENGINE", look.engine, engineDot, look.engineState == 2 ? Dine::crit : Dine::ink2);
        if (look.drops > 0)
            cell (g, r, "DROPS", juce::String (look.drops), Dine::warn, Dine::warn);
        cell (g, r, "DISK", look.disk, look.diskLow ? Dine::warn : Dine::ok, look.diskLow ? Dine::warn : Dine::ink2);
        cell (g, r, "RECORD", look.rec, look.recording ? Dine::keyRec : Dine::ink4,
              look.recording ? Dine::keyRec : Dine::ink2);
        cell (g, r, "BROADCAST", look.loudness,
              look.loudnessState == 0 ? Dine::ok : look.loudnessState == 1 ? Dine::ink4 : Dine::warn,
              look.loudnessState == 2 ? Dine::warn : Dine::ink2);
        cell (g, r, "MY EARS", look.monitor, look.monitorOn ? Dine::monitor : Dine::ink4,
              look.monitorOn ? Dine::monitor : Dine::ink3);
        if (look.safe) cell (g, r, "LIVE SAFE", "on", Dine::ok, Dine::ok);

        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f).withExtraKerningFactor (0.10f));
        g.drawText ("DLIVE  " + juce::String (Glyph::dot()) + "  DAUDIO", r, juce::Justification::centredRight);
    }

private:
    static void cell (juce::Graphics& g, juce::Rectangle<int>& r, const juce::String& label,
                      const juce::String& value, juce::Colour dot, juce::Colour ink)
    {
        const auto labelFont = Dine::mono (10.0f).withExtraKerningFactor (0.10f);
        const auto valueFont = Dine::mono (10.0f);
        const int w = 6 + 5 + 6 + Dine::textWidth (labelFont, label) + 6 + Dine::textWidth (valueFont, value) + 13;
        if (w > r.getWidth() - 110) return;          // the product mark keeps its place
        auto cellArea = r.removeFromLeft (w);
        g.setColour (Dine::hairSoft);
        g.fillRect (float (cellArea.getRight()) - 0.5f, float (cellArea.getY()) + 7.0f, 0.5f, 14.0f);
        auto inner = cellArea.reduced (6, 0).withTrimmedRight (7);
        g.setColour (dot);
        g.fillEllipse (inner.removeFromLeft (5).withSizeKeepingCentre (5, 5).toFloat());
        inner.removeFromLeft (6);
        g.setColour (Dine::ink4);
        g.setFont (labelFont);
        auto labelArea = inner.removeFromLeft (Dine::textWidth (labelFont, label));
        g.drawText (label, labelArea, juce::Justification::centredLeft);
        inner.removeFromLeft (6);
        g.setColour (ink);
        g.setFont (valueFont);
        g.drawText (value, inner, juce::Justification::centredLeft);
    }

    struct Look
    {
        juce::String engine, disk, rec, loudness, monitor;
        int engineState = 0, loudnessState = 1, drops = 0, armed = 0;
        bool recording = false, diskLow = false, monitorOn = false, safe = false;
        bool operator== (const Look& o) const
        {
            return engine == o.engine && disk == o.disk && rec == o.rec && loudness == o.loudness
                && monitor == o.monitor && engineState == o.engineState && loudnessState == o.loudnessState
                && drops == o.drops && armed == o.armed && recording == o.recording && diskLow == o.diskLow
                && monitorOn == o.monitorOn && safe == o.safe;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };

    MixController& controller;
    AppServices& services;
    Look look;
    juce::int64 disk = 0;
};

// ---------------------------------------------------------------- setup nav
// SETUP is a workspace now, not a mode: the five steps live in a 212 px list down its left
// side with a tick against the ones that are done, and the session's own facts along its
// foot. The pages themselves are unchanged - this only decides which of them is up.
class MainView::SetupNav : public juce::Component
{
public:
    explicit SetupNav (AppServices& s) : services (s)
    {
        const char* labels[4] = { "Sessions", "Audio device", "Inputs", "Purpose and sound" };
        const Dine::Icon icons[4] = { Dine::Icon::List, Dine::Icon::Device, Dine::Icon::Sliders, Dine::Icon::Target };
        for (int i = 0; i < 4; ++i)
        {
            items[size_t (i)] = std::make_unique<DineNavItem> (labels[i], icons[i]);
            addAndMakeVisible (*items[size_t (i)]);
        }
        setOpaque (true);
    }

    DineNavItem& item (int i) { return *items[size_t (i)]; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (Dine::sidebar);
        g.fillRect (r);
        g.setColour (Dine::hair);
        g.fillRect (float (r.getRight()) - 0.5f, 0.0f, 0.5f, float (getHeight()));

        // The engine's own state, along the foot: what setup is about, in three lines.
        auto foot = r.removeFromBottom (78);
        Dine::drawRule (g, foot.removeFromTop (1), Dine::hairSoft);
        foot = foot.reduced (10, 0).withTrimmedTop (10);
        const bool running = services.isAudioRunning();
        g.setColour (running ? Dine::ink2 : Dine::ink4);
        g.setFont (Dine::text (11.0f));
        g.drawText (running && services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                                                                         : juce::String ("No audio device"),
                    foot.removeFromTop (16), juce::Justification::centredLeft, true);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        g.drawText (running ? juce::String (services.sampleRate() / 1000.0, 1) + " kHz  "
                                  + juce::String (Glyph::dot()) + "  " + juce::String (services.bufferSize()) + " smp"
                            : juce::String (Glyph::dash()),
                    foot.removeFromTop (15), juce::Justification::centredLeft, true);
        g.drawText (juce::String (services.numInputChannels()) + " in / "
                        + juce::String (services.numOutputChannels()) + " out",
                    foot.removeFromTop (15), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedBottom (78).reduced (8, 10);
        for (auto& i : items) { i->setBounds (r.removeFromTop (34)); r.removeFromTop (2); }
    }

private:
    AppServices& services;
    std::array<std::unique_ptr<DineNavItem>, 4> items;
};

// ---------------------------------------------------------------- mixer window
// The console on its own window (a second screen, usually), with the timeline still in
// front of you. It is the same MixController, so both consoles always agree.
class MainView::MixerWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    MixerWindow (MixController& c, AppServices& s, MainView& owner)
        : juce::DocumentWindow ("Mixer " + juce::String (Glyph::dash()) + " DLIVE", Dine::window,
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton
                                    | juce::DocumentWindow::maximiseButton),
          view (owner)
    {
        page = std::make_unique<MixerPage> (c, s);
        page->setWindowButtonVisible (false);
        page->onOpenStrip = [&owner] (int strip) { owner.showPage (Page::Inspector); owner.getAdvancedPage().select (strip); };
        page->onOpenBus = [&owner] (MixBus bus) { owner.showPage (Page::Inspector); owner.getAdvancedPage().selectBus (bus); };
        page->onTuneStrip = [&owner] (int strip) { owner.toFront (true); owner.tuneChannel (strip); };
        page->onToast = [&owner] (const juce::String& t) { owner.showToast (t); };
        setUsingNativeTitleBar (true);
        setContentNonOwned (page.get(), false);
        setResizable (true, false);
        setResizeLimits (720, 420, 6000, 3000);
        centreWithSize (1180, 700);
        setVisible (true);
        startTimerHz (30);
    }

    ~MixerWindow() override { stopTimer(); clearContentComponent(); }

    MixerPage& getPage() { return *page; }
    void closeButtonPressed() override { view.closeMixerWindow(); }   // deletes this window, next message

private:
    void timerCallback() override { page->refresh(); }

    MainView& view;
    std::unique_ptr<MixerPage> page;
};

// ---------------------------------------------------------------- menu bar
// File / Edit / Track / Mix / Record / View / Help, as a professional audio application
// has. The shortcuts printed here are the ones MainView::keyPressed acts on, so the menu
// and the keyboard can never say different things.
class MainView::Menu : public juce::MenuBarModel
{
public:
    explicit Menu (MainView& v) : view (v) {}

    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "Track", "Mix", "Record", "View", "Help" };
    }

    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu m;
        switch (index)
        {
            case 0:
                m.addItem (100, "New Session");
                m.addItem (101, "Open Session...");
                m.addSeparator();
                m.addItem (102, "Save", true, false, nullptr);
                m.addItem (103, "Save As...");
                m.addSeparator();
                m.addItem (104, "Import Multitrack Folder...");
                m.addItem (107, "Add a Reference Mix...");
                m.addSeparator();
                // The patch, saved so next Sunday does not start from nothing.
                m.addItem (108, "Save Input Mapping" + juce::String (Glyph::ellip()),
                           ! view.controller.getSession().inputs.empty());
                m.addItem (109, "Input Mappings" + juce::String (Glyph::ellip()));
                m.addSeparator();
                m.addItem (105, "Export Stereo Mix (WAV)...");
                m.addItem (106, "Export Stereo Mix (MP3)...");
                break;
            case 1:
                m.addItem (200, "Undo", view.tracksPage != nullptr && view.tracksPage->canUndo());
                m.addSeparator();
                m.addItem (201, "Split at Playhead");
                m.addItem (202, "Delete Clip");
                m.addSeparator();
                m.addItem (203, "Add Marker at Playhead   M");
                break;
            case 2:
                m.addItem (300, "Set Every Track to Record");
                m.addItem (301, "Set No Tracks to Record");
                m.addSeparator();
                // Rearranging the channels, for the times a drag is not the way to do it.
                m.addItem (305, "Move Track Up", view.tracksPage != nullptr && view.tracksPage->canMoveSelectedTrack (-1));
                m.addItem (306, "Move Track Down", view.tracksPage != nullptr && view.tracksPage->canMoveSelectedTrack (1));
                m.addSeparator();
                m.addItem (302, "Monitoring: Input");
                m.addItem (303, "Monitoring: Auto");
                m.addItem (304, "Monitoring: Off");
                break;
            case 3:
                m.addItem (405, "TUNE LIVE MIX");
                m.addItem (400, "TUNE MIX");
                m.addItem (404, "TUNE CHANNEL   T", view.selectedChannel() >= 0);
                m.addSeparator();
                // A reference is a target, so it sits with the verbs that aim at one.
                m.addItem (407, view.controller.hasReference() ? "MATCH TO REFERENCE" : "MATCH TO REFERENCE...",
                           view.controller.hasReference());
                m.addItem (408, view.controller.hasReference()
                                    ? "Reference: " + juce::String (view.controller.getReference().name) + juce::String (Glyph::ellip())
                                    : "Add a Reference Mix...");
                m.addSeparator();
                m.addSeparator();
                // A change asked for in words, and a different reading of the same listen.
                m.addItem (409, "AI Mix Chat" + juce::String (Glyph::ellip()));
                m.addItem (410, "Try Another Mix", view.controller.canTryAnotherMix());
                m.addSeparator();
                // Undo and redo on the mix itself, named after what they undo. Never locked by
                // LIVE SAFE: going back to the mix that was working a minute ago is the thing an
                // operator needs most in the middle of a service.
                m.addItem (411, view.controller.canUndoMix()
                                    ? "Undo " + juce::String (view.controller.undoMixLabel())
                                    : juce::String ("Undo Mix Change"),
                           view.controller.canUndoMix());
                m.addItem (412, view.controller.canRedoMix()
                                    ? "Redo " + juce::String (view.controller.redoMixLabel())
                                    : juce::String ("Redo Mix Change"),
                           view.controller.canRedoMix());
                m.addSeparator();
                m.addItem (401, "Reset Macros");
                m.addItem (402, view.controller.numSoloed() > 0
                                    ? "Clear Solo (" + juce::String (view.controller.numSoloed()) + ")"
                                    : juce::String ("Clear Solo"),
                           view.controller.numSoloed() > 0);
                m.addSeparator();
                m.addItem (403, "Bypass: Hear the Inputs   B", true, view.controller.isBypassed());
                m.addSeparator();
                // Off unless the user turns it on, and only offered when a key exists. The
                // built-in engineer needs neither, so this is a choice and never a requirement.
                m.addItem (406, "Mix Engineer: Use the Cloud Model",
                           OpenAiMixProvider().isAvailable(), view.usingCloudMixEngineer);
                break;
            case 4:
                m.addItem (500, "Play / Stop");
                m.addItem (501, "Record");
                m.addItem (502, "Return to Start");
                m.addItem (503, "Loop");
                break;
            case 5:
                m.addItem (613, "Setup");
                m.addItem (600, "Tracks");
                m.addItem (601, "Mixer");
                m.addItem (604, "Channel");
                m.addItem (602, "Tune");
                m.addItem (603, "Live");
                m.addSeparator();
                m.addItem (608, "Open Mixer in a New Window");
                m.addItem (609, "Outputs" + juce::String (Glyph::ellip()));
                m.addSeparator();
                m.addItem (610, (view.sidebarShown ? "Hide Channels" : "Show Channels") + juce::String ("   ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\x83\xe2\x8c\x98")) + "S");
                if (const auto right = view.panelName (false); right.isNotEmpty())
                    m.addItem (612, (view.panelShown (false) ? "Hide " : "Show ") + right + "   ]");
                m.addSeparator();
                m.addItem (605, "Zoom In");
                m.addItem (606, "Zoom Out");
                m.addItem (607, "Zoom to Fit");
                break;
            default:
                m.addItem (701, "Getting started");
                m.addSeparator();
                m.addItem (700, "About DLIVE");
                break;
        }
        return m;
    }

    void menuItemSelected (int id, int) override { view.handleCommand (id); }

private:
    MainView& view;
};

// ---------------------------------------------------------------- MainView
MainView::MainView (MixController& c, AppServices& s) : controller (c), services (s)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);

    sessionsPage = std::make_unique<SessionsPage> (controller, services);
    devicePage = std::make_unique<DevicePage> (controller, services);
    assignPage = std::make_unique<AssignPage> (controller, services);
    purposePage = std::make_unique<PurposePage> (controller);
    tracksPage = std::make_unique<TracksPage> (controller, services);
    mixerPage = std::make_unique<MixerPage> (controller, services);
    mixPage = std::make_unique<MixPage> (controller);
    livePage = std::make_unique<LivePage> (controller, services);
    advancedPage = std::make_unique<AdvancedPage> (controller);
    transportBar = std::make_unique<TransportBar> (controller, services);
    toast = std::make_unique<Toast>();
    sessionButton = std::make_unique<SessionButton>();
    menu = std::make_unique<Menu> (*this);

    for (juce::Component* p : { (juce::Component*) sessionsPage.get(), (juce::Component*) devicePage.get(),
                                (juce::Component*) assignPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        addChildComponent (*p);
    addChildComponent (*transportBar);

    // ---- the setup workspace's own list of steps
    setupNav = std::make_unique<SetupNav> (services);
    const Page setupPages[4] = { Page::Sessions, Page::Device, Page::Assign, Page::Purpose };
    for (int i = 0; i < 4; ++i)
        setupNav->item (i).onClick = [this, p = setupPages[i]] { showPage (p); };
    setupNav->item (0).setTooltip ("The library: every saved session, and what each one was for.");
    addChildComponent (*setupNav);

    // ---- the channel list, beside every workspace
    channelRail = std::make_unique<ChannelRail> (controller);
    channelRail->onSelect = [this] (int strip)
    {
        if (page == Page::Mixer) mixerPage->selectStrip (strip);
        else if (page == Page::Tracks) tracksPage->selectTrack (strip);
        else if (page == Page::Inspector) advancedPage->select (strip);
    };
    channelRail->onOpen = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    channelRail->onSelectBus = [this] (MixBus bus)
    {
        if (page == Page::Mixer) mixerPage->selectBus (bus);
        else if (page == Page::Inspector) advancedPage->selectBus (bus);
    };
    channelRail->onOpenBus = [this] (MixBus bus) { showPage (Page::Inspector); advancedPage->selectBus (bus); };
    channelRail->onTune = [this] (int strip) { tuneChannel (strip); };
    channelRail->onCollapsedChanged = [this]
    {
        sidebarShown = ! channelRail->isCollapsed();
        sidebarButton->setOn (sidebarShown);
        resized();
        repaint();
    };
    addAndMakeVisible (*channelRail);
    // One channel list, not two. The Inspector had its own rail because there was nothing else
    // to hold one; the window carries it now, so the Inspector's is switched off for good and
    // its middle column keeps that width instead.
    advancedPage->setRailAvailable (false);
    mixPage->setRailAvailable (false);

    statusBar = std::make_unique<StatusBar> (controller, services);
    addAndMakeVisible (*statusBar);

    // ---- toolbar
    addAndMakeVisible (*sessionButton);
    sessionButton->onClick = [this] { setupPopover(); };

    // One navigation. SETUP is first because that is the order the work happens in, and
    // CHANNEL sits between MIXER and TUNE because that is where you drop into a channel from.
    const char* tabLabels[kWorkspaceTabs] = { "SETUP", "TRACKS", "MIXER", "CHANNEL", "TUNE", "LIVE" };
    const Page tabPages[kWorkspaceTabs] = { Page::Device, Page::Tracks, Page::Mixer, Page::Inspector,
                                            Page::Tune, Page::Live };
    for (int i = 0; i < kWorkspaceTabs; ++i)
    {
        tabs[size_t (i)] = std::make_unique<DineButton> (tabLabels[i], DineButton::Style::Segment);
        tabs[size_t (i)]->setFontPx (11.0f);
        tabs[size_t (i)]->setPadX (13);
        tabs[size_t (i)]->setCaps (true);
        tabs[size_t (i)]->setClickingTogglesState (false);
        tabs[size_t (i)]->onClick = [this, p = tabPages[i]] { showPage (p); };
        addAndMakeVisible (*tabs[size_t (i)]);
    }

    bypassButton = std::make_unique<ToolbarToggle> ("BYPASS", Dine::warn);
    bypassButton->setTooltip ("Hear the inputs exactly as they arrive: no processing, no fader moves, no effects. "
                              "Press it again for your mix. Nothing is changed either way (B).");
    bypassButton->onClick = [this] { setBypass (! controller.isBypassed()); };
    addChildComponent (*bypassButton);

    // LIVE SAFE was a badge on one page. It is a policy, so it belongs where BYPASS is:
    // on screen, on every workspace, one click from the person responsible for the service.
    liveSafeButton = std::make_unique<ToolbarToggle> ("LIVE SAFE", Dine::accent, Dine::Icon::Shield);
    liveSafeButton->setTooltip ("LIVE SAFE: routing, re-tuning and opening a session are locked, and a fader "
                                "cannot move more than 6 dB at a time. Mute, solo, the monitor and the "
                                "recording always stay free.");
    liveSafeButton->onClick = [this] { handleCommand (614); };
    addChildComponent (*liveSafeButton);

    chatButton = std::make_unique<IconButton> ("AI Mix Chat", Dine::Icon::Chat);
    chatButton->setTooltip ("AI Mix Chat: ask for a change in words. It proposes; you keep.");
    chatButton->onClick = [this] { showChat(); };
    addChildComponent (*chatButton);

    sidebarButton = std::make_unique<SidebarButton>();
    sidebarButton->setTooltip ("Show or hide the channel list. The workspace takes the width (Ctrl-Cmd-S).");
    sidebarButton->onClick = [this] { setSidebarShown (! sidebarShown); };
    addAndMakeVisible (*sidebarButton);

    addAndMakeVisible (outputButton);
    outputButton.setTooltip ("Where the finished mix goes out.");
    outputButton.onClick = [this] { chooseOutput(); };

    addChildComponent (*toast);

    sessionsPage->onNew = [this] { newSession(); };
    sessionsPage->onOpen = [this] (const juce::File& file)
    {
        const auto err = services.loadSession (file);
        if (err.isNotEmpty()) { showToast (err); return; }
        advancedPage->rebuild();
        mixerPage->rebuild();
        if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
        tracksPage->rebuild();
        showPage (controller.getSession().inputs.empty() ? Page::Assign : Page::Tracks);
        showToast ("Opened \"" + services.currentSessionName() + "\".");
        updateChrome();
    };
    devicePage->onBack = [this] { showPage (Page::Sessions); };
    devicePage->onContinue = [this]
    {
        if (! controller.getSession().inputs.empty()) enterSession();
        else showPage (Page::Assign);
    };
    devicePage->onContinueToAssign = [this] { showPage (Page::Assign); };
    devicePage->onSetUpOutputs = [this] { showOutputs(); };
    devicePage->onImportRecording = [this] (const juce::File& folder)
    {
        const auto err = services.importMultitrack (folder);
        if (err.isNotEmpty()) { showToast (err); return; }
        assignPage->refresh();
        tracksPage->rebuild();
        showToast ("Imported " + folder.getFileName() + ".");
        showPage (Page::Assign);
    };
    assignPage->onBack = [this] { showPage (Page::Device); };
    assignPage->onContinue = [this] { showPage (Page::Purpose); };
    purposePage->onBack = [this] { showPage (Page::Assign); };
    purposePage->onContinue = [this] { enterSession(); };

    tracksPage->onToast = [this] (const juce::String& t) { showToast (t); };
    // The channel panel's width is a layout preference, so it is saved with the session and
    // put back when one is opened - an engineer sets it once for a room full of long names.
    tracksPage->onPanelWidthChanged = [this]
    {
        services.setTrackPanelWidth (tracksPage->panelWidth());
        services.saveSession();
    };
    tracksPage->onTimelineChanged = [this] { updateChrome(); };
    tracksPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    tracksPage->onOpenAssign = [this] { assignPage->refresh(); showPage (Page::Assign); };
    tracksPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    // A track was renamed or given a different source: the console, the Inspector and the
    // window title all read the session, so they are rebuilt together.
    tracksPage->onSessionChanged = [this]
    {
        advancedPage->rebuild();
        mixerPage->rebuild();
        if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
        updateChrome();
    };
    mixPage->onOpenAdvanced = [this] { showPage (Page::Inspector); };
    mixPage->onToast = [this] (const juce::String& t) { showToast (t); };
    mixPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    mixerPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    mixerPage->onOpenBus = [this] (MixBus bus) { showPage (Page::Inspector); advancedPage->selectBus (bus); };
    mixerPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    mixerPage->onOpenWindow = [this] { openMixerWindow(); };
    mixerPage->onToast = [this] (const juce::String& t) { showToast (t); };
    livePage->onToast = [this] (const juce::String& t) { showToast (t); };
    livePage->onLiveSafeChanged = [this] { updateChrome(); repaint(); };
    livePage->onToggleRecord = [this] { handleCommand (501); };
    advancedPage->onBack = [this] { showPage (Page::Tune); };
    advancedPage->onRetune = [this] { handleCommand (400); };
    advancedPage->onTuneChannel = [this] (int strip) { tuneChannel (strip); };
    transportBar->onToast = [this] (const juce::String& t) { showToast (t); };
    transportBar->onTimelineChanged = [this] { timelineChanged(); };

    controller.onMessage = [this] (const std::string& m) { showToast (m); };
    controller.onMixChanged = [this] { requestSave(); };

    setWantsKeyboardFocus (true);
    showPage (! services.listSessions().isEmpty() ? Page::Sessions : Page::Device);
    startTimerHz (30);

    // First Sunday: nobody has opened this before, so say what to do rather than waiting to
    // be asked. After that it is Help > Getting started and nothing opens by itself.
    if (gAutoTutorial && ! Tutorial::hasBeenSeen() && services.listSessions().isEmpty()
        && controller.getSession().inputs.empty())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainView> (this)]
                                         { if (safe != nullptr) safe->showTutorial(); });
}

MainView::~MainView()
{
    stopTimer();
    mixerWindow.reset();          // before the look and feel it draws with
    outputsSheet.reset();
    channelSheet.reset();
    chatSheet.reset();
    controller.onMessage = nullptr;
    controller.onMixChanged = nullptr;
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

juce::MenuBarModel* MainView::getMenuModel() { return menu.get(); }

void MainView::enterSession()
{
    services.reconfigure();
    services.saveSession();
    advancedPage->rebuild();
    mixerPage->rebuild();
    if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
    tracksPage->rebuild();
    showPage (Page::Tracks);
}

void MainView::timelineChanged()
{
    tracksPage->rebuild();
    services.saveSession();
    updateChrome();
}

// The policy itself lives in src/Mix/LiveSafe.h and is enforced in MixController. This is the
// UI half: it greys a menu item out and says the same sentence the controller would, so the
// lock cannot mean one thing in a menu and another in the code.
bool MainView::liveSafeBlocks (const juce::String& what)
{
    if (! services.daw().getProject().liveSafe) return false;
    showToast ("LIVE SAFE is on: " + what + " is locked. " + juce::String (liveSafe::allowedSummary())
               + " Turn the lock off on the LIVE page.");
    return true;
}

void MainView::showPage (Page p)
{
    page = p;
    sessionsPage->setVisible (p == Page::Sessions);
    devicePage->setVisible (p == Page::Device);
    assignPage->setVisible (p == Page::Assign);
    purposePage->setVisible (p == Page::Purpose);
    tracksPage->setVisible (p == Page::Tracks);
    mixerPage->setVisible (p == Page::Mixer);
    mixPage->setVisible (p == Page::Tune);
    livePage->setVisible (p == Page::Live);
    advancedPage->setVisible (p == Page::Inspector);

    if (p == Page::Sessions) sessionsPage->refresh();
    if (p == Page::Device) devicePage->refresh();
    if (p == Page::Assign) assignPage->refresh();
    if (p == Page::Purpose) purposePage->refresh();
    if (p == Page::Tracks)
    {
        if (const int w = services.trackPanelWidth(); w > 0) tracksPage->setPanelWidth (w);
        tracksPage->rebuild();
    }
    if (p == Page::Mixer) mixerPage->rebuild();
    if (p == Page::Live) livePage->rebuild();
    if (p == Page::Inspector) advancedPage->rebuild();

    // The rail follows whichever workspace is up, so picking a channel anywhere picks it
    // out everywhere. It never moves the selection itself - it only reads it.
    if (channelRail != nullptr)
    {
        const int sel = selectedChannel();
        if (sel >= 0) channelRail->setSelected (sel);
    }

    updateChrome();
    resized();
    repaint();
    grabKeyboardFocus();          // Space, R, Return and L belong to the transport wherever you are
}

void MainView::updateChrome()
{
    const auto& session = controller.getSession();
    const bool running = services.isAudioRunning();
    const bool hasInputs = ! session.inputs.empty();
    const bool mixable = controller.isPrepared() && hasInputs;
    const bool inWorkspace = ! isSetupPage (page);
    const auto& project = services.daw().getProject();

    // ---- SETUP's own list of steps
    const Page setupPages[4] = { Page::Sessions, Page::Device, Page::Assign, Page::Purpose };
    const bool setupDone[4] = { ! services.listSessions().isEmpty(), running, hasInputs, mixable };
    for (int i = 0; i < 4; ++i)
    {
        setupNav->item (i).setSelected (page == setupPages[i]);
        setupNav->item (i).setDone (setupDone[i]);
        setupNav->item (i).setEnabled (i < 2 || running || hasInputs);
    }
    setupNav->item (0).setMeta (juce::String (services.listSessions().size()));
    setupNav->item (2).setMeta (hasInputs ? juce::String (int (session.inputs.size())) : juce::String());

    // ---- the one navigation
    const Page tabPages[kWorkspaceTabs] = { Page::Device, Page::Tracks, Page::Mixer, Page::Inspector,
                                            Page::Tune, Page::Live };
    for (int i = 0; i < kWorkspaceTabs; ++i)
    {
        const bool isSetupTab = i == 0;
        tabs[size_t (i)]->setToggleState (isSetupTab ? isSetupPage (page) : page == tabPages[i],
                                          juce::dontSendNotification);
        tabs[size_t (i)]->setEnabled (isSetupTab || mixable);
    }

    outputButton.setVisible (running || inWorkspace);
    bypassButton->setVisible (inWorkspace && mixable);
    bypassButton->setOn (controller.isBypassed());
    liveSafeButton->setVisible (inWorkspace && mixable);
    liveSafeButton->setOn (project.liveSafe);
    liveSafeButton->setSuffix (project.liveSafe ? "ON" : "OFF");
    chatButton->setVisible (inWorkspace && mixable);
    transportBar->setVisible (inWorkspace);
    setupNav->setVisible (isSetupPage (page));
    channelRail->setVisible (inWorkspace && mixable);


    juce::String name = services.currentSessionName();
    if (name.isEmpty()) name = "Untitled";
    // What the document line says under the name: how big this session is and what it is
    // meant to sound like. Short on purpose - the clock beside it may never be truncated.
    juce::String sub = hasInputs ? juce::String (int (session.inputs.size())) + " inputs  " + Glyph::dot() + "  "
                                       + juce::String (styleProfileName (session.profile))
                                 : juce::String (styleProfileName (session.profile)) + "  " + Glyph::dot() + "  "
                                       + juce::String (mixPurposeName (session.purpose));
    sessionButton->set (name, sub);

    // The device DLIVE built to join two of them is not a name anybody chose, so the toolbar
    // says what the user picked instead (AppServices::outputDisplayName).
    const juce::String out = services.outputDisplayName();
    outputButton.setValue (out.isEmpty() ? "No output" : out);
}

// The session button's popover: the four setup steps with what each is currently set to, a
// tick against the ones that are done, and the two ways out - the full setup workspace and
// the library. It is the same four steps SETUP holds, so there is one answer to "where am I
// up to" rather than two.
void MainView::setupPopover()
{
    const auto& session = controller.getSession();
    const bool running = services.isAudioRunning();
    const bool hasInputs = ! session.inputs.empty();
    const bool ready = controller.isPrepared() && hasInputs;

    juce::PopupMenu m;
    m.addSectionHeader (ready ? "Ready for soundcheck" : "Session and setup");
    m.addItem (1, juce::String (running ? juce::String (Glyph::check()) : juce::String (Glyph::dash()))
                      + "  Audio device" + juce::String ("   ")
                      + (running && services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                                                                              : juce::String ("not chosen")));
    m.addItem (2, juce::String (hasInputs ? juce::String (Glyph::check()) : juce::String (Glyph::dash()))
                      + "  Inputs" + juce::String ("   ")
                      + (hasInputs ? juce::String (int (session.inputs.size())) + " named" : juce::String ("none yet")));
    m.addItem (3, juce::String (ready ? juce::String (Glyph::check()) : juce::String (Glyph::dash()))
                      + "  Purpose and sound" + juce::String ("   ")
                      + juce::String (mixPurposeName (session.purpose)) + ", "
                      + juce::String (styleProfileName (session.profile)));
    m.addItem (4, juce::String (Glyph::dash()) + juce::String ("  Recording destination   ")
                      + (services.sessionFolder() != juce::File() ? services.sessionFolder().getFileName()
                                                                  : juce::String ("chosen when you save")));
    m.addSeparator();
    m.addItem (5, "Open setup");
    m.addItem (6, "Sessions...");
    m.addSeparator();
    m.addItem (7, "Save");
    m.addItem (8, "Save As...");
    m.addItem (9, "Rename or fix the inputs...");
    m.addSeparator();
    m.addItem (10, "Getting started");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton.get())
                                               .withMinimumWidth (300),
                     [this] (int r)
                     {
                         switch (r)
                         {
                             case 1: showPage (Page::Device); break;
                             case 2: assignPage->refresh(); showPage (Page::Assign); break;
                             case 3: showPage (Page::Purpose); break;
                             case 4:
                             case 7: saveNow(); break;
                             case 5: showPage (controller.isPrepared() ? Page::Purpose : Page::Device); break;
                             case 6: showPage (Page::Sessions); break;
                             case 8: saveAs(); break;
                             case 9: assignPage->refresh(); showPage (Page::Assign); break;
                             case 10: showTutorial(); break;
                             default: break;
                         }
                     });
}

// What the first-run coach is pointing at, in window coordinates. The rectangles are read
// from the live components rather than written down twice, so a layout change can never
// leave the tour ringing empty space.
juce::Rectangle<int> MainView::spotlight (const juce::String& what) const
{
    if (what == "session" && sessionButton != nullptr) return sessionButton->getBounds();
    if (what == "transport" && transportBar != nullptr && transportBar->isVisible()) return transportBar->getBounds();
    if (what == "livesafe" && liveSafeButton != nullptr && liveSafeButton->isVisible()) return liveSafeButton->getBounds();
    if (what == "rail" && channelRail != nullptr && channelRail->isVisible()) return channelRail->getBounds();
    if (what == "tabs" && tabs[0] != nullptr)
    {
        auto track = tabs[0]->getBounds();
        for (int i = 1; i < kWorkspaceTabs; ++i) track = track.getUnion (tabs[size_t (i)]->getBounds());
        return track.expanded (3, 3);
    }
    return {};
}

void MainView::setAutoTutorial (bool on) { gAutoTutorial = on; }

void MainView::closeTutorial() { tutorial.reset(); }

void MainView::showTutorial()
{
    if (tutorial != nullptr) { tutorial->toFront (true); return; }
    tutorial = std::make_unique<Tutorial>();
    tutorial->onStep = [this] (int p) { showPage (Page (juce::jlimit (0, 8, p))); };
    tutorial->spotFor = [this] (const juce::String& what) { return spotlight (what); };
    tutorial->onFinished = [this]
    {
        tutorial.reset();
        grabKeyboardFocus();
    };
    addAndMakeVisible (*tutorial);
    tutorial->setBounds (getLocalBounds());
    tutorial->start();
    tutorial->toFront (true);
}

// ---------------------------------------------------------------- the panels
// Every panel at the edge of the window folds away and comes back the same way: the channel
// list from the toolbar's leftmost switch, a workspace's own rails from the handle in their
// gutter. None of it touches the session - it is only where the width goes.
void MainView::setSidebarShown (bool shown)
{
    if (shown == sidebarShown) return;
    sidebarShown = shown;
    sidebarButton->setOn (shown);
    channelRail->setCollapsed (! shown);
    resized();
    repaint();
}

// What the page on screen calls the panel on that side, and whether it is open. The channel
// list is the left panel of every workspace, so `[` means the same thing everywhere.
juce::String MainView::panelName (bool left) const
{
    if (left) return "Channels";
    if (page == Page::Inspector) return "What DINE did";
    return {};
}

bool MainView::panelShown (bool left) const
{
    if (left) return sidebarShown;
    if (page == Page::Inspector) return advancedPage->isTrailShown();
    return true;
}

void MainView::togglePanel (bool left)
{
    if (left) { setSidebarShown (! sidebarShown); return; }
    if (page == Page::Inspector) { advancedPage->setTrailShown (! advancedPage->isTrailShown()); return; }
}

// ---------------------------------------------------------------- bypass and the second console
void MainView::setBypass (bool on)
{
    if (! controller.isPrepared() || controller.getSession().inputs.empty())
    {
        showToast ("There is no mix to bypass yet. Assign your inputs first.");
        return;
    }
    if (controller.isBypassed() == on) return;
    controller.setBypass (on);
    bypassButton->setOn (on);
    showToast (on ? "BYPASS on: hearing the inputs as they arrive. Press B for the mix."
                  : "BYPASS off: hearing the mix.");
    mixerPage->repaint();
    if (mixerWindow != nullptr) mixerWindow->getPage().repaint();
    livePage->repaint();
    mixPage->repaint();
}

void MainView::closeSheets()
{
    outputsSheet.reset();
    channelSheet.reset();
    chatSheet.reset();
    updateChrome();
    resized();
}

void MainView::showOutputs()
{
    if (outputsSheet != nullptr) { outputsSheet->refresh(); return; }
    outputsSheet = std::make_unique<OutputsSheet> (controller, services);
    outputsSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    outputsSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->outputsSheet.reset(); safe->updateChrome(); } });
    };
    outputsSheet->onChooseDevice = [this] (const juce::String& name)
    {
        if (name == services.currentOutputDevice()) return;
        const auto err = services.isAudioRunning() ? services.changeOutput (name) : services.openOutputOnly (name);
        if (err.isNotEmpty()) showToast (err);
        else { showToast ("Output: " + name); updateChrome(); }
        if (outputsSheet != nullptr) outputsSheet->refresh();
    };
    addAndMakeVisible (*outputsSheet);
    resized();
    outputsSheet->toFront (true);
}

// ---- TUNE CHANNEL
// The channel a workspace has picked out is the one the Mix menu and `T` tune: MIXER and
// TRACKS both keep a selection (it is what their chain strip reads), and the Inspector is
// always looking at one channel.
int MainView::selectedChannel() const
{
    switch (page)
    {
        case Page::Mixer:     return mixerPage->selectedStrip();
        case Page::Tracks:    return tracksPage->selectedTrack();
        case Page::Inspector: return advancedPage->selectedStrip();
        default:              return -1;
    }
}

void MainView::tuneChannel (int strip, const MixController::ListenSettings& settings)
{
    if (liveSafeBlocks ("TUNE CHANNEL")) return;
    if (! controller.isPrepared() || strip < 0 || strip >= controller.getEngine().getNumStrips())
    {
        showToast ("Assign your inputs first: there is nothing to tune yet.");
        return;
    }
    if (controller.isBypassed())
    {
        showToast ("BYPASS is on: switch it off to tune a channel.");
        return;
    }
    if (controller.getStage() == MixController::Stage::Listening || controller.getStage() == MixController::Stage::Planning)
    {
        showToast ("DLIVE is already listening. Let it finish, or cancel it first.");
        return;
    }

    channelSheet.reset();
    controller.startTuneChannel (strip, settings);
    if (! controller.isListening()) return;

    channelSheet = std::make_unique<ChannelTuneSheet> (controller, strip);
    channelSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    channelSheet->onOpenInspector = [this, strip] { showPage (Page::Inspector); advancedPage->select (strip); };
    channelSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->channelSheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*channelSheet);
    resized();
    channelSheet->toFront (true);
}

void MainView::showChat() { openChat(); }

// AI MIX CHAT. It opens over the workspace rather than taking one of its own: the mix is the
// thing being discussed, and it should stay visible and audible while it is.
void MainView::openChat()
{
    if (chatSheet != nullptr)
    {
        chatSheet->toFront (true);
        chatSheet->takeFocus();
        return;
    }
    if (! controller.isPrepared())
    {
        showToast ("Assign your inputs first: there is nothing to talk about yet.");
        return;
    }
    chatSheet = std::make_unique<ChatSheet> (controller);
    chatSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    chatSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->chatSheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*chatSheet);
    resized();
    chatSheet->toFront (true);
    chatSheet->takeFocus();
}

// ---------------------------------------------------------------------------
// Input mappings
//
// A church patches the same desk the same way every week. Saving that is the difference
// between a five-minute setup and a forty-minute one, and the only thing that matters about
// restoring it is that it must never put the wrong audio on a channel: a map made on a
// 32-channel desk applied to an 8-channel interface brings its extra inputs back switched
// off and named, with a sentence saying what is missing.
// ---------------------------------------------------------------------------
void MainView::saveInputMapping()
{
    const auto& session = controller.getSession();
    if (session.inputs.empty()) { showToast ("There is nothing patched yet."); return; }

    mapDialog = std::make_unique<juce::AlertWindow> ("Save input mapping",
                                                     "A name you will recognise next Sunday.",
                                                     juce::MessageBoxIconType::NoIcon);
    mapDialog->addTextEditor ("name", services.currentSessionName().isEmpty() ? "Main hall" : services.currentSessionName(), "Name");
    mapDialog->addTextEditor ("note", "", "Note (optional)");
    mapDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    mapDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    mapDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (mapDialog == nullptr) return;
        const auto name = mapDialog->getTextEditorContents ("name").trim();
        const auto note = mapDialog->getTextEditorContents ("note").trim();
        mapDialog.reset();
        if (result != 1 || name.isEmpty()) return;

        auto map = InputMapStore::fromSession (controller.getSession(), name, services.currentInputDevice(),
                                               services.numInputChannels(), true);
        map.note = note;
        if (InputMapStore::save (map))
            showToast ("Saved \"" + name + "\": " + juce::String (map.inputs.size()) + " inputs across "
                       + juce::String (map.channelsNeeded()) + " channels.");
        else
            showToast ("That mapping could not be saved.");
    }), false);
}

void MainView::openInputMappings()
{
    const auto maps = InputMapStore::list();
    juce::PopupMenu m;
    if (maps.isEmpty())
        m.addItem (-1, "No saved mappings yet", false, false);

    int id = 1;
    juce::Array<juce::File> files;
    for (const auto& l : maps)
    {
        juce::PopupMenu sub;
        const int base = id;
        files.add (l.file);
        sub.addItem (base, "Apply to this session");
        sub.addSeparator();
        sub.addItem (base + 1, "Rename" + juce::String (Glyph::ellip()));
        sub.addItem (base + 2, "Duplicate");
        sub.addItem (base + 3, "Export" + juce::String (Glyph::ellip()));
        sub.addSeparator();
        sub.addItem (base + 4, "Delete");
        id += 10;
        m.addSubMenu (l.name + "   (" + juce::String (l.inputCount) + " inputs, "
                          + juce::String (l.channelsNeeded) + " channels)", sub);
    }
    m.addSeparator();
    m.addItem (900, "Import a mapping" + juce::String (Glyph::ellip()));
    m.addItem (901, "Save this session's patch" + juce::String (Glyph::ellip()),
               ! controller.getSession().inputs.empty());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (300),
                     [this, files] (int chosen)
    {
        if (chosen <= 0) return;
        if (chosen == 901) { saveInputMapping(); return; }
        if (chosen == 900)
        {
            mapChooser = std::make_unique<juce::FileChooser> ("Import an input mapping", juce::File(), "*.dlivemap.json");
            mapChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                     [this] (const juce::FileChooser& fc)
                                     {
                                         const auto file = fc.getResult();
                                         if (file == juce::File()) return;
                                         InputMap imported;
                                         if (! InputMapStore::load (file, imported)) { showToast ("That is not a DLIVE input mapping."); return; }
                                         if (! InputMapStore::save (imported)) { showToast ("That mapping could not be saved."); return; }
                                         showToast ("Imported \"" + imported.name + "\". Open Input Mappings to apply it.");
                                     });
            return;
        }

        const int index = (chosen - 1) / 10;
        const int action = (chosen - 1) % 10;
        if (index < 0 || index >= files.size()) return;
        InputMap map;
        if (! InputMapStore::load (files[index], map)) { showToast ("That mapping could not be read."); return; }

        switch (action)
        {
            case 0: applyInputMapping (files[index]); break;
            case 1:
            {
                mapDialog = std::make_unique<juce::AlertWindow> ("Rename mapping", "", juce::MessageBoxIconType::NoIcon);
                mapDialog->addTextEditor ("name", map.name, "Name");
                mapDialog->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
                mapDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                const auto from = map.name;
                mapDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, from] (int r)
                {
                    if (mapDialog == nullptr) return;
                    const auto to = mapDialog->getTextEditorContents ("name").trim();
                    mapDialog.reset();
                    if (r != 1) return;
                    const auto err = InputMapStore::rename (from, to);
                    showToast (err.isEmpty() ? "Renamed to \"" + to + "\"." : err);
                }), false);
                break;
            }
            case 2:
            {
                const auto err = InputMapStore::duplicate (map.name, map.name + " copy");
                showToast (err.isEmpty() ? "Duplicated \"" + map.name + "\"." : err);
                break;
            }
            case 3:
            {
                mapChooser = std::make_unique<juce::FileChooser> ("Export input mapping",
                                                                  juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                      .getChildFile (juce::File::createLegalFileName (map.name) + ".dlivemap.json"),
                                                                  "*.dlivemap.json");
                mapChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                         [this, map] (const juce::FileChooser& fc)
                                         {
                                             const auto dest = fc.getResult();
                                             if (dest == juce::File()) return;
                                             showToast (InputMapStore::saveAs (map, dest)
                                                            ? "Exported to " + dest.getFileName() + "."
                                                            : juce::String ("That mapping could not be exported."));
                                         });
                break;
            }
            case 4:
                if (InputMapStore::remove (map.name)) showToast ("Deleted \"" + map.name + "\".");
                break;
            default: break;
        }
    });
}

void MainView::applyInputMapping (const juce::File& file)
{
    if (liveSafeBlocks ("changing the routing")) return;
    InputMap map;
    if (! InputMapStore::load (file, map)) { showToast ("That mapping could not be read."); return; }

    const auto result = InputMapStore::apply (map, controller.getSession(), services.numInputChannels(),
                                              services.currentInputDevice(), map.hasSound);

    // Anything that could not be honoured is said *before* it is applied, never after: this is
    // the one operation in DLIVE that could silently put the pastor's microphone on the kick.
    if (! result.problems.empty())
    {
        juce::String body;
        for (const auto& p : result.problems) body << p << "\n\n";
        body << juce::String (result.restored) << " of " << juce::String (map.inputs.size())
             << " inputs can be restored exactly as they were.";
        mapDialog = std::make_unique<juce::AlertWindow> ("Apply \"" + map.name + "\"?", body,
                                                         juce::MessageBoxIconType::WarningIcon);
        mapDialog->addButton ("Apply anyway", 1, juce::KeyPress (juce::KeyPress::returnKey));
        mapDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        const auto session = result.session;
        mapDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, session, map, result] (int r)
        {
            mapDialog.reset();
            if (r != 1) return;
            controller.setSession (session);
            services.reconfigure();
            services.saveSession();
            assignPage->refresh();
            updateChrome();
            resized();
            showToast ("Applied \"" + map.name + "\": " + juce::String (result.restored) + " inputs restored, "
                       + juce::String (result.unavailable + result.conflicts) + " switched off. Check the INPUTS page.");
        }), false);
        return;
    }

    controller.setSession (result.session);
    services.reconfigure();
    services.saveSession();
    assignPage->refresh();
    updateChrome();
    resized();
    showToast ("Applied \"" + map.name + "\": " + juce::String (result.restored) + " inputs.");
}

void MainView::openMixerWindow()
{
    if (! controller.isPrepared() || controller.getSession().inputs.empty())
    {
        showToast ("Assign your inputs first: there is nothing to mix yet.");
        return;
    }
    if (mixerWindow != nullptr)
    {
        mixerWindow->toFront (true);
        return;
    }
    mixerWindow = std::make_unique<MixerWindow> (controller, services, *this);
    showToast ("The mixer is open in its own window. Close it and it comes back here.");
}

void MainView::closeMixerWindow()
{
    juce::Component::SafePointer<MainView> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->mixerWindow.reset(); });
}

void MainView::showToast (const juce::String& text)
{
    toast->show (text);
    toastTicks = 30 * 5;
    resized();
}

// ---------------------------------------------------------------- commands
void MainView::handleCommand (int id)
{
    switch (id)
    {
        case 100: newSession(); break;
        case 101: openSession(); break;
        case 102: saveNow(); break;
        case 103: saveAs(); break;
        case 104: importMultitrack(); break;
        case 107:
        case 408:
            showPage (Page::Tune);
            mixPage->openReference();
            break;
        case 105: exportMix (AppServices::ExportFormat::Wav); break;
        case 106: exportMix (AppServices::ExportFormat::Mp3); break;

        case 200: if (! liveSafeBlocks ("editing the timeline")) tracksPage->undo(); break;
        case 201: if (! liveSafeBlocks ("editing the timeline")) tracksPage->splitAtPlayhead(); break;
        case 202: if (! liveSafeBlocks ("editing the timeline")) tracksPage->deleteSelection(); break;
        case 203:
            if (liveSafeBlocks ("adding a marker")) break;
            showPage (Page::Tracks);
            tracksPage->addMarkerAtPlayhead();
            break;

        case 300:
        case 301:
        {
            auto& project = services.daw().getProject();
            for (auto& t : project.tracks) t.armed = (id == 300);
            services.daw().refresh();
            services.saveSession();
            tracksPage->repaint();
            showToast (id == 300 ? "Every track will be recorded." : "No tracks will be recorded.");
            break;
        }
        case 302:
        case 303:
        case 304:
        {
            const MonitorMode mode = id == 302 ? MonitorMode::Input : id == 303 ? MonitorMode::Auto : MonitorMode::Off;
            auto& project = services.daw().getProject();
            for (auto& t : project.tracks) t.monitor = mode;
            services.daw().refresh();
            services.saveSession();
            tracksPage->repaint();
            showToast (juce::String ("Monitoring: ") + monitorModeName (mode) + " on every track.");
            break;
        }

        case 409:
            openChat();
            break;
        case 410:
            if (liveSafeBlocks ("TUNE LIVE MIX")) break;
            showPage (Page::Tune);
            controller.tryAnotherMix();
            break;
        case 411: controller.undoMix(); updateChrome(); break;
        case 412: controller.redoMix(); updateChrome(); break;
        case 108: saveInputMapping(); break;
        case 109: openInputMappings(); break;

        case 400:
            if (liveSafeBlocks ("TUNE MIX")) break;
            showPage (Page::Tune);
            mixPage->pressTune();
            break;
        case 405:
            // LIVE SAFE locks re-tuning for the same reason it locks routing: nothing may
            // change the sound by accident once the service has started.
            if (liveSafeBlocks ("TUNE LIVE MIX")) break;
            showPage (Page::Tune);
            mixPage->pressLiveTune();
            break;
        case 404:
            if (liveSafeBlocks ("TUNE CHANNEL")) break;
            if (selectedChannel() < 0) { showToast ("Pick a channel first: click a strip on the mixer or a track header."); break; }
            tuneChannel (selectedChannel());
            break;
        case 407:
            // The same rule as a re-tune: once the service is on, nothing changes the sound
            // by accident. Aiming at a reference is a re-tune of the master.
            if (liveSafeBlocks ("MATCH TO REFERENCE")) break;
            if (! controller.hasReference()) { showPage (Page::Tune); mixPage->openReference(); break; }
            showPage (Page::Tune);
            controller.startReferenceMatch();
            showToast (controller.hasListened()
                           ? "Aimed at " + juce::String (controller.getReference().name) + ". Compare it with BEFORE, then KEEP or REVERT."
                           : "DLIVE has not heard the band yet, so it is listening first.");
            break;
        case 305:
        case 306:
            if (tracksPage == nullptr) break;
            if (tracksPage->selectedTrack() < 0) { showToast ("Pick a track first: click its header on TRACKS."); break; }
            showPage (Page::Tracks);
            tracksPage->moveSelectedTrack (id == 305 ? -1 : 1);
            break;
        case 401: controller.resetMacros(); showToast ("Macros back to the plan."); break;
        case 402: controller.clearSolos(); showToast ("Solos cleared."); break;
        case 403: setBypass (! controller.isBypassed()); break;
        case 406:
        {
            // Nothing about the mix changes here: this only decides who is asked what the mix
            // should sound like. The capability list, the resolver and the bounds are the same
            // either way, so a cloud answer can do nothing a local one could not.
            usingCloudMixEngineer = ! usingCloudMixEngineer;
            if (usingCloudMixEngineer && ! OpenAiMixProvider().isAvailable())
            {
                usingCloudMixEngineer = false;
                showToast ("No API key is configured, so DLIVE is using its own mix engineer.");
                break;
            }
            if (usingCloudMixEngineer) controller.setReasoningProvider (std::make_shared<OpenAiMixProvider>());
            else controller.setReasoningProvider (nullptr);
            showToast (usingCloudMixEngineer
                           ? "TUNE LIVE MIX will ask the cloud mix engineer. Measurements and source names are sent; no audio ever leaves this machine."
                           : "TUNE LIVE MIX is back on DLIVE's own mix engineer. Nothing leaves this machine.");
            break;
        }

        case 500: transportBar->togglePlay(); break;
        // Recording is transport, and LIVE SAFE never locks the transport: the page that
        // tells you to "lock the session before the service starts" must not then refuse to
        // record the service. (The toolbar key always went straight through; this agrees.)
        case 501: transportBar->toggleRecord(); break;
        case 502: transportBar->returnToStart(); break;
        case 503: transportBar->toggleLoop(); break;

        case 600: showPage (Page::Tracks); break;
        case 601: showPage (Page::Mixer); break;
        case 602: showPage (Page::Tune); break;
        case 603: showPage (Page::Live); break;
        case 604: showPage (Page::Inspector); break;
        case 605: tracksPage->zoom (1.4); break;
        case 606: tracksPage->zoom (1.0 / 1.4); break;
        case 607: tracksPage->zoomToFit(); break;
        case 608: openMixerWindow(); break;
        case 609: showOutputs(); break;
        case 610: setSidebarShown (! sidebarShown); break;
        case 611: togglePanel (true); break;
        case 612: togglePanel (false); break;
        case 613: showPage (isSetupPage (page) ? page : Page::Device); break;

        // LIVE SAFE, from the toolbar as well as from the LIVE page: it is a policy about the
        // whole desk, so it is on screen on every workspace rather than on one of them.
        case 614:
        {
            auto& daw = services.daw();
            daw.setLiveSafe (! daw.isLiveSafe());
            livePage->rebuild();
            updateChrome();
            services.saveSession();
            showToast (daw.isLiveSafe()
                           ? juce::String ("LIVE SAFE on. ") + liveSafe::allowedSummary()
                           : juce::String ("LIVE SAFE off. Everything is editable again."));
            break;
        }

        case 701: showTutorial(); break;
        case 700:
            showToast ("DLIVE - the live recording and broadcast DAW. Connect. Record. Mix. Tune. Broadcast.");
            break;
        default: break;
    }
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    const bool cmd = key.getModifiers().isCommandDown();
    const auto code = key.getKeyCode();

    if (cmd)
    {
        // Ctrl-Cmd-S is the sidebar wherever macOS puts one; Cmd-S is still Save.
        if (code == 'S' && key.getModifiers().isCtrlDown()) { handleCommand (610); return true; }
        if (code == 'S' && ! key.getModifiers().isShiftDown()) { handleCommand (102); return true; }
        if (code == 'S') { handleCommand (103); return true; }
        if (code == 'Z') { handleCommand (200); return true; }
        if (code == 'E') { handleCommand (201); return true; }
        if (code == 'O') { handleCommand (101); return true; }
        if (code == 'N') { handleCommand (100); return true; }
        if (code == '=' || code == '+') { handleCommand (605); return true; }
        if (code == '-') { handleCommand (606); return true; }
        if (code == '0') { handleCommand (607); return true; }
        // Cmd-1..6 follow the tab bar left to right: SETUP TRACKS MIXER CHANNEL TUNE LIVE.
        if (code >= '1' && code <= '6')
        {
            static constexpr int kTabCommand[6] = { 613, 600, 601, 604, 602, 603 };
            handleCommand (kTabCommand[code - '1']);
            return true;
        }
        return false;
    }

    if (code == juce::KeyPress::spaceKey)  { handleCommand (500); return true; }
    if (code == juce::KeyPress::returnKey) { handleCommand (502); return true; }
    if (code == 'R')                       { handleCommand (501); return true; }
    if (code == 'L')                       { handleCommand (503); return true; }
    if (code == 'B')                       { handleCommand (403); return true; }
    if (code == 'T')                       { handleCommand (404); return true; }
    if (code == 'M')                       { handleCommand (203); return true; }
    if (code == '[')                       { handleCommand (611); return true; }
    if (code == ']')                       { handleCommand (612); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (page == Page::Tracks) { handleCommand (202); return true; }
    }
    return false;
}

// ---------------------------------------------------------------- session document
void MainView::newSession()
{
    if (liveSafeBlocks ("starting a new session")) return;
    if (! controller.getSession().inputs.empty()) services.saveSession();
    services.newSession();
    assignPage->refresh();
    tracksPage->rebuild();
    mixerPage->rebuild();
    if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
    advancedPage->rebuild();
    showPage (Page::Device);
    showToast ("New session.");
}

void MainView::sessionMenu()
{
    juce::PopupMenu m;
    m.addItem (100, "New session");
    m.addItem (101, "Open session...");
    m.addSeparator();
    m.addItem (102, "Save session");
    m.addItem (103, "Save as a new session...");
    m.addSeparator();
    m.addItem (104, "Import a multitrack folder...");
    m.addSeparator();
    m.addItem (105, "Export stereo mix as WAV...");
    m.addItem (106, "Export stereo mix as MP3...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton.get()).withMinimumWidth (260),
                     [this] (int result) { if (result > 0) handleCommand (result); });
}

void MainView::importMultitrack()
{
    if (liveSafeBlocks ("importing")) return;
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of recorded stems",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder == juce::File()) return;
                              const auto err = services.importMultitrack (folder);
                              if (err.isNotEmpty()) { showToast (err); return; }
                              assignPage->refresh();
                              tracksPage->rebuild();
                              mixerPage->rebuild();
                              if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
                              advancedPage->rebuild();
                              showToast ("Imported " + folder.getFileName() + ". Check the inputs, then build the mix.");
                              showPage (Page::Assign);
                          });
}

void MainView::exportMix (AppServices::ExportFormat format)
{
    if (! controller.isPrepared() || controller.getSession().inputs.empty())
    {
        showToast ("Finish setup and assign inputs before exporting.");
        return;
    }
    if (! services.daw().getProject().hasAudio())
    {
        showToast ("There is nothing recorded yet. Record a take, or import a multitrack folder.");
        return;
    }

    if (exporting) { showToast ("An export is already running. It will say when it is done."); return; }

    const bool mp3 = format == AppServices::ExportFormat::Mp3;
    const auto suggest = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                             .getChildFile (services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "DLIVE mix")
                             .withFileExtension (mp3 ? "mp3" : "wav");
    chooser = std::make_unique<juce::FileChooser> ("Export the stereo mix", suggest, mp3 ? "*.mp3" : "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, format] (const juce::FileChooser& fc)
                          {
                              const auto dest = fc.getResult();
                              if (dest == juce::File()) return;
                              if (exporting) return;

                              // The render reads the session, the mix and every clip for as long as it
                              // takes; the console keeps running behind it. So it works from a copy
                              // taken here, on the message thread, not from the live document.
                              auto job = services.snapshotExport();
                              exporting = true;
                              showToast ("Exporting " + dest.getFileName() + Glyph::ellip());
                              juce::Component::SafePointer<MainView> safe (this);
                              auto& srv = services;
                              juce::Thread::launch ([safe, &srv, job, dest, format]
                              {
                                  const auto err = srv.exportMix (job, dest, format, {});
                                  juce::MessageManager::callAsync ([safe, err, dest]
                                  {
                                      if (safe == nullptr) return;
                                      safe->exporting = false;
                                      safe->showToast (err.isNotEmpty() ? err : "Exported " + dest.getFileName() + ".");
                                  });
                              });
                          });
}

void MainView::saveNow()
{
    if (services.currentSessionName().isEmpty()) { saveAs(); return; }
    services.saveSession();
    showToast ("Session saved.");
}

void MainView::saveAs()
{
    auto* alert = new juce::AlertWindow ("Save session",
                                         "Name this session. It becomes a folder on this Mac, with its recordings inside.",
                                         juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "Sunday", "Name");
    alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert] (int r)
    {
        std::unique_ptr<juce::AlertWindow> closer (alert);
        if (r != 1) return;
        const auto err = services.saveSessionAs (alert->getTextEditorContents ("name"));
        if (err.isNotEmpty()) showToast (err);
        else { showToast ("Saved \"" + services.currentSessionName() + "\"."); updateChrome(); }
    }), true);
}

void MainView::openSession()
{
    juce::PopupMenu m;
    const auto listed = services.listSessions();
    if (listed.isEmpty())
    {
        showToast ("No saved sessions yet. Save one from the session menu.");
        return;
    }
    for (int i = 0; i < listed.size(); ++i)
        m.addItem (i + 1, listed[i].name + "   " + listed[i].modified.formatted ("%d %b"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton.get()).withMinimumWidth (280),
                     [this, listed] (int result)
                     {
                         if (result <= 0 || result > listed.size()) return;
                         const auto err = services.loadSession (listed[result - 1].file);
                         if (err.isNotEmpty()) { showToast (err); return; }
                         advancedPage->rebuild();
                         mixerPage->rebuild();
                         if (mixerWindow != nullptr) mixerWindow->getPage().rebuild();
                         tracksPage->rebuild();
                         showPage (controller.getSession().inputs.empty() ? Page::Assign : Page::Tracks);
                         showToast ("Opened \"" + services.currentSessionName() + "\".");
                         updateChrome();
                     });
}

void MainView::chooseOutput()
{
    const auto outs = services.outputDevices();
    if (outs.isEmpty()) { showToast ("No output devices found."); return; }
    juce::PopupMenu m;
    // Ticked against the *broadcast*, not the joined device: picking one here chooses where
    // the stream goes, and the engineer's listen stays where it is.
    const juce::String current = services.broadcastOutputDevice();
    for (int i = 0; i < outs.size(); ++i)
    {
        if (outs[i].name.startsWith ("DLIVE Monitoring")) continue;   // machinery, not a choice
        m.addItem (i + 1, outs[i].name, true, outs[i].name == current);
    }

    m.addSeparator();
    m.addItem (900, "Set up outputs" + Glyph::ellip());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton).withMinimumWidth (outputButton.getWidth()),
                     [this, outs] (int result)
                     {
                         if (result == 900) { showOutputs(); return; }
                         if (result <= 0 || result > outs.size()) return;
                         const auto& name = outs[result - 1].name;
                         if (name == services.broadcastOutputDevice()) return;
                         const auto err = services.isAudioRunning() ? services.changeOutput (name)
                                                                    : services.openOutputOnly (name);
                         if (err.isNotEmpty()) showToast (err);
                         else { showToast ("Broadcast: " + name); updateChrome(); }
                     });
}

// ---------------------------------------------------------------- ticking
void MainView::timerCallback()
{
    controller.poll();
    transportBar->refresh();

    if (page == Page::Tracks) tracksPage->refresh();
    else if (page == Page::Mixer) mixerPage->refresh();
    else if (page == Page::Tune) mixPage->refresh();
    else if (page == Page::Live) livePage->refresh();
    else if (page == Page::Inspector) advancedPage->refresh();

    if (channelSheet != nullptr) channelSheet->refresh();
    if (chatSheet != nullptr) chatSheet->refresh();

    if (channelRail->isVisible())
    {
        // The list and the workspace are one selection seen twice: picking a strip on the
        // console lights the same row here, and the other way round. Both calls early-out
        // when nothing moved, so this costs a comparison.
        if (const int sel = selectedChannel(); sel >= 0) channelRail->setSelected (sel);
        channelRail->refresh();
    }

    // The status foot reads the engine, the disk and the broadcast. The disk is a syscall,
    // so it is asked once a second rather than thirty times; everything else is an atomic.
    const bool slow = (++slowTicks % 30) == 0;
    statusBar->update (slow);

    if (toastTicks > 0 && --toastTicks == 0) toast->setVisible (false);
    if (saveTicks > 0 && --saveTicks == 0) services.saveSession();

    if (tuningLiveWasOn != controller.isTuningLive()) { tuningLiveWasOn = controller.isTuningLive(); updateChrome(); }

    const bool running = services.isAudioRunning();
    if (audioWasRunning != running) updateChrome();
    if (audioWasRunning && ! running && services.deviceStopped())
        showToast ("The audio device stopped. Check its connection, then choose it again under Audio device.");
    audioWasRunning = running;

    // The strip along the top breathes while it is going out. One rectangle 2 px tall, and
    // only repainted while the value is actually moving.
    const bool live = running && controller.isPrepared() && ! controller.getSession().inputs.empty();
    const float want = ! live ? 0.0f
                              : 0.35f + 0.65f * (0.5f + 0.5f * std::sin (float (juce::Time::getMillisecondCounter())
                                                                        * 0.0024f));
    if (std::abs (want - onAir) > 0.02f || (onAir > 0.0f && want == 0.0f))
    {
        onAir = want;
        repaint (0, 0, getWidth(), Dine::Metric::onAir);
    }
}

// ---------------------------------------------------------------- layout
// The transport lives in the unified toolbar, between the session and the workspace
// tabs, so it is in the same place on every page and the foot of a workspace is free
// for the chain strip.
juce::Rectangle<int> MainView::contentBounds() const
{
    auto r = getLocalBounds().withTrimmedTop (Dine::Metric::toolbar)
                             .withTrimmedBottom (Dine::Metric::status);
    if (channelRail != nullptr && channelRail->isVisible()) r.removeFromLeft (channelRail->width());
    if (setupNav != nullptr && setupNav->isVisible()) r.removeFromLeft (Dine::Metric::setupNav);
    return r;
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    // ---- the strip along the very top: lit while what this Mac is doing reaches somebody
    // else. Recording is red, live is the lime. It is 2 px and it never moves the layout.
    if (onAir > 0.01f)
    {
        const bool recording = services.daw().isRecording();
        const auto tint = recording ? Dine::keyRec : Dine::accent;
        auto strip = getLocalBounds().removeFromTop (Dine::Metric::onAir).toFloat();
        juce::ColourGradient glow (tint.withAlpha (0.0f), strip.getX(), 0.0f,
                                   tint.withAlpha (0.0f), strip.getRight(), 0.0f, false);
        glow.addColour (0.18, tint.withAlpha (onAir));
        glow.addColour (0.82, tint.withAlpha (onAir));
        g.setGradientFill (glow);
        g.fillRect (strip);
    }

    // ---- toolbar
    Dine::drawChrome (g, getLocalBounds().removeFromTop (Dine::Metric::toolbar));

    if (tabs[0] != nullptr && tabs[0]->isVisible())
    {
        auto track = tabs[0]->getBounds();
        for (int i = 1; i < kWorkspaceTabs; ++i) track = track.getUnion (tabs[size_t (i)]->getBounds());
        Dine::drawSegmentTrack (g, track.expanded (3, 3));
    }
}

// ---------------------------------------------------------------- layout
void MainView::resized()
{
    // ---- toolbar. The right-hand cluster is placed first, then the left; the tabs are
    // centred in the window if they fit between the two, which they do at 1280 and up.
    auto bar = getLocalBounds().removeFromTop (Dine::Metric::toolbar).reduced (14, 0);
    const int centreX = getWidth() / 2;

    auto right = bar;
    if (chatButton->isVisible())
    {
        chatButton->setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, Dine::Metric::control));
        right.removeFromRight (8);
    }
    if (outputButton.isVisible())
    {
        const int w = juce::jlimit (110, 210, outputButton.idealWidth());
        outputButton.setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
        right.removeFromRight (8);
    }
    if (liveSafeButton->isVisible())
    {
        const int w = liveSafeButton->idealWidth();
        liveSafeButton->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
        right.removeFromRight (8);
    }
    if (bypassButton->isVisible())
    {
        const int w = bypassButton->idealWidth();
        bypassButton->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
    }

    auto left = bar.withRight (right.getRight());
    sidebarButton->setBounds (left.removeFromLeft (30).withSizeKeepingCentre (30, Dine::Metric::control));
    left.removeFromLeft (6);

    // The tabs: fixed widths, one row, never wrapped. They are the only navigation, so they
    // keep their width before anything else in the toolbar does.
    int tabWidths[kWorkspaceTabs], tabTotal = 0;
    for (int i = 0; i < kWorkspaceTabs; ++i)
    {
        tabWidths[i] = juce::jmax (62, tabs[size_t (i)]->idealWidth() + 6);
        tabTotal += tabWidths[i];
    }
    const int rightEdge = right.getRight() - 12;
    int tabX = juce::jlimit (left.getX(), juce::jmax (left.getX(), rightEdge - tabTotal), centreX - tabTotal / 2);
    {
        auto seg = juce::Rectangle<int> (tabX, 0, tabTotal, Dine::Metric::toolbar)
                       .withSizeKeepingCentre (tabTotal, Dine::Metric::control)
                       .withY ((Dine::Metric::toolbar - Dine::Metric::control) / 2);
        for (int i = 0; i < kWorkspaceTabs; ++i) tabs[size_t (i)]->setBounds (seg.removeFromLeft (tabWidths[i]));
    }

    // What is left of the left half belongs to the session name and the transport, and the
    // clock is the one readout a DAW is never allowed to truncate: the name yields to it.
    auto leftRoom = left.withRight (juce::jmax (left.getX(), tabX - 14));
    const int room = leftRoom.getWidth();
    const int tIdeal = transportBar->isVisible() ? transportBar->idealWidth() : 0;
    const int tMin   = transportBar->isVisible() ? transportBar->minimumWidth() : 0;

    // Who gives way, and in what order. The clock may never be truncated, so the transport
    // keeps its width first; but the document title disappearing altogether is its own kind
    // of wrong on a 1280 booth screen, so the name holds a floor of 96 px and the transport
    // drops to its own minimum before that floor is touched.
    static constexpr int kSessionFloor = 96;
    const int tKeys  = transportBar->isVisible() ? transportBar->keysOnlyWidth() : 0;
    const int floorW = juce::jmin (sessionButton->idealWidth(), kSessionFloor);
    int sessionW = 0, transportW = 0;
    if (room >= tMin + 16 + floorW)
    {
        // Room for both: the transport takes what it wants, the name has the rest.
        transportW = juce::jmin (tIdeal, room - 16 - floorW);
        sessionW = juce::jlimit (floorW, sessionButton->idealWidth(), room - transportW - 16);
    }
    else if (room >= tMin + 16)
    {
        // The clock survives; the name gives what it has to. It is still in the popover,
        // the window title and the File menu - the timecode is nowhere else.
        transportW = tMin;
        sessionW = juce::jmax (0, room - tMin - 16);
    }
    else
    {
        // Narrower than both: the transport drops its clock (the ruler and the status foot
        // still carry the time) rather than the document losing its name altogether.
        transportW = juce::jmin (tKeys, room);
        sessionW = juce::jlimit (0, sessionButton->idealWidth(), room - transportW - 16);
    }
    sessionButton->setBounds (leftRoom.removeFromLeft (sessionW).withSizeKeepingCentre (sessionW, 38));
    if (transportW > 0)
    {
        leftRoom.removeFromLeft (juce::jmin (10, leftRoom.getWidth()));
        // One width, worked out before the rectangle is cut: `r.removeFromLeft (w)` mutates
        // `r`, so reading `r.getWidth()` again in the same expression is a coin toss.
        const int w = juce::jmin (transportW, leftRoom.getWidth());
        transportBar->setBounds (leftRoom.removeFromLeft (w).withSizeKeepingCentre (w, TransportBar::height));
    }

    // ---- the body: the channel list (or SETUP's steps) and the workspace, over the status foot.
    auto body = getLocalBounds().withTrimmedTop (Dine::Metric::toolbar);
    statusBar->setBounds (body.removeFromBottom (Dine::Metric::status));
    if (channelRail->isVisible()) channelRail->setBounds (body.removeFromLeft (channelRail->width()));
    if (setupNav->isVisible()) setupNav->setBounds (body.removeFromLeft (Dine::Metric::setupNav));

    auto content = body;
    for (juce::Component* p : { (juce::Component*) sessionsPage.get(), (juce::Component*) devicePage.get(),
                                (juce::Component*) assignPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (content);

    // A sheet covers the workspace and the channel list with it: it is a decision about the
    // whole session, and half a console showing behind it reads as still editable.
    auto sheetArea = getLocalBounds().withTrimmedTop (Dine::Metric::toolbar)
                                     .withTrimmedBottom (Dine::Metric::status);
    for (juce::Component* sheet : { (juce::Component*) outputsSheet.get(), (juce::Component*) channelSheet.get(),
                                    (juce::Component*) chatSheet.get() })
        if (sheet != nullptr) { sheet->setBounds (sheetArea); sheet->toFront (false); }

    if (tutorial != nullptr) { tutorial->setBounds (getLocalBounds()); tutorial->toFront (false); }

    if (toast->isVisible())
    {
        const int w = toast->idealWidth();
        // clear of the chain strip along the foot of a workspace
        toast->setBounds (content.getCentreX() - w / 2,
                          content.getBottom() - 22 - 30 - (page == Page::Tracks || page == Page::Mixer ? ChainStrip::height : 0),
                          w, 30);
        toast->toFront (false);
    }
}

} // namespace livemix
