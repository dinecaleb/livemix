#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include "ChannelPluginProcessor.h"
#include "UI/LiveMixLookAndFeel.h"
#include "UI/LiveState.h"
#include "UI/ShellBars.h"
#include "UI/SimplePanel.h"
#include "UI/AdvancedPanel.h"
#include "UI/KitPanel.h"
#include "UI/AnalyzeOverlay.h"

namespace livemix
{

// Editor shared by every Dine channel product: the common shell (top bar, view
// tabs), Simple / Advanced (/ Kit) views, the Tune overlay and a toast. Nothing
// here touches the audio thread; the timer reads atomics and host parameters.
class ChannelPluginEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit ChannelPluginEditor (ChannelPluginProcessor&);
    ~ChannelPluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Programmatic equivalents of the user's actions (used by the UI snapshot tools).
    void setView (SubBar::View v);
    void requestAnalyze();          // TUNE
    void dismissAnalyzeOverlay();   // close the card, keep whatever is audible
    void reviewAnalysisDetails();   // REVIEW: keep, open Advanced with every decision listed
    void keepTune();                // KEEP
    void revertTune();              // REVERT
    void compareTune (bool after);  // BEFORE / AFTER
    void showLastAnalysis();        // LAST TUNE in the sub bar
    AdvancedPanel& getAdvancedPanel() { return advancedPanel; }

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
    void refreshAnalyzeFlow();
    void refreshResults();
    void refreshKit();
    void beginSoundcheck();
    void updateSuggestions();
    void showTuneCard();
    void showToast (const juce::String& text);
    void showModuleMenu (juce::Component& anchor);
    void showSourceMenu (juce::Component& anchor);
    void showProfileMenu (juce::Component& anchor);
    void showSettingsMenu (juce::Component& anchor);
    void showPresetMenu (juce::PopupMenu& into);
    void handlePresetChoice (int id);
    void showGroupDialog();
    void showAISettings();
    void toggleAIAssist();
    juce::String sourceName() const;

    ChannelPluginProcessor& proc;
    const ProductDefinition& product;
    LiveMixLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ComponentBoundsConstrainer constrainer;

    TopBar topBar;
    SubBar subBar;
    SimplePanel simplePanel;
    AdvancedPanel advancedPanel;
    KitPanel kitPanel;
    AnalyzeOverlay overlay;
    Toast toast;

    LiveState live;
    int holdCounter = 0, recentCounter = 0, tick = 0, toastTicks = 0, peakHoldTicks = 0;
    float outHoldTicks = 0.0f;

    bool analyzeRequested = false;
    uint32_t shownResultsVersion = 0xFFFFFFFF, resultsVersionAtRequest = 0;
    bool resultsValid = false;
    bool lastLiveSafe = false;
    std::vector<std::string> shownKeys;
    std::set<int> dismissed;

    juce::String shownMembers;
    uint32_t shownKitVersion = 0xFFFFFFFF;

    std::vector<PresetManager::Info> factoryPresets, userPresets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelPluginEditor)
};

} // namespace livemix
