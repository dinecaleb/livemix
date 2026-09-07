#include "ShellBars.h"
#include "State/ParameterIDs.h"

namespace livemix
{

using LNF = LiveMixLookAndFeel;

TopBar::TopBar (juce::AudioProcessorValueTreeState& apvts)
    : bypassAtt (*apvts.getParameter (ParamID::bypass), abOriginal, [this] (bool b) { refreshAB (b); }),
      liveSafeAtt (*apvts.getParameter (ParamID::liveSafe), liveSafe, [this] (bool b) { refreshLiveSafe (b); })
{
    for (auto* c : std::initializer_list<juce::Component*> { &moduleButton, &source, &profile, &abOriginal, &abDine, &liveSafe, &settings })
        addAndMakeVisible (*c);

    moduleButton.onClick = [this] { if (onModuleMenu) onModuleMenu (moduleButton); };
    source.onClick = [this] { if (onSourceMenu) onSourceMenu (source); };
    profile.onClick = [this] { if (onProfileMenu) onProfileMenu (profile); };
    source.setTooltip ("What is plugged into this channel. Loads the Dine starting point for that source and profile.");
    profile.setTooltip ("The sound you are going for. Tune works toward it and the starting points follow it.");

    // A/B: the attachment owns abOriginal's click; DINE simply clears the bypass.
    abDine.onClick = [this] { bypassAtt.set (false); };
    abOriginal.setTooltip ("Hear the channel with Dine switched off, at the same loudness, so you can compare fairly.");
    abDine.setTooltip ("Hear the channel with Dine on.");
    liveSafe.setTooltip ("Switch on for the service. Locks Tune, presets and source/profile changes so nothing jumps mid-song. The knobs still work.");

    settings.onClick = [this] { if (onSettingsMenu) onSettingsMenu (settings); };
    // AI capabilities temporarily disabled — drop "AI assistance" from the tooltip.
    settings.setTooltip ("Presets, group name, AI assist and other settings.");

    refreshAB (bypassAtt.get());
    refreshLiveSafe (liveSafeAtt.get());
}

void TopBar::refreshAB (bool original)
{
    abOriginal.setToggleState (original, juce::dontSendNotification);
    abDine.setToggleState (! original, juce::dontSendNotification);
}

void TopBar::refreshLiveSafe (bool on)
{
    liveSafe.setTrailingIcon (on ? std::optional<LNF::Icon> (LNF::Icon::Check) : std::nullopt);
    if (on) liveSafe.setCustomColours (Tokens::okText, Tokens::ok.withAlpha (0.6f), Tokens::ok.withAlpha (0.1f));
    else    liveSafe.setCustomColours (std::nullopt, std::nullopt, std::nullopt);
    liveSafe.setToggleState (on, juce::dontSendNotification);
    source.setEnabled (! on);
    profile.setEnabled (! on);
    resized();
    repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (kHeight);
    g.setColour (topBar);
    g.fillRect (bar);
    g.setColour (hair);
    g.fillRect (bar.removeFromBottom (1));

    g.setColour (textHi);
    g.setFont (LNF::body (15.0f, 600));
    g.drawText ("Dine", 12, 0, 60, kHeight, juce::Justification::centredLeft);

    // divider after the module button
    g.setColour (hair);
    g.fillRect (moduleButton.getRight() + 12, (kHeight - 22) / 2, 1, 22);

    if (liveSafeAtt.get())
    {
        auto line = juce::Rectangle<int> (0, kHeight, getWidth(), kLiveSafeLine).toFloat();
        juce::ColourGradient grad (ok.withAlpha (0.0f), line.getX(), 0.0f, ok.withAlpha (0.0f), line.getRight(), 0.0f, false);
        grad.addColour (0.25, ok);
        grad.addColour (0.75, ok);
        g.setGradientFill (grad);
        g.fillRect (line);
    }

    // soft frame around the A/B pair
    g.setColour (hair2);
    g.drawRoundedRectangle (abOriginal.getBounds().getUnion (abDine.getBounds()).toFloat().expanded (1.0f), Tokens::Radius::control, 1.0f);
}

void TopBar::resized()
{
    auto b = getLocalBounds().withHeight (kHeight).reduced (12, 0);
    const int cy = kHeight / 2;
    b.removeFromLeft (52); // wordmark
    moduleButton.setBounds (b.removeFromLeft (moduleButton.getIdealWidth()).withSizeKeepingCentre (moduleButton.getIdealWidth(), 26));
    b.removeFromLeft (25); // divider + gaps
    source.setBounds (b.removeFromLeft (source.getIdealWidth()).withSizeKeepingCentre (source.getIdealWidth(), 36));
    b.removeFromLeft (8);
    profile.setBounds (b.removeFromLeft (profile.getIdealWidth()).withSizeKeepingCentre (profile.getIdealWidth(), 36));

    auto right = b;
    settings.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 30));
    right.removeFromRight (8);
    const int lsw = liveSafe.getIdealWidth();
    liveSafe.setBounds (right.removeFromRight (lsw).withSizeKeepingCentre (lsw, 28));
    right.removeFromRight (8);
    const int w2 = abDine.getIdealWidth(), w1 = abOriginal.getIdealWidth();
    auto seg = right.removeFromRight (w1 + w2 + 1).withSizeKeepingCentre (w1 + w2 + 1, 28);
    abOriginal.setBounds (seg.removeFromLeft (w1));
    seg.removeFromLeft (1);
    abDine.setBounds (seg);
    juce::ignoreUnused (cy);
}

// ---------------------------------------------------------------------------
SubBar::SubBar()
{
    for (int i = 0; i < 3; ++i)
    {
        addAndMakeVisible (tabs[i]);
        tabs[i].onClick = [this, i] { setView (View (i)); if (onViewChanged) onViewChanged (View (i)); };
    }
    tabs[0].setTooltip ("Five simple knobs, input health and the chain. This is all most people need.");
    tabs[1].setTooltip ("Every setting, stage by stage, for engineers. Same settings as Simple: switching never changes the sound.");
    tabs[2].setTooltip ("Every Dine channel in this group: tune the whole kit at once.");
    addChildComponent (results);
    results.setFontPx (10.5f);
    results.setPaddingX (10);
    results.onClick = [this] { if (onShowResults) onShowResults(); };
    results.setTooltip ("Show what the last Tune did.");
    setView (View::Simple);
}

void SubBar::setView (View v)
{
    view = v;
    for (int i = 0; i < 3; ++i) tabs[i].setToggleState (i == int (v), juce::dontSendNotification);
}

void SubBar::paint (juce::Graphics& g)
{
    using namespace Tokens;
    auto b = getLocalBounds();
    g.setColour (subBar);
    g.fillRect (b);
    g.setColour (hair);
    g.fillRect (b.removeFromBottom (1));

    auto right = getLocalBounds().reduced (12, 0);
    if (results.isVisible()) right.removeFromRight (results.getWidth() + 12);
    g.setColour (diagWarn ? warn : textLow);
    g.setFont (LNF::mono (10.0f, 400));
    const int dw = int (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), diag)) + 2;
    g.drawText (diag, right.removeFromRight (dw), juce::Justification::centredRight);
    if (liveSafeOn)
    {
        right.removeFromRight (10);
        g.setColour (ok);
        g.setFont (LNF::mono (10.0f, 500));
        g.drawText (juce::String ("settings locked ") + Glyph::dot() + " auto changes off", right, juce::Justification::centredRight);
    }
}

void SubBar::resized()
{
    auto b = getLocalBounds().reduced (12, 0);
    for (auto& t : tabs) { if (! t.isVisible()) continue; t.setBounds (b.removeFromLeft (t.getIdealWidth()).withHeight (kHeight)); b.removeFromLeft (2); }
    results.setBounds (getLocalBounds().reduced (12, 0).removeFromRight (results.getIdealWidth()).withSizeKeepingCentre (results.getIdealWidth(), 22));
}

} // namespace livemix
