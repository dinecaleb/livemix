# DINE identity (v1)

Source: the Claude Design project `6134b080-3212-4c44-abaa-4f53377f10c1`, file `DINE Brand.dc.html`
(read with DesignSync after `/design-login`). This file is what the app takes from it.

## The mark

- **Symbol** - a D cut in two. The stem is the broadcast, what everyone hears; the bowl is the
  monitor, what only you hear. Side by side, never touching.
- **Construction** - one unit is the stem: stem 1 unit wide (24 of 96), split 0.4 unit (10),
  bowl 2 units wide with a full radius (48), height 4 units (96). Stem corners 3.
- **Wordmark** - drawn, not typed: D is the symbol, then I, N (two stems and the stroke between),
  E (a stem and three bars: 64, 56, 64 wide, 20 tall). 312 x 96, 20 between letters.
- **Clear space** one stem on every side. **Minimum** symbol 12 px, wordmark 64 px wide.
- **Colour** - black and white only: Black #000000, Graphite #1C1C1E, Steel #8E8E93 (text only),
  Paper #F5F5F7. The mark never takes a colour - DINE's teal stays the app's "on" colour and the
  meters carry colour; the mark does not.

## In the app

| Where | What | Code |
| --- | --- | --- |
| Dock, Finder | The graphite tile with the symbol in Paper, on Apple's 1024 grid | `app/resources/AppIcon.png`, made by `scripts/brand-icon.py` |
| DINE > About DINE | The identity's splash: black card, wordmark, "The DAW built for live broadcast", version | `app/ui/AboutSheet.*` |
| Sessions, first launch | Wordmark and the line, where the product introduces itself | `SessionsPage::paint` |
| Drawing | `Dine::drawSymbol` / `Dine::drawWordmark`, exact paths on the grid above; `Dine::brand*` colours | `app/ui/AppTheme.*` |

Not used: a launch splash (it would make every start slower - the About sheet carries that
composition instead), and the mark on working screens - the symbol is for where space is
tight, the wordmark for where the product is introduced. After changing the icon PNG, delete
`build/app/DineApp_artefacts/JuceLibraryCode/Icon.icns` so JUCE regenerates the bundle's icon.
