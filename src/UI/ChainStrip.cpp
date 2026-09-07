#include "ChainStrip.h"
#include "Widgets.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

const char* chainModuleName (ChainModule m) noexcept
{
    switch (m)
    {
        case ChainModule::Input:     return "INPUT";
        case ChainModule::Gate:      return "GATE";
        case ChainModule::EQ:        return "EQ";
        case ChainModule::DeEss:     return "DE-ESS";
        case ChainModule::Comp:      return "COMP";
        case ChainModule::Transient: return "TRANSIENT";
        case ChainModule::Width:     return "WIDTH";
        case ChainModule::Limiter:   return "LIMITER";
        case ChainModule::Output:    return "OUTPUT";
        case ChainModule::Count:
        default:                     return "";
    }
}

const char* chainModuleFriendlyName (ChainModule m, Product p) noexcept
{
    switch (m)
    {
        case ChainModule::Input:     return "INPUT";
        case ChainModule::Gate:      return p == Product::Drums ? "BLEED" : "CLEAN-UP";
        case ChainModule::EQ:        return "TONE";
        case ChainModule::DeEss:     return "S CONTROL";
        case ChainModule::Comp:      return p == Product::Master ? "GLUE" : "LEVEL";
        case ChainModule::Transient: return "SNAP";
        case ChainModule::Width:     return "WIDTH";
        case ChainModule::Limiter:   return "SAFETY";
        case ChainModule::Output:    return "OUTPUT";
        case ChainModule::Count:
        default:                     return "";
    }
}

bool chainEnabled (ChainModule m, const ChannelParameters& p) noexcept
{
    switch (m)
    {
        case ChainModule::Input:     return true;
        case ChainModule::Gate:      return p.gateEnabled;
        case ChainModule::EQ:        return p.correctiveEqEnabled || p.toneEqEnabled;
        case ChainModule::DeEss:     return p.deEssEnabled;
        case ChainModule::Comp:      return p.compEnabled;
        case ChainModule::Transient: return p.transientEnabled;
        case ChainModule::Width:     return p.widthEnabled;
        case ChainModule::Limiter:   return p.limiterEnabled;
        case ChainModule::Output:    return true;
        case ChainModule::Count:
        default:                     return false;
    }
}

juce::String chainSummary (ChainModule m, const LiveState& live)
{
    const auto& p = live.params;
    auto db = [] (float v) { return LNF::formatValue (v, "dB", -24.0f, 24.0f); };
    switch (m)
    {
        case ChainModule::Input:
        {
            juce::String s = db (p.inputTrimDb);
            if (p.hpfEnabled) s += " " + Glyph::dot() + " HPF " + LNF::formatValue (p.hpfHz, "Hz", 20.0f, 1000.0f);
            return s;
        }
        case ChainModule::Gate:
            return p.gateEnabled ? "thr " + db (p.gateThresholdDb) : juce::String ("off");
        case ChainModule::EQ:
        {
            int n = 0;
            if (p.toneEqEnabled) for (const auto& b : p.toneBands) n += b.enabled ? 1 : 0;
            if (p.correctiveEqEnabled) for (const auto& b : p.correctiveBands) n += b.enabled ? 1 : 0;
            if (! p.toneEqEnabled && ! p.correctiveEqEnabled) return "off";
            return n == 0 ? juce::String ("flat") : juce::String (n) + (n == 1 ? " band" : " bands");
        }
        case ChainModule::DeEss:
            return p.deEssEnabled ? "up to " + Glyph::minus() + juce::String (p.deEssRangeDb, 0) + " dB" : juce::String ("off");
        case ChainModule::Comp:
            return p.compEnabled ? juce::String (p.compRatio, 1) + ":1" : juce::String ("off");
        case ChainModule::Transient:
            return p.transientEnabled ? LNF::formatValue (p.transientAttack, "", -1.0f, 1.0f) : juce::String ("off");
        case ChainModule::Width:
            return p.widthEnabled ? juce::String (juce::roundToInt (p.widthAmount * 100.0f)) + " %" : juce::String ("as recorded");
        case ChainModule::Limiter:
            return p.limiterEnabled ? "ceiling " + LNF::formatValue (p.limiterCeilingDb, "dB", -12.0f, 0.0f) : juce::String ("off");
        case ChainModule::Output:
        {
            if (live.product != nullptr && live.product->hasLoudness && live.shortTermLufs > -100.0f)
                return juce::String (live.shortTermLufs, 1) + " LUFS";
            juce::String s = db (p.outputTrimDb);
            if (p.satEnabled) s += " " + Glyph::dot() + " sat";
            return s;
        }
        case ChainModule::Count:
        default: return {};
    }
}

// ---------------------------------------------------------------------------
void ChainStrip::Item::set (const juce::String& s, bool en, float act, juce::Colour col)
{
    const bool changed = summary != s || enabled != en || std::abs (activity - act) > 0.01f;
    summary = s; enabled = en; activity = act; activityColour = col;
    if (changed) repaint();
}

void ChainStrip::Item::paintButton (juce::Graphics& g, bool over, bool)
{
    using namespace Tokens;
    auto b = getLocalBounds().toFloat();
    LNF::drawSurface (g, b, over ? juce::Colour (0xff1a1f24) : raised, over ? hairHover : hair2, Tokens::Radius::control);

    auto inner = b.reduced (9.0f, 0.0f).withTrimmedTop (8.0f).withTrimmedBottom (7.0f);
    auto row = inner.removeFromTop (13.0f);
    g.setColour (enabled ? ok : mark);
    g.fillEllipse (row.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
    row.removeFromLeft (5.0f);
    g.setColour (textHi);
    g.setFont (LNF::body (11.0f, 600));
    const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), getButtonText());
    g.drawText (getButtonText(), row.removeFromLeft (tw + 2.0f), juce::Justification::centredLeft);
    if (technical != getButtonText() && row.getWidth() > 30.0f)
    {
        g.setColour (textLow);
        g.setFont (LNF::mono (9.0f, 400));
        g.drawText (technical.toLowerCase(), row.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
    }
    inner.removeFromTop (3.0f);
    g.setColour (textMid);
    g.setFont (LNF::mono (10.0f, 400));
    g.drawText (summary, inner.removeFromTop (12.0f), juce::Justification::centredLeft, true);
    auto strip = juce::Rectangle<float> (inner.getX(), b.getBottom() - 10.0f, inner.getWidth(), 3.0f);
    g.setColour (track);
    g.fillRoundedRectangle (strip, 1.5f);
    g.setColour (activityColour);
    g.fillRoundedRectangle (strip.withWidth (strip.getWidth() * juce::jlimit (0.0f, 1.0f, activity)), 1.5f);
}

ChainStrip::ChainStrip (const ProductDefinition& def) : product (def)
{
    for (auto stage : product.stages)
    {
        auto it = std::make_unique<Item> (stage, product.product);
        addAndMakeVisible (*it);
        auto* raw = it.get();
        it->onClick = [this, raw] { if (onModuleClicked) onModuleClicked (raw->module); };
        it->setTooltip (juce::String ("Open ") + chainModuleName (stage) + " in Advanced.");
        items.push_back (std::move (it));
    }
}

void ChainStrip::update (const LiveState& live)
{
    const auto& p = live.params;
    for (auto& it : items)
    {
        float act = live.activity;
        juce::Colour col = Tokens::accentStroke;
        switch (it->module)
        {
            case ChainModule::Gate:    act = p.gateEnabled ? live.gateEnvelope : live.activity; break;
            case ChainModule::Comp:    act = juce::jlimit (0.0f, 1.0f, live.compGrDb / 10.0f); col = Tokens::warn; break;
            case ChainModule::DeEss:   act = juce::jlimit (0.0f, 1.0f, live.deEssGrDb / 8.0f); col = Tokens::warn; break;
            case ChainModule::Limiter: act = juce::jlimit (0.0f, 1.0f, live.limiterGrDb / 6.0f); col = Tokens::warn; break;
            case ChainModule::Width:   act = juce::jlimit (0.0f, 1.0f, 0.5f * (1.0f - live.correlation)); break;
            case ChainModule::Output:  act = juce::jlimit (0.0f, 1.0f, (juce::jmin (0.0f, live.outPeakDb) + 60.0f) / 60.0f); break;
            default: break;
        }
        it->set (chainSummary (it->module, live), chainEnabled (it->module, p), act, col);
    }
}

void ChainStrip::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    g.setColour (subBar);
    g.fillRect (b);
    g.setColour (hair);
    g.fillRect (b.removeFromTop (1));

    // Vertical "CHAIN" label at the left edge
    g.saveState();
    g.setColour (textLow);
    g.setFont (LNF::body (10.0f, 600));
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 22.0f, float (getHeight()) * 0.5f));
    g.drawText ("CHAIN", juce::Rectangle<int> (22 - 40, getHeight() / 2 - 7, 80, 14), juce::Justification::centred);
    g.restoreState();

    for (size_t i = 0; i + 1 < items.size(); ++i)
    {
        const auto r = items[i]->getBounds();
        LNF::drawIcon (g, LNF::Icon::ArrowRight, juce::Rectangle<float> (float (r.getRight()), float (r.getCentreY()) - 5.0f, 12.0f, 10.0f), mark);
    }
}

void ChainStrip::resized()
{
    auto b = getLocalBounds().reduced (16, 0).withTrimmedLeft (18);
    const int n = juce::jmax (1, int (items.size()));
    const int gap = 12;
    const int w = (b.getWidth() - gap * (n - 1)) / n;
    auto row = b.withSizeKeepingCentre (b.getWidth(), 64);
    for (auto& it : items)
    {
        it->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (gap);
    }
}

} // namespace livemix
