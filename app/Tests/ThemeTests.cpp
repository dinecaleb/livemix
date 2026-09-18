// THEMES: the document, the presets, the folder and the preference (app/native/ThemeStore).
// No window: the UI half is checked by dlive_ui_snapshots (the binding table, and a render
// of the console under every built-in theme).
#include "TestFramework.h"
#include "native/ThemeStore.h"
#include <juce_core/juce_core.h>

using namespace livemix;

namespace
{
    juce::File scratch (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dlive-theme-tests").getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }
}

TEST_CASE ("Themes: the built-in default is complete and every other built-in resolves to a full palette")
{
    const auto& all = ThemeStore::builtIn();
    REQUIRE (all.size() >= 2);
    CHECK (all.front().name == juce::String (ThemeStore::kDefaultName));
    for (const auto& t : ThemeStore::tokens()) CHECK (all.front().has (t.key));
    for (const auto& theme : all)
    {
        CHECK (theme.builtIn);
        const auto palette = ThemeStore::resolve (theme);
        CHECK (palette.size() == ThemeStore::tokens().size());
        for (const auto& kv : theme.colours) CHECK (ThemeStore::isToken (kv.first));   // no preset carries a key the app does not read
        CHECK (ThemeStore::isBuiltInName (theme.name));
    }
    // A partial built-in inherits what it leaves out from the default.
    if (const auto* lime = ThemeStore::builtIn ("Lime Desk"))
    {
        const auto palette = ThemeStore::resolve (*lime);
        CHECK (palette.at ("accent") == lime->colours.at ("accent"));
        CHECK (palette.at ("hair") == all.front().colours.at ("hair"));
    }
}

TEST_CASE ("Themes: hex reads and writes both forms and refuses what is not a colour")
{
    CHECK (ThemeStore::hex (0xff6db8a8) == "#6db8a8");
    CHECK (ThemeStore::hex (0x14ffffff) == "#14ffffff");
    juce::uint32 c = 0;
    CHECK (ThemeStore::parseHex ("#6db8a8", c) && c == 0xff6db8a8);
    CHECK (ThemeStore::parseHex ("6DB8A8", c) && c == 0xff6db8a8);
    CHECK (ThemeStore::parseHex ("#14ffffff", c) && c == 0x14ffffff);
    CHECK (! ThemeStore::parseHex ("#fff", c));
    CHECK (! ThemeStore::parseHex ("teal", c));
    CHECK (! ThemeStore::parseHex ("#6db8a8x", c));
}

TEST_CASE ("Themes: a document round-trips, keeps only what it sets, and names what it does not know")
{
    Theme t;
    t.name = "Sunday lime";
    t.note = "The booth's own";
    t.basedOn = "Studio Teal";
    t.colours["accent"] = 0xffd6f54c;
    t.colours["hair"] = 0x20ffffff;

    const auto v = ThemeStore::toVar (t);
    Theme back; juce::String why;
    REQUIRE (ThemeStore::fromVar (v, back, why));
    CHECK (back.name == "Sunday lime");
    CHECK (back.note == "The booth's own");
    CHECK (back.basedOn == "Studio Teal");
    CHECK (back.colours.size() == 2);
    CHECK (back.colours.at ("accent") == 0xffd6f54c);
    CHECK (back.colours.at ("hair") == 0x20ffffff);
    CHECK (! back.builtIn);

    // The file is JSON a person can write: a key this build does not know is reported, not fatal.
    juce::var hand;
    REQUIRE (! juce::JSON::parse (R"({ "kind": "theme", "schema": 1, "name": "Hand made",
                                     "colours": { "accent": "#ff0000", "glow": "#00ff00" } })", hand).failed());
    Theme made;
    REQUIRE (ThemeStore::fromVar (hand, made, why));
    CHECK (made.colours.size() == 1);
    CHECK (made.unknownKeys.size() == 1 && made.unknownKeys[0] == "glow");
    // Resolving it fills every other colour from the default.
    CHECK (ThemeStore::resolve (made).size() == ThemeStore::tokens().size());
    CHECK (ThemeStore::resolve (made).at ("accent") == 0xffff0000);
}

TEST_CASE ("Themes: a file that is not a theme, from the future, nameless or with a bad colour is refused with its reason")
{
    Theme t; juce::String why;
    juce::var v;
    juce::JSON::parse (R"({ "kind": "session", "schema": 1, "name": "x", "colours": {} })", v);
    CHECK (! ThemeStore::fromVar (v, t, why) && why.contains ("not a DLIVE theme"));
    juce::JSON::parse (R"({ "kind": "theme", "schema": 99, "name": "x", "colours": {} })", v);
    CHECK (! ThemeStore::fromVar (v, t, why) && why.contains ("newer"));
    juce::JSON::parse (R"({ "kind": "theme", "schema": 1, "colours": {} })", v);
    CHECK (! ThemeStore::fromVar (v, t, why) && why.contains ("no name"));
    juce::JSON::parse (R"({ "kind": "theme", "schema": 1, "name": "x", "colours": { "accent": "lime" } })", v);
    CHECK (! ThemeStore::fromVar (v, t, why) && why.contains ("accent"));
    CHECK (! ThemeStore::fromVar (juce::var ("text"), t, why));
    CHECK (! ThemeStore::load (juce::File ("/nowhere/none.dlivetheme.json"), t, why) && why.contains ("no file"));
}

TEST_CASE ("Themes: the folder lists yours by name, a built-in's name is refused, and delete removes the file")
{
    const auto dir = scratch ("folder");
    Theme mine;
    mine.name = "Sunday lime";
    mine.colours["accent"] = 0xffd6f54c;
    juce::String why;
    REQUIRE (ThemeStore::saveUser (mine, why, dir));
    CHECK (mine.file == ThemeStore::fileFor ("Sunday lime", dir));
    CHECK (mine.file.existsAsFile());

    Theme stolen;
    stolen.name = ThemeStore::kDefaultName;
    CHECK (! ThemeStore::saveUser (stolen, why, dir));
    CHECK (why.contains ("DLIVE's own"));
    Theme blank;
    blank.name = "   ";
    CHECK (! ThemeStore::saveUser (blank, why, dir));

    // A file that will not read is skipped and named, never listed as a theme.
    dir.getChildFile ("broken" + juce::String (ThemeStore::kExtension)).replaceWithText ("{ not json");
    juce::StringArray refused;
    const auto listed = ThemeStore::listUser (dir, &refused);
    REQUIRE (listed.size() == 1);
    CHECK (listed[0].name == "Sunday lime");
    CHECK (! listed[0].builtIn);
    CHECK (refused.size() == 1 && refused[0].startsWith ("broken"));

    const auto all = ThemeStore::all (dir);
    CHECK (all.size() == ThemeStore::builtIn().size() + 1);
    CHECK (all.back().name == "Sunday lime");

    // find: a built-in, then yours, then the default (never a failure).
    CHECK (ThemeStore::find ("Lime Desk", dir).builtIn);
    CHECK (ThemeStore::find ("sunday LIME", dir).name == "Sunday lime");
    CHECK (ThemeStore::find ("no such theme", dir).name == juce::String (ThemeStore::kDefaultName));

    CHECK (ThemeStore::removeUser ("Sunday lime", dir));
    CHECK (! mine.file.existsAsFile());
    CHECK (! ThemeStore::removeUser (ThemeStore::kDefaultName, dir));
    CHECK (ThemeStore::listUser (dir).empty());
}

TEST_CASE ("Themes: importing reads the file first and a built-in's name is kept apart")
{
    const auto dir = scratch ("import");
    const auto elsewhere = scratch ("elsewhere");

    Theme sent;
    sent.name = "Lime Desk";       // somebody exported a built-in and sent it on
    sent.colours["accent"] = 0xff00ff00;
    REQUIRE (ThemeStore::save (sent, elsewhere.getChildFile ("theirs.dlivetheme.json")));

    Theme imported; juce::String why;
    REQUIRE (ThemeStore::importFile (elsewhere.getChildFile ("theirs.dlivetheme.json"), imported, why, dir));
    CHECK (imported.name == "Lime Desk (imported)");
    CHECK (imported.file.getParentDirectory() == dir);
    CHECK (ThemeStore::listUser (dir).size() == 1);

    elsewhere.getChildFile ("bad.dlivetheme.json").replaceWithText (R"({ "kind": "theme", "schema": 1, "name": "Bad", "colours": { "ink": "white" } })");
    CHECK (! ThemeStore::importFile (elsewhere.getChildFile ("bad.dlivetheme.json"), imported, why, dir));
    CHECK (why.contains ("ink"));
    CHECK (ThemeStore::listUser (dir).size() == 1);
}

TEST_CASE ("Themes: the preference remembers the name, forgets the default, and leaves other settings alone")
{
    const auto prefs = scratch ("prefs").getChildFile ("preferences.json");
    CHECK (ThemeStore::chosenTheme (prefs).isEmpty());

    prefs.replaceWithText (R"({ "somethingElse": 3 })");
    REQUIRE (ThemeStore::setChosenTheme ("Lime Desk", prefs));
    CHECK (ThemeStore::chosenTheme (prefs) == "Lime Desk");
    CHECK (prefs.loadFileAsString().contains ("somethingElse"));

    REQUIRE (ThemeStore::setChosenTheme (ThemeStore::kDefaultName, prefs));
    CHECK (ThemeStore::chosenTheme (prefs).isEmpty());
    CHECK (prefs.loadFileAsString().contains ("somethingElse"));

    // A chosen theme that no longer exists falls back to the default rather than to nothing.
    REQUIRE (ThemeStore::setChosenTheme ("Gone", prefs));
    CHECK (ThemeStore::find (ThemeStore::chosenTheme (prefs), scratch ("prefs-empty")).name == juce::String (ThemeStore::kDefaultName));
}
