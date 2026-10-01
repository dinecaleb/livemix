#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "ChainEditor.h"
#include "UI/Widgets.h"

namespace livemix
{

// INSPECTOR: the engineer's drill-down, laid out the way a console is read.
//
// Left, the rail: every channel under its bus, with its level and what it is sitting at,
// and the engine's own state along the foot. In the middle the channel itself - its name,
// what it is, the meters either side of the chain, its level, and then the whole signal
// path as chips you can switch and pick from, with the stage you picked opened underneath
// as a device: what it is doing, drawn, and a knob for every number. Right, what TUNE MIX
// did and why, stage by stage, so a hand edit is always visible as a hand edit.
//
// Same state as the overview: switching views never changes the sound.
class AdvancedPage : public juce::Component
{
public:
    explicit AdvancedPage (MixController&);
    ~AdvancedPage() override;

    std::function<void()> onBack;
    std::function<void()> onRetune;           // the trail column's RE-TUNE: the toolbar's TUNE MIX
    std::function<void (int strip)> onTuneChannel;   // TUNE CHANNEL: this one source, listened to on its own
    // ADD A SOUND, from the sample stage: the window owns the chooser and the copying.
    std::function<void (RoleFamily)> onImportSample;

    void refresh();                           // 30 Hz
    void rebuild();                           // after the session / graph changed

    // The two side panels fold away, so the channel and its chain can have the whole
    // width when that is what you are working on. Nothing about the mix changes.
    void setRailShown (bool);                 // left: every channel under its bus
    // The window carries a channel list of its own beside every workspace now, so the
    // Inspector's is switched off entirely rather than folded to a handle: two lists of the
    // same channels, side by side, is worse than one.
    void setRailAvailable (bool);
    bool isRailAvailable() const noexcept { return railAvailable; }
    void setTrailShown (bool);                // right: what TUNE MIX did
    bool isRailShown() const noexcept  { return railShown; }
    bool isTrailShown() const noexcept { return trailShown; }
    void select (int strip);                  // programmatic (snapshot tool)
    int selectedStrip() const noexcept { return selection.isBus ? -1 : selection.strip; }
    void selectBus (MixBus bus);
    MixBus selectedBus() const noexcept { return selection.isBus ? selection.bus : MixBus::Count; }
    void selectStage (int index);             // ... and one stage of its chain
    // The chain this channel has, for the snapshot tool: one PNG per stage card is how the
    // fourteen stage frames of the design are checked.
    int numStages() const;
    juce::String stageName (int index) const;
    // SIMPLE / ADVANCED. Simple is the channel in five plain words, with 50 meaning "as TUNE
    // left it"; Advanced is the whole chain, stage by stage. It is a way of looking, never a
    // mode the mix is in: the sound is the same either way.
    void setSimpleView (bool);
    bool isSimpleView() const noexcept { return simpleView; }
    void revealHistory();                     // scroll the trail to the channel's HISTORY (a menu, the snapshot tool)
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SectionHeader;
    class Row;
    class Trail;
    class SimplePanel;
    struct Selection { bool isBus = false; int strip = -1; MixBus bus = MixBus::Master; };

    void showSelection();
    void paintHead (juce::Graphics&, juce::Rectangle<int>) const;
    void refreshKeys();
    juce::String tunedLabel() const;
    // One row of the channel's HISTORY, in the trail's words: what did it, when, how much,
    // and each change as "High-pass  80 Hz to 100 Hz".
    struct HistoryView
    {
        juce::String what, when, summary;
        juce::StringArray lines;
        bool operator== (const HistoryView& o) const { return what == o.what && when == o.when && summary == o.summary && lines == o.lines; }
    };
    std::vector<HistoryView> historyViews();
    int railWidth() const noexcept  { return ! railAvailable ? 0 : railShown ? kRailW : Dine::Metric::panelTab; }
    int trailWidth() const noexcept { return trailShown ? kTrailW : Dine::Metric::panelTab; }

    static constexpr int kRailW     = Dine::Metric::chanRail;   // the channel rail: 180, the design's
    static constexpr int kTrailW    = Dine::Metric::trail;      // what DINE did: 280
    static constexpr int kRailHeadH = 42;

    MixController& controller;
    Selection selection;
    std::vector<std::unique_ptr<juce::Component>> listItems; // headers + rows, layout order
    std::vector<Row*> rows;                  // only the selectable rows, for refresh/selection
    juce::Viewport viewport;
    juce::Component listHolder;
    std::unique_ptr<SimplePanel> simple;
    // The design's Simple / Advanced segment, and RE-TUNE, at the right of the channel head.
    DineSegmentRow viewTrack;
    DineButton simpleTab { "Simple", DineButton::Style::Segment };
    DineButton advancedTab { "Advanced", DineButton::Style::Segment };
    DineButton retuneButton { "RE-TUNE", DineButton::Style::Standard };
    // TUNE CHANNEL in Advanced, beside RE-TUNE: Simple's card has its own, and a channel
    // shown as its full chain was the one place the verb for it could not be found.
    DineButton tuneChannelButton { "TUNE CHANNEL", DineButton::Style::Filled };
    int headControlsLeft = 1 << 20;   // where the head's buttons begin: the name stops short of them
    bool headKeysWanted = false;      // MUTE / SOLO belong to this selection (not the master); resized() decides if they fit
    // MUTE and SOLO on the channel the Inspector has open. The design's head does not draw
    // them, and for a while this page did not carry them - but "is this one heard" and "is
    // this the only one I am hearing" are two of the states an engineer changes while looking
    // at a channel, and making them a journey to MIXER and back was wrong.
    DineButton muteButton { "MUTE", DineButton::Style::Standard };
    DineButton soloButton { "SOLO", DineButton::Style::Standard };
    bool simpleView = false;
    std::unique_ptr<ChainEditor> chain;
    std::unique_ptr<SignalPath> path;
    std::unique_ptr<Trail> trail;
    std::unique_ptr<DinePanelTab> railTab, trailTab;
    bool railShown = true, trailShown = true, railAvailable = true;
    int builtForStrips = -1;
    bool rebuilding = false;   // rebuild() selects, and a selection refreshes: never re-entered
    int historyCount = -1, historyStrip = -2;   // what the trail's HISTORY was last built from
    long long historyNewest = 0;
    // What this page's own paint last drew. A full repaint of the Inspector on a large
    // console costs more than a 30 Hz frame has, so a frame that would draw the same thing is
    // skipped; everything that really moves (meters, the chain, the trail) is a child
    // component that repaints itself.
    struct InspectorLook
    {
        bool isBus = false;
        MixBus bus = MixBus::Master;
        int strip = -2;
        int rowCount = -1;
        bool bypassed = false, prepared = false, rail = true, trail = true;
        int tunes = -1;
        bool operator== (const InspectorLook& o) const
        {
            return isBus == o.isBus && bus == o.bus && strip == o.strip && rowCount == o.rowCount
                && bypassed == o.bypassed && prepared == o.prepared && rail == o.rail && trail == o.trail
                && tunes == o.tunes;
        }
        bool operator!= (const InspectorLook& o) const { return ! (*this == o); }
    };
    InspectorLook painted;
};

} // namespace livemix
