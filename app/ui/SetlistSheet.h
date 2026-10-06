#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// THE SETLIST, edited (v4's "Setlist" sheet). The cues down the left in their order, with Now
// and Next; the one picked on the right: its name, what is happening (the scene it brings
// back - one of the four, a kept favourite, or the mix as it is), and the desk's two notes,
// Louder and Softer. Add, move up, move down and delete under the list. Nothing here changes
// the sound: a cue's scene comes back when somebody goes to it, on LIVE or with Space.
class SetlistSheet : public juce::Component, private juce::TextEditor::Listener
{
public:
    SetlistSheet (MixController&, int selectCue);
    ~SetlistSheet() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;

    int selectedCue() const noexcept { return picked; }
    void select (int cue);
    void refresh();                        // the controller may have moved "Now" from LIVE

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void lookAndFeelChanged() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void loadPicked();
    void commit();
    void pickScene (int scene, const std::string& favourite);
    void showFavourites();
    juce::Rectangle<int> cardBounds() const;
    juce::Rectangle<int> listBounds() const;
    int rowAt (juce::Point<int>) const;
    int rowsShown() const;

    MixController& controller;
    Setlist list;
    int picked = -1, firstRow = 0;
    bool loading = false;

    DineButton done { "Done", DineButton::Style::Filled };
    DineButton add { "Add a cue", DineButton::Style::Standard };
    DineButton up { "Move up", DineButton::Style::Standard };
    DineButton down { "Move down", DineButton::Style::Standard };
    DineButton remove { "Delete", DineButton::Style::Ghost };
    DineButton go { "Go to this cue", DineButton::Style::Standard };
    juce::TextEditor name, louder, softer;
    // What is happening: the four scenes, the mix as it is, and a kept favourite.
    std::array<std::unique_ptr<DineButton>, 6> scenes;
    juce::Rectangle<int> nameCaption, sceneCaption, sceneNote, louderCaption, softerCaption;

    static constexpr int kCardW = 900, kListW = 280, kRowH = 44, kPad = 22;
};

} // namespace livemix
