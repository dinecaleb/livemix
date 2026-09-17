#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"
#include "Mix/MixSession.h"
#include "UI/LiveMixLookAndFeel.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// The DLIVE look, taken from the marketing site (dlive-audio.dinecaleb.chatgpt.site).
//
// The site's ground is a true black (#080909) and its depth comes from black on black:
// #0d0f0d for the chrome, #101310 for a panel, #151815 for a tile inside one, with
// hairlines at a tenth of white and one electric lime (#c8ff3d) that is spent only on
// what matters - the primary action, the active workspace, what is selected, what is
// soloed into the engineer's headphones, what DLIVE tuned, and the signal itself.
// Nothing else in the application is allowed to be that colour.
//
// The application is denser than the site on purpose: a page is read from a booth
// during a service, not from a sofa, so the gutters are tighter, the controls are
// 24-28 px, every number is tabular and the type carries the site's tracking rather
// than its size. Letterspaced caps survive only in the product verbs (TUNE MIX,
// RE-TUNE, KEEP, REVERT, BEFORE, AFTER, BYPASS, LIVE SAFE).
//
// These tokens belong to the application; the plug-in keeps `Tokens` in src/UI.
// ---------------------------------------------------------------------------
namespace Dine
{
    // Materials. The ramp is deliberately shallow - eight levels of value between the
    // canvas and an elevated tile - because the hierarchy is carried by hairlines,
    // alignment and type, not by grey boxes stacked on each other.
    inline const juce::Colour desk        { 0xff050605 };   // behind the window / dim
    inline const juce::Colour window      { 0xff080909 };   // the brand black: the workspace ground
    inline const juce::Colour toolbar     { 0xff0a0c0a };   // unified toolbar
    inline const juce::Colour sidebar     { 0xff0b0d0b };   // sidebar - under the content, never over it
    inline const juce::Colour rail        { 0xff0a0c0a };   // a panel at the edge of a workspace
    inline const juce::Colour console     { 0xff0d0f0d };   // a console column, a timeline header
    inline const juce::Colour card        { 0xff101310 };   // panel / strip / grouped box
    inline const juce::Colour raised      { 0xff151815 };   // a tile inside a card (a readout, a well)
    inline const juce::Colour selected    { 0xff20231f };   // the row or segment that is chosen
    inline const juce::Colour sheet       { 0xff121512 };   // sheet material
    inline const juce::Colour popover     { 0xff171916 };   // menus, HUD

    // Edges and fills. Translucent white, so one hairline reads the same over every
    // material above: 0.08 / 0.12 / 0.20 / 0.30 of white is the site's #1b1e1b,
    // #252925, #3b3f3a and #50554f over the brand black.
    inline const juce::Colour hairSoft    { 0x14ffffff };
    inline const juce::Colour hair        { 0x1fffffff };
    inline const juce::Colour hairStrong  { 0x33ffffff };
    inline const juce::Colour edge        { 0x4dffffff };   // a product frame: a sheet, a window
    inline const juce::Colour fill        { 0x12ffffff };   // control background
    inline const juce::Colour fillHover   { 0x1effffff };
    inline const juce::Colour fillSoft    { 0x0bffffff };
    inline const juce::Colour well        { 0x12ffffff };   // slider slots, meters, fields: cut, and lighter

    // Ink. The site's neutrals are faintly green rather than blue, which is most of
    // what keeps a black interface from reading as the usual charcoal DAW.
    inline const juce::Colour ink         { 0xfff3f4ef };
    inline const juce::Colour ink2        { 0xffb4b9b1 };
    inline const juce::Colour ink3        { 0xff868c84 };
    inline const juce::Colour ink4        { 0xff5c625b };
    inline const juce::Colour glyph       { 0xff8b9189 };   // resting icon

    // Roles. The lime is the signal; it is never chrome.
    inline const juce::Colour accent      { 0xffc8ff3d };
    inline const juce::Colour accentDeep  { 0xffa4d62f };   // pressed
    inline const juce::Colour accentTop   { 0xffc8ff3d };   // filled buttons are flat: the site has no gradients
    inline const juce::Colour accentBottom{ 0xffc8ff3d };
    inline const juce::Colour onAccent    { 0xff0a0c09 };   // a lime button carries near-black type
    inline const juce::Colour ok          { 0xff57b98d };   // heard, healthy, done - green, so the lime stays rare
    inline const juce::Colour hot         { 0xffe1d54b };   // the meter's middle band
    inline const juce::Colour warn        { 0xffefaa52 };
    inline const juce::Colour crit        { 0xfff0655d };

    // The console keys keep their own colours, so a key says which key it is before it says
    // it is on: mute amber, solo lime (it is the one thing going to the engineer's own ears),
    // record red, monitoring blue.
    inline const juce::Colour keyMute     { 0xffefaa52 };
    inline const juce::Colour keySolo     { 0xffc8ff3d };
    inline const juce::Colour keyRec      { 0xfff0655d };
    inline const juce::Colour keyMon      { 0xff6eafff };

    // Focus and disablement, named once so every control agrees.
    inline const juce::Colour focusRing   { 0xffc8ff3d };
    inline constexpr float    disabled    = 0.38f;

    // Corners. The site frames its product at 13-20 px and everything inside it at 4-8:
    // the application only ever draws the inside, so it stays in the small half.
    namespace Radius
    {
        inline constexpr float window  = 10.0f;
        inline constexpr float card    = 8.0f;
        inline constexpr float control = 6.0f;
        inline constexpr float chip    = 4.0f;
        inline constexpr float pill    = 5.0f;
    }

    namespace Metric
    {
        inline constexpr int sidebar   = 224;
        inline constexpr int toolbar   = 56;
        inline constexpr int footer    = 52;
        inline constexpr int rail      = 274;   // the mix inspector
        inline constexpr int setupRail = 252;   // the column of small cards beside a setup page
        inline constexpr int padX      = 24;    // page gutter - tighter than the site: this is a workstation
        inline constexpr int padY      = 20;
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
        Device, List, Target, Check, Warn, Gear, Play, Refresh, Chevron, UpDown, Dash, Bus, Sidebar, Search
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

    // The one way a row in a table or a list says it is the chosen one: a lit plane and a
    // lime marker on its leading edge, never a bar of colour with the text knocked out of
    // it. A selected row still has to be read - it is data, not a button.
    void drawSelectedRow (juce::Graphics&, juce::Rectangle<int>);

    // A section caption over a card or a list: 11 px, 600, quiet. Every setup page's
    // right-hand column and every grouped box is introduced by one of these.
    void drawCaption (juce::Graphics&, juce::Rectangle<int>, const juce::String&);

    // The macOS radio a picker row or a picker card carries, filled with the accent when it
    // is the one that is chosen. Used by the device list, the output list and the purpose
    // and sound cards, so "this is the one" reads the same in all three.
    void drawRadio (juce::Graphics&, juce::Rectangle<float>, bool on);

    // One bar, divided by group: how the inputs, or a saved session's inputs, fall across
    // DRUMS / BASS / MUSIC / VOCALS / SPEECH. The segments keep their bus colours, so the
    // bar says the same thing the mixer's bands say.
    struct BarSlice { float share; juce::Colour colour; };   // share 0..1 of the whole bar
    void drawStackedBar (juce::Graphics&, juce::Rectangle<int>, const std::vector<BarSlice>&);

    // Status pill: "Heard", "Faint", "Muted", "Ready".
    float pillWidth (const juce::String& text, bool withIcon);
    void drawPill (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour, Icon icon = Icon::None);

    // Level colour on the design's scale: green to -6, amber to -1, red above.
    juce::Colour levelColour (float db) noexcept;

    // The colour a group bus is known by, in every view at once: the mixer's bands and strip
    // edges, a TRACKS header and its clips, the TUNE meters, the LIVE tiles, the Inspector's
    // rail and the output feeds. One place, because a group that reads as itself on four
    // workspaces and as something else on the fifth is worse than no colour at all.
    juce::Colour busTint (MixBus) noexcept;

    // Gestures --------------------------------------------------------------
    // A fader moves when it is dragged and at no other time. A two-finger swipe across a
    // bank of faders is a scroll, never twenty-four small changes to the mix, so no slider
    // in the app takes the wheel: the event passes through to the surface underneath.
    // Every slider goes through here.
    void dragOnly (juce::Slider&);

    // How far a swipe carries a scrolling surface. macOS hands JUCE a trackpad swipe in
    // points scaled by 0.5/256, and Viewport turns one unit into 14 x its single step, so
    // at the default step of 16 a console creeps along at well under half the speed of the
    // fingers. This makes a swipe move the view as far as it moves everything else on the
    // machine.
    void nativeScrolling (juce::Viewport&);
}

// A macOS push button in the three shapes the design uses.
class DineButton : public juce::Button
{
public:
    // Filled  - the primary action on the surface. Lime, near-black type, one per surface.
    // Toggle   - a setting that is on or off. On is a lit plane with a lime hairline, never
    //            a lime fill: the fill belongs to the action you are being asked to take.
    // Standard - everything else. Segment - one of a row inside a chip track. Ghost - bare.
    enum class Style { Filled, Standard, Ghost, Segment, Toggle };

    DineButton (const juce::String& text, Style s = Style::Standard);

    void setStyle (Style s)                 { if (s != style) { style = s; repaint(); } }
    // A filled button that means a console state rather than an action takes that state's
    // colour (a mute is amber wherever it is pressed), so the lime keeps its one meaning.
    void setTint (juce::Colour c)           { tint = c; repaint(); }
    void setIcon (Dine::Icon i)             { if (i != icon) { icon = i; repaint(); } }
    void setFontPx (float px)               { fontPx = px; repaint(); }
    void setPadX (int px)                   { padX = px; }
    void setCaps (bool on)                  { caps = on; repaint(); }   // the product verbs only
    int idealWidth() const;

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Style style;
    juce::Colour tint { Dine::accent };
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

// A filter chip: a segmented control's segment, with an optional colour dot in front of
// its label. The chips sit in one rounded track (drawChipTrack behind them), the way the
// bus filter on Inputs and the library filter on Sessions do.
class DineChip : public juce::Button
{
public:
    explicit DineChip (const juce::String& label, juce::Colour dot = juce::Colours::transparentBlack);
    int idealWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

    // The track a row of chips sits in. Draw it before the chips themselves.
    static void drawTrack (juce::Graphics&, juce::Rectangle<int>);

private:
    juce::String label;
    juce::Colour dot;
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
