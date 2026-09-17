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
// The DLIVE look, v2 (the Claude Design file "DLIVE Desktop v2", 2026-09-17).
//
// The desk is a deep blue-black (#070809) and the application sits on it as one rounded
// panel (#0e1014). Every surface is a flat plane a few levels apart - there are no
// gradients, no glows and no outlines: the hierarchy is carried by value, by radius and by
// type. The accent is a teal (#6db8a8) spent on the primary action, the active workspace,
// what is selected or soloed, what DLIVE tuned, and the meters. The console keys keep their
// own colours (record red, monitoring blue, mute amber, solo teal) so a key says which key
// it is before it says it is on.
//
// Type is Barlow for everything a person reads and IBM Plex Mono for every number - both
// embedded, so a booth Mac with no fonts installed reads exactly like the design.
//
// These tokens belong to the application; the plug-in keeps `Tokens` in src/UI.
// ---------------------------------------------------------------------------
namespace Dine
{
    // Materials. Named for what they are used for; several share a value on purpose, so a
    // page that asks for "the rail" and one that asks for "the toolbar" read as one thing.
    inline const juce::Colour desk        { 0xff070809 };   // behind the window, the sidebar ground
    inline const juce::Colour window      { 0xff0e1014 };   // the application panel: every workspace ground
    inline const juce::Colour toolbar     { 0xff13161c };   // the toolbar, the status foot, a side rail
    inline const juce::Colour title       { 0xff0c0e12 };   // the title row under the traffic lights
    inline const juce::Colour menubar     { 0xff0a0c10 };   // the menu row, the transport pill, a segment track
    inline const juce::Colour sidebar     { 0xff070809 };   // the sidebar - the desk showing through
    inline const juce::Colour rail        { 0xff13161c };   // a panel at the edge of a workspace
    inline const juce::Colour pageBar     { 0xff10131a };   // a workspace's own tool row
    inline const juce::Colour console     { 0xff13161c };   // a console column, a timeline row
    inline const juce::Colour tile        { 0xff10131a };   // a tile inside a workspace (a group, the stage device)
    inline const juce::Colour card        { 0xff1a1e26 };   // a card, a sheet, a selected column
    inline const juce::Colour raised      { 0xff1c212b };   // a row in a list, a popover
    inline const juce::Colour item        { 0xff161a22 };   // a row inside a card
    inline const juce::Colour selected    { 0xff222830 };   // the row or segment that is chosen
    inline const juce::Colour control     { 0xff2a303a };   // a resting button
    inline const juce::Colour controlHot  { 0xff3e4656 };   // ... under the pointer
    inline const juce::Colour controlOn   { 0xff4e5664 };   // a setting that is on (a monitor chip, MONO)
    inline const juce::Colour sheet       { 0xff1a1e26 };   // sheet material
    inline const juce::Colour popover     { 0xff1c212b };   // menus, HUD, tooltips
    inline const juce::Colour refuse      { 0xff231d17 };   // a refusal's ground (amber on it)
    inline const juce::Colour recGround   { 0xff241618 };   // a card that is recording / clipping
    inline const juce::Colour soloGround  { 0xff16241f };   // a soloed tile, a tuned chip
    inline const juce::Colour editGround  { 0xff15202b };   // a hand-edited chip

    // Kept for the few callers that name them; the v2 surfaces are flat, so they are the
    // flat value the band used to ramp to.
    inline const juce::Colour chromeTop   { 0xff13161c };
    inline const juce::Colour headerTop   { 0xff10131a };
    inline const juce::Colour footTop     { 0xff13161c };
    inline const juce::Colour footBottom  { 0xff13161c };
    inline const juce::Colour cardTop     { 0xff1a1e26 };
    inline const juce::Colour cardBottom  { 0xff1a1e26 };
    inline const juce::Colour sheetTop    { 0xff1a1e26 };
    inline const juce::Colour sheetBottom { 0xff1a1e26 };
    inline const juce::Colour railTop     { 0xff13161c };
    inline const juce::Colour accentTopLit{ 0xff6db8a8 };
    inline const juce::Colour accentBotLit{ 0xff6db8a8 };

    // Edges and fills: translucent white, so one hairline reads the same over every material.
    inline const juce::Colour hairSoft    { 0x0fffffff };   // .06
    inline const juce::Colour hair        { 0x14ffffff };   // .08
    inline const juce::Colour hairStrong  { 0x1fffffff };   // .12
    inline const juce::Colour edge        { 0x33ffffff };   // .20
    inline const juce::Colour fill        { 0x1effffff };   // control background on a plane
    inline const juce::Colour fillHover   { 0x2affffff };
    inline const juce::Colour fillSoft    { 0x0fffffff };
    inline const juce::Colour well        { 0x0fffffff };   // meter wells, slider tracks: .06 of white

    // Ink.
    inline const juce::Colour ink         { 0xfff4f5f7 };
    inline const juce::Colour ink2        { 0xffa8b0bc };
    inline const juce::Colour ink3        { 0xff6b7380 };
    inline const juce::Colour ink4        { 0xff4e5664 };
    inline const juce::Colour glyph       { 0xff6b7380 };   // resting icon
    inline const juce::Colour panMark     { 0xff556070 };   // the centre mark of a balance, a resting radio

    // Roles.
    inline const juce::Colour accent      { 0xff6db8a8 };
    inline const juce::Colour accentHover { 0xff8ed0c2 };
    inline const juce::Colour accentDeep  { 0xff5aa393 };   // pressed
    inline const juce::Colour accentTop   { 0xff6db8a8 };
    inline const juce::Colour accentBottom{ 0xff6db8a8 };
    inline const juce::Colour onAccent    { 0xff070809 };   // an accent button carries near-black type
    inline const juce::Colour ok          { 0xff57b98d };
    inline const juce::Colour hot         { 0xffcbbf6a };   // the meter's middle band
    inline const juce::Colour warn        { 0xffe0a85c };
    inline const juce::Colour crit        { 0xffe06a64 };

    // The console keys keep their own colours.
    inline const juce::Colour keyMute     { 0xffe0a85c };
    inline const juce::Colour keySolo     { 0xff6db8a8 };
    inline const juce::Colour keyRec      { 0xffe06a64 };
    inline const juce::Colour keyMon      { 0xff6eafff };
    inline const juce::Colour monitor     { 0xff6eafff };   // the engineer's own ears

    inline const juce::Colour focusRing   { 0xff6db8a8 };
    inline constexpr float    disabled    = 0.38f;

    // Corners: 12 for the window, 10 for a card or tile, 8 for a control, 6 for a chip.
    namespace Radius
    {
        inline constexpr float window  = 12.0f;
        inline constexpr float card    = 10.0f;
        inline constexpr float control = 8.0f;
        inline constexpr float chip    = 6.0f;
        inline constexpr float pill    = 6.0f;
    }

    namespace Metric
    {
        inline constexpr int titleRow  = 52;    // the document row: the sidebar switch, the session, the counts
        inline constexpr int toolbar   = 56;    // the transport, the tabs, BYPASS / LIVE SAFE / the output
        inline constexpr int sidebar   = 184;   // LIBRARY / SET-UP / WORKSPACE, and the device along the foot
        inline constexpr int railHandle= 17;    // what a closed sidebar leaves behind
        inline constexpr int header    = 46;    // a workspace's own tool row
        inline constexpr int status    = 50;    // the status foot
        inline constexpr int chainFoot = 48;    // the picked-out channel's chain, under every workspace
        inline constexpr int onAir     = 2;
        inline constexpr int chanRail  = 200;   // a workspace's own channel list
        inline constexpr int tuneRail  = 198;
        inline constexpr int trail     = 280;   // WHAT DLIVE DID
        inline constexpr int setupNav  = 212;
        inline constexpr int footer    = 52;
        inline constexpr int rail      = 198;
        inline constexpr int setupRail = 340;   // the column beside a setup page (WHAT IS ARRIVING)
        inline constexpr int padX      = 24;
        inline constexpr int padY      = 22;
        inline constexpr int control   = 28;    // segments, chips, popups
        inline constexpr int button    = 32;    // a standard button
        inline constexpr int panelTab  = 15;    // the gutter a folded side panel leaves behind
    }

    // Type: Barlow for words, IBM Plex Mono for numbers. Both embedded.
    juce::Font text (float px, int weight = 400);
    juce::Font mono (float px, int weight = 400);
    // A letterspaced caption: 600, tracked. The design's section labels and key words.
    juce::Font caps (float px, float tracking = 0.08f, int weight = 600);

    int textWidth (const juce::Font&, const juce::String&);

    // Surfaces -------------------------------------------------------------
    void fillRounded (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
    void hairlineRounded (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
    // A flat card. The edge is drawn only when it carries a meaning of its own (a solo, a
    // warning) - the plain hairlines are ignored, because the v2 surfaces have no outlines.
    void drawCard (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill = card, juce::Colour edge = hair);
    void drawWell (juce::Graphics&, juce::Rectangle<float>, float radius);
    void drawFilled (juce::Graphics&, juce::Rectangle<float>, float radius, bool hover, bool down);
    void drawStandard (juce::Graphics&, juce::Rectangle<float>, float radius, bool hover, bool down);
    void drawSheet (juce::Graphics&, juce::Rectangle<float>, float radius);
    void drawRule (juce::Graphics&, juce::Rectangle<int>, juce::Colour = hair);

    // The bands. Flat now; kept as one call each so no two pages can drift apart.
    void drawChrome (juce::Graphics&, juce::Rectangle<int>);          // the toolbar
    void drawHeaderBand (juce::Graphics&, juce::Rectangle<int>);      // a workspace's tool row
    void drawStatusBand (juce::Graphics&, juce::Rectangle<int>);      // the status foot
    void drawPanelGround (juce::Graphics&, juce::Rectangle<int>);     // a side rail
    void drawRaisedCard (juce::Graphics&, juce::Rectangle<float>, bool shadow = false, juce::Colour edge = hair);
    void drawInsetWell (juce::Graphics&, juce::Rectangle<float>, float radius);
    // The track a row of segments sits in: #0a0c10, radius 8, the segments 2 px inside it.
    void drawSegmentTrack (juce::Graphics&, juce::Rectangle<int>);

    // A section label: 12 px, 600, tracked .08 em, quiet. "MIX HEALTH", "GROUPS", "SIGNAL PATH".
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String&);
    // A small status chip: 9 px tracked caps on a 20 % tint of its colour. Gain staging, badges.
    void drawStatusChip (juce::Graphics&, juce::Rectangle<float>, const juce::String&, juce::Colour, float px = 9.0f);
    // A soft colour: the colour mixed 20 % (or `amount`) into the panel, the way the design
    // does with color-mix.
    juce::Colour mix (juce::Colour tint, float amount = 0.20f, juce::Colour over = window) noexcept;
    // A meter fill: the accent to two thirds, then yellow, then red at the top.
    juce::ColourGradient meterGradient (juce::Rectangle<float>, bool vertical);
    void fillMeter (juce::Graphics&, juce::Rectangle<float> well, float level01, bool vertical, bool muted, float radius = 2.0f);

    // Icons ----------------------------------------------------------------
    enum class Icon
    {
        None, Drum, Cymbal, Mic, Guitar, Piano, Speech, Room, Fx, Waveform, Sliders,
        Device, List, Target, Check, Warn, Gear, Play, Refresh, Chevron, UpDown, Dash, Bus, Sidebar, Search, Chat, Shield
    };
    void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour, float thickness = 1.4f);
    Icon iconForRole (ChannelRole) noexcept;

    struct IconChoice { const char* key; const char* label; Icon icon; };
    const std::vector<IconChoice>& iconChoices();
    Icon iconFor (const std::string& key, ChannelRole fallback) noexcept;

    // Sources ---------------------------------------------------------------
    struct RoleGroup { const char* name; std::vector<ChannelRole> roles; };
    const std::vector<RoleGroup>& roleGroups();
    juce::String friendlyRoleName (ChannelRole);

    // The one way a row says it is the chosen one: a lit plane (#222830), no bar of colour.
    void drawSelectedRow (juce::Graphics&, juce::Rectangle<int>);
    void drawCaption (juce::Graphics&, juce::Rectangle<int>, const juce::String&);
    // A 14 px radio: the accent when chosen, the resting grey when not.
    void drawRadio (juce::Graphics&, juce::Rectangle<float>, bool on);
    struct BarSlice { float share; juce::Colour colour; };
    void drawStackedBar (juce::Graphics&, juce::Rectangle<int>, const std::vector<BarSlice>&);
    float pillWidth (const juce::String& text, bool withIcon);
    void drawPill (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour, Icon icon = Icon::None);
    // A small chevron pointing down: the mark on every popup (5 px tall, 8 wide).
    void drawDropChevron (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

    juce::Colour levelColour (float db) noexcept;
    juce::Colour busTint (MixBus) noexcept;

    // Gestures --------------------------------------------------------------
    void dragOnly (juce::Slider&);
    void nativeScrolling (juce::Viewport&);
}

// A push button in the shapes the design uses.
class DineButton : public juce::Button
{
public:
    // Filled   - the primary action: the accent, near-black type.
    // Standard - a resting control: #2a303a, pale type, brighter under the pointer.
    // Ghost    - bare text, a plane under it only when hovered.
    // Segment  - one of a row inside a track: lit #222830 when chosen, quiet otherwise.
    // Toggle   - a setting that is on or off: on is #4e5664 with white type (the monitor chips).
    enum class Style { Filled, Standard, Ghost, Segment, Toggle };

    DineButton (const juce::String& text, Style s = Style::Standard);

    void setStyle (Style s)                 { if (s != style) { style = s; repaint(); } }
    // A filled button that means a console state takes that state's colour (a mute is amber).
    void setTint (juce::Colour c)           { tint = c; repaint(); }
    void setIcon (Dine::Icon i)             { if (i != icon) { icon = i; repaint(); } }
    void setFontPx (float px)               { fontPx = px; repaint(); }
    void setPadX (int px)                   { padX = px; }
    void setCaps (bool on)                  { caps = on; repaint(); }   // the product verbs only
    // A quieter resting plane (#1a1e26): the tool-row actions on TRACKS, Undo / Redo mix.
    void setQuiet (bool q)                  { quiet = q; repaint(); }
    int idealWidth() const;

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Style style;
    juce::Colour tint { Dine::accent };
    Dine::Icon icon = Dine::Icon::None;
    float fontPx = 12.0f;
    int padX = 12;
    bool caps = false, quiet = false;
};

// A popup: a value and a chevron, on the control plane.
class DinePopup : public juce::Button
{
public:
    DinePopup();
    void setValue (const juce::String& v) { if (v != value) { value = v; repaint(); } }
    const juce::String& getValue() const  { return value; }
    // A colour dot before the value (the Outputs sheet's source picker).
    void setDot (juce::Colour c)          { dot = c; repaint(); }
    int idealWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String value;
    juce::Colour dot { juce::Colours::transparentBlack };
};

// Sidebar row: a label, optionally a leading icon and a trailing meta or tick.
class DineNavItem : public juce::Button
{
public:
    DineNavItem (const juce::String& label, Dine::Icon = Dine::Icon::None);
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

// The handle a collapsible side panel is opened and closed by: a slim gutter with the
// panel's name written down it when it is folded.
class DinePanelTab : public juce::Button
{
public:
    enum class Side { Left, Right };

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

// A console key: M, S, R, A. #2a303a when off; its own colour with dark type when on.
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

// A filter chip in a segment track.
class DineChip : public juce::Button
{
public:
    explicit DineChip (const juce::String& label, juce::Colour dot = juce::Colours::transparentBlack);
    int idealWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;
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

// A balance control: a 4 px track, the throw in the accent from the centre, an 11 px dot.
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

// A level meter: a translucent well and one gradient bar (accent, yellow, red), rounded.
// One fill per frame, however tall it is - which is what lets forty-eight of them run.
class DineMeter : public juce::Component
{
public:
    enum class Style { Segments, Bar };
    explicit DineMeter (Style s = Style::Segments) : style (s) { setInterceptsMouseClicks (false, false); setOpaque (false); }

    // Ballistics are in time, not frames: the level falls 60 dB/s whatever the frame rate.
    void setLevels (float peakDb, float holdDb, bool clipped);
    float getPeakDb() const noexcept { return peak; }
    void setMuted (bool);
    void setStyle (Style s) { style = s; repaint(); }
    void paint (juce::Graphics&) override;

    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (juce::jmin (0.0f, db) + 60.0f) / 60.0f); }

private:
    Style style;
    float peak = -120.0f, hold = -120.0f;
    juce::uint32 lastMs = 0;
    bool clipped = false, muted = false;
};

// The application's look and feel.
class DineLookAndFeel : public LiveMixLookAndFeel
{
public:
    DineLookAndFeel();

    static void setBipolar (juce::Slider&, bool);

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
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
