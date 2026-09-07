#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace livemix
{

// Design tokens: dark professional-audio ground, steel/ice-blue accent,
// soft Apple-like radii and hairline borders.
namespace Tokens
{
    inline const juce::Colour ground      { 0xff0b0d0f };
    inline const juce::Colour window      { 0xff14171a };
    inline const juce::Colour panel       { 0xff111418 };
    inline const juce::Colour raised      { 0xff171b1f };
    inline const juce::Colour inset       { 0xff0e1013 };
    inline const juce::Colour topBar      { 0xff101316 };
    inline const juce::Colour subBar      { 0xff111417 };
    inline const juce::Colour rowBg       { 0xff15191d };
    inline const juce::Colour menuBg      { 0xff181c20 };
    inline const juce::Colour toastBg     { 0xff181f26 };
    inline const juce::Colour knobBody    { 0xff181c20 };

    inline const juce::Colour hair        { 0xff232930 };
    inline const juce::Colour hair2       { 0xff2a3038 };
    inline const juce::Colour hairStrong  { 0xff2e353d };
    inline const juce::Colour hairRow     { 0xff242a31 };
    inline const juce::Colour hairHover   { 0xff46525f };
    inline const juce::Colour track       { 0xff262c33 };
    inline const juce::Colour grid        { 0xff1b2025 };

    inline const juce::Colour textHi      { 0xffe8ebee };
    inline const juce::Colour textMid     { 0xff9aa3ac };
    inline const juce::Colour textLow     { 0xff5e6771 };
    inline const juce::Colour textDim     { 0xff49525b };
    inline const juce::Colour textGrid    { 0xff4a545e };
    inline const juce::Colour mark        { 0xff39434d };
    inline const juce::Colour markCard    { 0xff4a5866 };

    inline const juce::Colour accent      { 0xff5980a6 };
    inline const juce::Colour accentStroke{ 0xff7fa3c7 };
    inline const juce::Colour accentText  { 0xff9fbedb };
    inline const juce::Colour accentSolid { 0xff46688a };
    inline const juce::Colour accentHover { 0xff55799c };
    inline const juce::Colour accentDim   { 0xff3a4a5c };
    inline const juce::Colour solidText   { 0xfff2f5f8 };

    inline const juce::Colour ok          { 0xff58a876 };
    inline const juce::Colour okText      { 0xff6cc08a };
    inline const juce::Colour okDeep      { 0xff2f6b4c };
    inline const juce::Colour warn        { 0xffd99a4e };
    inline const juce::Colour crit        { 0xffd9534e };
    inline const juce::Colour critText    { 0xffe06661 };
    inline const juce::Colour gateClosed  { 0xff3a424b };
    inline const juce::Colour disabledBg  { 0xff20262c };

    // Continuous-corner radii (pt), tuned for a native macOS control feel.
    namespace Radius
    {
        inline constexpr float control = 6.0f;  // buttons, fields, tiles
        inline constexpr float card    = 8.0f;  // panels, cards, menus
        inline constexpr float chip    = 5.0f;  // small chips / tags
        inline constexpr float pill    = 100.0f; // switches, fully rounded ends
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
