#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"
#include "SetupPages.h"
#include "MixPage.h"
#include "MixerPage.h"
#include "AdvancedPage.h"

namespace livemix
{

// The window: a vibrancy sidebar (Library / Set up / Mix and the device's state), a
// unified toolbar (the setup's name and its menu, Mix|Mixer|Advanced, the output) and one
// page under it. Device -> Inputs -> Purpose -> Mix, with Mixer and Advanced one step aside.
class MainView : public juce::Component, private juce::Timer
{
public:
    enum class Page { Device = 0, Assign, Purpose, Mix, Mixer, Advanced };

    MainView (MixController&, AppServices&);
    ~MainView() override;

    void showPage (Page p);
    Page getPage() const noexcept { return page; }
    void showToast (const juce::String& text);
    void requestSave() { saveTicks = 30; }   // saved a second after the last change

    DevicePage& getDevicePage() { return *devicePage; }
    AssignPage& getAssignPage() { return *assignPage; }
    PurposePage& getPurposePage() { return *purposePage; }
    MixPage& getMixPage() { return *mixPage; }
    MixerPage& getMixerPage() { return *mixerPage; }
    AdvancedPage& getAdvancedPage() { return *advancedPage; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Toast;
    class SessionButton;

    void timerCallback() override;
    void enterMix();
    void updateChrome();
    void saveAs();
    void saveNow();
    void openMix();
    void sessionMenu();
    void chooseOutput();
    void exportMix (AppServices::ExportFormat format);
    juce::Rectangle<int> contentBounds() const;

    MixController& controller;
    AppServices& services;
    DineLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };
    Page page = Page::Device;

    std::unique_ptr<DevicePage> devicePage;
    std::unique_ptr<AssignPage> assignPage;
    std::unique_ptr<PurposePage> purposePage;
    std::unique_ptr<MixPage> mixPage;
    std::unique_ptr<MixerPage> mixerPage;
    std::unique_ptr<AdvancedPage> advancedPage;
    std::unique_ptr<Toast> toast;
    std::unique_ptr<SessionButton> sessionButton;

    // Sidebar: Setups, then the three setup steps, then the three mix views.
    DineNavItem setupsItem { "Setups", Dine::Icon::List };
    std::array<std::unique_ptr<DineNavItem>, 3> setupItems;
    std::array<std::unique_ptr<DineNavItem>, 3> mixItems;

    DineButton segMix { "Mix", DineButton::Style::Segment };
    DineButton segMixer { "Mixer", DineButton::Style::Segment };
    DineButton segAdvanced { "Advanced", DineButton::Style::Segment };
    DinePopup outputButton;
    std::unique_ptr<juce::FileChooser> exportChooser;

    int saveTicks = 0, toastTicks = 0;
    bool audioWasRunning = false;
};

} // namespace livemix
