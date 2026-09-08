#include "AppTheme.h"

namespace livemix
{

// ============================================================================ type
namespace
{
    juce::String uiFamily()
    {
        static const juce::String name = []
        {
            const auto all = juce::Font::findAllTypefaceNames();
            for (const char* candidate : { "SF Pro Text", "SF Pro", "SF Pro Display", "Helvetica Neue" })
                if (all.contains (candidate)) return juce::String (candidate);
            return juce::Font::getDefaultSansSerifFontName();
        }();
        return name;
    }

    juce::String monoFamily()
    {
        static const juce::String name = []
        {
            const auto all = juce::Font::findAllTypefaceNames();
            for (const char* candidate : { "SF Mono", "Menlo", "SFMono-Regular" })
                if (all.contains (candidate)) return juce::String (candidate);
            return juce::Font::getDefaultMonospacedFontName();
        }();
        return name;
    }

    // Nearest style the installed family actually has ("Semibold" on SF, "Bold" elsewhere).
    juce::String styleFor (const juce::String& family, int weight)
    {
        const auto styles = juce::Font::findAllTypefaceStyles (family);
        auto pick = [&] (std::initializer_list<const char*> wanted) -> juce::String
        {
            for (const char* s : wanted) if (styles.contains (s)) return juce::String (s);
            return "Regular";
        };
        if (weight >= 700) return pick ({ "Bold", "Semibold", "Medium" });
        if (weight >= 600) return pick ({ "Semibold", "Bold", "Medium" });
        if (weight >= 500) return pick ({ "Medium", "Semibold", "Regular" });
        return pick ({ "Regular" });
    }
}

juce::Font Dine::text (float px, int weight)
{
    const auto family = uiFamily();
    return juce::Font (juce::FontOptions().withName (family).withStyle (styleFor (family, weight)).withPointHeight (px));
}

juce::Font Dine::mono (float px, int weight)
{
    const auto family = monoFamily();
    return juce::Font (juce::FontOptions().withName (family).withStyle (styleFor (family, weight)).withPointHeight (px));
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

void Dine::drawCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fillColour, juce::Colour edge)
{
    fillRounded (g, r, fillColour, Radius::card);
    hairlineRounded (g, r, edge, Radius::card);
}

void Dine::drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    fillRounded (g, r, well, radius);
}

void Dine::drawFilled (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool hover, bool down)
{
    const float lift = down ? -0.06f : hover ? 0.08f : 0.0f;
    juce::ColourGradient grad (accentTop.brighter (lift), r.getCentreX(), r.getY(),
                               accentBottom.brighter (lift), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    // Inset top sheen — the macOS filled-button highlight.
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.saveState();
    g.reduceClipRegion (clip);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 0.5f);
    g.restoreState();
}

void Dine::drawStandard (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool hover, bool down)
{
    fillRounded (g, r, down ? fillHover.withMultipliedAlpha (1.4f) : hover ? fillHover : fill, radius);
    hairlineRounded (g, r, hairStrong, radius);
}

void Dine::drawSheet (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 34, { 0, 14 }).drawForRectangle (g, r.toNearestInt());
    fillRounded (g, r, sheet, radius);
    hairlineRounded (g, r, hairStrong, radius);
}

void Dine::drawRule (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c)
{
    g.setColour (c);
    g.fillRect (float (r.getX()), float (r.getY()), float (r.getWidth()), 0.5f);
}

juce::Colour Dine::levelColour (float db) noexcept
{
    return db >= -1.0f ? crit : db >= -6.0f ? warn : ok;
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

// ============================================================================ pills
float Dine::pillWidth (const juce::String& text, bool withIcon)
{
    return float (textWidth (Dine::text (11.0f, 600), text)) + (withIcon ? 37.0f : 18.0f);
}

void Dine::drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, juce::Colour colour, Icon icon)
{
    fillRounded (g, r, colour.withAlpha (0.14f), Radius::pill);
    auto inner = r.reduced (9.0f, 0.0f);
    if (icon != Icon::None)
    {
        drawIcon (g, icon, inner.removeFromLeft (13.0f).withSizeKeepingCentre (13.0f, 13.0f), colour);
        inner.removeFromLeft (6.0f);
    }
    g.setColour (colour);
    g.setFont (text (11.0f, 600));
    g.drawText (label, inner, juce::Justification::centredLeft);
}

// ============================================================================ DineMeter
void DineMeter::setLevels (float peakDb, float holdDb, bool clip)
{
    if (clip) clipped = true;
    // Rise instantly, fall ~2 dB per frame: the reading survives a frame with no audio.
    const float newPeak = juce::jmax (peakDb, peak - 2.0f);
    const float newHold = juce::jmax (holdDb, hold - 1.0f);
    if (std::abs (newPeak - peak) < 0.05f && std::abs (newHold - hold) < 0.05f) return;
    peak = newPeak; hold = newHold;
    repaint();
}

void DineMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const bool vertical = b.getHeight() >= b.getWidth();
    Dine::drawWell (g, b, 2.0f);

    const float level = norm (peak);
    if (level > 0.001f)
    {
        if (vertical)
        {
            auto lit = b.withTrimmedTop (b.getHeight() * (1.0f - level));
            if (style == Style::Bar)
            {
                g.setColour (Dine::levelColour (peak));
                g.fillRoundedRectangle (lit, 2.0f);
            }
            else
            {
                // Segments, coloured by where each one sits on the scale.
                for (float y = b.getBottom() - 3.0f; y >= lit.getY() - 0.5f; y -= 3.0f)
                {
                    const float db = -60.0f + 60.0f * ((b.getBottom() - y) / b.getHeight());
                    g.setColour (Dine::levelColour (db));
                    g.fillRect (b.getX(), y, b.getWidth(), 1.8f);
                }
            }
        }
        else
        {
            g.setColour (Dine::levelColour (peak));
            g.fillRoundedRectangle (b.withWidth (juce::jmax (2.0f, b.getWidth() * level)), 2.0f);
        }
    }

    // Peak hold.
    const float h = norm (hold);
    if (h > 0.001f)
    {
        g.setColour (hold >= -0.2f ? Dine::crit : juce::Colours::white.withAlpha (0.75f));
        if (vertical) g.fillRect (b.getX(), juce::jmax (b.getY(), b.getBottom() - b.getHeight() * h - 1.5f), b.getWidth(), 1.5f);
        else          g.fillRect (juce::jmin (b.getRight() - 1.5f, b.getX() + b.getWidth() * h), b.getY(), 1.5f, b.getHeight());
    }
    if (clipped)
    {
        g.setColour (Dine::crit);
        if (vertical) g.fillRect (b.getX(), b.getY(), b.getWidth(), 2.5f);
        else          g.fillRect (b.getRight() - 2.5f, b.getY(), 2.5f, b.getHeight());
    }
}

// ============================================================================ DineButton
DineButton::DineButton (const juce::String& t, Style s) : juce::Button (t), style (s)
{
    setWantsKeyboardFocus (false);   // Space and Return belong to the transport, not to whatever was clicked last
}

int DineButton::idealWidth() const
{
    auto font = Dine::text (fontPx, caps || style == Style::Filled ? 600 : 400);
    if (caps) font = font.withExtraKerningFactor (0.03f);
    return Dine::textWidth (font, caps ? getButtonText().toUpperCase() : getButtonText())
           + padX * 2 + (icon != Dine::Icon::None ? 21 : 0);
}

void DineButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();

    switch (style)
    {
        case Style::Filled:
            if (! isEnabled())
            {
                // A disabled default button is flat and quiet, never a bright fill.
                Dine::fillRounded (g, r, Dine::accentDeep.withAlpha (0.25f), Dine::Radius::control);
                Dine::hairlineRounded (g, r, Dine::hairStrong, Dine::Radius::control);
            }
            else Dine::drawFilled (g, r, Dine::Radius::control, over, down);
            break;
        case Style::Standard:
            Dine::drawStandard (g, r, Dine::Radius::control, over, down);
            break;
        case Style::Segment:
            if (on)
            {
                Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.16f), Dine::Radius::chip);
                Dine::hairlineRounded (g, r, juce::Colours::white.withAlpha (0.14f), Dine::Radius::chip);
            }
            break;
        case Style::Ghost:
            if (over) Dine::fillRounded (g, r, Dine::fillSoft, Dine::Radius::chip);
            break;
    }

    const bool filled = style == Style::Filled;
    juce::Colour fg = filled ? (isEnabled() ? Dine::onAccent : Dine::ink)
                             : style == Style::Segment ? (on ? Dine::ink : Dine::ink2)
                                                       : (over ? Dine::ink : style == Style::Ghost ? Dine::ink2 : Dine::ink);
    if (! isEnabled()) fg = fg.withAlpha (0.4f);

    auto content = getLocalBounds().reduced (padX, 0);
    const juce::String label = caps ? getButtonText().toUpperCase() : getButtonText();
    const int weight = filled || caps || (style == Style::Segment && on) ? 600 : 400;
    auto font = Dine::text (fontPx, weight);
    if (caps) font = font.withExtraKerningFactor (0.03f);
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
    setWantsKeyboardFocus (false);   // Space and Return belong to the transport, not to whatever was clicked last
}

int DinePopup::idealWidth() const
{
    return Dine::textWidth (Dine::text (12.5f), value) + 40;
}

void DinePopup::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    Dine::fillRounded (g, r, down || over ? Dine::fillHover : Dine::fill, Dine::Radius::chip);
    Dine::hairlineRounded (g, r, Dine::hairStrong, Dine::Radius::chip);
    auto inner = getLocalBounds().reduced (8, 0);
    Dine::drawIcon (g, Dine::Icon::UpDown, inner.removeFromRight (13).toFloat().withSizeKeepingCentre (13.0f, 13.0f), Dine::ink2);
    inner.removeFromRight (4);
    g.setColour (isEnabled() ? Dine::ink : Dine::ink3);
    g.setFont (Dine::text (12.5f));
    g.drawText (value, inner, juce::Justification::centredLeft, true);
}

// ============================================================================ DineNavItem
DineNavItem::DineNavItem (const juce::String& l, Dine::Icon i) : juce::Button (l), label (l), icon (i)
{
    setWantsKeyboardFocus (false);   // Space and Return belong to the transport, not to whatever was clicked last
}

void DineNavItem::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    if (selected)      Dine::fillRounded (g, r, Dine::accent.withAlpha (0.18f), Dine::Radius::control);
    else if (over && isEnabled()) Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.07f), Dine::Radius::control);

    auto inner = getLocalBounds().reduced (8, 0);
    const juce::Colour fg = ! isEnabled() ? Dine::ink4 : selected ? Dine::ink : Dine::ink.withAlpha (0.92f);
    Dine::drawIcon (g, icon, inner.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                    ! isEnabled() ? Dine::ink4 : selected ? Dine::accent : Dine::glyph);
    inner.removeFromLeft (9);

    auto right = inner;
    if (done)
    {
        Dine::drawIcon (g, Dine::Icon::Check, right.removeFromRight (13).toFloat().withSizeKeepingCentre (13.0f, 13.0f), Dine::accent);
        right.removeFromRight (4);
    }
    else if (meta.isNotEmpty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (11.0f));
        const int w = Dine::textWidth (Dine::mono (11.0f), meta);
        g.drawText (meta, right.removeFromRight (w), juce::Justification::centredRight);
        right.removeFromRight (4);
    }
    g.setColour (fg);
    g.setFont (Dine::text (13.0f, selected ? 600 : 400));
    g.drawText (label, right, juce::Justification::centredLeft, true);
}

// ============================================================================ DineSwitch
DineSwitch::DineSwitch (const juce::String& on, const juce::String& off) : juce::Button (on), onText (on), offText (off)
{
    setWantsKeyboardFocus (false);   // Space and Return belong to the transport, not to whatever was clicked last
}

void DineSwitch::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    auto r = getLocalBounds();
    auto track = r.removeFromLeft (28).withSizeKeepingCentre (28, 16).toFloat();
    Dine::fillRounded (g, track, on ? Dine::accentDeep : juce::Colours::white.withAlpha (over ? 0.20f : 0.14f), 8.0f);
    Dine::hairlineRounded (g, track, juce::Colours::white.withAlpha (0.12f), 8.0f);
    auto knob = juce::Rectangle<float> (on ? track.getRight() - 14.5f : track.getX() + 1.5f, track.getY() + 1.5f, 13.0f, 13.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (knob.translated (0.0f, 1.0f));
    g.setColour (juce::Colours::white);
    g.fillEllipse (knob);
    r.removeFromLeft (7);
    g.setColour (on ? Dine::ink : Dine::ink3);
    g.setFont (Dine::text (11.5f));
    g.drawText (on ? onText : offText, r, juce::Justification::centredLeft);
}

// ============================================================================ DineLookAndFeel
DineLookAndFeel::DineLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Dine::window);
    setColour (juce::DocumentWindow::backgroundColourId, Dine::window);
    setColour (juce::Label::textColourId, Dine::ink);
    setColour (juce::Slider::thumbColourId, juce::Colours::white);
    setColour (juce::Slider::trackColourId, Dine::accent);
    setColour (juce::Slider::backgroundColourId, Dine::well);
    setColour (juce::TextEditor::backgroundColourId, Dine::well);
    setColour (juce::TextEditor::textColourId, Dine::ink);
    setColour (juce::TextEditor::highlightColourId, Dine::accent.withAlpha (0.28f));
    setColour (juce::TextEditor::highlightedTextColourId, Dine::ink);
    setColour (juce::TextEditor::outlineColourId, Dine::hair);
    setColour (juce::TextEditor::focusedOutlineColourId, Dine::accent);
    setColour (juce::CaretComponent::caretColourId, Dine::accent);
    // Transparent so MenuWindow can be non-opaque (JUCE otherwise fillsAll white
    // under rounded chrome and leaves bright corner triangles).
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::textColourId, Dine::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Dine::accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::AlertWindow::backgroundColourId, Dine::sheet);
    setColour (juce::AlertWindow::textColourId, Dine::ink);
    setColour (juce::AlertWindow::outlineColourId, Dine::hairStrong);
    setColour (juce::TooltipWindow::backgroundColourId, Dine::popover);
    setColour (juce::TooltipWindow::textColourId, Dine::ink);
    setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.16f));
}

void DineLookAndFeel::setBipolar (juce::Slider& s, bool on) { s.getProperties().set ("dineBipolar", on); }

juce::Typeface::Ptr DineLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // The application uses the system face by name; only the defaults fall back to
    // the embedded family the plug-in ships with.
    return LiveMixLookAndFeel::getTypefaceForFont (f);
}

juce::Font DineLookAndFeel::getPopupMenuFont()           { return Dine::text (13.0f); }
juce::Font DineLookAndFeel::getAlertWindowTitleFont()    { return Dine::text (15.0f, 600); }
juce::Font DineLookAndFeel::getAlertWindowMessageFont()  { return Dine::text (13.0f); }
juce::Font DineLookAndFeel::getAlertWindowFont()         { return Dine::text (12.5f); }
juce::Font DineLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return Dine::text (13.0f, 500); }

void DineLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                        float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    const bool bipolar = bool (s.getProperties().getWithDefault ("dineBipolar", false));
    const bool consoleFader = bool (s.getProperties().getWithDefault ("dineFader", false));
    const juce::Colour fillColour = s.isEnabled() ? Dine::accent : Dine::ink4;

    // A console fader: a deep slot with the level filled in behind, and a moulded cap
    // wide enough to grab. The cap never leaves the slot, so the ends read as the ends.
    if (consoleFader)
    {
        const float capLong = 13.0f, capShort = juce::jmin (26.0f, float (vertical ? w : h) - 2.0f);
        auto full = juce::Rectangle<float> (float (x), float (y), float (w), float (h));
        auto slot = vertical ? full.withSizeKeepingCentre (6.0f, full.getHeight())
                             : full.withSizeKeepingCentre (full.getWidth(), 6.0f);
        Dine::drawWell (g, slot, 3.0f);
        // The travelled part of the slot is tinted, not lit: the meter is the bright thing
        // on a strip, and a bank of faders should not read as a wall of colour.
        g.setColour (fillColour.withAlpha (s.isEnabled() ? 0.45f : 0.25f));
        if (vertical)
        {
            const float top = juce::jlimit (slot.getY(), slot.getBottom(), sliderPos);
            g.fillRoundedRectangle (juce::Rectangle<float> (slot.getX(), top, slot.getWidth(), slot.getBottom() - top), 3.0f);
        }
        else
        {
            const float right = juce::jlimit (slot.getX(), slot.getRight(), sliderPos);
            g.fillRoundedRectangle (juce::Rectangle<float> (slot.getX(), slot.getY(), right - slot.getX(), slot.getHeight()), 3.0f);
        }

        const float centre = vertical ? juce::jlimit (full.getY() + capLong * 0.5f, full.getBottom() - capLong * 0.5f, sliderPos)
                                      : juce::jlimit (full.getX() + capLong * 0.5f, full.getRight() - capLong * 0.5f, sliderPos);
        auto cap = vertical ? juce::Rectangle<float> (full.getCentreX() - capShort * 0.5f, centre - capLong * 0.5f, capShort, capLong)
                            : juce::Rectangle<float> (centre - capLong * 0.5f, full.getCentreY() - capShort * 0.5f, capLong, capShort);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.0f), 3.0f);
        juce::ColourGradient capFill (juce::Colour (0xff62666e), cap.getCentreX(), cap.getY(),
                                      juce::Colour (0xff2b2d32), cap.getCentreX(), cap.getBottom(), false);
        if (! vertical) capFill = juce::ColourGradient (juce::Colour (0xff62666e), cap.getX(), cap.getCentreY(),
                                                        juce::Colour (0xff2b2d32), cap.getRight(), cap.getCentreY(), false);
        g.setGradientFill (capFill);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (cap.reduced (0.25f), 3.0f, 0.5f);
        g.setColour (juce::Colours::white.withAlpha (s.isEnabled() ? 0.85f : 0.35f));
        if (vertical) g.fillRect (cap.getX() + 2.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 4.0f, 1.0f);
        else          g.fillRect (cap.getCentreX() - 0.5f, cap.getY() + 2.0f, 1.0f, cap.getHeight() - 4.0f);
        return;
    }

    if (! vertical)
    {
        auto track = juce::Rectangle<float> (float (x), float (y) + float (h) * 0.5f - 2.0f, float (w), 4.0f);
        Dine::drawWell (g, track, 2.0f);
        const float centre = track.getCentreX();
        g.setColour (fillColour);
        if (bipolar)
        {
            const float lo = juce::jmin (centre, sliderPos), hi = juce::jmax (centre, sliderPos);
            g.fillRect (juce::Rectangle<float> (lo, track.getY(), hi - lo, track.getHeight()));
            // Centre detent, above and below the track.
            g.setColour (juce::Colours::white.withAlpha (0.28f));
            g.fillRect (centre - 0.5f, track.getY() - 4.0f, 1.0f, 4.0f);
            g.fillRect (centre - 0.5f, track.getBottom(), 1.0f, 4.0f);
        }
        else
        {
            g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), track.getY(), sliderPos - track.getX(), track.getHeight()), 2.0f);
        }
        auto knob = juce::Rectangle<float> (sliderPos - 7.5f, track.getCentreY() - 7.5f, 15.0f, 15.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (knob.translated (0.0f, 1.0f).expanded (0.5f));
        juce::ColourGradient knobFill (juce::Colour (0xfffbfbfc), knob.getCentreX(), knob.getY(),
                                       juce::Colour (0xffdcdee2), knob.getCentreX(), knob.getBottom(), false);
        g.setGradientFill (knobFill);
        g.fillEllipse (knob);
        return;
    }

    auto track = juce::Rectangle<float> (float (x) + float (w) * 0.5f - 2.0f, float (y), 4.0f, float (h));
    Dine::drawWell (g, track, 2.0f);
    g.setColour (fillColour);
    g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), sliderPos, track.getWidth(), track.getBottom() - sliderPos), 2.0f);
    auto knob = juce::Rectangle<float> (track.getCentreX() - 5.5f, sliderPos - 10.0f, 11.0f, 20.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (knob.translated (0.0f, 1.0f), 3.0f);
    juce::ColourGradient knobFill (juce::Colour (0xfffbfbfc), knob.getCentreX(), knob.getY(),
                                   juce::Colour (0xffd7d9dd), knob.getCentreX(), knob.getBottom(), false);
    g.setGradientFill (knobFill);
    g.fillRoundedRectangle (knob, 3.0f);
}

void DineLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    // When semi-transparent windows aren't available, JUCE still paints the opaque
    // peer white first — cover that before the rounded chrome.
    if (! juce::Desktop::canUseSemiTransparentWindows())
        g.fillAll (Dine::popover);

    auto r = juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h));
    Dine::fillRounded (g, r, Dine::popover, 8.0f);
    Dine::hairlineRounded (g, r, Dine::hairStrong, 8.0f);
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

    auto r = area.reduced (5, 1);
    if (isHighlighted && isActive)
        Dine::fillRounded (g, r.toFloat(), Dine::accent.withAlpha (0.35f), Dine::Radius::chip);

    auto content = r.reduced (7, 0);
    auto tick = content.removeFromLeft (16);
    if (isTicked)
        Dine::drawIcon (g, Dine::Icon::Check, tick.toFloat().withSizeKeepingCentre (12.0f, 12.0f),
                        isHighlighted ? juce::Colours::white : Dine::accent);
    content.removeFromLeft (5);

    if (hasSubMenu)
    {
        Dine::drawIcon (g, Dine::Icon::Chevron, content.removeFromRight (12).toFloat().withSizeKeepingCentre (11.0f, 11.0f),
                        isActive ? Dine::ink2 : Dine::ink4);
        content.removeFromRight (4);
    }
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        const int w = Dine::textWidth (Dine::text (12.0f), shortcutKeyText) + 6;
        g.drawText (shortcutKeyText, content.removeFromRight (w), juce::Justification::centredRight);
    }

    g.setColour (! isActive ? Dine::ink4 : isHighlighted ? juce::Colours::white : Dine::ink);
    g.setFont (Dine::text (13.0f));
    g.drawText (text, content, juce::Justification::centredLeft, true);
}

void DineLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 60; idealHeight = 9; return; }
    idealHeight = 26;
    idealWidth = Dine::textWidth (Dine::text (13.0f), text) + 62;
}

void DineLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                     int thumbStart, int thumbSize, bool mouseOver, bool down)
{
    if (thumbSize <= 0) return;
    auto thumb = vertical ? juce::Rectangle<float> (float (x) + float (w) * 0.5f - 2.5f, float (thumbStart) + 2.0f, 5.0f, float (thumbSize) - 4.0f)
                          : juce::Rectangle<float> (float (thumbStart) + 2.0f, float (y) + float (h) * 0.5f - 2.5f, float (thumbSize) - 4.0f, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.34f : mouseOver ? 0.26f : 0.16f));
    g.fillRoundedRectangle (thumb, 2.5f);
}

void DineLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    // TooltipWindow is always opaque; cover the peer so rounded chrome has no white corners.
    g.fillAll (Dine::popover);
    auto r = juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h));
    Dine::fillRounded (g, r, Dine::popover, 8.0f);
    Dine::hairlineRounded (g, r, Dine::hairStrong, 8.0f);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (12.0f));
    g.drawFittedText (text, r.reduced (10.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

void DineLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    // Honour the editor's own colour: rows in a table stay transparent until focused.
    const auto c = e.findColour (juce::TextEditor::backgroundColourId);
    if (c.isTransparent()) return;
    Dine::fillRounded (g, juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h)),
                       e.isEnabled() ? c : c.withMultipliedAlpha (0.4f), Dine::Radius::chip);
}

void DineLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h));
    if (e.hasKeyboardFocus (true))
    {
        Dine::fillRounded (g, r, juce::Colours::black.withAlpha (0.35f), Dine::Radius::chip);
        g.setColour (Dine::accent.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.75f), Dine::Radius::chip, 1.5f);
    }
    else if (! e.findColour (juce::TextEditor::backgroundColourId).isTransparent())
        Dine::hairlineRounded (g, r, Dine::hairStrong, Dine::Radius::chip);
}

void DineLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto r = w.getLocalBounds().toFloat().reduced (1.0f);
    Dine::fillRounded (g, r, Dine::sheet, 12.0f);
    Dine::hairlineRounded (g, r, Dine::hairStrong, 12.0f);
    layout.draw (g, textArea.toFloat());
}

} // namespace livemix
