#include "MainView.h"
#include "native/MultitrackImport.h"

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

// ---------------------------------------------------------------- mixer window
// The console on its own window (a second screen, usually), with the timeline still in
// front of you. It is the same MixController, so both consoles always agree.
class MainView::MixerWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    MixerWindow (MixController& c, AppServices& s, MainView& owner)
        : juce::DocumentWindow ("Mixer " + juce::String (Glyph::dash()) + " DINELIVE", Dine::window,
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton
                                    | juce::DocumentWindow::maximiseButton),
          view (owner)
    {
        page = std::make_unique<MixerPage> (c, s);
        page->setWindowButtonVisible (false);
        page->onOpenStrip = [&owner] (int strip) { owner.showPage (Page::Inspector); owner.getAdvancedPage().select (strip); };
        page->onOpenBus = [&owner] (MixBus bus) { owner.showPage (Page::Inspector); owner.getAdvancedPage().selectBus (bus); };
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
                m.addItem (300, "Arm All Tracks");
                m.addItem (301, "Disarm All Tracks");
                m.addSeparator();
                m.addItem (302, "Monitoring: Input");
                m.addItem (303, "Monitoring: Auto");
                m.addItem (304, "Monitoring: Off");
                break;
            case 3:
                m.addItem (400, "TUNE MIX");
                m.addSeparator();
                m.addItem (401, "Reset Macros");
                m.addItem (402, "Clear Solos");
                m.addSeparator();
                m.addItem (403, "Bypass: Hear the Inputs   B", true, view.controller.isBypassed());
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
                m.addSeparator();
                m.addItem (605, "Zoom In");
                m.addItem (606, "Zoom Out");
                m.addItem (607, "Zoom to Fit");
                break;
            default:
                m.addItem (700, "About DINELIVE");
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

    for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        addChildComponent (*p);
    addChildComponent (*transportBar);

    // ---- sidebar
    addAndMakeVisible (sessionsItem);
    sessionsItem.onClick = [this] { openSession(); };
    sessionsItem.setTooltip ("Open a saved session.");

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

    addAndMakeVisible (outputButton);
    outputButton.setTooltip ("Where the finished mix goes out.");
    outputButton.onClick = [this] { chooseOutput(); };

    addChildComponent (*toast);

    devicePage->onContinue = [this]
    {
        if (! controller.getSession().inputs.empty()) enterSession();
        else showPage (Page::Assign);
    };
    devicePage->onContinueToAssign = [this] { showPage (Page::Assign); };
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
    tracksPage->onTimelineChanged = [this] { updateChrome(); };
    tracksPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    mixPage->onOpenAdvanced = [this] { showPage (Page::Inspector); };
    mixPage->onToast = [this] (const juce::String& t) { showToast (t); };
    mixerPage->onOpenStrip = [this] (int strip) { showPage (Page::Inspector); advancedPage->select (strip); };
    mixerPage->onOpenBus = [this] (MixBus bus) { showPage (Page::Inspector); advancedPage->selectBus (bus); };
    mixerPage->onOpenWindow = [this] { openMixerWindow(); };
    mixerPage->onToast = [this] (const juce::String& t) { showToast (t); };
    livePage->onToast = [this] (const juce::String& t) { showToast (t); };
    livePage->onLiveSafeChanged = [this] { updateChrome(); repaint(); };
    advancedPage->onBack = [this] { showPage (Page::Tune); };
    transportBar->onToast = [this] (const juce::String& t) { showToast (t); };
    transportBar->onTimelineChanged = [this] { timelineChanged(); };

    controller.onMessage = [this] (const std::string& m) { showToast (m); };
    controller.onMixChanged = [this] { requestSave(); };

    setWantsKeyboardFocus (true);
    showPage (Page::Device);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    mixerWindow.reset();          // before the look and feel it draws with
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
    showToast ("LIVE SAFE is on: " + what + " is locked. Turn it off on the Live page.");
    return true;
}

void MainView::showPage (Page p)
{
    page = p;
    devicePage->setVisible (p == Page::Device);
    assignPage->setVisible (p == Page::Assign);
    purposePage->setVisible (p == Page::Purpose);
    tracksPage->setVisible (p == Page::Tracks);
    mixerPage->setVisible (p == Page::Mixer);
    mixPage->setVisible (p == Page::Tune);
    livePage->setVisible (p == Page::Live);
    advancedPage->setVisible (p == Page::Inspector);

    if (p == Page::Device) devicePage->refresh();
    if (p == Page::Assign) assignPage->refresh();
    if (p == Page::Purpose) purposePage->refresh();
    if (p == Page::Tracks) tracksPage->rebuild();
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
    workspaceItems[2]->setMeta (controller.getTuneCount() > 0 ? "tuned" : juce::String());
    workspaceItems[3]->setMeta (project.liveSafe ? "safe" : juce::String());

    sessionsItem.setMeta (juce::String (services.listSessions().size()));

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

    const juce::String out = services.currentOutputDevice();
    outputButton.setValue (out.isEmpty() ? "No output" : out);
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
            showToast (id == 300 ? "Every track armed." : "Every track disarmed.");
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

        case 400:
            if (liveSafeBlocks ("TUNE MIX")) break;
            showPage (Page::Tune);
            mixPage->pressTune();
            break;
        case 401: controller.resetMacros(); showToast ("Macros back to the plan."); break;
        case 402: controller.clearSolos(); showToast ("Solos cleared."); break;
        case 403: setBypass (! controller.isBypassed()); break;

        case 500: transportBar->togglePlay(); break;
        case 501: if (! liveSafeBlocks ("recording")) transportBar->toggleRecord(); break;
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

        case 700:
            showToast ("DINELIVE - the live recording and broadcast DAW. Connect. Record. Mix. Tune. Broadcast.");
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
    if (code == 'M')                       { handleCommand (203); return true; }
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

    const bool mp3 = format == AppServices::ExportFormat::Mp3;
    const auto suggest = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                             .getChildFile (services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "DINELIVE mix")
                             .withFileExtension (mp3 ? "mp3" : "wav");
    chooser = std::make_unique<juce::FileChooser> ("Export the stereo mix", suggest, mp3 ? "*.mp3" : "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, format] (const juce::FileChooser& fc)
                          {
                              const auto dest = fc.getResult();
                              if (dest == juce::File()) return;
                              showToast ("Exporting" + Glyph::ellip());
                              juce::Component::SafePointer<MainView> safe (this);
                              auto& srv = services;
                              juce::Thread::launch ([safe, &srv, dest, format]
                              {
                                  const auto err = srv.exportMix (dest, format, {});
                                  juce::MessageManager::callAsync ([safe, err, dest]
                                  {
                                      if (safe == nullptr) return;
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
    const juce::String current = services.currentOutputDevice();
    for (int i = 0; i < outs.size(); ++i)
        m.addItem (i + 1, outs[i].name, true, outs[i].name == current);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton).withMinimumWidth (outputButton.getWidth()),
                     [this, outs] (int result)
                     {
                         if (result <= 0 || result > outs.size()) return;
                         const auto& name = outs[result - 1].name;
                         if (name == services.currentOutputDevice()) return;
                         const auto err = services.isAudioRunning() ? services.changeOutput (name)
                                                                    : services.openOutputOnly (name);
                         if (err.isNotEmpty()) showToast (err);
                         else { showToast ("Output: " + name); updateChrome(); }
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

    if (toastTicks > 0 && --toastTicks == 0) toast->setVisible (false);
    if (saveTicks > 0 && --saveTicks == 0) services.saveSession();

    const bool running = services.isAudioRunning();
    if (audioWasRunning != running) updateChrome();
    if (audioWasRunning && ! running && services.deviceStopped())
        showToast ("The audio device stopped. Check its connection, then choose it again under Audio device.");
    audioWasRunning = running;

    repaint (0, getHeight() - Dine::Metric::footer - 96, Dine::Metric::sidebar, 96);
}

// ---------------------------------------------------------------- layout
juce::Rectangle<int> MainView::transportBounds() const
{
    return getLocalBounds().withTrimmedLeft (Dine::Metric::sidebar).removeFromBottom (Dine::Metric::footer);
}

juce::Rectangle<int> MainView::contentBounds() const
{
    auto r = getLocalBounds().withTrimmedLeft (Dine::Metric::sidebar).withTrimmedTop (Dine::Metric::toolbar);
    if (transportBar != nullptr && transportBar->isVisible()) r = r.withTrimmedBottom (Dine::Metric::footer);
    return r;
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

    auto sidebar = getLocalBounds().removeFromLeft (Dine::Metric::sidebar);
    g.setColour (Dine::sidebar);
    g.fillRect (sidebar);
    g.setColour (Dine::hair);
    g.fillRect (float (sidebar.getRight()) - 0.5f, 0.0f, 0.5f, float (getHeight()));

    auto brand = sidebar.withHeight (46).reduced (14, 0);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (14.0f, 700).withExtraKerningFactor (0.10f));
    g.drawText ("DINELIVE", brand, juce::Justification::centredLeft);

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
    const juce::Colour dot = ! running ? Dine::ink4 : services.xrunCount() > 0 ? Dine::warn : Dine::ok;
    auto dotArea = line.removeFromLeft (10);
    if (running)
    {
        g.setColour (dot.withAlpha (0.35f));
        g.fillEllipse (dotArea.withSizeKeepingCentre (12, 12).toFloat());
    }
    g.setColour (dot);
    g.fillEllipse (dotArea.withSizeKeepingCentre (7, 7).toFloat());
    line.removeFromLeft (4);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (12.0f, 600));
    g.drawText (! running ? "Not running" : services.daw().isRecording() ? "Recording" : "Running",
                line, juce::Justification::centredLeft);

    inner.removeFromTop (4);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (11.0f));
    const juce::String device = services.currentInputDevice().isNotEmpty() ? services.currentInputDevice()
                                                                           : (running ? services.currentOutputDevice() + " (output only)" : "No audio device");
    g.drawText (device, inner.removeFromTop (14), juce::Justification::topLeft, true);
    g.setColour (services.xrunCount() > 0 ? Dine::warn : Dine::ink3);
    g.setFont (Dine::mono (11.0f));
    juce::String clock = running ? juce::String (services.sampleRate() / 1000.0, 1) + " kHz  " + Glyph::dot() + "  "
                                       + juce::String (services.bufferSize()) + " smp"
                                 : juce::String ("--");
    if (running && services.xrunCount() > 0) clock += "  " + Glyph::dot() + "  " + juce::String (services.xrunCount()) + " drops";
    g.drawText (clock, inner.removeFromTop (14), juce::Justification::topLeft, true);

    // ---- toolbar
    auto toolbar = getLocalBounds().withTrimmedLeft (Dine::Metric::sidebar).removeFromTop (Dine::Metric::toolbar);
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

void MainView::resized()
{
    walkSidebar (Dine::Metric::sidebar,
                 [] (juce::Component* c, juce::Rectangle<int> r) { if (c != nullptr) c->setBounds (r); },
                 [] (juce::Rectangle<int>, const char*) {},
                 &sessionsItem, setupItems, workspaceItems);

    auto toolbar = getLocalBounds().withTrimmedLeft (Dine::Metric::sidebar).removeFromTop (Dine::Metric::toolbar).reduced (14, 0);
    sessionButton->setBounds (toolbar.removeFromLeft (sessionButton->idealWidth()).withSizeKeepingCentre (sessionButton->idealWidth(), 38));
    auto right = toolbar;
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

    if (transportBar->isVisible()) transportBar->setBounds (transportBounds());

    auto content = contentBounds();
    for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get(),
                                (juce::Component*) purposePage.get(), (juce::Component*) tracksPage.get(),
                                (juce::Component*) mixerPage.get(), (juce::Component*) mixPage.get(),
                                (juce::Component*) livePage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (content);

    if (toast->isVisible())
    {
        const int w = toast->idealWidth();
        toast->setBounds (content.getCentreX() - w / 2, content.getBottom() - 22 - 30, w, 30);
        toast->toFront (false);
    }
}

} // namespace livemix
