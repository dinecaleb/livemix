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
    g.setColour (juce::Colour (0xff181a1c));
    g.fillRect (r);
    Dine::drawRule (g, r.withHeight (1), Dine::hair);

    auto row = r.reduced (14, 0);

    // the right-hand note first, so the chain never runs under it
    if (note.isNotEmpty())
    {
        const auto noteFont = Dine::mono (10.5f);
        auto cell = row.removeFromRight (juce::jmin (row.getWidth() / 2, Dine::textWidth (noteFont, note)));
        g.setColour (Dine::ink3);
        g.setFont (noteFont);
        g.drawText (note, cell, juce::Justification::centredRight);
        row.removeFromRight (14);
    }

    if (! hasSource)
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::text (11.5f));
        g.drawText (empty, row, juce::Justification::centredLeft, true);
        return;
    }

    g.setColour (tint);
    g.fillRect (row.removeFromLeft (3).withSizeKeepingCentre (3, 15));
    row.removeFromLeft (9);

    const auto nameFont = Dine::text (11.5f, 600);
    g.setColour (Dine::ink);
    g.setFont (nameFont);
    g.drawText (name, row.removeFromLeft (juce::jmin (170, Dine::textWidth (nameFont, name))),
                juce::Justification::centredLeft, true);
    row.removeFromLeft (10);

    // The chain, stage by stage, with an arrow between. Stages that are off are still shown
    // - a chain you can read is a chain whose gaps you can see - but they are drawn quiet.
    const auto labelFont = Dine::text (9.5f, 700).withExtraKerningFactor (0.07f);
    const auto valueFont = Dine::mono (9.5f);
    for (size_t i = 0; i < stages.size(); ++i)
    {
        const auto& s = stages[i];
        const int labelW = Dine::textWidth (labelFont, s.label);
        const int valueW = Dine::textWidth (valueFont, s.value);
        const int chipW = 7 + 4 + 5 + labelW + 5 + valueW + 7;
        if (chipW + 16 > row.getWidth()) break;

        auto chip = row.removeFromLeft (chipW).withSizeKeepingCentre (chipW, 20);
        const bool out = s.label == "OUT";
        Dine::fillRounded (g, chip.toFloat(), s.active ? (out ? Dine::accent.withAlpha (0.14f)
                                                              : juce::Colours::white.withAlpha (0.06f))
                                                       : juce::Colours::white.withAlpha (0.025f),
                           Dine::Radius::chip);
        Dine::hairlineRounded (g, chip.toFloat(), s.active ? (out ? Dine::accent.withAlpha (0.4f) : Dine::hair)
                                                           : Dine::hairSoft, Dine::Radius::chip);

        auto inner = chip.reduced (7, 0);
        auto dot = inner.removeFromLeft (4).withSizeKeepingCentre (4, 4);
        g.setColour (s.active ? (out ? Dine::accent : tint) : juce::Colours::white.withAlpha (0.14f));
        g.fillEllipse (dot.toFloat());
        inner.removeFromLeft (5);

        g.setColour (s.active ? Dine::ink2 : Dine::ink4);
        g.setFont (labelFont);
        g.drawText (s.label, inner.removeFromLeft (labelW), juce::Justification::centredLeft);
        inner.removeFromLeft (5);
        g.setColour (s.active ? Dine::ink3 : Dine::ink4);
        g.setFont (valueFont);
        g.drawText (s.value, inner, juce::Justification::centredLeft);

        if (i + 1 < stages.size())
        {
            auto arrow = row.removeFromLeft (14);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.setFont (Dine::text (10.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), arrow, juce::Justification::centred);
        }
    }
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (hasSource && onOpen && ! e.mouseWasDraggedSinceMouseDown()) onOpen();
}

} // namespace livemix
