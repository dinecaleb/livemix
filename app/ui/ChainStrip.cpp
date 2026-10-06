#include "ChainStrip.h"
#include "UI/Widgets.h"
#include <cmath>

namespace livemix
{

namespace
{
    juce::String signed1 (float v)
    {
        return (v >= 0.0f ? "+" : Glyph::minus()) + juce::String (std::fabs (v), 1);
    }

    juce::String hz (float f)
    {
        if (f >= 1000.0f)
        {
            const float k = f / 1000.0f;
            return juce::String (k, k < 10.0f ? 1 : 0) + "k";
        }
        return juce::String (juce::roundToInt (f));
    }

    int countBands (const EQBandParams* bands, int n)
    {
        int used = 0;
        for (int i = 0; i < n; ++i)
            if (bands[i].enabled && std::fabs (bands[i].gainDb) > 0.05f) ++used;
        return used;
    }

    juce::String bandText (int n)
    {
        return juce::String (n) + (n == 1 ? " band" : " bands");
    }
}

std::vector<ChainStage> chainStages (const ChannelParameters& p, bool includeLimiter, bool stereo, bool includeSample)
{
    std::vector<ChainStage> out;
    auto add = [&out] (const char* label, juce::String value, bool active)
    {
        out.push_back ({ juce::String (label), std::move (value), active });
    };

    add ("INPUT", signed1 (p.inputTrimDb) + (p.polarityInvert ? "  \xc3\xb8" : ""),
         std::fabs (p.inputTrimDb) > 0.05f || p.polarityInvert);

    {
        juce::String value;
        if (p.hpfEnabled) value = hz (p.hpfHz);
        if (p.lpfEnabled) value += (value.isEmpty() ? juce::String() : "  " + Glyph::dot() + "  ") + hz (p.lpfHz);
        add ("FILTERS", value.isEmpty() ? Glyph::dash() : value, p.hpfEnabled || p.lpfEnabled);
    }

    add ("GATE", p.gateEnabled ? signed1 (p.gateThresholdDb) : Glyph::dash(), p.gateEnabled);
    if (includeSample)
        add ("SAMPLE", p.replaceEnabled ? juce::String (juce::roundToInt (p.replaceBlend * 100.0f)) + "%" : Glyph::dash(), p.replaceEnabled);

    {
        const int n = countBands (p.correctiveBands.data(), ParamID::kCorrectiveBands);
        add ("EQ", n > 0 ? bandText (n) : Glyph::dash(), p.correctiveEqEnabled && n > 0);
    }

    add ("DE-ESS", p.deEssEnabled ? hz (p.deEssHz) : Glyph::dash(), p.deEssEnabled);
    add ("COMP", p.compEnabled ? juce::String (p.compRatio, 1) + ":1" : Glyph::dash(), p.compEnabled);
    add ("TRANSIENT", p.transientEnabled ? signed1 (p.transientAttack * 10.0f) : Glyph::dash(), p.transientEnabled);

    {
        const int n = countBands (p.toneBands.data(), ParamID::kToneBands);
        add ("TONE", n > 0 ? bandText (n) : Glyph::dash(), p.toneEqEnabled && n > 0);
    }

    add ("SAT", p.satEnabled ? juce::String (juce::roundToInt (p.satDrive * 100.0f)) + "%" : Glyph::dash(), p.satEnabled);
    if (stereo)
        add ("WIDTH", p.widthEnabled ? juce::String (p.widthAmount, 2) + "x" : Glyph::dash(), p.widthEnabled);
    if (includeLimiter)
        add ("LIMIT", p.limiterEnabled ? signed1 (p.limiterCeilingDb) : Glyph::dash(), p.limiterEnabled);

    add ("OUT", signed1 (p.outputTrimDb), true);
    return out;
}

std::vector<ChainStage> activeChainStages (const ChannelParameters& p, bool includeLimiter, bool stereo, bool includeSample)
{
    std::vector<ChainStage> out;
    for (auto& s : chainStages (p, includeLimiter, stereo, includeSample))
        if (s.active && s.label != "INPUT" && s.label != "OUT") out.push_back (s);
    return out;
}

// ------------------------------------------------------------------ ChainStrip
ChainStrip::ChainStrip()
{
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ChainStrip::setSource (const juce::String& n, juce::Colour c, const ChannelParameters& p,
                            bool includeLimiter, bool stereo, bool includeSample)
{
    // This is called at the page's rate, so it only repaints when what it draws has changed.
    auto next = chainStages (p, includeLimiter, stereo, includeSample);
    bool same = hasSource && n == name && c == tint && next.size() == stages.size();
    for (size_t i = 0; same && i < next.size(); ++i)
        same = next[i].label == stages[i].label && next[i].value == stages[i].value
               && next[i].active == stages[i].active;
    if (same) return;

    name = n;
    tint = c;
    stages = std::move (next);
    hasSource = true;
    repaint();
}

void ChainStrip::setEmpty (const juce::String& message)
{
    if (! hasSource && message == empty) return;
    empty = message;
    stages.clear();
    hasSource = false;
    repaint();
}

void ChainStrip::setNote (const juce::String& text)
{
    if (text == note) return;
    note = text;
    repaint();
}

// The v4 chain strip (docs/design/v4): a card along the foot of every workspace - the
// picked-out channel's group lamp and name, then one chip per stage in chain order, each with
// a lamp (the accent while the stage is on, a quiet ring of white while it is off), its name
// and what it is set to in mono. A stage that is off stays in the row, so the gaps can be read.
void ChainStrip::paint (juce::Graphics& g)
{
    const auto card = getLocalBounds().toFloat();
    Dine::fillRounded (g, card, Dine::card, 12.0f);
    Dine::hairlineRounded (g, card.reduced (0.5f), Dine::hairSoft, 11.5f);

    auto row = getLocalBounds().reduced (13, 0);

    if (note.isNotEmpty())
    {
        const auto noteFont = Dine::text (11.0f, 700);
        auto cell = row.removeFromRight (juce::jmin (row.getWidth() / 2, Dine::textWidth (noteFont, note)));
        g.setColour (Dine::warn);
        g.setFont (noteFont);
        Dine::drawText (g, note, cell, juce::Justification::centredRight);
        row.removeFromRight (14);
    }

    if (! hasSource)
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, empty, row, juce::Justification::centredLeft, true);
        return;
    }

    // The group lamp and the name; the name lights when the pointer is on the strip, which
    // is what says the whole strip opens the Inspector.
    g.setColour (tint);
    g.fillRoundedRectangle (row.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat(), 3.0f);
    row.removeFromLeft (7);
    const auto nameFont = Dine::text (12.5f, 600);
    g.setColour (hover ? Dine::ink : Dine::ink.withMultipliedAlpha (0.92f));
    g.setFont (nameFont);
    Dine::drawText (g, name, row.removeFromLeft (juce::jmin (160, Dine::textWidth (nameFont, name))),
                    juce::Justification::centredLeft, true);
    row.removeFromLeft (14);

    const auto labelFont = Dine::text (11.5f);
    const auto valueFont = Dine::mono (10.5f);
    for (const auto& s : stages)
    {
        const juce::String label = s.label.substring (0, 1) + s.label.substring (1).toLowerCase();
        const int labelW = Dine::textWidth (labelFont, label);
        const int valueW = s.value.isEmpty() ? 0 : Dine::textWidth (valueFont, s.value);
        const int chipW = 10 + 6 + 6 + labelW + (valueW > 0 ? 6 + valueW : 0) + 10;
        if (chipW > row.getWidth()) break;
        auto chip = row.removeFromLeft (chipW).withSizeKeepingCentre (chipW, 28);
        row.removeFromLeft (6);
        Dine::fillRounded (g, chip.toFloat(), juce::Colours::white.withAlpha (0.05f), 8.0f);
        auto inner = chip.reduced (10, 0);
        g.setColour (s.active ? Dine::accent : juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (inner.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
        inner.removeFromLeft (6);
        g.setColour (s.active ? Dine::ink.withMultipliedAlpha (0.88f) : Dine::ink3);
        g.setFont (labelFont);
        Dine::drawText (g, label, inner.removeFromLeft (labelW), juce::Justification::centredLeft);
        if (valueW > 0)
        {
            inner.removeFromLeft (6);
            g.setColour (Dine::ink3);
            g.setFont (valueFont);
            Dine::drawText (g, s.value, inner.removeFromLeft (valueW), juce::Justification::centredLeft);
        }
    }
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (hasSource && onOpen && ! e.mouseWasDraggedSinceMouseDown()) onOpen();
}

} // namespace livemix
