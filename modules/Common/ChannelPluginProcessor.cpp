#include "ChannelPluginProcessor.h"
#include "ChannelPluginEditor.h"
#include "State/ParameterLayout.h"
#include "State/ParameterIDs.h"
#include "State/ParameterSpecs.h"
#include "Profiles/StyleProfile.h"
#include "Profiles/MacroMapping.h"
#include "Core/DbUtils.h"
#include <thread>

namespace livemix
{

namespace
{
    constexpr float kTuneSeconds = 12.0f;      // listening window once signal is present
    constexpr float kTuneTriggerDb = -45.0f;    // a 10 ms frame above this starts the window
    constexpr float kTuneMaxWaitSeconds = 20.0f;// then start anyway (the result will say "no signal")
    constexpr int kStateVersion = 2;

    juce::String parameterTreeType (const ProductDefinition& d)
    {
        // "LiveMixDrumsState" -> "LiveMixDrums" (the released Drums tree type)
        return juce::String (d.stateType).upToLastOccurrenceOf ("State", false, false);
    }

    juce::ValueTree analysisToTree (const AnalysisResult& a)
    {
        juce::ValueTree t ("Analysis");
        t.setProperty ("valid", a.valid, nullptr);
        t.setProperty ("sampleRate", a.sampleRate, nullptr);
        t.setProperty ("numChannels", a.numChannels, nullptr);
        t.setProperty ("duration", a.durationSeconds, nullptr);
        t.setProperty ("peakDb", a.peakDb, nullptr);
        t.setProperty ("rmsDb", a.rmsDb, nullptr);
        t.setProperty ("crestDb", a.crestFactorDb, nullptr);
        t.setProperty ("hitDb", a.hitLevelDb, nullptr);
        t.setProperty ("floorDb", a.noiseFloorDb, nullptr);
        t.setProperty ("rangeDb", a.dynamicRangeDb, nullptr);
        t.setProperty ("silence", a.silencePercent, nullptr);
        t.setProperty ("dc", a.dcOffset, nullptr);
        t.setProperty ("clips", a.clipCount, nullptr);
        t.setProperty ("transients", a.transientCount, nullptr);
        t.setProperty ("bleed", a.bleedEstimate, nullptr);
        t.setProperty ("balance", a.stereoBalanceDb, nullptr);
        t.setProperty ("fundamental", a.fundamentalHz, nullptr);
        t.setProperty ("decayMs", a.meanDecayMs, nullptr);
        t.setProperty ("correlation", a.stereoCorrelation, nullptr);
        t.setProperty ("sibilance", a.sibilanceDb, nullptr);
        t.setProperty ("sibilancePct", a.sibilancePercent, nullptr);
        t.setProperty ("lufs", a.loudnessLufs, nullptr);
        t.setProperty ("truePeak", a.truePeakDb, nullptr);
        juce::String bands;
        for (float b : a.bandEnergyDb) bands += juce::String (b, 2) + ",";
        t.setProperty ("bands", bands, nullptr);
        return t;
    }

    AnalysisResult treeToAnalysis (const juce::ValueTree& t)
    {
        AnalysisResult a;
        if (! t.isValid()) return a;
        a.valid = t.getProperty ("valid", false);
        a.sampleRate = t.getProperty ("sampleRate", 48000.0);
        a.numChannels = t.getProperty ("numChannels", 1);
        a.durationSeconds = t.getProperty ("duration", 0.0f);
        a.peakDb = t.getProperty ("peakDb", -120.0f);
        a.rmsDb = t.getProperty ("rmsDb", -120.0f);
        a.crestFactorDb = t.getProperty ("crestDb", 0.0f);
        a.hitLevelDb = t.getProperty ("hitDb", -120.0f);
        a.noiseFloorDb = t.getProperty ("floorDb", -120.0f);
        a.dynamicRangeDb = t.getProperty ("rangeDb", 0.0f);
        a.silencePercent = t.getProperty ("silence", 0.0f);
        a.dcOffset = t.getProperty ("dc", 0.0f);
        a.clipCount = t.getProperty ("clips", 0);
        a.transientCount = t.getProperty ("transients", 0);
        a.bleedEstimate = t.getProperty ("bleed", 0.0f);
        a.stereoBalanceDb = t.getProperty ("balance", 0.0f);
        a.fundamentalHz = t.getProperty ("fundamental", 0.0f);
        a.meanDecayMs = t.getProperty ("decayMs", 0.0f);
        a.stereoCorrelation = t.getProperty ("correlation", 1.0f);
        a.sibilanceDb = t.getProperty ("sibilance", -120.0f);
        a.sibilancePercent = t.getProperty ("sibilancePct", 0.0f);
        a.loudnessLufs = t.getProperty ("lufs", -120.0f);
        a.truePeakDb = t.getProperty ("truePeak", -120.0f);
        juce::StringArray bands = juce::StringArray::fromTokens (t.getProperty ("bands", "").toString(), ",", "");
        for (size_t i = 0; i < a.bandEnergyDb.size() && int (i) < bands.size(); ++i)
            a.bandEnergyDb[i] = bands[int (i)].getFloatValue();
        return a;
    }

    juce::ValueTree recommendationsToTree (const RecommendationResult& r)
    {
        juce::ValueTree t ("Recommendations");
        t.setProperty ("valid", r.valid, nullptr);
        t.setProperty ("health", juce::String (r.inputHealth), nullptr);
        t.setProperty ("peakDb", r.measuredPeakDb, nullptr);
        t.setProperty ("rmsDb", r.measuredRmsDb, nullptr);
        t.setProperty ("captureGain", r.suggestedCaptureGainDb, nullptr);
        for (const auto& item : r.items)
        {
            juce::ValueTree i ("Item");
            i.setProperty ("kind", int (item.kind), nullptr);
            i.setProperty ("section", int (item.section), nullptr);
            i.setProperty ("what", juce::String (item.what), nullptr);
            i.setProperty ("why", juce::String (item.why), nullptr);
            i.setProperty ("confidence", int (item.confidence), nullptr);
            i.setProperty ("safe", item.safeToAutoApply, nullptr);
            for (const auto& c : item.changes)
            {
                juce::ValueTree ch ("Change");
                ch.setProperty ("id", juce::String (c.paramId), nullptr);
                ch.setProperty ("value", c.value, nullptr);
                i.addChild (ch, -1, nullptr);
            }
            t.addChild (i, -1, nullptr);
        }
        return t;
    }

    juce::ValueTree parametersToTree (const juce::Identifier& type, const ChannelParameters& params)
    {
        juce::ValueTree t (type);
        ChannelParameters p = params;
        forEachDspParameter (p, [&] (const std::string& id, auto& v) { t.setProperty (juce::Identifier (id), float (v), nullptr); });
        return t;
    }

    bool treeToParameters (const juce::ValueTree& t, ChannelParameters& p)
    {
        if (! t.isValid()) return false;
        forEachDspParameter (p, [&] (const std::string& id, auto& v)
        {
            using T = std::remove_reference_t<decltype (v)>;
            const juce::Identifier ident (id);
            if (! t.hasProperty (ident)) return;
            const float raw = t.getProperty (ident);
            if constexpr (std::is_same_v<T, bool>) v = raw >= 0.5f;
            else if constexpr (std::is_same_v<T, int>) v = int (raw + 0.5f);
            else v = raw;
        });
        return true;
    }

    RecommendationResult treeToRecommendations (const juce::ValueTree& t)
    {
        RecommendationResult r;
        if (! t.isValid()) return r;
        r.valid = t.getProperty ("valid", false);
        r.inputHealth = t.getProperty ("health", "").toString().toStdString();
        r.measuredPeakDb = t.getProperty ("peakDb", -120.0f);
        r.measuredRmsDb = t.getProperty ("rmsDb", -120.0f);
        r.suggestedCaptureGainDb = t.getProperty ("captureGain", 0.0f);
        for (const auto& i : t)
        {
            if (! i.hasType ("Item")) continue;
            Recommendation item;
            item.kind = Recommendation::Kind (int (i.getProperty ("kind", 7)));
            item.section = i.hasProperty ("section") ? TuneSection (juce::jlimit (0, int (TuneSection::Count) - 1, int (i.getProperty ("section", 6))))
                                                      : Recommendation::sectionFor (item.kind);
            item.what = i.getProperty ("what", "").toString().toStdString();
            item.why = i.getProperty ("why", "").toString().toStdString();
            item.confidence = Confidence (juce::jlimit (0, 2, int (i.getProperty ("confidence", 1))));
            item.safeToAutoApply = i.getProperty ("safe", false);
            for (const auto& ch : i)
                if (ch.hasType ("Change"))
                    item.changes.push_back ({ ch.getProperty ("id", "").toString().toStdString(), float (ch.getProperty ("value", 0.0f)) });
            r.items.push_back (item);
        }
        return r;
    }
}

ChannelPluginProcessor::ChannelPluginProcessor (Product productId)
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      product (productDefinition (productId)),
      apvts (*this, nullptr, juce::Identifier (parameterTreeType (product)), createParameterLayout (channelParameterSpecs (productId))),
      bridge (apvts, product),
      groupName (product.defaultGroup)
{
    for (const auto& m : product.macros)
        apvts.addParameterListener (m.id, this);

    // AI capabilities are switched off on purpose (AIFeature.h): no provider is installed, Standard Tune only.
    // if (kAIAssistAvailable) coordinator.setProvider (std::make_shared<OpenAIProvider>());

    ChannelProcessor::Options options;
    options.limiter = product.hasLoudness;
    options.loudnessMeter = product.hasLoudness;
    for (auto st : product.stages) if (st == ChainStage::Width) options.widthMeter = true;
    channel.configure (options);

    // A fresh instance starts on the factory baseline for the product's default source and profile.
    reloadPreset();
    registerWithKit();
    setLatencySamples (channel.getLatencySamples());
    startTimerHz (20);
}

ChannelPluginProcessor::~ChannelPluginProcessor()
{
    stopTimer();
    analysis.abort();
    coordinator.cancel();
    stopAITest();
    if (registryId != 0) InstanceRegistry::get().unregisterInstance (registryId);
}

// ---------------------------------------------------------------------------
// Real-time
// ---------------------------------------------------------------------------
void ChannelPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (1, getTotalNumInputChannels());
    channel.prepare (sampleRate, samplesPerBlock, numChannels);
    channel.setParameters (bridge.read());
    analysis.prepare (sampleRate, numChannels);
    setLatencySamples (channel.getLatencySamples());
    currentSampleRate.store (sampleRate);
    currentBlockSize.store (samplesPerBlock);
    currentChannels.store (numChannels);
    peakBlockMicros.store (0.0f);
    overBudgetBlocks.store (0);
    totalBlocks.store (0);
    budgetMicros = float (1.0e6 * samplesPerBlock / sampleRate);
}

void ChannelPluginProcessor::releaseResources()
{
    analysis.abort();
}

bool ChannelPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ChannelPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) noexcept LIVEMIX_NONBLOCKING
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();

    const int numInputs = getTotalNumInputChannels();
    const int numOutputs = getTotalNumOutputChannels();
    for (int ch = numInputs; ch < numOutputs; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numChannels = juce::jmin (numInputs, kMaxChannels);
    if (numChannels <= 0 || buffer.getNumSamples() <= 0) return;

    AudioBlockView block { buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples() };

    // Parameters: cached atomics only, no lookups or allocation.
    channel.setParameters (bridge.read());

    // Analysis captures the *raw* input (capture-gain recommendations must see
    // what the converter delivered). Wait-free; a no-op unless capturing.
    if (! bridge.isLiveSafe())
        analysis.pushAudio (block);

    channel.process (block);

    // Processed-output statistics for mix-gain recommendations (only while capturing).
    if (collectOutputStats.load (std::memory_order_relaxed))
    {
        float peak = outputPeakAbs.load (std::memory_order_relaxed);
        double sq = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* d = block.channel (ch);
            for (int i = 0; i < block.numSamples; ++i)
            {
                const float a = std::fabs (d[i]);
                if (a > peak) peak = a;
                sq += double (d[i]) * d[i];
            }
        }
        outputPeakAbs.store (peak, std::memory_order_relaxed);
        outputSumSquares.store (outputSumSquares.load (std::memory_order_relaxed) + sq, std::memory_order_relaxed);
        outputSampleCount.fetch_add (int64_t (block.numSamples) * numChannels, std::memory_order_relaxed);
    }

    const double micros = 1.0e6 * double (juce::Time::getHighResolutionTicks() - startTicks) / double (juce::Time::getHighResolutionTicksPerSecond());
    lastBlockMicros.store (float (micros), std::memory_order_relaxed);
    if (micros > peakBlockMicros.load (std::memory_order_relaxed))
        peakBlockMicros.store (float (micros), std::memory_order_relaxed);
    totalBlocks.fetch_add (1, std::memory_order_relaxed);
    if (budgetMicros > 0.0f && micros > budgetMicros)
        overBudgetBlocks.fetch_add (1, std::memory_order_relaxed);
}

void ChannelPluginProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass: pass through, keep the meters alive so the UI stays honest.
    const int numChannels = juce::jmin (getTotalNumInputChannels(), kMaxChannels);
    if (numChannels <= 0) return;
    AudioBlockView block { buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples() };
    channel.getInputMeter().process (block);
    channel.getOutputMeter().process (block);
}

// ---------------------------------------------------------------------------
// Presets / knobs
// ---------------------------------------------------------------------------
void ChannelPluginProcessor::setRoleFromUI (ChannelRole role)
{
    if (isLiveSafe()) return;
    bridge.setRole (role);
    reloadPreset();
    registerWithKit();
}

void ChannelPluginProcessor::setProfileFromUI (StyleProfileId style)
{
    if (isLiveSafe()) return;
    bridge.setParameterValue (ParamID::profile, float (int (style)));
    reloadPreset();
}

void ChannelPluginProcessor::reloadPreset()
{
    const ChannelRole role = bridge.readRole();
    const StyleProfileId style = bridge.readStyle();
    const ChannelParameters base = StyleProfile::baseline (role, style);
    bridge.writeAll (MacroMapping::apply (product.product, base, bridge.readMacroValues(), roleFamily (role)));
    currentPresetName = juce::String (styleProfileName (style)) + " / " + channelRoleName (role);
}

void ChannelPluginProcessor::applyMacros()
{
    const ChannelRole role = bridge.readRole();
    const ChannelParameters base = StyleProfile::baseline (role, bridge.readStyle());
    bridge.writeSubset (MacroMapping::apply (product.product, base, bridge.readMacroValues(), roleFamily (role)),
                        MacroMapping::affectedParameterIds (product.product));
}

void ChannelPluginProcessor::parameterChanged (const juce::String&, float)
{
    // May be called from any thread (host automation). Defer to the timer.
    if (! restoringState)
        macrosDirty.store (true, std::memory_order_release);
}

void ChannelPluginProcessor::registerWithKit()
{
    InstanceInfo info;
    info.role = bridge.readRole();
    info.group = groupName.toStdString();
    info.displayName = channelRoleName (info.role);
    info.endpoint = this;
    if (registryId == 0) registryId = InstanceRegistry::get().registerInstance (info);
    else InstanceRegistry::get().updateInstance (registryId, info);
}

void ChannelPluginProcessor::setGroupName (const juce::String& g)
{
    groupName = g.isEmpty() ? juce::String (product.defaultGroup) : g;
    registerWithKit();
}

// ---------------------------------------------------------------------------
// Tune
// ---------------------------------------------------------------------------
bool ChannelPluginProcessor::isAnalyzeAvailable() const
{
    return ! isLiveSafe();
}

void ChannelPluginProcessor::startAnalyze()
{
    if (! isAnalyzeAvailable()) return;
    if (analysis.getState() == AnalysisEngine::State::Capturing) return;
    // AI is switched off on purpose: Standard Tune only.
    // const bool wantAI = isAIAssistEnabled() && (! aiProviderSendsDataExternally() || aiConsentGiven.load());
    analyzeWithAI.store (false);
    analysisPending.store (true);
    outputPeakAbs.store (0.0f);
    outputSumSquares.store (0.0);
    outputSampleCount.store (0);
    collectOutputStats.store (true);
    analysis.startCapture (kTuneSeconds, kTuneTriggerDb, kTuneMaxWaitSeconds);
}

void ChannelPluginProcessor::abortAnalyze()
{
    collectOutputStats.store (false);
    analysis.abort();
    coordinator.cancel();
    analysisPending.store (false);
}

juce::String ChannelPluginProcessor::getAnalyzeButtonText() const
{
    if (isLiveSafe()) return "TUNE (LIVE SAFE)";
    if (analysis.getState() == AnalysisEngine::State::Waiting) return "WAITING FOR SIGNAL...";
    if (analysis.getState() == AnalysisEngine::State::Capturing) return "LISTENING...";
    return hasTunedThisSession() ? "RE-TUNE" : "TUNE";
}

juce::String ChannelPluginProcessor::getAnalyzeStatusText() const
{
    switch (analysis.getState())
    {
        case AnalysisEngine::State::Waiting:
            return juce::String ("Play normally. Dine starts listening as soon as it hears ") + product.sourceNoun + ".";
        case AnalysisEngine::State::Capturing:
            return "Play normally for " + juce::String (int (kTuneSeconds)) + " seconds... "
                   + juce::String (int (analysis.getProgress() * 100.0f)) + "%";
        case AnalysisEngine::State::Processing:
            return "Tuning...";
        case AnalysisEngine::State::Failed:
            return "Tune failed: not enough audio was captured. Audio processing was never interrupted.";
        case AnalysisEngine::State::Complete:
        {
            const auto stage = coordinator.getStage();
            if (stage == AnalyzeCoordinator::Stage::WaitingForAI) return "Standard results ready. Waiting for AI interpretation...";
            const auto status = coordinator.getStatusMessage();
            return status.empty() ? "Tune complete." : juce::String (status);
        }
        case AnalysisEngine::State::Idle:
        default:
            if (isLiveSafe()) return "Live Safe is on: Tune and automatic changes are disabled.";
            return juce::String ("Press Tune, then ") + product.playerPrompt + ".";
    }
}

void ChannelPluginProcessor::onAnalysisComplete()
{
    collectOutputStats.store (false);
    const AnalysisResult result = analysis.getResult();
    if (lastAnalysisForHistory.valid)
    {
        previousAnalysis = lastAnalysisForHistory;
        previousRecommendations = coordinator.getResult();
    }
    lastAnalysisForHistory = result;

    OutputStats out;
    const int64_t count = outputSampleCount.load();
    if (count > 0)
    {
        out.valid = true;
        out.peakDb = gainToDb (outputPeakAbs.load());
        out.rmsDb = gainToDb (float (std::sqrt (outputSumSquares.load() / double (count))));
    }
    lastOutputStats = out;

    AnalyzeCoordinator::Options options;
    options.useAI = false; // AI is switched off on purpose (AIFeature.h)
    options.aiTimeoutMs = 15000;

    std::vector<KitMember> kitContext;
    if (options.useAI)
        for (const auto& m : InstanceRegistry::get().membersOfGroup (groupName.toStdString()))
            if (m.id != registryId && m.endpoint != nullptr)
                kitContext.push_back (m.endpoint->kitGetMember());

    coordinator.run (result, bridge.readRole(), bridge.readStyle(), bridge.read(), options, out.valid ? &out : nullptr,
                     kitContext, groupName.toStdString());
    lastStage = coordinator.getStage();
    applyTunePreview();
    tuneCount.fetch_add (1);
    resultsVersion.fetch_add (1);
}

// ---------------------------------------------------------------------------
// Tune preview: BEFORE / AFTER, KEEP, REVERT
// ---------------------------------------------------------------------------
void ChannelPluginProcessor::applyTunePreview()
{
    tunePreviewActive = false;
    tuneShowingAfter = true;
    if (isLiveSafe()) return;
    const TuneResult t = coordinator.getTuneResult();
    if (! t.valid || t.parametersChanged == 0) return;
    tuneBefore = bridge.read();
    bridge.writeAll (t.proposed);
    tunePreviewActive = true;
}

void ChannelPluginProcessor::setTuneCompare (bool after)
{
    if (! tunePreviewActive || isLiveSafe() || after == tuneShowingAfter) return;
    const TuneResult t = coordinator.getTuneResult();
    bridge.writeAll (after ? t.proposed : tuneBefore);
    tuneShowingAfter = after;
}

void ChannelPluginProcessor::keepTune()
{
    if (! tunePreviewActive) return;
    if (! tuneShowingAfter && ! isLiveSafe()) bridge.writeAll (coordinator.getTuneResult().proposed);
    tunePreviewActive = false;
    tuneShowingAfter = true;
}

void ChannelPluginProcessor::revertTune()
{
    if (! tunePreviewActive) return;
    if (! isLiveSafe()) bridge.writeAll (tuneBefore);
    tunePreviewActive = false;
    tuneShowingAfter = true;
}

int ChannelPluginProcessor::undoTuneItem (const Recommendation& item)
{
    if (isLiveSafe()) return 0;
    ChannelParameters before = tuneBefore;
    int n = 0;
    for (const auto& c : item.changes)
    {
        forEachDspParameter (before, [&] (const std::string& id, auto& v)
        {
            if (id != c.paramId) return;
            bridge.setParameterValue (id, float (v));
            ++n;
        });
    }
    return n;
}

void ChannelPluginProcessor::timerCallback()
{
    if (macrosDirty.exchange (false))
        applyMacros();

    const auto state = analysis.getState();
    if (analysisPending.load() && (state == AnalysisEngine::State::Complete || state == AnalysisEngine::State::Failed))
    {
        analysisPending.store (false);
        if (state == AnalysisEngine::State::Complete) onAnalysisComplete();
    }

    const auto stage = coordinator.getStage();
    if (stage != lastStage)
    {
        lastStage = stage;
        resultsVersion.fetch_add (1);
    }

    if (isLiveSafe() && analysis.isActive())
        abortAnalyze();

    if (kit.poll (bridge.readStyle()))
        kitResultsVersion.fetch_add (1);
}

// ---------------------------------------------------------------------------
// Group intelligence
// ---------------------------------------------------------------------------
void ChannelPluginProcessor::startKitAnalyze()
{
    if (isLiveSafe()) return;
    kit.startKitAnalyze (groupName.toStdString());
    kitResultsVersion.fetch_add (1);
}

void ChannelPluginProcessor::abortKitAnalyze()
{
    for (const auto& m : InstanceRegistry::get().membersOfGroup (groupName.toStdString()))
        if (m.endpoint != nullptr) if (auto* dp = dynamic_cast<ChannelPluginProcessor*> (m.endpoint)) dp->abortAnalyze();
    kit.abort();
    kitResultsVersion.fetch_add (1);
}

void ChannelPluginProcessor::applyKitSafeChanges()
{
    if (isLiveSafe()) return;
    kit.applyAllSafeChanges (groupName.toStdString());
}

void ChannelPluginProcessor::applyKitBalance()
{
    if (isLiveSafe()) return;
    kit.applyBalance (groupName.toStdString());
}

bool ChannelPluginProcessor::kitIsAnalyzing() const
{
    const auto s = analysis.getState();
    return analysisPending.load() || s == AnalysisEngine::State::Waiting || s == AnalysisEngine::State::Capturing
        || s == AnalysisEngine::State::Processing || coordinator.isBusy();
}

KitMember ChannelPluginProcessor::kitGetMember() const
{
    KitMember m;
    m.instanceId = registryId;
    m.role = bridge.readRole();
    m.name = channelRoleName (m.role);
    m.analysis = analysis.getResult();
    m.recommendations = coordinator.getResult();
    m.output = lastOutputStats;
    m.outputTrimDb = bridge.getParameterValue (ParamID::outputTrim);
    return m;
}

void ChannelPluginProcessor::kitApplyOutputTrimDelta (float deltaDb)
{
    if (isLiveSafe()) return;
    const float current = bridge.getParameterValue (ParamID::outputTrim);
    bridge.setParameterValue (ParamID::outputTrim, juce::jlimit (-24.0f, 24.0f, current + deltaDb));
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
juce::ValueTree ChannelPluginProcessor::createPresetTree (const juce::String& name) const
{
    juce::ValueTree t (product.presetType);
    t.setProperty ("name", name, nullptr);
    t.setProperty ("role", int (bridge.readRole()), nullptr);
    t.setProperty ("profile", int (bridge.readStyle()), nullptr);
    const MacroValues m = bridge.readMacroValues();
    juce::ValueTree macros ("Macros");
    for (size_t i = 0; i < 5; ++i) macros.setProperty (product.macros[i].id, m.v[i], nullptr);
    t.addChild (macros, -1, nullptr);
    juce::ValueTree params ("Params");
    ChannelParameters p = bridge.read();
    forEachDspParameter (p, [&] (const std::string& id, auto& v)
    {
        if (id == ParamID::bypass || id == ParamID::abMatch) return;
        if (! productUsesParameter (product.product, id)) return;
        params.setProperty (juce::Identifier (id), float (v), nullptr);
    });
    t.addChild (params, -1, nullptr);
    return t;
}

bool ChannelPluginProcessor::loadPresetTree (const juce::ValueTree& preset)
{
    if (isLiveSafe() || ! preset.hasType (product.presetType)) return false;
    const juce::ScopedValueSetter<bool> restoring (restoringState, true); // knob writes must not re-derive parameters
    const ChannelRole role = channelRoleFromIndex (preset.getProperty ("role", int (product.defaultRole)));
    bridge.setRole (productOf (role) == product.product ? role : product.defaultRole);
    bridge.setParameterValue (ParamID::profile, float (int (styleProfileFromIndex (preset.getProperty ("profile", 0)))));
    auto macros = preset.getChildWithName ("Macros");
    if (macros.isValid())
        for (const auto& m : product.macros)
            if (macros.hasProperty (m.id)) bridge.setParameterValue (m.id, macros.getProperty (m.id));
    auto params = preset.getChildWithName ("Params");
    if (params.isValid())
    {
        ChannelParameters p = bridge.read();
        forEachDspParameter (p, [&] (const std::string& id, auto& v)
        {
            using T = std::remove_reference_t<decltype (v)>;
            const juce::Identifier ident (id);
            if (! params.hasProperty (ident)) return;
            const float raw = params.getProperty (ident);
            if constexpr (std::is_same_v<T, bool>) v = raw >= 0.5f;
            else if constexpr (std::is_same_v<T, int>) v = int (raw + 0.5f);
            else v = raw;
        });
        bridge.writeAll (p);
    }
    macrosDirty.store (false);
    currentPresetName = preset.getProperty ("name", "").toString();
    registerWithKit();
    return true;
}

bool ChannelPluginProcessor::loadPreset (const PresetManager::Info& info)
{
    const juce::ValueTree tree = info.isFactory ? PresetManager::factoryPresetTree (product, info) : PresetManager::loadUserPreset (product, info);
    return loadPresetTree (tree);
}

int ChannelPluginProcessor::applyRecommendation (const Recommendation& rec)
{
    if (isLiveSafe()) return 0;
    int n = 0;
    for (const auto& change : rec.changes)
    {
        bridge.setParameterValue (change.paramId, change.value);
        ++n;
    }
    return n;
}

int ChannelPluginProcessor::applySafeChanges()
{
    if (isLiveSafe()) return 0;
    int n = 0;
    for (const auto& item : coordinator.getResult().items)
        if (item.safeToAutoApply && ! item.changes.empty() && applyRecommendation (item) > 0) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// AI assistance (switched off on purpose; the code stays for a later release)
// ---------------------------------------------------------------------------
void ChannelPluginProcessor::setAIAssistEnabled (bool enabled)
{
    aiAssistEnabled.store (kAIAssistAvailable && enabled);
}

void ChannelPluginProcessor::stopAITest()
{
    if (! aiTestThread.joinable()) return;
    aiTestCancel.store (true);
    aiTestThread.join();
}

void ChannelPluginProcessor::testAIConnection (const AISettings& settings, std::function<void (bool, juce::String)> callback)
{
    if (! kAIAssistAvailable) { if (callback) callback (false, "AI assistance is switched off in this build."); return; }
    stopAITest();
    aiTestCancel.store (false);
    aiTestThread = std::thread ([settings, callback, cancel = &aiTestCancel]
    {
        juce::String message;
        const bool ok = OpenAIProvider::testConnection (settings, message, cancel);
        if (cancel->load()) return;
        juce::MessageManager::callAsync ([ok, message, callback] { if (callback) callback (ok, message); });
    });
}

bool ChannelPluginProcessor::aiProviderAvailable() const
{
    auto p = coordinator.getProvider();
    return kAIAssistAvailable && p != nullptr && p->isAvailable();
}

bool ChannelPluginProcessor::aiProviderSendsDataExternally() const
{
    auto p = coordinator.getProvider();
    return p != nullptr && p->sendsDataExternally();
}

juce::String ChannelPluginProcessor::getAIProviderName() const
{
    auto p = coordinator.getProvider();
    return p != nullptr ? juce::String (p->getName()) : "none";
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
ChannelPluginProcessor::Diagnostics ChannelPluginProcessor::getDiagnostics() const
{
    Diagnostics d;
    d.sampleRate = currentSampleRate.load();
    d.blockSize = currentBlockSize.load();
    d.numChannels = currentChannels.load();
    d.latencySamples = getLatencySamples();
    d.lastBlockMicros = lastBlockMicros.load();
    d.peakBlockMicros = peakBlockMicros.load();
    d.budgetMicros = d.sampleRate > 0.0 ? float (1.0e6 * d.blockSize / d.sampleRate) : 0.0f;
    d.analysisCapturing = analysis.isCapturing();
    d.fifoDroppedFrames = analysis.getResult().droppedFrames;
    d.overBudgetBlocks = overBudgetBlocks.load();
    d.totalBlocks = totalBlocks.load();
    return d;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void ChannelPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (product.stateType);
    root.setProperty ("version", kStateVersion, nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);

    juce::ValueTree extras ("Extras");
    extras.setProperty ("group", groupName, nullptr);
    extras.setProperty ("aiAssist", aiAssistEnabled.load(), nullptr);
    extras.setProperty ("aiConsent", aiConsentGiven.load(), nullptr);
    extras.setProperty ("advancedView", advancedView.load(), nullptr);
    extras.setProperty ("presetName", currentPresetName, nullptr);
    extras.setProperty ("editorWidth", editorWidth.load(), nullptr);
    extras.setProperty ("editorHeight", editorHeight.load(), nullptr);
    root.addChild (extras, -1, nullptr);

    root.addChild (analysisToTree (analysis.getResult()), -1, nullptr);
    root.addChild (recommendationsToTree (coordinator.getResult()), -1, nullptr);
    if (tunePreviewActive)
    {
        auto before = parametersToTree ("TuneBefore", tuneBefore);
        before.setProperty ("showingAfter", tuneShowingAfter, nullptr);
        root.addChild (before, -1, nullptr);
        root.addChild (parametersToTree ("TuneProposed", coordinator.getTuneResult().proposed), -1, nullptr);
    }

    juce::MemoryOutputStream stream (destData, false);
    root.writeToStream (stream);
}

void ChannelPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;

    juce::ValueTree root = juce::ValueTree::readFromData (data, size_t (sizeInBytes));
    if (! root.isValid() || ! root.hasType (product.stateType)) return; // malformed or another product: keep current state, never crash

    const juce::ScopedValueSetter<bool> restoring (restoringState, true);

    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    auto extras = root.getChildWithName ("Extras");
    if (extras.isValid())
    {
        groupName = extras.getProperty ("group", product.defaultGroup).toString();
        aiAssistEnabled.store (kAIAssistAvailable && bool (extras.getProperty ("aiAssist", false)));
        aiConsentGiven.store (extras.getProperty ("aiConsent", false));
        advancedView.store (extras.getProperty ("advancedView", false));
        currentPresetName = extras.getProperty ("presetName", "").toString();
        editorWidth.store (extras.getProperty ("editorWidth", 0));
        editorHeight.store (extras.getProperty ("editorHeight", 0));
    }

    const AnalysisResult restoredAnalysis = treeToAnalysis (root.getChildWithName ("Analysis"));
    if (restoredAnalysis.valid) { analysis.setResult (restoredAnalysis); lastAnalysisForHistory = restoredAnalysis; }
    const RecommendationResult restoredRecs = treeToRecommendations (root.getChildWithName ("Recommendations"));
    if (restoredRecs.valid) coordinator.setResult (restoredRecs);

    tunePreviewActive = false;
    tuneShowingAfter = true;
    auto beforeTree = root.getChildWithName ("TuneBefore");
    auto proposedTree = root.getChildWithName ("TuneProposed");
    if (restoredRecs.valid && beforeTree.isValid() && proposedTree.isValid())
    {
        ChannelParameters before = bridge.read(), proposed = bridge.read();
        if (treeToParameters (beforeTree, before) && treeToParameters (proposedTree, proposed))
        {
            tuneBefore = before;
            tuneShowingAfter = beforeTree.getProperty ("showingAfter", true);
            tunePreviewActive = true;
            TuneResult t = coordinator.getTuneResult();
            t.before = before;
            t.proposed = proposed;
            t.parametersChanged = int (diffParameters (before, proposed).size());
            t.noChangeRequired = t.parametersChanged == 0;
            coordinator.setTuneResult (t);
            tuneCount.store (1);
        }
    }

    // Parameters restored from the session are authoritative: no preset reload, no knob re-application.
    macrosDirty.store (false);
    resultsVersion.fetch_add (1);
    registerWithKit();
}

juce::AudioProcessorEditor* ChannelPluginProcessor::createEditor()
{
    return new ChannelPluginEditor (*this);
}

} // namespace livemix
