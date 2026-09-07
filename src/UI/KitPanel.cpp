#include "KitPanel.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

class KitPanel::Row : public juce::Component
{
public:
    static constexpr int kHeight = 40;
    Row (const InstanceInfo& info, bool self) : name (juce::String (info.displayName).toUpperCase()), isSelf (self), id (info.id)
    {
        meter = std::make_unique<MeterComponent> (MeterComponent::Orientation::Horizontal);
        addAndMakeVisible (*meter);
        refreshCapture (info.endpoint);
    }
    // Re-reads the instance's latest capture. The endpoint is looked up fresh from the
    // registry by the caller on every refresh - never cached here - because the instance
    // (another plugin in the host) can be removed at any time between refreshes.
    void refreshCapture (IKitEndpoint* endpoint)
    {
        if (endpoint == nullptr) return;
        const auto m = endpoint->kitGetMember();
        if (m.analysis.valid) { lastPeakDb = m.analysis.peakDb; hasCapture = true; }
        if (! isSelf && hasCapture) meter->setLevels (lastPeakDb, lastPeakDb, false);
    }
    void setSelfLevel (float peak, float hold, bool clipped) { if (isSelf) { meter->setLevels (peak, hold, clipped); livePeak = hold; repaint(); } }
    void setStatus (const juce::String& health, const juce::String& rec, bool analyzed)
    {
        healthText = health; recText = rec; hasResult = analyzed;
        repaint();
    }
    uint64_t getId() const { return id; }

    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        LNF::drawSurface (g, b, rowBg, hairRow, Tokens::Radius::control);
        auto inner = getLocalBounds().reduced (14, 0);
        g.setColour (textHi);
        g.setFont (LNF::body (12.5f, 600));
        g.drawText (name, inner.removeFromLeft (100), juce::Justification::centredLeft, true);
        inner.removeFromLeft (130 + 12);
        const float db = isSelf ? livePeak : lastPeakDb;
        g.setColour (textMid);
        g.setFont (LNF::mono (11.0f, 500));
        const bool haveDb = isSelf || hasCapture;
        g.drawText (haveDb ? ((db > 0.0f ? "+" : Glyph::minus()) + juce::String (std::abs (juce::jmax (-99.9f, db)), 1) + " dBFS") : Glyph::dash(),
                    inner.removeFromLeft (74), juce::Justification::centredLeft);
        inner.removeFromLeft (12);

        // status chip: icon + text + colour
        juce::String label = "NOT TUNED"; juce::Colour fg = textLow, bd = hair2, bg = juce::Colours::transparentBlack; LNF::Icon icon = LNF::Icon::Dash;
        if (hasResult)
        {
            if (healthText == "Healthy")       { label = "HEALTHY"; fg = okText; bd = ok.withAlpha (0.5f); bg = ok.withAlpha (0.08f); icon = LNF::Icon::Check; }
            else if (healthText == "Low")      { label = "INPUT LOW"; fg = warn; bd = warn.withAlpha (0.5f); bg = warn.withAlpha (0.08f); icon = LNF::Icon::Up; }
            else if (healthText == "Hot" || healthText == "Clipping") { label = healthText == "Clipping" ? "CLIPPING" : "INPUT HIGH"; fg = healthText == "Clipping" ? critText : warn; bd = fg.withAlpha (0.5f); bg = fg.withAlpha (0.08f); icon = LNF::Icon::Down; }
            else if (healthText == "No signal") { label = "NO SIGNAL"; icon = LNF::Icon::Dash; }
            else                               { label = "NEEDS REVIEW"; fg = accentText; bd = accent.withAlpha (0.55f); bg = accent.withAlpha (0.1f); icon = LNF::Icon::Bang; }
        }
        const float cw = LNF::chipWidth (label, 10.0f, true);
        LNF::drawChip (g, inner.removeFromLeft (int (cw)).toFloat().withSizeKeepingCentre (cw, 18.0f), label, fg, bd, bg, 10.0f, &icon);
        inner.removeFromLeft (12);
        if (isSelf)
        {
            g.setColour (textLow);
            g.setFont (LNF::mono (9.5f, 500));
            g.drawText ("this channel", inner.removeFromRight (80), juce::Justification::centredRight);
        }
        g.setColour (textMid);
        g.setFont (LNF::body (11.0f, 400));
        g.drawText (recText, inner, juce::Justification::centredLeft, true);
    }
    void resized() override { meter->setBounds (14 + 100, (getHeight() - 8) / 2, 130, 8); }

private:
    juce::String name, healthText, recText;
    bool isSelf, hasCapture = false, hasResult = false;
    uint64_t id;
    float lastPeakDb = -120.0f, livePeak = -120.0f;
    std::unique_ptr<MeterComponent> meter;
};

// ---------------------------------------------------------------------------
KitPanel::KitPanel()
{
    for (auto* b : { &analyzeButton, &cancelButton, &applySafeButton, &applyBalanceButton }) addAndMakeVisible (*b);
    analyzeButton.setPaddingX (18);
    analyzeButton.setSpacing (0.2f);
    analyzeButton.onClick = [this] { if (onAnalyzeKit) onAnalyzeKit(); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    applySafeButton.setFontPx (11.5f);
    applySafeButton.onClick = [this] { if (onApplyAllSafe) onApplyAllSafe(); safeApplied = true; setResult (result, state, liveSafe); };
    applyBalanceButton.onClick = [this] { if (onApplyBalance) onApplyBalance(); balanceApplied = true; setResult (result, state, liveSafe); };
    analyzeButton.setTooltip ("Starts Tune on every Dine instance in this group at once. Have the drummer play the whole kit.");
    applySafeButton.setTooltip ("Applies every safe recommendation on every channel. Preamp recommendations must be set at the console.");
    applyBalanceButton.setTooltip ("Applies the suggested output-trim moves to the listed channels.");
    cancelButton.setVisible (false);
    applySafeButton.setVisible (false);
    applyBalanceButton.setVisible (false);

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&rowHost, false);
    viewport.setScrollBarsShown (true, false);
}

KitPanel::~KitPanel() = default;

void KitPanel::setMembers (const std::vector<InstanceInfo>& m, uint64_t self)
{
    members = m; selfId = self;
    rebuildRows();
    setResult (result, state, liveSafe);
}

void KitPanel::rebuildRows()
{
    rows.clear();
    for (const auto& info : members)
    {
        auto r = std::make_unique<Row> (info, info.id == selfId);
        rowHost.addAndMakeVisible (*r);
        rows.push_back (std::move (r));
    }
    resized();
}

void KitPanel::setSelfLevel (float peak, float hold, bool clipped)
{
    for (auto& r : rows) r->setSelfLevel (peak, hold, clipped);
}

void KitPanel::setResult (const KitRecommendationResult& r, KitController::State s, bool ls)
{
    if (r.valid && (! result.valid || r.membersAnalyzed != result.membersAnalyzed || r.processing.size() != result.processing.size()))
    { safeApplied = false; balanceApplied = false; }
    result = r; state = s; liveSafe = ls;
    const bool analyzing = s == KitController::State::Analyzing;
    analyzeButton.setEnabled (! ls && ! analyzing);
    analyzeButton.setButtonText (analyzing ? "TUNING KIT" + Glyph::ellip() : "TUNE KIT");
    cancelButton.setVisible (analyzing);
    applySafeButton.setVisible (r.valid);
    applyBalanceButton.setVisible (r.valid && ! r.balance.empty());
    applySafeButton.setEnabled (! ls && r.valid && ! safeApplied);
    applyBalanceButton.setEnabled (! ls && r.valid && ! r.balance.empty() && ! balanceApplied);

    // One registry snapshot per refresh: endpoints of instances that have since been
    // removed are simply absent, so no stale pointer is ever dereferenced.
    const std::vector<InstanceInfo> live = r.valid ? InstanceRegistry::get().snapshot() : std::vector<InstanceInfo>{};
    const auto endpointFor = [&live] (uint64_t id) -> IKitEndpoint*
    {
        for (const auto& m : live) if (m.id == id) return m.endpoint;
        return nullptr;
    };

    int safeCount = 0, channels = 0;
    for (auto& row : rows)
    {
        if (r.valid) row->refreshCapture (endpointFor (row->getId()));
        juce::String health, rec;
        for (const auto& c : r.capture)
            if (c.instanceId == row->getId())
            {
                health = c.health;
                if (c.captureGainDb != 0.0f) rec = "Preamp " + LNF::formatValue (c.captureGainDb, "dB", -24.0f, 24.0f) + " (console)";
            }
        int notes = 0;
        for (const auto& p : r.processing)
            if (p.instanceId == row->getId())
            {
                if (rec.isNotEmpty()) rec += " " + Glyph::dot() + " ";
                rec += juce::String (p.what);
                ++notes;
            }
        if (notes > 0) { safeCount += notes; ++channels; }
        row->setStatus (health, rec, r.valid);
    }
    for (const auto& b : r.balance) if (b.outputTrimDeltaDb != 0.0f) ++safeCount;
    applySafeButton.setButtonText (safeApplied ? "TUNE RE-APPLIED" : "RE-APPLY TUNE ON ALL");
    applyBalanceButton.setButtonText (balanceApplied ? "BALANCE APPLIED" : "APPLY BALANCE");
    footerText = r.valid ? juce::String (safeCount) + (safeCount == 1 ? " item" : " items") + " across " + juce::String (channels) + (channels == 1 ? " channel " : " channels ")
                           + Glyph::dot() + " preamp recommendations must be set at the console" : juce::String();
    resized();
    repaint();
}

void KitPanel::paint (juce::Graphics& g)
{
    using namespace Tokens;
    g.setColour (window);
    g.fillRect (getLocalBounds());
    auto b = getLocalBounds().reduced (20, 16);
    auto head = b.removeFromTop (34);
    g.setColour (textHi);
    g.setFont (LNF::body (15.0f, 600));
    g.drawText ("Drum Kit", head.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (textLow);
    g.setFont (LNF::mono (10.5f, 400));
    juce::String meta = juce::String (int (members.size())) + (members.size() == 1 ? " channel " : " channels ") + Glyph::dot() + " ";
    meta += result.valid ? juce::String ("tuned ") + Glyph::dot() + " " + juce::String (result.membersAnalyzed) + " captured" : juce::String ("not tuned this session");
    g.drawText (meta, head.withTrimmedTop (2), juce::Justification::centredLeft);

    if (status.isNotEmpty())
    {
        g.setColour (liveSafe ? ok : textMid);
        g.setFont (LNF::body (11.0f, 400));
        g.drawText (status, b.getX(), head.getBottom() + 4, b.getWidth() - analyzeButton.getWidth() - 20, 16, juce::Justification::centredLeft, true);
    }

    if (result.valid)
    {
        auto foot = getLocalBounds().reduced (20, 16).removeFromBottom (40);
        g.setColour (hair);
        g.fillRect (foot.removeFromTop (1));
        g.setColour (textMid);
        g.setFont (LNF::body (11.0f, 400));
        g.drawText (footerText, foot.withTrimmedRight (applySafeButton.getWidth() + applyBalanceButton.getWidth() + 30), juce::Justification::centredLeft, true);
        if (! result.notes.empty())
        {
            g.setColour (textLow);
            g.setFont (LNF::body (10.5f, 400));
        }
    }
}

void KitPanel::resized()
{
    auto b = getLocalBounds().reduced (20, 16);
    auto head = b.removeFromTop (34);
    const int aw = analyzeButton.getIdealWidth();
    analyzeButton.setBounds (head.removeFromRight (aw).withSizeKeepingCentre (aw, 34));
    if (cancelButton.isVisible())
    {
        head.removeFromRight (8);
        cancelButton.setBounds (head.removeFromRight (cancelButton.getIdealWidth()).withSizeKeepingCentre (cancelButton.getIdealWidth(), 28));
    }
    b.removeFromTop (24);
    if (result.valid)
    {
        auto foot = b.removeFromBottom (40).withTrimmedTop (1);
        const int sw = applySafeButton.getIdealWidth();
        applySafeButton.setBounds (foot.removeFromRight (sw).withSizeKeepingCentre (sw, 30));
        if (applyBalanceButton.isVisible())
        {
            foot.removeFromRight (8);
            const int bw = applyBalanceButton.getIdealWidth();
            applyBalanceButton.setBounds (foot.removeFromRight (bw).withSizeKeepingCentre (bw, 30));
        }
        b.removeFromBottom (10);
    }
    viewport.setBounds (b);
    const int rowW = b.getWidth() - (rows.size() * (Row::kHeight + 5) > size_t (b.getHeight()) ? 10 : 0);
    rowHost.setSize (rowW, int (rows.size()) * (Row::kHeight + 5));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, rowW, Row::kHeight); y += Row::kHeight + 5; }
}

} // namespace livemix
