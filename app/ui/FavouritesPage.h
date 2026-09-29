#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "AppServices.h"
#include "AppTheme.h"

namespace livemix
{

// FAVOURITE MIXES (design: `26 - Favourite mixes`, 88:23130).
//
// The mixes this church marked as good, as cards. A card does not show fader positions,
// because a fader at -6 means nothing without knowing what arrived at it: it shows what the
// mix actually *sounded* like - the lead over the band, the lead over the backing voices, the
// speech over the master, the kit against the band, and how loud the master ended up. Those
// are the `MixFingerprint` numbers, measured from a listen, and they are the reason a
// favourite is something a later mix can be aimed at rather than only something recalled.
//
// Two verbs per card, and they are different things:
//   Aim TUNE MIX at this   the next tune fits the levels to these relationships (through the
//                          same ReferenceMix path a record goes through - no second target).
//   Restore mix            put that whole mix back on the console now.
//
// Nothing here changes the sound by itself. Marking the mix that is running is the only way
// one gets into the list, and it is one press at the top right.
class FavouritesPage : public juce::Component
{
public:
    FavouritesPage (MixController&, AppServices&);
    ~FavouritesPage() override;

    std::function<void (const juce::String&)> onToast;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Card;

    void rebuild();
    void markCurrent();

    MixController& controller;
    AppServices& services;

    DineButton markButton { "Mark current mix as favourite", DineButton::Style::Filled };
    juce::Viewport view;
    juce::Component holder;
    std::vector<std::unique_ptr<Card>> cards;
    int seenCount = -1;
    int seenAimed = -2;
    std::unique_ptr<juce::AlertWindow> nameDialog;
};

} // namespace livemix
