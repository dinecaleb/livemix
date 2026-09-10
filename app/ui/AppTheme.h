#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"
#include "UI/LiveMixLookAndFeel.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// DLIVE v2 look: native macOS, dark. Materials are flat (desk / window / card /
// sidebar), edges are half-pixel hairlines, radii are 5-11, controls are 24-26 px
// and type is the system face at Mac sizes. Letterspaced caps survive only in the
// product verbs (TUNE MIX, RE-TUNE, KEEP, REVERT, BEFORE, AFTER).
// These tokens belong to the application; the plug-in keeps `Tokens` in src/UI.
// ---------------------------------------------------------------------------
namespace Dine
{
    // Materials
    inline const juce::Colour desk        { 0xff101113 };   // behind the window / dim
    inline const juce::Colour window      { 0xff1b1c1e };   // window content
    inline const juce::Colour card        { 0xff232528 };   // card / grouped box
    inline const juce::Colour sidebar     { 0xff26282c };   // sidebar vibrancy
    inline const juce::Colour toolbar     { 0xff1e1f22 };   // unified toolbar
    inline const juce::Colour sheet       { 0xff2c2e32 };   // sheet material
    inline const juce::Colour popover     { 0xff34363a };   // menus, HUD
    inline const juce::Colour rail        { 0xff1f2023 };   // inspector rail beside the mix

    // Edges and fills (the design's rgba(255,255,255,x) over these grounds)
    inline const juce::Colour hair        { 0x1affffff };
    inline const juce::Colour hairSoft    { 0x0dffffff };
    inline const juce::Colour hairStrong  { 0x26ffffff };
    inline const juce::Colour fill        { 0x1affffff };   // control background
    inline const juce::Colour fillHover   { 0x28ffffff };
    inline const juce::Colour fillSoft    { 0x12ffffff };
    inline const juce::Colour well        { 0x66000000 };   // slider tracks, meters, search fields

    // Ink
    inline const juce::Colour ink         { 0xfff2f3f5 };
    inline const juce::Colour ink2        { 0xffa0a5ad };
    inline const juce::Colour ink3        { 0xff7e838c };
    inline const juce::Colour ink4        { 0xff5b606a };
    inline const juce::Colour glyph       { 0xff8b9099 };   // resting icon

    // Roles
    inline const juce::Colour accent      { 0xff4db8a4 };
    inline const juce::Colour accentDeep  { 0xff2f8d7a };
    inline const juce::Colour accentTop   { 0xff3aa08c };   // filled button gradient
    inline const juce::Colour accentBottom{ 0xff2f8d7a };
    inline const juce::Colour ok          { 0xff4cc98a };
    inline const juce::Colour warn        { 0xfff0a33f };
    inline const juce::Colour crit        { 0xffe5645e };
    inline const juce::Colour onAccent    { 0xffffffff };

    // The console keys keep their own colours, so a key says which key it is before it says
    // it is on: mute amber, solo teal, record red, monitoring blue.
    inline const juce::Colour keyMute     { 0xfff0a33f };
    inline const juce::Colour keySolo     { 0xff4db8a4 };
    inline const juce::Colour keyRec      { 0xffe5645e };
    inline const juce::Colour keyMon      { 0xff6ea8e0 };

    namespace Radius
    {
        inline constexpr float window  = 11.0f;
        inline constexpr float card    = 9.0f;
        inline constexpr float control = 6.0f;
        inline constexpr float chip    = 5.0f;
        inline constexpr float pill    = 11.0f;
    }

    namespace Metric
    {
        inline constexpr int sidebar   = 224;
        inline constexpr int toolbar   = 56;
        inline constexpr int footer    = 52;
        inline constexpr int rail      = 274;   // the mix inspector
        inline constexpr int padX      = 30;    // page gutter
        inline constexpr int padY      = 26;
        inline constexpr int control   = 24;    // popups, rows, segments
        inline constexpr int button    = 26;    // default and filled buttons
        inline constexpr int panelTab  = 15;    // the gutter a collapsible side panel is opened by
    }

    // System type: SF Pro at Mac sizes, SF Mono for every number.
    juce::Font text (float px, int weight = 400);
    juce::Font mono (float px, int weight = 400);

    // Text measurement with a little slack, so drawText never ellipsises a label
    // that was measured to fit.
    int textWidth (const juce::Font&, const juce::String&);

    // Surfaces -------------------------------------------------------------
    void fillRounded (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
    void hairlineRounded (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
    void drawCard (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill = card, juce::Colour edge = hair);
    void drawWell (juce::Graphics&, juce::Rectangle<float>, float radius);
    void drawFilled (juce::Graphics&, juce::Rectangle<float>, float radius, bool hover, bool down);
    void drawStandard (juce::Graphics&, juce::Rectangle<float>, float radius, bool hover, bool down);
    void drawSheet (juce::Graphics&, juce::Rectangle<float>, float radius);
    void drawRule (juce::Graphics&, juce::Rectangle<int>, juce::Colour = hair);   // .5 px separator

    // Icons ----------------------------------------------------------------
    enum class Icon
    {
        None, Drum, Cymbal, Mic, Guitar, Piano, Speech, Room, Fx, Waveform, Sliders,
        Device, List, Target, Check, Warn, Gear, Play, Refresh, Chevron, UpDown, Dash, Bus, Sidebar
    };
    void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour, float thickness = 1.4f);
    Icon iconForRole (ChannelRole) noexcept;

    // What a source is drawn as. The role picks it, which is right almost always; a session
    // may override one input with a key from iconChoices() for the times it is not - a pad
    // playing backing tracks, a spare DI carrying talkback. The key is what gets saved, so
    // it stays stable while the enum is free to grow.
    struct IconChoice { const char* key; const char* label; Icon icon; };
    const std::vector<IconChoice>& iconChoices();
    Icon iconFor (const std::string& key, ChannelRole fallback) noexcept;   // "" = from the role

    // Sources ---------------------------------------------------------------
    // The sources a volunteer can pick, grouped the way the mix is built, in plain words.
    // Shared by the ASSIGN page and the TRACKS header menu, so a source is offered and
    // named the same wherever it is set or corrected.
    struct RoleGroup { const char* name; std::vector<ChannelRole> roles; };
    const std::vector<RoleGroup>& roleGroups();
    juce::String friendlyRoleName (ChannelRole);

    // Status pill: "Heard", "Faint", "Muted", "Ready".
    float pillWidth (const juce::String& text, bool withIcon);
    void drawPill (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour, Icon icon = Icon::None);

    // Level colour on the design's scale: green to -6, amber to -1, red above.
    juce::Colour levelColour (float db) noexcept;
}

// A macOS push button in the three shapes the design uses.
class DineButton : public juce::Button
{
public:
    enum class Style { Filled, Standard, Ghost, Segment };

    DineButton (const juce::String& text, Style s = Style::Standard);

    void setStyle (Style s)                 { style = s; repaint(); }
    void setIcon (Dine::Icon i)             { icon = i; repaint(); }
    void setFontPx (float px)               { fontPx = px; repaint(); }
    void setPadX (int px)                   { padX = px; }
    void setCaps (bool on)                  { caps = on; repaint(); }   // the product verbs only
    int idealWidth() const;

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Style style;
    Dine::Icon icon = Dine::Icon::None;
    float fontPx = 13.0f;
    int padX = 13;
    bool caps = false;
};

// NSPopUpButton: a value and the up/down chevrons, 24 px.
class DinePopup : public juce::Button
{
public:
    DinePopup();
    void setValue (const juce::String& v) { if (v != value) { value = v; repaint(); } }
    const juce::String& getValue() const  { return value; }
    int idealWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String value;
};

// Sidebar source-list row: icon, label, trailing meta or check.
class DineNavItem : public juce::Button
{
public:
    DineNavItem (const juce::String& label, Dine::Icon);
    void setMeta (const juce::String& m)  { if (m != meta) { meta = m; repaint(); } }
    void setDone (bool d)                 { if (d != done) { done = d; repaint(); } }
    void setSelected (bool s)             { if (s != selected) { selected = s; repaint(); } }
    bool isSelected() const noexcept      { return selected; }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String label, meta;
    Dine::Icon icon;
    bool selected = false, done = false;
};

// The handle a collapsible side panel is opened and closed by.
//
// It lives in a slim gutter on the panel's inner edge, so it never covers what the panel
// holds, and a collapsed panel leaves that same gutter behind with its name written down
// it: the middle of the workspace takes the width, and the panel is always one click away.
// The same handle is used on every side panel, so the gesture is learned once.
class DinePanelTab : public juce::Button
{
public:
    enum class Side { Left, Right };      // which edge of the workspace the panel sits on

    DinePanelTab (Side, const juce::String& panelName);

    void setCollapsed (bool);
    bool isCollapsed() const noexcept { return collapsed; }

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    void updateTooltip();

    Side side;
    juce::String name;
    bool collapsed = false;
};

// A console key: M, S, R, A. Quiet when it is off, unmistakable when it is on - filled
// with its own colour and a dark letter, so a muted strip reads as muted from across the
// room. The mixer and the timeline use the same key, so a mute looks the same everywhere.
class DineKey : public juce::Button
{
public:
    DineKey (const juce::String& letter, juce::Colour onColour);

    void setOn (bool);
    bool isOn() const noexcept { return on; }
    void setLetter (const juce::String&);

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String letter;
    juce::Colour tint;
    bool on = false;
};

// The 28x16 switch used for stereo pairs.
class DineSwitch : public juce::Button
{
public:
    explicit DineSwitch (const juce::String& onText, const juce::String& offText);
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String onText, offText;
};

// A small balance control: drag left / right, double-click to centre. Small enough to
// live on one line of a strip or an Inspector row, and it reads as "centre" at a glance.
class PanBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (float)> onChange;

    void setValue (float v)       { if (std::fabs (v - value) > 0.0005f) { value = v; repaint(); } }
    float getValue() const noexcept { return value; }
    void setTint (juce::Colour c) { tint = c; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void drag (const juce::MouseEvent&);

    float value = 0.0f;
    juce::Colour tint { Dine::accent };
};

// A level meter in the design's grammar: a dark well, 3 px segments in the vertical
// strips, one continuous 4 px bar in lists, and a peak-hold line. Green to -6, amber
// to -1, red above; the dB readout always sits beside it.
class DineMeter : public juce::Component
{
public:
    enum class Style { Segments, Bar };
    explicit DineMeter (Style s = Style::Segments) : style (s) { setInterceptsMouseClicks (false, false); }

    // Meter ballistics: the level falls back smoothly instead of dropping to silence
    // the moment a block is missed, so a paused UI never reads as "no signal".
    void setLevels (float peakDb, float holdDb, bool clipped);
    float getPeakDb() const noexcept { return peak; }

    // A muted channel still has signal arriving; it just is not heard. The meter keeps
    // reading it, in grey, so "there is nothing there" and "it is muted" never look alike.
    void setMuted (bool);
    void setStyle (Style s) { style = s; repaint(); }
    void paint (juce::Graphics&) override;

    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (juce::jmin (0.0f, db) + 60.0f) / 60.0f); }

private:
    Style style;
    float peak = -120.0f, hold = -120.0f;
    bool clipped = false, muted = false;
};

// The application's look and feel: LiveMixLookAndFeel with the v2 materials, Mac
// sliders (4 px track, round knob, centre detent), system menus and a HUD tooltip.
class DineLookAndFeel : public LiveMixLookAndFeel
{
public:
    DineLookAndFeel();

    // A slider whose fill grows from the centre ("as tuned" in the middle).
    static void setBipolar (juce::Slider&, bool);

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 8; }

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
};

} // namespace livemix
