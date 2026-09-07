#include "MainView.h"

namespace livemix
{

class MainView::Toast : public juce::Component
{
public:
    void show (const juce::String& t) { text = t; setVisible (true); repaint(); }
    int idealWidth() const { return juce::jmin (620, int (LiveMixLookAndFeel::body (13.0f).getStringWidthFloat (text)) + 40); }
    void paint (juce::Graphics& g) override
    {
        LiveMixLookAndFeel::drawSurface (g, getLocalBounds().toFloat().reduced (0.5f), Tokens::toastBg, Tokens::hairStrong, Tokens::Radius::card);
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
    addAndMakeVisible (deviceButton);
    addAndMakeVisible (inputsButton);
    deviceButton.setFontPx (11.0f);
    inputsButton.setFontPx (11.0f);
    deviceButton.onClick = [this] { showPage (Page::Device); };
    inputsButton.onClick = [this] { showPage (Page::Assign); };

    devicePage->onContinue = [this]
    {
        // A returning session with assignments goes straight to the mix; a new one assigns first.
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

    showPage (Page::Device);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    controller.onMessage = nullptr;
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainView::enterMix()
{
    services.reconfigure();      // builds the graph for the assignments with the callback stopped
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
    const bool setup = p == Page::Device || p == Page::Assign || p == Page::Purpose;
    deviceButton.setVisible (! setup || p != Page::Device);
    inputsButton.setVisible (! setup);
    resized();
    repaint();
}

void MainView::showToast (const juce::String& text)
{
    toast->show (text);
    toastTicks = 30 * 5;
    resized();
}

void MainView::timerCallback()
{
    controller.poll();
    if (page == Page::Mix) mixPage->refresh();
    else if (page == Page::Advanced) advancedPage->refresh();
    if (toastTicks > 0 && --toastTicks == 0) toast->setVisible (false);
    repaint (0, 0, getWidth(), AppStyle::kTopBar);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Tokens::ground);
    auto bar = getLocalBounds().removeFromTop (AppStyle::kTopBar);
    g.setColour (Tokens::topBar);
    g.fillRect (bar);
    LiveMixLookAndFeel::drawHairline (g, bar.removeFromBottom (1).toFloat(), Tokens::hair);
    auto r = bar.reduced (AppStyle::kMargin, 0);
    g.setColour (Tokens::textHi);
    g.setFont (LiveMixLookAndFeel::condensed (18.0f, 700, 0.14f));
    g.drawText ("DINELIVE", r.removeFromLeft (110), juce::Justification::centredLeft);
    g.setColour (Tokens::textLow);
    g.setFont (LiveMixLookAndFeel::condensed (11.0f, 600, 0.08f));
    juce::String where;
    switch (page)
    {
        case Page::Device:   where = "SETUP  1 / 3"; break;
        case Page::Assign:   where = "SETUP  2 / 3"; break;
        case Page::Purpose:  where = "SETUP  3 / 3"; break;
        case Page::Mix:      where = juce::String (styleProfileName (controller.getSession().profile)).toUpperCase(); break;
        case Page::Advanced: where = "ADVANCED"; break;
    }
    g.drawText (where, r.removeFromLeft (200), juce::Justification::centredLeft);
    // Right: the device and its clock, plus dropouts if any.
    juce::String device;
    if (services.isAudioRunning())
    {
        device = services.currentInputDevice() + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + juce::String (services.sampleRate() / 1000.0, 1) + " kHz / " + juce::String (services.bufferSize());
        const int xruns = services.xrunCount();
        if (xruns > 0) device += "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + juce::String (xruns) + (xruns == 1 ? " dropout" : " dropouts");
        g.setColour (xruns > 0 ? Tokens::warn : Tokens::textLow);
    }
    else { device = "no audio device"; g.setColour (Tokens::textDim); }
    g.setFont (LiveMixLookAndFeel::mono (11.0f));
    auto right = r;
    if (deviceButton.isVisible()) right.removeFromRight (deviceButton.getWidth() + 8);
    if (inputsButton.isVisible()) right.removeFromRight (inputsButton.getWidth() + 8);
    g.drawText (device, right, juce::Justification::centredRight);
}

void MainView::resized()
{
    auto bounds = getLocalBounds();
    auto bar = bounds.removeFromTop (AppStyle::kTopBar).reduced (AppStyle::kMargin, 10);
    deviceButton.setBounds (bar.removeFromRight (74));
    bar.removeFromRight (8);
    inputsButton.setBounds (bar.removeFromRight (74));
    for (juce::Component* p : { (juce::Component*) devicePage.get(), (juce::Component*) assignPage.get(), (juce::Component*) purposePage.get(),
                                (juce::Component*) mixPage.get(), (juce::Component*) advancedPage.get() })
        p->setBounds (bounds);
    if (toast->isVisible())
    {
        const int w = toast->idealWidth();
        toast->setBounds (getLocalBounds().removeFromBottom (72).withSizeKeepingCentre (w, 44));
    }
}

} // namespace livemix
