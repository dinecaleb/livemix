#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"
#include "SetupPages.h"
#include "FavouritesPage.h"
#include "RoutingPage.h"
#include "TracksPage.h"
#include "MixerPage.h"
#include "CheckSheet.h"
#include "SetlistSheet.h"
#include "ReadySheet.h"
#include "HistorySheet.h"
#include "BroadcastReadinessSheet.h"
#include "ChannelTuneSheet.h"
#include "ChatSheet.h"
#include "ThemeSheet.h"
#include "ExportSheet.h"
#include "ChoiceSheet.h"
#include "EffectSheet.h"
#include "AboutSheet.h"
#include "MixPage.h"
#include "LivePage.h"
#include "AdvancedPage.h"
#include "TransportBar.h"
#include "ChainStrip.h"
#include "Tutorial.h"
#include "WorkspaceGuide.h"

namespace livemix
{

// The window, after the v4 design (docs/design/v4, "DLIVE v3.html" as handed over). It was
// the v3 Figma design before it; the layout below is v4's, and docs/design/v4/INVENTORY.md is
// the list of everything it must keep.
//
// v4: a sidebar CARD floating 8 pt in from the window, the window's own buttons at its top,
// hiding completely (no rail); a 60 pt toolbar beside it - the session button, the transport
// pill and clock, the readiness pill, the solo pill, then at the right TUNE LIVE MIX as a white
// pill, the broadcast keys in one pill, LIVE SAFE as a switch, the output pill and Mix Buddy;
// the workspace as a card with 14 pt corners; the chain strip as a card under it; and the status
// foot as one quiet line along the bottom. What follows is the v3 description, still true of
// what each part is for.
//
// ONE 52 px toolbar across the whole width. The window's own buttons sit inside it, then the
// sidebar switch; the transport well is at the left of the workspace column with the clock in
// it, and the solo pill beside it while anything is soloed. At the right, in order: TUNE LIVE
// MIX, a divider, the four broadcast keys (DIM, MUTE, BYPASS, AUTOPILOT), LIVE SAFE, the
// output picker and Mix Buddy. There are no workspace tabs: the sidebar is the navigation.
//
// Under it, down the left, the sidebar - Library, Workspace, Safety, Setup, with the audio
// device along its foot - which folds to a 52 px rail of the same icons rather than to a
// handle, so every workspace stays one click away. Beside it the workspace, then the
// picked-out channel's chain along a 44 px strip, then a 28 px status foot that always says
// what the engine, the disk, the recording and the broadcast are doing, and how many inputs
// are set to record.
//
// A workspace's own side panels (TUNE's inputs, the Inspector's channels and its WHAT DINE
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
                      Outputs, Maps, Routing, Favourites };

    MainView (MixController&, AppServices&);
    ~MainView() override;

    void showPage (Page p);
    Page getPage() const noexcept { return page; }
    static bool isSetupPage (Page p) noexcept
    {
        return p == Page::Sessions || p == Page::Favourites || p == Page::Purpose || isRoutingPage (p);
    }
    // The sections of the ROUTING workspace. The library is not one of them: a list of
    // sessions is not routing.
    static bool isRoutingPage (Page p) noexcept
    {
        return p == Page::Device || p == Page::Assign || p == Page::Outputs || p == Page::Maps;
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

    // The sidebar hides completely (v4); a workspace's own panels fold from `[` and `]`.
    void setSidebarShown (bool shown, bool automatic = false);
    // The application answers whether the Mac asks for less motion (Accessibility > Display >
    // Reduce motion). Left empty - the snapshot tool, the tests - nothing animates.
    std::function<bool()> prefersReducedMotion;
    bool isSidebarShown() const noexcept { return sidebarShown; }
    bool isSoloBarShown() const;
    bool isCuePillShown() const;               // the reachability test and the snapshot tool
    void togglePanel (bool left);

    void openMixerWindow();
    // LIVE and the INSPECTOR in windows of their own, the way the console is: the service desk
    // on a second screen, or one channel's settings beside the console. A second copy of the
    // page on the same mix, so both stay true; closing the window loses nothing.
    void openLiveWindow();
    void openInspectorWindow();
    // A channel or group to look at: the Inspector's window when it is open, else the page.
    void inspectStrip (int strip);
    void inspectBus (MixBus bus);
    void showTutorial();
    void closeTutorial();
    // The one-card guide a workspace shows the first time it is opened. `maybeShowGuide` is
    // what `showPage` calls; it does nothing when that workspace has been dismissed or when
    // the guides are switched off.
    void maybeShowGuide();
    static void setAutoTutorial (bool);
    void showOutputs();                        // the ROUTING workspace, at its Outputs section
    void showHistory();                       // MIX HISTORY: the whole mix as it was, hours ago, by name
    void showCheck();                       // CHECK INPUTS: every assigned input, its level and one word about it
    void showBroadcastReadiness (bool history = false);  // optional livestream checklist
    // RESET MIX TO RAW: everything DINE decided, taken back. Asked out loud; never a one-way door.
    void resetMixToRaw();
    void updateChromeForSnapshot() { updateChrome(); }   // the snapshot tool: the toolbar re-reads the controller now
    void closeSheetsForSnapshot() { closeSheets(); }
    void exportMixForSnapshot() { exportMix (AppServices::ExportFormat::Wav); }
    // The snapshot tool and the tests: an export shown in the status foot as if one were under way.
    void showExportProgressForSnapshot (ExportProgress::State st, MixBounce::Stage stage, float fraction);
    // EXPORT, while it runs. Quitting asks first, then stops it and waits for the worker so
    // nothing it uses is destroyed under it (Main.cpp systemRequestedQuit / shutdown).
    bool isExporting() const noexcept { return exportRun != nullptr && exportRun->workerBusy.load(); }
    bool stopExportAndWait (int timeoutMs);
    void exportMultitrackForSnapshot() { exportMix (AppServices::ExportFormat::Wav, AppServices::ExportWhat::RawMultitrack); }
    void showChat();
    // A button in a Mix Buddy answer. Everything here is navigation, a listen in the engineer's
    // own headphones, or the start of a TUNE that ends on BEFORE / AFTER - never a change kept.
    void performBuddyAction (const BuddyAction&);
    void closeSheets();
    bool closeTopSheet();
    // THE SETLIST sheet, at a cue (-1: the one on now). LIVE's Edit and the Setlist row's menu.
    void showSetlist (int cue = -1);
    // READY TO GO LIVE? - the readiness pill's sheet (ReadyCheck: read-only, every row a Fix).
    void showReady();
    // RECOVER SESSION? DINE did not close cleanly and there is unsaved work beside the
    // document. The two are compared side by side and nothing is deleted by any of the three
    // answers; the application hands the facts in, because it is the thing that found them.
    struct RecoveryOffer
    {
        juce::String sentence;
        juce::String autosaveWhen, documentWhen;
        juce::StringArray autosaveFacts, documentFacts;
        std::function<void()> onRecover, onOpenSaved, onKeepBoth;
    };
    void offerRecovery (RecoveryOffer);

    // macOS IS ABOUT TO ASK ABOUT THE MICROPHONE. DINE reads a console; macOS calls every
    // audio input a microphone and puts its own prompt up the moment a process starts
    // listening - which, on a restored session, is a second after launch and before anybody
    // has asked for anything. This says what it is for first, in DINE's words, and the answer
    // is honoured: Not now opens the output alone and the session still opens. The application
    // hands the device in, because it is the thing that knows what is about to be opened.
    struct MicrophoneAsk
    {
        juce::String device;                                // the console the session is opening
        std::function<void()> onContinue, onNotNow;
    };
    void explainMicrophone (MicrophoneAsk);
    // The input is waiting for macOS (AudioHost::inputHeldBack): open it the moment macOS says
    // yes, and - once per run, when a session opened without asking (a recovery, the library,
    // a late console) - say what is about to be asked before asking it. MainView's slow tick.
    void followMicrophone();

    // Appearance: View > Appearance lists the themes and opens the sheet. The chosen theme is
    // applied before the pages are built and remembered on this Mac (ThemeStore); the headless
    // snapshot tool switches the stored choice off so every render starts from the design.
    void showThemes();
    // One effect return's sound by hand (EffectSheet): from its name on the console.
    void showEffect (FxSlot);
    void showAbout();                  // DINE > About DINE: the identity's splash
    void applyThemeNamed (const juce::String& name);
    // Text size: Standard / Large / Larger. The words grow, the console's geometry does not
    // (Dine::setTextScale); remembered on this Mac beside the theme.
    void applyTextSize (float scale, const juce::String& name);
    static void setStoredThemeUsed (bool);
    // The workspace guides, off in the headless tool so a render is not a walk through what
    // this Mac has already dismissed. On by default. `preferences` points the memory at a
    // file of its own - the tool renders the card from an empty one, so the same PNG comes
    // out on a machine where every guide has already been dismissed.
    static void setGuidesUsed (bool on, juce::File preferences = {});

    void tuneChannel (int strip, const MixController::ListenSettings& listen = MixController::channelListen());
    int selectedChannel() const;          // the channel the current workspace has picked out, or -1
    void setBypass (bool on);

    SessionsPage& getSessionsPage() { return *sessionsPage; }
    FavouritesPage& getFavouritesPage() { return *favouritesPage; }
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

    // The empty toolbar is the window's title bar: a press there moves the window and a
    // double-click does what macOS says a title-bar double-click does. The application
    // answers these (app/native/WindowChrome.mm); a MainView with no window leaves them empty.
    std::function<void()> onToolbarPressed, onToolbarDoubleClicked;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    class Toast;
    class Menu;
    class ToolbarToggle;
    class SoloPill;
    class CuePill;
    static constexpr int kRequestsW = 380;   // the Mix Buddy panel down the right of the workspace
    // What macOS draws at the left of the toolbar: three 12 pt buttons at 16 / 36 / 56, so the
    // first thing this window may put there starts at 86 (design: `Toolbar v3.4`).
    static constexpr int kTrafficLights = 77;   // v4: the buttons at 25 / 45 / 65, ending at 77
    // Where the toolbar's own left cluster ends: the window buttons, the sidebar switch and the
    // wordmark. Nothing else in the row may start before it, whatever the sidebar is doing.
    static constexpr int kToolbarLeft   = 224;
    class SidebarButton;
    class SessionButton;
    class ReadyPill;
    class OutputPill;
    class MixerWindow;
    class PageWindow;
    class StatusBar;
    class Sidebar;
    class TextButtonV2;
    class PerfOverlay;
    std::unique_ptr<PerfOverlay> perfOverlay;   // Cmd-Option-P, Debug or DINE_PERF_HUD=1

    void timerCallback() override;
    void tickBackground();                    // what never stops: poll, autosave, the microphone
    void tickFrame (bool mainShowing);        // what is drawn, at most 30 times a second
    void onVBlank();
    juce::uint32 lastFrameMs = 0, lastVBlankMs = 0;
    int frameTicks = 0;
    std::unique_ptr<juce::VBlankAttachment> frameClock;
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
    void importMultitrack (bool newSessionFirst = false);   // the chooser: files, folders, several
    void importMultitrackFolder (const juce::File&);
    void importAudio (const juce::Array<juce::File>&, bool newSessionFirst);   // every import lands here
    void refreshSoloPill();
    void exportMix (AppServices::ExportFormat format, AppServices::ExportWhat what = AppServices::ExportWhat::StereoMix);
    // The export under way, or the last one, until its result has been read off the status foot.
    // Shared with the worker, which writes it and holds it until it has finished.
    std::shared_ptr<ExportProgress> exportRun;
    MixBounce::What exportWhat = MixBounce::What::StereoMix;
    juce::File exportShown;                 // what "Show in Finder" opens once it is done
    juce::String exportError;               // what went wrong, said again when the cell is clicked
    int exportDoneTicks = 0;                // "Export done" stays this long, then the cell goes
    void exportCellClicked();
public:
    // UNDO: which history Cmd+Z reaches from here, and what it would take back (MainView.cpp).
    enum class UndoDomain { None, Mix, Timeline };
    struct UndoStep { UndoDomain domain = UndoDomain::None; juce::String label; };
    UndoStep undoTarget() const;
    UndoStep redoTarget() const;
    void undoHere();
    void redoHere();
private:
    UndoDomain lastUndone = UndoDomain::None;
    juce::String drumKitName() const;
    void drumKitMenu (juce::Component& anchor);
    bool micExplained = false;              // the microphone sheet has been shown this run
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
    std::unique_ptr<FavouritesPage> favouritesPage;
    std::unique_ptr<DevicePage> devicePage;
    std::unique_ptr<AssignPage> assignPage;
    std::unique_ptr<PurposePage> purposePage;
    // The set-up sections are one workspace now, reached deliberately. It hosts the three
    // pages above inside itself, and owns the outputs and the saved patches.
    std::unique_ptr<RoutingPage> routingPage;
    std::unique_ptr<TracksPage> tracksPage;
    std::unique_ptr<MixerPage> mixerPage;
    std::unique_ptr<CheckSheet> checkSheet;
    std::unique_ptr<SetlistSheet> setlistSheet;
    std::unique_ptr<ReadySheet> readySheet;
    std::unique_ptr<HistorySheet> historySheet;
    std::unique_ptr<BroadcastReadinessSheet> readinessSheet;
    std::unique_ptr<ThemeSheet> themeSheet;
    std::unique_ptr<ExportSheet> exportSheet;
    // RESET THE MIX TO RAW and RECOVER SESSION?: the two questions DINE asks out loud.
    std::unique_ptr<ChoiceSheet> choiceSheet;
    std::unique_ptr<EffectSheet> effectSheet;
    std::unique_ptr<AboutSheet> aboutSheet;
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
    std::unique_ptr<PageWindow> liveWindow, inspectorWindow;
    LivePage* liveInWindow = nullptr;          // the pages those windows hold, while they are open
    AdvancedPage* inspectorInWindow = nullptr;
    void closePageWindow (std::unique_ptr<PageWindow>& w);
    // Every page that lives in a window of its own, rebuilt when the session underneath them changes.
    void rebuildWindows();
    std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<StatusBar> statusBar;
    // Beside the clock whenever anything is soloed, on every workspace, and nowhere at all
    // when nothing is. See the class for why solo gets a permanent place in the chrome.
    std::unique_ptr<SoloPill> soloPill;
    std::unique_ptr<CuePill> cuePill;
    std::unique_ptr<ToolbarToggle> autopilotButton;
    void jumpToSoloed (const MixController::SoloedItem&);
    std::unique_ptr<ChainStrip> chainFoot;
    std::unique_ptr<Tutorial> tutorial;
    std::unique_ptr<WorkspaceGuide> guide;

    std::unique_ptr<OutputPill> outputButton;
    std::unique_ptr<SessionButton> sessionButton;
    std::unique_ptr<ReadyPill> readyPill;
    int inputsNeedingAttention() const;
    void readyPillClicked();
    std::unique_ptr<juce::FileChooser> chooser;

    int saveTicks = 0, toastTicks = 0, slowTicks = 0;
    bool saidAutosaveFailing = false;       // the toast is said once per failure, not every tick
    juce::uint32 stopAskedAt = 0;            // a stop of a running take was asked for once (see case 500/501)
    // What is written down follows the document's revision, not a call site: the tick notices
    // it has moved, and hands a snapshot to the autosave once it stops. A milestone - a tune
    // kept, a scene recalled, a new reference - does not wait. docs/SESSION-STATE.md §5.3.
    unsigned long long seenRevision = 0, seenMilestone = 0;
    bool audioWasRunning = false;
    bool tuningLiveWasOn = false;
    bool usingCloudMixEngineer = false;
    bool sidebarShown = true;
    // The sidebar's slide (v4): 1 out, 0 gone, eased over 420 ms on the display's own clock
    // while it moves and not at all otherwise. Going to LIVE folds it away by itself, and
    // leaving LIVE brings it back only if that is what folded it.
    float sidebarReveal = 1.0f, revealFrom = 1.0f;
    double revealStartMs = 0.0;
    std::unique_ptr<juce::VBlankAttachment> sidebarClock;
    // UI preferences (C3): what this Mac's layout was, read at start and written when it changes.
    juce::NamedValueSet uiState() const;
    void applyUiPrefs();
    juce::NamedValueSet uiSaved;
    void stepSidebar();
    int columnLeft() const noexcept;
    juce::Rectangle<int> workspaceCard() const;
    void paintOverChildren (juce::Graphics&) override;
    int lastChannel = -1;               // the last channel picked out anywhere: what the chain foot reads
    int dividerX = 0;                   // where the toolbar's one divider was last laid out
    juce::Rectangle<int> broadcastPill; // the one pill DIM, MUTE, BYPASS and Auto sit in
    static constexpr int kBypassBanner = 42;   // the white BYPASS bar under the toolbar, and its gap
    float onAir = 0.0f;
};

} // namespace livemix
