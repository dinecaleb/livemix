#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"
#include "DSP/ChannelParameters.h"

namespace livemix
{

// The processing chain of one channel or bus, one stage at a time.
//
// SignalPath draws the whole path along the top - a chip per stage in the order the
// audio meets them, its lamp, what it is set to and how hard it is working - and this
// panel opens the one you picked: what the stage is doing, drawn (an EQ curve you can
// drag a node on, a compressor's in-out line with the live gain reduction, the bars of a
// trim), and beside it a knob for every number it owns.
//
// Every edit goes through MixController as a whole ChannelParameters, the same way the
// engine takes it, so a hand edit sits on the kept mix beside the faders: it survives a
// macro move, it is saved with the session, and the next TUNE MIX replaces it exactly as
// it replaces a fader. Nothing here can be moved while BYPASS is on.
class ChainEditor : public juce::Component
{
public:
    struct Field;        // one number, switch or choice inside a stage
    struct StageSpec;    // one stage: what it is called, what it says, what it owns

    // One stage as the signal path reads it. Nothing here is a control: the path asks the
    // editor to select or switch a stage, so the two can never disagree.
    struct StageView
    {
        juce::String label;              // INPUT, FILTERS, GATE ... the same words as ChainStrip
        juce::String value;              // what it is set to, short enough for a chip
        Dine::Icon icon = Dine::Icon::Sliders;
        bool on = true;                  // the audio meets it
        bool switchable = false;         // it has a lamp of its own
        bool edited = false;             // moved by hand since the last TUNE MIX
        float grDb = 0.0f;               // live gain reduction, 0 where the stage has none
        juce::String why;                // why TUNE MIX set it this way, or what the stage is for
    };

    explicit ChainEditor (MixController&);
    ~ChainEditor() override;

    // What to edit. Rebuilds the stages: a mono input has no width stage, only the master
    // owns the limiter, and there are sends only where the session uses FX.
    void showStrip (int strip);
    void showBus (MixBus bus);

    int numStages() const noexcept { return int (views.size()); }
    const std::vector<StageView>& stageViews() const noexcept { return views; }
    int selectedStage() const noexcept { return selected; }
    void selectStage (int index);
    void toggleStage (int index);                // the lamp: switch the stage in or out

    void refresh();                              // values, meters and the graph, at the page's rate
    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onStageChanged;        // the path and the trail follow the panel

    static constexpr int kHeaderH = 42;

private:
    class Graph;
    class Knob;
    class BandCard;
    class SwitchChip;
    class SendRow;

    ChannelParameters read() const;
    void write (const ChannelParameters&);
    void commit (const std::function<void (ChannelParameters&)>&);
    void build();                                // the stages this channel has
    void buildControls();                        // the picked stage's knobs and cards
    void updateViews();
    void revertStage();                          // this stage back to what TUNE MIX set
    bool stageEdited (int index) const;
    const StageSpec& spec() const;
    const StripParameters* plannedStrip() const; // what the last TUNE MIX proposed, or null
    const ChannelParameters* plannedChannel() const;

    MixController& controller;
    bool isBus = false;
    int strip = -1;
    MixBus bus = MixBus::Master;
    int selected = 0, band = 0;
    bool bypassed = false, edited = false, stageOn = true;
    juce::String badge;
    juce::Colour badgeTint { Dine::accent };
    std::vector<StageSpec> stages;
    std::vector<StageView> views;
    std::unique_ptr<Graph> graph;
    juce::Viewport controlsView;
    juce::Component controlsHolder;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::unique_ptr<DineSwitch> power;
    std::unique_ptr<DineButton> revertButton;
};

// The whole chain as one row of chips: the lamp that switches a stage in and out, its
// number and icon, what it is set to, and a bar along the foot for how hard it is
// working. Click a chip to open that stage in the editor; click its lamp to switch it.
class SignalPath : public juce::Component
{
public:
    explicit SignalPath (ChainEditor&);

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int height = 128;
    static constexpr int titleH = 24;
    // Narrower than this and a chip stops being readable, so the path scrolls instead of
    // squeezing: a stage you cannot read is a stage you cannot pick.
    static constexpr int minChipW = 74;

private:
    juce::Rectangle<int> chipBounds (int index) const;
    int chipAt (juce::Point<int>) const;
    int contentWidth() const;
    int maxScroll() const;
    void clampScroll();

    ChainEditor& chain;
    int hover = -1;
    int scrollX = 0;
};

} // namespace livemix
