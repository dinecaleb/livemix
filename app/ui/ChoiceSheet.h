#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppTheme.h"

namespace livemix
{

// The two sheets DLIVE uses to ask before something it cannot quietly undo: RESET THE MIX TO
// RAW (`25`, 88:23073) and RECOVER SESSION? (`23`, 88:22923). They are the same shape, so they
// are one component: a title, one sentence, two columns that say exactly what will happen and
// exactly what will not, a note under them, and the buttons - the destructive one in red type
// on a hairline, never a red fill, and the default one last the way macOS orders them.
//
// Nothing here is a warning triangle and a shrug. Every line is a fact about this session.
class ChoiceSheet : public juce::Component
{
public:
    struct Column
    {
        juce::String heading;
        juce::Colour tint { 0xffc2c4c9 };
        juce::StringArray lines;
        bool tick = false;          // a tick before each line rather than a dash
    };

    ChoiceSheet (const juce::String& title, const juce::String& sentence);
    ~ChoiceSheet() override;

    void setColumns (Column left, Column right);
    void setNote (const juce::String& boxed, const juce::String& quiet);
    // `destructive` draws in red type on a hairline; `isDefault` fills with the accent and
    // takes Return. The order on screen is the order they are added, left to right, at the
    // right of the footer.
    void addAction (const juce::String& text, bool destructive, bool isDefault, std::function<void()> action);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> cardBounds() const;
    int columnsHeight() const;

    juce::String title, sentence, boxedNote, quietNote;
    Column left, right;
    std::vector<std::unique_ptr<DineButton>> actions;
    std::vector<std::function<void()>> handlers;
    int defaultAction = -1;
};

} // namespace livemix
