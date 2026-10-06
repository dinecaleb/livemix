#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"
#include "Mix/MixSession.h"
#include "UI/LiveMixLookAndFeel.h"
#include "native/ThemeStore.h"
#include <map>

namespace livemix
{

// ---------------------------------------------------------------------------
// The DINE look, v3 (the Figma file "DINE - Full UX Mockup", page `v3 - Screens`, 2026-09-29).
// docs/DESIGN-V3.md is the map from that file to this code; read it before moving anything.
//
// Flat planes a few values apart, one hairline between them, no gradients and no glow. The
// window is macOS-shaped: the traffic lights sit *inside* the one 52 pt toolbar, the sidebar is
// a source list in sentence case, and the only capitals in the product are the verbs. The accent
// is the same teal (#6db8a8) and it is spent on the primary action, what is on, what is selected
// or soloed, what TUNE MIX did, and the meters. The console keys keep their own colours so a key
// says which key it is before it says it is on.
//
// Type is Inter for every word and IBM Plex Mono for every number - both embedded, so a booth Mac
// with no fonts installed reads exactly like the design.
//
// These tokens belong to the application; the plug-ins keep `Tokens` in src/UI and stay on Barlow.
//
// THEMES. The values below are the design's - "Studio Teal", the default - and they are the
// tokens' *initial* values, not constants: a theme (`app/native/ThemeStore`, a named table of the
// same colours) is written into them by `Dine::applyTheme`, and every page reads the token at
// paint time, so one call changes the whole app. Nothing captures a token at construction unless
// it re-reads it in `lookAndFeelChanged()`, and `Dine::refreshAllWindows()` is what a theme change
// calls to make that happen. The colour literals still belong here and nowhere else.
// ---------------------------------------------------------------------------
namespace Dine
{
    // Materials. Named for what they are used for; several share a value on purpose, so a
    // page that asks for "the rail" and one that asks for "the console" read as one thing.
    inline juce::Colour desk        { 0xff1c1c1e };   // behind the window
    inline juce::Colour window      { 0xff161618 };   // every workspace ground
    inline juce::Colour toolbar     { 0xff1c1c1e };   // the one toolbar and the status foot
    inline juce::Colour title       { 0xff1c1c1e };   // kept as an alias: the title row is the toolbar now
    inline juce::Colour menubar     { 0xff242426 };   // the transport well, a segment track, an inset display
    inline juce::Colour sidebar     { 0xff2a2a2d };   // the source list
    inline juce::Colour rail        { 0xff1a1a1c };   // a panel at the edge of a workspace
    inline juce::Colour pageBar     { 0xff161618 };   // a workspace's own tool row: the window's own plane
    inline juce::Colour console     { 0xff222224 };   // a console column, a timeline header
    inline juce::Colour tile        { 0xff1f1f21 };   // a quiet tile inside a workspace
    inline juce::Colour card        { 0xff232325 };   // a card, a sheet, a scene
    inline juce::Colour raised      { 0xff262628 };   // a popover, a lifted row
    inline juce::Colour item        { 0xff1f1f21 };   // a row inside a card, the timeline ground
    inline juce::Colour selected    { 0xff2c2c2e };   // the row, segment or strip that is chosen
    inline juce::Colour control     { 0xff2a2a2c };   // a resting button
    inline juce::Colour controlHot  { 0xff333335 };   // ... under the pointer
    inline juce::Colour controlOn   { 0xff464648 };   // a segment that is on
    inline juce::Colour sheet       { 0xff262628 };   // sheet material
    inline juce::Colour popover     { 0xff2a2a2d };   // menus, HUD, tooltips
    inline juce::Colour inset       { 0xff141416 };   // a well inside a card: the stage's own ground
    inline juce::Colour deep        { 0xff0b0b0c };   // the deepest well: a meter track, an inset display
    inline juce::Colour refuse      { 0xff2e2414 };   // a refusal's ground, and LIVE SAFE's card (amber on it)
    inline juce::Colour recGround   { 0xff3a1f1e };   // a card or a key that is recording / clipping
    // A soloed strip, a tuned chip: a lifted neutral plane. Green is not a brand colour, so a chosen
    // or tuned state is said by the teal lamp / hairline on a neutral ground, never by a green ground.
    inline juce::Colour soloGround  { 0xff2c2c2e };
    inline juce::Colour editGround  { 0xff1f2730 };   // a hand-edited chip, Autopilot's card

    // Kept for the few callers that name them; the v3 surfaces are flat, so they are the
    // flat value the band used to ramp to.
    inline juce::Colour chromeTop   { 0xff1c1c1e };
    inline juce::Colour headerTop   { 0xff161618 };
    inline juce::Colour footTop     { 0xff1c1c1e };
    inline juce::Colour footBottom  { 0xff1c1c1e };
    inline juce::Colour cardTop     { 0xff232325 };
    inline juce::Colour cardBottom  { 0xff232325 };
    inline juce::Colour sheetTop    { 0xff262628 };
    inline juce::Colour sheetBottom { 0xff262628 };
    inline juce::Colour railTop     { 0xff1a1a1c };
    inline juce::Colour accentTopLit{ 0xff6db8a8 };
    inline juce::Colour accentBotLit{ 0xff6db8a8 };

    // Edges and fills: translucent white, so one hairline reads the same over every material.
    inline juce::Colour hairSoft    { 0x0fffffff };   // .06
    inline juce::Colour hair        { 0x17ffffff };   // .08
    inline juce::Colour hairStrong  { 0x24ffffff };   // .12
    inline juce::Colour edge        { 0x33ffffff };   // .20
    inline juce::Colour fill        { 0x0fffffff };   // control background on a plane
    inline juce::Colour fillHover   { 0x1affffff };
    inline juce::Colour fillSoft    { 0x0affffff };
    inline juce::Colour well        { 0x0fffffff };   // meter wells, slider tracks: .06 of white

    // Ink.
    inline juce::Colour ink         { 0xfff5f5f7 };
    inline juce::Colour ink2        { 0x9eebebf5 };
    inline juce::Colour ink3        { 0x57ebebf5 };
    inline juce::Colour ink4        { 0x40ebebf5 };
    inline juce::Colour glyph       { 0xbfebebf5 };   // a resting icon reads as body ink in v3
    inline juce::Colour panMark     { 0x59ebebf5 };   // the centre mark of a balance, a resting radio

    // Roles.
    inline juce::Colour accent      { 0xff6db8a8 };
    inline juce::Colour accentHover { 0xff8ed0c2 };
    inline juce::Colour accentDeep  { 0xff5aa393 };   // pressed
    inline juce::Colour accentTop   { 0xff6db8a8 };
    inline juce::Colour accentBottom{ 0xff6db8a8 };
    inline juce::Colour onAccent    { 0xff0b0d10 };   // an accent button carries near-black type
    inline juce::Colour ok          { 0xff6db8a8 };
    inline juce::Colour hot         { 0xffffd60a };   // the meter's middle band, DIM and BYPASS when lit
    inline juce::Colour warn        { 0xffff9f0a };   // LIVE SAFE, and the one amber control
    inline juce::Colour crit        { 0xffff453a };

    // The console keys keep their own colours.
    inline juce::Colour keyMute     { 0xffff9f0a };
    inline juce::Colour keySolo     { 0xffffd60a };
    inline juce::Colour keyRec      { 0xffff453a };
    inline juce::Colour keyMon      { 0xff5e5ce6 };
    inline juce::Colour keyFx       { 0xffbf5af2 };
    inline juce::Colour monitor     { 0xff64d2ff };   // the engineer's own ears

    // The group buses, in `MixBus` order; `busTint` reads them.
    inline juce::Colour busDrums    { 0xffff9f0a };
    inline juce::Colour busBass     { 0xff30d158 };
    inline juce::Colour busMusic    { 0xffbf5af2 };
    inline juce::Colour busVocals   { 0xff5e8bff };   // BGV, since LEAD became its own group
    // LEAD: the one voice the mix is built around, so it is the one group colour that is not
    // a member of the band's family - it is what everything else is set against.
    inline juce::Colour busLead     { 0xff64d2ff };
    inline juce::Colour busSpeech   { 0xffff6482 };
    inline juce::Colour busAmbience { 0xffac8e68 };
    inline juce::Colour busMaster   { 0xffe5e5ea };

    inline juce::Colour focusRing   { 0xff6db8a8 };
    inline constexpr float    disabled    = 0.38f;

    // Corners: 12 for the window, 10 for a card or tile, 8 for a control, 6 for a chip.
    // Corners: 12 the window, 10 a card or sheet, 8 the transport well and a stage well,
    // 6 a control, chip, segment or sidebar row, 4 a broadcast key.
    namespace Radius
    {
        inline constexpr float window  = 12.0f;
        inline constexpr float card    = 10.0f;
        inline constexpr float well    = 8.0f;
        inline constexpr float control = 6.0f;
        inline constexpr float chip    = 6.0f;
        inline constexpr float pill    = 6.0f;
        inline constexpr float key     = 4.0f;   // DIM / MUTE / BYPASS / AUTOPILOT
    }

    namespace Metric
    {
        inline constexpr int toolbar   = 60;    // v4: the one row beside the sidebar card: session, transport, the right cluster
        inline constexpr int titleRow  = 0;     // there is no separate title row in v3
        inline constexpr int sidebar   = 230;   // v4: the 214 pt sidebar card and the 8 pt margin either side of it
        inline constexpr int sidebarRail = 52;  // what a folded sidebar becomes: the icons, still reachable
        inline constexpr int railHandle= 52;    // kept as an alias of the above
        inline constexpr int header    = 40;    // a workspace's own tool row
        inline constexpr int status    = 30;    // the status foot
        inline constexpr int chainFoot = 52;    // v4: the chain card (46) and the 6 pt gap over it, under every workspace
        inline constexpr int onAir     = 2;
        inline constexpr int chanRail  = 180;   // the Inspector's channel list
        inline constexpr int tuneRail  = 240;   // TUNE's input rail: v4 gives every row its TUNE CHANNEL pill
        inline constexpr int trail     = 280;   // WHAT DINE DID / TUNE's right column
        inline constexpr int setupNav  = 212;
        inline constexpr int footer    = 28;
        inline constexpr int rail      = 198;
        inline constexpr int setupRail = 340;
        inline constexpr int padX      = 40;    // a full-width page's gutter
        inline constexpr int padY      = 26;
        inline constexpr int gutter    = 24;    // the gutter between a rail and the middle
        inline constexpr int control   = 28;    // segments, chips, popups
        inline constexpr int button    = 28;    // a standard button
        inline constexpr int row       = 28;    // a sidebar row, a list row's key line
        inline constexpr int panelTab  = 15;    // the gutter a folded side panel leaves behind
    }

    // Type: Inter for words, IBM Plex Mono for numbers. Both embedded.
    juce::Font text (float px, int weight = 400);
    juce::Font mono (float px, int weight = 400);
    // A letterspaced caption: 600, tracked. The design's section labels and key words.
    juce::Font caps (float px, float tracking = 0.08f, int weight = 600);

    // ---------------------------------------------------------------- text size
    // View > Appearance > Text size. It scales the three calls above and nothing else: every
    // px a page asks for goes through one of them, and no metric does. So a name, a value, a
    // label, a button, a menu, an alert and the status foot all grow, while strip widths, row
    // heights, meters and the console's geometry keep their pixels - a bigger console would be
    // a different layout, and a volunteer who cannot read a fader's name wants the name bigger,
    // not fewer channels on the screen. A name that no longer fits is ellipsised, and the
    // tooltip a strip already carries is what says it in full.
    //
    // 1.0 Standard / 1.2 Large / 1.35 Larger (ThemeStore::textSizes()); the chosen one is
    // remembered on this Mac beside the theme. Message thread only.
    void setTextScale (float);
    float textScale();

    int textWidth (const juce::Font&, const juce::String&);

    // ---------------------------------------------------------------- drawing text
    // Use these, not g.drawText / g.drawFittedText, everywhere in the application.
    //
    // They do exactly what JUCE's versions do - same arguments, same defaults, the font and
    // colour still come from the Graphics - and they lay the glyphs out through a cache big
    // enough for a real console. JUCE has a cache of its own and it holds 128 entries for the
    // whole window (juce_GraphicsContext.cpp), which a 48-channel MIXER or the Inspector goes
    // through several times over in a single paint. Worse, it is a least-recently-used cache
    // and a page asks for its strings in the same order every paint - the one pattern an LRU
    // is worst at, where a cycle longer than the cache evicts precisely the entry wanted next,
    // so the hit rate collapses and every paint re-shapes every string through HarfBuzz.
    // Measured on a 48-channel console, that was two thirds of what TUNE and the Inspector
    // spent painting.
    //
    // Message thread only, like all painting. Nothing here runs on the audio thread.
    void drawText (juce::Graphics&, const juce::String&, juce::Rectangle<int> area,
                   juce::Justification, bool useEllipsesIfTooBig = true);
    void drawText (juce::Graphics&, const juce::String&, juce::Rectangle<float> area,
                   juce::Justification, bool useEllipsesIfTooBig = true);
    void drawText (juce::Graphics&, const juce::String&, int x, int y, int width, int height,
                   juce::Justification, bool useEllipsesIfTooBig = true);
    void drawFittedText (juce::Graphics&, const juce::String&, juce::Rectangle<int> area,
                         juce::Justification, int maximumNumberOfLines,
                         float minimumHorizontalScale = 0.0f);
    void drawFittedText (juce::Graphics&, const juce::String&, int x, int y, int width, int height,
                         juce::Justification, int maximumNumberOfLines,
                         float minimumHorizontalScale = 0.0f);

    // How many strings were laid out rather than found already laid out. A count, not a clock:
    // it is the same on every machine, so it is the number a frame-budget regression is caught
    // by. `dine_ui_snapshots --frames` prints it per workspace.
    struct TextCacheStats { long long hits, misses, size; };
    TextCacheStats textCacheStats();
    void resetTextCacheStats();
    // Every layout held is for a face at a particular size; anything that changes the faces
    // themselves should throw them away.
    void clearTextCache();

    // TEXT THAT DID NOT FIT. A name is data and is ellipsised on purpose; CLOSE cut to
    // "Clo..." is a layout bug, and no amount of reading the layout code finds one - the cell
    // is only too small once the face, the Text size and the string meet each other. So the
    // drawing keeps the list: while the audit is on, every curtailed string is recorded with
    // the width it had and the width it wanted. `dine_ui_snapshots` walks every workspace
    // with it on and prints what came back, widest shortfall first.
    struct ClippedText { juce::String text, where; float available = 0.0f, wanted = 0.0f; };
    void beginTextClipAudit();                     // on, and empty
    void endTextClipAudit();                       // off; the report survives until the next begin
    // What is being drawn right now, so a finding names the screen it is on rather than
    // leaving somebody to grep for the string. The snapshot walk sets it per shot.
    void setTextClipScope (const juce::String&);
    std::vector<ClippedText> textClipReport();

    // A FILE PATH IS READ FROM THE RIGHT. A path cut at the left-hand end keeps the one part
    // nobody needs and loses the file - so a path too long for its cell drops folders off the
    // front, under the home folder's own tilde, and keeps the name whatever happens.
    juce::String shortPath (const juce::File&, const juce::Font&, int width);

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

    // A section label. The design's captions are sentence case - the only capitals in the
    // product are its verbs - so a caption written in capitals is put back into sentence case
    // by `sectionCase`, which leaves the verbs and the initialisms alone.
    juce::String sectionCase (const juce::String&);
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
        Device, List, Target, Check, Warn, Gear, Play, Refresh, Chevron, UpDown, Dash, Bus, Sidebar, Search, Chat, Shield,
        // The v3 set: the sixteen glyphs of the design's `Icon` component (61:9083), drawn from
        // its own path data in a 16 pt box at a 1.4 pt stroke with round caps and joins.
        Sessions, DeviceNav, Inputs, Purpose, TracksNav, MixerNav, TuneNav, LiveNav, InspectorNav,
        Lock, WindowNav, Close, Headphones,
        // The v4 sidebar set (docs/design/v4): the mockup's glyphs, drawn in the same 20 pt box.
        NavSessions, NavPurpose, NavRouting, NavCheck, NavMixer, NavTune, NavInspector, NavFavourite,
        NavHistory, NavLive, NavSetlist, NavTracks, NavExport
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
    // Two rings joined: the mark on a channel whose fader is linked to another's. Drawn the same
    // on a mixer strip, a TRACKS header and in a menu, so a link looks like a link everywhere.
    void drawLinkGlyph (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

    juce::Colour levelColour (float db) noexcept;
    juce::Colour busTint (MixBus) noexcept;

    // ---- Themes. `applyTheme` resolves the document (its own colours over its base over
    // the default) and writes every token; the legacy gradient aliases follow their parents.
    // `currentColours` reads the tokens back as a complete palette, which is what the
    // Appearance sheet edits and saves. `themeBindings` is the one table joining a theme
    // key to its token; the snapshot tool checks it against `ThemeStore::tokens()`.
    struct ThemeBinding { const char* key; juce::Colour* colour; };
    const std::vector<ThemeBinding>& themeBindings();
    void applyTheme (const Theme&);
    void setThemeColour (const juce::String& key, juce::Colour);
    std::map<juce::String, juce::uint32> currentColours();
    const juce::String& currentThemeName();
    // Every open window re-reads the tokens: the look-and-feel's colours, each component's
    // `lookAndFeelChanged()`, a full repaint, and the desk behind each document window.
    void refreshAllWindows();
    void refreshWindow (juce::Component& root);   // one window (the headless snapshot tool's view is on no desktop)
    // Every component in a tree lays itself out again. What a Text size change needs and a
    // theme change does not: bounds do not move, so JUCE would call nobody's `resized()`.
    void relayoutTree (juce::Component& root);
    // A text editor's colours are set on it, not read at paint time, so the components that
    // own one call this from their constructor *and* from `lookAndFeelChanged()`.
    void styleTextEditor (juce::TextEditor&, juce::Colour ground, bool softFocusRing = false);

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
    // Unset = the accent, read when painted, so a theme change reaches a button built before it.
    std::optional<juce::Colour> tint;
    juce::Colour tintOr() const noexcept { return tint.value_or (Dine::accent); }
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
    void setValue (const juce::String& v) { if (v != value) { value = v; brief = {}; repaint(); } }
    // The same choice said in fewer words, for when the popup is not as wide as it wants to
    // be. A picker cut to "From the purpo..." has stopped naming the thing it is set to, so
    // the paint falls back to this rather than to an ellipsis. Set it after setValue.
    void setBriefValue (const juce::String& v) { if (v != brief) { brief = v; repaint(); } }
    const juce::String& getValue() const  { return value; }
    // A colour dot before the value (the Outputs sheet's source picker).
    void setDot (juce::Colour c)          { dot = c; repaint(); }
    // Flat: no plane until the pointer is on it. What the toolbar's output picker is, and any
    // other picker that sits on chrome rather than inside a form.
    void setFlat (bool f)                 { if (f != flat) { flat = f; repaint(); } }
    int idealWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String value, brief;
    juce::Colour dot { juce::Colours::transparentBlack };
    bool flat = false;
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
    // The v4 sidebar row (docs/design/v4): 13 pt in sentence case on the sidebar card, a white
    // plane at .12 when chosen and .06 under the pointer, 8 pt corners. A child row (ROUTING's
    // sections) has no icon and quieter, smaller type. The badge is the meta at the right, in
    // its own colour: amber for "this many need you", ink3 for a plain count.
    void setSidebarLook (bool child)      { sidebarLook = true; childRow = child; repaint(); }
    void setMetaTint (juce::Colour c)     { if (c != metaTint) { metaTint = c; repaint(); } }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    // A right-click (or Ctrl-click) on the row: its own menu, instead of a click.
    std::function<void()> onSecondaryClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onSecondaryClick) { onSecondaryClick(); return; }
        juce::Button::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onSecondaryClick) return;
        juce::Button::mouseUp (e);
    }

private:
    juce::String label, meta;
    Dine::Icon icon;
    bool selected = false, done = false;
    bool sidebarLook = false, childRow = false;
    juce::Colour metaTint;
    void paintSidebarRow (juce::Graphics&, bool over);
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
    void setTint (juce::Colour c) { if (c != tint) { tint = c; repaint(); } }   // a theme change re-reads the key's token

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

// A row of segments in its own track. The track is a plane, so the row can sit over another
// page (the ROUTING sections sit over the page they host) rather than being painted by a
// parent the page covers up.
class DineSegmentRow : public juce::Component
{
public:
    DineSegmentRow() { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics&) override;
};

// The 28x16 switch used for stereo pairs.
class DineSwitch : public juce::Button
{
public:
    explicit DineSwitch (const juce::String& onText, const juce::String& offText);
    // The track, the gap and the longer of the two words: a switch whose word is cut off is a
    // switch nobody can read, and the word grows with Text size while the track does not.
    int idealWidth() const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String onText, offText;
};

// THE PRODUCT'S ONE ROTARY - the design's `Knob` (62:9301).
//
// A number an engineer turns is a knob, not a bar lying on its side: a send, a trim, a
// monitor level. A 270 degree ring drawn from the left stop, a raised body with one
// pointer, and - where the cell has the room for them - what it reads and what it is
// called, under it and centred. Drag up and down (shift for fine); a double-click puts it
// back to its default.
//
// The one place a horizontal track is still right is a fader lying down in a LIST row,
// where the whole point is that a column of channels line up - and that one is drawn as a
// real fader cap, not as a slider.
class DineKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    DineKnob();

    // `mid` is the value at the middle of the sweep: 120 Hz halfway up a 20 Hz - 1 kHz
    // travel rather than down in the corner. Left out - or put outside the range - the travel
    // is linear. It is not defaulted to 0, which would quietly skew every range that does not
    // happen to be centred on zero.
    void setRange (double min, double max, double step = 0.0,
                   double mid = std::numeric_limits<double>::quiet_NaN());
    void setDefaultValue (double v)          { defaultValue = v; }
    void setValue (double v);                                  // no callback: this is the pull
    double getValue() const noexcept         { return value; }
    void setCaption (const juce::String& c)  { caption = c; repaint(); }
    void setFormat (std::function<juce::String (double)> f) { format = std::move (f); repaint(); }
    void setTint (juce::Colour c)            { tint = c; repaint(); }
    void setDial (int px);                                     // the ring's diameter; 48 by default
    void setShowsText (bool readout, bool name);               // a knob in a list row says neither

    std::function<void (double)> onChange;

    // What one costs where it is laid out in a grid: the design's 80 pt cell, widened for a
    // caption that will not fit in it, because a knob you cannot name is a knob you cannot use.
    int cellWidth() const;
    int cellHeight() const;
    static int cellHeight (int dial);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void enablementChanged() override        { repaint(); }

private:
    void apply (double v);

    std::function<juce::String (double)> format;
    juce::String caption;
    std::optional<juce::Colour> tint;        // unset = the accent, read when painted
    double value = 0.0, minimum = 0.0, maximum = 1.0, step = 0.0, mid = std::numeric_limits<double>::quiet_NaN();
    double defaultValue = 0.0, dragFrom = 0.0;
    float anchor = 0.0f;
    int dial = 48;
    bool showReadout = true, showCaption = true;
};

// A balance control: a 4 px track, the throw in the accent from the centre, an 11 px dot.
class PanBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    // Bar  - a track with a thumb: the Inspector's head, a LIST row.
    // Knob - the design's 26 pt pan (`Pan`, 62:9271): a 270 degree track, the value arc drawn
    //        from the centre and a pointer dot. What a mixer strip carries.
    enum class Style { Bar, Knob };

    std::function<void (float)> onChange;

    void setStyle (Style s)       { if (s != style) { style = s; repaint(); } }

    void setValue (float v)       { if (std::fabs (v - value) > 0.0005f) { value = v; repaint(); } }
    float getValue() const noexcept { return value; }
    void setTint (juce::Colour c) { tint = c; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { dragFrom = value; if (style == Style::Bar) drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void drag (const juce::MouseEvent&);

    float value = 0.0f, dragFrom = 0.0f;
    Style style = Style::Bar;
    std::optional<juce::Colour> tint;    // unset = the accent, read when painted
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

    // JUCE's own colour ids from the tokens; called by the constructor and by every theme change.
    void applyPalette();

    static void setBipolar (juce::Slider&, bool);

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider& s) override
    {
        const int cap = int (s.getProperties().getWithDefault ("dineFaderCap", 0));
        return cap > 0 ? cap / 2 : 7;
    }

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool down) override;
    // The tooltip is measured with the face it is drawn in. JUCE's own measurement uses a
    // fixed 13 pt, which is right until Text size moves the drawing font out from under it.
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
};

} // namespace livemix
