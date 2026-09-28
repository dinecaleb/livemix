#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppTheme.h"
#include "native/ThemeStore.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// APPEARANCE (View > Appearance > Customise...)
//
// Down the left, every theme: DLIVE's own first, then the ones in ~/Music/DLIVE/Themes.
// Clicking one applies it to the whole app at once and remembers it for next time - the
// console behind the sheet is the preview, which is why the scrim is lighter here than
// under any other sheet. On the right, every colour the theme sets, grouped the way
// AppTheme names them, each a swatch that opens a colour picker; a change lands live in
// every window as the picker moves. A built-in theme cannot be changed: edit one and
// what you save is a theme of your own, named in the box, with the built-in as its base
// and only the colours you changed written to the file. Import copies somebody else's
// file into the folder after reading it (a broken one is refused with its reason);
// Export writes a complete file that stands on its own.
//
// Nothing here touches the session or the mix.
// ---------------------------------------------------------------------------
class ThemeSheet : public juce::Component
{
public:
    // `persisting` off (the headless snapshot tool): this Mac's chosen theme is never written.
    explicit ThemeSheet (bool persisting = true);
    ~ThemeSheet() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onToast;
    // Every window re-reads the tokens; the host also persists the choice.
    std::function<void()> onThemeChanged;

    void refresh();                        // re-list the folder, keep what is chosen
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void lookAndFeelChanged() override;

    // Programmatic equivalents of the clicks, for the menu and the snapshot tool.
    void chooseTheme (const juce::String& name);
    void importTheme();
    const Theme& getWorking() const noexcept { return working; }

private:
    class ThemeRow;
    class Swatch;
    class Grid;

    juce::Rectangle<int> cardBounds() const;
    void rebuildRows();
    void rebuildSwatches();
    void select (const Theme&);
    void colourEdited (const juce::String& key, juce::Colour);
    void saveTheme();
    void deleteTheme();
    void resetTheme();
    void exportTheme();
    void revealFolder();
    void themeChanged();
    void updateButtons();
    juce::String baseName() const;         // the built-in a saved copy of `working` is based on

    std::vector<Theme> themes;             // built-in first, then the user's
    Theme selected;                        // the row that is ticked
    Theme working;                         // what the swatches show: `selected`, resolved, plus live edits
    bool edited = false;
    const bool persisting;

    juce::Viewport railView, gridView;
    juce::Component railContent;
    std::unique_ptr<Grid> grid;
    std::vector<std::unique_ptr<ThemeRow>> rows;
    std::vector<std::unique_ptr<Swatch>> swatches;

    juce::TextEditor nameBox;
    DineButton saveButton   { "Save",   DineButton::Style::Filled };
    DineButton resetButton  { "Undo changes", DineButton::Style::Standard };
    DineButton deleteButton { "Delete", DineButton::Style::Standard };
    DineButton importButton { "Import" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")), DineButton::Style::Standard };
    DineButton exportButton { "Export" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")), DineButton::Style::Standard };
    DineButton folderButton { "Show folder", DineButton::Style::Ghost };
    DineButton closeButton  { "Close", DineButton::Style::Standard };

    // The sample console under the name: real widgets reading the live tokens.
    DineKey keyMute { "M", Dine::keyMute }, keySolo { "S", Dine::keySolo }, keyRec { "R", Dine::keyRec }, keyMon { "A", Dine::keyMon };
    DineButton sampleFilled { "TUNE MIX", DineButton::Style::Filled };
    DineButton sampleStandard { "Keep", DineButton::Style::Standard };
    PanBar samplePan;

    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace livemix
