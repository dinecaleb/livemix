#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"
#include "SetupPages.h"
#include "RoutingPage.h"
#include "TracksPage.h"
#include "MixerPage.h"
#include "CheckSheet.h"
#include "HistorySheet.h"
#include "ChannelTuneSheet.h"
#include "ChatSheet.h"
#include "ThemeSheet.h"
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
// Top to bottom: a 52 px title row (the sidebar switch, the wordmark, the five workspace tabs
// in the middle, how many inputs and how many are set to record, TUNE LIVE MIX, MIX BUDDY),
// the 56 px toolbar (the transport in its pill with the clock in the middle, BYPASS / LIVE
// SAFE / the output on the right), then the body. The session's name and its popover left the
// title row on 2026-09-18: everything the popover offered is in the File and Help menus and
// in the sidebar, and the row is better spent on the tabs. A hairline separates every plane
// (title / toolbar / body, the sidebar, the rails, the chain foot, the status foot). Down the left of the body is the
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
    // The five workspaces, the library, and the four set-up sections that now live inside the
    // ROUTING workspace. `Routing` is not a page a window is ever on: it means "the routing
    // workspace, at whatever section it was left on", and showPage resolves it to one of the
    // four. Appended rather than reordered, because Cmd-1..5 and the tab row count on the
    // first nine.
    enum class Page { Sessions = 0, Device, Assign, Purpose, Tracks, Mixer, Tune, Live, Inspector,
                      Outputs, Maps, Routing };

    MainView (MixController&, AppServices&);
    ~MainView() override;

    void showPage (Page p);
    Page getPage() const noexcept { return page; }
    static bool isSetupPage (Page p) noexcept
    {
        return p == Page::Sessions || isRoutingPage (p);
    }
    // The sections of the ROUTING workspace. The library is not one of them: a list of
    // sessions is not routing.
    static bool isRoutingPage (Page p) noexcept
    {
        return p == Page::Device || p == Page::Assign || p == Page::Purpose
            || p == Page::Outputs || p == Page::Maps;
    }
    static RoutingPage::Section sectionForPage (Page) noexcept;
    static Page pageForSection (RoutingPage::Section) noexcept;
    void showToast (const juce::String& text);
    // A different session document is now open - from the library, or recovered after a crash:
    // every workspace was built for the last one.
    void sessionReplaced();

    // The macOS menu bar. The application attaches it; the headless snapshot tool does not.
    juce::MenuBarModel* getMenuModel();

    // Which command a key press asks for, as data rather than as a chain of ifs: 0 when the
    // key is not bound. `keyPressed` is this plus the two bindings that depend on what is
    // open (Escape closes a sheet) - so the whole shortcut table can be read, asserted by the
    // reachability test (app/Tests/ReachabilityTests.cpp) and listed to the user, without a
    // command actually being run. Nothing here has a side effect.
    static int commandForKey (const juce::KeyPress&, Page);

    // Which sheet is open, by the name the reachability test and the snapshot tool use:
    // "outputs", "check", "history", "appearance", "channel", "chat", or "" for none.
    juce::String openSheetName() const;

    // The sidebar folds to a named handle; a workspace's own panels fold from `[` and `]`.
    void setSidebarShown (bool);
    bool isSidebarShown() const noexcept { return sidebarShown; }
    bool isSoloBarShown() const;               // the reachability test and the snapshot tool
    void togglePanel (bool left);

    void openMixerWindow();
    void showTutorial();
    void closeTutorial();
    static void setAutoTutorial (bool);
    void showOutputs();                        // the ROUTING workspace, at its Outputs section
    void showHistory();                       // MIX HISTORY: the whole mix as it was, hours ago, by name
    void showCheck();                       // CHECK INPUTS: every assigned input, its level and one word about it
    // RESET MIX TO RAW: everything DLIVE decided, taken back. Asked out loud; never a one-way door.
    void resetMixToRaw();
    void updateChromeForSnapshot() { updateChrome(); }   // the snapshot tool: the toolbar re-reads the controller now
    void closeSheetsForSnapshot() { closeSheets(); }
    void showChat();
    void closeSheets();

    // Appearance: View > Appearance lists the themes and opens the sheet. The chosen theme is
    // applied before the pages are built and remembered on this Mac (ThemeStore); the headless
    // snapshot tool switches the stored choice off so every render starts from the design.
    void showThemes();
    void applyThemeNamed (const juce::String& name);
    // Text size: Standard / Large / Larger. The words grow, the console's geometry does not
    // (Dine::setTextScale); remembered on this Mac beside the theme.
    void applyTextSize (float scale, const juce::String& name);
    static void setStoredThemeUsed (bool);

    void tuneChannel (int strip, const MixController::ListenSettings& listen = MixController::channelListen());
    int selectedChannel() const;          // the channel the current workspace has picked out, or -1
    void setBypass (bool on);

    SessionsPage& getSessionsPage() { return *sessionsPage; }
    DevicePage& getDevicePage() { return *devicePage; }
    AssignPage& getAssignPage() { return *assignPage; }
    PurposePage& getPurposePage() { return *purposePage; }
    RoutingPage& getRoutingPage() { return *routingPage; }
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
    class Menu;
    class ToolbarToggle;
    static constexpr int kWordmarkW = 58;
    static constexpr int kCountsW = 190;     // "N inputs   N to record", at the right of the title row before the buttons
    static constexpr int kRequestsW = 380;   // the Mix Buddy panel down the right of the workspace   // "DLIVE" at the right end of the title row
    class SidebarButton;
    class MixerWindow;
    class StatusBar;
    class StateBar;
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
    // The set-up sections are one workspace now, reached deliberately. It hosts the three
    // pages above inside itself, and owns the outputs and the saved patches.
    std::unique_ptr<RoutingPage> routingPage;
    std::unique_ptr<TracksPage> tracksPage;
    std::unique_ptr<MixerPage> mixerPage;
    std::unique_ptr<CheckSheet> checkSheet;
    std::unique_ptr<HistorySheet> historySheet;
    std::unique_ptr<ThemeSheet> themeSheet;
    juce::StringArray themeMenuNames;      // the View > Appearance list, as it was last built
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
    std::unique_ptr<Menu> menu;
    std::unique_ptr<ToolbarToggle> bypassButton;
    std::unique_ptr<ToolbarToggle> dimButton, muteButton;   // the emergency keys: the broadcast down 20 dB, or silent
    std::unique_ptr<ToolbarToggle> liveSafeButton;
    std::unique_ptr<ToolbarToggle> chatButton;
    std::unique_ptr<ToolbarToggle> tuneLiveButton;   // TUNE LIVE MIX from any workspace, in the title row
    std::unique_ptr<SidebarButton> sidebarButton;
    std::unique_ptr<MixerWindow> mixerWindow;
    std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<StatusBar> statusBar;
    // Under the toolbar whenever anything is soloed or Autopilot is on, on every workspace,
    // and nowhere at all when neither is. See the class for why those two get a band.
    std::unique_ptr<StateBar> stateBar;
    void jumpToSoloed (const MixController::SoloedItem&);
    std::unique_ptr<ChainStrip> chainFoot;
    std::unique_ptr<Tutorial> tutorial;

    // One row of tabs: TRACKS MIXER TUNE LIVE INSPECTOR (Cmd-1..5, left to right).
    static constexpr int kWorkspaceTabs = 5;
    std::array<std::unique_ptr<WorkspaceTab>, kWorkspaceTabs> tabs;
    DinePopup outputButton;
    std::unique_ptr<juce::FileChooser> chooser;

    int saveTicks = 0, toastTicks = 0, slowTicks = 0;
    // What is written down follows the document's revision, not a call site: the tick notices
    // it has moved, and hands a snapshot to the autosave once it stops. A milestone - a tune
    // kept, a scene recalled, a new reference - does not wait. docs/SESSION-STATE.md §5.3.
    unsigned long long seenRevision = 0, seenMilestone = 0;
    bool audioWasRunning = false;
    bool tuningLiveWasOn = false;
    bool usingCloudMixEngineer = false;
    bool sidebarShown = true;
    int lastChannel = -1;               // the last channel picked out anywhere: what the chain foot reads
    float onAir = 0.0f;
};

} // namespace livemix
