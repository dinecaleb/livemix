// THE TEXT CACHE: the same pixels JUCE would have drawn, and far fewer layouts to draw them.
//
// Every `g.drawText` in the application became `Dine::drawText`, because JUCE's own layout
// cache holds 128 strings for the whole window and one workspace of DINE uses several times
// that - so past 128 each paint re-shaped what the paint before it had shaped. Measured on a
// 48-channel console, TUNE's warm repaint was 20.0 ms and INSPECTOR's 19.0; with the layouts
// kept they are 9.6 and 6.9.
//
// A faster wrong pixel is no use, so this is the test that matters: render the same string the
// same way through `juce::Graphics` and through `Dine`, and require the two images to be
// identical. Every text style the design has, at every text size, in both the plain and the
// fitted form, justified every way, with and without the ellipsis, and including the cases
// that go wrong quietly - a string too long for its box, an empty one, one rectangle a pixel
// wide, a name with an accent in it.
#include "TestFramework.h"
#include "ui/AppTheme.h"
#include <juce_graphics/juce_graphics.h>
#include <vector>

using namespace livemix;

namespace
{
    // The two images, and where they first differ. A count rather than a bool: "3 pixels out"
    // and "the whole string is 2 px to the left" are different bugs.
    struct Diff { int pixels = 0; int firstX = -1, firstY = -1; };

    Diff compare (const juce::Image& a, const juce::Image& b)
    {
        Diff d;
        const juce::Image::BitmapData pa (a, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData pb (b, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (pa.getPixelColour (x, y) != pb.getPixelColour (x, y))
                {
                    if (d.pixels == 0) { d.firstX = x; d.firstY = y; }
                    ++d.pixels;
                }
        return d;
    }

    // Draws `paint` into a fresh image of the given size. The same renderer both times, so a
    // difference can only come from the layout.
    template <typename Paint>
    juce::Image render (int w, int h, Paint&& paint)
    {
        juce::Image image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::white);
        paint (g);
        return image;
    }

    // The faces DINE actually draws with, at the sizes it draws them: Barlow for words, the
    // tracked caps for labels, IBM Plex Mono for numbers. Tracking matters here - a tracked run
    // shapes differently, so a cache that got it wrong would show up on the labels first.
    std::vector<juce::Font> everyStyle()
    {
        std::vector<juce::Font> fonts;
        for (const float px : { 11.0f, 12.0f, 13.0f, 17.0f, 24.0f })
        {
            fonts.push_back (Dine::text (px));
            fonts.push_back (Dine::text (px, 600));
            fonts.push_back (Dine::mono (px));
        }
        for (const float px : { 9.0f, 10.0f, 12.0f, 13.0f })
            for (const float tracking : { 0.0f, 0.05f, 0.08f })
                fonts.push_back (Dine::caps (px, tracking));
        return fonts;
    }

    const std::vector<juce::String>& everyString()
    {
        static const std::vector<juce::String> strings {
            "WARMTH", "Lead Vocal", "-12.4 dB", "", " ", "1", "KICK IN - stage right 12",
            "A name far too long for the box it has been given to sit in", "Bb", "48 kHz",
            "Andr\xc3\xa9 - r\xc3\xa9p\xc3\xa9tition", "TUNE LIVE MIX", "00:00:00", "3.5 kHz  +2.0 dB"
        };
        return strings;
    }

    const std::vector<juce::Justification>& everyJustification()
    {
        static const std::vector<juce::Justification> js {
            juce::Justification::left, juce::Justification::right, juce::Justification::centred,
            juce::Justification::centredLeft, juce::Justification::centredRight,
            juce::Justification::topLeft, juce::Justification::bottomRight,
            juce::Justification::horizontallyCentred, juce::Justification::horizontallyJustified
        };
        return js;
    }
}

TEST_CASE ("Dine::drawText draws exactly the pixels juce::Graphics::drawText draws")
{
    const std::pair<int, int> boxes[] = { { 200, 24 }, { 40, 12 }, { 1, 10 }, { 300, 60 }, { 48, 11 } };
    int compared = 0;

    {
        for (const auto& font : everyStyle())
            for (const auto& text : everyString())
                for (const auto& [w, h] : boxes)
                    for (const auto& just : everyJustification())
                        for (const bool ellipses : { true, false })
                        {
                            const juce::Rectangle<int> area (3, 2, w, h);
                            const auto mine = render (w + 8, h + 6, [&] (juce::Graphics& g)
                                { g.setFont (font); Dine::drawText (g, text, area, just, ellipses); });
                            const auto theirs = render (w + 8, h + 6, [&] (juce::Graphics& g)
                                { g.setFont (font); g.drawText (text, area, just, ellipses); });

                            const auto d = compare (mine, theirs);
                            CHECK_MESSAGE (d.pixels == 0,
                                           ("\"" + text + "\" at " + juce::String (w) + "x" + juce::String (h)
                                            + ", just " + juce::String (just.getFlags())
                                            + ", ellipses " + juce::String (int (ellipses)) + ": "
                                            + juce::String (d.pixels) + " pixels differ, first at "
                                            + juce::String (d.firstX) + "," + juce::String (d.firstY)).toStdString());
                            ++compared;
                        }
    }

    CHECK (compared > 3000);
}

TEST_CASE ("Dine::drawFittedText draws exactly the pixels juce::Graphics::drawFittedText draws")
{
    const std::pair<int, int> boxes[] = { { 200, 40 }, { 60, 34 }, { 120, 14 }, { 300, 80 } };
    int compared = 0;

    for (const auto& font : everyStyle())
        for (const auto& text : everyString())
            for (const auto& [w, h] : boxes)
                for (const auto& just : everyJustification())
                    for (const int lines : { 1, 2, 4 })
                        for (const float minScale : { 0.0f, 0.7f, 1.0f })
                        {
                            const juce::Rectangle<int> area (3, 2, w, h);
                            const auto mine = render (w + 8, h + 6, [&] (juce::Graphics& g)
                                { g.setFont (font); Dine::drawFittedText (g, text, area, just, lines, minScale); });
                            const auto theirs = render (w + 8, h + 6, [&] (juce::Graphics& g)
                                { g.setFont (font); g.drawFittedText (text, area, just, lines, minScale); });

                            const auto d = compare (mine, theirs);
                            CHECK_MESSAGE (d.pixels == 0,
                                           ("\"" + text + "\" at " + juce::String (w) + "x" + juce::String (h)
                                            + ", " + juce::String (lines) + " lines, minScale " + juce::String (minScale)
                                            + ", just " + juce::String (just.getFlags()) + ": "
                                            + juce::String (d.pixels) + " pixels differ, first at "
                                            + juce::String (d.firstX) + "," + juce::String (d.firstY)).toStdString());
                            ++compared;
                        }

    CHECK (compared > 3000);
}

TEST_CASE ("The text cache answers from memory the second time, and never grows without bound")
{
    Dine::clearTextCache();
    Dine::resetTextCacheStats();

    // A page's worth of labels, drawn twice. The second pass must lay nothing out again.
    const auto drawAPage = []
    {
        juce::Image image (juce::Image::ARGB, 400, 40, true);
        juce::Graphics g (image);
        for (int i = 0; i < 200; ++i)
        {
            g.setFont (Dine::caps (11.0f, 0.02f));
            Dine::drawText (g, "CHANNEL " + juce::String (i), juce::Rectangle<int> (0, 0, 120, 14),
                            juce::Justification::centredLeft);
        }
    };

    drawAPage();
    const auto first = Dine::textCacheStats();
    CHECK (first.misses == 200);

    drawAPage();
    const auto second = Dine::textCacheStats();
    CHECK_MESSAGE (second.misses == first.misses,
                   "the second pass laid out " + std::to_string (second.misses - first.misses)
                   + " strings again; it should have found all 200 already laid out");
    CHECK (second.hits >= 200);

    // Churn: a readout is a new string every frame, and a cache that only ever grew would keep
    // every number a meter has ever shown. Two generations of 4096, so never past 8192.
    for (int i = 0; i < 30000; ++i)
    {
        juce::Image image (juce::Image::ARGB, 80, 16, true);
        juce::Graphics g (image);
        g.setFont (Dine::mono (12.0f, 500));
        Dine::drawText (g, juce::String (-i * 0.01, 2) + " dB", juce::Rectangle<int> (0, 0, 70, 14),
                        juce::Justification::centredRight);
    }
    const auto after = Dine::textCacheStats();
    CHECK_MESSAGE (after.size <= 8192, "the cache holds " + std::to_string (after.size) + " layouts");

    Dine::clearTextCache();
    CHECK (Dine::textCacheStats().size == 0);
}

// --------------------------------------------------------------------------- text size
// View > Appearance > Text size scales the words and nothing else. The two halves of that
// sentence are both worth asserting: a volunteer who cannot read a fader's name gets a bigger
// name, and a 32-channel console still shows 32 channels.
TEST_CASE ("Text size: every role grows with it, no metric does, and the layouts are thrown away")
{
    struct Restore { ~Restore() { Dine::setTextScale (1.0f); } } restore;

    Dine::setTextScale (1.0f);
    const auto standard = Dine::textWidth (Dine::text (13.0f), "Lead Vocal");
    const auto standardMono = Dine::textWidth (Dine::mono (12.0f, 500), "-12.4 dB");
    const auto standardCaps = Dine::textWidth (Dine::caps (11.0f, 0.08f), "DRUMS");

    for (const auto scale : { 1.2f, 1.35f })
    {
        Dine::setTextScale (scale);
        CHECK_NEAR (Dine::textScale(), scale, 0.0001);
        // Every face a page can ask for is bigger, within a pixel of rounding either way.
        CHECK (Dine::textWidth (Dine::text (13.0f), "Lead Vocal") > standard);
        CHECK (Dine::textWidth (Dine::mono (12.0f, 500), "-12.4 dB") > standardMono);
        CHECK (Dine::textWidth (Dine::caps (11.0f, 0.08f), "DRUMS") > standardCaps);
        CHECK_NEAR (double (Dine::text (13.0f).getHeight()) / double (Dine::text (13.0f / scale).getHeight()),
                    double (scale), 0.02);
    }

    // The console's geometry is constant, by construction: not one of these is a function of
    // the scale, so a bigger word can never cost a channel.
    CHECK (Dine::Metric::chanRail == 180);
    CHECK (Dine::Metric::tuneRail == 240);
    CHECK (Dine::Metric::sidebar == 230);
    CHECK (Dine::Metric::toolbar == 60);
    CHECK (Dine::Metric::status == 30);
    CHECK (Dine::Metric::chainFoot == 52);

    // Every layout held was for a face at the old size, so the cache is emptied rather than
    // left holding what nothing will ask for again.
    Dine::setTextScale (1.0f);
    {
        juce::Image image (juce::Image::ARGB, 200, 20, true);
        juce::Graphics g (image);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, "Lead Vocal", juce::Rectangle<int> (0, 0, 180, 18), juce::Justification::centredLeft);
    }
    CHECK (Dine::textCacheStats().size > 0);
    Dine::setTextScale (1.2f);
    CHECK (Dine::textCacheStats().size == 0);

    // Asking for the size it already is costs nothing and keeps what is cached.
    {
        juce::Image image (juce::Image::ARGB, 200, 20, true);
        juce::Graphics g (image);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, "Lead Vocal", juce::Rectangle<int> (0, 0, 180, 18), juce::Justification::centredLeft);
    }
    const auto held = Dine::textCacheStats().size;
    Dine::setTextScale (1.2f);
    CHECK (Dine::textCacheStats().size == held);

    // Out of range is clamped rather than obeyed: nothing can leave the window unreadable.
    Dine::setTextScale (9.0f);
    CHECK (Dine::textScale() <= 2.0f);
    Dine::setTextScale (0.1f);
    CHECK_NEAR (Dine::textScale(), 1.0f, 0.0001);
}

// THE CLIPPING AUDIT: the tool that found the sloppiness.
//
// A name is data and is ellipsised on purpose; a fixed word cut to "Clo..." is a cell that is
// too small, and no amount of reading layout code finds one - the cell is only too small once
// the face, the Text size and the string meet each other. So the drawing keeps the list, and
// `dine_ui_snapshots` walks every workspace with it on. This is the audit's own test.
TEST_CASE ("Text: the clipping audit reports what was cut and nothing that fitted")
{
    Dine::setTextScale (1.0f);
    Dine::beginTextClipAudit();
    Dine::setTextClipScope ("a test");
    {
        juce::Image image (juce::Image::ARGB, 300, 40, true);
        juce::Graphics g (image);
        g.setFont (Dine::text (13.0f));
        // Room to spare, exactly the ellipsis path, and nowhere near enough room.
        Dine::drawText (g, "Kick", juce::Rectangle<int> (0, 0, 200, 18), juce::Justification::centredLeft, true);
        Dine::drawText (g, "Close", juce::Rectangle<int> (0, 18, 28, 18), juce::Justification::centredLeft, true);
        // Ellipses off: the caller has said it would rather the string ran on.
        Dine::drawText (g, "Runs on past its box", juce::Rectangle<int> (0, 0, 30, 18), juce::Justification::centredLeft, false);
        // Fitted text squeezes before it cuts, so what counts is whether it fits after the
        // squeeze. This one does.
        Dine::drawFittedText (g, "Snare", juce::Rectangle<int> (0, 0, 40, 18), juce::Justification::centredLeft, 1, 0.7f);
        // ... and this one does not, which is how "Ambie..." reached a group tile.
        Dine::drawFittedText (g, "A sentence with nowhere near enough room", juce::Rectangle<int> (0, 0, 60, 18),
                              juce::Justification::centredLeft, 1, 0.7f);
        // Past one line it wraps, and where a word lands is JUCE's business.
        Dine::drawFittedText (g, "A sentence with nowhere near enough room", juce::Rectangle<int> (0, 0, 60, 40),
                              juce::Justification::topLeft, 3, 0.9f);
    }
    Dine::endTextClipAudit();
    const auto report = Dine::textClipReport();
    REQUIRE (report.size() == 2);
    CHECK (report[0].text == "A sentence with nowhere near enough room");   // the widest shortfall first
    CHECK (report[1].text == "Close");
    CHECK (report[1].where == "a test");
    CHECK (report[1].wanted > report[1].available);

    // Off again, and the next run starts from nothing.
    {
        juce::Image image (juce::Image::ARGB, 300, 40, true);
        juce::Graphics g (image);
        g.setFont (Dine::text (13.0f));
        Dine::drawText (g, "Another one that does not fit", juce::Rectangle<int> (0, 0, 20, 18), juce::Justification::centredLeft, true);
    }
    CHECK (Dine::textClipReport().size() == 2);
    Dine::beginTextClipAudit();
    CHECK (Dine::textClipReport().empty());
    Dine::endTextClipAudit();
}

// A PATH IS READ FROM THE RIGHT. Cutting one at the left-hand end keeps the part nobody needs
// and loses the file, so a path too long for its cell drops folders off the front instead.
TEST_CASE ("Text: a path too long for its cell keeps its file name")
{
    const auto font = Dine::mono (11.0f, 500);
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto file = home.getChildFile ("Documents").getChildFile ("A church with a long name")
                          .getChildFile ("Sunday 09:30").getChildFile ("Exports").getChildFile ("Sunday.wav");

    // Room for all of it: the home folder is still written as a tilde, and nothing is dropped.
    const auto whole = Dine::shortPath (file, font, 4000);
    CHECK (whole.startsWith ("~"));
    CHECK (whole.endsWith ("Sunday.wav"));
    CHECK (Dine::textWidth (font, whole) <= 4000);

    // No room: the name survives, the front is dropped, and what is left fits the cell.
    const auto cut = Dine::shortPath (file, font, 160);
    CHECK (cut.endsWith ("Sunday.wav"));
    CHECK (cut.startsWith (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"))));
    CHECK (Dine::textWidth (font, cut) <= 160 + 2);
}
