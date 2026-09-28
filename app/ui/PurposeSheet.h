#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "AppTheme.h"
#include "SetupPages.h"

namespace livemix
{

// PURPOSE AND SOUND, as a sheet (the v2 design, frame "11").
//
// What the mix is for - a broadcast, a room, a recording - and which sound it is built
// around. It was step three of a four-step walk through setting a session up, and it is two
// questions: on the first Sunday you answer them once, and after that you come back to them
// when the service changes, not when the console does. That is a sheet, not a page.
//
// It is `PurposePage` itself on a card: the same fields, the same effects, the same TUNE THE
// MIX at the end of it. Nothing about what it does changed - only where it appears, and that
// it now has somewhere to appear *from*: the session's name on the title row.
class PurposeSheet : public juce::Component
{
public:
    explicit PurposeSheet (PurposePage&);
    ~PurposeSheet() override;

    std::function<void()> onClose;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<int> cardBounds() const;

private:
    PurposePage& page;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PurposeSheet)
};

} // namespace livemix
