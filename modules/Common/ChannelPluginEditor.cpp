#include "ChannelPluginEditor.h"
#include "State/ParameterIDs.h"
#include "Profiles/StyleProfile.h"
#include "Intelligence/AISettings.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

namespace
{
    constexpr int kDefaultWidth = 900;
    constexpr int kDefaultHeight = 650;
    constexpr int kMinWidth = 900;
    constexpr int kMinHeight = 650;
    constexpr float kTuneSeconds = 12.0f;
    constexpr int kUiHz = 60;

    enum MenuIds
    {
        idNone = 0,
        idModuleSelf = 1, idModuleOther = 2,
        idSourceBase = 100, idProfileBase = 200,
        idGroup = 300, idAISetup = 301, idAbMatch = 302, idAbout = 303, idSavePreset = 304,
        idFactoryBase = 1000, idUserBase = 5000, idDeleteBase = 9000
    };
}

// ---------------------------------------------------------------------------
int ChannelPluginEditor::Toast::idealWidth() const
{
    return int (juce::GlyphArrangement::getStringWidth (LNF::body (12.0f, 500), text)) + 34;
}

void ChannelPluginEditor::Toast::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds().toFloat();
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 16, { 0, 6 }).drawForRectangle (g, getLocalBounds());
    LNF::drawSurface (g, b, toastBg, accentDim, Tokens::Radius::control);
    g.setColour (textHi);
    g.setFont (LNF::body (12.0f, 500));
    g.drawText (text, getLocalBounds(), juce::Justification::centred);
}

// ---------------------------------------------------------------------------
ChannelPluginEditor::ChannelPluginEditor (ChannelPluginProcessor& p)
    : AudioProcessorEditor (&p), proc (p), product (p.getProduct()),
      topBar (p.getState()), simplePanel (p.getState(), product), advancedPanel (p.getState(), product)
{
    setLookAndFeel (&lookAndFeel);
    live.product = &product;

    addAndMakeVisible (topBar);
    topBar.setModuleName (product.shortName);
    topBar.setSourceCaption (product.sourceCaption);
    topBar.onModuleMenu = [this] (juce::Component& c) { showModuleMenu (c); };
    topBar.onSourceMenu = [this] (juce::Component& c) { showSourceMenu (c); };
    topBar.onProfileMenu = [this] (juce::Component& c) { showProfileMenu (c); };
    topBar.onSettingsMenu = [this] (juce::Component& c) { showSettingsMenu (c); };

    addAndMakeVisible (subBar);
    subBar.setKitTabVisible (product.hasKit);
    subBar.onViewChanged = [this] (SubBar::View v) { setView (v); };
    subBar.onShowResults = [this] { showLastAnalysis(); };

    addAndMakeVisible (simplePanel);
    simplePanel.onAnalyze = [this] { requestAnalyze(); };
    if (kAIAssistAvailable) simplePanel.onToggleAI = [this] { toggleAIAssist(); };
    simplePanel.onOpenModule = [this] (ChainModule m) { advancedPanel.setModule (m); setView (SubBar::View::Advanced); };

    addChildComponent (advancedPanel);
    advancedPanel.onApplySuggestion = [this] (const Recommendation& r) // UNDO one Tune decision
    {
        const int n = proc.undoTuneItem (r);
        const auto items = proc.getRecommendations().items;
        for (int i = 0; i < int (items.size()); ++i)
            if (items[size_t (i)].what == r.what) dismissed.insert (i);
        showToast (n > 0 ? "Undone: " + juce::String (r.what) + "  (" + juce::String (n) + (n == 1 ? " setting)" : " settings)") : "Nothing to undo.");
        updateSuggestions();
    };
    advancedPanel.onDismissSuggestion = [this] (const Recommendation& r)
    {
        const auto items = proc.getRecommendations().items;
        for (int i = 0; i < int (items.size()); ++i)
            if (items[size_t (i)].what == r.what) dismissed.insert (i);
        updateSuggestions();
    };

    addChildComponent (kitPanel);
    kitPanel.onAnalyzeKit = [this]
    {
        if (proc.isLiveSafe()) { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " switch it off to tune."); return; }
        proc.startKitAnalyze();
    };
    kitPanel.onCancel = [this] { proc.abortKitAnalyze(); };
    kitPanel.onApplyAllSafe = [this] { proc.applyKitSafeChanges(); showToast (juce::String ("Tune settings re-applied on every channel ") + Glyph::dash() + " preamp notes left for the console"); };
    kitPanel.onApplyBalance = [this] { proc.applyKitBalance(); showToast ("Balance trims applied to the listed channels."); };

    addChildComponent (overlay);
    overlay.setProduct (&product);
    overlay.onCancel = [this]
    {
        analyzeRequested = false;
        if (proc.getAnalysis().isActive()) proc.abortAnalyze();
        overlay.setPhase (AnalyzeOverlay::Phase::Hidden);
    };
    overlay.onKeep = [this] { keepTune(); };
    overlay.onRevert = [this] { revertTune(); };
    overlay.onCompare = [this] (bool after) { compareTune (after); };
    overlay.onReview = [this] { reviewAnalysisDetails(); };

    addChildComponent (toast);
    toast.setInterceptsMouseClicks (false, false);

    setView (proc.isAdvancedViewOpen() ? SubBar::View::Advanced : SubBar::View::Simple);
    constrainer.setSizeLimits (kMinWidth, kMinHeight, 2400, 1600);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const auto saved = proc.getEditorSize();
    setSize (saved.x >= kMinWidth ? saved.x : kDefaultWidth, saved.y >= kMinHeight ? saved.y : kDefaultHeight);
    startTimerHz (kUiHz);
}

ChannelPluginEditor::~ChannelPluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ChannelPluginEditor::dismissAnalyzeOverlay()
{
    analyzeRequested = false;
    overlay.setPhase (AnalyzeOverlay::Phase::Hidden);
}

void ChannelPluginEditor::showTuneCard()
{
    overlay.setSourceName (sourceName());
    overlay.setAIOn (false); // AI is switched off on purpose (AIFeature.h)
    overlay.setTuneResult (proc.getTuneResult(), proc.getCoordinator().getAIInterpretation(),
                           styleProfileName (proc.getBridge().readStyle()), proc.isTunePreviewActive(), proc.isTuneShowingAfter(),
                           proc.getAnalysis().getResult().durationSeconds);
    overlay.setPhase (AnalyzeOverlay::Phase::Results);
}

void ChannelPluginEditor::showLastAnalysis()
{
    if (! proc.getRecommendations().valid) return;
    analyzeRequested = true;
    showTuneCard();
}

void ChannelPluginEditor::keepTune()
{
    const auto t = proc.getTuneResult();
    const bool hadPreview = proc.isTunePreviewActive();
    proc.keepTune();
    dismissAnalyzeOverlay();
    if (hadPreview)
        showToast ("Tune kept " + Glyph::dash() + " " + juce::String (t.parametersChanged) + (t.parametersChanged == 1 ? " setting changed" : " settings changed"));
    updateSuggestions();
}

void ChannelPluginEditor::revertTune()
{
    proc.revertTune();
    dismissAnalyzeOverlay();
    showToast ("Tune reverted " + Glyph::dash() + " previous settings restored");
    dismissed.clear();
    for (int i = 0; i < int (proc.getRecommendations().items.size()); ++i) dismissed.insert (i); // nothing left to undo
    updateSuggestions();
}

void ChannelPluginEditor::compareTune (bool after)
{
    proc.setTuneCompare (after);
    overlay.setCompareState (proc.isTuneShowingAfter());
}

void ChannelPluginEditor::reviewAnalysisDetails()
{
    proc.keepTune();
    dismissAnalyzeOverlay();
    updateSuggestions();
    ChainModule target = ChainModule::EQ;
    const auto items = proc.getRecommendations().items;
    for (int i = 0; i < int (items.size()); ++i)
        if (! items[size_t (i)].changes.empty() && dismissed.count (i) == 0)
        { target = AdvancedPanel::moduleFor (items[size_t (i)]); break; }
    advancedPanel.setModule (target);
    setView (SubBar::View::Advanced);
}

juce::String ChannelPluginEditor::sourceName() const
{
    return juce::String (channelRoleName (proc.getBridge().readRole())).toUpperCase();
}

void ChannelPluginEditor::setView (SubBar::View v)
{
    if (v == SubBar::View::Kit && ! product.hasKit) v = SubBar::View::Simple;
    subBar.setView (v);
    simplePanel.setVisible (v == SubBar::View::Simple);
    advancedPanel.setVisible (v == SubBar::View::Advanced);
    kitPanel.setVisible (v == SubBar::View::Kit);
    proc.setAdvancedViewOpen (v == SubBar::View::Advanced);
    if (v == SubBar::View::Kit) shownKitVersion = 0xFFFFFFFF;
}

void ChannelPluginEditor::showToast (const juce::String& text)
{
    toast.show (text);
    toastTicks = kUiHz * 4 + kUiHz / 5;
    resized();
}

// ---------------------------------------------------------------------------
void ChannelPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (Tokens::window);
}

void ChannelPluginEditor::resized()
{
    proc.setEditorSize (getWidth(), getHeight());
    auto b = getLocalBounds();
    const int topH = TopBar::kHeight + (topBar.isLiveSafe() ? TopBar::kLiveSafeLine : 0);
    topBar.setBounds (b.removeFromTop (topH));
    subBar.setBounds (b.removeFromTop (SubBar::kHeight));
    simplePanel.setBounds (b);
    advancedPanel.setBounds (b);
    kitPanel.setBounds (b);
    overlay.setBounds (b);
    const int tw = juce::jmin (toast.idealWidth(), getWidth() - 40);
    toast.setBounds (juce::Rectangle<int> (0, 0, tw, 34).withCentre ({ getWidth() / 2, getHeight() - 14 - 17 }));
}

// ---------------------------------------------------------------------------
void ChannelPluginEditor::updateLiveState()
{
    const auto& ch = proc.getChannel();
    // Max since the last tick, not the last block: the timer sees one block in many and would miss short peaks.
    const float inPeak = ch.getInputMeter().consumeMaxPeakDb();
    const float outPeak = ch.getOutputMeter().consumeMaxPeakDb();
    live.params = proc.getBridge().read();
    live.sampleRate = proc.getDiagnostics().sampleRate > 0.0 ? proc.getDiagnostics().sampleRate : 48000.0;

    live.inPeakDb = inPeak > live.inPeakDb ? inPeak : juce::jmax (inPeak, live.inPeakDb - 0.6f);
    live.outPeakDb = outPeak > live.outPeakDb ? outPeak : juce::jmax (outPeak, live.outPeakDb - 0.6f);
    if (inPeak >= live.inHoldDb) { live.inHoldDb = inPeak; holdCounter = kUiHz * 2; }
    else if (--holdCounter <= 0) live.inHoldDb = juce::jmax (live.inPeakDb, live.inHoldDb - 1.5f);
    if (outPeak >= live.outHoldDb) { live.outHoldDb = outPeak; outHoldTicks = float (kUiHz * 2); }
    else if ((outHoldTicks -= 1.0f) <= 0.0f) live.outHoldDb = juce::jmax (live.outPeakDb, live.outHoldDb - 1.5f);
    if (inPeak >= live.inRecentMaxDb) { live.inRecentMaxDb = inPeak; recentCounter = kUiHz * 3; }
    else if (--recentCounter <= 0) live.inRecentMaxDb = juce::jmax (live.inPeakDb, live.inRecentMaxDb - 0.3f);
    live.inClipped = live.inRecentMaxDb >= -0.3f;
    live.outClipped = live.outHoldDb >= -0.3f;

    live.gateOpen = ! live.params.gateEnabled || ch.getGate().isOpen();
    live.gateEnvelope = live.gateOpen ? 1.0f : juce::jmax (0.0f, live.gateEnvelope - 0.02f * (120.0f / juce::jmax (20.0f, live.params.gateReleaseMs)));
    live.gateGrDb = ch.getGate().getGainReductionDb();
    live.compGrDb = juce::jmax (0.0f, -ch.getCompressor().getGainReductionDb());
    live.deEssGrDb = juce::jmax (0.0f, -ch.getDeEsser().getGainReductionDb());
    live.limiterGrDb = product.hasLoudness ? juce::jmax (0.0f, -ch.getLimiter().getGainReductionDb()) : 0.0f;
    live.correlation = ch.getWidth().getCorrelation();
    live.abMatchDb = ch.getLoudnessMatchGainDb();
    live.activity = MeterComponent::normFor (live.inPeakDb);
    if (product.hasLoudness)
    {
        const auto& lm = ch.getLoudness();
        live.momentaryLufs = lm.getMomentaryLufs();
        live.shortTermLufs = lm.getShortTermLufs();
        live.integratedLufs = lm.getIntegratedLufs();
        live.truePeakDb = lm.getTruePeakDb();
        if (live.truePeakDb >= live.truePeakHoldDb) { live.truePeakHoldDb = live.truePeakDb; peakHoldTicks = kUiHz * 3; }
        else if (--peakHoldTicks <= 0) live.truePeakHoldDb = juce::jmax (live.truePeakDb, live.truePeakHoldDb - 0.5f);
        const auto t = StyleProfile::targets (proc.getBridge().readRole(), proc.getBridge().readStyle());
        live.targetLufs = t.loudnessTargetAppropriate ? t.targetLufs : -120.0f;
    }
    live.history.push ({ inPeak, outPeak, live.gateOpen, live.compGrDb });
}

void ChannelPluginEditor::refreshShell()
{
    topBar.setSource (sourceName());
    topBar.setProfile (juce::String (styleProfileName (proc.getBridge().readStyle())).toUpperCase());
    const bool liveSafe = proc.isLiveSafe();
    subBar.setLiveSafe (liveSafe);
    subBar.setResultsAvailable (resultsValid);
    if (lastLiveSafe != liveSafe) { lastLiveSafe = liveSafe; resized(); }

    const auto d = proc.getDiagnostics();
    juce::String host = juce::AudioProcessor::getWrapperTypeDescription (proc.wrapperType);
    if (host == "AudioUnit") host = "AU";
    else if (host == "Standalone") host = "STANDALONE";
    else if (host == "Undefined") host = "OFFLINE";
    else host = host.toUpperCase();
    const float pct = d.budgetMicros > 0.0f ? 100.0f * d.peakBlockMicros / d.budgetMicros : 0.0f;
    juce::String diag = host + " " + Glyph::dot() + " " + juce::String (d.sampleRate / 1000.0, 1) + " kHz " + Glyph::dot() + " "
                      + juce::String (d.blockSize) + " smp " + Glyph::dot() + " DSP " + juce::String (juce::roundToInt (pct)) + " %";
    if (d.latencySamples > 0 && d.sampleRate > 0.0) diag += " " + Glyph::dot() + " " + juce::String (1000.0 * d.latencySamples / d.sampleRate, 1) + " ms";
    if (d.overBudgetBlocks > 0) diag += " " + Glyph::dot() + " OVERRUNS " + juce::String (d.overBudgetBlocks);
    subBar.setDiagnostics (diag, d.overBudgetBlocks > 0);

    SimplePanel::Status s;
    s.role = proc.getBridge().readRole();
    s.style = proc.getBridge().readStyle();
    s.liveSafe = liveSafe;
    s.aiOn = proc.isAIAssistEnabled();
    s.aiAvailable = proc.aiProviderAvailable();
    s.analyzeBusy = proc.getAnalysis().isActive() || proc.getAnalysis().getState() == AnalysisEngine::State::Processing;
    s.tuned = proc.hasTunedThisSession();
    if (simplePanel.isVisible()) simplePanel.update (live, s);
    if (advancedPanel.isVisible()) advancedPanel.update (live);
}

void ChannelPluginEditor::requestAnalyze()
{
    if (proc.isLiveSafe()) { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " switch it off to tune."); return; }
    if (proc.getAnalysis().isActive()) return;
    // AI is switched off on purpose: no consent prompt, Standard Tune only.
    beginSoundcheck();
}

void ChannelPluginEditor::beginSoundcheck()
{
    // A Re-Tune while a preview is still open keeps what is audible: the new Tune starts from it.
    proc.keepTune();
    analyzeRequested = true;
    resultsVersionAtRequest = proc.getResultsVersion();
    live.history.clear();
    overlay.setSourceName (sourceName());
    overlay.setAIOn (false);
    overlay.setPhase (AnalyzeOverlay::Phase::Setup);
    proc.startAnalyze(); // the engine waits for signal itself (State::Waiting)
}

void ChannelPluginEditor::refreshAnalyzeFlow()
{
    auto& analysis = proc.getAnalysis();
    if (! analyzeRequested) return;

    const auto state = analysis.getState();
    if (state == AnalysisEngine::State::Waiting)
    {
        overlay.setPhase (AnalyzeOverlay::Phase::Setup);
    }
    else if (analysis.isCapturing())
    {
        const float prog = analysis.getProgress();
        overlay.setPhase (AnalyzeOverlay::Phase::Listening);
        overlay.setListening ((1.0f - prog) * kTuneSeconds, prog, live.history, live.inPeakDb);
    }
    else if (state == AnalysisEngine::State::Processing)
    {
        overlay.setPhase (AnalyzeOverlay::Phase::Processing);
        overlay.setListening (0.0f, 1.0f, live.history, live.inPeakDb);
    }
    else if (state == AnalysisEngine::State::Failed && overlay.getPhase() != AnalyzeOverlay::Phase::Results)
    {
        analyzeRequested = false;
        overlay.setPhase (AnalyzeOverlay::Phase::Hidden);
        showToast (proc.getAnalyzeStatusText());
    }
    else if (overlay.getPhase() != AnalyzeOverlay::Phase::Results && proc.getResultsVersion() != resultsVersionAtRequest)
    {
        shownResultsVersion = 0xFFFFFFFF; // force refreshResults to (re)build the card
    }
}

void ChannelPluginEditor::refreshResults()
{
    const uint32_t version = proc.getResultsVersion();
    if (version == shownResultsVersion) return;
    shownResultsVersion = version;
    const auto rec = proc.getRecommendations();
    resultsValid = rec.valid;
    if (! rec.valid) { subBar.setResultsAvailable (false); advancedPanel.setSuggestions ({}); return; }
    bool sameTune = rec.items.size() >= shownKeys.size() && ! shownKeys.empty();
    for (size_t i = 0; sameTune && i < shownKeys.size(); ++i) sameTune = rec.items[i].what == shownKeys[i];
    if (! sameTune) dismissed.clear();
    shownKeys.clear();
    for (const auto& item : rec.items) shownKeys.push_back (item.what);
    if (analyzeRequested) showTuneCard();
    updateSuggestions();
}

void ChannelPluginEditor::updateSuggestions()
{
    std::vector<Recommendation> pending;
    const auto rec = proc.getRecommendations();
    for (int i = 0; i < int (rec.items.size()); ++i)
        if (! rec.items[size_t (i)].changes.empty() && dismissed.count (i) == 0)
            pending.push_back (rec.items[size_t (i)]);
    advancedPanel.setSuggestions (pending);
}

void ChannelPluginEditor::refreshKit()
{
    if (! kitPanel.isVisible()) return;
    kitPanel.setSelfLevel (live.inPeakDb, live.inHoldDb, live.inClipped);
    if (tick % 30 != 0) return;
    const auto members = proc.getKit().getGroupMembers (proc.getGroupName().toStdString());
    juce::String key;
    for (const auto& m : members) key += juce::String (m.id) + ":" + m.displayName + ";";
    if (key != shownMembers)
    {
        shownMembers = key;
        kitPanel.setMembers (members, proc.getInstanceId());
    }
    const uint32_t version = proc.getKitResultsVersion();
    const auto state = proc.getKit().getState();
    if (version != shownKitVersion || state == KitController::State::Analyzing)
    {
        shownKitVersion = version;
        kitPanel.setResult (proc.getKit().getResult(), state, proc.isLiveSafe());
    }
    if (proc.isLiveSafe()) kitPanel.setStatus (juce::String ("Live Safe is on ") + Glyph::dash() + " kit tuning is off.");
    else if (state == KitController::State::Analyzing) kitPanel.setStatus ("Tuning " + juce::String (int (members.size())) + " channels" + Glyph::ellip() + " have the drummer play the whole kit.");
    else if (members.size() < 2) kitPanel.setStatus ("Add Dine to more drum channels with group \"" + proc.getGroupName() + "\" to tune the kit together.");
    else kitPanel.setStatus ("Ready " + Glyph::dot() + " " + juce::String (int (members.size())) + " channels in group \"" + proc.getGroupName() + "\"");
}

void ChannelPluginEditor::timerCallback()
{
    ++tick;
    updateLiveState();
    refreshShell();
    refreshAnalyzeFlow();
    refreshResults();
    refreshKit();
    if (toastTicks > 0 && --toastTicks == 0) toast.setVisible (false);
}

// ---------------------------------------------------------------------------
void ChannelPluginEditor::toggleAIAssist()
{
    if (! kAIAssistAvailable) { showToast ("AI Assist is switched off " + Glyph::dash() + " Standard Tune only."); return; }
    if (proc.isLiveSafe()) { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " AI Assist stays inactive."); return; }
    const bool enable = ! proc.isAIAssistEnabled();
    proc.setAIAssistEnabled (enable);
    if (enable && ! proc.aiProviderAvailable()) showAISettings();
}

void ChannelPluginEditor::showModuleMenu (juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addItem (idModuleSelf, juce::String (product.name).toUpperCase() + "\tactive", true, true);
    for (int i = 0; i < int (Product::Count); ++i)
        if (Product (i) != product.product)
            m.addItem (idModuleOther, juce::String (productDefinition (Product (i)).name).toUpperCase() + "\tinstall separately", false);
    m.addItem (idModuleOther, "DINE FX\tinstall separately", false);
    for (auto* n : { "BASS", "GUITAR" }) m.addItem (idModuleOther, juce::String ("DINE ") + n + "\troadmap", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (230), nullptr);
}

void ChannelPluginEditor::showSourceMenu (juce::Component& anchor)
{
    if (proc.isLiveSafe()) { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " the source is locked."); return; }
    juce::PopupMenu m;
    const ChannelRole cur = proc.getBridge().readRole();
    for (size_t i = 0; i < product.roles.size(); ++i)
    {
        const ChannelRole r = product.roles[i];
        m.addItem (idSourceBase + int (i), juce::String (channelRoleName (r)).toUpperCase() + "\t" + (r == cur ? "selected" : juce::String (juce::CharPointer_UTF8 (roleHint (r)))), true, r == cur);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (260),
        [safe = juce::Component::SafePointer<ChannelPluginEditor> (this)] (int id)
        {
            if (safe == nullptr || id < idSourceBase || id >= idSourceBase + int (safe->product.roles.size())) return;
            const ChannelRole r = safe->product.roles[size_t (id - idSourceBase)];
            if (r != safe->proc.getBridge().readRole()) safe->proc.setRoleFromUI (r);
        });
}

void ChannelPluginEditor::showProfileMenu (juce::Component& anchor)
{
    if (proc.isLiveSafe()) { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " the profile is locked."); return; }
    juce::PopupMenu m;
    const int cur = int (proc.getBridge().readStyle());
    for (int i = 0; i < int (StyleProfileId::Count); ++i)
        m.addItem (idProfileBase + i, juce::String (styleProfileName (StyleProfileId (i))).toUpperCase() + (i == cur ? "\tselected" : ""), true, i == cur);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (230),
        [safe = juce::Component::SafePointer<ChannelPluginEditor> (this)] (int id)
        {
            if (safe == nullptr || id < idProfileBase || id >= idProfileBase + int (StyleProfileId::Count)) return;
            const int idx = id - idProfileBase;
            if (idx != int (safe->proc.getBridge().readStyle())) safe->proc.setProfileFromUI (styleProfileFromIndex (idx));
        });
}

void ChannelPluginEditor::showPresetMenu (juce::PopupMenu& into)
{
    factoryPresets = PresetManager::getFactoryPresets (product);
    userPresets = PresetManager::getUserPresets (product);
    juce::PopupMenu factory;
    juce::PopupMenu byStyle[int (StyleProfileId::Count)];
    for (size_t i = 0; i < factoryPresets.size(); ++i)
        byStyle[int (factoryPresets[i].style)].addItem (idFactoryBase + int (i), channelRoleName (factoryPresets[i].role));
    for (int st = 0; st < int (StyleProfileId::Count); ++st)
        factory.addSubMenu (styleProfileName (StyleProfileId (st)), byStyle[st]);
    into.addSubMenu ("Starting points", factory);
    juce::PopupMenu user;
    for (size_t i = 0; i < userPresets.size(); ++i) user.addItem (idUserBase + int (i), userPresets[i].name);
    if (userPresets.empty()) user.addItem (idNone, "(no saved presets yet)", false);
    into.addSubMenu ("My presets", user);
    into.addItem (idSavePreset, "Save these settings as a preset" + Glyph::ellip());
    if (! userPresets.empty())
    {
        juce::PopupMenu del;
        for (size_t i = 0; i < userPresets.size(); ++i) del.addItem (idDeleteBase + int (i), userPresets[i].name);
        into.addSubMenu ("Delete a preset", del);
    }
}

void ChannelPluginEditor::handlePresetChoice (int id)
{
    const bool liveSafe = proc.isLiveSafe();
    auto locked = [&] { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " presets are locked."); };
    if (id >= idFactoryBase && id < idFactoryBase + int (factoryPresets.size()))
    {
        if (liveSafe) { locked(); return; }
        proc.loadPreset (factoryPresets[size_t (id - idFactoryBase)]);
        showToast ("Loaded: " + proc.getCurrentPresetName());
    }
    else if (id >= idUserBase && id < idUserBase + int (userPresets.size()))
    {
        if (liveSafe) { locked(); return; }
        proc.loadPreset (userPresets[size_t (id - idUserBase)]);
        showToast ("Loaded: " + proc.getCurrentPresetName());
    }
    else if (id == idSavePreset)
    {
        auto* window = new juce::AlertWindow ("Save preset", "Give these settings a name:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", proc.getCurrentPresetName(), "Name");
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ChannelPluginEditor> (this), window] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owner (window);
            if (result != 1 || safe == nullptr) return;
            const auto name = window->getTextEditorContents ("name");
            PresetManager::Info saved;
            if (PresetManager::saveUserPreset (safe->product, name, safe->proc.createPresetTree (name), &saved))
            {
                safe->proc.loadPreset (saved);
                safe->showToast ("Saved: " + saved.name);
            }
        }), false);
    }
    else if (id >= idDeleteBase && id < idDeleteBase + int (userPresets.size()))
    {
        PresetManager::deleteUserPreset (userPresets[size_t (id - idDeleteBase)]);
        showToast ("Preset deleted.");
    }
}

void ChannelPluginEditor::showSettingsMenu (juce::Component& anchor)
{
    juce::PopupMenu m;
    showPresetMenu (m);
    m.addSeparator();
    m.addItem (idGroup, juce::String (product.hasKit ? "Kit group\t" : "Group\t") + proc.getGroupName());
    if (kAIAssistAvailable)
        m.addItem (idAISetup, "AI assist setup" + Glyph::ellip() + "\t" + (proc.aiProviderAvailable() ? "key set" : "no key"));
    const bool abMatch = proc.getBridge().read().abLoudnessMatch;
    m.addItem (idAbMatch, juce::String ("Match loudness when comparing\t") + (abMatch ? "on" : "off"), true, abMatch);
    m.addSeparator();
    m.addItem (idAbout, "About " + juce::String (product.name) + "\t" + juce::String (JucePlugin_VersionString), false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (230),
        [safe = juce::Component::SafePointer<ChannelPluginEditor> (this)] (int id)
        {
            if (safe == nullptr || id == idNone) return;
            if (id == idGroup) safe->showGroupDialog();
            else if (id == idAISetup) safe->showAISettings();
            else if (id == idAbMatch) safe->proc.getBridge().setParameterValue (ParamID::abMatch, safe->proc.getBridge().read().abLoudnessMatch ? 0.0f : 1.0f);
            else safe->handlePresetChoice (id);
        });
}

void ChannelPluginEditor::showGroupDialog()
{
    auto* window = new juce::AlertWindow (product.hasKit ? "Kit group" : "Group",
                                          product.hasKit ? "Channels with the same group name form a kit and can be tuned together."
                                                         : "Channels with the same group name belong together (for example all the backing vocals).",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("group", proc.getGroupName(), "Group name");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ChannelPluginEditor> (this), window] (int result)
    {
        std::unique_ptr<juce::AlertWindow> owner (window);
        if (result != 1 || safe == nullptr) return;
        safe->proc.setGroupName (window->getTextEditorContents ("group").trim());
        safe->shownMembers.clear();
    }), false);
}

void ChannelPluginEditor::showAISettings()
{
    if (! kAIAssistAvailable) { showToast ("AI Assist is switched off " + Glyph::dash() + " Standard Tune only."); return; }
    const AISettings current = AISettings::reload();
    auto* window = new juce::AlertWindow ("AI assistance (OpenAI)",
        "Optional. When AI Assist is on and you press Tune, the measured analysis data for this channel "
        "(levels, spectrum, dynamics, settings, other channels' measurements - never audio) is sent to OpenAI "
        "and its suggestions are shown next to the Standard results after safety validation. Nothing is sent "
        "during normal processing. The key is stored in ~/Library/Application Support/LiveMix, not in the session.",
        juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("key", current.apiKey, "OpenAI API key", true);
    window->addComboBox ("model", AISettings::availableModels(), "Model");
    if (auto* box = window->getComboBoxComponent ("model"))
    {
        box->setEditableText (true);
        box->setText (current.model, juce::dontSendNotification);
    }
    window->addComboBox ("effort", { "low", "medium", "high" }, "Reasoning effort");
    if (auto* box = window->getComboBoxComponent ("effort")) box->setText (current.effort, juce::dontSendNotification);
    window->addTextEditor ("timeout", juce::String (current.timeoutSeconds), "Timeout (seconds)");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Test & Save", 2);
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<ChannelPluginEditor> (this), window] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owner (window);
            if (result == 0 || safe == nullptr) return;
            AISettings s;
            s.apiKey = window->getTextEditorContents ("key").trim();
            if (auto* box = window->getComboBoxComponent ("model")) s.model = box->getText().trim();
            if (auto* box = window->getComboBoxComponent ("effort")) s.effort = box->getText().trim();
            s.timeoutSeconds = window->getTextEditorContents ("timeout").getIntValue();
            AISettings::save (s);
            const AISettings saved = AISettings::load();
            if (result == 2)
            {
                safe->showToast ("Testing connection to OpenAI" + Glyph::ellip());
                safe->proc.testAIConnection (saved, [safe] (bool ok, juce::String message)
                {
                    if (safe == nullptr) return;
                    safe->showToast ((ok ? "AI ready. " : "AI test failed: ") + message);
                });
            }
            else
                safe->showToast (saved.hasKey() ? "AI settings saved. Model: " + saved.model : "AI settings saved (no key: Standard Tune will be used).");
        }), false);
}

} // namespace livemix
