#include "AnalyzeOverlay.h"
#include "MeterComponent.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

namespace
{
    juce::String sectionLabel (TuneSection s, const ProductDefinition* p) { return juce::String (tuneSectionNameFor (s, p == nullptr || p->product == Product::Drums)).toUpperCase(); }

    juce::String chipLabel (const Recommendation& r, bool previewActive)
    {
        if (! r.changes.empty()) return previewActive ? "APPLIED" : "SET";
        if (r.kind == Recommendation::Kind::CaptureGain) return "CONSOLE";
        return "MEASURED";
    }
    juce::String confidenceLabel (Confidence c)
    {
        switch (c)
        {
            case Confidence::High:   return "HIGH CONFIDENCE";
            case Confidence::Medium: return "MODERATE";
            case Confidence::Low:
            default:                 return "LOW CONFIDENCE";
        }
    }
    juce::String valuesLine (const Recommendation& r) { return formatRecommendationValues (r); }
}

// Modal card with blueprint corner marks.
class AnalyzeOverlay::Card : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> onPaintContent;
    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat().reduced (10.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 28, { 0, 10 }).drawForRectangle (g, b.toNearestInt());
        LNF::drawSurface (g, b, window, hairStrong, Tokens::Radius::card);
        if (onPaintContent) onPaintContent (g);
    }
    juce::Rectangle<int> inner() const { return getLocalBounds().reduced (10); }
};

// One decision: section, status chip, confidence, what, why, values, optional AI insight.
class AnalyzeOverlay::ResultRow : public juce::Component
{
public:
    const ProductDefinition* product = nullptr;
    ResultRow (const Recommendation& r, bool previewActive, bool showAi, const juce::String& ai, const ProductDefinition* prod)
        : product (prod), rec (r), chip (chipLabel (r, previewActive)), aiText (showAi ? ai : juce::String())
    {
        values = valuesLine (r);
    }

    int preferredHeight (int width) const
    {
        const int textW = width - 26 - 12;
        juce::AttributedString body; body.setFont (LNF::body (11.5f, 400)); body.append (juce::String (rec.why));
        juce::TextLayout tl; tl.createLayout (body, float (textW));
        int h = 10 + 14 + 4 + 17 + 2 + int (std::ceil (tl.getHeight())) + 10;
        if (values.isNotEmpty()) h += 5 + 14;
        if (aiText.isNotEmpty())
        {
            juce::AttributedString a; a.setFont (LNF::body (11.0f, 400)); a.append (aiText);
            juce::TextLayout at; at.createLayout (a, float (textW - 70));
            h += 7 + 1 + 6 + int (std::ceil (at.getHeight()));
        }
        return h;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        const bool applied = ! rec.changes.empty();
        g.setColour (applied ? ok.withAlpha (0.05f) : rowBg);
        g.fillRoundedRectangle (b, Tokens::Radius::control);
        g.setColour (applied ? ok.withAlpha (0.35f) : hairRow);
        g.drawRoundedRectangle (b.reduced (0.5f), Tokens::Radius::control, 1.0f);
        auto inner = getLocalBounds().reduced (13, 10);

        auto head = inner.removeFromTop (14);
        const bool console = rec.kind == Recommendation::Kind::CaptureGain;
        g.setColour (console ? warn : (applied ? accentStroke : textLow));
        g.setFont (LNF::body (11.0f, 600));
        const juce::String cat = sectionLabel (rec.section, product);
        const int cw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), cat)) + 4;
        g.drawText (cat, head.removeFromLeft (cw), juce::Justification::centredLeft);
        head.removeFromLeft (8);
        const float chipW = LNF::chipWidth (chip, 9.5f, false);
        LNF::drawChip (g, head.removeFromLeft (int (chipW)).toFloat().withSizeKeepingCentre (chipW, 14.0f), chip,
                       applied ? okText : (console ? warn : textLow), applied ? ok.withAlpha (0.5f) : (console ? warn.withAlpha (0.5f) : hair2), juce::Colours::transparentBlack, 9.5f);
        head.removeFromLeft (6);
        const juce::String conf = confidenceLabel (rec.confidence);
        const float confW = LNF::chipWidth (conf, 9.0f, false);
        LNF::drawChip (g, head.removeFromLeft (int (confW)).toFloat().withSizeKeepingCentre (confW, 14.0f), conf, textLow, hair2, juce::Colours::transparentBlack, 9.0f);
        inner.removeFromTop (4);
        g.setColour (textHi);
        g.setFont (LNF::body (13.0f, 500));
        g.drawText (juce::String (rec.what), inner.removeFromTop (17), juce::Justification::centredLeft, true);
        inner.removeFromTop (2);
        juce::AttributedString body; body.setFont (LNF::body (11.5f, 400)); body.setColour (textMid); body.append (juce::String (rec.why));
        juce::TextLayout tl; tl.createLayout (body, float (inner.getWidth()));
        tl.draw (g, inner.removeFromTop (int (std::ceil (tl.getHeight()))).toFloat());
        if (values.isNotEmpty())
        {
            inner.removeFromTop (5);
            g.setColour (accentText);
            g.setFont (LNF::mono (11.5f, 500));
            g.drawText (values, inner.removeFromTop (14), juce::Justification::centredLeft, true);
        }
        if (aiText.isNotEmpty())
        {
            inner.removeFromTop (7);
            g.setColour (hair2);
            const float dashes[] = { 3.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (float (inner.getX()), float (inner.getY()), float (inner.getRight()), float (inner.getY())), dashes, 2, 1.0f);
            inner.removeFromTop (7);
            auto tag = inner.removeFromLeft (62);
            const float tw = LNF::chipWidth ("AI INSIGHT", 9.0f, false);
            LNF::drawChip (g, tag.toFloat().withPosition (float (tag.getX()), float (inner.getY()) + 1.0f).withSize (tw, 13.0f), "AI INSIGHT", accentText, accentDim, juce::Colours::transparentBlack, 9.0f);
            inner.removeFromLeft (8);
            juce::AttributedString a; a.setFont (LNF::body (11.0f, 400).italicised()); a.setColour (textMid); a.append (aiText);
            juce::TextLayout at; at.createLayout (a, float (inner.getWidth()));
            at.draw (g, inner.toFloat());
        }
    }

private:
    Recommendation rec;
    juce::String chip, aiText, values;
};

// ---------------------------------------------------------------------------
AnalyzeOverlay::AnalyzeOverlay()
{
    setupCard = std::make_unique<Card>();
    listenCard = std::make_unique<Card>();
    resultsCard = std::make_unique<Card>();
    for (auto* c : { setupCard.get(), listenCard.get(), resultsCard.get() })
    {
        addChildComponent (*c);
        c->onPaintContent = [this, c] (juce::Graphics& g) { paintCard (g, *c); };
    }
    setupCard->addAndMakeVisible (cancelSetup);
    listenCard->addAndMakeVisible (cancelListen);
    cancelListen.setFontPx (10.5f);
    for (auto* c : std::initializer_list<juce::Component*> { &beforeButton, &afterButton, &keepButton, &reviewButton, &revertButton, &resultsViewport })
        resultsCard->addAndMakeVisible (*c);
    resultsViewport.setViewedComponent (&resultsHost, false);
    resultsViewport.setScrollBarsShown (true, false);
    for (auto* b : { &beforeButton, &afterButton }) { b->setFontPx (11.0f); b->setPaddingX (14); b->setClickingTogglesState (false); }
    keepButton.setFontPx (12.0f);
    reviewButton.setFontPx (12.0f);
    revertButton.setFontPx (12.0f);

    cancelSetup.onClick = cancelListen.onClick = [this] { if (onCancel) onCancel(); };
    beforeButton.onClick = [this] { setCompareState (false); if (onCompare) onCompare (false); };
    afterButton.onClick = [this] { setCompareState (true); if (onCompare) onCompare (true); };
    keepButton.onClick = [this] { if (onKeep) onKeep(); };
    reviewButton.onClick = [this] { if (onReview) onReview(); };
    revertButton.onClick = [this] { if (onRevert) onRevert(); };
    beforeButton.setTooltip ("Listen to the settings from before Tune. Nothing is discarded until you choose.");
    afterButton.setTooltip ("Listen to the tuned settings.");
    keepButton.setTooltip ("Keep the tuned settings. The previous settings remain available in A/B only if you change nothing else.");
    reviewButton.setTooltip ("Keep the tuned settings and open Advanced mode with every decision listed, each with UNDO.");
    revertButton.setTooltip ("Restore the settings from before Tune.");
    setInterceptsMouseClicks (true, true);
}

AnalyzeOverlay::~AnalyzeOverlay() = default;

void AnalyzeOverlay::setPhase (Phase p)
{
    if (phase == p) return;
    phase = p;
    setVisible (p != Phase::Hidden);
    setupCard->setVisible (p == Phase::Setup);
    listenCard->setVisible (p == Phase::Listening || p == Phase::Processing);
    resultsCard->setVisible (p == Phase::Results);
    if (p == Phase::Setup || p == Phase::Listening || p == Phase::Processing) startTimerHz (30); else stopTimer();
    resized();
    repaint();
}

void AnalyzeOverlay::timerCallback()
{
    pulse += 1.0f / 30.0f;
    if (setupCard->isVisible()) setupCard->repaint();
    if (listenCard->isVisible()) listenCard->repaint();
}

void AnalyzeOverlay::setSourceName (const juce::String& s) { sourceName = s.toUpperCase(); for (auto* c : { setupCard.get(), listenCard.get(), resultsCard.get() }) c->repaint(); }
void AnalyzeOverlay::setAIOn (bool on) { aiOn = on; for (auto* c : { setupCard.get(), listenCard.get(), resultsCard.get() }) c->repaint(); }

void AnalyzeOverlay::setListening (float secondsRemaining, float prog, const LevelHistory& h, float db)
{
    remaining = secondsRemaining; progress = prog; history = &h; inputDb = db;
    if (listenCard->isVisible()) listenCard->repaint();
}

void AnalyzeOverlay::setCompareState (bool after)
{
    showingAfter = after;
    beforeButton.setToggleState (! after, juce::dontSendNotification);
    afterButton.setToggleState (after, juce::dontSendNotification);
    resultsCard->repaint();
}

void AnalyzeOverlay::setTuneResult (const TuneResult& r, const juce::String& ai, const juce::String& profileName,
                                    bool preview, bool after, float capturedSeconds)
{
    result = r; aiText = ai; previewActive = preview;
    resultMeta = juce::String (capturedSeconds, 1) + " s captured " + Glyph::dot() + " " + profileName.toUpperCase() + " " + Glyph::dot() + " "
               + (r.parametersChanged == 0 ? juce::String ("no parameters changed") : juce::String (r.parametersChanged) + (r.parametersChanged == 1 ? " parameter changed" : " parameters changed"))
               + (aiOn ? " " + Glyph::dot() + " AI interpretation" : juce::String());

    visibleSections.clear();
    for (int s = 0; s < int (TuneSection::Count); ++s)
    {
        const auto& sum = r.sections[size_t (s)];
        if (TuneSection (s) == TuneSection::Notes && sum.itemCount == 0) continue;
        if (TuneSection (s) == TuneSection::Mix && sum.itemCount == 0) continue;
        visibleSections.push_back (s);
    }

    rows.clear();
    std::vector<int> order;
    for (int i = 0; i < int (r.report.items.size()); ++i) order.push_back (i);
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return int (r.report.items[size_t (a)].section) < int (r.report.items[size_t (b)].section); });
    bool aiShown = false;
    for (int i : order)
    {
        const auto& item = r.report.items[size_t (i)];
        const bool showAi = ! aiShown && ai.isNotEmpty() && ! item.changes.empty();
        auto row = std::make_unique<ResultRow> (item, previewActive, showAi, ai, product);
        if (showAi) aiShown = true;
        resultsHost.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    if (! aiShown && ai.isNotEmpty())
    {
        Recommendation info;
        info.kind = Recommendation::Kind::Info;
        info.section = TuneSection::Notes;
        info.what = "AI interpretation";
        info.why = ai.toStdString();
        info.confidence = Confidence::Low;
        auto row = std::make_unique<ResultRow> (info, false, false, juce::String(), product);
        resultsHost.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    bool hasChanges = false;
    for (const auto& item : r.report.items) if (! item.changes.empty()) { hasChanges = true; break; }
    beforeButton.setVisible (previewActive);
    afterButton.setVisible (previewActive);
    revertButton.setVisible (previewActive);
    reviewButton.setVisible (hasChanges);
    keepButton.setButtonText (previewActive ? "KEEP" : "OK");
    setCompareState (previewActive ? after : true);
    resized();
    repaint();
}

void AnalyzeOverlay::paint (juce::Graphics& g)
{
    g.fillAll (Tokens::ground.withAlpha (0.8f));
}

int AnalyzeOverlay::sectionsHeight() const
{
    return int (visibleSections.size()) * 20 + 14;
}

void AnalyzeOverlay::paintCard (juce::Graphics& g, Card& card)
{
    using namespace Tokens;

    auto drawAiChip = [&] (juce::Rectangle<int> area, const juce::String& text)
    {
        const float w = LNF::chipWidth (text, 9.5f, false);
        LNF::drawChip (g, area.toFloat().removeFromRight (w).withSizeKeepingCentre (w, 18.0f), text, accentText, accentDim, juce::Colours::transparentBlack, 9.5f);
    };

    if (&card == setupCard.get())
    {
        auto a = card.inner().reduced (28, 26);
        g.setColour (textHi);
        g.setFont (LNF::body (16.0f, 600));
        g.drawText ("Tune " + Glyph::dash() + " " + sourceName, a.removeFromTop (22), juce::Justification::centredLeft);
        a.removeFromTop (8);
        g.setColour (textMid);
        g.setFont (LNF::body (12.5f, 400));
        const juce::String noun = product != nullptr ? juce::String (product->sourceNoun) : juce::String ("the source");
        const juce::String prompt = product != nullptr ? juce::String (product->playerPrompt) : juce::String ("play normally");
        g.drawFittedText ("Play normally. Dine starts listening when it hears " + noun + ", then listens for about 12 seconds " + Glyph::dash() + " " + prompt + ", the way it will be in the service.", a.removeFromTop (38), juce::Justification::topLeft, 2, 1.0f);
        a.removeFromTop (10);
        auto wait = a.removeFromTop (14);
        const float alpha = 0.3f + 0.7f * (0.5f + 0.5f * std::cos (pulse * juce::MathConstants<float>::twoPi / 1.1f));
        g.setColour (warn.withAlpha (alpha));
        g.fillEllipse (wait.removeFromLeft (8).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
        wait.removeFromLeft (9);
        g.setColour (warn);
        g.setFont (LNF::mono (12.0f, 500));
        g.drawText ("Waiting for signal" + Glyph::ellip(), wait, juce::Justification::centredLeft);
        a.removeFromTop (20);
        if (aiOn)
        {
            const float w = LNF::chipWidth ("AI ASSISTANCE ON", 9.5f, false);
            LNF::drawChip (g, a.toFloat().removeFromTop (30.0f).removeFromLeft (w).withSizeKeepingCentre (w, 18.0f), "AI ASSISTANCE ON", accentText, accentDim, juce::Colours::transparentBlack, 9.5f);
        }
    }
    if (&card == listenCard.get())
    {
        auto a = card.inner().reduced (26, 24);
        auto head = a.removeFromTop (22);
        g.setColour (textHi);
        g.setFont (LNF::body (16.0f, 600));
        g.drawText ((phase == Phase::Processing ? "Tuning " : "Listening " + Glyph::dot() + " ") + sourceName, head, juce::Justification::centredLeft);
        if (aiOn) drawAiChip (head, "AI ASSISTANCE ON");
        a.removeFromTop (14);
        auto wave = a.removeFromTop (110);
        g.setColour (inset);
        g.fillRoundedRectangle (wave.toFloat(), Tokens::Radius::control);
        if (history != nullptr)
        {
            const float mid = float (wave.getCentreY());
            const int n = juce::jmin (history->size(), wave.getWidth() / 2);
            g.setColour (accentStroke);
            for (int i = 0; i < n; ++i)
            {
                const float amp = juce::jmax (0.02f, MeterComponent::normFor (history->fromNewest (i).inDb));
                const float x = float (wave.getRight()) - float (i + 1) * 2.0f;
                g.fillRect (x, mid - amp * 50.0f, 1.4f, amp * 100.0f);
            }
        }
        g.setColour (textGrid);
        g.setFont (LNF::mono (9.0f, 400));
        g.drawText ("input " + Glyph::dot() + " " + (inputDb <= -99.0f ? Glyph::minus() + "inf" : (inputDb < 0.0f ? Glyph::minus() : juce::String ("+")) + juce::String (std::abs (inputDb), 1)) + " dBFS", wave.getX() + 8, wave.getY() + 4, 160, 12, juce::Justification::centredLeft);
        g.setColour (hair);
        g.drawRoundedRectangle (wave.toFloat().reduced (0.5f), Tokens::Radius::control, 1.0f);
        a.removeFromTop (12);
        auto row = a.removeFromTop (28);
        row.removeFromRight (cancelListen.getWidth() + 12);
        g.setColour (textHi);
        g.setFont (LNF::mono (13.0f, 500));
        const juce::String rem = phase == Phase::Processing ? "0.0" : juce::String (remaining, 1);
        const int rw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), rem)) + 2;
        g.drawText (rem, row.removeFromLeft (rw), juce::Justification::centredLeft);
        row.removeFromLeft (12);
        g.setColour (textLow);
        g.setFont (LNF::body (11.0f, 400));
        const juce::String lab = phase == Phase::Processing ? "tuning" + Glyph::ellip() : "seconds remaining";
        const int lw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), lab)) + 2;
        g.drawText (lab, row.removeFromLeft (lw), juce::Justification::centredLeft);
        row.removeFromLeft (12);
        auto bar = row.toFloat().withSizeKeepingCentre (float (row.getWidth()), 4.0f);
        g.setColour (track);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (accentStroke);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, phase == Phase::Processing ? 1.0f : progress)), 2.0f);
        a.removeFromTop (10);
        g.setColour (textLow);
        g.setFont (LNF::mono (10.5f, 400));
        g.drawText ("measuring level " + Glyph::dot() + " spectrum " + Glyph::dot() + " fundamental " + Glyph::dot() + " transients " + Glyph::dot() + " bleed", a.removeFromTop (14), juce::Justification::centredLeft);
    }
    if (&card == resultsCard.get())
    {
        auto a = card.inner();
        auto head = a.removeFromTop (52).reduced (22, 0).withTrimmedTop (18);
        g.setColour (textHi);
        g.setFont (LNF::body (15.0f, 600));
        const juce::String t = juce::String (result.headline).isEmpty() ? "Tune " + Glyph::dash() + " " + sourceName : juce::String (result.headline);
        const int tw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t)) + 4;
        g.drawText (t, head.removeFromLeft (tw), juce::Justification::centredLeft);
        if (aiOn) drawAiChip (head, "AI ASSIST");
        head.removeFromLeft (10);
        g.setColour (textLow);
        g.setFont (LNF::mono (10.5f, 400));
        g.drawText (resultMeta, head, juce::Justification::bottomLeft, true);
        g.setColour (hair);
        g.fillRect (a.getX(), a.getY(), a.getWidth(), 1);

        // Section summary: one line per section.
        auto sec = a.removeFromTop (sectionsHeight()).reduced (22, 7);
        for (int s : visibleSections)
        {
            const auto& sum = result.sections[size_t (s)];
            auto line = sec.removeFromTop (20);
            g.setColour (sum.changed ? accentText : (TuneSection (s) == TuneSection::Input && sum.summary.find ("Healthy") == std::string::npos && sum.itemCount > 0 ? warn : textLow));
            g.setFont (LNF::body (11.0f, 600));
            g.drawText (sectionLabel (TuneSection (s), product), line.removeFromLeft (84), juce::Justification::centredLeft);
            g.setColour (sum.changed ? textHi : textMid);
            g.setFont (LNF::body (12.0f, sum.changed ? 500 : 400));
            g.drawText (juce::String (sum.summary), line, juce::Justification::centredLeft, true);
        }
        g.setColour (hair);
        g.fillRect (a.getX(), a.getY(), a.getWidth(), 1);
        auto foot = a.removeFromBottom (54);
        g.fillRect (foot.getX(), foot.getY(), foot.getWidth(), 1);
        if (previewActive)
        {
            g.setColour (textLow);
            g.setFont (LNF::mono (9.5f, 400));
            g.drawText (showingAfter ? "listening to AFTER" : "listening to BEFORE", foot.reduced (22, 0).withTrimmedLeft (beforeButton.getWidth() + afterButton.getWidth() + 10).removeFromLeft (150), juce::Justification::centredLeft);
        }
    }
}

void AnalyzeOverlay::resized()
{
    auto b = getLocalBounds();
    // Waiting card 440 wide
    {
        const int w = 440 + 20, h = 26 + 22 + 8 + 38 + 10 + 14 + 20 + 30 + 26 + 20;
        setupCard->setBounds (b.withSizeKeepingCentre (w, h));
        auto in = setupCard->inner().reduced (28, 26);
        cancelSetup.setBounds (in.removeFromBottom (30).removeFromRight (cancelSetup.getIdealWidth()).withSizeKeepingCentre (cancelSetup.getIdealWidth(), 28));
    }
    // Listening card 480 wide
    {
        const int w = 480 + 20, h = 24 + 22 + 14 + 110 + 12 + 28 + 10 + 14 + 24 + 20;
        listenCard->setBounds (b.withSizeKeepingCentre (w, h));
        auto in = listenCard->inner().reduced (26, 24);
        in.removeFromTop (22 + 14 + 110 + 12);
        cancelListen.setBounds (in.removeFromTop (28).removeFromRight (cancelListen.getIdealWidth()).withSizeKeepingCentre (cancelListen.getIdealWidth(), 26));
    }
    layoutResults();
}

void AnalyzeOverlay::layoutResults()
{
    auto b = getLocalBounds();
    const int w = juce::jmin (640, b.getWidth() - 40) + 20;
    const int contentW = w - 20 - 44 - 10;
    int listH = 12;
    for (auto& r : rows) listH += r->preferredHeight (contentW) + 9;
    listH += 3;
    const int fixed = 52 + sectionsHeight() + 1 + 54;
    const int maxH = juce::jmin (600, b.getHeight() - 40);
    const int h = juce::jmin (maxH, fixed + juce::jmin (listH, 260)) + 20;
    resultsCard->setBounds (b.withSizeKeepingCentre (w, h));
    auto in = resultsCard->inner();
    in.removeFromTop (52 + sectionsHeight() + 1);
    auto foot = in.removeFromBottom (54).reduced (22, 12);
    if (previewActive)
    {
        beforeButton.setBounds (foot.removeFromLeft (beforeButton.getIdealWidth()));
        afterButton.setBounds (foot.removeFromLeft (afterButton.getIdealWidth()));
        foot.removeFromLeft (170);
    }
    revertButton.setBounds (foot.removeFromRight (revertButton.getIdealWidth()));
    foot.removeFromRight (10);
    if (reviewButton.isVisible())
    {
        reviewButton.setBounds (foot.removeFromRight (reviewButton.getIdealWidth()));
        foot.removeFromRight (10);
    }
    keepButton.setBounds (foot.removeFromRight (keepButton.getIdealWidth()));
    resultsViewport.setBounds (in.reduced (22, 0));
    const bool scroll = listH > resultsViewport.getHeight();
    const int rowW = resultsViewport.getWidth() - (scroll ? 10 : 0);
    int y = 12;
    for (auto& r : rows) { const int rh = r->preferredHeight (rowW); r->setBounds (0, y, rowW, rh); y += rh + 9; }
    resultsHost.setSize (rowW, y + 3);
}

} // namespace livemix
