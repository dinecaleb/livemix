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

namespace livemix
{

// The window: a vibrancy sidebar (Library / Set up / Workspace and the device's state), a
// unified toolbar (the session's name and its menu, the transport and its clock, the
// workspace tabs, the output) and one page under it, which ends in its own chain strip.
//
// Setup runs Device -> Inputs -> Purpose once; after that the four workspaces —
// TRACKS, MIXER, TUNE, LIVE — are four views of the same session, never four states.
class MainView : public juce::Component, private juce::Timer
{
public:
    enum class Page { Sessions = 0, Device, Assign, Purpose, Tracks, Mixer, Tune, Live, Inspector };

    MainView (MixController&, AppServices&);
    ~MainView() override;

    void showPage (Page p);
    Page getPage() const noexcept { return page; }
    void showToast (const juce::String& text);
    void requestSave() { saveTicks = 30; }   // saved a second after the last change

    // The macOS menu bar. The application attaches it; the headless snapshot tool does not.
    juce::MenuBarModel* getMenuModel();

    // Space: the sidebar and a workspace's own side panels fold away, so the middle - the
    // timeline, the console, the channel and its chain - can have the window when it needs it.
    void setSidebarShown (bool);
    bool isSidebarShown() const noexcept { return sidebarShown; }
    void togglePanel (bool left);            // the panel on that side of whatever page you are on

    void openMixerWindow();
    void showOutputs();                   // the Outputs sheet: where the sound leaves this Mac
    void showChat();                      // MIX CHAT: ask for a change in words (Mix > AI Mix Chat...)
    void closeSheets();                   // dismiss whatever sheet is over the workspace

    // TUNE CHANNEL: one source listened to and tuned on its own, from wherever it was
    // clicked. The sheet drops over the workspace you are on - the console keeps playing
    // behind it - and KEEP / REVERT decide what happens to that channel.
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

    void timerCallback() override;
    void closeMixerWindow();
    void handleCommand (int id);
    void enterSession();
    void updateChrome();
    void paintSidebar (juce::Graphics&);
    void saveAs();
    void saveNow();
    void newSession();
    void openSession();
    void sessionMenu();
    void chooseOutput();
    void importMultitrack();
    void exportMix (AppServices::ExportFormat format);
    bool exporting = false;              // one bounce at a time; two would race for the file
    void timelineChanged();
    bool liveSafeBlocks (const juce::String& what);
    juce::Rectangle<int> contentBounds() const;
    int sidebarWidth() const noexcept { return sidebarShown ? Dine::Metric::sidebar : 0; }
    juce::String panelName (bool left) const;   // what this page calls that panel ("" = it has none)
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
    // AI MIX CHAT. A sheet over whatever workspace you are on, so the console keeps playing
    // behind it and the answer lands on BEFORE / AFTER where everything else does.
    std::unique_ptr<ChatSheet> chatSheet;
    void openChat();
    // Input mappings: the patch, saved so a recurring setup is one click rather than
    // twenty-four. Applying one never routes audio to a channel the device does not have -
    // what is missing is shown and switched off instead (native/InputMapStore.h).
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
    std::unique_ptr<SidebarButton> sidebarButton;
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
    bool tuningLiveWasOn = false;
    // Who TUNE LIVE MIX asks what the mix should sound like. Off by default and only
    // offered when a key is configured: DLIVE's own mix engineer needs no network and no
    // account, so the cloud one is a choice the user makes, never a requirement.
    bool usingCloudMixEngineer = false;
    bool sidebarShown = true;
};

} // namespace livemix
