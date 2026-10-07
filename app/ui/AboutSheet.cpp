#include "AboutSheet.h"

namespace livemix
{

AboutSheet::AboutSheet (juce::String v) : version (std::move (v))
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
}

juce::Rectangle<int> AboutSheet::cardBounds() const
{
    // The identity's 16:10 splash, as wide as the window lets it be up to 720.
    const int w = juce::jmin (720, getWidth() - 60);
    const int h = juce::jmin (w * 10 / 16, getHeight() - 60);
    return juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
}

void AboutSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.86f));
    const auto card = cardBounds().toFloat();
    g.setColour (Dine::brandBlack);
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (Dine::brandPaper.withAlpha (0.14f));
    g.drawRoundedRectangle (card.reduced (0.25f), 12.0f, 0.5f);

    // The wordmark at half the identity's size (48 tall on a 720 card), the line under it.
    const float markH = juce::jmax (24.0f, card.getWidth() * 48.0f / 720.0f);
    const float lineH = 18.0f, gap = 22.0f;
    const float top = card.getCentreY() - (markH + gap + lineH) * 0.5f;
    Dine::drawWordmark (g, { card.getX(), top, card.getWidth(), markH }, Dine::brandPaper);
    g.setColour (Dine::brandSteel);
    g.setFont (Dine::text (14.0f));
    Dine::drawText (g, kTagline, juce::Rectangle<float> (card.getX(), top + markH + gap, card.getWidth(), lineH).toNearestInt(),
                    juce::Justification::centred, false);

    auto foot = card.reduced (24.0f, 0.0f).removeFromBottom (20.0f + 14.0f).removeFromTop (14.0f).toNearestInt();
    g.setColour (Dine::brandSteel.withAlpha (0.72f));
    g.setFont (Dine::mono (11.0f));
    Dine::drawText (g, "DINE Audio", foot, juce::Justification::centredLeft, false);
    if (version.isNotEmpty())
        Dine::drawText (g, "Version " + version, foot, juce::Justification::centredRight, false);
}

void AboutSheet::mouseUp (const juce::MouseEvent&)
{
    auto close = onClose;
    if (close) close();
}

bool AboutSheet::keyPressed (const juce::KeyPress&)
{
    auto close = onClose;
    if (close) close();
    return true;
}

} // namespace livemix
