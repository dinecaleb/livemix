#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "AppServices.h"
#include "AppTheme.h"
#include "UI/Widgets.h"

namespace livemix
{

// TUNE CHANNEL, on whatever workspace you are on. One click on a channel - a mixer strip,
// a track header, the Inspector - listens to that source and proposes its chain, its input
// gain, its level and its sends; nothing else in the mix moves.
//
// It is a sheet over the page rather than a workspace of its own, because tuning one
// channel is a small thing: the console keeps playing behind it, you keep your place, and
// the same BEFORE / AFTER, KEEP and REVERT the mix uses decide what happens to it.
class ChannelTuneSheet : public juce::Component
{
public:
    ChannelTuneSheet (MixController&, int strip);

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;
    std::function<void()> onOpenInspector;

    void refresh();                       // 30 Hz from MainView: the listen, then the plan
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool previewing() const;              // the plan is ready and this channel is what it is about
    juce::Rectangle<int> cardBounds() const;
    void updateControls();

    // The lines under the headline: what it did, then why, in the plan's own words.
    std::vector<std::pair<juce::String, juce::String>> lines() const;
    int listHeight() const;               // exactly what those lines take, so the card fits them

    MixController& controller;
    int strip = -1;
    juce::String name;
    Dine::Icon icon = Dine::Icon::Mic;
    bool wasPreviewing = false;

    DineButton cancel { "Cancel", DineButton::Style::Standard };
    DineButton before { "BEFORE", DineButton::Style::Segment };
    DineButton after  { "AFTER",  DineButton::Style::Segment };
    DineButton keep   { "KEEP",   DineButton::Style::Filled };
    DineButton revert { "REVERT", DineButton::Style::Standard };
    DineButton inspect { "Open in the Inspector", DineButton::Style::Ghost };
};

} // namespace livemix
