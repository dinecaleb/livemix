#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// LIVE: the view for the service itself (v4). One health strip for the four things that
// matter while it is happening (is it recording, is it going out, is anything clipping, how
// much room the master has); the cue that is on and This cue / Groups / All / Alerts over the
// strips; and a rail with what is up next, the cues, what LIVE SAFE
// and Autopilot are doing, what needs attention, and the engineer's own listen at its foot.
class LivePage : public juce::Component
{
public:
    LivePage (MixController&, AppServices&);
    ~LivePage() override;

    std::function<void (const juce::String&)> onToast;
    std::function<void()> onLiveSafeChanged;
    std::function<void()> onToggleRecord;
    std::function<void()> onOpenHistory;      // MIX HISTORY: the mix as it was, by name
    std::function<void()> onOpenCheck;        // CHECK INPUTS, from the "Needs attention" card
    std::function<void (int)> onEditSetlist;  // the Setlist sheet, at a cue (-1: the one on now)

    // The sidebar's SETLIST row: the same, for the setlist on the rail.
    void focusSetlist();

    // THE SETLIST: Space on LIVE, the Up next card's button, and a click on a cue all land here.
    void goToNextCue();
    void goToCue (int index);

    // What the strips show: the groups on in this cue, every group, every input and effect,
    // or only the inputs that need attention.
    enum class View { ThisCue, Groups, All, Alerts };
    void setView (View);
    View getView() const noexcept { return view; }

    void refresh();                    // 30 Hz
    void rebuild();
    void paint (juce::Graphics&) override;
    void resized() override;

    // The effect returns, each on its own fader: the All view, scrolled to them; or back to the groups.
    void showEffects (bool open);
    bool effectsShown() const noexcept { return view == View::All; }

private:
    class GroupTile;
    class Link;
    class LevelLine;
    class SafeDetail;
    class CueList;

    struct Layout
    {
        juce::Rectangle<int> health, groupsHeader, viewTrack, strips, safe, autopilot, monitor,
                             modesA, modesB, output, speaking, priorityRow, shareRow, attention, upNext, setlist, empty;
        bool safeCompact = false, autopilotCompact = false;
    };
    Layout lay;                        // measured in resized(), and again when a card changes height
    int setlistFlash = 0;              // ... and its SETLIST row

    MixController& controller;
    AppServices& services;
    // One strip per group bus in the console's order, then the effects returns.
    std::array<std::unique_ptr<GroupTile>, size_t (MixBus::Master) + 1 + size_t (FxSlot::Count)> tiles;
    // ONE PER INPUT, for All and Alerts: a strip each, in a row that scrolls sideways.
    std::vector<std::unique_ptr<GroupTile>> inputTiles;
    std::unique_ptr<GroupTile> masterTile;   // the master's fader, at the end of the row in every view
    juce::Viewport scroller;
    juce::Component scrollHolder;
    void rebuildInputTiles();
    std::vector<int> shownTiles;       // what the row holds now: group tiles by index, inputs as 1000 + strip
    View view = View::ThisCue;
    std::array<std::unique_ptr<DineButton>, 4> viewTabs;
    bool anyEffects() const;
    std::vector<int> tilesFor (View) const;
    void refreshViewTabs();

    // THE SETLIST on the rail: Up next and its GO, and the list with Now and Next.
    std::unique_ptr<CueList> cueList;
    DineButton goButton { "Go", DineButton::Style::Filled };
    DineButton editSetlist { "Edit", DineButton::Style::Standard };
    DineButton clearCueButton { "Clear cue", DineButton::Style::Ghost };   // MixController::clearCue
    DineButton saveCueButton { "Save as cue", DineButton::Style::Ghost };  // MixController::saveMixAsCue


    std::unique_ptr<Link> safeLink, autopilotLink, checkLink;
    juce::StringArray attentionNow;    // re-read twice a second with the clipping count
    juce::StringArray attentionStripsNow;
    int attentionShown = 0;            // how many of them the rail has room for
    int attentionLines = 2;            // ... and how many lines of what to do each one may take, whole
    // SPEAKING MICS: speech priority and share the mics, the two things that move a level for
    // the spoken word, on LIVE where a service or a show is run - not only in the Mix menu.
    std::unique_ptr<Link> priorityLink, shareLink;
    juce::String priorityText() const;
    juce::String shareText() const;

    // The engineer's listen: MONITOR SOLO / SOLO IN PLACE, AFL / PFL, CLEAR SOLO, DIM, where
    // solo goes and how loud it is there.
    std::array<std::unique_ptr<DineButton>, 4> modes;
    DineButton clearSolo { "CLEAR SOLO", DineButton::Style::Standard };
    DineButton monitorDim { "DIM", DineButton::Style::Toggle };
    DinePopup soloDevice;                                // where solo goes: the device only the engineer hears
    std::unique_ptr<LevelLine> monitorLevel;

    struct Look
    {
        juce::String recording, recordingNote, output, outputNote, clipping, clippingNote, headroom, headroomNote,
                     monitorNote, autopilotSince;
        juce::StringArray autopilotLog;
        // v4's "Needs attention": "Snare Btm is clipping\tTurn its preamp down 6 dB at the console.\tc"
        // - what, the sentence, and c / w for the lamp (critical, warning).
        juce::StringArray attention;
        juce::StringArray attentionStrips;       // the strips the Alerts view shows, by number
        Setlist setlist;                         // what the cue header, Up next and the list read
        juce::StringArray cueScenes;             // what each cue is, in words ("Band", "Quiet moment")
        juce::StringArray cueLouder, cueSofter;  // who each cue puts up front, and who softer
        std::array<int, 4> counts {};            // This cue, Groups, All, Alerts
        bool isRecording = false, safe = false, running = false, anyClip = false, inPlace = false, routed = false,
             autopilotOn = false, autopilotMoved = false, priorityOn = false, shareOn = false;
        int speakingMics = 0;
        int soloCount = -1;
        float headroomDb = 0.0f;
        bool operator== (const Look& o) const
        {
            return recording == o.recording && recordingNote == o.recordingNote && output == o.output && outputNote == o.outputNote
                && clipping == o.clipping && clippingNote == o.clippingNote && headroom == o.headroom && headroomNote == o.headroomNote
                && monitorNote == o.monitorNote && autopilotSince == o.autopilotSince && autopilotLog == o.autopilotLog
                && attention == o.attention && attentionStrips == o.attentionStrips && setlist == o.setlist
                && cueScenes == o.cueScenes && cueLouder == o.cueLouder && cueSofter == o.cueSofter && counts == o.counts
                && isRecording == o.isRecording && safe == o.safe && running == o.running && anyClip == o.anyClip
                && inPlace == o.inPlace && routed == o.routed && autopilotOn == o.autopilotOn
                && autopilotMoved == o.autopilotMoved && soloCount == o.soloCount
                && priorityOn == o.priorityOn && shareOn == o.shareOn && speakingMics == o.speakingMics
                && std::abs (headroomDb - o.headroomDb) < 0.05f;
        }
        bool operator!= (const Look& o) const { return ! (*this == o); }
    };
    Look look;
    juce::String safeText() const;
    juce::String autopilotText() const;
    void refreshMonitor();
    void updateDiskNote();
    int diskTicks = 0, adviceTicks = 0;
    double secondsFree = 0.0;
    juce::String clipText, clipNote;
    bool anyClipping = false;
    size_t checkpointsSeen = size_t (-1);
    juce::StringArray autopilotLog;
    juce::String autopilotSince;
};

} // namespace livemix
