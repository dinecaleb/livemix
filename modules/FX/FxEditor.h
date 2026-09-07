#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FxProcessor.h"
#include "FxPanels.h"
#include "UI/LiveMixLookAndFeel.h"
#include "UI/ShellBars.h"

namespace livemix
{

// Dine FX editor: the common Dine shell (top bar with TYPE / PROFILE, A/B,
// LIVE SAFE; SIMPLE / ADVANCED tabs), the Simple and Advanced panels and a
// toast. The timer reads atomics and host parameters only.
class FxEditor : public juce::AudioProcessorEditor,
                 private juce::Timer
{
public:
    explicit FxEditor (FxProcessor&);
    ~FxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Programmatic equivalents of the user's actions (used by the UI snapshot tool).
    void setView (SubBar::View v);
    FxAdvancedPanel& getAdvancedPanel() { return advancedPanel; }

private:
    class Toast : public juce::Component
    {
    public:
        void show (const juce::String& t) { text = t; setVisible (true); repaint(); }
        int idealWidth() const;
        void paint (juce::Graphics&) override;
    private:
        juce::String text;
    };

    void timerCallback() override;
    void updateLiveState();
    void refreshShell();
    void showToast (const juce::String& text);
    void showModuleMenu (juce::Component& anchor);
    void showTypeMenu (juce::Component& anchor);
    void showProfileMenu (juce::Component& anchor);
    void showSettingsMenu (juce::Component& anchor);
    void showPresetMenu (juce::PopupMenu& into);
    void handlePresetChoice (int id);

    FxProcessor& fx;
    LiveMixLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ComponentBoundsConstrainer constrainer;

    TopBar topBar;
    SubBar subBar;
    FxSimplePanel simplePanel;
    FxAdvancedPanel advancedPanel;
    Toast toast;

    FxLiveState live;
    int holdCounter = 0, outHoldCounter = 0, toastTicks = 0;
    bool lastLiveSafe = false;
    std::vector<FxPresets::Info> factoryPresets, usePresets, userPresets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxEditor)
};

} // namespace livemix
