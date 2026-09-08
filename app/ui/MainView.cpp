#include "MainView.h"

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
        return juce::jmin (560, Dine::textWidth (Dine::text (12.5f), text) + 54);
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
// The toolbar's document title: the setup's name over what it is set to mix.
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
        return juce::jmin (360, juce::jmax (Dine::textWidth (Dine::text (13.0f, 600), name) + 22,
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

// ---------------------------------------------------------------- MainView
MainView::MainView (MixController& c, AppServices& s) : controller (c), services (s)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);

    devicePage = std::make_unique<DevicePage> (controller, services);
    assignPage = std::make_unique<AssignPage> (controller, services);
    purposePage = std::make_unique<PurposePage> (controller);
    mixPage = std::make_unique<MixPage> (controller);
    advancedPage = std::make_unique<AdvancedPage> (controller);
    toast = std::make_unique<Toast>();
    sessionButton = std::make_unique<SessionButton>();
    for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get(), (juce::Component*) purposePage.get(),
                                (juce::Component*) mixPage.get(), (juce::Component*) advancedPage.get() })
        addChildComponent (*p);

    // ---- sidebar
    addAndMakeVisible (setupsItem);
    setupsItem.onClick = [this] { openMix(); };
    setupsItem.setTooltip ("Open a saved setup.");

    const char* setupLabels[3] = { "Audio device", "Inputs", "Purpose and sound" };
    const Dine::Icon setupIcons[3] = { Dine::Icon::Device, Dine::Icon::Sliders, Dine::Icon::Target };
    const Page setupPages[3] = { Page::Device, Page::Assign, Page::Purpose };
    for (int i = 0; i < 3; ++i)
    {
        setupItems[size_t (i)] = std::make_unique<DineNavItem> (setupLabels[i], setupIcons[i]);
        setupItems[size_t (i)]->onClick = [this, p = setupPages[i]] { showPage (p); };
        addAndMakeVisible (*setupItems[size_t (i)]);
    }
    const char* mixLabels[2] = { "Mix", "Advanced" };
    const Dine::Icon mixIcons[2] = { Dine::Icon::Waveform, Dine::Icon::List };
    const Page mixPages[2] = { Page::Mix, Page::Advanced };
    for (int i = 0; i < 2; ++i)
    {
        mixItems[size_t (i)] = std::make_unique<DineNavItem> (mixLabels[i], mixIcons[i]);
        mixItems[size_t (i)]->onClick = [this, p = mixPages[i]] { showPage (p); };
        addAndMakeVisible (*mixItems[size_t (i)]);
    }

    // ---- toolbar
    addAndMakeVisible (*sessionButton);
    sessionButton->onClick = [this] { sessionMenu(); };
    addAndMakeVisible (segMix);
    addAndMakeVisible (segAdvanced);
    segMix.setFontPx (12.0f);
    segAdvanced.setFontPx (12.0f);
    segMix.setPadX (13);
    segAdvanced.setPadX (13);
    segMix.setClickingTogglesState (false);
    segAdvanced.setClickingTogglesState (false);
    segMix.onClick = [this] { showPage (Page::Mix); };
    segAdvanced.onClick = [this] { showPage (Page::Advanced); };
    addAndMakeVisible (outputButton);
    outputButton.setTooltip ("Where the finished mix goes out.");
    outputButton.onClick = [this] { chooseOutput(); };

    addChildComponent (*toast);

    devicePage->onContinue = [this]
    {
        if (! controller.getSession().inputs.empty()) enterMix();
        else showPage (Page::Assign);
    };
    devicePage->onContinueToAssign = [this] { showPage (Page::Assign); };
    assignPage->onBack = [this] { showPage (Page::Device); };
    assignPage->onContinue = [this] { showPage (Page::Purpose); };
    purposePage->onBack = [this] { showPage (Page::Assign); };
    purposePage->onContinue = [this] { enterMix(); };
    mixPage->onOpenAdvanced = [this] { showPage (Page::Advanced); };
    mixPage->onToast = [this] (const juce::String& t) { showToast (t); };
    advancedPage->onBack = [this] { showPage (Page::Mix); };
    controller.onMessage = [this] (const std::string& m) { showToast (m); };
    controller.onMixChanged = [this] { requestSave(); };

    showPage (Page::Device);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    controller.onMessage = nullptr;
    controller.onMixChanged = nullptr;
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainView::enterMix()
{
    services.reconfigure();
    services.saveSession();
    advancedPage->rebuild();
    showPage (Page::Mix);
}

void MainView::showPage (Page p)
{
    page = p;
    devicePage->setVisible (p == Page::Device);
    assignPage->setVisible (p == Page::Assign);
    purposePage->setVisible (p == Page::Purpose);
    mixPage->setVisible (p == Page::Mix);
    advancedPage->setVisible (p == Page::Advanced);
    if (p == Page::Device) devicePage->refresh();
    if (p == Page::Assign) assignPage->refresh();
    if (p == Page::Purpose) purposePage->refresh();
    if (p == Page::Advanced) advancedPage->rebuild();
    updateChrome();
    resized();
    repaint();
}

void MainView::updateChrome()
{
    const auto& session = controller.getSession();
    const bool running = services.isAudioRunning();
    const bool hasInputs = ! session.inputs.empty();
    const bool mixable = controller.isPrepared() && hasInputs;

    setupItems[0]->setSelected (page == Page::Device);
    setupItems[0]->setDone (running);
    setupItems[1]->setSelected (page == Page::Assign);
    setupItems[1]->setEnabled (running || hasInputs);
    setupItems[1]->setDone (hasInputs);
    setupItems[2]->setSelected (page == Page::Purpose);
    setupItems[2]->setEnabled (running || hasInputs);
    setupItems[2]->setDone (mixable);

    mixItems[0]->setSelected (page == Page::Mix);
    mixItems[0]->setEnabled (mixable);
    mixItems[0]->setMeta (controller.getTuneCount() > 0 ? "tuned" : juce::String());
    mixItems[1]->setSelected (page == Page::Advanced);
    mixItems[1]->setEnabled (mixable);
    mixItems[1]->setMeta (hasInputs ? juce::String (int (session.inputs.size())) : juce::String());

    setupsItem.setMeta (juce::String (services.listSessions().size()));

    const bool mixing = page == Page::Mix || page == Page::Advanced;
    segMix.setVisible (mixing);
    segAdvanced.setVisible (mixing);
    segMix.setToggleState (page == Page::Mix, juce::dontSendNotification);
    segAdvanced.setToggleState (page == Page::Advanced, juce::dontSendNotification);
    outputButton.setVisible (running || mixing);

    juce::String name = services.currentSessionName();
    if (name.isEmpty()) name = "Untitled";
    juce::String sub = juce::String (styleProfileName (session.profile)) + "  " + Glyph::dot() + "  "
                       + juce::String (mixPurposeName (session.purpose));
    if (hasInputs) sub += "  " + Glyph::dot() + "  " + juce::String (int (session.inputs.size())) + " inputs";
    sessionButton->set (name, sub);

    juce::String out = services.currentOutputDevice();
    outputButton.setValue (out.isEmpty() ? "No output" : out);
}

void MainView::showToast (const juce::String& text)
{
    toast->show (text);
    toastTicks = 30 * 5;
    resized();
}

void MainView::sessionMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Save setup");
    menu.addItem (2, "Save as a new setup...");
    menu.addSeparator();
    menu.addItem (3, "Open a setup...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton.get()).withMinimumWidth (240),
                        [this] (int result)
                        {
                            if (result == 1) saveNow();
                            else if (result == 2) saveAs();
                            else if (result == 3) openMix();
                        });
}

void MainView::saveNow()
{
    if (services.currentSessionName().isEmpty()) { saveAs(); return; }
    services.saveSession();
    showToast ("Setup saved.");
}

void MainView::saveAs()
{
    auto* alert = new juce::AlertWindow ("Save setup", "Name this setup. It is stored on this Mac and can be opened later.", juce::MessageBoxIconType::NoIcon);
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

void MainView::openMix()
{
    juce::PopupMenu menu;
    const auto listed = services.listSessions();
    if (listed.isEmpty())
    {
        showToast ("No saved setups yet. Save one from the setup menu.");
        return;
    }
    for (int i = 0; i < listed.size(); ++i)
        menu.addItem (i + 1, listed[i].name + "   " + listed[i].modified.formatted ("%d %b"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setupsItem).withMinimumWidth (260),
                        [this, listed] (int result)
                        {
                            if (result <= 0 || result > listed.size()) return;
                            const auto err = services.loadSession (listed[result - 1].file);
                            if (err.isNotEmpty()) { showToast (err); return; }
                            advancedPage->rebuild();
                            showPage (controller.getSession().inputs.empty() ? Page::Assign : Page::Mix);
                            showToast ("Opened \"" + services.currentSessionName() + "\".");
                            updateChrome();
                        });
}

void MainView::chooseOutput()
{
    const auto outs = services.outputDevices();
    if (outs.isEmpty()) { showToast ("No output devices found."); return; }
    juce::PopupMenu menu;
    const juce::String current = services.currentOutputDevice();
    for (int i = 0; i < outs.size(); ++i)
        menu.addItem (i + 1, outs[i].name, true, outs[i].name == current);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton).withMinimumWidth (outputButton.getWidth()),
                        [this, outs] (int result)
                        {
                            if (result <= 0 || result > outs.size()) return;
                            const auto& name = outs[result - 1].name;
                            if (name == services.currentOutputDevice()) return;
                            if (! services.isAudioRunning())
                            {
                                showToast ("Open an input device first, then choose the output.");
                                return;
                            }
                            const auto err = services.changeOutput (name);
                            if (err.isNotEmpty()) showToast (err);
                            else { showToast ("Output: " + name); updateChrome(); }
                        });
}

void MainView::timerCallback()
{
    controller.poll();
    if (page == Page::Mix) mixPage->refresh();
    else if (page == Page::Advanced) advancedPage->refresh();
    if (toastTicks > 0 && --toastTicks == 0) toast->setVisible (false);
    if (saveTicks > 0 && --saveTicks == 0) services.saveSession();
    const bool running = services.isAudioRunning();
    if (audioWasRunning != running) updateChrome();
    if (audioWasRunning && ! running && services.deviceStopped())
        showToast ("The audio device stopped. Check its connection, then choose it again under Audio device.");
    audioWasRunning = running;
    // The sidebar's status card carries the live clock.
    repaint (0, getHeight() - 96, Dine::Metric::sidebar, 96);
}

juce::Rectangle<int> MainView::contentBounds() const
{
    return getLocalBounds().withTrimmedLeft (Dine::Metric::sidebar).withTrimmedTop (Dine::Metric::toolbar);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Dine::window);

    // ---- sidebar
    auto sidebar = getLocalBounds().removeFromLeft (Dine::Metric::sidebar);
    g.setColour (Dine::sidebar);
    g.fillRect (sidebar);
    g.setColour (Dine::hair);
    g.fillRect (float (sidebar.getRight()) - 0.5f, 0.0f, 0.5f, float (getHeight()));

    auto brand = sidebar.removeFromTop (46).reduced (14, 0);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (14.0f, 700).withExtraKerningFactor (0.10f));
    g.drawText ("DINELIVE", brand, juce::Justification::centredLeft);

    auto sectionLabel = [&] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 600));
        g.drawText (t, r.reduced (10, 0), juce::Justification::centredLeft);
    };
    sectionLabel (juce::Rectangle<int> (0, 46, Dine::Metric::sidebar, 18), "Library");
    sectionLabel (juce::Rectangle<int> (0, 46 + 18 + 28 + 14, Dine::Metric::sidebar, 18), "Set up");
    sectionLabel (juce::Rectangle<int> (0, 46 + 18 + 28 + 14 + 18 + 3 * 29 + 14, Dine::Metric::sidebar, 18), "Mix");

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
    g.drawText (! running ? "Not running" : services.isPlayingRecording() ? "Playing a recording" : "Running", line, juce::Justification::centredLeft);

    inner.removeFromTop (4);
    g.setColour (Dine::ink2);
    g.setFont (Dine::text (11.0f));
    g.drawText (running ? services.currentInputDevice() : "No audio device", inner.removeFromTop (14), juce::Justification::topLeft, true);
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

    // The Mix | Advanced segment sits in its own track.
    if (segMix.isVisible())
    {
        auto track = segMix.getBounds().getUnion (segAdvanced.getBounds()).expanded (2, 2);
        Dine::fillRounded (g, track.toFloat(), juce::Colours::white.withAlpha (0.07f), 7.0f);
    }
}

void MainView::resized()
{
    // ---- sidebar
    int y = 46 + 18;
    auto navRow = [&] (juce::Component& c) { c.setBounds (8, y, Dine::Metric::sidebar - 16, 28); y += 29; };
    navRow (setupsItem);
    y += 14 + 18;
    for (auto& item : setupItems) navRow (*item);
    y += 14 + 18;
    for (auto& item : mixItems) navRow (*item);

    // ---- toolbar
    auto toolbar = getLocalBounds().withTrimmedLeft (Dine::Metric::sidebar).removeFromTop (Dine::Metric::toolbar).reduced (14, 0);
    sessionButton->setBounds (toolbar.removeFromLeft (sessionButton->idealWidth()).withSizeKeepingCentre (sessionButton->idealWidth(), 38));
    auto right = toolbar;
    if (outputButton.isVisible())
    {
        const int w = juce::jlimit (120, 230, outputButton.idealWidth());
        outputButton.setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
        right.removeFromRight (10);
    }
    if (segMix.isVisible())
    {
        const int wA = juce::jmax (72, segAdvanced.idealWidth());
        const int wM = juce::jmax (52, segMix.idealWidth());
        auto seg = right.removeFromRight (wA + wM).withSizeKeepingCentre (wA + wM, Dine::Metric::control);
        segMix.setBounds (seg.removeFromLeft (wM));
        segAdvanced.setBounds (seg);
    }

    auto content = contentBounds();
    for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get(), (juce::Component*) purposePage.get(),
                                (juce::Component*) mixPage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (content);

    if (toast->isVisible())
    {
        const int w = toast->idealWidth();
        toast->setBounds (content.getCentreX() - w / 2, content.getBottom() - 22 - 30, w, 30);
        toast->toFront (false);
    }
}

} // namespace livemix
