#include "MeterComponent.h"
#include "LiveMixLookAndFeel.h"

namespace livemix
{

void MeterComponent::setLevels (float p, float h, bool clipped)
{
    if (clipped) clipLatched = true;
    if (std::abs (p - peakDb) < 0.05f && std::abs (h - holdDb) < 0.05f) return;
    peakDb = p; holdDb = h;
    repaint();
}

void MeterComponent::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds().toFloat();
    LiveMixLookAndFeel::fillSurface (g, b, inset, Tokens::Radius::control);

    if (orientation == Orientation::Vertical)
    {
        const float h = b.getHeight(), y = yFor (peakDb, h);
        juce::ColourGradient grad (okDeep, 0.0f, b.getBottom(), crit, 0.0f, b.getY(), false);
        grad.addColour (0.72, ok);
        grad.addColour (0.82, warn);
        grad.addColour (0.94, crit);
        g.setGradientFill (grad);
        g.fillRect (b.getX() + 3.0f, b.getY() + y, b.getWidth() - 6.0f, h - y);

        const float py = yFor (holdDb, h);
        g.setColour (holdDb >= -0.2f ? critText : textHi);
        g.fillRect (b.getX() + 3.0f, b.getY() + juce::jmin (py, h - 2.0f), b.getWidth() - 6.0f, 2.0f);
        if (clipLatched) { g.setColour (crit); g.fillRect (b.getX(), b.getY(), b.getWidth(), 5.0f); }
    }
    else
    {
        const float f = normFor (peakDb);
        g.setColour (peakDb >= -1.0f ? crit : peakDb > -8.0f ? warn : ok);
        g.fillRoundedRectangle (b.getX(), b.getY() + 1.0f, b.getWidth() * f, b.getHeight() - 2.0f, 2.0f);
        const float hx = b.getX() + b.getWidth() * normFor (holdDb);
        g.setColour (textHi.withAlpha (0.8f));
        g.fillRect (juce::jmin (hx, b.getRight() - 1.5f), b.getY() + 1.0f, 1.5f, b.getHeight() - 2.0f);
    }
    LiveMixLookAndFeel::strokeSurface (g, b, hair, Tokens::Radius::control);
}

} // namespace livemix
