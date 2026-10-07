#include "WorkspaceGuide.h"

namespace livemix
{

namespace
{
    // One card per workspace, in the order MainView::Page has them. Two sentences: what this
    // workspace is for, and the one thing to press. Plain words - somebody reading this may be
    // on their second shift and twenty minutes from a service.
    const WorkspaceGuide::Entry kEntries[] =
    {
        { "tracks", "Everything gets recorded, on its own track.",
          "Press the red button and every input is kept separately, so it can all be mixed again "
          "afterwards. Drag a track's bottom edge to make it taller; click its name to read its chain "
          "along the foot." },
        { "mixer", "The console, if you want it.",
          "Faders, mutes and meters. M is mute, S is solo, R sets a track to record - and solo only goes "
          "to your own headphones, never to the room or the stream." },
        { "tune", "TUNE MIX listens, then does the mix.",
          "DINE hears the whole band for thirty seconds and sets every level, tone and effect for this "
          "room, then tells you what it did and why. Nothing changes until you press KEEP." },
        { "live", "Everything live, on one screen.",
          "What is recording, what is going out, and one fader per group. Scenes keep a whole mix under "
          "a name, and LIVE SAFE locks the things that could go wrong by accident." },
        { "inspector", "One channel, in full detail.",
          "Every stage of a channel, what it is set to, and whether DINE set it or you did. Change "
          "anything you like - the column at the right remembers it, and puts it back in one press." },
    };

    int indexForPage (int page) noexcept
    {
        // MainView::Page: Sessions, Device, Assign, Purpose, Tracks, Mixer, Tune, Live, Inspector, ...
        switch (page)
        {
            case 4: return 0;   // Tracks
            case 5: return 1;   // Mixer
            case 6: return 2;   // Tune
            case 7: return 3;   // Live
            case 8: return 4;   // Inspector
            default: return -1; // the set-up pages explain themselves in their own words
        }
    }
}

const WorkspaceGuide::Entry* WorkspaceGuide::entryFor (int page)
{
    const int i = indexForPage (page);
    return i < 0 ? nullptr : &kEntries[i];
}

WorkspaceGuide::WorkspaceGuide (const Entry& entry) : title (entry.title), body (entry.body)
{
    gotIt.setFontPx (12.0f);
    gotIt.setCaps (true);
    gotIt.setTooltip ("Dismiss this one. It will not come back.");
    gotIt.onClick = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible (gotIt);

    noMore.setFontPx (12.0f);
    noMore.setTooltip ("Switch off every workspace's guide. Help > Show the guides again brings them back.");
    noMore.onClick = [this] { if (onTurnOff) onTurnOff(); };
    addAndMakeVisible (noMore);

    // A card, not a modal: what is behind it keeps working while it is read.
    setInterceptsMouseClicks (false, true);
}

int WorkspaceGuide::wantedHeight() const
{
    const auto font = Dine::text (12.5f);
    const float w = juce::GlyphArrangement::getStringWidth (font, body);
    const int lines = juce::jlimit (2, 6, int (std::ceil (w / float (width - 2 * pad))) + 1);
    return pad + 18 + 6 + lines * 17 + 14 + Dine::Metric::button + pad;
}

void WorkspaceGuide::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // Lifted off the workspace, with the accent hairline that says DINE is the one talking.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), Dine::Radius::card);
    Dine::fillRounded (g, r, Dine::card, Dine::Radius::card);
    Dine::hairlineRounded (g, r, Dine::accent.withAlpha (0.45f), Dine::Radius::card);

    auto inner = getLocalBounds().reduced (pad, pad);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (13.5f, 600));
    Dine::drawText (g, title, inner.removeFromTop (18), juce::Justification::centredLeft, true);
    inner.removeFromTop (6);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.5f));
    Dine::drawFittedText (g, body, inner.removeFromTop (inner.getHeight() - Dine::Metric::button - 14),
                          juce::Justification::topLeft, 6, 1.0f);
}

void WorkspaceGuide::resized()
{
    auto foot = getLocalBounds().reduced (pad, pad).removeFromBottom (Dine::Metric::button);
    const int gw = juce::jmax (72, gotIt.idealWidth());
    gotIt.setBounds (foot.removeFromLeft (gw));
    foot.removeFromLeft (8);
    noMore.setBounds (foot.removeFromLeft (juce::jmax (80, noMore.idealWidth())));
}

} // namespace livemix
