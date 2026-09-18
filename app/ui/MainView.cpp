#include "MainView.h"
#include "native/InputMapStore.h"
#include "native/MultitrackImport.h"
#include "native/OpenAiMixProvider.h"

namespace livemix
{

// Nothing opens by itself in the headless snapshot tool (MainView::setAutoTutorial), and it
// renders from the design rather than from this Mac's chosen theme (setStoredThemeUsed).
namespace { bool gAutoTutorial = true; bool gUseStoredTheme = true; }

// ---------------------------------------------------------------- toast
// A sentence at the foot of the workspace, never a banner: nothing in the layout moves. A
// refusal (LIVE SAFE said no, something could not be done) is amber on its own ground.
class MainView::Toast : public juce::Component
{
public:
    void show (const juce::String& t)
    {
        text = t;
        const auto lower = t.toLowerCase();
        refuse = lower.contains ("could not") || lower.contains ("cannot") || lower.contains ("is locked")
              || lower.contains ("blocked") || lower.contains ("nowhere") || lower.contains ("no earlier")
              || lower.contains ("first") || lower.contains ("stopped") || lower.contains ("is not ");
        setVisible (true);
        repaint();
    }
    int idealWidth() const
    {
        return juce::jmin (640, Dine::textWidth (Dine::text (13.0f), text) + 38);
    }
    int idealHeight() const
    {
        const int w = idealWidth() - 36;
        juce::AttributedString s; s.setText (text); s.setFont (Dine::text (13.0f));
        juce::TextLayout l; l.createLayout (s, float (juce::jmax (80, w)));
        return juce::jlimit (44, 120, int (l.getHeight()) + 28);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 18 }).drawForRectangle (g, getLocalBounds());
        Dine::fillRounded (g, r, refuse ? Dine::refuse : Dine::control, Dine::Radius::card);
        g.setColour (refuse ? Dine::warn : Dine::ink);
        g.setFont (Dine::text (13.0f));
        g.drawFittedText (text, getLocalBounds().reduced (18, 10), juce::Justification::centredLeft, 4, 1.0f);
    }
private:
    juce::String text;
    bool refuse = false;
};

// ---------------------------------------------------------------- session button
// The document title in the middle of the title row: the session's name and a chevron.
class MainView::SessionButton : public juce::Button
{
public:
    SessionButton() : juce::Button ("session") { setWantsKeyboardFocus (false); }
    void set (const juce::String& n)
    {
        if (n == name) return;
        name = n; repaint();
    }
    int idealWidth() const
    {
        return juce::jmin (420, Dine::textWidth (Dine::text (14.0f, 500), name) + 20 + 12 + 10);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (over || down) Dine::fillRounded (g, getLocalBounds().toFloat(), Dine::card, Dine::Radius::chip);
        auto r = getLocalBounds().reduced (10, 0);
        const int w = juce::jmin (r.getWidth() - 16, Dine::textWidth (Dine::text (14.0f, 500), name));
        auto block = r.withSizeKeepingCentre (w + 16, r.getHeight());
        g.setColour (Dine::ink);
        g.setFont (Dine::text (14.0f, 500));
        g.drawText (name, block.removeFromLeft (w), juce::Justification::centredLeft, true);
        block.removeFromLeft (7);
        Dine::drawDropChevron (g, block.toFloat(), Dine::ink);
    }
private:
    juce::String name;
};

// ---------------------------------------------------------------- toolbar toggle
// BYPASS, LIVE SAFE ON / OFF and MIX BUDDY: the accent when on, the control plane when
// not, tracked caps either way.
class MainView::ToolbarToggle : public juce::Button
{
public:
    ToolbarToggle (const juce::String& text, float px = 12.0f, float tracking = 0.04f)
        : juce::Button (text), fontPx (px), track (tracking)
    {
        setClickingTogglesState (false);
        setWantsKeyboardFocus (false);
    }

    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    bool isOn() const noexcept { return on; }
    void setSuffix (const juce::String& s) { if (s != suffix) { suffix = s; repaint(); } }
    juce::String label() const { return suffix.isEmpty() ? getButtonText() : getButtonText() + " " + suffix; }

    int idealWidth() const { return Dine::textWidth (Dine::caps (fontPx, track), label()) + 24; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (on) Dine::fillRounded (g, r, down ? Dine::accentDeep : over ? Dine::accentHover : Dine::accent, Dine::Radius::control);
        else    Dine::fillRounded (g, r, down ? Dine::controlOn : over ? Dine::controlHot : Dine::control, Dine::Radius::control);
        g.setColour (on ? Dine::onAccent : over ? Dine::ink : Dine::ink2);
        g.setFont (Dine::caps (fontPx, track));
        g.drawText (label(), getLocalBounds(), juce::Justification::centred);
    }

private:
    float fontPx;
    float track;
    juce::String suffix;
    bool on = false;
};

// The sidebar switch: 26 x 22, on the control plane, with the two-pane glyph.
class MainView::SidebarButton : public juce::Button
{
public:
    SidebarButton() : juce::Button ("Sidebar") { setWantsKeyboardFocus (false); }
    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, down ? Dine::controlOn : over ? Dine::controlHot : Dine::control, Dine::Radius::chip);
        auto glyph = r.withSizeKeepingCentre (11.0f, 9.0f);
        g.setColour (on ? Dine::ink2 : Dine::ink3);
        g.fillRoundedRectangle (glyph.removeFromLeft (3.0f), 1.0f);
        glyph.removeFromLeft (2.0f);
        g.setColour (Dine::controlOn);
        g.fillRoundedRectangle (glyph, 1.0f);
    }
private:
    bool on = true;
};

// A workspace tab: tracked caps with a 2 px accent underline when it is the one on screen.
class MainView::WorkspaceTab : public juce::Button
{
public:
    explicit WorkspaceTab (const juce::String& text) : juce::Button (text) { setWantsKeyboardFocus (false); setClickingTogglesState (false); }
    int idealWidth() const { return Dine::textWidth (Dine::caps (12.5f, 0.08f), getButtonText()) + 4; }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds();
        g.setColour (! isEnabled() ? Dine::ink4 : on || over ? Dine::ink : Dine::ink3);
        g.setFont (Dine::caps (12.5f, 0.08f));
        g.drawText (getButtonText(), r.withTrimmedBottom (2), juce::Justification::centred);
        if (on)
        {
            g.setColour (Dine::accent);
            g.fillRect (r.removeFromBottom (2));
        }
    }
};

// ---------------------------------------------------------------- status foot
// Always on screen: the engine, the CPU, the disk, what is recording, the broadcast, what
// the engineer is listening on, and dropped buffers. Painted, compared before a repaint.
class MainView::StatusBar : public juce::Component
{
public:
    StatusBar (MixController& c, AppServices& s) : controller (c), services (s) { setOpaque (true); }

    void update (bool slow)
    {
        Look next;
        const bool running = services.isAudioRunning();
        const bool lost = ! running && services.deviceStopped();
        next.engine = lost ? "Device lost" : ! running ? "Not running" : controller.isBypassed() ? "Bypassed" : "Stable";
        next.engineTint = lost ? Dine::crit : ! running ? Dine::ink3 : controller.isBypassed() ? Dine::warn : Dine::ink;
        const double cpu = services.cpuLoad();
        next.cpu = cpu < 0.0 ? juce::String (Glyph::dash()) : juce::String (juce::roundToInt (cpu * 100.0)) + "%";
        next.cpuTint = cpu > 0.8 ? Dine::warn : Dine::ink;
        next.drops = services.xrunCount();

        const auto& daw = services.daw();
        const auto& project = daw.getProject();
        int armed = 0;
        for (const auto& t : project.tracks) if (t.armed) ++armed;
        next.recording = daw.isRecording();
        next.rec = next.recording ? juce::String (armed) + " to record" : juce::String ("stopped");
        next.recTint = next.recording ? Dine::crit : Dine::ink3;

        if (slow || diskText.isEmpty())
        {
            const double seconds = daw.getRecordingSecondsFree();
            if (seconds <= 0.0) diskText = Glyph::dash();
            else
            {
                const int total = int (seconds);
                diskText = total >= 24 * 3600 ? "a day+" : total >= 3600 ? juce::String (total / 3600) + " h " + juce::String ((total / 60) % 60) + " m"
                                                                         : juce::String (juce::jmax (0, total / 60)) + " m";
            }
            diskLow = seconds > 0.0 && seconds < 15.0 * 60.0;
        }
        next.disk = diskText;
        next.diskTint = diskLow ? Dine::warn : Dine::ink;

        const auto loud = controller.getMasterLoudness();
        next.loudness = ! loud.known || loud.integratedLufs <= -100.0f ? juce::String (Glyph::dash()) + " LUFS"
                                                                       : juce::String (loud.integratedLufs, 1) + " LUFS";
        next.loudTint = ! loud.known || loud.integratedLufs <= -100.0f ? Dine::ink3 : loud.onTarget() ? Dine::ink : Dine::warn;

        const auto& mon = controller.getMonitor();
        next.monitor = juce::String (mon.mode == SoloMode::InPlace ? "IN PLACE" : "SOLO") + " " + Glyph::dot() + " "
                     + (mon.point == SoloPoint::PFL ? "PFL" : "AFL");
        next.monitorTint = controller.hasMonitorOutput() ? Dine::ink : Dine::ink3;
        next.safe = project.liveSafe;

        if (next != look) { look = next; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        Dine::drawStatusBand (g, getLocalBounds());
        auto r = getLocalBounds().reduced (10, 0);
        cell (g, r, "Engine", look.engine, look.engineTint);
        cell (g, r, "CPU", look.cpu, look.cpuTint);
        cell (g, r, "Disk", look.disk, look.diskTint);
        cell (g, r, "Recording", look.rec, look.recTint);
        cell (g, r, "Broadcast", look.loudness, look.loudTint);
        cell (g, r, "Monitor", look.monitor, look.monitorTint);
        cell (g, r, "Dropped", juce::String (look.drops), look.drops > 0 ? Dine::warn : Dine::ink3);
        if (look.safe) cell (g, r, "Live safe", "ON", Dine::accent);
    }

private:
    static void cell (juce::Graphics& g, juce::Rectangle<int>& r, const juce::String& label,
                      const juce::String& value, juce::Colour ink)
    {
        const auto labelFont = Dine::caps (11.0f, 0.08f);
        const auto valueFont = Dine::mono (12.0f, 500);
        const int w = 14 + Dine::textWidth (labelFont, label.toUpperCase()) + 8 + Dine::textWidth (valueFont, value) + 14;
        if (w > r.getWidth()) return;
        auto area = r.removeFromLeft (w).reduced (14, 0);
        g.setColour (Dine::ink3);
        g.setFont (labelFont);
        g.drawText (label.toUpperCase(), area.removeFromLeft (Dine::textWidth (labelFont, label.toUpperCase())), juce::Justification::centredLeft);
        area.removeFromLeft (8);
        g.setColour (ink);
        g.setFont (valueFont);
        g.drawText (value, area, juce::Justification::centredLeft);
    }

    struct Look
    {
        juce::String engine, cpu, disk, rec, loudness, monitor;
        juce::Colour engineTint, cpuTint, diskTint, recTint, loudTint, monitorTint;
        int drops = 0;
        bool recording = false, safe = false;
        bool operator== (const Look& o) const
        {
            return engine == o.engine && cpu == o.cpu && disk == o.disk && rec == o.rec && loudness == o.loudness
                && monitor == o.monitor && engineTint == o.engineTint && cpuTint == o.cpuTint && diskTint == o.diskTint
                && recTint == o.recTint && loudTint == o.loudTint && monitorTint == o.monitorTint
                && drops == o.drops && recording == o.recording && safe == o.safe;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };

    MixController& controller;
    AppServices& services;
    Look look;
    juce::String diskText;
    bool diskLow = false;
};

// ---------------------------------------------------------------- sidebar
// LIBRARY / SET-UP / WORKSPACE, each a caption over its rows, the wordmark at the top and
// the device along the foot. It is 184 px and folds to a 17 px handle with its name down it.
class MainView::Sidebar : public juce::Component
{
public:
    Sidebar (AppServices& s, std::function<void (Page)> go) : services (s)
    {
        struct Def { const char* label; Page page; };
        const Def defs[9] = { { "Sessions", Page::Sessions },
                              { "Audio device", Page::Device }, { "Inputs", Page::Assign }, { "Purpose and sound", Page::Purpose },
                              { "Tracks", Page::Tracks }, { "Mixer", Page::Mixer }, { "Tune", Page::Tune }, { "Live", Page::Live },
                              { "Inspector", Page::Inspector } };
        for (int i = 0; i < 9; ++i)
        {
            items[size_t (i)] = std::make_unique<DineNavItem> (defs[i].label);
            items[size_t (i)]->onClick = [go, p = defs[i].page] { go (p); };
            addAndMakeVisible (*items[size_t (i)]);
        }
        handle = std::make_unique<DinePanelTab> (DinePanelTab::Side::Left, "Sidebar");
        handle->setCollapsed (true);
        addChildComponent (*handle);
        setOpaque (true);
    }

    DineNavItem& item (Page p) { return *items[size_t (indexOf (p))]; }
    DinePanelTab& getHandle() { return *handle; }

    void setCollapsed (bool c)
    {
        if (c == collapsed) return;
        collapsed = c;
        for (auto& i : items) i->setVisible (! c);
        handle->setVisible (c);
        resized();
        repaint();
    }
    bool isCollapsed() const noexcept { return collapsed; }
    int width() const noexcept { return collapsed ? Dine::Metric::railHandle : Dine::Metric::sidebar; }

    // The device along the foot. Compared before a repaint.
    void refresh (bool recording)
    {
        const bool running = services.isAudioRunning();
        juce::String state = recording ? "Recording" : running ? "Running" : "Not running";
        // The input device, or the output one when the engine is running on outputs alone (playback into a monitor).
        juce::String name = ! running ? juce::String ("No audio device")
                          : services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                          : services.currentOutputDevice().isNotEmpty() ? services.currentOutputDevice() : juce::String ("No audio device");
        juce::String spec = running ? juce::String (services.sampleRate() / 1000.0, 0) + " kHz " + Glyph::dot() + " "
                                          + juce::String (services.bufferSize()) + " " + Glyph::dot() + " "
                                          + juce::String (1000.0 * services.bufferSize() / juce::jmax (1.0, services.sampleRate()), 1) + " ms"
                                    : juce::String();
        const int xruns = services.xrunCount();
        if (state == footState && name == footName && spec == footSpec && xruns == footXruns && recording == footRecording) return;
        footState = state; footName = name; footSpec = spec; footXruns = xruns; footRecording = recording;
        repaint (getLocalBounds().removeFromBottom (kFootH));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (Dine::sidebar);
        g.fillRect (r);
        if (collapsed) return;
        r.removeFromTop (kHeadH);   // the wordmark lives in the title row now, where it is always on screen

        // the captions over each group of rows
        for (const auto& c : captions)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::caps (9.5f, 0.12f));
            g.drawText (c.second, c.first, juce::Justification::bottomLeft);
        }

        // the device
        auto foot = getLocalBounds().removeFromBottom (kFootH).reduced (14, 0).withTrimmedTop (12);
        const bool running = services.isAudioRunning();
        auto line = foot.removeFromTop (16);
        g.setColour (footRecording ? Dine::crit : running ? Dine::ok : Dine::ink4);
        g.fillEllipse (line.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
        line.removeFromLeft (7);
        g.setFont (Dine::text (11.5f, 500));
        g.drawText (footState, line, juce::Justification::centredLeft, true);
        foot.removeFromTop (3);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.5f));
        g.drawText (footName, foot.removeFromTop (16), juce::Justification::centredLeft, true);
        foot.removeFromTop (2);
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.0f));
        g.drawText (footSpec, foot.removeFromTop (14), juce::Justification::centredLeft, true);
        if (footXruns > 0)
        {
            foot.removeFromTop (3);
            g.setColour (Dine::warn);
            g.setFont (Dine::text (11.0f));
            g.drawText (juce::String (footXruns) + " dropped buffers", foot.removeFromTop (14), juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (collapsed) { handle->setBounds (r); return; }
        r.removeFromTop (kHeadH);
        r.removeFromBottom (kFootH);
        auto list = r.reduced (10, 0);
        captions.clear();
        auto caption = [&] (const char* text, bool first)
        {
            auto c = list.removeFromTop (first ? 22 : 30).withTrimmedBottom (5);
            captions.push_back ({ c.withTrimmedLeft (0).translated (0, 0).withX (c.getX() + 10).withWidth (c.getWidth() - 10), text });
        };
        auto rows = [&] (int from, int to)
        {
            for (int i = from; i < to; ++i) { items[size_t (i)]->setBounds (list.removeFromTop (26)); list.removeFromTop (1); }
        };
        caption ("LIBRARY", true);   rows (0, 1);
        caption ("SET-UP", false);   rows (1, 4);
        caption ("WORKSPACE", false); rows (4, 9);
    }

private:
    static constexpr int kHeadH = 14, kFootH = 96;
    static int indexOf (Page p) noexcept
    {
        switch (p)
        {
            case Page::Sessions: return 0; case Page::Device: return 1; case Page::Assign: return 2; case Page::Purpose: return 3;
            case Page::Tracks: return 4; case Page::Mixer: return 5; case Page::Tune: return 6; case Page::Live: return 7;
            case Page::Inspector: return 8;
        }
        return 0;
    }

    AppServices& services;
    std::array<std::unique_ptr<DineNavItem>, 9> items;
    std::unique_ptr<DinePanelTab> handle;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
    juce::String footState, footName, footSpec;
    int footXruns = 0;
    bool footRecording = false, collapsed = false;
};

// ---------------------------------------------------------------- mixer window
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
    void closeButtonPressed() override { view.closeMixerWindow(); }

private:
    void timerCallback() override { page->refresh(); }

    MainView& view;
    std::unique_ptr<MixerPage> page;
};

// ---------------------------------------------------------------- menu bar
// File / Edit / Track / Mix / Transport / View / Help. The shortcuts printed here are the
// ones MainView::keyPressed acts on, so the menu and the keyboard never disagree.
class MainView::Menu : public juce::MenuBarModel
{
public:
    explicit Menu (MainView& v) : view (v) {}

    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "Track", "Mix", "Transport", "View", "Help" };
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
                m.addItem (407, view.controller.hasReference() ? "MATCH TO REFERENCE" : "MATCH TO REFERENCE...",
                           view.controller.hasReference());
                m.addItem (408, view.controller.hasReference()
                                    ? "Reference: " + juce::String (view.controller.getReference().name) + juce::String (Glyph::ellip())
                                    : "Add a Reference Mix...");
                m.addSeparator();
                m.addItem (409, "Mix Buddy" + juce::String (Glyph::ellip()));
                m.addItem (410, "Try Another Mix", view.controller.canTryAnotherMix());
                m.addSeparator();
                m.addItem (411, view.controller.canUndoMix()
                                    ? "Undo " + juce::String (view.controller.undoMixLabel())
                                    : juce::String ("Undo Mix"),
                           view.controller.canUndoMix());
                m.addItem (412, view.controller.canRedoMix()
                                    ? "Redo " + juce::String (view.controller.redoMixLabel())
                                    : juce::String ("Redo Mix"),
                           view.controller.canRedoMix());
                m.addSeparator();
                {
                    const auto move = view.controller.previewLoudnessMove();
                    m.addItem (420, move.possible ? "Raise Loudness to Target   (" + juce::String (move.moveDb > 0 ? "+" : "") + juce::String (move.moveDb, 1) + " dB)"
                                                  : juce::String ("Raise Loudness to Target"),
                               move.possible && ! view.controller.isLiveSafe());
                    juce::PopupMenu target;
                    const auto current = view.controller.getDelivery();
                    for (int i = 0; i < int (DeliveryLoudness::Count); ++i)
                    {
                        const auto d = DeliveryLoudness (i);
                        target.addItem (430 + i, d == DeliveryLoudness::FromPurpose ? juce::String (deliveryLoudnessName (d))
                                                                                    : juce::String (deliveryLoudnessName (d)) + "   " + juce::String (deliveryLoudnessLufs (d), 0) + " LUFS",
                                        true, current == d);
                    }
                    m.addSubMenu ("Loudness Target", target);
                    juce::PopupMenu sound;
                    for (int i = 0; i < int (MasterVoicing::Count); ++i)
                        sound.addItem (450 + i, masterVoicingName (MasterVoicing (i)), true, view.controller.getVoicing() == MasterVoicing (i));
                    m.addSubMenu ("Master Sound", sound);
                }
                m.addSeparator();
                m.addItem (401, "Reset Macros");
                m.addItem (402, view.controller.numSoloed() > 0
                                    ? "Clear Solo (" + juce::String (view.controller.numSoloed()) + ")"
                                    : juce::String ("Clear Solo"),
                           view.controller.numSoloed() > 0);
                m.addSeparator();
                m.addItem (403, "Bypass: Hear the Inputs   B", true, view.controller.isBypassed());
                m.addSeparator();
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
                m.addItem (600, "Tracks");
                m.addItem (601, "Mixer");
                m.addItem (602, "Tune");
                m.addItem (603, "Live");
                m.addItem (604, "Inspector");
                m.addSeparator();
                m.addItem (613, "Setup");
                m.addSeparator();
                m.addItem (608, "Open Mixer in a New Window");
                m.addItem (609, "Outputs" + juce::String (Glyph::ellip()));
                m.addSeparator();
                m.addItem (610, (view.sidebarShown ? "Hide Sidebar" : "Show Sidebar") + juce::String ("   ")
                                    + juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\x83\xe2\x8c\x98")) + "S");
                m.addItem (615, "Show/Hide the two side panels");
                m.addSeparator();
                {
                    // Every theme, DLIVE's own then yours, the chosen one ticked; then the sheet.
                    juce::PopupMenu appearance;
                    view.themeMenuNames.clear();
                    bool mine = false;
                    for (const auto& t : ThemeStore::all())
                    {
                        if (! t.builtIn && ! mine) { mine = true; appearance.addSeparator(); }
                        appearance.addItem (640 + view.themeMenuNames.size(), t.name, true, t.name.equalsIgnoreCase (Dine::currentThemeName()));
                        view.themeMenuNames.add (t.name);
                    }
                    appearance.addSeparator();
                    appearance.addItem (620, "Customise Appearance" + juce::String (Glyph::ellip()));
                    appearance.addItem (621, "Import a Theme" + juce::String (Glyph::ellip()));
                    appearance.addItem (622, "Show Themes Folder");
                    m.addSubMenu ("Appearance", appearance);
                }
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
    // The theme first, before a single page reads a token.
    if (gUseStoredTheme) Dine::applyTheme (ThemeStore::find (ThemeStore::chosenTheme()));
    lookAndFeel.applyPalette();
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

    // The chain along the foot belongs to the window now: one strip under every workspace.
    tracksPage->setFootShown (false);
    mixerPage->setFootShown (false);
    chainFoot = std::make_unique<ChainStrip>();
    chainFoot->setEmpty ("Click a strip, a track header or a channel to read its chain here. Click the chain to open the Inspector.");
    chainFoot->onOpen = [this]
    {
        const int strip = selectedChannel() >= 0 ? selectedChannel() : lastChannel;
        if (strip >= 0) { showPage (Page::Inspector); advancedPage->select (strip); }
        else if (page == Page::Mixer && mixerPage->selectedStrip() < 0) showPage (Page::Inspector);
    };
    addChildComponent (*chainFoot);

    // ---- the sidebar
    sidebar = std::make_unique<Sidebar> (services, [this] (Page p)
    {
        if (p == Page::Assign) assignPage->refresh();
        showPage (p);
    });
    sidebar->getHandle().onClick = [this] { setSidebarShown (true); };
    sidebar->item (Page::Sessions).setTooltip ("The library: every saved session, and what each one was for.");
    addAndMakeVisible (*sidebar);

    statusBar = std::make_unique<StatusBar> (controller, services);
    addAndMakeVisible (*statusBar);

    // ---- title row
    addAndMakeVisible (*sessionButton);
    sessionButton->onClick = [this] { setupPopover(); };

    sidebarButton = std::make_unique<SidebarButton>();
    sidebarButton->setTooltip ("Show or hide the sidebar (Ctrl-Cmd-S)");
    sidebarButton->onClick = [this] { setSidebarShown (! sidebarShown); };
    addAndMakeVisible (*sidebarButton);

    chatButton = std::make_unique<ToolbarToggle> ("MIX BUDDY", 11.0f, 0.06f);
    chatButton->setTooltip ("Open or close Mix Buddy, DLIVE's mix engineer in plain words: ask for a change to the mix - "
                            "a source, a level, a tone. It proposes; you keep.");
    chatButton->onClick = [this] { if (chatSheet != nullptr) closeSheets(); else showChat(); };
    addChildComponent (*chatButton);

    // ---- toolbar
    const char* tabLabels[kWorkspaceTabs] = { "TRACKS", "MIXER", "TUNE", "LIVE", "INSPECTOR" };
    const Page tabPages[kWorkspaceTabs] = { Page::Tracks, Page::Mixer, Page::Tune, Page::Live, Page::Inspector };
    for (int i = 0; i < kWorkspaceTabs; ++i)
    {
        tabs[size_t (i)] = std::make_unique<WorkspaceTab> (tabLabels[i]);
        tabs[size_t (i)]->setTooltip ("Switching workspaces never changes the sound");
        tabs[size_t (i)]->onClick = [this, p = tabPages[i]] { showPage (p); };
        addAndMakeVisible (*tabs[size_t (i)]);
    }

    bypassButton = std::make_unique<ToolbarToggle> ("BYPASS");
    bypassButton->setTooltip ("Hear the raw console feed: no processing, no fader moves, no effects. Press it again "
                              "for your mix. Nothing is changed either way (B).");
    bypassButton->onClick = [this] { setBypass (! controller.isBypassed()); };
    addChildComponent (*bypassButton);

    liveSafeButton = std::make_unique<ToolbarToggle> ("LIVE SAFE");
    liveSafeButton->setTooltip ("Locks the sound: re-routes and re-tunes are blocked, and a fader cannot move more "
                                "than 6 dB at a time. Mute, solo, the monitor and the recording always stay free.");
    liveSafeButton->onClick = [this] { handleCommand (614); };
    addChildComponent (*liveSafeButton);

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
    assignPage->onSaveMapping = [this] { saveInputMapping(); };
    assignPage->onApplyMapping = [this] { openInputMappings(); };
    purposePage->onBack = [this] { showPage (Page::Assign); };
    purposePage->onContinue = [this] { enterSession(); };

    tracksPage->onToast = [this] (const juce::String& t) { showToast (t); };
    tracksPage->onPanelWidthChanged = [this]
    {
        services.setTrackPanelWidth (tracksPage->panelWidth());
        services.saveSession();
    };
    tracksPage->onTimelineChanged = [this] { updateChrome(); };
    tracksPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    tracksPage->onOpenAssign = [this] { assignPage->refresh(); showPage (Page::Assign); };
    tracksPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
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
    mixPage->onOpenChat = [this] { showChat(); };
    mixPage->onSelectStrip = [this] (int strip) { lastChannel = strip; updateChainFoot(); };
    mixerPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    mixerPage->onOpenBus = [this] (MixBus bus) { showPage (Page::Inspector); advancedPage->selectBus (bus); };
    mixerPage->onTuneStrip = [this] (int strip) { tuneChannel (strip); };
    mixerPage->onOpenWindow = [this] { openMixerWindow(); };
    mixerPage->onToast = [this] (const juce::String& t) { showToast (t); };
    mixerPage->onOpenAssign = [this] { assignPage->refresh(); showPage (Page::Assign); };
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

    if (gAutoTutorial && ! Tutorial::hasBeenSeen() && services.listSessions().isEmpty()
        && controller.getSession().inputs.empty())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainView> (this)]
                                         { if (safe != nullptr) safe->showTutorial(); });
}

MainView::~MainView()
{
    stopTimer();
    mixerWindow.reset();
    outputsSheet.reset();
    themeSheet.reset();
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

bool MainView::liveSafeBlocks (const juce::String& what)
{
    if (! services.daw().getProject().liveSafe) return false;
    showToast ("LIVE SAFE is on, so " + what + " is blocked: it would change what is on air. "
               + juce::String (liveSafe::allowedSummary()) + " Turn LIVE SAFE off first.");
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

    updateChrome();
    resized();
    repaint();
    grabKeyboardFocus();
}

void MainView::updateChrome()
{
    const auto& session = controller.getSession();
    const bool running = services.isAudioRunning();
    const bool hasInputs = ! session.inputs.empty();
    const bool mixable = controller.isPrepared() && hasInputs;
    const bool inWorkspace = ! isSetupPage (page);
    const auto& project = services.daw().getProject();

    // ---- the sidebar's rows
    const Page all[9] = { Page::Sessions, Page::Device, Page::Assign, Page::Purpose,
                          Page::Tracks, Page::Mixer, Page::Tune, Page::Live, Page::Inspector };
    for (const Page p : all)
    {
        auto& row = sidebar->item (p);
        row.setSelected (page == p);
        const bool setup = isSetupPage (p);
        row.setEnabled (setup ? (p == Page::Sessions || p == Page::Device || running || hasInputs) : mixable);
    }
    sidebar->item (Page::Sessions).setMeta (juce::String (services.listSessions().size()));
    sidebar->item (Page::Assign).setMeta (hasInputs ? juce::String (int (session.inputs.size())) : juce::String());
    sidebar->item (Page::Device).setDone (running && page != Page::Device);
    sidebar->item (Page::Purpose).setDone (mixable && page != Page::Purpose);

    // ---- the tabs
    const Page tabPages[kWorkspaceTabs] = { Page::Tracks, Page::Mixer, Page::Tune, Page::Live, Page::Inspector };
    for (int i = 0; i < kWorkspaceTabs; ++i)
    {
        tabs[size_t (i)]->setToggleState (page == tabPages[i], juce::dontSendNotification);
        tabs[size_t (i)]->setEnabled (mixable);
    }

    outputButton.setVisible (running || inWorkspace);
    bypassButton->setVisible (inWorkspace && mixable);
    bypassButton->setOn (controller.isBypassed());
    liveSafeButton->setVisible (inWorkspace && mixable);
    liveSafeButton->setOn (project.liveSafe);
    liveSafeButton->setSuffix (project.liveSafe ? "ON" : "OFF");
    chatButton->setVisible (mixable);
    chatButton->setOn (chatSheet != nullptr);
    transportBar->setVisible (inWorkspace);
    chainFoot->setVisible (inWorkspace && mixable);

    juce::String name = services.currentSessionName();
    if (name.isEmpty()) name = "Untitled";
    sessionButton->set (name);

    const juce::String out = services.outputDisplayName();
    outputButton.setValue (out.isEmpty() ? "No output" : out);
    updateChainFoot();
}

// The chain along the foot reads the channel that was picked out last, wherever that was:
// the console, a track header, the Inspector, TUNE's rail.
void MainView::updateChainFoot()
{
    if (! chainFoot->isVisible()) return;
    if (! controller.isPrepared()) { chainFoot->setEmpty ("Set the device up first."); return; }

    const auto& state = controller.getBase();
    const auto& graph = controller.getGraph();
    int strip = selectedChannel();
    MixBus bus = MixBus::Count;
    if (page == Page::Mixer && strip < 0 && mixerPage->selectedBus() != MixBus::Count) bus = mixerPage->selectedBus();
    if (page == Page::Inspector && strip < 0) bus = advancedPage->selectedBus();
    if (strip >= 0) lastChannel = strip;
    if (strip < 0 && bus == MixBus::Count) strip = lastChannel;

    if (strip >= 0 && strip < state.numStrips && strip < graph.numStrips())
    {
        const auto& r = graph.strips[size_t (strip)];
        chainFoot->setSource (juce::String (r.name), Dine::busTint (r.bus), state.strips[size_t (strip)].channel,
                              false, r.inputB >= 0);
    }
    else if (bus != MixBus::Count)
    {
        const juce::String n = bus == MixBus::Master ? juce::String ("Master") : juce::String (mixBusName (bus));
        chainFoot->setSource (n.substring (0, 1) + n.substring (1).toLowerCase(), Dine::busTint (bus),
                              state.buses[size_t (bus)].channel, bus == MixBus::Master, true);
    }
    else chainFoot->setEmpty ("Click a strip, a track header or a channel to read its chain here. Click the chain to open the Inspector.");

    juce::String note;
    if (controller.isBypassed()) note = "BYPASS";
    chainFoot->setNote (note);
}

// The session button's popover: the four setup steps with what each is set to, and the
// ways out - the setup pages, the library, saving.
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
    m.addItem (11, "New session");
    m.addItem (6, "Open session" + juce::String (Glyph::ellip()));
    m.addItem (7, "Save");
    m.addItem (8, "Save as" + juce::String (Glyph::ellip()));
    m.addSeparator();
    m.addItem (12, "Import multitrack folder" + juce::String (Glyph::ellip()));
    m.addItem (13, "Add a reference mix" + juce::String (Glyph::ellip()));
    m.addItem (14, "Save input mapping" + juce::String (Glyph::ellip()), hasInputs);
    m.addItem (15, "Export stereo mix (WAV)" + juce::String (Glyph::ellip()));
    m.addSeparator();
    m.addItem (5, "Open setup");
    m.addItem (9, "Rename or fix the inputs" + juce::String (Glyph::ellip()));
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
                             case 11: newSession(); break;
                             case 12: importMultitrack(); break;
                             case 13: handleCommand (107); break;
                             case 14: saveInputMapping(); break;
                             case 15: exportMix (AppServices::ExportFormat::Wav); break;
                             default: break;
                         }
                     });
}

juce::Rectangle<int> MainView::spotlight (const juce::String& what) const
{
    if (what == "session" && sessionButton != nullptr) return sessionButton->getBounds();
    if (what == "transport" && transportBar != nullptr && transportBar->isVisible()) return transportBar->getBounds();
    if (what == "livesafe" && liveSafeButton != nullptr && liveSafeButton->isVisible()) return liveSafeButton->getBounds();
    if (what == "rail" && sidebar != nullptr && sidebar->isVisible()) return sidebar->getBounds();
    if (what == "tabs" && tabs[0] != nullptr)
    {
        auto track = tabs[0]->getBounds();
        for (int i = 1; i < kWorkspaceTabs; ++i) track = track.getUnion (tabs[size_t (i)]->getBounds());
        return track.expanded (6, 4);
    }
    return {};
}

void MainView::setAutoTutorial (bool on) { gAutoTutorial = on; }
void MainView::setStoredThemeUsed (bool on) { gUseStoredTheme = on; }

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
void MainView::setSidebarShown (bool shown)
{
    if (shown == sidebarShown) return;
    sidebarShown = shown;
    sidebarButton->setOn (shown);
    sidebar->setCollapsed (! shown);
    resized();
    repaint();
}

juce::String MainView::panelName (bool left) const
{
    if (left)
    {
        if (page == Page::Tune) return "Inputs";
        if (page == Page::Inspector) return "Channels";
        return "Sidebar";
    }
    if (page == Page::Inspector) return "What DLIVE did";
    return {};
}

bool MainView::panelShown (bool left) const
{
    if (left)
    {
        if (page == Page::Tune) return mixPage->isRailShown();
        if (page == Page::Inspector) return advancedPage->isRailShown();
        return sidebarShown;
    }
    if (page == Page::Inspector) return advancedPage->isTrailShown();
    return true;
}

void MainView::togglePanel (bool left)
{
    if (left)
    {
        if (page == Page::Tune) { mixPage->setRailShown (! mixPage->isRailShown()); return; }
        if (page == Page::Inspector) { advancedPage->setRailShown (! advancedPage->isRailShown()); return; }
        setSidebarShown (! sidebarShown);
        return;
    }
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
    showToast (on ? "Bypass on: you are hearing the raw console feed. Faders are disabled and the kept mix is untouched."
                  : "Bypass off. You are hearing the kept mix again.");
    mixerPage->repaint();
    if (mixerWindow != nullptr) mixerWindow->getPage().repaint();
    livePage->repaint();
    mixPage->repaint();
    updateChainFoot();
}

void MainView::closeSheets()
{
    outputsSheet.reset();
    themeSheet.reset();
    channelSheet.reset();
    chatSheet.reset();
    updateChrome();
    resized();
}

void MainView::showThemes()
{
    if (themeSheet != nullptr) { themeSheet->refresh(); return; }
    themeSheet = std::make_unique<ThemeSheet> (gUseStoredTheme);
    themeSheet->onToast = [this] (const juce::String& t) { showToast (t); };
    themeSheet->onThemeChanged = [this]
    {
        updateChrome();
        if (menu != nullptr) menu->menuItemsChanged();   // the tick in View > Appearance follows
    };
    themeSheet->onClose = [this]
    {
        juce::Component::SafePointer<MainView> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->themeSheet.reset(); safe->updateChrome(); } });
    };
    addAndMakeVisible (*themeSheet);
    resized();
    themeSheet->toFront (true);
}

void MainView::applyThemeNamed (const juce::String& name)
{
    if (themeSheet != nullptr) { themeSheet->chooseTheme (name); return; }
    const auto theme = ThemeStore::find (name);
    Dine::applyTheme (theme);
    if (gUseStoredTheme) ThemeStore::setChosenTheme (theme.name);
    Dine::refreshAllWindows();
    updateChrome();
    if (menu != nullptr) menu->menuItemsChanged();       // the macOS menu is cached until the model says it changed
    showToast ("Appearance: " + theme.name);
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

int MainView::selectedChannel() const
{
    switch (page)
    {
        case Page::Mixer:     return mixerPage->selectedStrip();
        case Page::Tracks:    return tracksPage->selectedTrack();
        case Page::Inspector: return advancedPage->selectedStrip();
        case Page::Tune:      return mixPage->selectedStrip();
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
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->chatSheet.reset(); safe->updateChrome(); safe->resized(); } });
    };
    addAndMakeVisible (*chatSheet);
    updateChrome();
    resized();
    chatSheet->toFront (true);
    chatSheet->takeFocus();
}

// ---------------------------------------------------------------------------
// Input mappings
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
            showToast ("Patch saved as \"" + name + "\": " + juce::String (map.inputs.size()) + " inputs across "
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
        m.addItem (-1, "No saved patches yet", false, false);

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
    m.addItem (900, "Import a patch" + juce::String (Glyph::ellip()));
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

    if (! result.problems.empty())
    {
        juce::String body;
        for (const auto& p : result.problems) body << p << "\n\n";
        body << juce::String (result.restored) << " of " << juce::String (map.inputs.size())
             << " inputs can be restored exactly as they were. Nothing is routed until you say so.";
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
    showToast ("The console is open in its own window. Close it and it comes back here.");
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
    if (id >= 640 && id < 640 + themeMenuNames.size()) { applyThemeNamed (themeMenuNames[id - 640]); return; }
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
            showToast (id == 300 ? "Every track is set to record." : "No tracks are set to record.");
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
        case 411:
            if (! controller.canUndoMix()) { showToast ("There is no earlier mix to step back to in this session."); break; }
            controller.undoMix(); updateChrome();
            showToast ("Stepped back a whole mix. Every value it set went with it.");
            break;
        case 412:
            if (! controller.canRedoMix()) { showToast ("This is the newest mix in this session."); break; }
            controller.redoMix(); updateChrome();
            showToast ("Stepped forward a whole mix.");
            break;
        case 108: saveInputMapping(); break;
        case 109: openInputMappings(); break;

        case 400:
            if (liveSafeBlocks ("TUNE MIX")) break;
            showPage (Page::Tune);
            mixPage->pressTune();
            break;
        case 405:
            if (liveSafeBlocks ("TUNE LIVE MIX")) break;
            showPage (Page::Tune);
            mixPage->pressLiveTune();
            break;
        case 404:
            if (liveSafeBlocks ("TUNE CHANNEL")) break;
            if (selectedChannel() < 0 && lastChannel < 0) { showToast ("Pick a channel first: click a strip on the mixer or a track header."); break; }
            tuneChannel (selectedChannel() >= 0 ? selectedChannel() : lastChannel);
            break;
        case 407:
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
        case 420: showToast (juce::String (controller.raiseLoudnessToTarget())); break;
        case 430: case 431: case 432: case 433: case 434: case 435: case 436:
            controller.setDelivery (DeliveryLoudness (id - 430));
            break;
        case 450: case 451: case 452: case 453: case 454: case 455: case 456: case 457:
            controller.setVoicing (MasterVoicing (id - 450));
            showToast (MasterVoicing (id - 450) == MasterVoicing::Neutral ? juce::String ("Master back to exactly what TUNE MIX built.")
                                                                          : "Master voiced for " + juce::String (masterVoicingName (MasterVoicing (id - 450))).toLowerCase() + ".");
            break;
        case 402: controller.clearSolos(); showToast ("Solo cleared."); break;
        case 403: setBypass (! controller.isBypassed()); break;
        case 406:
        {
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
        case 501: transportBar->toggleRecord(); break;
        case 502: transportBar->returnToStart(); break;
        case 503: transportBar->toggleLoop(); break;

        case 600: showPage (Page::Tracks); break;
        case 601: showPage (Page::Mixer); break;
        case 602: showPage (Page::Tune); break;
        case 603: showPage (Page::Live); break;
        case 604: showPage (Page::Inspector); break;
        case 605: tracksPage->zoom (1.25); break;
        case 606: tracksPage->zoom (0.8); break;
        case 607: tracksPage->zoomToFit(); break;
        case 608: openMixerWindow(); break;
        case 609: showOutputs(); break;
        case 620: showThemes(); break;
        case 621: showThemes(); if (themeSheet != nullptr) themeSheet->importTheme(); break;
        case 622: ThemeStore::folder().createDirectory(); ThemeStore::folder().revealToUser(); break;
        case 610: setSidebarShown (! sidebarShown); break;
        case 611: togglePanel (true); break;
        case 612: togglePanel (false); break;
        case 613: showPage (isSetupPage (page) ? page : Page::Device); break;
        case 615:
            if (page == Page::Inspector)
            {
                const bool open = advancedPage->isRailShown() || advancedPage->isTrailShown();
                advancedPage->setRailShown (! open);
                advancedPage->setTrailShown (! open);
            }
            else if (page == Page::Tune) mixPage->setRailShown (! mixPage->isRailShown());
            else setSidebarShown (! sidebarShown);
            break;

        case 614:
        {
            auto& daw = services.daw();
            daw.setLiveSafe (! daw.isLiveSafe());
            livePage->rebuild();
            updateChrome();
            services.saveSession();
            showToast (daw.isLiveSafe()
                           ? juce::String ("LIVE SAFE on. The sound is locked: re-routes and re-tunes are blocked. ") + liveSafe::allowedSummary()
                           : juce::String ("LIVE SAFE off. Re-routes and re-tunes are allowed again."));
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
        // Cmd-1..5 follow the tab bar left to right: TRACKS MIXER TUNE LIVE INSPECTOR.
        if (code >= '1' && code <= '5')
        {
            static constexpr int kTabCommand[5] = { 600, 601, 602, 603, 604 };
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
    if (code == juce::KeyPress::escapeKey) { if (chatSheet != nullptr || outputsSheet != nullptr || themeSheet != nullptr) { closeSheets(); return true; } }
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
    setupPopover();
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
    const juce::String current = services.broadcastOutputDevice();
    for (int i = 0; i < outs.size(); ++i)
    {
        if (outs[i].name.startsWith ("DLIVE Monitoring")) continue;
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
                         if (liveSafeBlocks ("changing the output device")) return;
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

    const bool slow = (++slowTicks % 30) == 0;
    statusBar->update (slow);
    if (slow || slowTicks % 10 == 0) sidebar->refresh (services.daw().isRecording());
    if (chainFoot->isVisible() && slowTicks % 3 == 0) updateChainFoot();

    if (toastTicks > 0 && --toastTicks == 0) toast->setVisible (false);
    if (saveTicks > 0 && --saveTicks == 0) services.saveSession();

    if (tuningLiveWasOn != controller.isTuningLive()) { tuningLiveWasOn = controller.isTuningLive(); updateChrome(); }

    const bool running = services.isAudioRunning();
    if (audioWasRunning != running) updateChrome();
    if (audioWasRunning && ! running && services.deviceStopped())
        showToast ("The audio device stopped. Check its connection, then choose it again under Audio device.");
    audioWasRunning = running;

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
juce::Rectangle<int> MainView::columnBounds() const
{
    auto r = getLocalBounds().withTrimmedTop (Dine::Metric::titleRow + Dine::Metric::toolbar);
    if (sidebar != nullptr && sidebar->isVisible()) r.removeFromLeft (sidebar->width());
    return r;
}

juce::Rectangle<int> MainView::contentBounds() const
{
    auto r = columnBounds().withTrimmedBottom (Dine::Metric::status);
    if (chainFoot != nullptr && chainFoot->isVisible()) r.removeFromBottom (Dine::Metric::chainFoot);
    return r;
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    // ---- the strip along the very top: lit while what this Mac is doing reaches somebody else.
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

    // ---- the title row, then the toolbar
    auto top = getLocalBounds();
    auto titleRow = top.removeFromTop (Dine::Metric::titleRow);
    g.setColour (Dine::title);
    g.fillRect (titleRow);
    {
        // the counts beside the chat button
        const auto& session = controller.getSession();
        const auto& project = services.daw().getProject();
        int armed = 0;
        for (const auto& t : project.tracks) if (t.armed) ++armed;
        juce::String counts;
        if (! session.inputs.empty())
            counts = juce::String (int (session.inputs.size())) + " inputs      " + juce::String (armed) + " to record";
        auto cell = titleRow.reduced (18, 0);
        // the wordmark, at the left end of the title row after the sidebar switch, on screen whatever the sidebar does
        g.setColour (Dine::accent);
        g.setFont (Dine::caps (13.0f, 0.16f));
        cell.removeFromLeft (26 + 14);
        g.drawText ("DLIVE", cell.removeFromLeft (kWordmarkW), juce::Justification::centredLeft);
        if (chatButton->isVisible()) cell.removeFromRight (chatButton->getWidth() + 14);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (13.0f));
        g.drawText (counts, cell, juce::Justification::centredRight, true);
    }
    Dine::drawChrome (g, top.removeFromTop (Dine::Metric::toolbar));
}

void MainView::resized()
{
    // ---- the title row: the sidebar switch, the session in the middle, the chat on the right
    auto titleRow = getLocalBounds().removeFromTop (Dine::Metric::titleRow).reduced (18, 0);
    sidebarButton->setBounds (titleRow.removeFromLeft (26).withSizeKeepingCentre (26, 22));
    auto titleRight = titleRow;
    if (chatButton->isVisible())
    {
        const int w = chatButton->idealWidth();
        chatButton->setBounds (titleRight.removeFromRight (w).withSizeKeepingCentre (w, 26));
    }
    {
        const int w = juce::jmin (sessionButton->idealWidth(), juce::jmax (80, getWidth() - 520));
        sessionButton->setBounds (juce::Rectangle<int> (getWidth() / 2 - w / 2, titleRow.getY(), w, titleRow.getHeight())
                                      .withSizeKeepingCentre (w, 28));
    }

    // ---- the toolbar: the right cluster, the transport, then the tabs in the middle
    auto bar = getLocalBounds().withTrimmedTop (Dine::Metric::titleRow).removeFromTop (Dine::Metric::toolbar).reduced (18, 0);
    auto right = bar;
    // What the tabs need, so the right cluster can give way before they overlap it.
    int tabsNeed = 0;
    for (int i = 0; i < kWorkspaceTabs; ++i) tabsNeed += tabs[size_t (i)]->idealWidth();
    tabsNeed += 18 * (kWorkspaceTabs - 1);
    const int transportNeed = transportBar->isVisible() ? transportBar->keysOnlyWidth() + 16 : 0;
    const int clusterNeed = (bypassButton->isVisible() ? bypassButton->idealWidth() + 16 : 0)
                          + (liveSafeButton->isVisible() ? liveSafeButton->idealWidth() + 10 : 0);
    if (outputButton.isVisible())
    {
        const int room = bar.getWidth() - transportNeed - tabsNeed - clusterNeed - 24;
        const int w = juce::jlimit (60, 220, juce::jmin (outputButton.idealWidth(), room));
        outputButton.setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
        right.removeFromRight (10);
    }
    if (liveSafeButton->isVisible())
    {
        const int w = liveSafeButton->idealWidth();
        liveSafeButton->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
        right.removeFromRight (10);
    }
    if (bypassButton->isVisible())
    {
        const int w = bypassButton->idealWidth();
        bypassButton->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
        right.removeFromRight (16);
    }
    auto left = bar.withRight (right.getRight());
    int transportW = 0;
    if (transportBar->isVisible())
    {
        transportW = juce::jmin (transportBar->idealWidth(), juce::jmax (transportBar->keysOnlyWidth(), left.getWidth() / 2 - 60));
        transportBar->setBounds (left.removeFromLeft (transportW).withSizeKeepingCentre (transportW, TransportBar::height));
        left.removeFromLeft (16);
    }
    {
        int widths[kWorkspaceTabs], total = 0;
        for (int i = 0; i < kWorkspaceTabs; ++i) { widths[i] = tabs[size_t (i)]->idealWidth(); total += widths[i]; }
        const int gap = 18;
        total += gap * (kWorkspaceTabs - 1);
        int x = juce::jlimit (left.getX(), juce::jmax (left.getX(), left.getRight() - total), getWidth() / 2 - total / 2);
        auto row = juce::Rectangle<int> (x, bar.getY(), total, bar.getHeight()).withSizeKeepingCentre (total, 30);
        for (int i = 0; i < kWorkspaceTabs; ++i)
        {
            tabs[size_t (i)]->setBounds (row.removeFromLeft (widths[i]));
            row.removeFromLeft (gap);
        }
    }

    // ---- the body
    auto body = getLocalBounds().withTrimmedTop (Dine::Metric::titleRow + Dine::Metric::toolbar);
    sidebar->setBounds (body.removeFromLeft (sidebar->width()));
    statusBar->setBounds (body.removeFromBottom (Dine::Metric::status));
    // The requests panel is a column beside the workspace, never over it: the pages and the chain foot
    // give up its width, so a sheet a page opens stays whole and the panel stays readable.
    const int panelW = chatSheet != nullptr ? juce::jmin (kRequestsW, body.getWidth() / 2) : 0;
    body.removeFromRight (panelW);
    if (chainFoot->isVisible()) chainFoot->setBounds (body.removeFromBottom (Dine::Metric::chainFoot));

    auto content = body;
    for (juce::Component* p : { (juce::Component*) sessionsPage.get(), (juce::Component*) devicePage.get(),
                                (juce::Component*) assignPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (content);

    // A sheet covers the workspace column; the chat is a panel down the right of it.
    auto column = columnBounds();
    for (juce::Component* sheetComponent : { (juce::Component*) outputsSheet.get(), (juce::Component*) themeSheet.get(),
                                             (juce::Component*) channelSheet.get() })
        if (sheetComponent != nullptr) { sheetComponent->setBounds (column); sheetComponent->toFront (false); }
    if (chatSheet != nullptr)
    {
        chatSheet->setBounds (column.removeFromRight (panelW).withTrimmedBottom (Dine::Metric::status));
        chatSheet->toFront (false);
    }

    if (tutorial != nullptr) { tutorial->setBounds (getLocalBounds()); tutorial->toFront (false); }

    if (toast->isVisible())
    {
        const int w = toast->idealWidth();
        const int h = toast->idealHeight();
        auto area = contentBounds();
        if (chatSheet != nullptr) area.removeFromRight (chatSheet->getWidth());
        toast->setBounds (area.getCentreX() - w / 2, area.getBottom() - 30 - h, w, h);
        toast->toFront (false);
    }
}

} // namespace livemix
