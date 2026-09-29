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
// SignalPath draws the whole path along the top - one small chip per stage in the order
// the audio meets them, with its lamp - and this panel is the design's Stage card
// (`65:9397`): the stage's name and the macro word it answers to, an Off / On segment at
// the right, the line that says who set it, then a well holding what the stage is doing,
// drawn (an EQ curve you can drag a node on, a compressor's in-out line with the live
// gain reduction, the bars of a trim), a row of knobs under it - one per number - and the
// stage's choices under those. The sentence that says what it is for closes the card.
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
    // ADD A SOUND: a file from this Mac becomes one of this drum's sounds, copied into the
    // session so it travels with it. The window owns the chooser and the copying; the panel
    // only knows which family is being asked about.
    std::function<void (RoleFamily)> onImportSample;

    // The card's own head: the title row at y = 20 (22 tall) and the provenance line under
    // it, so the well starts at 84 exactly as the design draws it.
    static constexpr int kHeaderH = 84;
    static constexpr int kSentenceH = 56;   // the closing sentence, along the foot of the card

private:
    class Graph;
    class Knob;
    class ChoiceGroup;
    class SendRow;

    ChannelParameters read() const;
    void write (const ChannelParameters&);
    void commit (const std::function<void (ChannelParameters&)>&);
    void build();                                // the stages this channel has
    void buildControls();                        // the picked stage's knobs and choices
    void updateViews();
    void revertStage();                          // this stage back to what TUNE MIX set
    bool stageEdited (int index) const;
    // Where the card's well - the drawing, the knobs and the choices - lands, in this
    // component's own coordinates.
    juce::Rectangle<int> wellBounds() const;
    juce::String lastTuneClock() const;          // the clock on the newest tune this channel carries
    const StageSpec& spec() const;
    const StripParameters* plannedStrip() const; // what the last TUNE MIX proposed, or null
    const ChannelParameters* plannedChannel() const;

    MixController& controller;
    bool isBus = false;
    int strip = -1;
    MixBus bus = MixBus::Master;
    int selected = 0, band = 0;
    bool bypassed = false, edited = false, stageOn = true;
    int sampleHits = 0;                          // the sampler's count, for the hit marks on the drawing
    juce::Colour badgeTint { Dine::accent };
    std::vector<StageSpec> stages;
    std::vector<StageView> views;
    juce::String provenance, sentence;
    std::unique_ptr<Graph> graph;
    juce::Viewport controlsView;
    juce::Component controlsHolder;
    std::vector<std::unique_ptr<juce::Component>> controls;   // knobs, then popups/buttons, then choice groups
    // Off / On: the design's two-segment track at the right of the title row. A switch that
    // reads "Off | On" says which of the two it is in; a lamp only says that it is lit.
    DineSegmentRow onOffTrack;
    DineButton offButton { "Off", DineButton::Style::Segment };
    DineButton onButton  { "On",  DineButton::Style::Segment };
    std::unique_ptr<DineButton> revertButton;
};

// The whole chain as one row of small chips, the way the design draws it (`Signal path`,
// 73:10380): a lamp and the stage's name, nothing else. The chosen chip is a lit plane; a
// stage that is out of the chain is quiet. Click a chip to open that stage; click its lamp
// to switch it in or out. The row scrolls when the chain is longer than the width.
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

    static constexpr int height = 32;     // the design's row; the chips are 30 inside it
    static constexpr int chipH  = 30;
    static constexpr int gap    = 6;

private:
    int chipWidth (int index) const;
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
