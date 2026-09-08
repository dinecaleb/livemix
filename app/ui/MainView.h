#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"
#include "SetupPages.h"
#include "TracksPage.h"
#include "MixerPage.h"
#include "MixPage.h"
#include "LivePage.h"
#include "AdvancedPage.h"
#include "TransportBar.h"

namespace livemix
{

// The window: a vibrancy sidebar (Library / Set up / Workspace and the device's state), a
// unified toolbar (the session's name and its menu, the workspace tabs, the output), one
// page under it and the transport along the foot.
//
// Setup runs Device -> Inputs -> Purpose once; after that the four workspaces —
// TRACKS, MIXER, TUNE, LIVE — are four views of the same session, never four states.
class MainView : public juce::Component, private juce::Timer
{
public:
    enum class Page { Device = 0, Assign, Purpose, Tracks, Mixer, Tune, Live, Inspector };

    MainView (MixController&, AppServices&);
    ~MainView() override;

    void showPage (Page p);
    Page getPage() const noexcept { return page; }
    void showToast (const juce::String& text);
    void requestSave() { saveTicks = 30; }   // saved a second after the last change

    // The macOS menu bar. The application attaches it; the headless snapshot tool does not.
    juce::MenuBarModel* getMenuModel();

    void openMixerWindow();
    void setBypass (bool on);

    DevicePage& getDevicePage() { return *devicePage; }
    AssignPage& getAssignPage() { return *assignPage; }
    PurposePage& getPurposePage() { return *purposePage; }
    TracksPage& getTracksPage() { return *tracksPage; }
    MixPage& getMixPage() { return *mixPage; }
    MixerPage& getMixerPage() { return *mixerPage; }
    LivePage& getLivePage() { return *livePage; }
    AdvancedPage& getAdvancedPage() { return *advancedPage; }
    TransportBar& getTransportBar() { return *transportBar; }

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Toast;
    class SessionButton;
    class Menu;
    class ToolbarToggle;
    class MixerWindow;

    void timerCallback() override;
    void closeMixerWindow();
    void handleCommand (int id);
    void enterSession();
    void updateChrome();
    void saveAs();
    void saveNow();
    void newSession();
    void openSession();
    void sessionMenu();
    void chooseOutput();
    void importMultitrack();
    void exportMix (AppServices::ExportFormat format);
    void timelineChanged();
    bool liveSafeBlocks (const juce::String& what);
    juce::Rectangle<int> contentBounds() const;
    juce::Rectangle<int> transportBounds() const;

    MixController& controller;
    AppServices& services;
    DineLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };
    Page page = Page::Device;

    std::unique_ptr<DevicePage> devicePage;
    std::unique_ptr<AssignPage> assignPage;
    std::unique_ptr<PurposePage> purposePage;
    std::unique_ptr<TracksPage> tracksPage;
    std::unique_ptr<MixerPage> mixerPage;
    std::unique_ptr<MixPage> mixPage;
    std::unique_ptr<LivePage> livePage;
    std::unique_ptr<AdvancedPage> advancedPage;
    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<Toast> toast;
    std::unique_ptr<SessionButton> sessionButton;
    std::unique_ptr<Menu> menu;
    std::unique_ptr<ToolbarToggle> bypassButton;
    std::unique_ptr<MixerWindow> mixerWindow;

    // Sidebar: Sessions, then the three setup steps, then the workspaces.
    DineNavItem sessionsItem { "Sessions", Dine::Icon::List };
    std::array<std::unique_ptr<DineNavItem>, 3> setupItems;
    std::array<std::unique_ptr<DineNavItem>, 5> workspaceItems;

    static constexpr int kWorkspaceTabs = 4;
    std::array<std::unique_ptr<DineButton>, kWorkspaceTabs> tabs;
    DinePopup outputButton;
    std::unique_ptr<juce::FileChooser> chooser;

    int saveTicks = 0, toastTicks = 0;
    bool audioWasRunning = false;
};

} // namespace livemix
