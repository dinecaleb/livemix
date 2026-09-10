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

    void refresh();                           // 30 Hz
    void rebuild();                           // after the session / graph changed

    // The two side panels fold away, so the channel and its chain can have the whole
    // width when that is what you are working on. Nothing about the mix changes.
    void setRailShown (bool);                 // left: every channel under its bus
    void setTrailShown (bool);                // right: what TUNE MIX did
    bool isRailShown() const noexcept  { return railShown; }
    bool isTrailShown() const noexcept { return trailShown; }
    void select (int strip);                  // programmatic (snapshot tool)
    int selectedStrip() const noexcept { return selection.isBus ? -1 : selection.strip; }
    void selectBus (MixBus bus);
    void selectStage (int index);             // ... and one stage of its chain
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SectionHeader;
    class Row;
    class Head;
    class Trail;
    struct Selection { bool isBus = false; int strip = -1; MixBus bus = MixBus::Master; };

    void showSelection();
    void paintWorkspaceBands (juce::Graphics&, juce::Rectangle<int>) const;
    int railWidth() const noexcept  { return railShown  ? kRailW  : Dine::Metric::panelTab; }
    int trailWidth() const noexcept { return trailShown ? kTrailW : Dine::Metric::panelTab; }

    static constexpr int kRailW  = 206;       // the channel rail
    static constexpr int kTrailW = 272;       // what DINE did

    MixController& controller;
    Selection selection;
    std::vector<std::unique_ptr<juce::Component>> listItems; // headers + rows, layout order
    std::vector<Row*> rows;                  // only the selectable rows, for refresh/selection
    juce::Viewport viewport;
    juce::Component listHolder;
    std::unique_ptr<Head> head;
    std::unique_ptr<ChainEditor> chain;
    std::unique_ptr<SignalPath> path;
    std::unique_ptr<Trail> trail;
    std::unique_ptr<DinePanelTab> railTab, trailTab;
    bool railShown = true, trailShown = true;
    int builtForStrips = -1;
};

} // namespace livemix
