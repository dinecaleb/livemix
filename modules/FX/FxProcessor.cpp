#include "FxProcessor.h"
#include "FxEditor.h"
#include "State/ParameterLayout.h"
#include "State/ParameterIDs.h"
#include "FX/FxParameterSpecs.h"
#include "FX/FxProfiles.h"
#include "FX/FxMacroMapping.h"

namespace livemix
{

namespace
{
    constexpr int kStateVersion = 1;
}

FxProcessor::FxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LiveMixFX", createParameterLayout (fxParameterSpecs())),
      bridge (apvts)
{
    for (auto* id : { FxParamID::space, FxParamID::length, FxParamID::warmth, FxParamID::clarity, FxParamID::distance })
        apvts.addParameterListener (id, this);

    // A fresh instance starts on the factory baseline for Vocal Plate / Modern Gospel.
    reloadPreset();
    setLatencySamples (chain.getLatencySamples());
    startTimerHz (20);
}

FxProcessor::~FxProcessor()
{
    stopTimer();
}

// ---------------------------------------------------------------------------
// Real-time
// ---------------------------------------------------------------------------
void FxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (1, getTotalNumInputChannels());
    chain.prepare (sampleRate, samplesPerBlock, numChannels);
    chain.setTempo (tempo.load());
    chain.setParameters (bridge.read());
    tailSeconds.store (chain.getTailSeconds());
    setLatencySamples (chain.getLatencySamples());
    currentSampleRate.store (sampleRate);
    currentBlockSize.store (samplesPerBlock);
    currentChannels.store (numChannels);
    peakBlockMicros.store (0.0f);
    overBudgetBlocks.store (0);
    totalBlocks.store (0);
    budgetMicros = float (1.0e6 * samplesPerBlock / sampleRate);
}

bool FxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void FxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();

    const int numInputs = getTotalNumInputChannels();
    const int numOutputs = getTotalNumOutputChannels();
    for (int ch = numInputs; ch < numOutputs; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numChannels = juce::jmin (numInputs, kMaxChannels);
    if (numChannels <= 0 || buffer.getNumSamples() <= 0) return;

    // Host tempo for synced delays (no allocation; JUCE fills a small struct).
    if (auto* playHead = getPlayHead())
    {
        if (const auto pos = playHead->getPosition())
        {
            if (const auto bpm = pos->getBpm())
            {
                if (*bpm > 0.0) { tempo.store (*bpm, std::memory_order_relaxed); hostTempo.store (true, std::memory_order_relaxed); }
            }
        }
    }
    chain.setTempo (tempo.load (std::memory_order_relaxed));

    AudioBlockView block { buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples() };
    chain.setParameters (bridge.read());
    chain.process (block);
    tailSeconds.store (chain.getTailSeconds(), std::memory_order_relaxed);

    const double micros = 1.0e6 * double (juce::Time::getHighResolutionTicks() - startTicks) / double (juce::Time::getHighResolutionTicksPerSecond());
    lastBlockMicros.store (float (micros), std::memory_order_relaxed);
    if (micros > peakBlockMicros.load (std::memory_order_relaxed))
        peakBlockMicros.store (float (micros), std::memory_order_relaxed);
    totalBlocks.fetch_add (1, std::memory_order_relaxed);
    if (budgetMicros > 0.0f && micros > budgetMicros)
        overBudgetBlocks.fetch_add (1, std::memory_order_relaxed);
}

void FxProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numChannels = juce::jmin (getTotalNumInputChannels(), kMaxChannels);
    if (numChannels <= 0) return;
    AudioBlockView block { buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples() };
    chain.getInputMeter().process (block);
    chain.getOutputMeter().process (block);
}

FxProcessor::Diagnostics FxProcessor::getDiagnostics() const
{
    Diagnostics d;
    d.sampleRate = currentSampleRate.load();
    d.blockSize = currentBlockSize.load();
    d.numChannels = currentChannels.load();
    d.lastBlockMicros = lastBlockMicros.load();
    d.peakBlockMicros = peakBlockMicros.load();
    d.budgetMicros = budgetMicros;
    d.overBudgetBlocks = overBudgetBlocks.load();
    d.totalBlocks = totalBlocks.load();
    return d;
}

// ---------------------------------------------------------------------------
// Type / profile / macros / presets
// ---------------------------------------------------------------------------
void FxProcessor::setTypeFromUI (FxType type)
{
    if (isLiveSafe()) return;
    bridge.setParameterValue (FxParamID::fxType, float (int (type)));
    reloadPreset();
}

void FxProcessor::setProfileFromUI (StyleProfileId style)
{
    if (isLiveSafe()) return;
    bridge.setParameterValue (ParamID::profile, float (int (style)));
    reloadPreset();
}

void FxProcessor::reloadPreset()
{
    const FxType type = bridge.readType();
    const StyleProfileId style = bridge.readStyle();
    const FxParameters base = FxProfiles::baseline (style, type);
    bridge.writeAll (FxMacroMapping::apply (base, bridge.readMacros(), fxFamily (type)));
    currentPresetName = juce::String (styleProfileName (style)) + " / " + fxTypeName (type);
}

void FxProcessor::applyMacros()
{
    const FxType type = bridge.readType();
    const FxParameters base = FxProfiles::baseline (bridge.readStyle(), type);
    bridge.writeSubset (FxMacroMapping::apply (base, bridge.readMacros(), fxFamily (type)), FxMacroMapping::affectedParameterIds());
}

void FxProcessor::parameterChanged (const juce::String&, float)
{
    if (! restoringState) macrosDirty.store (true, std::memory_order_release); // any thread; handled on the timer
}

void FxProcessor::timerCallback()
{
    if (macrosDirty.exchange (false)) applyMacros();
}

juce::ValueTree FxProcessor::createPresetTree (const juce::String& name) const
{
    juce::ValueTree t ("LiveMixFxPreset");
    t.setProperty ("name", name, nullptr);
    t.setProperty ("type", int (bridge.readType()), nullptr);
    t.setProperty ("profile", int (bridge.readStyle()), nullptr);
    const FxMacros m = bridge.readMacros();
    juce::ValueTree macros ("Macros");
    macros.setProperty (FxParamID::space, m.space, nullptr);
    macros.setProperty (FxParamID::length, m.length, nullptr);
    macros.setProperty (FxParamID::warmth, m.warmth, nullptr);
    macros.setProperty (FxParamID::clarity, m.clarity, nullptr);
    macros.setProperty (FxParamID::distance, m.distance, nullptr);
    t.addChild (macros, -1, nullptr);
    juce::ValueTree params ("Params");
    FxParameters p = bridge.read();
    forEachFxParameter (p, [&] (const std::string& id, auto& v) { params.setProperty (juce::Identifier (id), float (v), nullptr); });
    t.addChild (params, -1, nullptr);
    return t;
}

bool FxProcessor::loadPresetTree (const juce::ValueTree& preset)
{
    if (! preset.isValid() || ! preset.hasType ("LiveMixFxPreset") || isLiveSafe()) return false;
    const juce::ScopedValueSetter<bool> restoring (restoringState, true);
    bridge.setParameterValue (FxParamID::fxType, float (int (fxTypeFromIndex (preset.getProperty ("type", 0)))));
    bridge.setParameterValue (ParamID::profile, float (int (styleProfileFromIndex (preset.getProperty ("profile", 0)))));
    auto macros = preset.getChildWithName ("Macros");
    if (macros.isValid())
        for (auto* id : { FxParamID::space, FxParamID::length, FxParamID::warmth, FxParamID::clarity, FxParamID::distance })
            if (macros.hasProperty (id)) bridge.setParameterValue (id, float (macros.getProperty (id)));
    auto params = preset.getChildWithName ("Params");
    if (params.isValid())
    {
        FxParameters p = bridge.read();
        forEachFxParameter (p, [&] (const std::string& id, auto& v)
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
    else
        reloadPreset();
    macrosDirty.store (false);
    currentPresetName = preset.getProperty ("name", "").toString();
    return true;
}

bool FxProcessor::loadPreset (const FxPresets::Info& info)
{
    return loadPresetTree (FxPresets::presetTree (info));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void FxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("LiveMixFxState");
    root.setProperty ("version", kStateVersion, nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);
    juce::ValueTree extras ("Extras");
    extras.setProperty ("advancedView", advancedView.load(), nullptr);
    extras.setProperty ("presetName", currentPresetName, nullptr);
    extras.setProperty ("editorWidth", editorWidth.load(), nullptr);
    extras.setProperty ("editorHeight", editorHeight.load(), nullptr);
    root.addChild (extras, -1, nullptr);
    juce::MemoryOutputStream stream (destData, false);
    root.writeToStream (stream);
}

void FxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;
    juce::ValueTree root = juce::ValueTree::readFromData (data, size_t (sizeInBytes));
    if (! root.isValid() || ! root.hasType ("LiveMixFxState")) return; // malformed: keep current state, never crash

    const juce::ScopedValueSetter<bool> restoring (restoringState, true);
    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);
    auto extras = root.getChildWithName ("Extras");
    if (extras.isValid())
    {
        advancedView.store (extras.getProperty ("advancedView", false));
        currentPresetName = extras.getProperty ("presetName", "").toString();
        editorWidth.store (extras.getProperty ("editorWidth", 0));
        editorHeight.store (extras.getProperty ("editorHeight", 0));
    }
    // Parameters restored from the session are authoritative: no preset reload, no macro re-application.
    macrosDirty.store (false);
}

juce::AudioProcessorEditor* FxProcessor::createEditor()
{
    return new FxEditor (*this);
}

} // namespace livemix

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new livemix::FxProcessor();
}
