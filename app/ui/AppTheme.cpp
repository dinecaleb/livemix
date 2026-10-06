#include "AppTheme.h"
#include <algorithm>
#include <map>
#include <unordered_map>
#include <cstring>
#include <tuple>
#include <cmath>

namespace livemix
{

// ============================================================================ type
// SF Pro and SF Mono, the Mac's own faces (v4, docs/design/v4); Inter and IBM Plex Mono stay
// embedded (LiveMixFonts) as the fallback and for the six plug-ins.
//
// EVERY ROLE IS BUILT ONCE, and that is not only about speed.
// `LiveMixLookAndFeel::typefaceFor` shares its typeface cache through a **stack**
// `SharedResourcePointer`, so the cache lives only while something else is holding one - a
// live LookAndFeel, usually. With none held, each call builds the cache, parses 109 KB of TTF,
// hands back a Typeface and destroys the cache again. Two Fonts built the same way then carry
// two *different* Typeface objects and do not compare equal, so anything keyed on a Font -
// JUCE's own glyph cache, and the layout cache below - stops caching anything at all.
//
// Memoising here fixes both halves and touches nothing the six plug-ins share: DINE asks for
// a role, gets the same Font object back every time, and the TTF is parsed once.
namespace
{
    // DeletedAtShutdown, not a plain function static, and the reason is the order things are
    // destroyed in. A juce::Font holds a Typeface::Ptr, and JUCE's typeface cache is itself torn
    // down when JUCE shuts down. A static map of Fonts outlives that, so it hands its typefaces
    // back to a cache whose lock has already been destroyed - "mutex lock failed: Invalid
    // argument", after every test has passed. JUCE's own glyph caches are DeletedAtShutdown for
    // exactly this reason.
    struct FontMemo final : public juce::DeletedAtShutdown
    {
        ~FontMemo() override { clearSingletonInstance(); }
        std::map<std::tuple<int, int, int, int>, juce::Font> faces;
        std::map<std::pair<bool, int>, juce::Typeface::Ptr> systemFaces;   // SF Pro / SF Mono by weight
        juce::CriticalSection lock;
        JUCE_DECLARE_SINGLETON_INLINE (FontMemo, false)
    };

    // THE v4 TYPE IS THE MAC'S OWN: SF Pro for words, SF Mono for every number (docs/design/v4).
    // Neither may be embedded, and neither needs to be - every Mac DINE runs on has both. They
    // are hidden families, so they are asked for by the names CoreText gives them
    // (".AppleSystemUIFont", ".AppleSystemUIFontMonospaced"); "SF Pro" by its public name falls
    // back to Helvetica. One Typeface per face and weight, held for the life of the app, so two
    // Fonts of the same role carry the same object and the layout cache below keeps working.
    juce::Typeface::Ptr systemFace (bool monospaced, int weight)
    {
        // Held by the memo, which is DeletedAtShutdown for the reason given above: a static map
        // of typefaces outlives JUCE's typeface cache and fails on its lock at exit.
        const int w = weight >= 700 ? 700 : weight >= 600 ? 600 : weight >= 500 ? 500 : 400;
        auto* memo = FontMemo::getInstance();
        juce::Typeface::Ptr spare;
        auto& face = memo != nullptr ? memo->systemFaces[{ monospaced, w }] : spare;
        if (face == nullptr)
        {
            const char* style = w == 700 ? "Bold" : w == 600 ? "Semibold" : w == 500 ? "Medium" : "Regular";
            face = juce::Typeface::createSystemTypefaceFor (
                juce::Font (juce::FontOptions (monospaced ? ".AppleSystemUIFontMonospaced" : ".AppleSystemUIFont", style, 13.0f)));
            if (face == nullptr)   // not a Mac, or a Mac that renamed its system face: the embedded pair
                face = monospaced ? LiveMixLookAndFeel::mono (13.0f, weight, 0.0f).getTypefacePtr()
                                  : LiveMixLookAndFeel::inter (13.0f, weight, 0.0f).getTypefacePtr();
        }
        return face;
    }

    juce::Font memoisedFont (int kind, float px, int weight, float tracking)
    {
        const std::tuple<int, int, int, int> key { kind, juce::roundToInt (px * 100.0f), weight,
                                                   juce::roundToInt (tracking * 1000.0f) };
        const auto build = [&]
        {
            return juce::Font (juce::FontOptions().withTypeface (systemFace (kind == 1, weight))
                                                  .withPointHeight (px))
                       .withExtraKerningFactor (kind == 1 ? 0.0f : tracking);
        };

        auto* memo = FontMemo::getInstance();
        if (memo == nullptr) return build();          // past shutdown: correct, just uncached

        const juce::ScopedLock sl (memo->lock);
        if (const auto found = memo->faces.find (key); found != memo->faces.end()) return found->second;
        return memo->faces.emplace (key, build()).first->second;
    }
}

// ---------------------------------------------------------------- text size
// One number, multiplied into every px on its way to a face. It is deliberately not a
// component transform: scaling the window would scale the meters, the fader travel and the
// strip widths along with the words, and the whole point is that a 32-channel console still
// shows 32 channels. The memo above is keyed on the *scaled* size, so the three sizes coexist
// in it and switching back to Standard costs nothing.
namespace { float gTextScale = 1.0f; }

void Dine::setTextScale (float scale)
{
    const auto wanted = juce::jlimit (1.0f, 2.0f, scale);
    if (std::abs (wanted - gTextScale) < 0.0005f) return;
    gTextScale = wanted;
    // Every role is a different size now, so every layout held is for a face nothing will ask
    // for again. (A theme change needs no such thing: a colour is not baked into a layout.)
    clearTextCache();
}

float Dine::textScale() { return gTextScale; }

juce::Font Dine::text (float px, int weight)
{
    return memoisedFont (0, px * gTextScale, weight, 0.0f);
}

juce::Font Dine::mono (float px, int weight)
{
    return memoisedFont (1, px * gTextScale, weight, 0.0f);
}

juce::Font Dine::caps (float px, float tracking, int weight)
{
    return memoisedFont (0, px * gTextScale, weight, tracking);
}

// MEASURING A STRING IS SHAPING IT, and the chrome measures the same strings every paint: the
// status line's sentences, the toolbar's pills, a chip row laying itself out, a column fitting a
// name. Uncached, that was most of a whole-window repaint in v4 (the status line alone cost
// 1.8 ms). So widths are remembered the way layouts are: two generations, the live one checked
// first, the old one dropped when the live one fills - bounded, and nothing to bookkeep. The
// fonts DINE draws with are memoised, so a typeface pointer and a size identify one exactly.
namespace
{
    struct WidthKey
    {
        const void* face = nullptr;
        juce::uint32 height = 0, kerning = 0, scale = 0;
        juce::String text;
        bool operator== (const WidthKey& o) const
        {
            return face == o.face && height == o.height && kerning == o.kerning && scale == o.scale && text == o.text;
        }
    };
    struct WidthKeyHash
    {
        size_t operator() (const WidthKey& k) const noexcept
        {
            size_t h = size_t (k.text.hashCode64());
            const auto mix = [&h] (size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
            mix (size_t (reinterpret_cast<juce::pointer_sized_uint> (k.face)));
            mix (k.height); mix (k.kerning); mix (k.scale);
            return h;
        }
    };
    struct WidthCache final : public juce::DeletedAtShutdown
    {
        ~WidthCache() override { clearSingletonInstance(); }
        static constexpr size_t kLimit = 6000;
        std::unordered_map<WidthKey, int, WidthKeyHash> live, old;
        juce::CriticalSection lock;
        void clear() { live.clear(); old.clear(); }
        JUCE_DECLARE_SINGLETON_INLINE (WidthCache, false)
    };

    juce::uint32 bitsOf (float v) noexcept { juce::uint32 b; std::memcpy (&b, &v, sizeof b); return b; }
}

int Dine::textWidth (const juce::Font& f, const juce::String& t)
{
    const auto measure = [&] { return int (std::ceil (juce::GlyphArrangement::getStringWidth (f, t))) + 2; };
    auto* cache = WidthCache::getInstance();
    if (cache == nullptr || t.isEmpty()) return measure();

    WidthKey key { f.getTypefacePtr().get(), bitsOf (f.getHeight()), bitsOf (f.getExtraKerningFactor()),
                   bitsOf (f.getHorizontalScale()), t };
    const juce::ScopedLock sl (cache->lock);
    if (const auto it = cache->live.find (key); it != cache->live.end()) return it->second;
    if (const auto it = cache->old.find (key); it != cache->old.end())
    {
        const int w = it->second;
        cache->live.emplace (std::move (key), w);
        return w;
    }
    if (cache->live.size() >= WidthCache::kLimit) { cache->old = std::move (cache->live); cache->live.clear(); }
    const int w = measure();
    cache->live.emplace (std::move (key), w);
    return w;
}

// ============================================================================ drawing text
// The layout cache. Laying a string out means shaping it - HarfBuzz, kerning, ligature
// suppression where the design asks for tracking, then a position per glyph - and it is the
// same answer every frame for every label that has not changed. JUCE caches it too, in 128
// entries shared by the whole window, which is fewer than one workspace of DINE uses: 48
// strips of a dozen labels each is 576 strings before the chrome has drawn anything. Past 128
// the cache stops being a cache and every paint re-shapes what the paint before it shaped.
//
// Two generations rather than a least-recently-used list. A lookup checks the live generation
// and then the one behind it, promoting what it finds; when the live one is full the old one is
// dropped and it becomes the old one. That keeps whatever has been drawn in the last two
// sweeps, costs one hash and no bookkeeping, and cannot grow past twice the limit - which
// matters, because a readout like "-12.4 dB" is a new string every frame and a cache that only
// ever grew would hold every number the meter has ever shown.
namespace
{
    struct TextLayoutKey
    {
        juce::Font font;
        juce::String text;
        float width = 0.0f, height = 0.0f, minScale = 0.0f;
        int justification = 0, maxLines = 1;
        bool fitted = false, ellipses = true;

        bool operator== (const TextLayoutKey& o) const
        {
            return justification == o.justification && maxLines == o.maxLines
                && fitted == o.fitted && ellipses == o.ellipses
                && juce::exactlyEqual (width, o.width) && juce::exactlyEqual (height, o.height)
                && juce::exactlyEqual (minScale, o.minScale)
                && text == o.text && font == o.font;
        }
    };

    struct TextLayoutKeyHash
    {
        size_t operator() (const TextLayoutKey& k) const noexcept
        {
            size_t h = size_t (k.text.hashCode64());
            const auto mix = [&h] (size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
            // The font is hashed on its size alone and compared in full on equality: two roles
            // at the same size land in the same bucket and are told apart there, which is
            // cheaper than asking a Font for its typeface on every label.
            mix (size_t (std::lround (double (k.font.getHeight()) * 64.0)));
            mix (size_t (std::lround (double (k.width) * 4.0)));
            mix (size_t (std::lround (double (k.height) * 4.0)));
            mix (size_t (k.justification) * 131u + size_t (k.maxLines) * 17u
                 + size_t (k.fitted) * 3u + size_t (k.ellipses));
            return h;
        }
    };

    class TextLayoutCache final : public juce::DeletedAtShutdown
    {
    public:
        ~TextLayoutCache() override { clearSingletonInstance(); }

        // Room for a 48-channel console and the chrome around it, twice over. Each entry is a
        // short string's worth of positioned glyphs, so the whole cache is a few megabytes at
        // its fullest - and it is only ever as full as the window is busy.
        static constexpr size_t kPerGeneration = 4096;

        // Returns the layout, building it if this is the first time anyone has asked. `build`
        // is only called on a miss.
        template <typename Build>
        const juce::GlyphArrangement& get (const TextLayoutKey& key, Build&& build)
        {
            if (const auto found = live.find (key); found != live.end())
            {
                ++hits;
                return found->second;
            }

            if (const auto found = previous.find (key); found != previous.end())
            {
                ++hits;
                return live.emplace (key, std::move (found->second)).first->second;
            }

            ++misses;

            if (live.size() >= kPerGeneration)
            {
                previous = std::move (live);
                live.clear();
            }

            return live.emplace (key, build()).first->second;
        }

        void clear() { live.clear(); previous.clear(); }
        long long hitCount() const { return hits; }
        long long missCount() const { return misses; }
        size_t size() const { return live.size() + previous.size(); }
        void resetCounts() { hits = 0; misses = 0; }

        JUCE_DECLARE_SINGLETON_INLINE (TextLayoutCache, false)

    private:
        std::unordered_map<TextLayoutKey, juce::GlyphArrangement, TextLayoutKeyHash> live, previous;
        long long hits = 0, misses = 0;
    };

    // Painting is the message thread's job and this cache is its own. A try-lock rather than a
    // lock so that anything which ever does render off it - a thumbnail, a test rig - draws
    // uncached instead of waiting behind a paint.
    // The lock outlives the cache on purpose: it is a plain static with no JUCE object in it,
    // so it is safe at any point, and every use checks the cache for null first.
    juce::CriticalSection& textLayoutLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }

    // The clipping audit. Off by default and free when it is off: one bool on the way into
    // every laid-out string. On, it measures the string the way the ellipsis path would and
    // keeps the ones that lost characters, deduplicated by text and cell width.
    struct ClipAudit
    {
        bool on = false;
        juce::String scope;
        std::vector<Dine::ClippedText> found;
    };

    ClipAudit& clipAudit()
    {
        static ClipAudit audit;
        return audit;
    }

    void noteIfClipped (const TextLayoutKey& key)
    {
        auto& audit = clipAudit();
        if (! audit.on || key.width <= 0.0f || key.text.isEmpty()) return;
        // Fitted text squeezes before it cuts, so what it needs is what it needs after the
        // squeeze. Past one line it wraps as well, and where a word lands is JUCE's business
        // rather than something worth guessing at here.
        float wanted = juce::GlyphArrangement::getStringWidth (key.font, key.text);
        if (key.fitted)
        {
            if (key.maxLines != 1) return;
            wanted *= key.minScale > 0.0f ? key.minScale : juce::Font::getDefaultMinimumHorizontalScaleFactor();
        }
        else if (! key.ellipses) return;
        if (wanted <= key.width + 0.5f) return;
        for (auto& c : audit.found)
            if (c.text == key.text && std::abs (c.available - key.width) < 0.5f)
            {
                c.wanted = juce::jmax (c.wanted, wanted);
                return;
            }
        audit.found.push_back ({ key.text, audit.scope, key.width, wanted });
    }

    void drawLayout (juce::Graphics& g, const TextLayoutKey& key, juce::Point<float> at)
    {
        noteIfClipped (key);

        const auto build = [&key]
        {
            juce::GlyphArrangement a;
            if (key.fitted)
            {
                a.addFittedText (key.font, key.text, 0.0f, 0.0f, key.width, key.height,
                                 juce::Justification (key.justification), key.maxLines, key.minScale);
            }
            else
            {
                a.addCurtailedLineOfText (key.font, key.text, 0.0f, 0.0f, key.width, key.ellipses);
                a.justifyGlyphs (0, a.getNumGlyphs(), 0.0f, 0.0f, key.width, key.height,
                                 juce::Justification (key.justification));
            }
            return a;
        };

        const juce::ScopedTryLock tryLock (textLayoutLock());
        auto* cache = tryLock.isLocked() ? TextLayoutCache::getInstance() : nullptr;

        if (cache == nullptr)
        {
            build().draw (g, juce::AffineTransform::translation (at.x, at.y));
            return;
        }

        cache->get (key, build).draw (g, juce::AffineTransform::translation (at.x, at.y));
    }
}

void Dine::drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                     juce::Justification justification, bool useEllipsesIfTooBig)
{
    if (text.isEmpty() || ! g.clipRegionIntersects (area.getSmallestIntegerContainer()))
        return;

    TextLayoutKey key;
    key.font = g.getCurrentFont();
    key.text = text;
    key.width = area.getWidth();
    key.height = area.getHeight();
    key.justification = justification.getFlags();
    key.ellipses = useEllipsesIfTooBig;
    drawLayout (g, key, area.getPosition());
}

void Dine::drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                     juce::Justification justification, bool useEllipsesIfTooBig)
{
    drawText (g, text, area.toFloat(), justification, useEllipsesIfTooBig);
}

void Dine::drawText (juce::Graphics& g, const juce::String& text,
                     int x, int y, int width, int height,
                     juce::Justification justification, bool useEllipsesIfTooBig)
{
    drawText (g, text, juce::Rectangle<int> (x, y, width, height).toFloat(),
              justification, useEllipsesIfTooBig);
}

void Dine::drawFittedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                           juce::Justification justification, int maximumNumberOfLines,
                           float minimumHorizontalScale)
{
    if (text.isEmpty() || area.isEmpty() || ! g.clipRegionIntersects (area))
        return;

    TextLayoutKey key;
    key.font = g.getCurrentFont();
    key.text = text;
    key.width = float (area.getWidth());
    key.height = float (area.getHeight());
    key.justification = justification.getFlags();
    key.maxLines = juce::jmax (1, maximumNumberOfLines);
    key.minScale = minimumHorizontalScale;
    key.fitted = true;
    drawLayout (g, key, area.getPosition().toFloat());
}

void Dine::drawFittedText (juce::Graphics& g, const juce::String& text,
                           int x, int y, int width, int height,
                           juce::Justification justification, int maximumNumberOfLines,
                           float minimumHorizontalScale)
{
    drawFittedText (g, text, juce::Rectangle<int> (x, y, width, height),
                    justification, maximumNumberOfLines, minimumHorizontalScale);
}

Dine::TextCacheStats Dine::textCacheStats()
{
    const juce::ScopedLock lock (textLayoutLock());
    auto* cache = TextLayoutCache::getInstance();
    if (cache == nullptr) return { 0, 0, 0 };
    return { cache->hitCount(), cache->missCount(), (long long) cache->size() };
}

void Dine::resetTextCacheStats()
{
    const juce::ScopedLock lock (textLayoutLock());
    if (auto* cache = TextLayoutCache::getInstance()) cache->resetCounts();
}

void Dine::clearTextCache()
{
    {
        const juce::ScopedLock lock (textLayoutLock());
        if (auto* cache = TextLayoutCache::getInstance()) cache->clear();
    }
    if (auto* widths = WidthCache::getInstance()) { const juce::ScopedLock sl (widths->lock); widths->clear(); }
}

void Dine::beginTextClipAudit()
{
    auto& audit = clipAudit();
    audit.found.clear();
    audit.on = true;
    // Every string already laid out would be drawn from the cache and never measured again,
    // so the audit would see a page it had not walked as clean.
    clearTextCache();
}

void Dine::endTextClipAudit()
{
    clipAudit().on = false;
}

void Dine::setTextClipScope (const juce::String& where)
{
    clipAudit().scope = where;
}

juce::String Dine::shortPath (const juce::File& file, const juce::Font& font, int width)
{
    static const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    auto full = file.getFullPathName();
    if (home.isNotEmpty() && full.startsWith (home)) full = "~" + full.substring (home.length());
    if (width <= 0 || textWidth (font, full) <= width) return full;

    auto parts = juce::StringArray::fromTokens (full, juce::File::getSeparatorString(), {});
    parts.removeEmptyStrings();
    if (parts.isEmpty()) return full;
    juce::String kept = parts[parts.size() - 1];
    for (int i = parts.size() - 2; i >= 0; --i)
    {
        const auto next = parts[i] + juce::File::getSeparatorString() + kept;
        if (textWidth (font, ellipsis + juce::File::getSeparatorString() + next) > width) break;
        kept = next;
    }
    return ellipsis + juce::File::getSeparatorString() + kept;
}

std::vector<Dine::ClippedText> Dine::textClipReport()
{
    auto out = clipAudit().found;
    std::sort (out.begin(), out.end(), [] (const ClippedText& a, const ClippedText& b)
    {
        const float sa = a.wanted - a.available, sb = b.wanted - b.available;
        return juce::exactlyEqual (sa, sb) ? a.text < b.text : sa > sb;
    });
    return out;
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

// v4: white at .06 on whatever is under it, 8 pt corners; the chosen segment lifts inside it.
void Dine::drawSegmentTrack (juce::Graphics& g, juce::Rectangle<int> r)
{
    fillRounded (g, r.toFloat(), juce::Colours::white.withAlpha (0.06f), 8.0f);
}

// A section caption. In v3 the only capitals in the product are its verbs, so a caption that
// arrives in capitals is written back in sentence case here rather than at three hundred call
// sites - and the words that really are verbs, or initialisms, keep their capitals.
juce::String Dine::sectionCase (const juce::String& label)
{
    static const char* kept[] = { "DINE", "TUNE", "MIX", "LIVE", "SAFE", "BYPASS", "RE-TUNE", "KEEP", "REVERT",
                                  "BEFORE", "AFTER", "CHANNEL", "REFERENCE", "MATCH", "TO", "EQ", "FX", "BGV",
                                  "AFL", "PFL", "LUFS", "DIM", "MUTE", "SOLO", "R", "A", "M", "S", "L", "PA", "AI" };
    juce::StringArray words;
    words.addTokens (label, " ", "");
    bool first = true;
    juce::String out;
    for (auto word : words)
    {
        if (word.isEmpty()) continue;
        juce::String written = word;
        const bool isCaps = word == word.toUpperCase() && word.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        if (isCaps)
        {
            bool keep = false;
            for (const char* k : kept) if (word == juce::String (k)) { keep = true; break; }
            // The product's two-word verbs keep their capitals as a pair; a lone word that
            // happens to match one of them ("MIX HEALTH") does not.
            if (! keep) written = first ? word.substring (0, 1) + word.substring (1).toLowerCase()
                                        : word.toLowerCase();
        }
        else if (first) written = written.substring (0, 1).toUpperCase() + written.substring (1);
        out += (out.isEmpty() ? juce::String() : juce::String (" ")) + written;
        first = false;
    }
    return out.isEmpty() ? label : out;
}

void Dine::drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label)
{
    g.setColour (ink3);
    g.setFont (text (12.0f, 600));
    Dine::drawText (g, sectionCase (label), r, juce::Justification::centredLeft, true);
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
    Dine::drawText (g, label, r, juce::Justification::centred, true);
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
        case MixBus::Lead:     return busLead;
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
        { "controlHot", &controlHot }, { "controlOn", &controlOn }, { "sheet", &sheet }, { "popover", &popover }, { "inset", &inset }, { "deep", &deep },
        { "refuse", &refuse }, { "recGround", &recGround }, { "soloGround", &soloGround }, { "editGround", &editGround },
        { "hairSoft", &hairSoft }, { "hair", &hair }, { "hairStrong", &hairStrong }, { "edge", &edge }, { "fill", &fill },
        { "fillHover", &fillHover }, { "fillSoft", &fillSoft }, { "well", &well },
        { "ink", &ink }, { "ink2", &ink2 }, { "ink3", &ink3 }, { "ink4", &ink4 }, { "glyph", &glyph }, { "panMark", &panMark },
        { "accent", &accent }, { "accentHover", &accentHover }, { "accentDeep", &accentDeep }, { "onAccent", &onAccent },
        { "focusRing", &focusRing },
        { "ok", &ok }, { "hot", &hot }, { "warn", &warn }, { "crit", &crit }, { "monitor", &monitor },
        { "keyMute", &keyMute }, { "keySolo", &keySolo }, { "keyRec", &keyRec }, { "keyMon", &keyMon },
        { "keyFx", &keyFx },
        { "busDrums", &busDrums }, { "busBass", &busBass }, { "busMusic", &busMusic }, { "busVocals", &busVocals },
        { "busLead", &busLead },
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

// A theme change moves no size, so a repaint is all it needs. A TEXT SIZE change does move
// sizes - every page measures its own labels - and JUCE only calls `resized()` when a
// component's bounds actually change, which they do not here. So the tree is told to lay
// itself out again, deepest last, the way a window resize would.
void Dine::relayoutTree (juce::Component& root)
{
    root.resized();
    for (auto* child : root.getChildren())
        if (child != nullptr) relayoutTree (*child);
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

        // ---------------------------------------------------------------- the v3 set
        // Transcribed straight from the design's `Icon` component (61:9083): a 16 pt box at a
        // 1.4 pt stroke, round caps and joins. The numbers below are that path data scaled by
        // 1.25 into the 20 pt box every icon here lives in, and the weight is 1.75 so that the
        // stroke lands back on 1.4 pt once `drawIcon` scales a 16 pt icon down again.
        static const std::vector<IconStroke> v3Sessions { { "M2.5 5H17.5M2.5 10H17.5M2.5 15H12.5", 1.75f, false } };
        static const std::vector<IconStroke> v3Device { { "M7.5 8.75V12.5M10 7.5V12.5M12.5 10V12.5M3.75 3.75H16.25V16.25H3.75V3.75Z", 1.75f, false } };
        static const std::vector<IconStroke> v3Inputs { { "M10 2.5V11.25M6.25 7.5C6.25 13.75 13.75 13.75 13.75 7.5M10 13.75V17.5M6.25 17.5H13.75", 1.75f, false } };
        static const std::vector<IconStroke> v3Purpose { { "M2.5 10C2.5 5.875 5.875 2.5 10 2.5C14.12 2.5 17.5 5.875 17.5 10C17.5 14.12 14.12 17.5 10 17.5C5.875 17.5 2.5 14.12 2.5 10Z", 1.75f, false } };
        static const std::vector<IconStroke> v3Tracks { { "M2.5 5H11.25M6.25 10H17.5M2.5 15H13.75", 1.75f, false } };
        static const std::vector<IconStroke> v3Mixer { { "M5 2.5V17.5M10 2.5V17.5M15 2.5V17.5M2.5 12.5H7.5M7.5 6.25H12.5M12.5 13.75H17.5", 1.75f, false } };
        static const std::vector<IconStroke> v3Tune { { "M2.5 15C6.25 5 8.75 5 10 10C11.25 15 13.75 15 17.5 5", 1.75f, false } };
        static const std::vector<IconStroke> v3Live { { "M10 8.125V11.88M5.625 5.625C3.125 8.125 3.125 11.88 5.625 14.38M14.38 5.625C16.88 8.125 16.88 11.88 14.38 14.38", 1.75f, false } };
        static const std::vector<IconStroke> v3Inspector { { "M3.75 8.75H16.25M8.75 8.75V16.25M3.75 3.75H16.25V16.25H3.75V3.75Z", 1.75f, false } };
        static const std::vector<IconStroke> v3Sidebar { { "M7.5 3.75V16.25M2.5 3.75H17.5V16.25H2.5V3.75Z", 1.75f, false } };
        static const std::vector<IconStroke> v3Chat { { "M16.25 3.75H3.75V12.5H5V16.25L8.75 12.5H16.25V3.75Z", 1.75f, false } };
        static const std::vector<IconStroke> v3Lock { { "M6.875 8.75V6.25C6.875 3.125 13.12 3.125 13.12 6.25V8.75M5 8.75H15V16.25H5V8.75Z", 1.75f, false } };
        static const std::vector<IconStroke> v3Window { { "M6.25 2.5H17.5V13.75M2.5 6.25H13.75V17.5H2.5V6.25Z", 1.75f, false } };
        static const std::vector<IconStroke> v3Chevron { { "M6.25 7.5L10 11.25L13.75 7.5", 1.75f, false } };
        static const std::vector<IconStroke> v3Close { { "M5 5L15 15M15 5L5 15", 1.75f, false } };
        static const std::vector<IconStroke> v3Headphones { { "M3.75 13.75V10C3.75 3.75 16.25 3.75 16.25 10V13.75M2.5 12.5H6.25V17.5H2.5V12.5ZM13.75 12.5H17.5V17.5H13.75V12.5Z", 1.75f, false } };

        // ---------------------------------------------------------------- the v4 sidebar set
        // The mockup's sidebar glyphs (docs/design/v4): small geometric marks, a solid part where
        // the mockup fills one, a 1.5 stroke in the 20 pt box everywhere else.
        static const std::vector<IconStroke> v4Sessions { { "M6 4.5h8a3 3 0 0 1 3 3v5a3 3 0 0 1-3 3H6a3 3 0 0 1-3-3v-5a3 3 0 0 1 3-3z", 1.5f, false } };
        static const std::vector<IconStroke> v4Purpose {
            { "M10 3.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13", 1.5f, false },
            { "M10 3.5a6.5 6.5 0 0 0 0 13z", 1.0f, true } };
        static const std::vector<IconStroke> v4Routing {
            { "M5 7.4a2.6 2.6 0 1 0 0 5.2a2.6 2.6 0 1 0 0-5.2", 1.0f, true },
            { "M7.6 10h4.8M15 7.4a2.6 2.6 0 1 0 0 5.2a2.6 2.6 0 1 0 0-5.2", 1.5f, false } };
        static const std::vector<IconStroke> v4Check { { "M4 11.5h3v5H4zM8.5 8h3v8.5h-3zM13 4.5h3v12h-3z", 1.0f, true } };
        static const std::vector<IconStroke> v4Mixer { { "M5.5 6v10M10 9v7M14.5 4v12", 2.0f, false } };
        static const std::vector<IconStroke> v4Tune {
            { "M10 3.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13", 1.5f, false },
            { "M10 8a2 2 0 1 0 0 4a2 2 0 1 0 0-4", 1.0f, true } };
        static const std::vector<IconStroke> v4Inspector { { "M5 4h10a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM9 4v12", 1.5f, false } };
        static const std::vector<IconStroke> v4Favourite { { "M10 3.2l6.8 6.8-6.8 6.8-6.8-6.8z", 1.5f, false } };
        static const std::vector<IconStroke> v4History {
            { "M10 3.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13", 1.5f, false },
            { "M10 6.5V10l2.5 1.6", 1.5f, false } };
        static const std::vector<IconStroke> v4Live {
            { "M10 3.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13", 1.5f, false },
            { "M10 7a3 3 0 1 0 0 6a3 3 0 1 0 0-6", 1.0f, true } };
        static const std::vector<IconStroke> v4Setlist {
            { "M3.5 3.5h5.5v5.5H3.5z", 1.0f, true },
            { "M11 3.5h5.5v5.5H11zM3.5 11h5.5v5.5H3.5zM11 11h5.5v5.5H11z", 1.4f, false } };
        static const std::vector<IconStroke> v4Tracks { { "M3 5h14M3 10h10M3 15h12", 1.8f, false } };
        static const std::vector<IconStroke> v4Export { { "M10 3.5v9M6.5 9l3.5 3.5 3.5-3.5M4 16.5h12", 1.5f, false } };

        switch (icon)
        {
            case Dine::Icon::NavSessions:  return v4Sessions;
            case Dine::Icon::NavPurpose:   return v4Purpose;
            case Dine::Icon::NavRouting:   return v4Routing;
            case Dine::Icon::NavCheck:     return v4Check;
            case Dine::Icon::NavMixer:     return v4Mixer;
            case Dine::Icon::NavTune:      return v4Tune;
            case Dine::Icon::NavInspector: return v4Inspector;
            case Dine::Icon::NavFavourite: return v4Favourite;
            case Dine::Icon::NavHistory:   return v4History;
            case Dine::Icon::NavLive:      return v4Live;
            case Dine::Icon::NavSetlist:   return v4Setlist;
            case Dine::Icon::NavTracks:    return v4Tracks;
            case Dine::Icon::NavExport:    return v4Export;
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
            case Dine::Icon::Chevron:  return v3Chevron;
            case Dine::Icon::UpDown:   return updown;
            case Dine::Icon::Dash:     return dash;
            case Dine::Icon::Bus:      return bus;
            case Dine::Icon::Sidebar:  return v3Sidebar;
            case Dine::Icon::Search:   return search;
            case Dine::Icon::Chat:     return v3Chat;
            case Dine::Icon::Shield:   return shield;
            case Dine::Icon::Sessions:      return v3Sessions;
            case Dine::Icon::DeviceNav:     return v3Device;
            case Dine::Icon::Inputs:        return v3Inputs;
            case Dine::Icon::Purpose:       return v3Purpose;
            case Dine::Icon::TracksNav:     return v3Tracks;
            case Dine::Icon::MixerNav:      return v3Mixer;
            case Dine::Icon::TuneNav:       return v3Tune;
            case Dine::Icon::LiveNav:       return v3Live;
            case Dine::Icon::InspectorNav:  return v3Inspector;
            case Dine::Icon::Lock:          return v3Lock;
            case Dine::Icon::WindowNav:     return v3Window;
            case Dine::Icon::Close:         return v3Close;
            case Dine::Icon::Headphones:    return v3Headphones;
            case Dine::Icon::None:
            default:                   return empty;
        }
    }

    // Parsed once per icon; the paths are plain geometry in the 20 x 20 box.
    struct ParsedIcon { std::vector<std::pair<juce::Path, IconStroke>> parts; };

    const ParsedIcon& parsedIcon (Dine::Icon icon)
    {
        static std::array<std::unique_ptr<ParsedIcon>, 64> cache;   // past the last Icon
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
                     ChannelRole::RackTom, ChannelRole::FloorTom, ChannelRole::Overhead, ChannelRole::OverheadLeft, ChannelRole::OverheadRight, ChannelRole::Room, ChannelRole::DrumBus,
                     ChannelRole::DrumPad } },
        { "Bass",  { ChannelRole::BassDI, ChannelRole::BassAmp, ChannelRole::SynthBass } },
        { "Music", { ChannelRole::Piano, ChannelRole::ElectricPiano, ChannelRole::Organ, ChannelRole::SynthPad, ChannelRole::SynthLead,
                     ChannelRole::AcousticGuitar, ChannelRole::ElectricGuitarClean, ChannelRole::ElectricGuitarDrive,
                     ChannelRole::SaxAlto, ChannelRole::SaxTenor, ChannelRole::SaxBari } },
        { "Vocals", { ChannelRole::LeadVocal, ChannelRole::BackingVocal, ChannelRole::Choir } },
        // Speaking microphones are their own group in the mix, so they are their own group here:
        // whoever assigns the inputs finds the pastor's microphone among the other speaking
        // microphones, not among the singers - and picks what he is speaking into while they
        // are there, because that is what decides the chain.
        { "Speech", { ChannelRole::Speech, ChannelRole::SpeechLapel, ChannelRole::SpeechHeadset,
                      ChannelRole::SpeechHandheld, ChannelRole::SpeechLectern } },
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
        // What it is spoken into: a lapel on the chest and a handheld at the mouth are not
        // the same microphone, and the chain DINE builds for them is not the same either.
        case ChannelRole::SpeechLapel:    return "Lapel / lavalier";
        case ChannelRole::SpeechHeadset:  return "Headset / earset";
        case ChannelRole::SpeechHandheld: return "Handheld (roving)";
        case ChannelRole::SpeechLectern:  return "Lectern / pulpit";
        case ChannelRole::Overhead:  return "Overheads (stereo pair)";
        case ChannelRole::DrumBus:   return "Drum mix (stereo, from the console)";
        case ChannelRole::BassDI:    return "Bass (DI)";
        case ChannelRole::BassAmp:   return "Bass (amp mic)";
        case ChannelRole::CrowdMic:  return "Crowd / congregation";
        case ChannelRole::AmbienceMic: return "Room ambience";
        case ChannelRole::SaxAlto:   return "Saxophone (alto)";
        case ChannelRole::SaxTenor:  return "Saxophone (tenor)";
        case ChannelRole::SaxBari:   return "Saxophone (baritone)";
        case ChannelRole::DrumPad:   return "Drum pad (SPD / samples)";
        default:                     return channelRoleName (r);
    }
}

Dine::Icon Dine::iconForRole (ChannelRole r) noexcept
{
    switch (r)
    {
        case ChannelRole::KickIn: case ChannelRole::KickOut: case ChannelRole::SnareTop:
        case ChannelRole::SnareBottom: case ChannelRole::RackTom: case ChannelRole::FloorTom:
        case ChannelRole::DrumBus: case ChannelRole::DrumPad:
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
        case ChannelRole::Speech: case ChannelRole::SpeechLapel: case ChannelRole::SpeechHeadset:
        case ChannelRole::SpeechHandheld: case ChannelRole::SpeechLectern:
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

void Dine::drawLinkGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    // Two small rounded links, overlapping by a third, inside `r` (about 14 x 8 reads best).
    auto box = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 14.0f), juce::jmin (r.getHeight(), 8.0f));
    const float w = box.getWidth() * 0.62f, h = box.getHeight(), rad = h * 0.5f;
    g.setColour (c);
    g.drawRoundedRectangle (box.getX(), box.getY(), w, h, rad, 1.4f);
    g.drawRoundedRectangle (box.getRight() - w, box.getY(), w, h, rad, 1.4f);
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
    Dine::drawText (g, label, inner, juce::Justification::centred, true);
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
    Dine::drawText (g, label, inner, juce::Justification::centredLeft);
}

// ============================================================================ PanBar
void PanBar::paint (juce::Graphics& g)
{
    if (style == Style::Knob)
    {
        const float size = juce::jmin (float (getWidth()), float (getHeight()));
        auto box = getLocalBounds().toFloat().withSizeKeepingCentre (size, size).reduced (1.5f);
        const auto centre = box.getCentre();
        const float radius = box.getWidth() * 0.5f;
        const float start = juce::MathConstants<float>::pi * 1.25f;
        const float end   = juce::MathConstants<float>::pi * 2.75f;
        const float mid   = (start + end) * 0.5f;
        const float angle = mid + value * (end - mid);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
        g.setColour (Dine::control);
        g.strokePath (track, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (std::fabs (value) > 0.004f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (mid, angle), juce::jmax (mid, angle), true);
            g.setColour (isEnabled() ? tint.value_or (Dine::accent) : Dine::ink4);
            g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // the pointer: a dot on the rim, so the arc is never covered by it
        const auto dot = centre.getPointOnCircumference (radius - 4.5f, angle);
        g.setColour (isEnabled() ? Dine::ink : Dine::ink4);
        g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre (dot));
        return;
    }

    // v4: a white .10 track, a centre tick, the throw from the centre in white .55 (or the
    // tint a caller gives it), and a 10 pt white knob.
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (float (getWidth()), 4.0f);
    Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.10f), 2.0f);
    const float centre = r.getCentreX();
    const float x = centre + value * (r.getWidth() * 0.5f - 5.0f);
    g.setColour (Dine::ink.withAlpha (0.30f));
    g.fillRect (centre - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
    const float lo = juce::jmin (centre, x), hi = juce::jmax (centre, x);
    if (hi - lo > 0.5f)
    {
        g.setColour (tint.value_or (Dine::ink.withAlpha (0.55f)));
        g.fillRoundedRectangle (juce::Rectangle<float> (lo, r.getY(), hi - lo, r.getHeight()), 2.0f);
    }
    auto knob = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, r.getCentreY() });
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
    if (style == Style::Knob)
    {
        // A knob is dragged, never jumped to: the distance travelled is the change, so a click
        // on it does not fling the balance to wherever the pointer landed.
        const float half = juce::jmax (1.0f, float (getWidth()) * 1.6f);
        const float v = juce::jlimit (-1.0f, 1.0f, dragFrom + float (e.getDistanceFromDragStartX()) / half);
        setValue (std::fabs (v) < 0.04f ? 0.0f : v);
        if (onChange) onChange (value);
        return;
    }
    const float half = juce::jmax (1.0f, float (getWidth()) * 0.5f - 5.5f);
    const float v = juce::jlimit (-1.0f, 1.0f, (float (e.position.x) - float (getWidth()) * 0.5f) / half);
    setValue (std::fabs (v) < 0.06f ? 0.0f : v);
    if (onChange) onChange (value);
}

// ============================================================================ DineKnob
namespace
{
    // A knob's travel: `mid` is the value at the middle of the sweep, so a range can be
    // skewed to where the useful part of it is.
    double knobSkew (double min, double max, double mid) noexcept
    {
        if (! (mid > min && mid < max)) return 1.0;
        return std::log (0.5) / std::log ((mid - min) / (max - min));
    }

    double knobProportion (double v, double min, double max, double mid) noexcept
    {
        const double t = juce::jlimit (0.0, 1.0, (v - min) / juce::jmax (1.0e-9, max - min));
        return std::pow (t, knobSkew (min, max, mid));
    }

    double knobValue (double t, double min, double max, double mid) noexcept
    {
        return min + (max - min) * std::pow (juce::jlimit (0.0, 1.0, t), 1.0 / knobSkew (min, max, mid));
    }

    juce::Font knobCaps() { return Dine::text (10.0f, 600).withExtraKerningFactor (0.09f); }
    juce::Font knobRead() { return Dine::mono (11.0f, 500); }
}

DineKnob::DineKnob()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    format = [] (double v) { return juce::String (v, 1); };
}

void DineKnob::setRange (double lo, double hi, double increment, double middle)
{
    minimum = lo;
    maximum = juce::jmax (lo + 1.0e-9, hi);
    step = increment;
    mid = middle;
    value = juce::jlimit (minimum, maximum, value);
    repaint();
}

void DineKnob::setValue (double v)
{
    const double clamped = juce::jlimit (minimum, maximum, v);
    if (std::fabs (clamped - value) < 1.0e-6) return;
    value = clamped;
    repaint();
}

void DineKnob::setDial (int px)
{
    dial = juce::jmax (18, px);
    repaint();
}

void DineKnob::setShowsText (bool readout, bool name)
{
    showReadout = readout;
    showCaption = name;
    repaint();
}

// The words under a knob are a readout, so they are what has to stay legible when Text size
// is turned up: the cell keeps its pixels and the type grows inside it.
int DineKnob::cellHeight (int d)
{
    return d + 4 + 2 * juce::jmax (14, int (Dine::text (11.0f).getHeight()));
}

int DineKnob::cellHeight() const
{
    const int line = juce::jmax (14, int (Dine::text (11.0f).getHeight()));
    return dial + (showReadout || showCaption ? 4 : 0) + (showReadout ? line : 0) + (showCaption ? line : 0);
}

// The design's cell is 80 wide, which every one of its own labels fits in. A label that does
// not - LISTEN ABOVE, DETECTOR HP - takes the width it needs rather than an ellipsis.
int DineKnob::cellWidth() const
{
    int w = juce::jmax (dial + 8, 80);
    if (showCaption) w = juce::jmax (w, 8 + Dine::textWidth (knobCaps(), caption.trim().toUpperCase()));
    if (showReadout) w = juce::jmax (w, 8 + Dine::textWidth (knobRead(), format ? format (value) : juce::String()));
    return w;
}

void DineKnob::paint (juce::Graphics& g)
{
    const bool live = isEnabled();
    auto r = getLocalBounds();
    const int d = juce::jmin (dial, juce::jmin (r.getWidth(), showReadout || showCaption ? r.getHeight() : r.getHeight()));
    auto face = r.removeFromTop (d).toFloat().withSizeKeepingCentre (float (d), float (d));
    const float cx = face.getCentreX(), cy = face.getCentreY(), rad = d * 0.5f - 2.0f;
    const float a0 = juce::degreesToRadians (-135.0f), sweep = juce::degreesToRadians (270.0f);
    const float t = float (knobProportion (value, minimum, maximum, mid));

    // A proportion of the knob rather than a fixed 3 pt, so every dial in the product reads
    // as the same family whatever size it is drawn at.
    const float ring = juce::jmax (3.0f, rad * 0.17f);
    juce::Path track, arc;
    track.addCentredArc (cx, cy, rad, rad, 0.0f, a0, a0 + sweep, true);
    g.setColour (juce::Colours::white.withAlpha (live ? 0.10f : 0.05f));
    g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (t > 0.004f)
    {
        arc.addCentredArc (cx, cy, rad, rad, 0.0f, a0, a0 + sweep * t, true);
        g.setColour (live ? tint.value_or (Dine::accent) : Dine::ink4);
        g.strokePath (arc, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyR = juce::jmax (6.0f, rad - ring - 1.0f);
    g.setColour (live ? Dine::raised : Dine::card);
    g.fillEllipse (cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    const float a = a0 + sweep * t;
    const float sx = std::sin (a), sy = -std::cos (a);
    g.setColour (live ? Dine::ink : Dine::ink4);
    g.drawLine (cx + sx * bodyR * 0.35f, cy + sy * bodyR * 0.35f,
                cx + sx * (bodyR - juce::jmax (2.0f, bodyR * 0.2f)), cy + sy * (bodyR - juce::jmax (2.0f, bodyR * 0.2f)),
                juce::jmax (1.6f, bodyR * 0.14f));

    if (! showReadout && ! showCaption) return;
    r.removeFromTop (4);
    const int lineH = juce::jmax (14, int (Dine::text (11.0f).getHeight()));
    if (showReadout)
    {
        g.setColour (live ? Dine::ink : Dine::ink4);
        g.setFont (knobRead());
        Dine::drawText (g, format ? format (value) : juce::String(), r.removeFromTop (lineH), juce::Justification::centred, true);
    }
    if (showCaption)
    {
        // v4: a knob's caption in sentence case - "Threshold", "Detector HP" - with a word of
        // one or two letters (HP, Q, LF) kept as the initialism it is.
        juce::StringArray words;
        words.addTokens (caption.trim(), " ", {});
        for (int i = 0; i < words.size(); ++i)
        {
            const auto w = words[i];
            words.set (i, w.length() <= 2 ? w.toUpperCase()
                                          : i == 0 ? w.substring (0, 1).toUpperCase() + w.substring (1).toLowerCase() : w.toLowerCase());
        }
        g.setColour (live ? Dine::ink3 : Dine::ink4);
        g.setFont (Dine::text (10.5f));
        Dine::drawText (g, words.joinIntoString (" "), r.removeFromTop (lineH), juce::Justification::centred, true);
    }
}

void DineKnob::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = knobProportion (value, minimum, maximum, mid);
    anchor = e.position.y;
}

void DineKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    const double t = juce::jlimit (0.0, 1.0, dragFrom + double (anchor - e.position.y) / (e.mods.isShiftDown() ? 700.0 : 170.0));
    apply (knobValue (t, minimum, maximum, mid));
}

void DineKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (isEnabled()) apply (defaultValue);
}

void DineKnob::apply (double v)
{
    const double q = step > 0.0 ? minimum + std::round ((v - minimum) / step) * step : v;
    const double clamped = juce::jlimit (minimum, maximum, q);
    if (std::fabs (clamped - value) < 1.0e-9) return;
    value = clamped;
    repaint();
    if (onChange) onChange (clamped);
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
    // An icon on its own is a button too - the sheets' close cross - and it wants the glyph
    // and its padding, not the glyph plus the gap before a word that is not there.
    const bool hasLabel = getButtonText().isNotEmpty();
    const int iconW = icon == Dine::Icon::None ? 0 : hasLabel ? 21 : 15;
    return (hasLabel ? Dine::textWidth (font, caps ? getButtonText().toUpperCase() : getButtonText()) : 0)
           + padX * 2 + iconW;
}

void DineButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    // v4 (docs/design/v4): every button is a pill, its ends as round as it is tall; a segment
    // is the one exception, a 6 pt lift inside its 8 pt track.
    const float radius = style == Style::Segment ? 6.0f : r.getHeight() * 0.5f;
    const auto white = [] (float a) { return juce::Colours::white.withAlpha (a); };
    const bool primary = style == Style::Filled && (! tint.has_value() || *tint == Dine::accent);

    switch (style)
    {
        case Style::Filled:
            // The primary action is the white pill with dark type (TUNE CHANNEL, KEEP); a
            // filled button that means a console state keeps that state's colour.
            if (! isEnabled())  Dine::fillRounded (g, r, white (0.12f), radius);
            else if (primary)   Dine::fillRounded (g, r, down ? Dine::ink.darker (0.12f) : over ? Dine::ink.brighter (0.05f) : Dine::ink, radius);
            else Dine::fillRounded (g, r, down ? tint->darker (0.18f) : over ? tint->brighter (0.08f) : *tint, radius);
            break;
        case Style::Toggle:
            Dine::fillRounded (g, r, on ? white (down ? 0.12f : 0.16f) : white (down ? 0.12f : over ? 0.10f : (quiet ? 0.04f : 0.06f)), radius);
            Dine::hairlineRounded (g, r.reduced (0.5f), white (0.10f), radius - 0.5f);
            break;
        case Style::Standard:
            Dine::fillRounded (g, r, white (down ? 0.12f : over ? 0.10f : (quiet ? 0.04f : 0.06f)), radius);
            Dine::hairlineRounded (g, r.reduced (0.5f), white (0.10f), radius - 0.5f);
            break;
        case Style::Segment:
            if (on) Dine::fillRounded (g, r, white (0.16f), radius);
            break;
        case Style::Ghost:
            if (over || down) Dine::fillRounded (g, r, Dine::fillSoft, radius);
            break;
    }

    const bool filled = style == Style::Filled;
    juce::Colour fg;
    if (filled)               fg = ! isEnabled() ? Dine::ink3 : primary ? Dine::desk
                                 : (tintOr().getPerceivedBrightness() > 0.55f ? Dine::onAccent : Dine::ink);
    else if (style == Style::Toggle)  fg = on ? Dine::ink : (over ? Dine::ink : Dine::ink2);
    else if (style == Style::Segment) fg = on ? Dine::ink : (over ? Dine::ink : Dine::ink2);
    else if (style == Style::Ghost)   fg = over ? Dine::ink : Dine::ink2;
    else                              fg = over ? Dine::ink : Dine::ink2;
    if (! isEnabled() && ! filled) fg = fg.withAlpha (Dine::disabled);

    const juce::String label = caps ? getButtonText().toUpperCase() : getButtonText();
    const int weight = caps ? 700 : filled ? 600 : 500;
    const bool hasLabel = label.isNotEmpty();
    const int iconW = icon == Dine::Icon::None ? 0 : hasLabel ? 21 : 15;
    const auto faceAt = [&] (float px)
    {
        auto f = Dine::text (px, weight);
        return caps ? f.withExtraKerningFactor (0.04f) : f;
    };

    // A LABEL IS NOT A NAME. A channel called "Overhead - stage right 12" is data and is
    // ellipsised when it will not fit; MUTE is a fixed word on a control, and "M..." on a key
    // says nothing at all. So a button squeezed into a narrow cell gives up its side padding
    // first, then its type size - down to 9 px on the screen, whatever Text size is set to -
    // and only then a letter. At Standard with room to spare none of this does anything.
    auto font = faceAt (fontPx);
    int textW = hasLabel ? Dine::textWidth (font, label) : 0;
    int pad = padX;
    if (textW + iconW > getWidth() - padX * 2)
    {
        pad = juce::jlimit (2, padX, (getWidth() - textW - iconW) / 2);
        const int room = getWidth() - pad * 2 - iconW;
        if (textW > room && room > 0)
        {
            const float floorPx = 9.0f / juce::jmax (0.001f, Dine::textScale());
            const float wanted = fontPx * float (room) / float (textW);
            if (wanted < fontPx)
            {
                font = faceAt (juce::jmax (floorPx, wanted));
                textW = Dine::textWidth (font, label);
                pad = juce::jlimit (2, padX, (getWidth() - textW - iconW) / 2);
            }
        }
    }
    auto content = getLocalBounds().reduced (pad, 0);
    auto block = content.withSizeKeepingCentre (juce::jmin (content.getWidth(), textW + iconW), content.getHeight());
    if (icon != Dine::Icon::None)
        Dine::drawIcon (g, icon, block.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), fg);
    if (! hasLabel) return;
    if (iconW > 0) block.removeFromLeft (6);
    g.setColour (fg);
    g.setFont (font);
    Dine::drawText (g, label, block, juce::Justification::centredLeft);
}

// ============================================================================ DinePopup
DinePopup::DinePopup() : juce::Button ({})
{
    setWantsKeyboardFocus (false);
}

int DinePopup::idealWidth() const
{
    return Dine::textWidth (Dine::text (12.0f, 500), value) + (flat ? 38 : 40) + (dot.isTransparent() ? 0 : 15);
}

void DinePopup::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (flat) { if (over || down) Dine::fillRounded (g, r, down ? Dine::selected : Dine::control, Dine::Radius::control); }
    else      Dine::fillRounded (g, r, down ? Dine::controlOn : over ? Dine::controlHot : Dine::control, Dine::Radius::control);
    auto inner = getLocalBounds().reduced (flat ? 8 : 10, 0);
    Dine::drawIcon (g, Dine::Icon::Chevron, inner.removeFromRight (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                    over ? Dine::ink : Dine::ink2);
    inner.removeFromRight (6);
    if (! dot.isTransparent())
    {
        g.setColour (dot);
        g.fillEllipse (inner.removeFromLeft (7).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
        inner.removeFromLeft (8);
    }
    g.setColour (! isEnabled() ? Dine::ink4 : over ? Dine::ink : Dine::ink2);
    const auto font = Dine::text (12.0f, 500);
    g.setFont (font);
    Dine::drawText (g, brief.isNotEmpty() && Dine::textWidth (font, value) > inner.getWidth() ? brief : value,
                    inner, juce::Justification::centredLeft, true);
}

void DineSegmentRow::paint (juce::Graphics& g)
{
    Dine::drawSegmentTrack (g, getLocalBounds());
}

// ============================================================================ DineNavItem
DineNavItem::DineNavItem (const juce::String& l, Dine::Icon i) : juce::Button (l), label (l), icon (i)
{
    setWantsKeyboardFocus (false);
}

// The design's `Sidebar Row` (61:9156): 28 pt, 6 pt radius, sentence case, a 16 pt icon and a
// 10 pt gap. Selected lifts to a plane and turns the icon accent - which is what says "you are
// here"; there is no bar of colour and no capital letter anywhere on it. It is also the row a
// set-up list is built from, so `meta` and `done` still have their places at the right.
void DineNavItem::paintSidebarRow (juce::Graphics& g, bool over)
{
    auto r = getLocalBounds().toFloat();
    if (selected)                 Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.12f), 8.0f);
    else if (over && isEnabled()) Dine::fillRounded (g, r, juce::Colours::white.withAlpha (0.06f), 8.0f);

    auto inner = getLocalBounds().withTrimmedLeft (childRow ? 35 : 10).withTrimmedRight (9);
    if (! childRow)
    {
        Dine::drawIcon (g, icon, inner.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                        ! isEnabled() ? Dine::ink4 : selected ? Dine::ink : Dine::glyph);
        inner.removeFromLeft (9);
    }
    if (meta.isNotEmpty())
    {
        const auto font = Dine::text (11.0f, 600);
        g.setColour (metaTint.isTransparent() ? Dine::ink3 : metaTint);
        g.setFont (font);
        Dine::drawText (g, meta, inner.removeFromRight (Dine::textWidth (font, meta)), juce::Justification::centredRight);
        inner.removeFromRight (6);
    }
    g.setColour (! isEnabled() ? Dine::ink4 : childRow && ! selected ? Dine::ink2 : Dine::ink);
    g.setFont (Dine::text (childRow ? 12.5f : 13.0f));
    Dine::drawText (g, label, inner, juce::Justification::centredLeft, true);
}

void DineNavItem::paintButton (juce::Graphics& g, bool over, bool)
{
    if (sidebarLook) { paintSidebarRow (g, over); return; }
    auto r = getLocalBounds().toFloat();
    if (selected)                     Dine::fillRounded (g, r, Dine::selected, Dine::Radius::control);
    else if (over && isEnabled())     Dine::fillRounded (g, r, Dine::control, Dine::Radius::control);

    auto inner = getLocalBounds().reduced (10, 0);
    const juce::Colour fg = ! isEnabled() ? Dine::ink4 : selected ? Dine::ink : over ? Dine::ink : Dine::ink2;
    if (icon != Dine::Icon::None)
    {
        Dine::drawIcon (g, icon, inner.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                        ! isEnabled() ? Dine::ink4 : selected ? Dine::accent : fg);
        inner.removeFromLeft (10);
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
        g.setFont (Dine::mono (11.0f, 500));
        const int w = Dine::textWidth (Dine::mono (11.0f, 500), meta);
        Dine::drawText (g, meta, right.removeFromRight (w), juce::Justification::centredRight);
        right.removeFromRight (6);
    }
    g.setColour (fg);
    g.setFont (Dine::text (13.0f, 500));
    Dine::drawText (g, label, right, juce::Justification::centredLeft, true);
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
    // v4: 6 pt corners; off is white at .08; on fills with the key's own colour, with white
    // type on a dark colour (R, A) and dark type on a light one (M, S).
    auto r = getLocalBounds().toFloat();
    const float radius = juce::jmin (6.0f, r.getHeight() * 0.3f);
    if (on) Dine::fillRounded (g, r, down ? tint.darker (0.15f) : tint, radius);
    else    Dine::fillRounded (g, r, juce::Colours::white.withAlpha (down ? 0.16f : over ? 0.12f : 0.08f), radius);

    const auto onInk = tint.getPerceivedBrightness() > 0.62f ? Dine::desk : juce::Colours::white;
    g.setColour (! isEnabled() ? Dine::ink4 : on ? onInk : over ? Dine::ink : Dine::ink2);
    g.setFont (Dine::text (juce::jlimit (9.0f, 10.5f, float (getHeight()) * 0.5f), 700));
    Dine::drawText (g, letter, getLocalBounds(), juce::Justification::centred, false);
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
        Dine::drawText (g, name.toUpperCase(), juce::Rectangle<float> (0.0f, 0.0f, below.getHeight(), r.getWidth()),
                    juce::Justification::centred, false);
    }
}

// ============================================================================ DineSwitch
DineSwitch::DineSwitch (const juce::String& on, const juce::String& off) : juce::Button (on), onText (on), offText (off)
{
    setWantsKeyboardFocus (false);
}

int DineSwitch::idealWidth() const
{
    const auto font = Dine::text (11.5f);
    return 28 + 7 + juce::jmax (Dine::textWidth (font, onText), Dine::textWidth (font, offText)) + 2;
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
    Dine::drawText (g, on ? onText : offText, r, juce::Justification::centredLeft);
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
    // A scrollbar's own ground stays clear: JUCE's default painted a white band the width of
    // the console under every horizontal scroller.
    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
}

void DineLookAndFeel::setBipolar (juce::Slider& s, bool on) { s.getProperties().set ("dineBipolar", on); }

juce::Typeface::Ptr DineLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // A stock JUCE widget asking for "the sans" or "the mono" gets the same faces DINE draws with.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return systemFace (false, f.isBold() ? 600 : 400);
    if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return systemFace (true, f.isBold() ? 600 : 400);
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
    // An overlay slider sits on top of something that is already drawn - a meter, usually - so
    // it draws its thumb and nothing else: no track, no fill.
    const bool overlay = bool (s.getProperties().getWithDefault ("dineOverlay", false));
    auto full = juce::Rectangle<float> (float (x), float (y), float (w), float (h));
    const juce::Colour capColour = s.isEnabled() ? Dine::ink2 : Dine::ink4;

    // The design's fader (`Fader`, 62:9241): a 4 pt well and a machined cap - a two-stop
    // vertical gradient, a lighter bevel along its top edge and one index line across its
    // middle. No glow and no texture; the slot never fills, so a bank reads as a row of caps
    // rather than a wall of colour, and the one mark it is read against is the unity line the
    // strip draws across the fader and the meter together.
    if (consoleFader)
    {
        const auto capTop  = s.isEnabled() ? juce::Colour (0xffd8dadd) : Dine::ink4;
        const auto capBot  = s.isEnabled() ? juce::Colour (0xff9a9da4) : Dine::ink4.darker (0.2f);
        const auto bevel   = s.isEnabled() ? juce::Colour (0xfff0f1f3) : Dine::ink4.brighter (0.2f);

        if (vertical)
        {
            auto well = juce::Rectangle<float> (full.getCentreX() - 2.0f, full.getY(), 4.0f, full.getHeight());
            Dine::fillRounded (g, well, Dine::deep, 2.0f);
            // A fader that asks for the design's full 26 x 40 cap (LIVE's strips) gets it, with
            // the thumb radius to match so the travel is the well and the cap rides over its ends.
            const float tallCap = float (int (s.getProperties().getWithDefault ("dineFaderCap", 0)));
            const float capH = tallCap > 0.0f ? tallCap : 18.0f, capW = juce::jlimit (18.0f, 26.0f, full.getWidth());
            const float capR = tallCap > 0.0f ? 4.0f : 3.0f;
            const float cy = tallCap > 0.0f ? juce::jlimit (capH * 0.5f, float (s.getHeight()) - capH * 0.5f, sliderPos)
                                            : juce::jlimit (full.getY() + capH * 0.5f, full.getBottom() - capH * 0.5f, sliderPos);
            auto cap = juce::Rectangle<float> (full.getCentreX() - capW * 0.5f, cy - capH * 0.5f, capW, capH);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (cap.translated (0.0f, tallCap > 0.0f ? 2.0f : 1.0f), capR);
            g.setGradientFill ({ capTop, cap.getX(), cap.getY(), capBot, cap.getX(), cap.getBottom(), false });
            g.fillRoundedRectangle (cap, capR);
            g.setColour (bevel);
            g.fillRect (cap.getX() + 2.0f, cap.getY() + 0.5f, cap.getWidth() - 4.0f, 1.0f);
            g.setColour (juce::Colour (0xff5a5d63));
            g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 6.0f, 1.0f);
        }
        else
        {
            auto well = juce::Rectangle<float> (full.getX(), full.getCentreY() - 2.0f, full.getWidth(), 4.0f);
            Dine::fillRounded (g, well, Dine::deep, 2.0f);
            const float capW = 12.0f, capH = juce::jlimit (12.0f, 18.0f, full.getHeight());
            const float cx = juce::jlimit (full.getX() + capW * 0.5f, full.getRight() - capW * 0.5f, sliderPos);
            auto cap = juce::Rectangle<float> (cx - capW * 0.5f, full.getCentreY() - capH * 0.5f, capW, capH);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (cap.translated (0.0f, 1.0f), 3.0f);
            g.setGradientFill ({ capTop, cap.getX(), cap.getY(), capBot, cap.getX(), cap.getBottom(), false });
            g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (juce::Colour (0xff5a5d63));
            g.fillRect (cap.getCentreX() - 0.5f, cap.getY() + 3.0f, 1.0f, cap.getHeight() - 6.0f);
        }
        return;
    }

    if (overlay)
    {
        const auto ring = s.isEnabled() ? Dine::ink : Dine::ink4;
        if (vertical)
        {
            const float cy = juce::jlimit (full.getY() + 7.0f, full.getBottom() - 7.0f, sliderPos);
            auto knob = juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ full.getCentreX(), cy });
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (knob.translated (0.0f, 1.0f));
            g.setColour (ring);
            g.fillEllipse (knob);
        }
        else
        {
            const float cx = juce::jlimit (full.getX() + 7.0f, full.getRight() - 7.0f, sliderPos);
            auto knob = juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ cx, full.getCentreY() });
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (knob.translated (0.0f, 1.0f));
            g.setColour (ring);
            g.fillEllipse (knob);
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
        Dine::drawText (g, shortcutKeyText, content.removeFromRight (w), juce::Justification::centredRight);
    }

    g.setColour (! isActive ? Dine::ink4 : isHighlighted ? Dine::ink : isTicked ? Dine::accent : Dine::ink2);
    g.setFont (Dine::text (12.5f));
    Dine::drawText (g, text, content, juce::Justification::centredLeft, true);
}

void DineLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setColour (Dine::ink4);
    g.setFont (Dine::caps (10.0f, 0.10f));
    Dine::drawText (g, name.toUpperCase(), area.reduced (16, 0), juce::Justification::centredLeft, true);
}

void DineLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 60; idealHeight = 9; return; }
    // A menu is not a console: when the words grow the row grows with them, because there is
    // no layout here whose geometry means anything.
    idealHeight = juce::roundToInt (28.0f * Dine::textScale());
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

juce::Rectangle<int> DineLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    juce::AttributedString a;
    a.setJustification (juce::Justification::centredLeft);
    a.append (tip, Dine::text (12.0f), juce::Colours::black);
    juce::TextLayout layout;
    layout.createLayout (a, 400.0f);

    const int w = int (layout.getWidth()) + 22;      // the 10 px the text is reduced by, both sides
    const int h = int (layout.getHeight()) + 14;     // ... and the 6 px
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
             .constrainedWithin (parentArea);
}

void DineLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Dine::popover);
    auto r = juce::Rectangle<float> (0.0f, 0.0f, float (w), float (h));
    Dine::fillRounded (g, r, Dine::popover, Dine::Radius::control);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (12.0f));
    Dine::drawFittedText (g, text, r.reduced (10.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 4);
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
