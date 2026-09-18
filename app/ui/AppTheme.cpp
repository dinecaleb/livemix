#include "AppTheme.h"

namespace livemix
{

// ============================================================================ type
// Barlow and IBM Plex Mono, embedded (LiveMixFonts): the same faces the design file loads,
// so the booth Mac reads exactly like the mock whatever it has installed.
juce::Font Dine::text (float px, int weight)
{
    return LiveMixLookAndFeel::body (px, weight, 0.0f);
}

juce::Font Dine::mono (float px, int weight)
{
    return LiveMixLookAndFeel::mono (px, weight, 0.0f);
}

juce::Font Dine::caps (float px, float tracking, int weight)
{
    return LiveMixLookAndFeel::body (px, weight, 0.0f).withExtraKerningFactor (tracking);
}

int Dine::textWidth (const juce::Font& f, const juce::String& t)
{
    return int (std::ceil (juce::GlyphArrangement::getStringWidth (f, t))) + 2;
}

// ============================================================================ surfaces
void Dine::fillRounded (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius)
{
    g.setColour (c);
    g.fillRoundedRectangle (r, radius);
}

void Dine::hairlineRounded (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius)
{
    g.setColour (c);
    g.drawRoundedRectangle (r.reduced (0.25f), radius, 0.5f);
}

void Dine::drawCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fillColour, juce::Colour edgeColour)
{
    fillRounded (g, r, fillColour, Radius::card);
    // The v2 surfaces carry no outline. An edge is drawn only when a caller gave it a meaning
    // of its own - a solo, a warning - which is never one of the plain hairlines.
    if (edgeColour != hair && edgeColour != hairSoft && edgeColour != hairStrong && edgeColour != edge
        && ! edgeColour.isTransparent())
        hairlineRounded (g, r, edgeColour, Radius::card);
}

void Dine::drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    fillRounded (g, r, well, radius);
}

void Dine::drawFilled (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool hover, bool down)
{
    fillRounded (g, r, down ? accentDeep : hover ? accentHover : accent, radius);
}

void Dine::drawStandard (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool hover, bool down)
{
    fillRounded (g, r, down ? controlOn : hover ? controlHot : control, radius);
}

void Dine::drawSheet (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    juce::DropShadow (juce::Colours::black.withAlpha (0.65f), 60, { 0, 28 }).drawForRectangle (g, r.toNearestInt());
    fillRounded (g, r, sheet, radius);
}

void Dine::drawRule (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c)
{
    g.setColour (c);
    g.fillRect (float (r.getX()), float (r.getY()), float (r.getWidth()), 0.5f);
}

void Dine::drawChrome (juce::Graphics& g, juce::Rectangle<int> r)      { g.setColour (toolbar); g.fillRect (r); }
void Dine::drawHeaderBand (juce::Graphics& g, juce::Rectangle<int> r)  { g.setColour (pageBar); g.fillRect (r); }
void Dine::drawStatusBand (juce::Graphics& g, juce::Rectangle<int> r)  { g.setColour (toolbar); g.fillRect (r); }
void Dine::drawPanelGround (juce::Graphics& g, juce::Rectangle<int> r) { g.setColour (rail); g.fillRect (r); }

void Dine::drawRaisedCard (juce::Graphics& g, juce::Rectangle<float> r, bool, juce::Colour edgeColour)
{
    drawCard (g, r, card, edgeColour);
}

void Dine::drawInsetWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    fillRounded (g, r, well, radius);
}

void Dine::drawSegmentTrack (juce::Graphics& g, juce::Rectangle<int> r)
{
    fillRounded (g, r.toFloat(), menubar, Radius::control);
}

void Dine::drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label)
{
    g.setColour (ink4);
    g.setFont (caps (12.0f, 0.08f));
    g.drawText (label, r, juce::Justification::centredLeft, true);
}

juce::Colour Dine::mix (juce::Colour tint, float amount, juce::Colour over) noexcept
{
    return over.overlaidWith (tint.withAlpha (amount));
}

void Dine::drawStatusChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, juce::Colour colour, float px)
{
    fillRounded (g, r, mix (colour, 0.20f), Radius::chip);
    g.setColour (colour);
    g.setFont (caps (px, 0.06f, 500));
    g.drawText (label, r, juce::Justification::centred, true);
}

juce::ColourGradient Dine::meterGradient (juce::Rectangle<float> r, bool vertical)
{
    // to top: accent 0 -> 66 %, yellow at 84 %, red at 100 %
    juce::ColourGradient grad (accent, vertical ? r.getX() : r.getX(), vertical ? r.getBottom() : r.getY(),
                               crit, vertical ? r.getX() : r.getRight(), vertical ? r.getY() : r.getY(), false);
    grad.addColour (0.66, accent);
    grad.addColour (0.84, hot);
    return grad;
}

void Dine::fillMeter (juce::Graphics& g, juce::Rectangle<float> wellArea, float level, bool vertical, bool muted, float radius)
{
    fillRounded (g, wellArea, well, radius);
    if (level <= 0.001f) return;
    auto lit = vertical ? wellArea.withTrimmedTop (wellArea.getHeight() * (1.0f - level))
                        : wellArea.withWidth (juce::jmax (2.0f, wellArea.getWidth() * level));
    if (muted)
    {
        g.setColour (ink4);
        g.fillRoundedRectangle (lit, radius);
        return;
    }
    // The gradient is fixed to the well, not to the lit part, so the yellow and the red only
    // appear when the level actually reaches them.
    g.setGradientFill (meterGradient (wellArea, vertical));
    g.fillRoundedRectangle (lit, radius);
}

juce::Colour Dine::levelColour (float db) noexcept
{
    return db >= -1.0f ? crit : db >= -6.0f ? hot : accent;
}

juce::Colour Dine::busTint (MixBus b) noexcept
{
    switch (b)
    {
        case MixBus::Drums:    return busDrums;
        case MixBus::Bass:     return busBass;
        case MixBus::Music:    return busMusic;
        case MixBus::Vocals:   return busVocals;
        case MixBus::Speech:   return busSpeech;
        case MixBus::Ambience: return busAmbience;
        case MixBus::Master:   return busMaster;
        case MixBus::Count:    break;
    }
    return ink2;
}

// ============================================================================ themes
namespace
{
    juce::String activeThemeName { ThemeStore::kDefaultName };

    void followParents()
    {
        // The flat design keeps the old gradient names as aliases; they follow their parents.
        Dine::chromeTop = Dine::footTop = Dine::footBottom = Dine::railTop = Dine::toolbar;
        Dine::headerTop = Dine::pageBar;
        Dine::cardTop = Dine::cardBottom = Dine::card;
        Dine::sheetTop = Dine::sheetBottom = Dine::sheet;
        Dine::accentTop = Dine::accentBottom = Dine::accentTopLit = Dine::accentBotLit = Dine::accent;
    }
}

const std::vector<Dine::ThemeBinding>& Dine::themeBindings()
{
    static const std::vector<ThemeBinding> table = {
        { "desk", &desk }, { "window", &window }, { "toolbar", &toolbar }, { "title", &title }, { "menubar", &menubar },
        { "sidebar", &sidebar }, { "rail", &rail }, { "pageBar", &pageBar }, { "console", &console }, { "tile", &tile },
        { "card", &card }, { "raised", &raised }, { "item", &item }, { "selected", &selected }, { "control", &control },
        { "controlHot", &controlHot }, { "controlOn", &controlOn }, { "sheet", &sheet }, { "popover", &popover },
        { "refuse", &refuse }, { "recGround", &recGround }, { "soloGround", &soloGround }, { "editGround", &editGround },
        { "hairSoft", &hairSoft }, { "hair", &hair }, { "hairStrong", &hairStrong }, { "edge", &edge }, { "fill", &fill },
        { "fillHover", &fillHover }, { "fillSoft", &fillSoft }, { "well", &well },
        { "ink", &ink }, { "ink2", &ink2 }, { "ink3", &ink3 }, { "ink4", &ink4 }, { "glyph", &glyph }, { "panMark", &panMark },
        { "accent", &accent }, { "accentHover", &accentHover }, { "accentDeep", &accentDeep }, { "onAccent", &onAccent },
        { "focusRing", &focusRing },
        { "ok", &ok }, { "hot", &hot }, { "warn", &warn }, { "crit", &crit }, { "monitor", &monitor },
        { "keyMute", &keyMute }, { "keySolo", &keySolo }, { "keyRec", &keyRec }, { "keyMon", &keyMon },
        { "busDrums", &busDrums }, { "busBass", &busBass }, { "busMusic", &busMusic }, { "busVocals", &busVocals },
        { "busSpeech", &busSpeech }, { "busAmbience", &busAmbience }, { "busMaster", &busMaster },
    };
    return table;
}

void Dine::applyTheme (const Theme& theme)
{
    const auto palette = ThemeStore::resolve (theme);
    for (const auto& b : themeBindings())
    {
        const auto it = palette.find (b.key);
        if (it != palette.end()) *b.colour = juce::Colour (it->second);
    }
    followParents();
    activeThemeName = theme.name;
}

void Dine::setThemeColour (const juce::String& key, juce::Colour c)
{
    for (const auto& b : themeBindings())
        if (key == b.key) { *b.colour = c; followParents(); return; }
}

std::map<juce::String, juce::uint32> Dine::currentColours()
{
    std::map<juce::String, juce::uint32> out;
    for (const auto& b : themeBindings()) out[b.key] = b.colour->getARGB();
    return out;
}

const juce::String& Dine::currentThemeName() { return activeThemeName; }

void Dine::refreshWindow (juce::Component& root)
{
    if (auto* laf = dynamic_cast<DineLookAndFeel*> (&root.getLookAndFeel())) laf->applyPalette();
    if (auto* doc = dynamic_cast<juce::DocumentWindow*> (&root)) doc->setBackgroundColour (desk);
    root.sendLookAndFeelChange();     // every child: lookAndFeelChanged() and a repaint, which also drops a cached image
}

void Dine::refreshAllWindows()
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent (i)) refreshWindow (*top);
}

void Dine::styleTextEditor (juce::TextEditor& e, juce::Colour ground, bool softFocusRing)
{
    e.setColour (juce::TextEditor::backgroundColourId, ground);
    e.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    e.setColour (juce::TextEditor::focusedOutlineColourId, softFocusRing ? accent.withAlpha (0.6f) : accent);
    e.setColour (juce::TextEditor::textColourId, ink);
    e.setColour (juce::TextEditor::highlightedTextColourId, ink);
    e.setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.28f));
    e.setColour (juce::CaretComponent::caretColourId, accent);
    e.applyColourToAllText (ink, true);
}

// ============================================================================ gestures
void Dine::dragOnly (juce::Slider& s)
{
    s.setScrollWheelEnabled (false);
}

void Dine::nativeScrolling (juce::Viewport& v)
{
    v.setSingleStepSizes (37, 37);
}

// ============================================================================ icons
namespace
{
    struct IconStroke { const char* d; float weight; bool filled; };

    // Transcribed from the design's symbol set (20 x 20 view box, thin strokes).
    const std::vector<IconStroke>& iconPaths (Dine::Icon icon)
    {
        static const std::vector<IconStroke> empty {};
        static const std::vector<IconStroke> drum {
            { "M3.5 6.5a6.5 2.8 0 1 0 13 0a6.5 2.8 0 1 0 -13 0", 1.4f, false },
            { "M3.5 6.5v7c0 1.55 2.91 2.8 6.5 2.8s6.5-1.25 6.5-2.8v-7", 1.4f, false },
            { "M5.6 8.9l2.2 6.9M14.4 8.9l-2.2 6.9", 1.1f, false } };
        static const std::vector<IconStroke> cymbal {
            { "M2.2 8.4h15.6", 1.4f, false },
            { "M4.6 8.4c1.6-2.4 3.4-3.6 5.4-3.6s3.8 1.2 5.4 3.6", 1.3f, false },
            { "M10 8.6V17", 1.4f, false } };
        static const std::vector<IconStroke> mic {
            { "M10 2.4h0a2.4 2.4 0 0 1 2.4 2.4v3.8a2.4 2.4 0 0 1 -2.4 2.4h0a2.4 2.4 0 0 1 -2.4 -2.4v-3.8a2.4 2.4 0 0 1 2.4 -2.4z", 1.4f, false },
            { "M5 9.6a5 5 0 0 0 10 0M10 14.6V17.4M7.4 17.4h5.2", 1.4f, false } };
        static const std::vector<IconStroke> guitar {
            { "M17.2 2.8l-4 4M12.6 6.2l1.4 1.4", 1.4f, false },
            { "M11.6 7.4c-1.3-.5-2.9-.3-4 .8-1.5 1.5-1 3-2 4s-2.6 1-3.4 1.8c-.7.8-.6 2 .2 2.8.8.8 2 .9 2.8.2.8-.8.8-2.4 1.8-3.4s2.5-.5 4-2c1.1-1.1 1.3-2.7.8-4z", 1.3f, false } };
        static const std::vector<IconStroke> piano {
            { "M4 4.6h12a1.6 1.6 0 0 1 1.6 1.6v7.6a1.6 1.6 0 0 1 -1.6 1.6h-12a1.6 1.6 0 0 1 -1.6 -1.6v-7.6a1.6 1.6 0 0 1 1.6 -1.6z", 1.4f, false },
            { "M7.4 4.6v6.4M12.6 4.6v6.4M2.4 11h15.2", 1.2f, false } };
        static const std::vector<IconStroke> speech {
            { "M3 10h2l1.6-4.6L9 15l2.4-8 1.8 5.4L14.8 10H17", 1.4f, false } };
        static const std::vector<IconStroke> room {
            { "M10 3v14", 1.4f, false },
            { "M6.4 5.6a6.6 6.6 0 0 0 0 8.8M3.4 3.2a10.4 10.4 0 0 0 0 13.6M13.6 5.6a6.6 6.6 0 0 1 0 8.8M16.6 3.2a10.4 10.4 0 0 1 0 13.6", 1.3f, false } };
        static const std::vector<IconStroke> fx {
            { "M2.6 14.4c2.4 0 3.2-9 5.6-9s3.2 9 5.6 9c1.4 0 2.2-3 3.6-3", 1.4f, false } };
        static const std::vector<IconStroke> dash { { "M5 10h10", 1.4f, false } };
        static const std::vector<IconStroke> waveform {
            { "M2.6 8v4M5.8 5.4v9.2M9 2.6v14.8M12.2 5.4v9.2M15.4 7v6M18.2 9v2", 1.5f, false } };
        static const std::vector<IconStroke> sliders {
            { "M4 3.2v13.6M10 3.2v13.6M16 3.2v13.6", 1.4f, false },
            { "M3.3 6h1.4a1.1 1.1 0 0 1 1.1 1.1v0.4a1.1 1.1 0 0 1 -1.1 1.1h-1.4a1.1 1.1 0 0 1 -1.1 -1.1v-0.4a1.1 1.1 0 0 1 1.1 -1.1z", 1.4f, false },
            { "M9.3 10.4h1.4a1.1 1.1 0 0 1 1.1 1.1v0.4a1.1 1.1 0 0 1 -1.1 1.1h-1.4a1.1 1.1 0 0 1 -1.1 -1.1v-0.4a1.1 1.1 0 0 1 1.1 -1.1z", 1.4f, false },
            { "M15.3 7.4h1.4a1.1 1.1 0 0 1 1.1 1.1v0.4a1.1 1.1 0 0 1 -1.1 1.1h-1.4a1.1 1.1 0 0 1 -1.1 -1.1v-0.4a1.1 1.1 0 0 1 1.1 -1.1z", 1.4f, false } };
        static const std::vector<IconStroke> device {
            { "M4.4 4.4h11.2a2 2 0 0 1 2 2v7.2a2 2 0 0 1 -2 2h-11.2a2 2 0 0 1 -2 -2v-7.2a2 2 0 0 1 2 -2z", 1.4f, false },
            { "M6.6 8.1a1.9 1.9 0 1 0 0 3.8a1.9 1.9 0 1 0 0 -3.8", 1.3f, false },
            { "M11.4 8h4.2M11.4 12h4.2", 1.3f, false } };
        static const std::vector<IconStroke> list {
            { "M6.4 5.4h11M6.4 10h11M6.4 14.6h11", 1.4f, false },
            { "M3.2 4.3a1.1 1.1 0 1 0 0 2.2a1.1 1.1 0 1 0 0 -2.2M3.2 8.9a1.1 1.1 0 1 0 0 2.2a1.1 1.1 0 1 0 0 -2.2M3.2 13.5a1.1 1.1 0 1 0 0 2.2a1.1 1.1 0 1 0 0 -2.2", 1.0f, true } };
        static const std::vector<IconStroke> target {
            { "M10 2.8a7.2 7.2 0 1 0 0 14.4a7.2 7.2 0 1 0 0 -14.4", 1.4f, false },
            { "M10 6.8a3.2 3.2 0 1 0 0 6.4a3.2 3.2 0 1 0 0 -6.4", 1.4f, false } };
        static const std::vector<IconStroke> check { { "M4.6 10.6l3.4 3.4 7.4-8", 1.7f, false } };
        static const std::vector<IconStroke> warn {
            { "M10 3.4l7 12.2H3z", 1.4f, false },
            { "M10 7.8v3.6", 1.4f, false },
            { "M10 12.5a0.9 0.9 0 1 0 0 1.8a0.9 0.9 0 1 0 0 -1.8", 1.0f, true } };
        static const std::vector<IconStroke> gear {
            { "M10 7.4a2.6 2.6 0 1 0 0 5.2a2.6 2.6 0 1 0 0 -5.2", 1.4f, false },
            { "M10 2.6v2M10 15.4v2M2.6 10h2M15.4 10h2M4.8 4.8l1.4 1.4M13.8 13.8l1.4 1.4M15.2 4.8l-1.4 1.4M6.2 13.8l-1.4 1.4", 1.3f, false } };
        static const std::vector<IconStroke> play { { "M6.4 3.8l9.2 6.2-9.2 6.2z", 1.4f, false } };
        static const std::vector<IconStroke> refresh {
            { "M16.4 10a6.4 6.4 0 1 1-2-4.6", 1.4f, false },
            { "M16.6 2.8v3.2h-3.2", 1.4f, false } };
        static const std::vector<IconStroke> chevron { { "M7.6 4.6l4.8 5.4-4.8 5.4", 1.5f, false } };
        static const std::vector<IconStroke> updown { { "M6.8 8.4L10 5.2l3.2 3.2M6.8 11.6L10 14.8l3.2-3.2", 1.5f, false } };
        static const std::vector<IconStroke> bus {
            { "M3.4 5.2h13.2M3.4 10h13.2M3.4 14.8h13.2", 1.4f, false } };
        static const std::vector<IconStroke> search {
            { "M9 3.6a5.4 5.4 0 1 0 0 10.8a5.4 5.4 0 1 0 0 -10.8", 1.5f, false },
            { "M12.9 12.9l3.6 3.6", 1.5f, false } };
        // The revamp's two new glyphs: the chat the toolbar opens, and the shield LIVE SAFE
        // is known by wherever it appears (the toolbar key, the LIVE panel, a refusal).
        static const std::vector<IconStroke> chat {
            { "M3.2 5.4a2 2 0 0 1 2-2h9.6a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H8.4L4.6 16.4V13.4h-.6v-8z", 1.4f, false },
            { "M6.6 7.4h6.8M6.6 10.2h4.4", 1.4f, false } };
        static const std::vector<IconStroke> shield {
            { "M10 2.8l6 2.4v5.2c0 3.4-2.5 5.6-6 6.8-3.5-1.2-6-3.4-6-6.8V5.2z", 1.6f, false } };
        static const std::vector<IconStroke> sidebar {
            { "M3.4 4.2h13.2a1.8 1.8 0 0 1 1.8 1.8v8a1.8 1.8 0 0 1 -1.8 1.8h-13.2a1.8 1.8 0 0 1 -1.8 -1.8v-8a1.8 1.8 0 0 1 1.8 -1.8z", 1.4f, false },
            { "M7.8 4.2v11.6", 1.4f, false } };

        switch (icon)
        {
            case Dine::Icon::Drum:     return drum;
            case Dine::Icon::Cymbal:   return cymbal;
            case Dine::Icon::Mic:      return mic;
            case Dine::Icon::Guitar:   return guitar;
            case Dine::Icon::Piano:    return piano;
            case Dine::Icon::Speech:   return speech;
            case Dine::Icon::Room:     return room;
            case Dine::Icon::Fx:       return fx;
            case Dine::Icon::Waveform: return waveform;
            case Dine::Icon::Sliders:  return sliders;
            case Dine::Icon::Device:   return device;
            case Dine::Icon::List:     return list;
            case Dine::Icon::Target:   return target;
            case Dine::Icon::Check:    return check;
            case Dine::Icon::Warn:     return warn;
            case Dine::Icon::Gear:     return gear;
            case Dine::Icon::Play:     return play;
            case Dine::Icon::Refresh:  return refresh;
            case Dine::Icon::Chevron:  return chevron;
            case Dine::Icon::UpDown:   return updown;
            case Dine::Icon::Dash:     return dash;
            case Dine::Icon::Bus:      return bus;
            case Dine::Icon::Sidebar:  return sidebar;
            case Dine::Icon::Search:   return search;
            case Dine::Icon::Chat:     return chat;
            case Dine::Icon::Shield:   return shield;
            case Dine::Icon::None:
            default:                   return empty;
        }
    }

    // Parsed once per icon; the paths are plain geometry in the 20 x 20 box.
    struct ParsedIcon { std::vector<std::pair<juce::Path, IconStroke>> parts; };

    const ParsedIcon& parsedIcon (Dine::Icon icon)
    {
        static std::array<std::unique_ptr<ParsedIcon>, 32> cache;
        auto& slot = cache[size_t (icon)];
        if (slot == nullptr)
        {
            slot = std::make_unique<ParsedIcon>();
            for (const auto& s : iconPaths (icon))
                slot->parts.push_back ({ juce::Drawable::parseSVGPath (s.d), s });
        }
        return *slot;
    }
}

void Dine::drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour, float thickness)
{
    if (icon == Icon::None) return;
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const float scale = size / 20.0f;
    auto box = bounds.withSizeKeepingCentre (size, size);
    const auto transform = juce::AffineTransform::scale (scale).translated (box.getX(), box.getY());

    g.setColour (colour);
    for (const auto& part : parsedIcon (icon).parts)
    {
        auto p = part.first;
        p.applyTransform (transform);
        if (part.second.filled) g.fillPath (p);
        else g.strokePath (p, juce::PathStrokeType (juce::jmax (0.75f, part.second.weight * scale * thickness / 1.4f),
                                                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

const std::vector<Dine::IconChoice>& Dine::iconChoices()
{
    static const std::vector<IconChoice> choices {
        { "drum",     "Drum",            Icon::Drum },
        { "cymbal",   "Cymbal",          Icon::Cymbal },
        { "mic",      "Microphone",      Icon::Mic },
        { "speech",   "Speech",          Icon::Speech },
        { "guitar",   "Guitar / Bass",   Icon::Guitar },
        { "piano",    "Keys",            Icon::Piano },
        { "room",     "Room",            Icon::Room },
        { "bus",      "Group / console", Icon::Bus },
        { "waveform", "Playback track",  Icon::Waveform },
        { "fx",       "Effect",          Icon::Fx },
        { "sliders",  "Console feed",    Icon::Sliders },
        { "device",   "Device",          Icon::Device },
    };
    return choices;
}

Dine::Icon Dine::iconFor (const std::string& key, ChannelRole fallback) noexcept
{
    if (! key.empty())
        for (const auto& c : iconChoices())
            if (key == c.key) return c.icon;
    return iconForRole (fallback);
}

const std::vector<Dine::RoleGroup>& Dine::roleGroups()
{
    static const std::vector<RoleGroup> groups {
        { "Drums", { ChannelRole::KickIn, ChannelRole::KickOut, ChannelRole::SnareTop, ChannelRole::SnareBottom, ChannelRole::HiHat,
                     ChannelRole::RackTom, ChannelRole::FloorTom, ChannelRole::Overhead, ChannelRole::OverheadLeft, ChannelRole::OverheadRight, ChannelRole::Room, ChannelRole::DrumBus } },
        { "Bass",  { ChannelRole::BassDI, ChannelRole::BassAmp, ChannelRole::SynthBass } },
        { "Music", { ChannelRole::Piano, ChannelRole::ElectricPiano, ChannelRole::Organ, ChannelRole::SynthPad, ChannelRole::SynthLead,
                     ChannelRole::AcousticGuitar, ChannelRole::ElectricGuitarClean, ChannelRole::ElectricGuitarDrive,
                     ChannelRole::SaxAlto, ChannelRole::SaxTenor, ChannelRole::SaxBari } },
        { "Vocals", { ChannelRole::LeadVocal, ChannelRole::BackingVocal, ChannelRole::Choir } },
        // Speaking microphones are their own group in the mix, so they are their own group here:
        // whoever assigns the inputs picks the pastor out of a list of one, not out of the singers.
        { "Speech", { ChannelRole::Speech } },
        // The room and the people in it. Its own group for the same reason SPEECH is: these
        // microphones are turned up and down at moments nothing else moves at, and an operator
        // has to be able to find them.
        { "Crowd and room", { ChannelRole::CrowdMic, ChannelRole::AmbienceMic } },
    };
    return groups;
}

// Plain words for the menu: "Tracks" is a synth pad, "Pastor" is speech.
juce::String Dine::friendlyRoleName (ChannelRole r)
{
    switch (r)
    {
        case ChannelRole::SynthPad:  return "Synth Pad / Tracks";
        case ChannelRole::Speech:    return "Pastor / Speech";
        case ChannelRole::Overhead:  return "Overheads (stereo pair)";
        case ChannelRole::DrumBus:   return "Drum mix (stereo, from the console)";
        case ChannelRole::BassDI:    return "Bass (DI)";
        case ChannelRole::BassAmp:   return "Bass (amp mic)";
        case ChannelRole::CrowdMic:  return "Crowd / congregation";
        case ChannelRole::AmbienceMic: return "Room ambience";
        case ChannelRole::SaxAlto:   return "Saxophone (alto)";
        case ChannelRole::SaxTenor:  return "Saxophone (tenor)";
        case ChannelRole::SaxBari:   return "Saxophone (baritone)";
        default:                     return channelRoleName (r);
    }
}

Dine::Icon Dine::iconForRole (ChannelRole r) noexcept
{
    switch (r)
    {
        case ChannelRole::KickIn: case ChannelRole::KickOut: case ChannelRole::SnareTop:
        case ChannelRole::SnareBottom: case ChannelRole::RackTom: case ChannelRole::FloorTom:
        case ChannelRole::DrumBus:
            return Icon::Drum;
        case ChannelRole::HiHat: case ChannelRole::Overhead: case ChannelRole::OverheadLeft:
        case ChannelRole::OverheadRight:
            return Icon::Cymbal;
        case ChannelRole::Room:
        case ChannelRole::CrowdMic: case ChannelRole::AmbienceMic: case ChannelRole::AmbienceBus:
            return Icon::Room;
        case ChannelRole::LeadVocal: case ChannelRole::BackingVocal: case ChannelRole::Choir:
        case ChannelRole::VocalBus:
            return Icon::Mic;
        case ChannelRole::Speech:
            return Icon::Speech;
        case ChannelRole::Piano: case ChannelRole::ElectricPiano: case ChannelRole::Organ:
        case ChannelRole::SynthPad: case ChannelRole::SynthLead: case ChannelRole::KeysBus:
            return Icon::Piano;
        case ChannelRole::AcousticGuitar: case ChannelRole::ElectricGuitarClean:
        case ChannelRole::ElectricGuitarDrive: case ChannelRole::GuitarBus:
        case ChannelRole::BassDI: case ChannelRole::BassAmp: case ChannelRole::SynthBass:
        case ChannelRole::BassBus:
            return Icon::Guitar;
        default:
            return Icon::Waveform;
    }
}

// ============================================================================ captions, radios, bars
void Dine::drawSelectedRow (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (selected);
    g.fillRect (r);
}

void Dine::drawCaption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label)
{
    drawSection (g, r, label.toUpperCase());
}

void Dine::drawRadio (juce::Graphics& g, juce::Rectangle<float> r, bool on)
{
    auto dot = r.withSizeKeepingCentre (14.0f, 14.0f);
    g.setColour (on ? accent : panMark);
    g.fillEllipse (dot);
}

void Dine::drawStackedBar (juce::Graphics& g, juce::Rectangle<int> r, const std::vector<BarSlice>& slices)
{
    const float radius = juce::jmin (3.0f, r.getHeight() * 0.5f);
    auto bar = r.toFloat();
    fillRounded (g, bar, well, radius);
    juce::Path clip;
    clip.addRoundedRectangle (bar, radius);
    g.saveState();
    g.reduceClipRegion (clip);
    float x = bar.getX();
    for (const auto& s : slices)
    {
        const float w = juce::jmax (0.0f, s.share) * bar.getWidth();
        if (w <= 0.0f) continue;
        g.setColour (s.colour);
        g.fillRect (x, bar.getY(), w, bar.getHeight());
        x += w;
    }
    g.restoreState();
}

void Dine::drawDropChevron (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    juce::Path p;
    const float cx = r.getCentreX(), cy = r.getCentreY();
    p.addTriangle (cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 2.5f);
    g.setColour (c);
    g.fillPath (p);
}

// ============================================================================ DineChip
DineChip::DineChip (const juce::String& t, juce::Colour d) : juce::Button (t), label (t), dot (d)
{
    setClickingTogglesState (false);
    setWantsKeyboardFocus (false);
}

int DineChip::idealWidth() const
{
    return Dine::textWidth (Dine::text (12.0f, 500), label) + (dot.isTransparent() ? 20 : 32);
}

void DineChip::drawTrack (juce::Graphics& g, juce::Rectangle<int> r)
{
    Dine::fillRounded (g, r.toFloat(), Dine::menubar, Dine::Radius::control);
}

void DineChip::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat();
    if (on) Dine::fillRounded (g, r, Dine::selected, Dine::Radius::control);

    auto inner = getLocalBounds().reduced (dot.isTransparent() ? 10 : 8, 0);
    if (! dot.isTransparent())
    {
        auto d = inner.removeFromLeft (6).toFloat().withSizeKeepingCentre (6.0f, 6.0f);
        g.setColour (on ? dot : dot.withMultipliedAlpha (0.8f));
        g.fillEllipse (d);
        inner.removeFromLeft (6);
    }
    g.setColour (on || over ? Dine::ink : Dine::ink3);
    g.setFont (Dine::text (12.0f, 500));
    g.drawText (label, inner, juce::Justification::centred, true);
}

// ============================================================================ pills
float Dine::pillWidth (const juce::String& text, bool withIcon)
{
    return float (textWidth (caps (10.0f, 0.06f, 500), text)) + (withIcon ? 37.0f : 16.0f);
}

void Dine::drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, juce::Colour colour, Icon icon)
{
    fillRounded (g, r, mix (colour, 0.20f), Radius::pill);
    auto inner = r.reduced (8.0f, 0.0f);
    if (icon != Icon::None)
    {
        drawIcon (g, icon, inner.removeFromLeft (13.0f).withSizeKeepingCentre (13.0f, 13.0f), colour);
        inner.removeFromLeft (6.0f);
    }
    g.setColour (colour);
    g.setFont (caps (10.0f, 0.06f, 500));
    g.drawText (label, inner, juce::Justification::centredLeft);
}

// ============================================================================ PanBar
void PanBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (float (getWidth()), 4.0f);
    Dine::fillRounded (g, r, Dine::control, 2.0f);
    const float centre = r.getCentreX();
    const float x = centre + value * (r.getWidth() * 0.5f - 5.5f);
    g.setColour (Dine::panMark);
    g.fillRect (centre - 0.5f, r.getY() - 3.0f, 1.0f, r.getHeight() + 6.0f);
    const float lo = juce::jmin (centre, x), hi = juce::jmax (centre, x);
    if (hi - lo > 0.5f)
    {
        g.setColour (tint.value_or (Dine::accent));
        g.fillRoundedRectangle (juce::Rectangle<float> (lo, r.getY(), hi - lo, r.getHeight()), 2.0f);
    }
    auto knob = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ x, r.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (knob.translated (0.0f, 1.0f));
    g.setColour (isEnabled() ? Dine::ink : Dine::ink3);
    g.fillEllipse (knob);
}

void PanBar::mouseDoubleClick (const juce::MouseEvent&)
{
    value = 0.0f;
    repaint();
    if (onChange) onChange (value);
}

void PanBar::drag (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    const float half = juce::jmax (1.0f, float (getWidth()) * 0.5f - 5.5f);
    const float v = juce::jlimit (-1.0f, 1.0f, (float (e.position.x) - float (getWidth()) * 0.5f) / half);
    setValue (std::fabs (v) < 0.06f ? 0.0f : v);
    if (onChange) onChange (value);
}

// ============================================================================ DineMeter
void DineMeter::setLevels (float peakDb, float holdDb, bool clip)
{
    if (clip) clipped = true;
    // Rise at once, fall at 60 dB a second wherever the frame rate is.
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = lastMs == 0 ? 1.0f / 30.0f : juce::jlimit (0.0f, 0.25f, float (now - lastMs) / 1000.0f);
    lastMs = now;
    const float newPeak = juce::jmax (peakDb, peak - 60.0f * dt);
    const float newHold = juce::jmax (holdDb, hold - 30.0f * dt);
    if (std::abs (newPeak - peak) < 0.05f && std::abs (newHold - hold) < 0.05f) return;
    peak = newPeak; hold = newHold;
    repaint();
}

void DineMeter::setMuted (bool m)
{
    if (m == muted) return;
    muted = m;
    repaint();
}

void DineMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const bool vertical = b.getHeight() >= b.getWidth();
    const float radius = juce::jmin (2.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.5f);
    Dine::fillMeter (g, b, norm (peak), vertical, muted, radius);

    const float h = norm (hold);
    if (h > 0.001f && ! muted)
    {
        g.setColour (hold >= -0.2f ? Dine::crit : juce::Colours::white.withAlpha (0.7f));
        if (vertical) g.fillRect (b.getX(), juce::jmax (b.getY(), b.getBottom() - b.getHeight() * h - 1.0f), b.getWidth(), 1.0f);
        else          g.fillRect (juce::jmin (b.getRight() - 1.0f, b.getX() + b.getWidth() * h), b.getY(), 1.0f, b.getHeight());
    }
    if (clipped)
    {
        g.setColour (Dine::crit);
        if (vertical) g.fillRect (b.getX(), b.getY(), b.getWidth(), 2.0f);
        else          g.fillRect (b.getRight() - 2.0f, b.getY(), 2.0f, b.getHeight());
    }
}

// ============================================================================ DineButton
DineButton::DineButton (const juce::String& t, Style s) : juce::Button (t), style (s)
{
    setWantsKeyboardFocus (false);
}

int DineButton::idealWidth() const
{
    auto font = Dine::text (fontPx, caps || style == Style::Filled ? 600 : 500);
    if (caps) font = font.withExtraKerningFactor (0.04f);
    return Dine::textWidth (font, caps ? getButtonText().toUpperCase() : getButtonText())
           + padX * 2 + (icon != Dine::Icon::None ? 21 : 0);
}

void DineButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float radius = style == Style::Segment ? Dine::Radius::control : Dine::Radius::control;

    switch (style)
    {
        case Style::Filled:
            if (! isEnabled())
                Dine::fillRounded (g, r, Dine::mix (tintOr(), 0.22f, Dine::control), radius);
            else if (! tint.has_value() || *tint == Dine::accent) Dine::drawFilled (g, r, radius, over, down);
            else Dine::fillRounded (g, r, down ? tint->darker (0.18f) : over ? tint->brighter (0.08f) : *tint, radius);
            break;
        case Style::Toggle:
            if (on) Dine::fillRounded (g, r, down ? Dine::controlHot : Dine::controlOn, radius);
            else    Dine::fillRounded (g, r, down ? Dine::controlOn : over ? Dine::controlHot : (quiet ? Dine::card : Dine::control), radius);
            break;
        case Style::Standard:
            Dine::fillRounded (g, r, down ? Dine::controlOn : over ? Dine::controlHot : (quiet ? Dine::card : Dine::control), radius);
            break;
        case Style::Segment:
            if (on) Dine::fillRounded (g, r, Dine::selected, radius);
            break;
        case Style::Ghost:
            if (over || down) Dine::fillRounded (g, r, Dine::fillSoft, radius);
            break;
    }

    const bool filled = style == Style::Filled;
    juce::Colour fg;
    if (filled)               fg = isEnabled() ? (tintOr().getPerceivedBrightness() > 0.55f ? Dine::onAccent : Dine::ink) : Dine::ink3;
    else if (style == Style::Toggle)  fg = on ? Dine::ink : (over ? Dine::ink : Dine::ink2);
    else if (style == Style::Segment) fg = on ? Dine::ink : (over ? Dine::ink : Dine::ink3);
    else if (style == Style::Ghost)   fg = over ? Dine::ink : Dine::ink2;
    else                              fg = over ? Dine::ink : Dine::ink2;
    if (! isEnabled() && ! filled) fg = fg.withAlpha (Dine::disabled);

    auto content = getLocalBounds().reduced (padX, 0);
    const juce::String label = caps ? getButtonText().toUpperCase() : getButtonText();
    const int weight = filled || caps ? 600 : 500;
    auto font = Dine::text (fontPx, weight);
    if (caps) font = font.withExtraKerningFactor (0.04f);
    const int textW = Dine::textWidth (font, label);
    const int iconW = icon != Dine::Icon::None ? 21 : 0;
    auto block = content.withSizeKeepingCentre (juce::jmin (content.getWidth(), textW + iconW), content.getHeight());
    if (icon != Dine::Icon::None)
        Dine::drawIcon (g, icon, block.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), fg);
    if (iconW > 0) block.removeFromLeft (6);
    g.setColour (fg);
    g.setFont (font);
    g.drawText (label, block, juce::Justification::centredLeft);
}

// ============================================================================ DinePopup
DinePopup::DinePopup() : juce::Button ({})
{
    setWantsKeyboardFocus (false);
}

int DinePopup::idealWidth() const
{
    return Dine::textWidth (Dine::text (12.5f), value) + 40 + (dot.isTransparent() ? 0 : 15);
}

void DinePopup::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    Dine::fillRounded (g, r, down ? Dine::controlOn : over ? Dine::controlHot : Dine::control, Dine::Radius::control);
    auto inner = getLocalBounds().reduced (10, 0);
    Dine::drawDropChevron (g, inner.removeFromRight (10).toFloat(), over ? Dine::ink : Dine::ink2);
    inner.removeFromRight (6);
    if (! dot.isTransparent())
    {
        g.setColour (dot);
        g.fillEllipse (inner.removeFromLeft (7).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
        inner.removeFromLeft (8);
    }
    g.setColour (! isEnabled() ? Dine::ink4 : over ? Dine::ink : Dine::ink2);
    g.setFont (Dine::text (12.5f, 500));
    g.drawText (value, inner, juce::Justification::centredLeft, true);
}

// ============================================================================ DineNavItem
DineNavItem::DineNavItem (const juce::String& l, Dine::Icon i) : juce::Button (l), label (l), icon (i)
{
    setWantsKeyboardFocus (false);
}

void DineNavItem::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    if (selected)                     Dine::fillRounded (g, r, Dine::selected, Dine::Radius::control);
    else if (over && isEnabled())     Dine::fillRounded (g, r, Dine::card, Dine::Radius::control);

    auto inner = getLocalBounds().reduced (10, 0);
    const juce::Colour fg = ! isEnabled() ? Dine::ink4 : selected || over ? Dine::ink : Dine::ink3;
    if (icon != Dine::Icon::None)
    {
        Dine::drawIcon (g, icon, inner.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f), fg);
        inner.removeFromLeft (8);
    }
    auto right = inner;
    if (done)
    {
        Dine::drawIcon (g, Dine::Icon::Check, right.removeFromRight (13).toFloat().withSizeKeepingCentre (13.0f, 13.0f), Dine::ok);
        right.removeFromRight (4);
    }
    else if (meta.isNotEmpty())
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (10.5f));
        const int w = Dine::textWidth (Dine::mono (10.5f), meta);
        g.drawText (meta, right.removeFromRight (w), juce::Justification::centredRight);
        right.removeFromRight (4);
    }
    g.setColour (fg);
    g.setFont (Dine::text (12.5f, 500));
    g.drawText (label, right, juce::Justification::centredLeft, true);
}

// ============================================================================ DineKey
DineKey::DineKey (const juce::String& l, juce::Colour onColour)
    : juce::Button (l), letter (l), tint (onColour)
{
    setWantsKeyboardFocus (false);
}

void DineKey::setOn (bool o)
{
    if (o == on) return;
    on = o;
    repaint();
}

void DineKey::setLetter (const juce::String& l)
{
    if (l == letter) return;
    letter = l;
    repaint();
}

void DineKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const float radius = juce::jmin (Dine::Radius::chip, r.getHeight() * 0.3f);
    if (on) Dine::fillRounded (g, r, down ? tint.darker (0.15f) : tint, radius);
    else    Dine::fillRounded (g, r, down ? Dine::controlOn : over ? Dine::controlHot : Dine::control, radius);

    g.setColour (! isEnabled() ? Dine::ink4 : on ? Dine::onAccent : over ? Dine::ink : Dine::ink2);
    g.setFont (Dine::text (juce::jlimit (9.0f, 11.0f, float (getHeight()) * 0.5f), 600));
    g.drawText (letter, getLocalBounds(), juce::Justification::centred, false);
}

// ============================================================================ DinePanelTab
DinePanelTab::DinePanelTab (Side s, const juce::String& panelName)
    : juce::Button (panelName), side (s), name (panelName)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    updateTooltip();
}

void DinePanelTab::setCollapsed (bool c)
{
    if (c == collapsed) return;
    collapsed = c;
    updateTooltip();
    repaint();
}

void DinePanelTab::updateTooltip()
{
    setTooltip (collapsed ? "Show " + name.toLowerCase() + "."
                          : "Fold " + name.toLowerCase() + " away and give the width to the middle of the workspace.");
}

void DinePanelTab::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (collapsed ? (over || down ? Dine::toolbar : Dine::menubar) : (over || down ? Dine::hairSoft : juce::Colours::transparentBlack));
    g.fillRect (r);

    // The handle: a small key with a chevron pointing the way the panel will move - in when it
    // is open (fold it away), out when it is folded (bring it back).
    const bool pointsLeft = (side == Side::Left) != collapsed;
    auto key = juce::Rectangle<float> (r.getCentreX() - 7.0f, 10.0f, 14.0f, 22.0f);
    Dine::fillRounded (g, key, over || down ? Dine::controlHot : Dine::control, 4.0f);
    juce::Path chevron;
    const float cx = key.getCentreX(), cy = key.getCentreY(), w = 2.5f, h = 4.0f;
    if (pointsLeft) { chevron.startNewSubPath (cx + w, cy - h); chevron.lineTo (cx - w, cy); chevron.lineTo (cx + w, cy + h); }
    else            { chevron.startNewSubPath (cx - w, cy - h); chevron.lineTo (cx + w, cy); chevron.lineTo (cx - w, cy + h); }
    g.setColour (over ? Dine::ink : Dine::ink2);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

    if (! collapsed) return;

    // Folded: the panel's name, written down the gutter under the key.
    if (r.getHeight() > 120.0f)
    {
        juce::Graphics::ScopedSaveState save (g);
        auto below = r.withTrimmedTop (key.getBottom() + 8.0f);
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi)
                            .translated (r.getWidth(), below.getY()));
        g.setColour (over ? Dine::ink2 : Dine::ink3);
        g.setFont (Dine::caps (10.0f, 0.14f, 500));
        g.drawText (name.toUpperCase(), juce::Rectangle<float> (0.0f, 0.0f, below.getHeight(), r.getWidth()),
                    juce::Justification::centred, false);
    }
}

// ============================================================================ DineSwitch
DineSwitch::DineSwitch (const juce::String& on, const juce::String& off) : juce::Button (on), onText (on), offText (off)
{
    setWantsKeyboardFocus (false);
}

void DineSwitch::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    auto r = getLocalBounds();
    auto track = r.removeFromLeft (28).withSizeKeepingCentre (28, 16).toFloat();
    Dine::fillRounded (g, track, on ? Dine::accent : (over ? Dine::controlHot : Dine::control), 8.0f);
    auto knob = juce::Rectangle<float> (on ? track.getRight() - 14.5f : track.getX() + 1.5f, track.getY() + 1.5f, 13.0f, 13.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (knob.translated (0.0f, 1.0f));
    g.setColour (on ? Dine::onAccent : Dine::ink);
    g.fillEllipse (knob);
    r.removeFromLeft (7);
    g.setColour (on ? Dine::ink : Dine::ink3);
    g.setFont (Dine::text (11.5f));
    g.drawText (on ? onText : offText, r, juce::Justification::centredLeft);
}

// ============================================================================ DineLookAndFeel
DineLookAndFeel::DineLookAndFeel()
{
    applyPalette();
}

void DineLookAndFeel::applyPalette()
{
    setColour (juce::ResizableWindow::backgroundColourId, Dine::window);
    setColour (juce::DocumentWindow::backgroundColourId, Dine::window);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Dine::ink);
    setColour (juce::Slider::thumbColourId, Dine::ink2);
    setColour (juce::Slider::trackColourId, Dine::ink2);
    setColour (juce::Slider::backgroundColourId, Dine::well);
    setColour (juce::TextEditor::backgroundColourId, Dine::card);
    setColour (juce::TextEditor::textColourId, Dine::ink);
    setColour (juce::TextEditor::highlightColourId, Dine::accent.withAlpha (0.28f));
    setColour (juce::TextEditor::highlightedTextColourId, Dine::ink);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId, Dine::accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::textColourId, Dine::ink2);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Dine::control);
    setColour (juce::PopupMenu::highlightedTextColourId, Dine::ink);
    setColour (juce::PopupMenu::headerTextColourId, Dine::ink4);
    setColour (juce::AlertWindow::backgroundColourId, Dine::sheet);
    setColour (juce::AlertWindow::textColourId, Dine::ink);
    setColour (juce::AlertWindow::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, Dine::popover);
    setColour (juce::TooltipWindow::textColourId, Dine::ink);
    setColour (juce::ScrollBar::thumbColourId, Dine::hairStrong);
}

void DineLookAndFeel::setBipolar (juce::Slider& s, bool on) { s.getProperties().set ("dineBipolar", on); }

juce::Typeface::Ptr DineLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    return LiveMixLookAndFeel::getTypefaceForFont (f);
}

juce::Font DineLookAndFeel::getPopupMenuFont()           { return Dine::text (12.5f); }
juce::Font DineLookAndFeel::getAlertWindowTitleFont()    { return Dine::text (16.0f, 600); }
juce::Font DineLookAndFeel::getAlertWindowMessageFont()  { return Dine::text (13.0f); }
juce::Font DineLookAndFeel::getAlertWindowFont()         { return Dine::text (12.5f); }
juce::Font DineLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return Dine::text (13.0f, 500); }

void DineLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                        float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    const bool bipolar = bool (s.getProperties().getWithDefault ("dineBipolar", false));
    const bool consoleFader = bool (s.getProperties().getWithDefault ("dineFader", false));
    auto full = juce::Rectangle<float> (float (x), float (y), float (w), float (h));
    const juce::Colour capColour = s.isEnabled() ? Dine::ink2 : Dine::ink4;

    // The design's fader: a 2 px line and a flat cap (24 x 9 standing, 9 x 14 lying). The
    // line never fills - a bank of faders reads as a row of caps, not a wall of colour.
    if (consoleFader)
    {
        if (vertical)
        {
            g.setColour (Dine::hairStrong);
            g.fillRect (full.getCentreX() - 1.0f, full.getY(), 2.0f, full.getHeight());
            const float capH = 9.0f, capW = juce::jlimit (16.0f, 26.0f, full.getWidth());
            const float cy = juce::jlimit (full.getY() + capH * 0.5f, full.getBottom() - capH * 0.5f, sliderPos);
            g.setColour (capColour);
            g.fillRoundedRectangle (full.getCentreX() - capW * 0.5f, cy - capH * 0.5f, capW, capH, 3.0f);
        }
        else
        {
            g.setColour (Dine::hairStrong);
            g.fillRect (full.getX(), full.getCentreY() - 1.0f, full.getWidth(), 2.0f);
            const float capW = 9.0f, capH = juce::jlimit (10.0f, 14.0f, full.getHeight());
            const float cx = juce::jlimit (full.getX() + capW * 0.5f, full.getRight() - capW * 0.5f, sliderPos);
            g.setColour (capColour);
            g.fillRoundedRectangle (cx - capW * 0.5f, full.getCentreY() - capH * 0.5f, capW, capH, 3.0f);
        }
        return;
    }

    if (! vertical)
    {
        auto track = juce::Rectangle<float> (full.getX(), full.getCentreY() - 1.0f, full.getWidth(), 2.0f);
        g.setColour (Dine::hairStrong);
        g.fillRect (track);
        if (bipolar)
        {
            // The centre mark: "as tuned" is the middle, and the cap says how far from it.
            g.setColour (Dine::edge);
            g.fillRect (track.getCentreX() - 0.5f, full.getY(), 1.0f, full.getHeight());
        }
        else
        {
            g.setColour (capColour.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (track.getX(), track.getY(), juce::jmax (0.0f, sliderPos - track.getX()), track.getHeight()));
        }
        const float capW = 9.0f, capH = juce::jlimit (10.0f, 14.0f, full.getHeight());
        const float cx = juce::jlimit (full.getX() + capW * 0.5f, full.getRight() - capW * 0.5f, sliderPos);
        g.setColour (capColour);
        g.fillRoundedRectangle (cx - capW * 0.5f, full.getCentreY() - capH * 0.5f, capW, capH, 3.0f);
        return;
    }

    auto track = juce::Rectangle<float> (full.getCentreX() - 1.0f, full.getY(), 2.0f, full.getHeight());
    g.setColour (Dine::hairStrong);
    g.fillRect (track);
    const float capH = 9.0f, capW = juce::jlimit (16.0f, 24.0f, full.getWidth());
    const float cy = juce::jlimit (full.getY() + capH * 0.5f, full.getBottom() - capH * 0.5f, sliderPos);
    g.setColour (capColour);
    g.fillRoundedRectangle (full.getCentreX() - capW * 0.5f, cy - capH * 0.5f, capW, capH, 3.0f);
}

void DineLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    if (! juce::Desktop::canUseSemiTransparentWindows())
        g.fillAll (Dine::popover);
    auto r = juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h));
    Dine::fillRounded (g, r, Dine::popover, Dine::Radius::card);
}

void DineLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                         bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                         const juce::String& text, const juce::String& shortcutKeyText,
                                         const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        Dine::drawRule (g, area.reduced (10, 0).withHeight (1).withY (area.getCentreY()), Dine::hair);
        return;
    }

    auto r = area.reduced (6, 1);
    if (isHighlighted && isActive)
        Dine::fillRounded (g, r.toFloat(), Dine::control, Dine::Radius::chip);

    auto content = r.reduced (10, 0);
    auto tick = content.removeFromLeft (14);
    if (isTicked)
        Dine::drawIcon (g, Dine::Icon::Check, tick.toFloat().withSizeKeepingCentre (12.0f, 12.0f), Dine::accent);
    content.removeFromLeft (4);

    if (hasSubMenu)
    {
        Dine::drawIcon (g, Dine::Icon::Chevron, content.removeFromRight (12).toFloat().withSizeKeepingCentre (11.0f, 11.0f),
                        isActive ? Dine::ink2 : Dine::ink4);
        content.removeFromRight (4);
    }
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (Dine::ink4);
        g.setFont (Dine::mono (11.0f));
        const int w = Dine::textWidth (Dine::mono (11.0f), shortcutKeyText) + 6;
        g.drawText (shortcutKeyText, content.removeFromRight (w), juce::Justification::centredRight);
    }

    g.setColour (! isActive ? Dine::ink4 : isHighlighted ? Dine::ink : isTicked ? Dine::accent : Dine::ink2);
    g.setFont (Dine::text (12.5f));
    g.drawText (text, content, juce::Justification::centredLeft, true);
}

void DineLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setColour (Dine::ink4);
    g.setFont (Dine::caps (10.0f, 0.10f));
    g.drawText (name.toUpperCase(), area.reduced (16, 0), juce::Justification::centredLeft, true);
}

void DineLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 60; idealHeight = 9; return; }
    idealHeight = 28;
    idealWidth = Dine::textWidth (Dine::text (12.5f), text) + 62;
}

void DineLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                     int thumbStart, int thumbSize, bool mouseOver, bool down)
{
    if (thumbSize <= 0) return;
    auto thumb = vertical ? juce::Rectangle<float> (float (x) + float (w) * 0.5f - 2.0f, float (thumbStart) + 2.0f, 4.0f, float (thumbSize) - 4.0f)
                          : juce::Rectangle<float> (float (thumbStart) + 2.0f, float (y) + float (h) * 0.5f - 2.0f, float (thumbSize) - 4.0f, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.26f : mouseOver ? 0.18f : 0.09f));
    g.fillRoundedRectangle (thumb, 2.0f);
}

void DineLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Dine::popover);
    auto r = juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h));
    Dine::fillRounded (g, r, Dine::popover, Dine::Radius::control);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (12.0f));
    g.drawFittedText (text, r.reduced (10.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

void DineLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    const auto c = e.findColour (juce::TextEditor::backgroundColourId);
    if (c.isTransparent()) return;
    Dine::fillRounded (g, juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h)),
                       e.isEnabled() ? c : c.withMultipliedAlpha (0.4f), Dine::Radius::control);
}

void DineLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    if (! e.hasKeyboardFocus (true)) return;
    const auto c = e.findColour (juce::TextEditor::focusedOutlineColourId);
    if (c.isTransparent()) return;
    g.setColour (c);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h)).reduced (0.75f), Dine::Radius::control, 1.0f);
}

void DineLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto r = w.getLocalBounds().toFloat();
    Dine::fillRounded (g, r, Dine::sheet, Dine::Radius::window);
    layout.draw (g, textArea.toFloat());
}

} // namespace livemix
