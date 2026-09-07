#include "MainView.h"
#include "UI/LiveMixLookAndFeel.h"

namespace livemix
{

class MainView::Toast : public juce::Component
{
public:
    void show (const juce::String& t) { text = t; setVisible (true); repaint(); }
    int idealWidth() const
    {
        return juce::jmin (520, int (juce::GlyphArrangement::getStringWidth (LiveMixLookAndFeel::body (13.0f), text)) + 40);
    }
    void paint (juce::Graphics& g) override
    {
        LiveMixLookAndFeel::drawElevated (g, getLocalBounds().toFloat().reduced (0.5f), Tokens::toastBg, Tokens::hairStrong, Tokens::Radius::card);
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::body (13.0f));
        g.drawFittedText (text, getLocalBounds().reduced (16, 8), juce::Justification::centredLeft, 2);
    }
private:
    juce::String text;
};

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
    for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get(), (juce::Component*) purposePage.get(),
                                (juce::Component*) mixPage.get(), (juce::Component*) advancedPage.get() })
        addChildComponent (*p);
    addChildComponent (*toast);

    for (auto* b : { &sessionButton, &outputButton, &saveButton, &openButton, &deviceButton, &inputsButton })
    {
        addAndMakeVisible (*b);
        b->setFontPx (11.5f);
        b->setSpacing (0.04f);
    }
    sessionButton.setChevron (true);
    outputButton.setChevron (true);

    deviceButton.onClick = [this] { showPage (Page::Device); };
    inputsButton.onClick = [this] { showPage (Page::Assign); };
    saveButton.onClick = [this] { saveAs(); };
    openButton.onClick = [this] { openMix(); };
    outputButton.onClick = [this] { chooseOutput(); };
    sessionButton.onClick = [this] { saveAs(); }; // rename via Save As

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
    const bool setup = page == Page::Device || page == Page::Assign || page == Page::Purpose;
    const bool mixing = page == Page::Mix || page == Page::Advanced;
    deviceButton.setVisible (! setup || page != Page::Device);
    inputsButton.setVisible (! setup);
    saveButton.setVisible (mixing);
    openButton.setVisible (true);
    outputButton.setVisible (services.isAudioRunning() || mixing);
    sessionButton.setVisible (mixing || ! controller.getSession().inputs.empty());

    juce::String name = services.currentSessionName();
    if (name.isEmpty()) name = "Untitled";
    sessionButton.setButtonText (name.toUpperCase());

    juce::String out = services.currentOutputDevice();
    if (out.isEmpty()) out = "Output";
    if (out.length() > 22) out = out.substring (0, 20) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    outputButton.setButtonText (out);
}

void MainView::showToast (const juce::String& text)
{
    toast->show (text);
    toastTicks = 30 * 5;
    resized();
}

void MainView::saveAs()
{
    auto* alert = new juce::AlertWindow ("Save mix", "Name this mix. It is stored on this Mac and can be opened later.", juce::MessageBoxIconType::NoIcon);
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
        showToast ("No saved mixes yet. Save one from the mix screen.");
        return;
    }
    for (int i = 0; i < listed.size(); ++i)
        menu.addItem (i + 1, listed[i].name + "   " + listed[i].modified.formatted ("%d %b"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&openButton),
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

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton),
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
    if (audioWasRunning && ! running && services.deviceStopped())
        showToast ("The audio device stopped. Check its connection, then choose it again on DEVICE.");
    audioWasRunning = running;
    repaint (0, 0, getWidth(), AppStyle::kTopBar);
}

void MainView::paint (juce::Graphics& g)
{
    LiveMixLookAndFeel::drawAmbient (g, getLocalBounds().toFloat());

    auto bar = getLocalBounds().removeFromTop (AppStyle::kTopBar);
    g.setColour (Tokens::topBar.withAlpha (0.92f));
    g.fillRect (bar);
    // Soft jade wash under the wordmark.
    juce::ColourGradient brand (Tokens::accentDim.withAlpha (0.35f), float (AppStyle::kMargin), 0.0f,
                                Tokens::topBar.withAlpha (0.0f), float (AppStyle::kMargin + 220), float (bar.getHeight()), false);
    g.setGradientFill (brand);
    g.fillRect (bar);
    LiveMixLookAndFeel::drawHairline (g, bar.removeFromBottom (1).toFloat(), Tokens::hair);

    auto r = getLocalBounds().removeFromTop (AppStyle::kTopBar).reduced (AppStyle::kMargin, 0);
    g.setColour (Tokens::textHi);
    g.setFont (LiveMixLookAndFeel::condensed (19.0f, 700, 0.16f));
    g.drawText ("DINELIVE", r.removeFromLeft (118), juce::Justification::centredLeft);

    g.setColour (Tokens::textLow);
    g.setFont (LiveMixLookAndFeel::condensed (11.0f, 600, 0.1f));
    juce::String where;
    switch (page)
    {
        case Page::Device:   where = "SETUP  1 / 3"; break;
        case Page::Assign:   where = "SETUP  2 / 3"; break;
        case Page::Purpose:  where = "SETUP  3 / 3"; break;
        case Page::Mix:      where = juce::String (styleProfileName (controller.getSession().profile)).toUpperCase(); break;
        case Page::Advanced: where = "ADVANCED"; break;
    }
    g.drawText (where, r.removeFromLeft (160), juce::Justification::centredLeft);

    if (services.isAudioRunning())
    {
        g.setColour (services.xrunCount() > 0 ? Tokens::warn : Tokens::textDim);
        g.setFont (LiveMixLookAndFeel::mono (10.5f));
        auto mid = r;
        // Leave room for chrome buttons on the right.
        mid.removeFromRight (520);
        g.drawText (juce::String (services.sampleRate() / 1000.0, 1) + " kHz / " + juce::String (services.bufferSize())
                        + (services.xrunCount() > 0 ? "  ·  " + juce::String (services.xrunCount()) + " dropouts" : juce::String()),
                    mid, juce::Justification::centredLeft);
    }
}

void MainView::resized()
{
    auto bounds = getLocalBounds();
    auto bar = bounds.removeFromTop (AppStyle::kTopBar).reduced (AppStyle::kMargin, 12);
    auto place = [&] (FlatButton& b, int minW)
    {
        const int w = juce::jmax (minW, b.getIdealWidth());
        b.setBounds (bar.removeFromRight (w));
        bar.removeFromRight (6);
    };
    place (deviceButton, 70);
    place (inputsButton, 70);
    place (openButton, 60);
    place (saveButton, 60);
    place (outputButton, 100);
    place (sessionButton, 90);

    for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get(), (juce::Component*) purposePage.get(),
                                (juce::Component*) mixPage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (bounds);

    if (toast->isVisible())
    {
        const int w = toast->idealWidth();
        auto area = getLocalBounds().withTrimmedTop (AppStyle::kTopBar + 10).removeFromTop (40);
        area.removeFromRight (AppStyle::kMargin);
        toast->setBounds (area.removeFromRight (w));
    }
}

} // namespace livemix
