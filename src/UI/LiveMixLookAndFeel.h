#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace livemix
{

// Design tokens: studio charcoal with soft jade accent — calm, premium, Apple-native
// radii. Intentionally not ice-blue-on-black (generic pro-audio / AI default).
namespace Tokens
{
    inline const juce::Colour ground      { 0xff070809 };
    inline const juce::Colour window      { 0xff0e1014 };
    inline const juce::Colour panel       { 0xff13161c };
    inline const juce::Colour raised      { 0xff1a1e26 };
    inline const juce::Colour inset       { 0xff0a0c10 };
    inline const juce::Colour topBar      { 0xff0c0e12 };
    inline const juce::Colour subBar      { 0xff10131a };
    inline const juce::Colour rowBg       { 0xff161a22 };
    inline const juce::Colour menuBg      { 0xff1c212b };
    inline const juce::Colour toastBg     { 0xff1a2228 };
    inline const juce::Colour knobBody    { 0xff1a1e26 };

    inline const juce::Colour hair        { 0xff2a303a };
    inline const juce::Colour hair2       { 0xff343b48 };
    inline const juce::Colour hairStrong  { 0xff3e4656 };
    inline const juce::Colour hairRow     { 0xff2c3340 };
    inline const juce::Colour hairHover   { 0xff556070 };
    inline const juce::Colour track       { 0xff2a303a };
    inline const juce::Colour grid        { 0xff181c24 };

    inline const juce::Colour textHi      { 0xfff4f5f7 };
    inline const juce::Colour textMid     { 0xffa8b0bc };
    inline const juce::Colour textLow     { 0xff6b7380 };
    inline const juce::Colour textDim     { 0xff4e5664 };
    inline const juce::Colour textGrid    { 0xff555e6c };
    inline const juce::Colour mark        { 0xff3e4654 };
    inline const juce::Colour markCard    { 0xff505a6a };

    inline const juce::Colour accent      { 0xff6db8a8 };
    inline const juce::Colour accentStroke{ 0xff8ed0c2 };
    inline const juce::Colour accentText  { 0xffa8ddd2 };
    inline const juce::Colour accentSolid { 0xff3d8f7f };
    inline const juce::Colour accentHover { 0xff4aa090 };
    inline const juce::Colour accentDim   { 0xff2a4a44 };
    inline const juce::Colour solidText   { 0xfff7faf9 };

    inline const juce::Colour ok          { 0xff5cb88a };
    inline const juce::Colour okText      { 0xff74d0a0 };
    inline const juce::Colour okDeep      { 0xff2f6b4c };
    inline const juce::Colour warn        { 0xffe0a85c };
    inline const juce::Colour crit        { 0xffe06a64 };
    inline const juce::Colour critText    { 0xffef8a84 };
    inline const juce::Colour gateClosed  { 0xff3a424b };
    inline const juce::Colour disabledBg  { 0xff222830 };

    // Soft continuous corners — closer to macOS 14+ controls than sharp pro-audio.
    namespace Radius
    {
        inline constexpr float control = 8.0f;
        inline constexpr float card    = 14.0f;
        inline constexpr float chip    = 6.0f;
        inline constexpr float pill    = 100.0f;
    }
}

// Look and feel for every LiveMix editor. Fonts are embedded (assets/fonts):
// Barlow for UI labels and body (SF-like proportions), IBM Plex Mono for values.
class LiveMixLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LiveMixLookAndFeel();

    // Font helpers. `px` is the design pixel size; `spacingEm` is letter-spacing
    // (soft-capped — heavy tracking is intentionally avoided). Weights: 400/500/600/700.
    static juce::Font condensed (float px, int weight = 600, float spacingEm = 0.0f);
    static juce::Font body (float px, int weight = 400, float spacingEm = 0.0f);
    static juce::Font mono (float px, int weight = 400, float spacingEm = 0.0f);

    // Shared surface drawing (fill + optional 1px stroke) with continuous corners.
    static void fillSurface (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour fill, float radius);
    static void strokeSurface (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour stroke, float radius, float thickness = 1.0f);
    static void drawSurface (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour fill, juce::Colour stroke, float radius);
    // Soft vertical wash + corner vignette — atmosphere for full-window grounds.
    static void drawAmbient (juce::Graphics&, juce::Rectangle<float> bounds);
    // Raised material: fill, hairline, soft top sheen.
    static void drawElevated (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour fill, juce::Colour stroke, float radius);

    // Legacy colour accessors kept for the few call sites that still use them.
    static juce::Colour accent()  { return Tokens::accent; }
    static juce::Colour green()   { return Tokens::ok; }
    static juce::Colour warn()    { return Tokens::warn; }
    static juce::Colour danger()  { return Tokens::crit; }
    static juce::Colour good()    { return Tokens::ok; }
    static juce::Colour muted()   { return Tokens::textMid; }
    static juce::Colour text()    { return Tokens::textHi; }
    static juce::Colour window()  { return Tokens::window; }
    static juce::Colour panel()   { return Tokens::panel; }
    static juce::Colour panelHi() { return Tokens::raised; }
    static juce::Colour inset()   { return Tokens::inset; }

    // Shared drawing helpers
    enum class Icon { Check, Warning, Bang, Up, Down, ArrowRight, ChevronDown, Gear, Dash, Polarity };
    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> bounds, juce::Colour);
    static void drawChip (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& text,
                          juce::Colour fg, juce::Colour border, juce::Colour bg, float fontPx = 10.0f,
                          Icon* icon = nullptr);
    static float chipWidth (const juce::String& text, float fontPx = 10.0f, bool withIcon = false);
    static void drawCornerMarks (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour, float inset = 8.0f);
    static void drawHairline (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour);

    // Formats a value in the design's mono style: "+1.5 dB", "45 ms", "18.0 kHz", "3.2:1", "45 %".
    static juce::String formatValue (float value, const juce::String& unit, float minValue, float maxValue);

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 6; }
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawGroupComponentOutline (juce::Graphics&, int w, int h, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent&) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String& text) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override { return 28; }
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override;

private:
    // Embedded typefaces, shared by every LookAndFeel instance and released with
    // the last one (i.e. before JUCE shuts down). A function-local static would
    // outlive JUCE's own globals and crash on host quit / plugin unload.
    struct TypefaceCache { juce::Typeface::Ptr faces[3][3]; };
    juce::SharedResourcePointer<TypefaceCache> typefaces;
    static juce::Typeface::Ptr typefaceFor (int family, int weight);
};

} // namespace livemix
