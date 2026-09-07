#include "FxEditor.h"
#include "State/ParameterIDs.h"
#include "FX/FxProfiles.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

namespace
{
    constexpr int kDefaultWidth = 900, kDefaultHeight = 600, kMinWidth = 900, kMinHeight = 560;
    constexpr int kUiHz = 60;

    enum MenuIds
    {
        idNone = 0,
        idModuleFx = 1, idModuleOther = 2,
        idTypeBase = 100, idProfileBase = 200,
        idAbMatch = 302, idAbout = 303, idSavePreset = 304,
        idFactoryBase = 1000, idUseBase = 4000, idUserBase = 5000, idDeleteBase = 9000
    };
}

int FxEditor::Toast::idealWidth() const
{
    return int (juce::GlyphArrangement::getStringWidth (LNF::body (12.0f, 500), text)) + 34;
}

void FxEditor::Toast::paint (juce::Graphics& g)
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
FxEditor::FxEditor (FxProcessor& p)
    : AudioProcessorEditor (&p), fx (p),
      topBar (p.getState()), simplePanel (p.getState()), advancedPanel (p.getState())
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (topBar);
    topBar.setModuleName ("FX");
    topBar.setSourceCaption ("TYPE");
    topBar.onModuleMenu = [this] (juce::Component& c) { showModuleMenu (c); };
    topBar.onSourceMenu = [this] (juce::Component& c) { showTypeMenu (c); };
    topBar.onProfileMenu = [this] (juce::Component& c) { showProfileMenu (c); };
    topBar.onSettingsMenu = [this] (juce::Component& c) { showSettingsMenu (c); };

    addAndMakeVisible (subBar);
    subBar.setKitTabVisible (false);
    subBar.onViewChanged = [this] (SubBar::View v) { setView (v); };

    addAndMakeVisible (simplePanel);
    simplePanel.onOpenModule = [this] (FxModule m) { advancedPanel.setModule (m); setView (SubBar::View::Advanced); };
    addChildComponent (advancedPanel);

    addChildComponent (toast);
    toast.setInterceptsMouseClicks (false, false);

    setView (fx.isAdvancedViewOpen() ? SubBar::View::Advanced : SubBar::View::Simple);
    constrainer.setSizeLimits (kMinWidth, kMinHeight, 2400, 1600);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const auto saved = fx.getEditorSize();
    setSize (saved.x >= kMinWidth ? saved.x : kDefaultWidth, saved.y >= kMinHeight ? saved.y : kDefaultHeight);
    startTimerHz (kUiHz);
}

FxEditor::~FxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void FxEditor::setView (SubBar::View v)
{
    if (v == SubBar::View::Kit) v = SubBar::View::Simple;
    subBar.setView (v);
    simplePanel.setVisible (v == SubBar::View::Simple);
    advancedPanel.setVisible (v == SubBar::View::Advanced);
    fx.setAdvancedViewOpen (v == SubBar::View::Advanced);
}

void FxEditor::showToast (const juce::String& text)
{
    toast.show (text);
    toastTicks = kUiHz * 4 + kUiHz / 5;
    resized();
}

void FxEditor::paint (juce::Graphics& g)
{
    g.fillAll (Tokens::window);
}

void FxEditor::resized()
{
    fx.setEditorSize (getWidth(), getHeight());
    auto b = getLocalBounds();
    const int topH = TopBar::kHeight + (topBar.isLiveSafe() ? TopBar::kLiveSafeLine : 0);
    topBar.setBounds (b.removeFromTop (topH));
    subBar.setBounds (b.removeFromTop (SubBar::kHeight));
    simplePanel.setBounds (b);
    advancedPanel.setBounds (b);
    const int tw = juce::jmin (toast.idealWidth(), getWidth() - 40);
    toast.setBounds (juce::Rectangle<int> (0, 0, tw, 34).withCentre ({ getWidth() / 2, getHeight() - 14 - 17 }));
}

// ---------------------------------------------------------------------------
void FxEditor::updateLiveState()
{
    const auto& chain = fx.getChain();
    const float inPeak = chain.getInputMeter().consumeMaxPeakDb();   // max since the last tick, not the last block
    const float outPeak = chain.getOutputMeter().consumeMaxPeakDb();
    live.params = fx.getBridge().read();
    live.inPeakDb = inPeak > live.inPeakDb ? inPeak : juce::jmax (inPeak, live.inPeakDb - 0.6f);
    live.outPeakDb = outPeak > live.outPeakDb ? outPeak : juce::jmax (outPeak, live.outPeakDb - 0.6f);
    if (inPeak >= live.inHoldDb) { live.inHoldDb = inPeak; holdCounter = kUiHz * 2; }
    else if (--holdCounter <= 0) live.inHoldDb = juce::jmax (live.inPeakDb, live.inHoldDb - 1.5f);
    if (outPeak >= live.outHoldDb) { live.outHoldDb = outPeak; outHoldCounter = kUiHz * 2; }
    else if (--outHoldCounter <= 0) live.outHoldDb = juce::jmax (live.outPeakDb, live.outHoldDb - 1.5f);
    live.inClipped = live.inHoldDb >= -0.3f;
    live.outClipped = live.outHoldDb >= -0.3f;
    live.duckDb = chain.getDelay().getDuckingDb();
    live.delayMsL = chain.getDelay().getTimeMs (0);
    live.delayMsR = chain.getDelay().getTimeMs (1);
    live.tempo = fx.getTempo();
    live.hostTempo = fx.hasHostTempo();
    live.matchDb = chain.getLoudnessMatchGainDb();
    live.activity = MeterComponent::normFor (live.inPeakDb);
}

void FxEditor::refreshShell()
{
    const FxType type = fx.getBridge().readType();
    topBar.setSource (juce::String (fxTypeName (type)).toUpperCase());
    topBar.setProfile (juce::String (styleProfileName (fx.getBridge().readStyle())).toUpperCase());
    const bool liveSafe = fx.isLiveSafe();
    subBar.setLiveSafe (liveSafe);
    if (lastLiveSafe != liveSafe) { lastLiveSafe = liveSafe; resized(); }

    const auto d = fx.getDiagnostics();
    juce::String host = juce::AudioProcessor::getWrapperTypeDescription (fx.wrapperType);
    if (host == "AudioUnit") host = "AU";
    else if (host == "Standalone") host = "STANDALONE";
    else if (host == "Undefined") host = "OFFLINE";
    else host = host.toUpperCase();
    const float pctv = d.budgetMicros > 0.0f ? 100.0f * d.peakBlockMicros / d.budgetMicros : 0.0f;
    juce::String diag = host + " " + Glyph::dot() + " " + juce::String (d.sampleRate / 1000.0, 1) + " kHz " + Glyph::dot() + " "
                      + juce::String (d.blockSize) + " smp " + Glyph::dot() + " DSP " + juce::String (juce::roundToInt (pctv)) + " %";
    if (d.overBudgetBlocks > 0) diag += " " + Glyph::dot() + " OVERRUNS " + juce::String (d.overBudgetBlocks);
    subBar.setDiagnostics (diag, d.overBudgetBlocks > 0);

    FxSimplePanel::Status s;
    s.type = type;
    s.style = fx.getBridge().readStyle();
    s.liveSafe = liveSafe;
    if (simplePanel.isVisible()) simplePanel.update (live, s);
    if (advancedPanel.isVisible())
    {
        advancedPanel.setIntent (juce::String (fxTypeName (type)).toUpperCase() + " " + Glyph::dot() + " " + juce::String (juce::CharPointer_UTF8 (FxProfiles::intent (type))));
        advancedPanel.update (live);
    }
}

void FxEditor::timerCallback()
{
    updateLiveState();
    refreshShell();
    if (toastTicks > 0 && --toastTicks == 0) toast.setVisible (false);
}

// ---------------------------------------------------------------------------
void FxEditor::showModuleMenu (juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addItem (idModuleFx, "DINE FX\tactive", true, true);
    for (auto* n : { "DRUMS", "VOCALS", "KEYS", "MASTER" }) m.addItem (idModuleOther, juce::String ("DINE ") + n + "\tinstall separately", false);
    for (auto* n : { "BASS", "GUITAR" }) m.addItem (idModuleOther, juce::String ("DINE ") + n + "\troadmap", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (230), nullptr);
}

void FxEditor::showTypeMenu (juce::Component& anchor)
{
    if (fx.isLiveSafe()) { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " type is locked."); return; }
    juce::PopupMenu m;
    const int cur = int (fx.getBridge().readType());
    for (int i = 0; i < int (FxType::Count); ++i)
    {
        if (i == int (FxType::SlapDelay)) m.addSeparator();
        m.addItem (idTypeBase + i, juce::String (fxTypeName (FxType (i))).toUpperCase() + "\t" + (i == cur ? "selected" : juce::String (juce::CharPointer_UTF8 (FxProfiles::intent (FxType (i))))), true, i == cur);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (260),
        [safe = juce::Component::SafePointer<FxEditor> (this)] (int id)
        {
            if (safe == nullptr || id < idTypeBase || id >= idTypeBase + int (FxType::Count)) return;
            const int idx = id - idTypeBase;
            if (idx != int (safe->fx.getBridge().readType())) safe->fx.setTypeFromUI (fxTypeFromIndex (idx));
        });
}

void FxEditor::showProfileMenu (juce::Component& anchor)
{
    if (fx.isLiveSafe()) { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " profile is locked."); return; }
    juce::PopupMenu m;
    const int cur = int (fx.getBridge().readStyle());
    for (int i = 0; i < int (StyleProfileId::Count); ++i)
        m.addItem (idProfileBase + i, juce::String (styleProfileName (StyleProfileId (i))).toUpperCase() + (i == cur ? "\tselected" : ""), true, i == cur);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (230),
        [safe = juce::Component::SafePointer<FxEditor> (this)] (int id)
        {
            if (safe == nullptr || id < idProfileBase || id >= idProfileBase + int (StyleProfileId::Count)) return;
            const int idx = id - idProfileBase;
            if (idx != int (safe->fx.getBridge().readStyle())) safe->fx.setProfileFromUI (styleProfileFromIndex (idx));
        });
}

void FxEditor::showPresetMenu (juce::PopupMenu& into)
{
    factoryPresets = FxPresets::getFactoryPresets();
    usePresets = FxPresets::getUsePresets();
    userPresets = FxPresets::getUserPresets();
    juce::PopupMenu use;
    for (size_t i = 0; i < usePresets.size(); ++i) use.addItem (idUseBase + int (i), usePresets[i].name);
    into.addSubMenu ("Starting points", use);
    juce::PopupMenu factory;
    juce::PopupMenu byStyle[int (StyleProfileId::Count)];
    for (size_t i = 0; i < factoryPresets.size(); ++i)
        byStyle[int (factoryPresets[i].style)].addItem (idFactoryBase + int (i), fxTypeName (factoryPresets[i].type));
    for (int st = 0; st < int (StyleProfileId::Count); ++st) factory.addSubMenu (styleProfileName (StyleProfileId (st)), byStyle[st]);
    into.addSubMenu ("Factory presets", factory);
    juce::PopupMenu user;
    for (size_t i = 0; i < userPresets.size(); ++i) user.addItem (idUserBase + int (i), userPresets[i].name);
    if (userPresets.empty()) user.addItem (idNone, "(no user presets)", false);
    into.addSubMenu ("User presets", user);
    into.addItem (idSavePreset, "Save as user preset" + Glyph::ellip());
    if (! userPresets.empty())
    {
        juce::PopupMenu del;
        for (size_t i = 0; i < userPresets.size(); ++i) del.addItem (idDeleteBase + int (i), userPresets[i].name);
        into.addSubMenu ("Delete user preset", del);
    }
}

void FxEditor::handlePresetChoice (int id)
{
    const bool liveSafe = fx.isLiveSafe();
    auto locked = [&] { showToast (juce::String ("Live Safe is on ") + Glyph::dash() + " presets are locked."); };
    if (id >= idFactoryBase && id < idFactoryBase + int (factoryPresets.size()))
    {
        if (liveSafe) { locked(); return; }
        fx.loadPreset (factoryPresets[size_t (id - idFactoryBase)]);
        showToast ("Preset loaded: " + fx.getCurrentPresetName());
    }
    else if (id >= idUseBase && id < idUseBase + int (usePresets.size()))
    {
        if (liveSafe) { locked(); return; }
        fx.loadPreset (usePresets[size_t (id - idUseBase)]);
        showToast ("Starting point loaded: " + fx.getCurrentPresetName());
    }
    else if (id >= idUserBase && id < idUserBase + int (userPresets.size()))
    {
        if (liveSafe) { locked(); return; }
        fx.loadPreset (userPresets[size_t (id - idUserBase)]);
        showToast ("Preset loaded: " + fx.getCurrentPresetName());
    }
    else if (id == idSavePreset)
    {
        auto* window = new juce::AlertWindow ("Save user preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", fx.getCurrentPresetName(), "Name");
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<FxEditor> (this), window] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owner (window);
            if (result != 1 || safe == nullptr) return;
            const auto name = window->getTextEditorContents ("name");
            FxPresets::Info saved;
            if (FxPresets::saveUserPreset (name, safe->fx.createPresetTree (name), &saved))
            {
                safe->fx.loadPreset (saved);
                safe->showToast ("Preset saved: " + saved.name);
            }
        }), false);
    }
    else if (id >= idDeleteBase && id < idDeleteBase + int (userPresets.size()))
    {
        FxPresets::deleteUserPreset (userPresets[size_t (id - idDeleteBase)]);
        showToast ("Preset deleted.");
    }
}

void FxEditor::showSettingsMenu (juce::Component& anchor)
{
    juce::PopupMenu m;
    showPresetMenu (m);
    m.addSeparator();
    const bool abMatch = fx.getBridge().read().abLoudnessMatch;
    m.addItem (idAbMatch, juce::String ("A/B loudness match\t") + (abMatch ? "on" : "off"), true, abMatch);
    m.addSeparator();
    m.addItem (idAbout, "About DINE\t" + juce::String (JucePlugin_VersionString), false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMinimumWidth (230),
        [safe = juce::Component::SafePointer<FxEditor> (this)] (int id)
        {
            if (safe == nullptr || id == idNone) return;
            if (id == idAbMatch) safe->fx.getBridge().setParameterValue (ParamID::abMatch, safe->fx.getBridge().read().abLoudnessMatch ? 0.0f : 1.0f);
            else safe->handlePresetChoice (id);
        });
}

} // namespace livemix
