#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// LIVE: the view for the service itself. Once the mix is built, nobody should have to
// watch forty processors — only what matters while it is happening: is it recording, is
// the output going out, is anything clipping, and the five faders anyone might touch.
// LIVE SAFE locks away everything that could change the sound by accident.
class LivePage : public juce::Component
{
public:
    LivePage (MixController&, AppServices&);
    ~LivePage() override;

    std::function<void (const juce::String&)> onToast;
    std::function<void()> onLiveSafeChanged;

    void refresh();                    // 30 Hz
    void rebuild();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class GroupFader;

    juce::Rectangle<int> body() const;

    MixController& controller;
    AppServices& services;
    std::array<std::unique_ptr<GroupFader>, int (MixBus::Count)> faders;
    DineButton liveSafeButton { "LIVE SAFE", DineButton::Style::Standard };
    int health = 0;
    float headroomDb = 0.0f;
    juce::String clock, state;
};

} // namespace livemix
