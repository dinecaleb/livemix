#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "AppTheme.h"
#include "DSP/ChannelParameters.h"
#include "DSP/SampleBank.h"

namespace livemix
{

// One stage of a channel's chain, read for display: what it is called, what it is set to,
// and whether the audio actually meets it. This is the same order ChannelProcessor runs
// them in and the same order the Inspector's cards appear in, so the strip along the foot
// of a workspace, the INSERTS list on a mixer strip and the Inspector never disagree.
struct ChainStage
{
    juce::String label;      // INPUT, FILTERS, GATE, EQ ... letterspaced caps
    juce::String value;      // what it is set to, in numbers
    bool active = false;     // the stage is switched on
};

// Every stage, in chain order. `includeLimiter` is true only where MixEngine configures
// one (the master); `stereo` decides whether the width stage is there at all; `includeSample`
// is true on the strips MixEngine gives a sample stage (kick, snare, toms: hasSampleStage).
std::vector<ChainStage> chainStages (const ChannelParameters&, bool includeLimiter, bool stereo, bool includeSample = false);

// Only the stages that are doing something, for a short list such as INSERTS.
std::vector<ChainStage> activeChainStages (const ChannelParameters&, bool includeLimiter, bool stereo, bool includeSample = false);

// Whether a strip of this role carries the sample stage - the one rule, read by every list.
inline bool hasSampleStage (ChannelRole role) noexcept { return sampleReplacementAppropriate (roleFamily (role)); }

// The strip along the foot of TRACKS and MIXER: the selected channel's colour and name,
// then its chain stage by stage with the arrows between, then a right-hand note. It is a
// readout, not a control - click it to open the Inspector.
class ChainStrip : public juce::Component
{
public:
    ChainStrip();

    std::function<void()> onOpen;

    void setSource (const juce::String& name, juce::Colour tint, const ChannelParameters&,
                    bool includeLimiter, bool stereo, bool includeSample = false);
    void setEmpty (const juce::String& message);
    void setNote (const juce::String& text);          // the right-hand meta, e.g. the session's clock

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int height = 48;

private:
    juce::String name, note, empty;
    juce::Colour tint { Dine::ink3 };
    std::vector<ChainStage> stages;
    bool hasSource = false;
    bool hover = false;
public:
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
};

} // namespace livemix
