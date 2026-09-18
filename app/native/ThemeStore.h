#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace livemix
{

// ---------------------------------------------------------------------------
// THEMES
//
// The look of DLIVE is a table of named colours (`Dine::` in app/ui/AppTheme.h): the desk,
// the window, the cards, the ink, the accent, the status colours, the console keys and the
// group tints. A theme is that table written down. Pick one and every workspace, sheet,
// menu and meter reads it - the way an editor's colour theme changes the whole editor and
// nothing about the document.
//
// Two kinds exist. The **built-in** themes ship with the app and cannot be changed: the v2
// design ("Studio Teal", the default), the lime desk, a slate, a tape-warm one and a light
// one. **Your** themes are files in ~/Music/DLIVE/Themes - one JSON document per theme,
// so a theme can be sent to somebody else, kept in a backup or written by hand. A theme
// only has to name the colours it changes: whatever it leaves out comes from the theme it
// says it is `basedOn` (a built-in), and from Studio Teal after that, so "the default with
// a lime accent" is a five-line file.
//
// This half of the feature is JUCE-core only - the document, the folder, the presets and
// the preference - so it is tested without a window. The UI half (`Dine::applyTheme`, the
// Appearance sheet, the View menu) lives in app/ui.
//
// A theme never touches the session or the mix. It is a preference of this Mac, stored in
// ~/Music/DLIVE/preferences.json, not of the document.
// ---------------------------------------------------------------------------

struct ThemeToken
{
    const char* key;     // the JSON key and the `Dine::` name: "accent"
    const char* group;   // where the Appearance sheet lists it: "Accent"
    const char* what;    // one plain sentence
};

struct Theme
{
    static constexpr int kSchemaVersion = 1;

    juce::String name;
    juce::String note;                      // the author's own words
    juce::String basedOn;                   // a built-in theme's name; empty = Studio Teal
    std::map<juce::String, juce::uint32> colours;   // key -> ARGB; may be partial
    bool builtIn = false;
    juce::File file;                        // where it was read from (empty for a built-in)
    juce::StringArray unknownKeys;          // keys a newer or hand-edited file carried that this build does not know

    bool has (const juce::String& key) const { return colours.count (key) > 0; }
};

namespace ThemeStore
{
    inline constexpr const char* kDefaultName = "Studio Teal";
    inline constexpr const char* kExtension = ".dlivetheme.json";

    // Every colour a theme may set, in the order the sheet shows them.
    const std::vector<ThemeToken>& tokens();
    bool isToken (const juce::String& key);

    // The themes that ship with the app. The first is the default.
    const std::vector<Theme>& builtIn();
    const Theme* builtIn (const juce::String& name);
    bool isBuiltInName (const juce::String& name);

    // A complete palette: the theme's own colours over its base over the default.
    std::map<juce::String, juce::uint32> resolve (const Theme&);

    // "#rrggbb" for an opaque colour, "#aarrggbb" otherwise. Parsing accepts both, with or
    // without the '#', and "rgb"/"rgba" shorthand is deliberately not accepted.
    juce::String hex (juce::uint32 argb);
    bool parseHex (const juce::String&, juce::uint32& argb);

    // The document.
    juce::var toVar (const Theme&);
    bool fromVar (const juce::var&, Theme&, juce::String& why);
    bool load (const juce::File&, Theme&, juce::String& why);
    bool save (const Theme&, const juce::File&);

    // Where your themes live: beside the sessions, so backing up one folder backs up both.
    juce::File folder();
    juce::File fileFor (const juce::String& name, const juce::File& folder = ThemeStore::folder());
    juce::String safeFileName (const juce::String& name);

    // Your themes, by name; a file that will not read is skipped (and named in `refused`).
    std::vector<Theme> listUser (const juce::File& folder = ThemeStore::folder(), juce::StringArray* refused = nullptr);
    // Built-in first, then yours.
    std::vector<Theme> all (const juce::File& folder = ThemeStore::folder());
    // By name: a built-in, then one of yours, then the default. Never fails.
    Theme find (const juce::String& name, const juce::File& folder = ThemeStore::folder());

    // Saving one of yours. A built-in's name is refused (`why` says so), a name that is only
    // punctuation is refused, and the file is named after the theme.
    bool saveUser (Theme&, juce::String& why, const juce::File& folder = ThemeStore::folder());
    bool removeUser (const juce::String& name, const juce::File& folder = ThemeStore::folder());
    // A copy of somebody else's file into the folder, read first so a broken file is refused
    // with its reason rather than copied in. On success `imported` is the theme as stored.
    bool importFile (const juce::File& source, Theme& imported, juce::String& why, const juce::File& folder = ThemeStore::folder());

    // Which theme this Mac uses. Empty = the default.
    juce::File preferencesFile();
    juce::String chosenTheme (const juce::File& preferences = preferencesFile());
    bool setChosenTheme (const juce::String& name, const juce::File& preferences = preferencesFile());
}

} // namespace livemix
