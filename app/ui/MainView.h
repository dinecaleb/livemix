#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"
#include "SetupPages.h"
#include "TracksPage.h"
#include "MixerPage.h"
#include "OutputsSheet.h"
#include "ChannelTuneSheet.h"
#include "ChatSheet.h"
#include "MixPage.h"
#include "LivePage.h"
#include "AdvancedPage.h"
#include "TransportBar.h"
#include "ChainStrip.h"
#include "Tutorial.h"

namespace livemix
{

// The window, after the v2 design (DLIVE Desktop v2, 2026-09-17).
//
// Top to bottom: a 52 px title row (the sidebar switch, the session's name with its popover,
// how many inputs and how many are set to record, AI MIX CHAT), the 56 px toolbar (the
// transport in its pill with the clock, the five workspace tabs in the middle, BYPASS /
// LIVE SAFE / the output on the right), then the body. Down the left of the body is the
// sidebar - LIBRARY, SET-UP and WORKSPACE, with the device along its foot - which folds to a
// 17 px handle; beside it the workspace, then the picked-out channel's chain along a 48 px
// strip, then a 50 px status foot that always says what the engine, the disk, the recording,
// the broadcast and the engineer's own ears are doing.
//
// A workspace's own side panels (TUNE's inputs, the Inspector's channels and its WHAT DLIVE
// DID column) belong to the workspace, not to the window, and fold with `[` and `]`.
class MainView : public juce::Component, private juce::Timer
{
public:
    enum class Page { Sessions = 0, Device, Assign, Purpose, Tracks, Mixer, Tune, Live, Inspector };

    MainView (MixController&, AppServices&);
    ~MainView() override;

    void showPage (Page p);
    Page getPage() const noexcept { return page; }
    static bool isSetupPage (Page p) noexcept
    {
        return p == Page::Sessions || p == Page::Device || p == Page::Assign || p == Page::Purpose;
    }
    void showToast (const juce::String& text);
    void requestSave() { saveTicks = 30; }   // saved a second after the last change

    // The macOS menu bar. The application attaches it; the headless snapshot tool does not.
    juce::MenuBarModel* getMenuModel();

    // The sidebar folds to a named handle; a workspace's own panels fold from `[` and `]`.
    void setSidebarShown (bool);
    bool isSidebarShown() const noexcept { return sidebarShown; }
    void togglePanel (bool left);

    void openMixerWindow();
    void showTutorial();
    void closeTutorial();
    static void setAutoTutorial (bool);
    void showOutputs();
    void showChat();
    void closeSheets();

    void tuneChannel (int strip, const MixController::ListenSettings& listen = MixController::channelListen());
    int selectedChannel() const;          // the channel the current workspace has picked out, or -1
    void setBypass (bool on);

    SessionsPage& getSessionsPage() { return *sessionsPage; }
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
    class SidebarButton;
    class MixerWindow;
    class StatusBar;
    class Sidebar;
    class TextButtonV2;
    class WorkspaceTab;

    void timerCallback() override;
    void closeMixerWindow();
    void handleCommand (int id);
    void enterSession();
    void updateChrome();
    void updateChainFoot();
    void setupPopover();
    juce::Rectangle<int> spotlight (const juce::String& what) const;
    void saveAs();
    void saveNow();
    void newSession();
    void openSession();
    void sessionMenu();
    void chooseOutput();
    void importMultitrack();
    void exportMix (AppServices::ExportFormat format);
    bool exporting = false;
    void timelineChanged();
    bool liveSafeBlocks (const juce::String& what);
    juce::Rectangle<int> contentBounds() const;      // the workspace, under the toolbar and beside the sidebar
    juce::Rectangle<int> columnBounds() const;       // the workspace column: workspace + chain foot + status
    juce::String panelName (bool left) const;
    bool panelShown (bool left) const;

    MixController& controller;
    AppServices& services;
    DineLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };
    Page page = Page::Sessions;

    std::unique_ptr<SessionsPage> sessionsPage;
    std::unique_ptr<DevicePage> devicePage;
    std::unique_ptr<AssignPage> assignPage;
    std::unique_ptr<PurposePage> purposePage;
    std::unique_ptr<TracksPage> tracksPage;
    std::unique_ptr<MixerPage> mixerPage;
    std::unique_ptr<OutputsSheet> outputsSheet;
    std::unique_ptr<ChannelTuneSheet> channelSheet;
    std::unique_ptr<ChatSheet> chatSheet;
    void openChat();
    void saveInputMapping();
    void openInputMappings();
    void applyInputMapping (const juce::File&);
    std::unique_ptr<juce::FileChooser> mapChooser;
    std::unique_ptr<juce::AlertWindow> mapDialog;
    std::unique_ptr<MixPage> mixPage;
    std::unique_ptr<LivePage> livePage;
    std::unique_ptr<AdvancedPage> advancedPage;
    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<Toast> toast;
    std::unique_ptr<SessionButton> sessionButton;
    std::unique_ptr<Menu> menu;
    std::unique_ptr<ToolbarToggle> bypassButton;
    std::unique_ptr<ToolbarToggle> liveSafeButton;
    std::unique_ptr<ToolbarToggle> chatButton;
    std::unique_ptr<SidebarButton> sidebarButton;
    std::unique_ptr<MixerWindow> mixerWindow;
    std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<StatusBar> statusBar;
    std::unique_ptr<ChainStrip> chainFoot;
    std::unique_ptr<Tutorial> tutorial;

    // One row of tabs: TRACKS MIXER TUNE LIVE INSPECTOR (Cmd-1..5, left to right).
    static constexpr int kWorkspaceTabs = 5;
    std::array<std::unique_ptr<WorkspaceTab>, kWorkspaceTabs> tabs;
    DinePopup outputButton;
    std::unique_ptr<juce::FileChooser> chooser;

    int saveTicks = 0, toastTicks = 0, slowTicks = 0;
    bool audioWasRunning = false;
    bool tuningLiveWasOn = false;
    bool usingCloudMixEngineer = false;
    bool sidebarShown = true;
    int lastChannel = -1;               // the last channel picked out anywhere: what the chain foot reads
    float onAir = 0.0f;
};

} // namespace livemix
