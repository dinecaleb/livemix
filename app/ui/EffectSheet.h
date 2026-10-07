#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppTheme.h"
#include "Mix/MixSession.h"
#include "FX/FxParameters.h"

namespace livemix
{

class MixController;

// ONE EFFECT, BY HAND. Opened from an effect return's name on the console: what the return
// sounds like, in the few numbers an engineer actually reaches for mid-service.
//
//   a reverb  - Decay (how long it rings) and Pre-delay (how late it starts after the voice)
//   the delay - Note (what it repeats at: a note of the song, or a time of its own), Time
//               when it is free, Repeats, and the song's Tempo with Tap beside it
//
// Every turn reaches the mix at once (MixController::setFxSlotCharacter / setTempo), so it is
// heard while it is being set; it is kept with the session and undone like any other edit,
// and the next TUNE MIX replaces it the way it replaces a fader. Nothing moves under BYPASS.
class EffectSheet : public juce::Component
{
public:
    EffectSheet (MixController&, FxSlot);
    ~EffectSheet() override;

    std::function<void()> onClose;     // the window removes the sheet
    std::function<void()> onEdited;    // the window moves the document's revision

    FxSlot getSlot() const noexcept { return slot; }
    void tap (double nowMs);           // one press of Tap, at that moment (public for the tests)

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> cardBounds() const;
    void pull();                       // the knobs read the mix
    void commitFx (const std::function<void (FxParameters&)>&);
    bool isDelay() const;

    MixController& controller;
    const FxSlot slot;
    std::vector<std::unique_ptr<DineKnob>> knobs;
    DineKnob* decay = nullptr;
    DineKnob* preDelay = nullptr;
    DineKnob* time = nullptr;
    DineKnob* repeats = nullptr;
    DineKnob* tempo = nullptr;
    std::unique_ptr<DinePopup> note;
    std::unique_ptr<DineButton> tapButton, doneButton;
    std::vector<double> taps;
};

} // namespace livemix
