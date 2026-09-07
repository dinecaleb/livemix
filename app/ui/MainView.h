#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "AppServices.h"
#include "SetupPages.h"
#include "MixPage.h"
#include "AdvancedPage.h"
#include "UI/LiveMixLookAndFeel.h"

namespace livemix
{

// The window content: a slim top bar (wordmark, where we are, the device) and one page
// at a time. Device -> Assign -> Purpose -> Mix, with Advanced one step aside. A 30 Hz
// timer polls the controller and refreshes the visible page; nothing here touches audio.
class MainView : public juce::Component, private juce::Timer
{
public:
    enum class Page { Device = 0, Assign, Purpose, Mix, Advanced };

    MainView (MixController&, AppServices&);
    ~MainView() override;

    void showPage (Page p);
    Page getPage() const noexcept { return page; }
    void showToast (const juce::String& text);

    DevicePage& getDevicePage() { return *devicePage; }
    AssignPage& getAssignPage() { return *assignPage; }
    PurposePage& getPurposePage() { return *purposePage; }
    MixPage& getMixPage() { return *mixPage; }
    AdvancedPage& getAdvancedPage() { return *advancedPage; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Toast;
    void timerCallback() override;
    void enterMix();

    MixController& controller;
    AppServices& services;
    LiveMixLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };
    Page page = Page::Device;

    std::unique_ptr<DevicePage> devicePage;
    std::unique_ptr<AssignPage> assignPage;
    std::unique_ptr<PurposePage> purposePage;
    std::unique_ptr<MixPage> mixPage;
    std::unique_ptr<AdvancedPage> advancedPage;
    std::unique_ptr<Toast> toast;
    FlatButton deviceButton { "DEVICE", FlatButton::Style::Ghost };
    FlatButton inputsButton { "INPUTS", FlatButton::Style::Ghost };
    int toastTicks = 0;
};

} // namespace livemix
