#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <set>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "MacroPad.h"
#include "UI/Widgets.h"
#include "Mix/MixMacros.h"

namespace livemix
{

class ReferenceSheet;

// TUNE, in three columns: every input on a rail down the left; the group buses with their
// faders and meters, the master's loudness row and the two macro pads in the middle - sized
// so nothing there ever scrolls; and down the right a panel of its own with the verbs
// (TUNE MIX, TUNE LIVE MIX, Reference, Mix Buddy, Undo / Redo), a card saying what the pad
// under the pointer does, and MIX HEALTH. While DINE listens, and again when the plan is
// ready, a sheet drops over the workspace.
class MixPage : public juce::Component
{
public:
    // The group row opened out to the effect returns (the FX tile's Each effect), or back.
    void showEffects (bool open);
    bool effectsShown() const noexcept { return effectsOpen; }

    explicit MixPage (MixController&);
    // What is arriving at a device input right now (dBFS), for the listen's "silent - nothing
    // arriving". Set by the window; unset, nothing is said about arrival.
    std::function<float (int deviceInput)> inputArriving;
    ~MixPage() override;

    std::function<void()> onOpenAdvanced;
    std::function<void (int strip)> onTuneStrip;      // TUNE CHANNEL, from the input rail
    std::function<void (const juce::String&)> onToast;
    std::function<void()> onOpenChat;                 // Mix Buddy, from the action column
    std::function<void()> onOpenHistory;              // MIX HISTORY: the whole mix as it was, by name
    std::function<void()> onOpenCheck;                // CHECK INPUTS: every assigned input and one word about it
    std::function<void()> onOpenFavourites;           // the favourite mixes a tune can be aimed at
    std::function<void (int strip)> onSelectStrip;    // a row on the rail was picked out
    std::function<void()> onGraphChanged;             // a voice changed job: the device follows the new graph
    int selectedStrip() const noexcept { return selectedRow; }

    void refresh();                    // 30 Hz: meters, stage, health

    void setRailShown (bool);
    bool isRailShown() const noexcept { return railShown; }
    void setRailAvailable (bool);
    bool isRailAvailable() const noexcept { return railAvailable; }
    void setSideShown (bool);          // the right panel: the verbs, the pad card, MIX HEALTH
    bool isSideShown() const noexcept { return sideShown; }

    void paint (juce::Graphics&) override;
    void resized() override;

    void openReference();
    // TUNE: the scope picker opens first. `pressTune` is what the button, the Mix menu and the
    // keyboard all call, so there is one way in and it always says what it is about to do.
    void pressTune();
    bool isScopeSheetOpen() const;
    void closeScopeSheet();
    // The snapshot tool and the reachability test: which scope the open picker is showing
    // (0 the whole mix, 1 one group, 2 some channels) and, for a group, which one (-1 = leave
    // it where it is). Nothing about the mix.
    void setScopeForSnapshot (int scope, int group);
    // Pick an input out of the rail, the way a click on its row does. -1 picks nothing out.
    void selectRow (int strip);
    void pressLiveTune();
    void setMacroValue (MixMacro m, float v);
    void centreMacroPads();            // both pads and the ribbon back to the plan, eased

    static constexpr int kSideW = 296;

private:
    class GroupTile;
    class VoiceRow;
    class ScopeSheet;
    class ListenSheet;
    class ResultSheet;
    class InputRow;
    class SidePanel;

    struct Layout
    {
        juce::Rectangle<int> groupsCaption, groups, master, macrosCaption, ribbon, rail, railTab, side, sideTab;
        std::array<juce::Rectangle<int>, 2> pads;
        int padSize = MacroPad::kMinPad;
        bool compact = false;
    };
    Layout layout() const;
    int railWidth() const noexcept
    {
        return ! railAvailable ? 0 : railSlide.width (Dine::Metric::panelTab, Dine::Metric::tuneRail);
    }
    int sideWidth() const noexcept { return sideSlide.width (Dine::Metric::panelTab, kSideW); }
    // Out: open, or on its way in or out - the panel's contents are laid out and drawn.
    bool railOut() const noexcept { return railShown || railSlide.isMoving(); }
    bool sideOut() const noexcept { return sideShown || sideSlide.isMoving(); }
    void refreshTuneButton();
    void refreshMaster();              // the loudness readout and the two pickers, a few times a second
    void rebuildRail();
    std::vector<int> railOrder;        // the strips in the rail's order: by family, in console order
    void syncMacros();                 // the pads and the ribbon read the controller
    void updateSide();                 // the pad card's words follow the pads
    void layoutSide();                 // the panel's content height, for its own scroll

    MixController& controller;
    // One tile per group bus, then the FX returns: DRUMS BASS MUSIC VOCALS SPEECH AMBIENCE FX.
    // ... then, behind the FX tile, one per effect return (shown instead of the groups while open).
    std::array<std::unique_ptr<GroupTile>, size_t (MixBus::Master) + 1 + size_t (FxSlot::Count)> groups;
    bool effectsOpen = false;
    DineButton backToGroups { "Back to groups", DineButton::Style::Ghost };
    // BODY x VOICE (bass across, vocals up) and DRIVE x ROOM (space across, drums up), then ENERGY on a ribbon.
    std::array<std::unique_ptr<MacroPad>, 2> pads;
    std::unique_ptr<MacroRibbon> ribbon;
    std::unique_ptr<ScopeSheet> scopeSheet;      // WHAT SHOULD DINE TUNE: the whole mix, one group, some channels
    std::unique_ptr<ListenSheet> listenSheet;
    std::unique_ptr<ResultSheet> resultSheet;
    std::unique_ptr<ReferenceSheet> referenceSheet;
    std::vector<std::unique_ptr<InputRow>> inputRows;
    juce::Viewport railView;
    juce::Component railHolder;
    std::unique_ptr<DinePanelTab> railTab;
    bool railShown = true, railAvailable = true;
    std::unique_ptr<SidePanel> side;
    juce::Viewport sideView;
    std::unique_ptr<DinePanelTab> sideTab;
    bool sideShown = true;
    // Both panels fold on the sidebar's slide; while they move, their contents keep their full
    // width and slide behind the edge, so nothing inside re-flows mid-motion.
    Dine::Slide railSlide { *this, [this] { resized(); repaint(); } };
    Dine::Slide sideSlide { *this, [this] { resized(); repaint(); } };
    DineButton tuneButton { "TUNE MIX", DineButton::Style::Filled };
    DineButton liveTuneButton { "TUNE LIVE MIX", DineButton::Style::Standard };
    DineButton referenceButton { "Match to reference", DineButton::Style::Standard };
    DineButton checkButton { "Check inputs", DineButton::Style::Standard };
    // WHAT TO TUNE: the whole mix, one group, or the channels picked out. The segment sets it;
    // TUNE MIX acts on it, and asks *which* group or channels when it has to.
    std::array<std::unique_ptr<DineButton>, 3> scopeTabs;
    int wantedScope = 0;
    // AIM AT: the favourite mix a later tune is fitted to, by name.
    DineButton aimButton { "Pick a favourite mix", DineButton::Style::Standard };
    std::vector<std::unique_ptr<VoiceRow>> voiceRows;
    int builtVoicesFor = -1;
    void rebuildVoices();
    DineButton chatButton { "Mix Buddy", DineButton::Style::Standard };
    DineButton undoButton { "Undo mix", DineButton::Style::Standard };
    DineButton redoButton { "Redo mix", DineButton::Style::Standard };
    DineButton historyButton { "Mix history", DineButton::Style::Standard };
    DineButton advancedButton { "Open the Inspector", DineButton::Style::Ghost };
    DineButton resetMacrosButton { "Centre both pads", DineButton::Style::Ghost };
    // MASTER: how loud the finished mix should be, one press to get there, and who it is for.
    DinePopup loudnessTargetButton;
    DineButton raiseButton { "Raise loudness to target", DineButton::Style::Standard };
    DinePopup voicingButton;
    juce::String masterNote;
    // The three numbers on the master's card, and what they mean.
    juce::String masterLevelText { "0.0 dB" }, masterLoudText { "not measured yet" }, masterPeakText;
    juce::String masterLoudBrief { "not yet" }, masterPeakBrief;   // the same numbers for a narrow card
    bool masterOnTarget = true, masterPeakOver = false;
    bool raisePossible = false;
    static constexpr int kMasterCardH = 66;
    int health = 0;
    struct PageLook
    {
        juce::String status, notes;
        int health = -1;
        MixController::Stage stage = MixController::Stage::Setup;
        int tunes = -1;
        bool hasReference = false, canUndo = false, canRedo = false;
        juce::String referenceName, masterNote, padCard;
        bool operator== (const PageLook& o) const
        {
            return status == o.status && notes == o.notes && health == o.health && stage == o.stage && tunes == o.tunes
                && hasReference == o.hasReference && referenceName == o.referenceName && canUndo == o.canUndo && canRedo == o.canRedo
                && masterNote == o.masterNote && padCard == o.padCard;
        }
        bool operator!= (const PageLook& o) const { return ! (*this == o); }
    };
    PageLook painted;
    int builtRailFor = -1;
    int selectedRow = -1;
    juce::String status;
    MixController::Stage lastStage = MixController::Stage::Setup;
    bool lastLiveRun = false;
    int adviceTicks = 0;
};

} // namespace livemix
