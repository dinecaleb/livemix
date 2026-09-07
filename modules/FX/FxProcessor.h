#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "FX/FxChain.h"
#include "State/FxParameterBridge.h"
#include "FxPresets.h"

namespace livemix
{

// Dine FX: one reverb/delay instance, normally on an aux/send. The real-time
// path is FxChain only; type/profile/preset/macro logic lives on the message
// thread and reaches the DSP solely through host parameters.
class FxProcessor : public juce::AudioProcessor,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::Timer
{
public:
    FxProcessor();
    ~FxProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dine FX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return double (tailSeconds.load()); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Dine FX API (message thread) ----
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    FxParameterBridge& getBridge() noexcept { return bridge; }
    const FxChain& getChain() const noexcept { return chain; }
    bool isLiveSafe() const noexcept { return bridge.isLiveSafe(); }
    double getTempo() const noexcept { return tempo.load(); }
    bool hasHostTempo() const noexcept { return hostTempo.load(); }

    // Type / profile chosen from the UI: also loads the factory baseline for that combination.
    void setTypeFromUI (FxType type);
    void setProfileFromUI (StyleProfileId style);
    void reloadPreset(); // baseline(type, profile) + current macros -> all DSP parameters

    // Presets
    juce::ValueTree createPresetTree (const juce::String& name) const;
    bool loadPresetTree (const juce::ValueTree& preset);
    bool loadPreset (const FxPresets::Info& info);
    juce::String getCurrentPresetName() const { return currentPresetName; }

    // UI state persisted with the session
    bool isAdvancedViewOpen() const noexcept { return advancedView.load(); }
    void setAdvancedViewOpen (bool open) noexcept { advancedView.store (open); }
    juce::Point<int> getEditorSize() const noexcept { return { editorWidth.load(), editorHeight.load() }; }
    void setEditorSize (int w, int h) noexcept { editorWidth.store (w); editorHeight.store (h); }

    struct Diagnostics
    {
        double sampleRate = 0.0;
        int blockSize = 0;
        int numChannels = 0;
        float lastBlockMicros = 0.0f;
        float peakBlockMicros = 0.0f;
        float budgetMicros = 0.0f;
        int overBudgetBlocks = 0;
        int totalBlocks = 0;
    };
    Diagnostics getDiagnostics() const;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void applyMacros();

    juce::AudioProcessorValueTreeState apvts;
    FxParameterBridge bridge;
    FxChain chain;

    std::atomic<bool> macrosDirty { false };
    std::atomic<float> tailSeconds { 0.0f };
    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> hostTempo { false };
    juce::String currentPresetName;
    std::atomic<bool> advancedView { false };
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<int> currentChannels { 0 };
    std::atomic<float> lastBlockMicros { 0.0f };
    std::atomic<float> peakBlockMicros { 0.0f };
    std::atomic<int> overBudgetBlocks { 0 };
    std::atomic<int> totalBlocks { 0 };
    float budgetMicros = 0.0f;
    bool restoringState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxProcessor)
};

} // namespace livemix
