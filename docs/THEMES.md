# Themes: make DLIVE look the way you like

DLIVE's whole look is a table of named colours. A **theme** is that table written down. Pick one
and every workspace, sheet, menu, meter and console key reads it at once, the way an editor's
colour theme changes the whole editor and nothing about the document. A theme is a preference of
the Mac, not of the session: a service saved under one theme opens under whatever the booth has
chosen, and nothing about the mix, the routing or the recording ever depends on it.

This is a selling point and it is meant to be one. A church that mixes in the dark wants a black
desk and one bright colour; a booth with a window wants a light one; a volunteer who came from
another console wants it to look like the one they know. All of that is a file they can keep, send
to the next campus and edit by hand.

## For the person at the desk

**View > Appearance** lists every theme, DLIVE's own first and yours after a line, with the one in
use ticked. Click one and the whole app changes; DLIVE remembers it for next time.

**View > Appearance > Customise Appearance…** opens the Appearance sheet:

- Down the left, the same list. The console behind the sheet is the preview, so the scrim is
  lighter here than under any other sheet.
- Under the name box, a sample strip: the four console keys, a filled and a resting button, a
  balance, a meter, two status chips and the seven group tints. They are real widgets reading the
  live colours, so what the picker moves is what the console gets.
- On the right, every colour the theme sets, grouped by what it is for: **Surfaces** (the desk,
  the window, cards, rows, buttons), **Lines** (the hairlines and fills), **Ink** (four levels of
  type), **Accent** (the primary action, the active tab, what is selected, what DLIVE tuned, the
  meters), **Status** (ok / hot / warn / crit / monitor), **Keys** (M / S / R / A when they are on)
  and **Groups** (the bus tints). Each is a swatch; click it and a colour picker opens; every move
  of the picker lands in every open window as it happens.
- DLIVE's own themes cannot be changed. Edit a colour on one and **Save** writes a theme of your
  own, named in the box ("Studio Teal (mine)" is suggested), based on the built-in, with only the
  colours you changed in the file. **Undo changes** puts the chosen theme back. **Delete** removes
  one of yours and returns to Studio Teal.
- **Import…** copies a theme file somebody sent you into your themes. It is read first; a file
  that will not read is refused with the reason (not a theme, from a newer DLIVE, a colour that is
  not a colour). A file that carries one of DLIVE's own names is imported as "… (imported)".
- **Export…** writes a complete file that stands on its own, to send to another campus.
- **Show folder** opens `~/Music/DLIVE/Themes` in the Finder.

Nothing on this sheet touches the session or the mix. LIVE SAFE does not lock it: changing the
colour of the desk mid-service is not a risk to the broadcast.

## The built-in themes

| Name | What it is |
| --- | --- |
| **Studio Teal** | The default. The v2 desktop design: a deep blue-black desk and one teal spent where it matters. |
| **Lime Desk** | A black desk, neutral greys and a lime that is the only colour on it. For the booth that mixes in the dark. |
| **Slate** | Blue-grey planes a shade lighter than the default, with a sky-blue accent. For a bright booth. |
| **Tape** | Warm dark browns and an amber accent, the colour of a tape machine's meters. |
| **Daylight** | A light desk for a booth with a window. Every line that was white on black is black on white. |

## The file

`~/Music/DLIVE/Themes/<name>.dlivetheme.json` — one JSON document per theme, beside the sessions so
backing up one folder backs up both. The chosen theme's name is in `~/Music/DLIVE/preferences.json`
under `theme`; no entry means Studio Teal.

```json
{
  "app": "DLIVE",
  "kind": "theme",
  "schema": 1,
  "name": "Sunday lime",
  "note": "The main hall's desk",
  "basedOn": "Studio Teal",
  "colours": {
    "accent":      "#d6f54c",
    "accentHover": "#e4ff72",
    "keySolo":     "#d6f54c",
    "hair":        "#20ffffff"
  }
}
```

- A theme only has to name the colours it changes. Whatever it leaves out comes from the theme it
  says it is `basedOn` (one of DLIVE's own), and from Studio Teal after that. "The default with a
  lime accent" is a five-line file. `basedOn` naming anything other than a built-in is ignored, so
  a chain of user themes cannot loop and a theme always stands on the app.
- A colour is `#rrggbb`, or `#aarrggbb` when it is translucent (the hairlines and fills are). Any
  other form is refused with the key named.
- A key this build does not know is kept aside and reported, not fatal, so a theme written for a
  newer DLIVE still opens as far as it goes. A `schema` above what this build knows is refused
  rather than half-read.
- The keys are the `Dine::` token names in `app/ui/AppTheme.h`, listed with a sentence each in
  `ThemeStore::tokens()` (`app/native/ThemeStore.cpp`). The full list, in order:

  `desk window toolbar title menubar sidebar rail pageBar console tile card raised item selected
  control controlHot controlOn sheet popover refuse recGround soloGround editGround` ·
  `hairSoft hair hairStrong edge fill fillHover fillSoft well` ·
  `ink ink2 ink3 ink4 glyph panMark` ·
  `accent accentHover accentDeep onAccent focusRing` ·
  `ok hot warn crit monitor` ·
  `keyMute keySolo keyRec keyMon` ·
  `busDrums busBass busMusic busVocals busSpeech busAmbience busMaster`

## How it is built

The feature is in two halves, and the split is what keeps it honest.

**`app/native/ThemeStore.{h,cpp}` — JUCE-core only, tested without a window.** `Theme` is the
document (name, note, base, a possibly partial map of key → ARGB, and the unknown keys it
carried). `ThemeStore::tokens()` is the table of keys with their group and sentence.
`ThemeStore::builtIn()` holds the five presets; the first is the default and is complete, which
is the floor `ThemeStore::resolve()` fills every other theme down to. `toVar` / `fromVar` /
`load` / `save` are the document; `folder` / `listUser` / `all` / `find` / `saveUser` /
`removeUser` / `importFile` are the folder (`saveUser` refuses a built-in's name and a blank
one, `find` never fails); `chosenTheme` / `setChosenTheme` are the preference, and they leave any
other setting in `preferences.json` alone. `app/Tests/ThemeTests.cpp` covers all of it
(`build/app/dlive_app_tests`).

**`app/ui/AppTheme.{h,cpp}` — the tokens, mutable.** The `Dine::` colours were `inline const`;
they are now `inline` variables whose initial values are the design's, so every page still reads
`Dine::accent` at paint time and nothing about the call sites changed. `Dine::themeBindings()`
is the one table joining a key to its token; `Dine::applyTheme (theme)` resolves the document
and writes every token (the flat design's legacy gradient aliases follow their parents);
`Dine::setThemeColour` moves one; `Dine::currentColours()` reads the tokens back as a full
palette; `Dine::currentThemeName()` says which is in. `Dine::refreshAllWindows()` (or
`refreshWindow (root)` for a window that is on no desktop, which is the snapshot tool's) makes
a change land: it re-applies the look-and-feel's JUCE colour ids (`DineLookAndFeel::applyPalette`),
re-colours each document window's desk, and calls `sendLookAndFeelChange()` on the root, which
reaches every child with `lookAndFeelChanged()` and a repaint - and a repaint is what drops a
strip's cached image. `busTint` reads the seven bus tokens instead of literals.

The rule that makes this work: **nothing captures a token at construction unless it re-reads it
in `lookAndFeelChanged()`.** The places that used to are fixed: `DineButton` and `PanBar` keep
an *optional* tint and read the accent when it is unset; the text editors (the session search,
the input names, the chat box, the theme's own name box) go through `Dine::styleTextEditor`
from their constructor *and* from `lookAndFeelChanged()`; `DineKey` has `setTint` for the same
reason. When adding a widget, do the same or it will draw the old theme after a change.

**`app/ui/ThemeSheet.{h,cpp}` — the Appearance sheet** described above. Selecting a row applies
the theme and persists the choice; a swatch's picker calls `colourEdited` live; Save writes only
the diff against the base. `ThemeSheet (persisting)` is false in the headless snapshot tool so a
render never writes this Mac's preference. `MainView::showThemes` / `applyThemeNamed` /
`setStoredThemeUsed` are the host's side; the View menu's Appearance submenu is built from
`ThemeStore::all()` each time it opens (ids 640+, then 620 Customise, 621 Import, 622 Show
folder), and the stored theme is applied in the `MainView` constructor before a single page
reads a token.

**Verifying.** `build/app/dlive_ui_snapshots <dir>` starts by checking that every theme key has
a `Dine::` binding and vice versa, and that `AppTheme.h`'s initial values equal the Studio Teal
preset exactly (a fresh build and "Studio Teal" must be the same thing); it fails with the key
named if not. The default set ends with `30-theme-<name>.png`, the MIXER under every built-in
theme, and `31-appearance.png`, the sheet. `dlive_ui_snapshots --theme "Lime Desk" <dir>`
renders the *whole* set under one theme, which is how a theme that breaks a page is found before
it reaches a booth. Colour literals still belong in `AppTheme.h` and `ThemeStore.cpp` and nowhere
else; a theme is the one thing allowed to move them.
