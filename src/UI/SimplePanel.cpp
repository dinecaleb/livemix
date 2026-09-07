#include "SimplePanel.h"
#include "State/ParameterIDs.h"
#include "Intelligence/AIFeature.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

SimplePanel::SimplePanel (juce::AudioProcessorValueTreeState& apvts, const ProductDefinition& def)
    : product (def), input (apvts), chain (def)
{
    addAndMakeVisible (input);
    for (const auto& m : product.macros)
    {
        auto k = std::make_unique<MacroKnob> (apvts, m.id, m.label);
        k->getSlider().setTooltip (m.tooltip);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    }

    addAndMakeVisible (analyzeButton);
    analyzeButton.setFontPx (14.0f);
    analyzeButton.setPaddingX (28);
    analyzeButton.onClick = [this] { if (onAnalyze) onAnalyze(); };
    analyzeButton.setTooltip (juce::String ("Press once, then ") + product.playerPrompt + ". Dine listens for about 12 seconds and sets a good starting point for this source. "
                              "You can compare BEFORE and AFTER, then KEEP or REVERT. Works offline; nothing is sent anywhere.");

    addChildComponent (aiButton);
    aiButton.setVisible (kAIAssistAvailable); // AI is switched off on purpose (see AIFeature.h)
    aiButton.setDot (true, Tokens::mark);
    aiButton.onClick = [this] { if (onToggleAI) onToggleAI(); };
    aiButton.setTooltip ("Optional. Off = Tune works on its own, offline. On = an AI adds a second opinion when you press Tune. It never changes anything by itself.");

    addAndMakeVisible (chain);
    chain.onModuleClicked = [this] (ChainModule m) { if (onOpenModule) onOpenModule (m); };
}

void SimplePanel::update (const LiveState& live, const Status& s)
{
    input.update (live, s.role, s.style);
    chain.update (live);

    if (product.hasLoudness)
    {
        juce::String value, line;
        juce::Colour colour = Tokens::textMid;
        if (live.shortTermLufs > -100.0f)
        {
            value = juce::String (live.shortTermLufs, 1) + " LUFS";
            if (live.targetLufs > -100.0f)
            {
                const float diff = live.shortTermLufs - live.targetLufs;
                line = "target " + juce::String (live.targetLufs, 0) + " LUFS";
                if (std::abs (diff) <= 1.0f) { line += " " + Glyph::dot() + " on target"; colour = Tokens::okText; }
                else if (diff < 0.0f) { line += " " + Glyph::dot() + " " + juce::String (-diff, 1) + " dB quiet"; colour = Tokens::warn; }
                else { line += " " + Glyph::dot() + " " + juce::String (diff, 1) + " dB loud"; colour = Tokens::warn; }
            }
            else line = "no loudness target for this output";
            line += " " + Glyph::dot() + " peak " + LNF::formatValue (live.truePeakHoldDb, "dB", -60.0f, 6.0f);
            if (live.limiterGrDb > 0.5f) line += " " + Glyph::dot() + " limiting " + Glyph::minus() + juce::String (live.limiterGrDb, 1) + " dB";
        }
        else { value = Glyph::dash() + " LUFS"; line = "waiting for signal"; colour = Tokens::textLow; }
        if (value != loudnessValue || line != loudnessLine) { loudnessValue = value; loudnessLine = line; loudnessColour = colour; repaint(); }
    }

    const bool changed = s.liveSafe != status.liveSafe || s.aiOn != status.aiOn || s.aiAvailable != status.aiAvailable
                      || s.analyzeBusy != status.analyzeBusy || s.tuned != status.tuned || s.role != status.role || s.style != status.style || hint.isEmpty();
    status = s;
    if (! changed) return;
    hint = juce::String (styleProfileName (s.style)).toUpperCase() + " " + Glyph::dot() + " " + juce::String (juce::CharPointer_UTF8 (roleHint (s.role)));

    analyzeButton.setEnabled (! s.liveSafe && ! s.analyzeBusy);
    analyzeButton.setButtonText (s.analyzeBusy ? "LISTENING" + Glyph::ellip() : (s.tuned ? "RE-TUNE" : "TUNE"));
    aiButton.setEnabled (! s.liveSafe);
    aiButton.setToggleState (s.aiOn, juce::dontSendNotification);
    aiButton.setButtonText (s.aiOn ? (s.aiAvailable ? "AI ASSIST ON" : "AI ASSIST ON (NO KEY)") : "AI ASSIST OFF");
    aiButton.setDot (true, s.aiOn ? (s.aiAvailable ? Tokens::accentStroke : Tokens::warn) : Tokens::mark);
    resized();
    repaint();
}

void SimplePanel::paint (juce::Graphics& g)
{
    using namespace Tokens;
    g.setColour (window);
    g.fillRect (getLocalBounds());

    auto centre = getLocalBounds().withTrimmedLeft (InputPanel::kWidth).withTrimmedBottom (ChainStrip::kHeight);
    const int knobTop = knobs.empty() ? centre.getY() + 40 : knobs.front()->getY();
    g.setColour (textLow);
    g.setFont (LNF::body (11.5f, 400, 0.04f));
    g.drawText (hint, centre.getX(), knobTop - 24, centre.getWidth(), 16, juce::Justification::centred);

    int below = analyzeButton.getBottom() + 8;
    if (product.hasLoudness)
    {
        auto row = juce::Rectangle<int> (centre.getX(), below, centre.getWidth(), 22);
        g.setColour (textHi);
        g.setFont (LNF::mono (16.0f, 500));
        const int vw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), loudnessValue)) + 6;
        g.setColour (textLow);
        g.setFont (LNF::body (11.0f, 400));
        const int lw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), loudnessLine)) + 4;
        auto both = row.withSizeKeepingCentre (vw + 10 + lw, 22);
        g.setColour (textHi);
        g.setFont (LNF::mono (16.0f, 500));
        g.drawText (loudnessValue, both.removeFromLeft (vw), juce::Justification::centredLeft);
        both.removeFromLeft (10);
        g.setColour (loudnessColour);
        g.setFont (LNF::body (11.0f, 400));
        g.drawText (loudnessLine, both, juce::Justification::centredLeft);
        below += 24;
    }

    if (status.liveSafe)
    {
        g.setColour (ok);
        g.setFont (LNF::body (10.5f, 400));
        g.drawText (juce::String ("Live Safe is on ") + Glyph::dash() + " Tune and automatic changes are off until you switch it off.",
                    centre.getX(), below, centre.getWidth(), 14, juce::Justification::centred);
    }
}

void SimplePanel::resized()
{
    auto b = getLocalBounds();
    chain.setBounds (b.removeFromBottom (ChainStrip::kHeight));
    input.setBounds (b.removeFromLeft (InputPanel::kWidth));

    // Centre block: hint (16) + 6 + knobs (119) + 16 + buttons (40) (+ loudness / live-safe notes)
    const int extra = product.hasLoudness ? 24 : 0;
    const int blockH = 16 + 6 + MacroKnob::kHeight + 16 + 40 + extra;
    auto block = b.withSizeKeepingCentre (b.getWidth(), blockH);
    block.removeFromTop (22);
    auto knobRow = block.removeFromTop (MacroKnob::kHeight);
    const int gap = 14, kw = MacroKnob::kWidth, n = int (knobs.size());
    auto row = knobRow.withSizeKeepingCentre (kw * n + gap * (n - 1), MacroKnob::kHeight);
    for (auto& k : knobs) { k->setBounds (row.removeFromLeft (kw)); row.removeFromLeft (gap); }
    block.removeFromTop (16);
    auto buttons = block.removeFromTop (40);
    const int aw = analyzeButton.getIdealWidth(), iw = aiButton.isVisible() ? aiButton.getIdealWidth() + 12 : 0;
    auto brow = buttons.withSizeKeepingCentre (aw + iw, 40);
    analyzeButton.setBounds (brow.removeFromLeft (aw));
    if (aiButton.isVisible()) { brow.removeFromLeft (12); aiButton.setBounds (brow.withSizeKeepingCentre (iw - 12, 32)); }
}

} // namespace livemix
