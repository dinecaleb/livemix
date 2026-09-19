#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/Realtime.h"
#include <atomic>
#include <thread>
#include <memory>
#include "Core/ProductDefinition.h"
#include "DSP/ChannelProcessor.h"
#include "Analysis/AnalysisEngine.h"
#include "Intelligence/AnalyzeCoordinator.h"
#include "Intelligence/AIFeature.h"
#include "State/ParameterBridge.h"
#include "Communication/InstanceRegistry.h"
#include "Communication/KitController.h"
#include "State/PresetManager.h"
#include "Intelligence/OpenAIProvider.h"

namespace livemix
{

// One channel of a Dine channel product (Drums, Vocals, Keys, Master). The
// real-time path is ChannelProcessor only; everything intelligent lives on the
// message thread or the analysis worker and reaches the DSP solely through host
// parameters. Which sources, knobs and stages exist comes from the
// ProductDefinition; the behaviour is identical across products.
class ChannelPluginProcessor : public juce::AudioProcessor,
                               public IKitEndpoint,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::Timer
{
public:
    explicit ChannelPluginProcessor (Product product);
    ~ChannelPluginProcessor() override;

    const ProductDefinition& getProduct() const noexcept { return product; }

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) noexcept LIVEMIX_NONBLOCKING override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return product.name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Dine API (message thread) ----
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    ParameterBridge& getBridge() noexcept { return bridge; }
    const ChannelProcessor& getChannel() const noexcept { return channel; }
    AnalysisEngine& getAnalysis() noexcept { return analysis; }
    AnalyzeCoordinator& getCoordinator() noexcept { return coordinator; }

    bool isLiveSafe() const noexcept { return bridge.isLiveSafe(); }

    // Source / profile chosen from the UI: also loads the factory baseline for that combination.
    void setRoleFromUI (ChannelRole role);
    void setProfileFromUI (StyleProfileId style);
    void reloadPreset(); // baseline(role, profile) + current knobs -> all DSP parameters

    // Tune. startAnalyze() arms the capture: the engine waits for signal, listens for
    // kTuneSeconds, then the coordinator produces a TuneResult. The proposed settings are
    // applied as a preview (AFTER) with the previous settings kept (BEFORE) until the user
    // keeps or reverts. Nothing here runs on the audio thread.
    void startAnalyze();
    void abortAnalyze();
    bool isAnalyzeAvailable() const;     // false in Live Safe
    juce::String getAnalyzeButtonText() const;
    juce::String getAnalyzeStatusText() const;
    RecommendationResult getRecommendations() const { return coordinator.getResult(); }
    TuneResult getTuneResult() const { return coordinator.getTuneResult(); }
    bool hasTunedThisSession() const noexcept { return tuneCount.load() > 0; }
    bool isTunePreviewActive() const noexcept { return tunePreviewActive; }
    bool isTuneShowingAfter() const noexcept { return tuneShowingAfter; }
    void setTuneCompare (bool after);    // BEFORE / AFTER while the preview is active
    void keepTune();                     // commit the proposed settings
    void revertTune();                   // restore the settings from before Tune
    int undoTuneItem (const Recommendation& item); // restore only the parameters one item changed
    uint32_t getResultsVersion() const noexcept { return resultsVersion.load(); }
    AnalysisResult getPreviousAnalysis() const { return previousAnalysis; }
    RecommendationResult getPreviousRecommendations() const { return previousRecommendations; }
    OutputStats getLastOutputStats() const { return lastOutputStats; }
    int applyRecommendation (const Recommendation& rec); // number of parameters written
    int applySafeChanges();                              // number of items applied

    // AI assistance. Switched off on purpose (kAIAssistAvailable): no provider, never used.
    bool isAIAssistEnabled() const noexcept { return kAIAssistAvailable && aiAssistEnabled.load(); }
    void setAIAssistEnabled (bool enabled);
    bool hasAIConsent() const noexcept { return aiConsentGiven.load(); }
    void setAIConsent (bool given) noexcept { aiConsentGiven.store (given); }
    bool aiProviderAvailable() const;
    void testAIConnection (const AISettings& settings, std::function<void (bool, juce::String)> callback);
    bool aiProviderSendsDataExternally() const;
    juce::String getAIProviderName() const;

    // Group membership and group intelligence (the KIT view in Dine Drums)
    juce::String getGroupName() const { return groupName; }
    void setGroupName (const juce::String& g);
    uint64_t getInstanceId() const noexcept { return registryId; }
    KitController& getKit() noexcept { return kit; }
    void startKitAnalyze();
    void abortKitAnalyze();
    void applyKitSafeChanges();
    void applyKitBalance();
    uint32_t getKitResultsVersion() const noexcept { return kitResultsVersion.load(); }

    // IKitEndpoint (message thread, called by whichever instance runs TUNE KIT)
    void kitStartAnalyze() override { startAnalyze(); }
    bool kitIsAnalyzing() const override;
    KitMember kitGetMember() const override;
    void kitApplySafeChanges() override { applySafeChanges(); }
    void kitApplyOutputTrimDelta (float deltaDb) override;

    // Presets
    juce::ValueTree createPresetTree (const juce::String& name) const;
    bool loadPresetTree (const juce::ValueTree& preset);
    bool loadPreset (const PresetManager::Info& info);
    juce::String getCurrentPresetName() const { return currentPresetName; }

    // UI state persisted with the session
    bool isAdvancedViewOpen() const noexcept { return advancedView.load(); }
    void setAdvancedViewOpen (bool open) noexcept { advancedView.store (open); }
    juce::Point<int> getEditorSize() const noexcept { return { editorWidth.load(), editorHeight.load() }; }
    void setEditorSize (int w, int h) noexcept { editorWidth.store (w); editorHeight.store (h); }

    // Developer diagnostics
    struct Diagnostics
    {
        double sampleRate = 0.0;
        int blockSize = 0;
        int numChannels = 0;
        int latencySamples = 0;
        float lastBlockMicros = 0.0f;
        float peakBlockMicros = 0.0f;
        float budgetMicros = 0.0f;
        bool analysisCapturing = false;
        int fifoDroppedFrames = 0;
        int overBudgetBlocks = 0;
        int totalBlocks = 0;
    };
    Diagnostics getDiagnostics() const;
    void resetPeakDiagnostics() noexcept { peakBlockMicros.store (0.0f); overBudgetBlocks.store (0); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void applyMacros();
    void registerWithKit();
    void onAnalysisComplete();
    void applyTunePreview();

    const ProductDefinition& product;
    juce::AudioProcessorValueTreeState apvts;
    ParameterBridge bridge;
    ChannelProcessor channel;
    AnalysisEngine analysis;
    AnalyzeCoordinator coordinator;

    std::atomic<bool> macrosDirty { false };
    std::atomic<bool> analysisPending { false };
    std::atomic<bool> analyzeWithAI { false };
    std::atomic<uint32_t> resultsVersion { 0 };
    AnalyzeCoordinator::Stage lastStage = AnalyzeCoordinator::Stage::Idle;
    AnalysisResult previousAnalysis;
    AnalysisResult lastAnalysisForHistory;
    ChannelParameters tuneBefore;
    bool tunePreviewActive = false, tuneShowingAfter = true;
    std::atomic<int> tuneCount { 0 };
    RecommendationResult previousRecommendations;
    OutputStats lastOutputStats;

    std::atomic<bool> collectOutputStats { false };
    std::atomic<float> outputPeakAbs { 0.0f };
    std::atomic<double> outputSumSquares { 0.0 };
    std::atomic<int64_t> outputSampleCount { 0 };

    KitController kit;
    std::atomic<uint32_t> kitResultsVersion { 0 };
    juce::String currentPresetName;

    std::atomic<bool> aiAssistEnabled { false };
    std::atomic<bool> aiConsentGiven { false };
    std::thread aiTestThread;
    std::atomic<bool> aiTestCancel { false };
    void stopAITest();
    std::atomic<bool> advancedView { false };
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };
    juce::String groupName;
    uint64_t registryId = 0;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<int> currentChannels { 0 };
    std::atomic<float> lastBlockMicros { 0.0f };
    std::atomic<float> peakBlockMicros { 0.0f };
    std::atomic<int> overBudgetBlocks { 0 };
    std::atomic<int> totalBlocks { 0 };
    float budgetMicros = 0.0f;

    bool restoringState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelPluginProcessor)
};

} // namespace livemix
