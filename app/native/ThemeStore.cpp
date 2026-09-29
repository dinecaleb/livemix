#include "ThemeStore.h"

namespace livemix
{

namespace
{
    // The table. The key is both the JSON key and the `Dine::` token it sets; the binding
    // is made in app/ui/AppTheme.cpp (`Dine::themeBindings`) and a token without a binding,
    // or a binding without a token, is caught by the snapshot tool at start-up.
    const std::vector<ThemeToken> kTokens = {
        { "desk",        "Surfaces", "Behind the window, and the ground under the sidebar." },
        { "window",      "Surfaces", "The application panel: every workspace's ground." },
        { "toolbar",     "Surfaces", "The toolbar, the status foot and a side rail." },
        { "title",       "Surfaces", "The title row under the traffic lights." },
        { "menubar",     "Surfaces", "The transport pill and a segment track." },
        { "sidebar",     "Surfaces", "The sidebar down the left." },
        { "rail",        "Surfaces", "A panel at the edge of a workspace." },
        { "pageBar",     "Surfaces", "A workspace's own tool row." },
        { "console",     "Surfaces", "A mixer column, a timeline row." },
        { "tile",        "Surfaces", "A tile inside a workspace: a group, a stage device." },
        { "card",        "Surfaces", "A card, a sheet, a chosen column." },
        { "raised",      "Surfaces", "A row in a list, a popover." },
        { "item",        "Surfaces", "A row inside a card." },
        { "selected",    "Surfaces", "The row or segment that is chosen." },
        { "control",     "Surfaces", "A resting button." },
        { "controlHot",  "Surfaces", "A button under the pointer." },
        { "controlOn",   "Surfaces", "A setting that is on." },
        { "sheet",       "Surfaces", "A modal sheet." },
        { "popover",     "Surfaces", "Menus, tooltips and the HUD." },
        { "inset",       "Surfaces", "A well inside a card: a stage's own ground." },
        { "deep",        "Surfaces", "The deepest well: a meter track, an inset display." },
        { "refuse",      "Surfaces", "The ground of a refusal (amber type on it)." },
        { "recGround",   "Surfaces", "A card that is recording or clipping." },
        { "soloGround",  "Surfaces", "A soloed tile." },
        { "editGround",  "Surfaces", "A hand-edited chip." },
        { "hairSoft",    "Lines",    "The faintest rule." },
        { "hair",        "Lines",    "A rule between rows." },
        { "hairStrong",  "Lines",    "A rule that has to be seen." },
        { "edge",        "Lines",    "An outline that carries a meaning." },
        { "fill",        "Lines",    "A control's background on a plane." },
        { "fillHover",   "Lines",    "That background under the pointer." },
        { "fillSoft",    "Lines",    "A quieter fill." },
        { "well",        "Lines",    "A meter well, a slider track." },
        { "ink",         "Ink",      "Words." },
        { "ink2",        "Ink",      "Secondary words." },
        { "ink3",        "Ink",      "A caption, a resting icon." },
        { "ink4",        "Ink",      "A label, a disabled word." },
        { "glyph",       "Ink",      "A resting icon." },
        { "panMark",     "Ink",      "The centre mark of a balance." },
        { "accent",      "Accent",   "The primary action, the active tab, what is selected, what DLIVE tuned, the meters." },
        { "accentHover", "Accent",   "The accent under the pointer." },
        { "accentDeep",  "Accent",   "The accent pressed." },
        { "onAccent",    "Accent",   "Type on an accent button." },
        { "focusRing",   "Accent",   "The keyboard focus ring." },
        { "ok",          "Status",   "A healthy status chip." },
        { "hot",         "Status",   "The meter's middle band." },
        { "warn",        "Status",   "A warning: the mute key, a hand edit, a refusal." },
        { "crit",        "Status",   "Clipping, record, a critical status." },
        { "monitor",     "Status",   "The engineer's own listen." },
        { "keyMute",     "Keys",     "The M key when it is on." },
        { "keySolo",     "Keys",     "The S key when it is on." },
        { "keyRec",      "Keys",     "The R key when it is on." },
        { "keyMon",      "Keys",     "The A key when it is on." },
        { "keyFx",       "Keys",     "The FX key when the effects are on this microphone." },
        { "busDrums",    "Groups",   "The DRUMS group." },
        { "busBass",     "Groups",   "The BASS group." },
        { "busMusic",    "Groups",   "The MUSIC group." },
        { "busVocals",   "Groups",   "The BGV group - the backing voices." },
        { "busLead",     "Groups",   "The LEAD group - the one voice the mix is built around." },
        { "busSpeech",   "Groups",   "The SPEECH group." },
        { "busAmbience", "Groups",   "The AMBIENCE group." },
        { "busMaster",   "Groups",   "The MASTER." },

    };

    using Palette = std::map<juce::String, juce::uint32>;

    Theme make (const char* name, const char* note, const char* basedOn, Palette colours)
    {
        Theme t;
        t.name = name;
        t.note = note;
        t.basedOn = basedOn;
        t.colours = std::move (colours);
        t.builtIn = true;
        return t;
    }

    // The v3 design, exactly as app/ui/AppTheme.h declares it. Every key is present, so this
    // is the floor every other theme resolves down to.
    Theme studioTeal()
    {
        return make ("Studio Teal", "Flat neutral planes, one teal accent.", "", {
            { "desk", 0xff0b0b0c }, { "window", 0xff161719 }, { "toolbar", 0xff1d1e21 }, { "title", 0xff1d1e21 },
            { "menubar", 0xff111214 }, { "sidebar", 0xff1a1b1e }, { "rail", 0xff18191c }, { "pageBar", 0xff161719 },
            { "console", 0xff18191c }, { "tile", 0xff18191c }, { "card", 0xff202226 }, { "raised", 0xff202226 },
            { "item", 0xff1c1d20 }, { "selected", 0xff2a2c30 }, { "control", 0xff2b2d31 }, { "controlHot", 0xff35373c },
            { "controlOn", 0xff44474d }, { "sheet", 0xff202226 }, { "popover", 0xff202226 },
            { "inset", 0xff151619 }, { "deep", 0xff111214 }, { "refuse", 0xff2a2218 },
            { "recGround", 0xff2d1f1f }, { "soloGround", 0xff2a2c30 }, { "editGround", 0xff1f2730 },
            { "hairSoft", 0x0fffffff }, { "hair", 0x14ffffff }, { "hairStrong", 0x1fffffff }, { "edge", 0x33ffffff },
            { "fill", 0x1effffff }, { "fillHover", 0x2affffff }, { "fillSoft", 0x0fffffff }, { "well", 0x0fffffff },
            { "ink", 0xfff2f2f4 }, { "ink2", 0xffc2c4c9 }, { "ink3", 0xff9a9da4 }, { "ink4", 0xff74777d },
            { "glyph", 0xffc2c4c9 }, { "panMark", 0xff74777d },
            { "accent", 0xff6db8a8 }, { "accentHover", 0xff8ed0c2 }, { "accentDeep", 0xff5aa393 }, { "onAccent", 0xff0b0d10 },
            { "focusRing", 0xff6db8a8 },
            { "ok", 0xff57b98d }, { "hot", 0xffd8c46a }, { "warn", 0xffe0a85c }, { "crit", 0xfff06a61 }, { "monitor", 0xff6eafff },
            { "keyMute", 0xffe5534b }, { "keySolo", 0xff6db8a8 }, { "keyRec", 0xffe5534b }, { "keyMon", 0xff6eafff },
            { "keyFx", 0xffc98fb0 },
            { "busDrums", 0xffe09a4b }, { "busBass", 0xff8e80ff }, { "busMusic", 0xff6eafff }, { "busVocals", 0xff57b98d },
            { "busSpeech", 0xffc98fb0 }, { "busAmbience", 0xff9a9da4 }, { "busMaster", 0xffc2c4c9 },
            { "busLead", 0xfff07f8f },
        });
    }

    // A pure black desk, neutral greys and a lime that is the only colour on it - the
    // "lime is the signal" look, for the booth that mixes in the dark.
    Theme limeDesk()
    {
        return make ("Lime Desk", "A black desk and one lime accent.", "Studio Teal", {
            { "desk", 0xff0a0a0a }, { "window", 0xff111111 }, { "toolbar", 0xff151515 }, { "title", 0xff0f0f0f },
            { "menubar", 0xff0c0c0c }, { "sidebar", 0xff0f0f0f }, { "rail", 0xff151515 }, { "pageBar", 0xff131313 },
            { "console", 0xff161616 }, { "tile", 0xff131313 }, { "card", 0xff191919 }, { "raised", 0xff1d1d1d },
            { "item", 0xff161616 }, { "selected", 0xff242424 }, { "control", 0xff2b2b2b }, { "controlHot", 0xff3a3a3a },
            { "controlOn", 0xff4c4c4c }, { "sheet", 0xff191919 }, { "popover", 0xff1d1d1d },
            { "inset", 0xff0d0d0d }, { "deep", 0xff090909 }, { "refuse", 0xff231d14 },
            { "recGround", 0xff241515 }, { "soloGround", 0xff242424 }, { "editGround", 0xff1c2216 },
            { "ink", 0xfff2f2f2 }, { "ink2", 0xffa3a3a3 }, { "ink3", 0xff707070 }, { "ink4", 0xff4d4d4d },
            { "glyph", 0xff707070 }, { "panMark", 0xff5a5a5a },
            { "accent", 0xffd6f54c }, { "accentHover", 0xffe4ff72 }, { "accentDeep", 0xffb9d63a }, { "onAccent", 0xff0a0a0a },
            { "focusRing", 0xffd6f54c },
            { "ok", 0xff9ad96a }, { "hot", 0xffe8d45a }, { "warn", 0xffe6a85a }, { "crit", 0xffe8735f }, { "monitor", 0xff7ab4ff },
            { "keyMute", 0xffe6a85a }, { "keySolo", 0xffd6f54c }, { "keyRec", 0xffe8735f }, { "keyMon", 0xff7ab4ff },
            { "busDrums", 0xffe0a25a }, { "busBass", 0xff9a8cff }, { "busMusic", 0xff7ab4ff }, { "busVocals", 0xffd6f54c },
            { "busSpeech", 0xffcf98b8 }, { "busAmbience", 0xffa3a3a3 }, { "busMaster", 0xffa3a3a3 },
            { "busLead", 0xfff08a78 },
        });
    }

    // Cooler and a shade lighter than the default, with a sky-blue accent: for a bright booth.
    Theme slate()
    {
        return make ("Slate", "Lighter blue-grey, a sky-blue accent.", "Studio Teal", {
            { "desk", 0xff0c1015 }, { "window", 0xff151b23 }, { "toolbar", 0xff1b232d }, { "title", 0xff121820 },
            { "menubar", 0xff10151c }, { "sidebar", 0xff121820 }, { "rail", 0xff1b232d }, { "pageBar", 0xff18202a },
            { "console", 0xff1b232d }, { "tile", 0xff18202a }, { "card", 0xff222c38 }, { "raised", 0xff25303d },
            { "item", 0xff1e2732 }, { "selected", 0xff2c3745 }, { "control", 0xff35414f }, { "controlHot", 0xff485565 },
            { "controlOn", 0xff586677 }, { "sheet", 0xff222c38 }, { "popover", 0xff25303d },
            { "inset", 0xff121820 }, { "deep", 0xff0e141b }, { "refuse", 0xff2c261d },
            { "recGround", 0xff2c1e21 }, { "soloGround", 0xff2c3745 }, { "editGround", 0xff1e2c3a },
            { "ink", 0xfff5f7fa }, { "ink2", 0xffb1bac7 }, { "ink3", 0xff76818f }, { "ink4", 0xff55606e },
            { "glyph", 0xff76818f }, { "panMark", 0xff5d6a7a },
            { "accent", 0xff7fb3ff }, { "accentHover", 0xff9dc6ff }, { "accentDeep", 0xff669ce6 }, { "onAccent", 0xff0c1015 },
            { "focusRing", 0xff7fb3ff },
            { "monitor", 0xff8ed0c2 }, { "keyMon", 0xff8ed0c2 }, { "keySolo", 0xff7fb3ff },
            { "busMusic", 0xff7fb3ff },
        });
    }

    // Warm browns and an amber accent - the colour of a tape machine's meters.
    Theme tape()
    {
        return make ("Tape", "Warm browns, an amber accent.", "Studio Teal", {
            { "desk", 0xff0b0908 }, { "window", 0xff141110 }, { "toolbar", 0xff1a1614 }, { "title", 0xff110e0d },
            { "menubar", 0xff0e0c0b }, { "sidebar", 0xff110e0d }, { "rail", 0xff1a1614 }, { "pageBar", 0xff171412 },
            { "console", 0xff1a1614 }, { "tile", 0xff171412 }, { "card", 0xff221d1a }, { "raised", 0xff26201c },
            { "item", 0xff1d1815 }, { "selected", 0xff2c2622 }, { "control", 0xff362e29 }, { "controlHot", 0xff4a403a },
            { "controlOn", 0xff5b4f48 }, { "sheet", 0xff221d1a }, { "popover", 0xff26201c },
            { "inset", 0xff110e0d }, { "deep", 0xff0d0b0a }, { "refuse", 0xff2a1f14 },
            { "recGround", 0xff2c1a18 }, { "soloGround", 0xff2c2622 }, { "editGround", 0xff2a2216 },
            { "ink", 0xfff7f3ee }, { "ink2", 0xffbcb1a6 }, { "ink3", 0xff7f7369 }, { "ink4", 0xff5b514a },
            { "glyph", 0xff7f7369 }, { "panMark", 0xff655a52 },
            { "accent", 0xfff0a650 }, { "accentHover", 0xfff8bd74 }, { "accentDeep", 0xffd28f3f }, { "onAccent", 0xff0b0908 },
            { "focusRing", 0xfff0a650 },
            { "hot", 0xffe6d05a }, { "warn", 0xffe9c35c }, { "keyMute", 0xffe9c35c }, { "keySolo", 0xfff0a650 },
            { "busDrums", 0xffe6c35c }, { "busVocals", 0xff6dc19a }, { "busMusic", 0xff8fb8f0 },
            { "busLead", 0xffe08a72 },
        });
    }

    // A light desk for a booth with a window. Every line and fill that was white on black is
    // black on white here; the accent is a teal deep enough to carry white type.
    Theme daylight()
    {
        return make ("Daylight", "A light desk for a booth with a window.", "Studio Teal", {
            { "desk", 0xffd6dae1 }, { "window", 0xfff2f3f5 }, { "toolbar", 0xffe5e8ec }, { "title", 0xffdfe3e8 },
            { "menubar", 0xffd9dde3 }, { "sidebar", 0xffdfe3e8 }, { "rail", 0xffe5e8ec }, { "pageBar", 0xffeaecf0 },
            { "console", 0xffe5e8ec }, { "tile", 0xffeaecf0 }, { "card", 0xffffffff }, { "raised", 0xfff7f8fa },
            { "item", 0xfff2f3f5 }, { "selected", 0xffdde2ea }, { "control", 0xffd2d8e0 }, { "controlHot", 0xffc2c9d3 },
            { "controlOn", 0xffa9b2bf }, { "sheet", 0xffffffff }, { "popover", 0xffffffff },
            { "inset", 0xffeaecf0 }, { "deep", 0xffe3e6ea }, { "refuse", 0xfff6ecdc },
            { "recGround", 0xfff8e2e0 }, { "soloGround", 0xffdde2ea }, { "editGround", 0xffdde9f3 },
            { "hairSoft", 0x0f000000 }, { "hair", 0x14000000 }, { "hairStrong", 0x1f000000 }, { "edge", 0x33000000 },
            { "fill", 0x14000000 }, { "fillHover", 0x22000000 }, { "fillSoft", 0x0a000000 }, { "well", 0x14000000 },
            { "ink", 0xff14171c }, { "ink2", 0xff4a5260 }, { "ink3", 0xff7a8390 }, { "ink4", 0xffa3abb7 },
            { "glyph", 0xff7a8390 }, { "panMark", 0xff9aa3af },
            { "accent", 0xff1f8f7a }, { "accentHover", 0xff26a68e }, { "accentDeep", 0xff197465 }, { "onAccent", 0xffffffff },
            { "focusRing", 0xff1f8f7a },
            { "ok", 0xff2f9a68 }, { "hot", 0xffb89a1e }, { "warn", 0xffc47f1e }, { "crit", 0xffcf3f38 }, { "monitor", 0xff2b78d6 },
            { "keyMute", 0xffc47f1e }, { "keySolo", 0xff1f8f7a }, { "keyRec", 0xffcf3f38 }, { "keyMon", 0xff2b78d6 },
            { "busDrums", 0xffc47a1e }, { "busBass", 0xff6a5ce0 }, { "busMusic", 0xff2b78d6 }, { "busVocals", 0xff2f9a68 },
            { "busSpeech", 0xffb0508f }, { "busAmbience", 0xff6b7380 }, { "busMaster", 0xff4a5260 },
            { "busLead", 0xffd1435b },
        });
    }

    juce::DynamicObject* object (const juce::var& v) { return v.getDynamicObject(); }
}

// ---------------------------------------------------------------- the table
const std::vector<ThemeToken>& ThemeStore::tokens() { return kTokens; }

bool ThemeStore::isToken (const juce::String& key)
{
    for (const auto& t : kTokens) if (key == t.key) return true;
    return false;
}

// ---------------------------------------------------------------- built in
const std::vector<Theme>& ThemeStore::builtIn()
{
    static const std::vector<Theme> themes = { studioTeal(), limeDesk(), slate(), tape(), daylight() };
    return themes;
}

const Theme* ThemeStore::builtIn (const juce::String& name)
{
    for (const auto& t : builtIn()) if (t.name.equalsIgnoreCase (name)) return &t;
    return nullptr;
}

bool ThemeStore::isBuiltInName (const juce::String& name) { return builtIn (name) != nullptr; }

std::map<juce::String, juce::uint32> ThemeStore::resolve (const Theme& theme)
{
    // The default, then the base it names (if that is a built-in other than the default),
    // then the theme's own colours. A base that is not a built-in is ignored rather than
    // followed: a chain of user themes could loop, and a theme should stand on the app.
    auto out = builtIn().front().colours;
    if (const auto* base = builtIn (theme.basedOn))
        for (const auto& kv : base->colours) out[kv.first] = kv.second;
    for (const auto& kv : theme.colours)
        if (isToken (kv.first)) out[kv.first] = kv.second;
    return out;
}

// ---------------------------------------------------------------- hex
juce::String ThemeStore::hex (juce::uint32 argb)
{
    const auto alpha = (argb >> 24) & 0xff;
    return "#" + juce::String::toHexString (int (alpha == 0xff ? (argb & 0xffffff) : argb))
                     .paddedLeft ('0', alpha == 0xff ? 6 : 8);
}

bool ThemeStore::parseHex (const juce::String& text, juce::uint32& argb)
{
    auto s = text.trim();
    if (s.startsWithChar ('#')) s = s.substring (1);
    if (s.length() != 6 && s.length() != 8) return false;
    static const juce::String hexDigits ("0123456789abcdefABCDEF");
    for (auto c : s) if (! hexDigits.containsChar (c)) return false;
    const auto value = juce::uint32 (s.getHexValue64());
    argb = s.length() == 6 ? (0xff000000u | value) : value;
    return true;
}

// ---------------------------------------------------------------- the document
juce::var ThemeStore::toVar (const Theme& t)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "DLIVE");
    root->setProperty ("kind", "theme");
    root->setProperty ("schema", Theme::kSchemaVersion);
    root->setProperty ("name", t.name);
    if (t.note.isNotEmpty()) root->setProperty ("note", t.note);
    if (t.basedOn.isNotEmpty()) root->setProperty ("basedOn", t.basedOn);
    auto* colours = new juce::DynamicObject();
    for (const auto& tok : kTokens)            // in table order, so a diff between two files reads
    {
        const auto it = t.colours.find (tok.key);
        if (it != t.colours.end()) colours->setProperty (tok.key, hex (it->second));
    }
    root->setProperty ("colours", juce::var (colours));
    return juce::var (root);
}

bool ThemeStore::fromVar (const juce::var& v, Theme& out, juce::String& why)
{
    auto* root = object (v);
    if (root == nullptr) { why = "This is not a DLIVE theme file."; return false; }
    if (root->getProperty ("kind").toString() != "theme") { why = "This is not a DLIVE theme file."; return false; }
    const int schema = int (root->getProperty ("schema"));
    if (schema > Theme::kSchemaVersion)
    {
        why = "This theme was made by a newer DLIVE (schema " + juce::String (schema) + "); this build reads schema "
              + juce::String (Theme::kSchemaVersion) + ".";
        return false;
    }
    Theme t;
    t.name = root->getProperty ("name").toString().trim();
    if (t.name.isEmpty()) { why = "The theme has no name."; return false; }
    t.note = root->getProperty ("note").toString();
    t.basedOn = root->getProperty ("basedOn").toString();
    auto* colours = object (root->getProperty ("colours"));
    if (colours == nullptr) { why = "The theme has no colours."; return false; }
    for (const auto& p : colours->getProperties())
    {
        const auto key = p.name.toString();
        if (! isToken (key)) { t.unknownKeys.add (key); continue; }
        juce::uint32 argb = 0;
        if (! parseHex (p.value.toString(), argb))
        {
            why = "'" + key + "' is not a colour: " + p.value.toString() + " (expected #rrggbb or #aarrggbb).";
            return false;
        }
        t.colours[key] = argb;
    }
    out = std::move (t);
    return true;
}

bool ThemeStore::load (const juce::File& file, Theme& out, juce::String& why)
{
    if (! file.existsAsFile()) { why = "There is no file at " + file.getFullPathName(); return false; }
    juce::var v;
    const auto result = juce::JSON::parse (file.loadFileAsString(), v);
    if (result.failed()) { why = "The file does not read as JSON: " + result.getErrorMessage(); return false; }
    if (! fromVar (v, out, why)) return false;
    out.file = file;
    return true;
}

bool ThemeStore::save (const Theme& t, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (toVar (t), false));
}

// ---------------------------------------------------------------- the folder
juce::File ThemeStore::folder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DLIVE").getChildFile ("Themes");
}

juce::String ThemeStore::safeFileName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).trim();
}

juce::File ThemeStore::fileFor (const juce::String& name, const juce::File& dir)
{
    return dir.getChildFile (safeFileName (name) + kExtension);
}

std::vector<Theme> ThemeStore::listUser (const juce::File& dir, juce::StringArray* refused)
{
    std::vector<Theme> out;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*" + juce::String (kExtension)))
    {
        Theme t; juce::String why;
        if (load (f, t, why)) { if (! isBuiltInName (t.name)) out.push_back (std::move (t)); }
        else if (refused != nullptr) refused->add (f.getFileName() + ": " + why);
    }
    std::sort (out.begin(), out.end(), [] (const Theme& a, const Theme& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

std::vector<Theme> ThemeStore::all (const juce::File& dir)
{
    auto out = builtIn();
    for (auto& t : listUser (dir)) out.push_back (std::move (t));
    return out;
}

Theme ThemeStore::find (const juce::String& name, const juce::File& dir)
{
    if (const auto* b = builtIn (name)) return *b;
    for (auto& t : listUser (dir)) if (t.name.equalsIgnoreCase (name)) return t;
    return builtIn().front();
}

bool ThemeStore::saveUser (Theme& t, juce::String& why, const juce::File& dir)
{
    t.name = t.name.trim();
    if (t.name.isEmpty() || safeFileName (t.name).isEmpty()) { why = "Give the theme a name first."; return false; }
    if (isBuiltInName (t.name)) { why = "'" + t.name + "' is one of DLIVE's own themes. Save yours under another name."; return false; }
    t.builtIn = false;
    t.file = fileFor (t.name, dir);
    if (! save (t, t.file)) { why = "The theme could not be written to " + t.file.getFullPathName(); return false; }
    return true;
}

bool ThemeStore::removeUser (const juce::String& name, const juce::File& dir)
{
    if (isBuiltInName (name)) return false;
    for (const auto& t : listUser (dir))
        if (t.name.equalsIgnoreCase (name)) return t.file.deleteFile();
    return false;
}

bool ThemeStore::importFile (const juce::File& source, Theme& imported, juce::String& why, const juce::File& dir)
{
    Theme t;
    if (! load (source, t, why)) return false;
    if (isBuiltInName (t.name)) t.name += " (imported)";
    return saveUser (t, why, dir) && (imported = t, true);
}

// ---------------------------------------------------------------- the preference
juce::File ThemeStore::preferencesFile()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DLIVE").getChildFile ("preferences.json");
}

juce::String ThemeStore::chosenTheme (const juce::File& prefs)
{
    if (! prefs.existsAsFile()) return {};
    juce::var v;
    if (juce::JSON::parse (prefs.loadFileAsString(), v).failed()) return {};
    if (auto* o = object (v)) return o->getProperty ("theme").toString().trim();
    return {};
}

bool ThemeStore::setChosenTheme (const juce::String& name, const juce::File& prefs)
{
    // The preferences file may carry other settings one day; only the theme is touched.
    juce::var v;
    if (prefs.existsAsFile()) juce::JSON::parse (prefs.loadFileAsString(), v);
    juce::DynamicObject::Ptr o = object (v);
    if (o == nullptr) o = new juce::DynamicObject();
    if (name.isEmpty() || name.equalsIgnoreCase (kDefaultName)) o->removeProperty ("theme");
    else o->setProperty ("theme", name);
    prefs.getParentDirectory().createDirectory();
    return prefs.replaceWithText (juce::JSON::toString (juce::var (o.get()), false));
}

const std::vector<ThemeStore::TextSize>& ThemeStore::textSizes()
{
    static const std::vector<TextSize> sizes = { { "Standard", 1.0f }, { "Large", 1.2f }, { "Larger", 1.35f } };
    return sizes;
}

float ThemeStore::chosenTextSize (const juce::File& prefs)
{
    if (! prefs.existsAsFile()) return 1.0f;
    juce::var v;
    if (juce::JSON::parse (prefs.loadFileAsString(), v).failed()) return 1.0f;
    auto* o = object (v);
    if (o == nullptr || ! o->hasProperty ("textSize")) return 1.0f;
    const auto wanted = float (double (o->getProperty ("textSize")));
    // Only a size this build offers. A file written by a later version, or by hand, reads
    // back as Standard rather than as some scale no menu can get you out of.
    for (const auto& size : textSizes())
        if (std::abs (size.scale - wanted) < 0.001f) return size.scale;
    return 1.0f;
}

bool ThemeStore::setChosenTextSize (float scale, const juce::File& prefs)
{
    juce::var v;
    if (prefs.existsAsFile()) juce::JSON::parse (prefs.loadFileAsString(), v);
    juce::DynamicObject::Ptr o = object (v);
    if (o == nullptr) o = new juce::DynamicObject();
    if (std::abs (scale - 1.0f) < 0.001f) o->removeProperty ("textSize");   // the default is not written down
    else o->setProperty ("textSize", double (scale));
    prefs.getParentDirectory().createDirectory();
    return prefs.replaceWithText (juce::JSON::toString (juce::var (o.get()), false));
}

// ---------------------------------------------------------------------------- the guides
namespace
{
    juce::var readPrefs (const juce::File& prefs)
    {
        juce::var v;
        if (prefs.existsAsFile()) juce::JSON::parse (prefs.loadFileAsString(), v);
        return v;
    }

    bool writePrefs (const juce::File& prefs, juce::DynamicObject::Ptr o)
    {
        prefs.getParentDirectory().createDirectory();
        return prefs.replaceWithText (juce::JSON::toString (juce::var (o.get()), false));
    }

    // The dismissed guides, as one comma-separated string: a handful of short keys, and a
    // string keeps the preferences file readable by a person looking for what went wrong.
    juce::StringArray seenList (const juce::File& prefs)
    {
        auto v = readPrefs (prefs);
        if (auto* o = object (v))
            return juce::StringArray::fromTokens (o->getProperty ("guidesSeen").toString(), ",", "");
        return {};
    }
}

bool Guides::seen (const juce::String& key, const juce::File& prefs)
{
    return seenList (prefs).contains (key);
}

void Guides::markSeen (const juce::String& key, const juce::File& prefs)
{
    auto list = seenList (prefs);
    if (list.contains (key)) return;
    list.add (key);
    auto v = readPrefs (prefs);
    juce::DynamicObject::Ptr o = object (v);
    if (o == nullptr) o = new juce::DynamicObject();
    o->setProperty ("guidesSeen", list.joinIntoString (","));
    writePrefs (prefs, o);
}

bool Guides::enabled (const juce::File& prefs)
{
    auto v = readPrefs (prefs);
    if (auto* o = object (v); o != nullptr && o->hasProperty ("guides"))
        return bool (o->getProperty ("guides"));
    return true;        // on until somebody turns them off
}

void Guides::setEnabled (bool on, const juce::File& prefs)
{
    auto v = readPrefs (prefs);
    juce::DynamicObject::Ptr o = object (v);
    if (o == nullptr) o = new juce::DynamicObject();
    if (on) o->removeProperty ("guides");      // on is the default, so it is not written down
    else    o->setProperty ("guides", false);
    writePrefs (prefs, o);
}

void Guides::reset (const juce::File& prefs)
{
    auto v = readPrefs (prefs);
    juce::DynamicObject::Ptr o = object (v);
    if (o == nullptr) o = new juce::DynamicObject();
    o->removeProperty ("guidesSeen");
    o->removeProperty ("guides");
    writePrefs (prefs, o);
}

} // namespace livemix
