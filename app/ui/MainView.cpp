#include "MainView.h"
#include "native/InputMapStore.h"
#include "native/MultitrackImport.h"
#include "native/OpenAiMixProvider.h"

namespace livemix
{

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
    ToolbarToggle (const juce::String& text, juce::Colour c) : juce::Button (text), tint (c)
    {
        setClickingTogglesState (false);
    }

    void setOn (bool o) { if (o != on) { on = o; repaint(); } }
    bool isOn() const noexcept { return on; }

    int idealWidth() const
    {
        return Dine::textWidth (Dine::text (12.0f, 600).withExtraKerningFactor (0.05f), getButtonText()) + 26;
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

        g.setColour (on ? juce::Colour (0xff1b1c1e) : Dine::ink2);
        g.setFont (Dine::text (12.0f, 600).withExtraKerningFactor (0.05f));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::Colour tint;
    bool on = false;
};

// The toolbar's sidebar switch, in the place macOS puts it: the far left, before the
// document. It is the same command as View > Sidebar and Ctrl-Cmd-S.
class MainView::SidebarButton : public juce::Button
{
public:
    SidebarButton() : juce::Button ("Sidebar") { setWantsKeyboardFocus (false); }

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
                m.addItem (600, "Tracks");
                m.addItem (601, "Mixer");
                m.addItem (602, "Tune");
                m.addItem (603, "Live");
                m.addItem (604, "Inspector");
                m.addSeparator();
                m.addItem (608, "Open Mixer in a New Window");
                m.addItem (609, "Outputs" + juce::String (Glyph::ellip()));
                m.addSeparator();
                m.addItem (610, (view.sidebarShown ? "Hide Sidebar" : "Show Sidebar") + juce::String ("   ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\x83\xe2\x8c\x98")) + "S");
                if (const auto left = view.panelName (true); left != "Sidebar")
                    m.addItem (611, (view.panelShown (true) ? "Hide " : "Show ") + left + "   [");
                if (const auto right = view.panelName (false); right.isNotEmpty())
                    m.addItem (612, (view.panelShown (false) ? "Hide " : "Show ") + right + "   ]");
                m.addSeparator();
                m.addItem (605, "Zoom In");
                m.addItem (606, "Zoom Out");
                m.addItem (607, "Zoom to Fit");
                break;
            default:
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

    // ---- sidebar
    addAndMakeVisible (sessionsItem);
    sessionsItem.onClick = [this] { showPage (Page::Sessions); };
    sessionsItem.setTooltip ("The library: every saved session, and what each one was for.");

    const char* setupLabels[3] = { "Audio device", "Inputs", "Purpose and sound" };
    const Dine::Icon setupIcons[3] = { Dine::Icon::Device, Dine::Icon::Sliders, Dine::Icon::Target };
    const Page setupPages[3] = { Page::Device, Page::Assign, Page::Purpose };
    for (int i = 0; i < 3; ++i)
    {
        setupItems[size_t (i)] = std::make_unique<DineNavItem> (setupLabels[i], setupIcons[i]);
        setupItems[size_t (i)]->onClick = [this, p = setupPages[i]] { showPage (p); };
        addAndMakeVisible (*setupItems[size_t (i)]);
    }

    const char* workspaceLabels[5] = { "Tracks", "Mixer", "Tune", "Live", "Inspector" };
    const Dine::Icon workspaceIcons[5] = { Dine::Icon::Waveform, Dine::Icon::Sliders, Dine::Icon::Target,
                                           Dine::Icon::Play, Dine::Icon::List };
    const Page workspacePages[5] = { Page::Tracks, Page::Mixer, Page::Tune, Page::Live, Page::Inspector };
    for (int i = 0; i < 5; ++i)
    {
        workspaceItems[size_t (i)] = std::make_unique<DineNavItem> (workspaceLabels[i], workspaceIcons[i]);
        workspaceItems[size_t (i)]->onClick = [this, p = workspacePages[i]] { showPage (p); };
        addAndMakeVisible (*workspaceItems[size_t (i)]);
    }

    // ---- toolbar
    addAndMakeVisible (*sessionButton);
    sessionButton->onClick = [this] { sessionMenu(); };

    const char* tabLabels[kWorkspaceTabs] = { "Tracks", "Mixer", "Tune", "Live" };
    const Page tabPages[kWorkspaceTabs] = { Page::Tracks, Page::Mixer, Page::Tune, Page::Live };
    for (int i = 0; i < kWorkspaceTabs; ++i)
    {
        tabs[size_t (i)] = std::make_unique<DineButton> (tabLabels[i], DineButton::Style::Segment);
        tabs[size_t (i)]->setFontPx (12.0f);
        tabs[size_t (i)]->setPadX (13);
        tabs[size_t (i)]->setClickingTogglesState (false);
        tabs[size_t (i)]->onClick = [this, p = tabPages[i]] { showPage (p); };
        addAndMakeVisible (*tabs[size_t (i)]);
    }

    bypassButton = std::make_unique<ToolbarToggle> ("BYPASS", Dine::warn);
    bypassButton->setTooltip ("Hear the inputs exactly as they arrive: no processing, no fader moves, no effects. "
                              "Press it again for your mix. Nothing is changed either way (B).");
    bypassButton->onClick = [this] { setBypass (! controller.isBypassed()); };
    addChildComponent (*bypassButton);

    sidebarButton = std::make_unique<SidebarButton>();
    sidebarButton->setTooltip ("Show or hide the sidebar. The workspace takes the width (Ctrl-Cmd-S).");
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
    const bool inWorkspace = page == Page::Tracks || page == Page::Mixer || page == Page::Tune
                             || page == Page::Live || page == Page::Inspector;

    setupItems[0]->setSelected (page == Page::Device);
    setupItems[0]->setDone (running);
    setupItems[1]->setSelected (page == Page::Assign);
    setupItems[1]->setEnabled (running || hasInputs);
    setupItems[1]->setDone (hasInputs);
    setupItems[2]->setSelected (page == Page::Purpose);
    setupItems[2]->setEnabled (running || hasInputs);
    setupItems[2]->setDone (mixable);

    const auto& project = services.daw().getProject();
    const Page workspacePages[5] = { Page::Tracks, Page::Mixer, Page::Tune, Page::Live, Page::Inspector };
    for (int i = 0; i < 5; ++i)
    {
        workspaceItems[size_t (i)]->setSelected (page == workspacePages[i]);
        workspaceItems[size_t (i)]->setEnabled (mixable);
    }
    workspaceItems[0]->setMeta (hasInputs ? juce::String (int (session.inputs.size())) : juce::String());
    workspaceItems[1]->setMeta (hasInputs ? juce::String (int (session.inputs.size())) : juce::String());
    // A live run carries on whichever workspace you are looking at, so the sidebar says so:
    // leaving the Tune page should not be the same as losing sight of the run.
    workspaceItems[2]->setMeta (controller.isTuningLive() ? "working"
                                : controller.getTuneCount() > 0 ? "tuned" : juce::String());
    workspaceItems[3]->setMeta (project.liveSafe ? "safe" : juce::String());

    sessionsItem.setMeta (juce::String (services.listSessions().size()));
    sessionsItem.setSelected (page == Page::Sessions);

    const Page tabPages[kWorkspaceTabs] = { Page::Tracks, Page::Mixer, Page::Tune, Page::Live };
    for (int i = 0; i < kWorkspaceTabs; ++i)
    {
        tabs[size_t (i)]->setVisible (inWorkspace);
        tabs[size_t (i)]->setToggleState (page == tabPages[i], juce::dontSendNotification);
    }
    outputButton.setVisible (running || inWorkspace);
    bypassButton->setVisible (inWorkspace && mixable);
    bypassButton->setOn (controller.isBypassed());
    transportBar->setVisible (inWorkspace);

    juce::String name = services.currentSessionName();
    if (name.isEmpty()) name = "Untitled";
    juce::String sub = juce::String (styleProfileName (session.profile)) + "  " + Glyph::dot() + "  "
                       + juce::String (mixPurposeName (session.purpose));
    if (hasInputs) sub += "  " + Glyph::dot() + "  " + juce::String (int (session.inputs.size())) + " tracks";
    sessionButton->set (name, sub);

    // The device DLIVE built to join two of them is not a name anybody chose, so the toolbar
    // says what the user picked instead (AppServices::outputDisplayName).
    const juce::String out = services.outputDisplayName();
    outputButton.setValue (out.isEmpty() ? "No output" : out);
}

// ---------------------------------------------------------------- the panels
// Every panel at the edge of the window folds away and comes back the same way: the
// sidebar from the toolbar, a workspace's own rails from the handle in their gutter.
// None of it touches the session - it is only where the width goes.
void MainView::setSidebarShown (bool shown)
{
    if (shown == sidebarShown) return;
    sidebarShown = shown;
    sidebarButton->setOn (shown);
    sessionsItem.setVisible (shown);
    for (auto& item : setupItems) item->setVisible (shown);
    for (auto& item : workspaceItems) item->setVisible (shown);
    resized();
    repaint();
}

// What the page on screen calls the panel on that side, and whether it is open. A page
// with no panel of its own leaves the left side to the sidebar and the right side alone.
juce::String MainView::panelName (bool left) const
{
    if (page == Page::Inspector) return left ? "Channels" : "What DINE did";
    if (page == Page::Tune && ! left) return "Inputs";
    return left ? "Sidebar" : juce::String();
}

bool MainView::panelShown (bool left) const
{
    if (page == Page::Inspector) return left ? advancedPage->isRailShown() : advancedPage->isTrailShown();
    if (page == Page::Tune && ! left) return mixPage->isRailShown();
    return left ? sidebarShown : true;
}

void MainView::togglePanel (bool left)
{
    if (page == Page::Inspector)
    {
        if (left) advancedPage->setRailShown (! advancedPage->isRailShown());
        else      advancedPage->setTrailShown (! advancedPage->isTrailShown());
        return;
    }
    if (page == Page::Tune && ! left) { mixPage->setRailShown (! mixPage->isRailShown()); return; }
    if (left) setSidebarShown (! sidebarShown);
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
        if (code >= '1' && code <= '5') { handleCommand (600 + (code - '1')); return true; }
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

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sessionsItem).withMinimumWidth (280),
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

    if (toastTicks > 0 && --toastTicks == 0) toast->setVisible (false);
    if (saveTicks > 0 && --saveTicks == 0) services.saveSession();

    if (tuningLiveWasOn != controller.isTuningLive()) { tuningLiveWasOn = controller.isTuningLive(); updateChrome(); }

    const bool running = services.isAudioRunning();
    if (audioWasRunning != running) updateChrome();
    if (audioWasRunning && ! running && services.deviceStopped())
        showToast ("The audio device stopped. Check its connection, then choose it again under Audio device.");
    audioWasRunning = running;

    if (sidebarShown) repaint (0, getHeight() - 90, Dine::Metric::sidebar, 90);
}

// ---------------------------------------------------------------- layout
// The transport lives in the unified toolbar, between the session and the workspace
// tabs, so it is in the same place on every page and the foot of a workspace is free
// for the chain strip.
juce::Rectangle<int> MainView::contentBounds() const
{
    return getLocalBounds().withTrimmedLeft (sidebarWidth()).withTrimmedTop (Dine::Metric::toolbar);
}

// The sidebar is walked once, so paint() and resized() can never disagree about where a row is.
template <typename RowFn, typename LabelFn>
static void walkSidebar (int width, RowFn row, LabelFn label,
                         juce::Component* sessions,
                         std::array<std::unique_ptr<DineNavItem>, 3>& setup,
                         std::array<std::unique_ptr<DineNavItem>, 5>& workspace)
{
    int y = 46;
    label (juce::Rectangle<int> (0, y, width, 18), "Library");
    y += 18;
    row (sessions, juce::Rectangle<int> (8, y, width - 16, 28));
    y += 29 + 14;
    label (juce::Rectangle<int> (0, y, width, 18), "Set up");
    y += 18;
    for (auto& item : setup) { row (item.get(), juce::Rectangle<int> (8, y, width - 16, 28)); y += 29; }
    y += 14;
    label (juce::Rectangle<int> (0, y, width, 18), "Workspace");
    y += 18;
    for (auto& item : workspace) { row (item.get(), juce::Rectangle<int> (8, y, width - 16, 28)); y += 29; }
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    if (sidebarShown) paintSidebar (g);

    // ---- toolbar
    auto toolbar = getLocalBounds().withTrimmedLeft (sidebarWidth()).removeFromTop (Dine::Metric::toolbar);
    g.setColour (Dine::toolbar);
    g.fillRect (toolbar);
    g.setColour (Dine::hair);
    g.fillRect (float (toolbar.getX()), float (toolbar.getBottom()) - 0.5f, float (toolbar.getWidth()), 0.5f);

    if (tabs[0] != nullptr && tabs[0]->isVisible())
    {
        auto track = tabs[0]->getBounds();
        for (int i = 1; i < kWorkspaceTabs; ++i) track = track.getUnion (tabs[size_t (i)]->getBounds());
        Dine::fillRounded (g, track.expanded (2, 2).toFloat(), juce::Colours::white.withAlpha (0.07f), 7.0f);
    }
}

void MainView::paintSidebar (juce::Graphics& g)
{
    auto sidebar = getLocalBounds().removeFromLeft (Dine::Metric::sidebar);
    g.setColour (Dine::sidebar);
    g.fillRect (sidebar);
    g.setColour (Dine::hair);
    g.fillRect (float (sidebar.getRight()) - 0.5f, 0.0f, 0.5f, float (getHeight()));

    auto brand = sidebar.withHeight (46).reduced (14, 0);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (14.0f, 700).withExtraKerningFactor (0.10f));
    g.drawText ("DLIVE", brand, juce::Justification::centredLeft);

    walkSidebar (Dine::Metric::sidebar,
                 [] (juce::Component*, juce::Rectangle<int>) {},
                 [&g] (juce::Rectangle<int> r, const char* t)
                 {
                     g.setColour (Dine::ink3);
                     g.setFont (Dine::text (11.0f, 600));
                     g.drawText (t, r.reduced (10, 0), juce::Justification::centredLeft);
                 },
                 &sessionsItem, setupItems, workspaceItems);

    // ---- the device's state, at the foot of the sidebar
    auto status = juce::Rectangle<int> (8, getHeight() - 8 - 74, Dine::Metric::sidebar - 16, 74);
    Dine::fillRounded (g, status.toFloat(), juce::Colours::white.withAlpha (0.05f), Dine::Radius::card);
    auto inner = status.reduced (11, 10);
    auto line = inner.removeFromTop (15);
    const bool running = services.isAudioRunning();
    // "Not running" and "the interface just vanished" are not the same news. A device that
    // went away during a service has to keep saying so, not settle into a grey resting state.
    const bool lost = ! running && services.deviceStopped();
    const juce::Colour dot = lost ? Dine::crit : ! running ? Dine::ink4
                                  : services.xrunCount() > 0 ? Dine::warn : Dine::ok;
    auto dotArea = line.removeFromLeft (10);
    if (running || lost)
    {
        g.setColour (dot.withAlpha (0.35f));
        g.fillEllipse (dotArea.withSizeKeepingCentre (12, 12).toFloat());
    }
    g.setColour (dot);
    g.fillEllipse (dotArea.withSizeKeepingCentre (7, 7).toFloat());
    line.removeFromLeft (4);
    g.setColour (lost ? Dine::crit : Dine::ink);
    g.setFont (Dine::text (12.0f, 600));
    g.drawText (lost ? "Device lost" : ! running ? "Not running"
                     : services.daw().isRecording() ? "Recording" : "Running",
                line, juce::Justification::centredLeft);

    inner.removeFromTop (4);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (11.0f));
    const juce::String device = lost ? services.currentInputDevice().isNotEmpty()
                                           ? services.currentInputDevice() + " stopped"
                                           : juce::String ("The audio device stopped")
                              : services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                                                                           : (running ? services.outputDisplayName() + " (output only)" : "No audio device");
    g.drawText (device, inner.removeFromTop (14), juce::Justification::topLeft, true);
    g.setColour (lost ? Dine::crit : services.xrunCount() > 0 ? Dine::warn : Dine::ink3);
    g.setFont (lost ? Dine::text (11.0f) : Dine::mono (11.0f));
    juce::String clock = running ? juce::String (services.sampleRate() / 1000.0, 1) + " kHz  " + Glyph::dot() + "  "
                                       + juce::String (services.bufferSize()) + " smp"
                       : lost    ? juce::String ("Choose it again under Audio device")
                                 : juce::String ("--");
    if (running && services.xrunCount() > 0) clock += "  " + Glyph::dot() + "  " + juce::String (services.xrunCount()) + " drops";
    g.drawText (clock, inner.removeFromTop (14), juce::Justification::topLeft, true);
}

void MainView::resized()
{
    if (sidebarShown)
        walkSidebar (Dine::Metric::sidebar,
                     [] (juce::Component* c, juce::Rectangle<int> r) { if (c != nullptr) c->setBounds (r); },
                     [] (juce::Rectangle<int>, const char*) {},
                     &sessionsItem, setupItems, workspaceItems);

    // The right-hand cluster is placed first; the session name and the transport then
    // share what is left, the transport keeping its width before the name does.
    auto right = getLocalBounds().withTrimmedLeft (sidebarWidth()).removeFromTop (Dine::Metric::toolbar).reduced (14, 0);
    sidebarButton->setBounds (right.removeFromLeft (30).withSizeKeepingCentre (30, Dine::Metric::control));
    right.removeFromLeft (6);
    if (outputButton.isVisible())
    {
        const int w = juce::jlimit (120, 230, outputButton.idealWidth());
        outputButton.setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
        right.removeFromRight (10);
    }
    if (tabs[0] != nullptr && tabs[0]->isVisible())
    {
        int widths[kWorkspaceTabs], total = 0;
        for (int i = 0; i < kWorkspaceTabs; ++i) { widths[i] = juce::jmax (62, tabs[size_t (i)]->idealWidth() + 12); total += widths[i]; }
        auto seg = right.removeFromRight (total).withSizeKeepingCentre (total, Dine::Metric::control);
        for (int i = 0; i < kWorkspaceTabs; ++i) tabs[size_t (i)]->setBounds (seg.removeFromLeft (widths[i]));
        right.removeFromRight (12);
    }
    if (bypassButton->isVisible())
    {
        const int w = bypassButton->idealWidth();
        bypassButton->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
    }

    int transportW = 0;
    if (transportBar->isVisible())
    {
        transportW = juce::jmin (transportBar->idealWidth(), juce::jmax (0, right.getWidth() - 150));
        if (transportW < transportBar->minimumWidth())
            transportW = juce::jmin (transportBar->minimumWidth(), right.getWidth());
    }

    // The clock is the one readout a DAW is never allowed to truncate, so the session
    // name yields to it rather than the other way round: it can shrink to nothing (the
    // session is also on the sidebar and in the File menu), the timecode cannot.
    const int sessionW = juce::jlimit (0, sessionButton->idealWidth(), right.getWidth() - transportW - 24);
    sessionButton->setBounds (right.removeFromLeft (sessionW).withSizeKeepingCentre (sessionW, 38));

    if (transportW > 0)
        transportBar->setBounds (right.withSizeKeepingCentre (juce::jmin (transportW, right.getWidth()),
                                                              TransportBar::height));

    auto content = contentBounds();
    for (juce::Component* p : { (juce::Component*) sessionsPage.get(), (juce::Component*) devicePage.get(),
                                (juce::Component*) assignPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (content);

    if (outputsSheet != nullptr)
    {
        outputsSheet->setBounds (getLocalBounds().withTrimmedLeft (sidebarWidth())
                                                 .withTrimmedTop (Dine::Metric::toolbar));
        outputsSheet->toFront (false);
    }

    if (channelSheet != nullptr)
    {
        channelSheet->setBounds (getLocalBounds().withTrimmedLeft (sidebarWidth())
                                                 .withTrimmedTop (Dine::Metric::toolbar));
        channelSheet->toFront (false);
    }
    if (chatSheet != nullptr)
    {
        chatSheet->setBounds (getLocalBounds().withTrimmedLeft (sidebarWidth())
                                             .withTrimmedTop (Dine::Metric::toolbar));
        chatSheet->toFront (false);
    }

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
