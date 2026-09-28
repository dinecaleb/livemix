#include "PurposeSheet.h"

namespace livemix
{

PurposeSheet::PurposeSheet (PurposePage& p) : page (p)
{
    // The page has a footer of its own - a sentence, Back, and TUNE THE MIX - and that footer
    // is the sheet's footer. A second Close under it would be a second way to do what Back
    // already does, which is how a sheet ends up with two rows of buttons and no headline.
    addAndMakeVisible (page);
    setWantsKeyboardFocus (false);
}

PurposeSheet::~PurposeSheet()
{
    // The page belongs to the window, not to this sheet: give it back rather than take it away.
    removeChildComponent (&page);
}

juce::Rectangle<int> PurposeSheet::cardBounds() const
{
    auto r = getLocalBounds();
    const int w = juce::jmin (r.getWidth() - 80, 960);
    const int h = juce::jmin (r.getHeight() - 80, 640);
    return r.withSizeKeepingCentre (juce::jmax (420, w), juce::jmax (320, h));
}

void PurposeSheet::paint (juce::Graphics& g)
{
    g.setColour (Dine::scrim.withAlpha (0.55f));
    g.fillRect (getLocalBounds());
    Dine::drawSheet (g, cardBounds().toFloat(), Dine::Radius::card);
}

void PurposeSheet::resized()
{
    page.setBounds (cardBounds());
}

void PurposeSheet::refresh() { page.refresh(); }

void PurposeSheet::mouseUp (const juce::MouseEvent& e)
{
    // Clicking the console behind the sheet closes it, the way every other sheet behaves.
    if (! cardBounds().contains (e.getPosition()) && onClose) onClose();
}

} // namespace livemix
