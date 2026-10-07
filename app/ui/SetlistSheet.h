#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// THE SETLIST, edited (v4's prototype). The cues down the left in their order, with what
// each one is (Band, Speaking...) and Now and Next; the one picked on the right: its name,
// what is happening (four presets that fill who is on), and who is on - a switch per source
// and, for the ones that are, a level in dB from where the cue starts, shown as a number.
// Everyone switched off is muted when the cue starts. A cue can also start from a mix of its
// own (saved with it), a kept scene or a favourite. Add, move, delete at the foot.
// Nothing here changes the sound until somebody goes to the cue.
class SetlistSheet : public juce::Component
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

    // For the tests and the snapshot tool: what the editor would do on a press.
    void chooseKind (CueKind);
    void setWho (const std::string& unit, CueLevel);
    void setLevelDb (const std::string& unit, float db);   // a level set by hand, in dB
    void useWhatIsOnNow();

private:
    class Editor;                          // the right-hand side, in a viewport of its own
    void loadPicked();
    void store (const Cue&);
    juce::Rectangle<int> cardBounds() const;
    juce::Rectangle<int> listBounds() const;
    int rowAt (juce::Point<int>) const;
    int rowsShown() const;

    MixController& controller;
    Setlist list;
    int picked = -1, firstRow = 0;

    DineButton done { "Done", DineButton::Style::Filled };
    DineButton add { "Add cue", DineButton::Style::Filled };
    DineButton up { "", DineButton::Style::Standard };
    DineButton down { "", DineButton::Style::Standard };
    DineButton remove { "Delete", DineButton::Style::Ghost };
    std::unique_ptr<Editor> editor;
    juce::Viewport editorView;

    static constexpr int kCardW = 960, kListW = 280, kRowH = 50, kPad = 22, kHeadH = 64;
};

} // namespace livemix
