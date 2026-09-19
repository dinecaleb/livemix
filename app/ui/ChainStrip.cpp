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

std::vector<ChainStage> chainStages (const ChannelParameters& p, bool includeLimiter, bool stereo)
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

std::vector<ChainStage> activeChainStages (const ChannelParameters& p, bool includeLimiter, bool stereo)
{
    std::vector<ChainStage> out;
    for (auto& s : chainStages (p, includeLimiter, stereo))
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
                            bool includeLimiter, bool stereo)
{
    // This is called at the page's rate, so it only repaints when what it draws has changed.
    auto next = chainStages (p, includeLimiter, stereo);
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

void ChainStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (Dine::window);
    g.fillRect (r);
    g.setColour (Dine::hair);
    g.fillRect (r.removeFromTop (1));   // the seam between the workspace and its chain foot

    auto row = r.reduced (18, 0);

    if (note.isNotEmpty())
    {
        const auto noteFont = Dine::caps (10.0f, 0.08f);
        auto cell = row.removeFromRight (juce::jmin (row.getWidth() / 2, Dine::textWidth (noteFont, note)));
        g.setColour (Dine::warn);
        g.setFont (noteFont);
        g.drawText (note, cell, juce::Justification::centredRight);
        row.removeFromRight (14);
    }

    if (! hasSource)
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (12.0f));
        g.drawText (empty, row, juce::Justification::centredLeft, true);
        return;
    }

    // The name, in the group's colour when it is hovered so the strip reads as one thing
    // that opens the Inspector.
    const auto nameFont = Dine::text (13.0f, 600);
    g.setColour (hover ? Dine::ink : Dine::ink2);
    g.setFont (nameFont);
    g.drawText (name, row.removeFromLeft (juce::jmin (180, Dine::textWidth (nameFont, name))),
                juce::Justification::centredLeft, true);
    row.removeFromLeft (8);

    // The chain, stage by stage: a chip per stage on the control plane, 11 px, the value
    // beside the name. A stage that is off stays in the row, quiet, so the gaps can be read.
    const auto font = Dine::text (11.0f);
    for (size_t i = 0; i < stages.size(); ++i)
    {
        const auto& s = stages[i];
        const juce::String label = s.label.substring (0, 1) + s.label.substring (1).toLowerCase();
        const juce::String textValue = label + " " + s.value;
        const int chipW = Dine::textWidth (font, textValue) + 18;
        if (chipW + 8 > row.getWidth()) break;
        auto chip = row.removeFromLeft (chipW).withSizeKeepingCentre (chipW, 24);
        const bool out = s.label == "OUT";
        Dine::fillRounded (g, chip.toFloat(), Dine::control, Dine::Radius::chip);
        g.setColour (! s.active ? Dine::ink4 : out ? Dine::accent : Dine::ink2);
        g.setFont (font);
        g.drawText (textValue, chip, juce::Justification::centred);
        row.removeFromLeft (8);
    }
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (hasSource && onOpen && ! e.mouseWasDraggedSinceMouseDown()) onOpen();
}

} // namespace livemix
