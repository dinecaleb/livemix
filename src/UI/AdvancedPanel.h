#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <memory>
#include "Widgets.h"
#include "ChainStrip.h"
#include "EqCurveComponent.h"
#include "MeterComponent.h"
#include "LiveState.h"
#include "Recommendations/Recommendation.h"

namespace livemix
{

// Advanced mode: module rail on the left (INPUT GATE EQ COMP TRANSIENT
// OUTPUT), one module at a time on the right with a live visual (EQ curve,
// gate scope, compressor transfer curve, meters) and its parameter tiles.
// Same parameters as Simple mode, so switching views never changes the sound.
class AdvancedPanel : public juce::Component
{
public:
    AdvancedPanel (juce::AudioProcessorValueTreeState&, const ProductDefinition& product);
    ~AdvancedPanel() override;

    void setModule (ChainModule m);
    ChainModule getModule() const { return module; }
    void update (const LiveState& live);

    // Unapplied recommendations from the last analysis; the one matching the
    // open module is offered as a card above its visual.
    void setSuggestions (const std::vector<Recommendation>& recs);
    static ChainModule moduleFor (const Recommendation&);
    std::function<void (const Recommendation&)> onApplySuggestion, onDismissSuggestion;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RailButton;
    class BandTile;
    class GateScope;
    class CompView;
    class ReductionView;
    class SuggestionCard;

    void rebuildTiles();

    juce::AudioProcessorValueTreeState& apvts;
    const ProductDefinition& product;
    ChainModule module = ChainModule::EQ;
    std::vector<std::unique_ptr<RailButton>> rail;
    std::vector<std::unique_ptr<ParamTile>> tiles;
    std::vector<std::unique_ptr<BandTile>> bandTiles;
    std::unique_ptr<EqCurveComponent> eqCurve;
    std::unique_ptr<GateScope> gateScope;
    std::unique_ptr<CompView> compView;
    std::unique_ptr<ReductionView> reductionView; // de-esser / limiter / width readouts
    std::unique_ptr<MeterComponent> inMeter, outMeter;
    std::unique_ptr<FlatButton> polarityButton;
    std::unique_ptr<BoolParamAttachment> polarityAtt;
    std::unique_ptr<SuggestionCard> suggestion;
    std::vector<Recommendation> suggestions;
    const LiveState* lastLive = nullptr;
    juce::String title, subtitle;
};

} // namespace livemix
