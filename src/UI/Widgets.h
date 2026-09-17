#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include "LiveMixLookAndFeel.h"
#include "State/ParameterSpecs.h"

namespace livemix
{

// UTF-8 literals used across the UI (Barlow / Plex Mono both carry these glyphs).
namespace Glyph
{
    inline juce::String dot()    { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")); }       // ·
    inline juce::String minus()  { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }   // −
    inline juce::String dash()   { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")); }   // —
    inline juce::String approx() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x89\x88")); }   // ≈
    inline juce::String ellip()  { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")); }   // …
    inline juce::String check()  { return juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")); }   // ✓
}

// Soft-corner, hairline-bordered button in the design's styles.
class FlatButton : public juce::Button
{
public:
    enum class Style
    {
        Outline,   // hairline border, mid text
        Solid,     // accent-filled, light text (primary actions)
        Ghost,     // no border
        Accent,    // accent border + accent text
        Tab,       // sub-bar tab: underline when toggled on
        Segment,   // A/B segment: tinted when toggled on
        Module     // top-bar module menu button
    };

    FlatButton (const juce::String& text, Style style);

    void setStyle (Style s)              { style = s; repaint(); }
    void setFontPx (float px)            { fontPx = px; }
    void setSpacing (float em)           { spacingEm = em; }
    void setPaddingX (int px)            { padX = px; }
    void setDot (bool show, juce::Colour colour = Tokens::mark) { hasDot = show; dotColour = colour; repaint(); }
    void setChevron (bool show)          { chevron = show; }
    void setTrailingIcon (std::optional<LiveMixLookAndFeel::Icon> icon) { trailingIcon = icon; repaint(); }
    void setCustomColours (std::optional<juce::Colour> fg, std::optional<juce::Colour> border, std::optional<juce::Colour> bg);
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Style style;
    float fontPx = 12.0f, spacingEm = 0.0f;
    int padX = 12;
    bool hasDot = false, chevron = false;
    juce::Colour dotColour;
    std::optional<LiveMixLookAndFeel::Icon> trailingIcon;
    std::optional<juce::Colour> customFg, customBorder, customBg;
};

// Pop-up button: optional caption over the value, chevron on the right (NSPopUpButton-ish).
class DropdownButton : public juce::Button
{
public:
    DropdownButton (const juce::String& caption);
    void setValue (const juce::String& v) { if (value != v) { value = v; repaint(); } }
    void setCaption (const juce::String& c) { if (caption != c) { caption = c; repaint(); } }
    const juce::String& getValue() const { return value; }
    int getIdealWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String caption, value;
};

// Advanced-mode value tile: label / mono value / drag bar. Float parameters
// drag vertically, bools toggle on click, choices open a menu.
class ParamTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamTile (juce::RangedAudioParameter& param, const ParameterSpec& spec, const juce::String& label);
    ~ParamTile() override;

    static constexpr int kWidth = 100, kHeight = 58;

    juce::String getParamId() const { return spec.id; } // by value: spec.id is a std::string
    void setLabel (const juce::String& l) { label = l; repaint(); }
    static juce::String format (const ParameterSpec& spec, float value);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

private:
    juce::RangedAudioParameter& param;
    const ParameterSpec spec;
    juce::String label;
    juce::ParameterAttachment attachment;
    float current = 0.0f;
    float dragStartNorm = 0.0f;
    bool dragging = false, hover = false;
};

// Simple-mode macro knob with label and mono value beneath.
class MacroKnob : public juce::Component
{
public:
    MacroKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name);
    static constexpr int kWidth = 100, kHeight = 84 + 7 + 14 + 14;
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider& getSlider() { return slider; }

private:
    juce::String name;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool percentOfUnit = false;
};

// "GATE threshold −34 dB · range −16 dB · hold 45 ms" / "EQ 290 Hz · −3.5 dB · Q 2.4":
// the mono value line for a recommendation's parameter changes.
struct Recommendation;
juce::String formatRecommendationValues (const Recommendation& r);

// Attaches a bool parameter to any juce::Button (toggle semantics) without the
// button needing to be a ToggleButton.
class BoolParamAttachment
{
public:
    BoolParamAttachment (juce::RangedAudioParameter& param, juce::Button& button, std::function<void (bool)> onChange = nullptr);
    bool get() const { return state; }
    void set (bool on);
private:
    juce::RangedAudioParameter& param;
    juce::Button& button;
    juce::ParameterAttachment attachment;
    std::function<void (bool)> onChange;
    bool state = false;
};

} // namespace livemix
