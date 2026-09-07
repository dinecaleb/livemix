#include "AdvancedPanel.h"
#include "State/ParameterIDs.h"
#include "DSP/Compressor.h"
#include <algorithm>

namespace livemix
{

using LNF = LiveMixLookAndFeel;

namespace
{
    constexpr int kRailWidth = 172;
    constexpr int kPadX = 16, kPadY = 14, kGap = 12;

    const char* subtitleFor (ChainModule m)
    {
        switch (m)
        {
            case ChainModule::Input:     return "trim · polarity · filters";
            case ChainModule::Gate:      return "threshold · range · timing";
            case ChainModule::EQ:        return "7 bands · response curve";
            case ChainModule::Comp:      return "transfer curve · gain reduction";
            case ChainModule::Transient: return "attack · sustain";
            case ChainModule::DeEss:     return "split · threshold · range";
            case ChainModule::Width:     return "mid/side · mono below";
            case ChainModule::Limiter:   return "ceiling · release · lookahead 1.5 ms";
            case ChainModule::Output:    return "saturation · trim · meter";
            case ChainModule::Count:
            default:                     return "";
        }
    }

    std::vector<juce::String> tileIds (ChainModule m)
    {
        using namespace ParamID;
        switch (m)
        {
            case ChainModule::Input:     return { inputTrim, hpfOn, hpfFreq, hpfSlope, lpfOn, lpfFreq, lpfSlope };
            case ChainModule::Gate:      return { gateOn, gateThreshold, gateRange, gateAttack, gateHold, gateRelease, gateHysteresis, gateRatio };
            case ChainModule::EQ:        return { toneEqOn, corrEqOn };
            case ChainModule::Comp:      return { compOn, compThreshold, compRatio, compAttack, compRelease, compKnee, compMakeup, compMix };
            case ChainModule::Transient: return { transOn, transAttack, transSustain };
            case ChainModule::DeEss:     return { deEssOn, deEssFreq, deEssThreshold, deEssRange };
            case ChainModule::Width:     return { widthOn, widthAmount, widthMonoBelow };
            case ChainModule::Limiter:   return { limiterOn, limiterCeiling, limiterRelease };
            case ChainModule::Output:    return { satOn, satDrive, satMix, outputTrim };
            case ChainModule::Count:
            default: return {};
        }
    }

    juce::String tileLabel (const ParameterSpec& spec)
    {
        juce::String n (spec.name);
        for (auto* prefix : { "Gate ", "Comp ", "Transient ", "Saturation ", "Input ", "Output ", "De-esser ", "Width ", "Limiter " })
            if (n.startsWith (prefix) && n.length() > int (juce::String (prefix).length()))
                n = n.fromFirstOccurrenceOf (prefix, false, false);
        if (n == "HPF Frequency") n = "HPF FREQ";
        if (n == "LPF Frequency") n = "LPF FREQ";
        if (n == "Corrective EQ") n = "CORRECTIVE";
        if (n == "Tone EQ") n = "TONE EQ";
        return n.toUpperCase();
    }

    juce::String describeChanges (const Recommendation& r) { return formatRecommendationValues (r); }
}

// ---------------------------------------------------------------------------
class AdvancedPanel::RailButton : public juce::Button
{
public:
    explicit RailButton (ChainModule m) : juce::Button (chainModuleName (m)), module (m) {}
    ChainModule module;
    void set (const juce::String& s, bool en) { if (summary != s || enabled != en) { summary = s; enabled = en; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        const bool on = getToggleState();
        LNF::drawSurface (g, b, on ? accent.withAlpha (0.12f) : juce::Colours::transparentBlack,
                          on ? accent : over ? hairHover : hairRow, Tokens::Radius::control);
        auto inner = b.reduced (10.0f, 8.0f);
        auto row = inner.removeFromTop (13.0f);
        g.setColour (enabled ? ok : mark);
        g.fillEllipse (row.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
        row.removeFromLeft (6.0f);
        g.setColour (on ? textHi : textMid);
        g.setFont (LNF::body (11.0f, 600));
        g.drawText (getButtonText(), row, juce::Justification::centredLeft);
        inner.removeFromTop (2.0f);
        g.setColour (textLow);
        g.setFont (LNF::mono (9.5f, 400));
        g.drawText (summary, inner.removeFromTop (12.0f), juce::Justification::centredLeft, true);
    }
private:
    juce::String summary;
    bool enabled = true;
};

// EQ band tile: drag vertically for gain, horizontally for frequency, wheel for Q, click the corner square to enable.
class AdvancedPanel::BandTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    BandTile (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix, int index, const juce::String& label)
        : name (label),
          onParam (*apvts.getParameter (eqBandId (prefix.toRawUTF8(), index, "On"))),
          freqParam (*apvts.getParameter (eqBandId (prefix.toRawUTF8(), index, "Freq"))),
          gainParam (*apvts.getParameter (eqBandId (prefix.toRawUTF8(), index, "Gain"))),
          qParam (*apvts.getParameter (eqBandId (prefix.toRawUTF8(), index, "Q"))),
          onAtt (onParam, [this] (float v) { on = v >= 0.5f; repaint(); }),
          freqAtt (freqParam, [this] (float v) { freq = v; repaint(); }),
          gainAtt (gainParam, [this] (float v) { gain = v; repaint(); }),
          qAtt (qParam, [this] (float v) { q = v; repaint(); })
    {
        for (auto* a : { &onAtt, &freqAtt, &gainAtt, &qAtt }) a->sendInitialUpdate();
        setTooltip ("Drag up/down: gain. Drag left/right: frequency. Wheel: Q. Click the square to switch the band on or off. Double-click resets gain.");
    }
    void setFlagged (bool f) { if (flagged != f) { flagged = f; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        LNF::drawSurface (g, b, raised, dragging ? hairHover : hair2, Tokens::Radius::control);
        auto inner = b.reduced (10.0f, 8.0f);
        auto head = inner.removeFromTop (12.0f);
        auto sw = head.removeFromRight (10.0f).withSizeKeepingCentre (9.0f, 9.0f);
        g.setColour (on ? accentText : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (sw.reduced (1.5f), 2.0f);
        g.setColour (on ? accent : hairHover);
        g.drawRoundedRectangle (sw.reduced (0.5f), 2.5f, 1.0f);
        g.setColour (flagged ? warn : textMid);
        g.setFont (LNF::body (10.0f, 600));
        g.drawText (name, head, juce::Justification::centredLeft);
        inner.removeFromTop (3.0f);
        g.setColour (on ? textHi : textLow);
        g.setFont (LNF::mono (13.0f, 500));
        g.drawText (LNF::formatValue (freq, "Hz", 20.0f, 20000.0f) + " " + Glyph::dot() + " " + LNF::formatValue (gain, "dB", -18.0f, 18.0f),
                    inner.removeFromTop (16.0f), juce::Justification::centredLeft, true);
        inner.removeFromTop (1.0f);
        g.setColour (textLow);
        g.setFont (LNF::mono (9.5f, 400));
        g.drawText ("Q " + juce::String (q, 1), inner.removeFromTop (12.0f), juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x > getWidth() - 24 && e.y < 22) { onAtt.setValueAsCompleteGesture (on ? 0.0f : 1.0f); return; }
        gStart = gainParam.convertTo0to1 (gain); fStart = freqParam.convertTo0to1 (freq);
        dragging = true;
        gainAtt.beginGesture(); freqAtt.beginGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        gainAtt.setValueAsPartOfGesture (gainParam.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, gStart - float (e.getDistanceFromDragStartY()) / 200.0f)));
        freqAtt.setValueAsPartOfGesture (freqParam.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, fStart + float (e.getDistanceFromDragStartX()) / 400.0f)));
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; gainAtt.endGesture(); freqAtt.endGesture(); repaint(); } }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! (e.x > getWidth() - 24 && e.y < 22)) gainAtt.setValueAsCompleteGesture (0.0f); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        qAtt.setValueAsCompleteGesture (qParam.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, qParam.convertTo0to1 (q) + w.deltaY * 0.1f)));
    }
    float getFreq() const { return freq; }
    bool isOn() const { return on; }

private:
    juce::String name;
    juce::RangedAudioParameter &onParam, &freqParam, &gainParam, &qParam;
    juce::ParameterAttachment onAtt, freqAtt, gainAtt, qAtt;
    bool on = false, flagged = false, dragging = false;
    float freq = 1000.0f, gain = 0.0f, q = 1.0f, gStart = 0.0f, fStart = 0.0f;
};

// Scrolling input waveform coloured by gate state, threshold lines, GR ticks.
class AdvancedPanel::GateScope : public juce::Component
{
public:
    void update (const LiveState& l) { live = &l; repaint(); }
    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        g.setColour (inset);
        g.fillRoundedRectangle (b, Tokens::Radius::control);
        if (live == nullptr) return;
        const float w = b.getWidth(), h = b.getHeight(), mid = (h - 26.0f) * 0.5f;
        const auto& H = live->history;
        const int n = juce::jmin (H.size(), int (w / 2.0f));
        for (int i = 0; i < n; ++i)
        {
            const auto& s = H.fromNewest (i);
            const float x = w - float (i + 1) * 2.0f;
            const float a = MeterComponent::normFor (s.inDb);
            g.setColour (s.gateOpen || ! live->params.gateEnabled ? accentStroke : gateClosed);
            g.fillRect (x, mid - a * mid, 1.4f, juce::jmax (1.0f, a * mid * 2.0f));
            if (! s.gateOpen && live->params.gateEnabled) { g.setColour (warn.withAlpha (0.7f)); g.fillRect (x, h - 22.0f, 1.4f, 10.0f); }
        }
        const float thr = live->params.gateThresholdDb, ta = MeterComponent::normFor (thr);
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (ok);
        g.drawDashedLine (juce::Line<float> (0.0f, mid - ta * mid, w, mid - ta * mid), dashes, 2, 1.0f);
        g.drawDashedLine (juce::Line<float> (0.0f, mid + ta * mid, w, mid + ta * mid), dashes, 2, 1.0f);
        g.setFont (LNF::mono (9.0f, 600));
        g.drawText ("THRESHOLD " + LNF::formatValue (thr, "dB", -80.0f, 0.0f), 8, int (mid - ta * mid) - 14, 160, 12, juce::Justification::centredLeft);
        g.setColour (textGrid);
        g.drawText ("GR", 8, int (h) - 20, 30, 12, juce::Justification::centredLeft);
        if (! live->params.gateEnabled)
        {
            g.setColour (textLow);
            g.setFont (LNF::mono (10.0f, 500));
            g.drawText ("gate off", int (w) - 70, 8, 60, 12, juce::Justification::centredRight);
        }
        g.setColour (hair);
        g.drawRoundedRectangle (b.reduced (0.5f), Tokens::Radius::control, 1.0f);
    }
private:
    const LiveState* live = nullptr;
};

// Compressor transfer curve with the moving operating point and a GR meter.
class AdvancedPanel::CompView : public juce::Component
{
public:
    void update (const LiveState& l) { live = &l; repaint(); }
    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        auto grCol = b.removeFromRight (76.0f);
        b.removeFromRight (12.0f);
        g.setColour (inset);
        g.fillRoundedRectangle (b, Tokens::Radius::control);
        if (live == nullptr) return;
        const auto& p = live->params;
        const float w = b.getWidth(), h = b.getHeight();
        auto X = [&] (float db) { return b.getX() + w * (db + 60.0f) / 60.0f; };
        auto Y = [&] (float db) { return b.getY() + h * (1.0f - (db + 60.0f) / 60.0f); };
        g.setColour (grid);
        for (float d : { -48.0f, -36.0f, -24.0f, -12.0f })
        {
            g.drawVerticalLine (int (X (d)), b.getY(), b.getBottom());
            g.drawHorizontalLine (int (Y (d)), b.getX(), b.getRight());
        }
        const float dashes[] = { 3.0f, 4.0f };
        g.setColour (hair2);
        g.drawDashedLine (juce::Line<float> (b.getX(), b.getBottom(), b.getRight(), b.getY()), dashes, 2, 1.0f);
        g.setColour (markCard);
        const float d2[] = { 2.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (X (p.compThresholdDb), b.getY(), X (p.compThresholdDb), b.getBottom()), d2, 2, 1.0f);

        juce::Path curve;
        for (int i = 0; i <= 80; ++i)
        {
            const float x = -60.0f + float (i) * 60.0f / 80.0f;
            const float y = p.compEnabled ? Compressor::computeGain (x, p.compThresholdDb, p.compRatio, p.compKneeDb) + p.compMakeupDb : x;
            const auto pt = juce::Point<float> (X (x), Y (juce::jlimit (-60.0f, 0.0f, y)));
            if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
        }
        g.setColour (p.compEnabled ? accentStroke : textLow);
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        const float inDb = juce::jlimit (-60.0f, 0.0f, live->inPeakDb);
        const float outDb = juce::jlimit (-60.0f, 0.0f, p.compEnabled ? Compressor::computeGain (inDb, p.compThresholdDb, p.compRatio, p.compKneeDb) + p.compMakeupDb : inDb);
        g.setColour (textHi);
        g.fillEllipse (X (inDb) - 4.0f, Y (outDb) - 4.0f, 8.0f, 8.0f);

        g.setColour (textGrid);
        g.setFont (LNF::mono (9.0f, 400));
        g.drawText ("IN", int (b.getRight()) - 34, int (b.getBottom()) - 16, 28, 10, juce::Justification::centredRight);
        g.drawText ("OUT", int (b.getX()) + 6, int (b.getY()) + 6, 30, 10, juce::Justification::centredLeft);
        g.setColour (hair);
        g.drawRoundedRectangle (b.reduced (0.5f), Tokens::Radius::control, 1.0f);

        // GR column
        g.setColour (textMid);
        g.setFont (LNF::body (10.0f, 600));
        g.drawText ("GR", grCol.removeFromTop (14.0f), juce::Justification::centred);
        grCol.removeFromTop (6.0f);
        auto value = grCol.removeFromBottom (16.0f);
        grCol.removeFromBottom (6.0f);
        auto bar = grCol.withSizeKeepingCentre (30.0f, grCol.getHeight());
        g.setColour (inset);
        g.fillRoundedRectangle (bar, Tokens::Radius::chip);
        const float gr = live->compGrDb;
        g.setColour (warn);
        g.fillRect (bar.getX() + 2.0f, bar.getY(), bar.getWidth() - 4.0f, bar.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 18.0f));
        g.setColour (hair);
        for (int d : { 3, 6, 9, 12 }) g.drawHorizontalLine (int (bar.getY() + bar.getHeight() * float (d) / 18.0f), bar.getX(), bar.getRight());
        g.drawRoundedRectangle (bar.reduced (0.5f), Tokens::Radius::chip, 1.0f);
        g.setColour (warn);
        g.setFont (LNF::mono (12.0f, 500));
        g.drawText ((gr >= 0.05f ? Glyph::minus() : juce::String()) + juce::String (gr, 1) + " dB", value, juce::Justification::centred);
    }
private:
    const LiveState* live = nullptr;
};

// De-esser / limiter / width: a big readout of what the stage is doing right now.
class AdvancedPanel::ReductionView : public juce::Component
{
public:
    void set (ChainModule m, const LiveState& l) { module = m; live = &l; repaint(); }
    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        LNF::drawSurface (g, b, inset, hair, Tokens::Radius::control);
        if (live == nullptr) return;
        auto inner = getLocalBounds().reduced (24, 18);
        juce::String title, value, note;
        float bar = 0.0f; juce::Colour barColour = warn;
        if (module == ChainModule::DeEss)
        {
            title = "S REDUCTION";
            value = (live->deEssGrDb >= 0.05f ? Glyph::minus() : juce::String()) + juce::String (live->deEssGrDb, 1) + " dB";
            bar = juce::jlimit (0.0f, 1.0f, live->deEssGrDb / 12.0f);
            note = live->params.deEssEnabled ? "Only the band above the split frequency is turned down, and only while an S is loud. Everything else passes untouched."
                                             : "S control is off.";
        }
        else if (module == ChainModule::Limiter)
        {
            title = "LIMITING";
            value = (live->limiterGrDb >= 0.05f ? Glyph::minus() : juce::String()) + juce::String (live->limiterGrDb, 1) + " dB";
            bar = juce::jlimit (0.0f, 1.0f, live->limiterGrDb / 12.0f);
            note = "Peaks are stopped at the ceiling 1.5 ms before they arrive. Occasional 1-3 dB is normal; constant limiting means the mix is being pushed too hard.";
            if (live->shortTermLufs > -100.0f) note += "  Loudness right now: " + juce::String (live->shortTermLufs, 1) + " LUFS, peak " + LNF::formatValue (live->truePeakHoldDb, "dB", -60.0f, 6.0f) + ".";
        }
        else
        {
            title = "STEREO CORRELATION";
            value = juce::String (live->correlation, 2);
            bar = juce::jlimit (0.0f, 1.0f, 0.5f * (live->correlation + 1.0f));
            barColour = live->correlation < 0.2f ? warn : ok;
            note = live->correlation < 0.2f ? "Left and right disagree a lot: this image collapses on phones and mono systems. Narrow the width or centre more of the low end."
                                            : "1.0 = mono, 0 = fully independent sides. Anything above about 0.3 stays solid on mono systems.";
        }
        g.setColour (textMid);
        g.setFont (LNF::body (11.0f, 600));
        g.drawText (title, inner.removeFromTop (14), juce::Justification::centredLeft);
        inner.removeFromTop (4);
        g.setColour (textHi);
        g.setFont (LNF::mono (26.0f, 500));
        g.drawText (value, inner.removeFromTop (32), juce::Justification::centredLeft);
        inner.removeFromTop (8);
        auto track = inner.removeFromTop (6).toFloat();
        g.setColour (Tokens::track);
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (barColour);
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * bar), 3.0f);
        inner.removeFromTop (12);
        g.setColour (textLow);
        g.setFont (LNF::body (11.5f, 400));
        g.drawFittedText (note, inner, juce::Justification::topLeft, 4, 1.0f);
    }
private:
    ChainModule module = ChainModule::DeEss;
    const LiveState* live = nullptr;
};

// "! NOTCHED RING   Tune set −3.5 dB · Q 4   [UNDO] [DISMISS]"
class AdvancedPanel::SuggestionCard : public juce::Component
{
public:
    SuggestionCard()
    {
        addAndMakeVisible (apply);
        addAndMakeVisible (dismiss);
        apply.setPaddingX (12);
        dismiss.setPaddingX (12);
    }
    static constexpr int kHeight = 40;
    void set (const Recommendation& r)
    {
        rec = r;
        title = juce::String (r.what).toUpperCase();
        values = describeChanges (r);
        apply.setButtonText ("UNDO");
        resized();
        repaint();
    }
    const Recommendation& get() const { return rec; }
    FlatButton apply { "UNDO", FlatButton::Style::Accent }, dismiss { "DISMISS", FlatButton::Style::Outline };

    void paint (juce::Graphics& g) override
    {
        using namespace Tokens;
        auto b = getLocalBounds().toFloat();
        LNF::drawSurface (g, b, warn.withAlpha (0.06f), warn.withAlpha (0.45f), Tokens::Radius::control);
        auto inner = getLocalBounds().reduced (12, 0).withTrimmedRight (apply.getWidth() + dismiss.getWidth() + 20);
        LNF::drawIcon (g, LNF::Icon::Bang, inner.removeFromLeft (9).toFloat().withSizeKeepingCentre (9.0f, 9.0f), warn);
        inner.removeFromLeft (5);
        g.setColour (warn);
        g.setFont (LNF::body (11.0f, 600));
        const int tw = juce::jmin (inner.getWidth() / 2, int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 4);
        g.drawText (title, inner.removeFromLeft (tw), juce::Justification::centredLeft, true);
        inner.removeFromLeft (8);
        g.setColour (textMid);
        g.setFont (LNF::body (11.5f, 400));
        const juce::String lead = "Tune set ";
        const int lw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), lead));
        g.drawText (lead, inner.removeFromLeft (lw), juce::Justification::centredLeft);
        g.setColour (textHi);
        g.setFont (LNF::mono (11.0f, 500));
        g.drawText (values, inner, juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        auto b = getLocalBounds().reduced (12, 0);
        dismiss.setBounds (b.removeFromRight (dismiss.getIdealWidth()).withSizeKeepingCentre (dismiss.getIdealWidth(), 26));
        b.removeFromRight (8);
        apply.setBounds (b.removeFromRight (apply.getIdealWidth()).withSizeKeepingCentre (apply.getIdealWidth(), 26));
    }
private:
    Recommendation rec;
    juce::String title, values;
};

// ---------------------------------------------------------------------------
AdvancedPanel::AdvancedPanel (juce::AudioProcessorValueTreeState& s, const ProductDefinition& def) : apvts (s), product (def)
{
    for (auto stage : product.stages)
    {
        auto rb = std::make_unique<RailButton> (stage);
        rb->onClick = [this, stage] { setModule (stage); };
        addAndMakeVisible (*rb);
        rail.push_back (std::move (rb));
    }
    eqCurve = std::make_unique<EqCurveComponent>();
    gateScope = std::make_unique<GateScope>();
    compView = std::make_unique<CompView>();
    reductionView = std::make_unique<ReductionView>();
    inMeter = std::make_unique<MeterComponent>();
    outMeter = std::make_unique<MeterComponent>();
    polarityButton = std::make_unique<FlatButton> ("POLARITY " + Glyph::dot() + " NORMAL", FlatButton::Style::Outline);
    polarityButton->setFontPx (11.5f);
    polarityButton->setPaddingX (14);
    polarityAtt = std::make_unique<BoolParamAttachment> (*apvts.getParameter (ParamID::polarity), *polarityButton, [this] (bool on)
    {
        polarityButton->setButtonText ("POLARITY " + Glyph::dot() + (on ? " INVERTED" : " NORMAL"));
        polarityButton->setCustomColours (on ? std::optional<juce::Colour> (Tokens::accentText) : std::nullopt, std::nullopt, std::nullopt);
        if (suggestion != nullptr) resized(); // the initial update fires mid-construction
    });
    suggestion = std::make_unique<SuggestionCard>();
    suggestion->apply.onClick = [this] { if (onApplySuggestion) onApplySuggestion (suggestion->get()); };
    suggestion->dismiss.onClick = [this] { if (onDismissSuggestion) onDismissSuggestion (suggestion->get()); };
    for (auto* c : std::initializer_list<juce::Component*> { eqCurve.get(), gateScope.get(), compView.get(), reductionView.get(), inMeter.get(), outMeter.get(), polarityButton.get(), suggestion.get() })
        addChildComponent (*c);

    const char* toneNames[] = { "LOW SHELF", "LOW MID", "HIGH MID", "HIGH SHELF" };
    for (int i = 0; i < ParamID::kToneBands; ++i)
        bandTiles.push_back (std::make_unique<BandTile> (apvts, "toneEq", i, toneNames[i]));
    for (int i = 0; i < ParamID::kCorrectiveBands; ++i)
        bandTiles.push_back (std::make_unique<BandTile> (apvts, "corrEq", i, "CORR " + juce::String (i + 1)));
    for (auto& t : bandTiles) addChildComponent (*t);

    setModule (ChainModule::EQ);
}

AdvancedPanel::~AdvancedPanel() = default;

ChainModule AdvancedPanel::moduleFor (const Recommendation& r)
{
    for (const auto& c : r.changes)
    {
        const juce::String id (c.paramId);
        if (id.startsWith ("deEss")) return ChainModule::DeEss;
        if (id.startsWith ("width")) return ChainModule::Width;
        if (id.startsWith ("limiter")) return ChainModule::Limiter;
    }
    switch (r.kind)
    {
        case Recommendation::Kind::Gate:        return ChainModule::Gate;
        case Recommendation::Kind::EQ:          return ChainModule::EQ;
        case Recommendation::Kind::Compression: return ChainModule::Comp;
        case Recommendation::Kind::Transient:   return ChainModule::Transient;
        case Recommendation::Kind::MixGain:     return ChainModule::Output;
        case Recommendation::Kind::Filter:
        case Recommendation::Kind::CaptureGain:
        case Recommendation::Kind::Info:
        default:                                return ChainModule::Input;
    }
}

void AdvancedPanel::setSuggestions (const std::vector<Recommendation>& recs)
{
    suggestions = recs;
    setModule (module);
}

void AdvancedPanel::setModule (ChainModule m)
{
    if (std::find (product.stages.begin(), product.stages.end(), m) == product.stages.end()) m = ChainModule::EQ; // not part of this product
    module = m;
    for (auto& rb : rail) rb->setToggleState (rb->module == m, juce::dontSendNotification);
    title = chainModuleName (m);
    subtitle = juce::String (juce::CharPointer_UTF8 (subtitleFor (m)));

    eqCurve->setVisible (m == ChainModule::EQ);
    for (auto& t : bandTiles) t->setVisible (m == ChainModule::EQ);
    gateScope->setVisible (m == ChainModule::Gate);
    compView->setVisible (m == ChainModule::Comp);
    reductionView->setVisible (m == ChainModule::DeEss || m == ChainModule::Width || m == ChainModule::Limiter);
    inMeter->setVisible (m == ChainModule::Input);
    polarityButton->setVisible (m == ChainModule::Input);
    outMeter->setVisible (m == ChainModule::Output);

    const Recommendation* rec = nullptr;
    for (const auto& r : suggestions)
        if (! r.changes.empty() && moduleFor (r) == m) { rec = &r; break; }
    suggestion->setVisible (rec != nullptr);
    if (rec != nullptr) suggestion->set (*rec);

    float flagHz = 0.0f;
    if (rec != nullptr && m == ChainModule::EQ)
        for (const auto& c : rec->changes)
            if (juce::String (c.paramId).endsWith ("Freq")) flagHz = c.value;
    if (flagHz > 0.0f) eqCurve->setFlag (flagHz, juce::String (rec->what).toUpperCase(), "tune " + describeChanges (*rec));
    else eqCurve->clearFlag();
    for (auto& t : bandTiles) t->setFlagged (flagHz > 0.0f && t->isOn() && std::abs (std::log2 (t->getFreq() / flagHz)) < 0.15f);

    rebuildTiles();
    resized();
    repaint();
}

void AdvancedPanel::rebuildTiles()
{
    tiles.clear();
    for (const auto& id : tileIds (module))
    {
        const auto* spec = findParameterSpec (id.toStdString());
        auto* param = apvts.getParameter (id);
        if (spec == nullptr || param == nullptr) continue;
        auto t = std::make_unique<ParamTile> (*param, *spec, tileLabel (*spec));
        addAndMakeVisible (*t);
        tiles.push_back (std::move (t));
    }
}

void AdvancedPanel::update (const LiveState& live)
{
    lastLive = &live;
    for (auto& rb : rail) rb->set (chainSummary (rb->module, live), chainEnabled (rb->module, live.params));
    switch (module)
    {
        case ChainModule::EQ:        eqCurve->update (live.params, live.sampleRate, live.activity); break;
        case ChainModule::Gate:      gateScope->update (live); break;
        case ChainModule::Comp:      compView->update (live); break;
        case ChainModule::DeEss:
        case ChainModule::Width:
        case ChainModule::Limiter:   reductionView->set (module, live); break;
        case ChainModule::Input:     inMeter->setLevels (live.inPeakDb, live.inHoldDb, live.inClipped); break;
        case ChainModule::Output:    outMeter->setLevels (live.outPeakDb, live.outHoldDb, live.outClipped); break;
        case ChainModule::Transient:
        case ChainModule::Count:
        default: break;
    }
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    g.setColour (window);
    g.fillRect (b);
    auto railArea = b.removeFromLeft (kRailWidth);
    g.setColour (panel);
    g.fillRect (railArea);
    g.setColour (hair);
    g.fillRect (railArea.removeFromRight (1));

    g.setColour (textDim);
    g.setFont (LNF::body (10.0f, 400));
    g.drawFittedText (juce::String ("Same parameters as Simple ") + Glyph::dash() + " switching modes never changes the sound.",
                      juce::Rectangle<int> (12, getHeight() - 52, kRailWidth - 24, 42), juce::Justification::bottomLeft, 3, 1.0f);

    auto content = b.reduced (kPadX, kPadY);
    auto head = content.removeFromTop (18);
    g.setColour (textHi);
    g.setFont (LNF::body (14.0f, 600));
    const int tw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 4;
    g.drawText (title, head.removeFromLeft (tw), juce::Justification::centredLeft);
    head.removeFromLeft (8);
    g.setColour (textLow);
    g.setFont (LNF::mono (10.5f, 400));
    g.drawText (subtitle, head, juce::Justification::bottomLeft);

    auto note = [&] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (textLow);
        g.setFont (LNF::body (11.0f, 400));
        g.drawFittedText (text, area, juce::Justification::centredLeft, 4, 1.0f);
    };
    if (module == ChainModule::Input)
        note (juce::String ("Trim adjusts level into DINE's processing. It is not the microphone preamp ") + Glyph::dash()
              + " if input health reads LOW or CLIPPING, fix it at the console.",
              juce::Rectangle<int> (polarityButton->getX(), polarityButton->getBottom() + 10, juce::jmin (420, getWidth() - polarityButton->getX() - kPadX), 48));
    else if (module == ChainModule::Output && product.hasLoudness && lastLive != nullptr)
        note (juce::String ("Loudness: ") + (lastLive->shortTermLufs > -100.0f ? juce::String (lastLive->shortTermLufs, 1) + " LUFS short-term, " + juce::String (lastLive->integratedLufs, 1) + " LUFS since play started" : juce::String ("waiting for signal"))
              + (lastLive->targetLufs > -100.0f ? ", target " + juce::String (lastLive->targetLufs, 0) + " LUFS" : juce::String()) + ". Output trim drives the level into the limiter; the ceiling never moves.",
              juce::Rectangle<int> (outMeter->getRight() + 14, outMeter->getY(), juce::jmin (420, getWidth() - outMeter->getRight() - 14 - kPadX), outMeter->getHeight()));
    else if (module == ChainModule::Output)
        note ("Output trim sets the level handed back to the host. Saturation adds harmonic density before the trim. Peaks hold for 2 s; the clip lamp latches until clicked.",
              juce::Rectangle<int> (outMeter->getRight() + 14, outMeter->getY(), juce::jmin (420, getWidth() - outMeter->getRight() - 14 - kPadX), outMeter->getHeight()));
    else if (module == ChainModule::Gate)
    {
        auto gb = gateScope->getBounds();
        note (juce::String ("Blue = gate open (hits passing) ") + Glyph::dot() + " grey = closed " + Glyph::dot() + " green line = threshold. Ghost notes should still open it; cymbal bleed should not.",
              juce::Rectangle<int> (gb.getX(), gb.getBottom() + 6, gb.getWidth(), 16));
    }
    else if (module == ChainModule::Transient)
    {
        auto area = juce::Rectangle<int> (content.getX(), head.getBottom() + kGap, content.getWidth(), juce::jmax (60, tiles.empty() ? 100 : tiles.front()->getY() - head.getBottom() - kGap * 2));
        LNF::drawSurface (g, area.toFloat(), inset, hair, Tokens::Radius::control);
        g.setColour (textLow);
        g.setFont (LNF::body (11.5f, 400));
        g.drawFittedText (juce::String ("Transient shaping ") + Glyph::dash() + " positive attack sharpens the hit, negative sustain tightens the tail. "
                          "Values below map to the same engine parameters Simple Mode's PUNCH and ATTACK reach.",
                          area.reduced (60, 10), juce::Justification::centred, 4, 1.0f);
    }
}

void AdvancedPanel::resized()
{
    auto b = getLocalBounds();
    auto railArea = b.removeFromLeft (kRailWidth).reduced (8, 10);
    for (auto& rb : rail) { rb->setBounds (railArea.removeFromTop (46)); railArea.removeFromTop (4); }

    auto content = b.reduced (kPadX, kPadY);
    content.removeFromTop (18 + kGap);

    // Tiles wrap from the bottom up.
    const int perRow = juce::jmax (1, (content.getWidth() + 8) / (ParamTile::kWidth + 8));
    const int rows = tiles.empty() ? 0 : (int (tiles.size()) + perRow - 1) / perRow;
    auto tileArea = content.removeFromBottom (rows * ParamTile::kHeight + juce::jmax (0, rows - 1) * 8);
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const int r = int (i) / perRow, c = int (i) % perRow;
        tiles[i]->setBounds (tileArea.getX() + c * (ParamTile::kWidth + 8), tileArea.getY() + r * (ParamTile::kHeight + 8), ParamTile::kWidth, ParamTile::kHeight);
    }
    if (rows > 0) content.removeFromBottom (kGap);

    if (suggestion->isVisible()) { suggestion->setBounds (content.removeFromTop (SuggestionCard::kHeight)); content.removeFromTop (kGap); }

    switch (module)
    {
        case ChainModule::EQ:
        {
            auto corr = content.removeFromBottom (58);
            content.removeFromBottom (8);
            auto tone = content.removeFromBottom (58);
            const int nt = ParamID::kToneBands, nc = ParamID::kCorrectiveBands;
            const int wt = (tone.getWidth() - 8 * (nt - 1)) / nt, wc = (corr.getWidth() - 8 * (nc - 1)) / nc;
            for (int i = 0; i < int (bandTiles.size()); ++i)
            {
                if (i < nt) { bandTiles[size_t (i)]->setBounds (tone.removeFromLeft (wt)); tone.removeFromLeft (8); }
                else        { bandTiles[size_t (i)]->setBounds (corr.removeFromLeft (wc)); corr.removeFromLeft (8); }
            }
            content.removeFromBottom (kGap);
            eqCurve->setBounds (content);
            break;
        }
        case ChainModule::Gate:
            gateScope->setBounds (content.withTrimmedBottom (24));
            break;
        case ChainModule::Comp:
            compView->setBounds (content);
            break;
        case ChainModule::DeEss:
        case ChainModule::Width:
        case ChainModule::Limiter:
            reductionView->setBounds (content);
            break;
        case ChainModule::Input:
        {
            inMeter->setBounds (content.removeFromLeft (44));
            content.removeFromLeft (14);
            const int pw = polarityButton->getIdealWidth();
            polarityButton->setBounds (content.getX(), content.getY() + juce::jmax (0, content.getHeight() / 2 - 40), pw, 32);
            break;
        }
        case ChainModule::Output:
            outMeter->setBounds (content.removeFromLeft (44));
            break;
        case ChainModule::Transient:
        case ChainModule::Count:
        default: break;
    }
}

} // namespace livemix
