#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Widgets.h"

namespace livemix
{

// 48 px top bar: wordmark, module menu, SOURCE / PROFILE dropdowns,
// ORIGINAL | DINE A/B, LIVE SAFE and settings. Live Safe adds a 2 px green
// hairline beneath the bar.
class TopBar : public juce::Component
{
public:
    static constexpr int kHeight = 48;
    static constexpr int kLiveSafeLine = 2;

    explicit TopBar (juce::AudioProcessorValueTreeState&);

    std::function<void (juce::Component&)> onModuleMenu, onSourceMenu, onProfileMenu, onSettingsMenu;
    std::function<void()> onLiveSafeBlocked; // clicked something Live Safe forbids

    // Product shell: module button text ("DRUMS", "FX") and the caption of the first dropdown ("SOURCE", "TYPE").
    void setModuleName (const juce::String& s) { if (moduleButton.getButtonText() != s) { moduleButton.setButtonText (s); resized(); repaint(); } }
    void setSourceCaption (const juce::String& c) { source.setCaption (c); }
    void setSource (const juce::String& s)  { if (source.getValue() != s) { source.setValue (s); resized(); } }
    void setProfile (const juce::String& p) { if (profile.getValue() != p) { profile.setValue (p); resized(); } }
    bool isLiveSafe() const { return liveSafeAtt.get(); }
    bool isOriginal() const { return bypassAtt.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshLiveSafe (bool on);
    void refreshAB (bool original);

    FlatButton moduleButton { "DRUMS", FlatButton::Style::Module };
    DropdownButton source { "SOURCE" }, profile { "PROFILE" };
    FlatButton abOriginal { "ORIGINAL", FlatButton::Style::Segment }, abDine { "DINE", FlatButton::Style::Segment };
    FlatButton liveSafe { "LIVE SAFE", FlatButton::Style::Outline };
    class IconButton : public juce::Button
    {
    public:
        IconButton() : juce::Button ("settings") {}
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto b = getLocalBounds().toFloat();
            LiveMixLookAndFeel::strokeSurface (g, b, over ? Tokens::hairHover : Tokens::hair2, Tokens::Radius::control);
            LiveMixLookAndFeel::drawIcon (g, LiveMixLookAndFeel::Icon::Gear, b.withSizeKeepingCentre (15.0f, 15.0f), over ? Tokens::textHi : Tokens::textMid);
        }
    };
    IconButton settings;
    BoolParamAttachment bypassAtt, liveSafeAtt;
};

// 33 px sub bar: SIMPLE / ADVANCED / KIT tabs, Live Safe note and host diagnostics.
class SubBar : public juce::Component
{
public:
    static constexpr int kHeight = 33;
    enum class View { Simple = 0, Advanced, Kit };

    SubBar();
    std::function<void (View)> onViewChanged;
    std::function<void()> onShowResults;

    void setView (View v);
    View getView() const { return view; }
    void setKitTabVisible (bool on)                       { tabs[2].setVisible (on); resized(); }
    void setLiveSafe (bool on)                            { if (liveSafeOn != on) { liveSafeOn = on; repaint(); } }
    void setDiagnostics (const juce::String& t, bool warn) { if (diag != t || diagWarn != warn) { diag = t; diagWarn = warn; repaint(); } }
    void setResultsAvailable (bool on)                    { results.setVisible (on); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    View view = View::Simple;
    FlatButton tabs[3] { { "SIMPLE", FlatButton::Style::Tab }, { "ADVANCED", FlatButton::Style::Tab }, { "KIT", FlatButton::Style::Tab } };
    FlatButton results { "LAST TUNE", FlatButton::Style::Accent };
    bool liveSafeOn = false, diagWarn = false;
    juce::String diag;
};

} // namespace livemix
