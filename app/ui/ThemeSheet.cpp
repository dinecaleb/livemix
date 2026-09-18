#include "ThemeSheet.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace livemix
{

namespace
{
    constexpr int kCardW = 1120, kCardH = 760, kRailW = 236, kRowH = 44, kSwatchH = 54, kGap = 8;

    juce::String dot()   { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")); }

    // A colour picker that reports every move. The callout owns it; the swatch it reports
    // to is held by a SafePointer, so a rebuild under an open picker is harmless.
    class Picker : public juce::ColourSelector, private juce::ChangeListener
    {
    public:
        Picker() : juce::ColourSelector (showColourAtTop | showSliders | showColourspace | editableColour, 4, 7)
        {
            addChangeListener (this);
            setSize (280, 400);
        }
        ~Picker() override { removeChangeListener (this); }
        std::function<void (juce::Colour)> onColour;
    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onColour) onColour (getCurrentColour()); }
    };
}

// ---------------------------------------------------------------- a theme in the rail
class ThemeSheet::ThemeRow : public juce::Button
{
public:
    ThemeRow (const Theme& t) : juce::Button (t.name), theme (t) { setWantsKeyboardFocus (false); }
    const Theme theme;
    bool chosen = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
        if (chosen)      Dine::fillRounded (g, r, Dine::selected, Dine::Radius::control);
        else if (over)   Dine::fillRounded (g, r, Dine::fillSoft, Dine::Radius::control);
        if (chosen)
        {
            g.setColour (Dine::accent);
            g.fillRoundedRectangle (r.getX() + 4.0f, r.getY() + 10.0f, 3.0f, r.getHeight() - 20.0f, 1.5f);
        }
        auto text = getLocalBounds().reduced (16, 0);
        g.setColour (chosen || over ? Dine::ink : Dine::ink2);
        g.setFont (Dine::text (13.0f, chosen ? 600 : 500));
        g.drawText (theme.name, text.removeFromTop (getHeight() / 2 + 2), juce::Justification::bottomLeft);
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (10.5f));
        g.drawText (theme.note.isNotEmpty() ? theme.note : (theme.builtIn ? "DLIVE's own" : "Yours"),
                    text, juce::Justification::topLeft, true);
    }
};

// ---------------------------------------------------------------- a colour
class ThemeSheet::Swatch : public juce::Button
{
public:
    Swatch (ThemeSheet& owner, const ThemeToken& t) : juce::Button (t.key), sheet (owner), token (t)
    {
        for (const auto& b : Dine::themeBindings()) if (juce::String (b.key) == t.key) live = b.colour;
        setWantsKeyboardFocus (false);
        setTooltip (t.what);
        onClick = [this] { open(); };
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        Dine::fillRounded (g, r, over ? Dine::raised : Dine::item, Dine::Radius::control);
        const auto c = live != nullptr ? *live : juce::Colours::transparentBlack;
        auto block = r.reduced (8.0f).removeFromLeft (38.0f);
        // A translucent token (a hairline, a fill) is shown over the window it is drawn on.
        if (! c.isOpaque()) Dine::fillRounded (g, block, Dine::window, Dine::Radius::chip);
        Dine::fillRounded (g, block, c, Dine::Radius::chip);
        g.setColour (Dine::hairStrong);
        g.drawRoundedRectangle (block.reduced (0.5f), Dine::Radius::chip, 1.0f);
        auto text = getLocalBounds().reduced (8).withTrimmedLeft (46);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (11.5f, 600));
        g.drawText (token.key, text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft);
        g.setColour (Dine::ink3);
        g.setFont (Dine::mono (10.5f));
        g.drawText (ThemeStore::hex (c.getARGB()), text, juce::Justification::topLeft);
    }

private:
    void open()
    {
        auto picker = std::make_unique<Picker>();
        picker->setCurrentColour (live != nullptr ? *live : juce::Colours::black, juce::dontSendNotification);
        juce::Component::SafePointer<Swatch> safe (this);
        const juce::String key (token.key);
        picker->onColour = [safe, key] (juce::Colour c)
        {
            if (safe != nullptr) safe->sheet.colourEdited (key, c);
        };
        juce::CallOutBox::launchAsynchronously (std::move (picker), getScreenBounds(), nullptr);
    }

    ThemeSheet& sheet;
    const ThemeToken token;
    const juce::Colour* live = nullptr;
};

// ---------------------------------------------------------------- the grid
class ThemeSheet::Grid : public juce::Component
{
public:
    struct Header { juce::String label; int y; };
    std::vector<Header> headers;
    std::vector<Swatch*> cells;

    void layout (int width)
    {
        headers.clear();
        const int cols = juce::jmax (1, (width + kGap) / (176 + kGap));
        const int cellW = (width - (cols - 1) * kGap) / cols;
        int y = 0, col = 0;
        juce::String group;
        for (auto* s : cells)
        {
            const auto thisGroup = groupOf (s->getName());
            if (thisGroup != group)
            {
                if (group.isNotEmpty()) y += kSwatchH + 18;
                group = thisGroup;
                headers.push_back ({ group.toUpperCase(), y });
                y += 22;
                col = 0;
            }
            else if (col == cols) { col = 0; y += kSwatchH + kGap; }
            s->setBounds (col * (cellW + kGap), y, cellW, kSwatchH);
            ++col;
        }
        setSize (width, y + kSwatchH + 8);
    }

    void paint (juce::Graphics& g) override
    {
        for (const auto& h : headers)
            Dine::drawCaption (g, { 0, h.y, getWidth(), 18 }, h.label);
    }

private:
    static juce::String groupOf (const juce::String& key)
    {
        for (const auto& t : ThemeStore::tokens()) if (key == t.key) return t.group;
        return {};
    }
};

// ---------------------------------------------------------------- the sheet
ThemeSheet::ThemeSheet (bool persist) : persisting (persist)
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (railView);
    railView.setViewedComponent (&railContent, false);
    railView.setScrollBarsShown (true, false);
    Dine::nativeScrolling (railView);

    grid = std::make_unique<Grid>();
    addAndMakeVisible (gridView);
    gridView.setViewedComponent (grid.get(), false);
    gridView.setScrollBarsShown (true, false);
    Dine::nativeScrolling (gridView);

    addAndMakeVisible (nameBox);
    nameBox.setFont (Dine::text (13.0f));
    nameBox.setJustification (juce::Justification::centredLeft);
    nameBox.setIndents (10, 0);
    nameBox.setBorder (juce::BorderSize<int> (0));
    nameBox.setTextToShowWhenEmpty ("Name this theme", Dine::ink4);
    Dine::styleTextEditor (nameBox, Dine::control);
    nameBox.onReturnKey = [this] { saveTheme(); };
    nameBox.onTextChange = [this] { updateButtons(); };

    for (auto* b : { &saveButton, &resetButton, &deleteButton, &importButton, &exportButton, &folderButton, &closeButton })
        addAndMakeVisible (*b);
    saveButton.setTooltip ("Write this theme to ~/Music/DLIVE/Themes under the name in the box. DLIVE's own themes are never overwritten.");
    resetButton.setTooltip ("Put back the theme as it was chosen, dropping the colours you changed.");
    deleteButton.setTooltip ("Remove this theme's file. DLIVE goes back to Studio Teal.");
    importButton.setTooltip ("Copy a theme file somebody sent you into your themes. A file that will not read is refused with its reason.");
    exportButton.setTooltip ("Write this theme as a complete file that stands on its own, to send to somebody else.");
    folderButton.setTooltip ("Open ~/Music/DLIVE/Themes in the Finder.");
    saveButton.onClick = [this] { saveTheme(); };
    resetButton.onClick = [this] { resetTheme(); };
    deleteButton.onClick = [this] { deleteTheme(); };
    importButton.onClick = [this] { importTheme(); };
    exportButton.onClick = [this] { exportTheme(); };
    folderButton.onClick = [this] { revealFolder(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };

    // The sample: the keys on, a filled and a resting button, a balance. They are real
    // widgets reading the live tokens, so what the picker moves is what the console gets.
    for (auto* k : { &keyMute, &keySolo, &keyRec, &keyMon }) { addAndMakeVisible (*k); k->setOn (true); k->setInterceptsMouseClicks (false, false); }
    for (auto* b : { &sampleFilled, &sampleStandard }) { addAndMakeVisible (*b); b->setInterceptsMouseClicks (false, false); }
    sampleFilled.setCaps (true);
    addAndMakeVisible (samplePan);
    samplePan.setValue (0.3f);
    samplePan.setInterceptsMouseClicks (false, false);

    refresh();
    const auto chosen = ThemeStore::chosenTheme();
    for (const auto& t : themes)
        if (t.name.equalsIgnoreCase (chosen.isEmpty() ? juce::String (ThemeStore::kDefaultName) : chosen)) { select (t); break; }
    if (selected.name.isEmpty() && ! themes.empty()) select (themes.front());
}

ThemeSheet::~ThemeSheet() = default;

void ThemeSheet::refresh()
{
    themes = ThemeStore::all();
    rebuildRows();
    if (swatches.empty()) rebuildSwatches();
    resized();
}

void ThemeSheet::rebuildRows()
{
    rows.clear();
    railContent.removeAllChildren();
    for (const auto& t : themes)
    {
        auto row = std::make_unique<ThemeRow> (t);
        row->chosen = t.name.equalsIgnoreCase (selected.name);
        const auto name = t.name;
        row->onClick = [this, name] { chooseTheme (name); };
        railContent.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
}

void ThemeSheet::rebuildSwatches()
{
    swatches.clear();
    grid->cells.clear();
    for (const auto& t : ThemeStore::tokens())
    {
        auto s = std::make_unique<Swatch> (*this, t);
        grid->addAndMakeVisible (*s);
        grid->cells.push_back (s.get());
        swatches.push_back (std::move (s));
    }
}

void ThemeSheet::chooseTheme (const juce::String& name)
{
    for (const auto& t : themes)
        if (t.name.equalsIgnoreCase (name)) { select (t); return; }
}

void ThemeSheet::select (const Theme& t)
{
    Dine::applyTheme (t);
    selected = t;
    working = t;
    working.colours = Dine::currentColours();
    edited = false;
    if (persisting) ThemeStore::setChosenTheme (t.name);
    nameBox.setText (t.builtIn ? t.name + " (mine)" : t.name, juce::dontSendNotification);
    for (auto& r : rows) { r->chosen = r->theme.name.equalsIgnoreCase (t.name); r->repaint(); }
    themeChanged();
}

void ThemeSheet::colourEdited (const juce::String& key, juce::Colour c)
{
    Dine::setThemeColour (key, c);
    working.colours[key] = c.getARGB();
    edited = true;
    themeChanged();
}

void ThemeSheet::themeChanged()
{
    Dine::refreshAllWindows();
    if (onThemeChanged) onThemeChanged();
    updateButtons();
    repaint();
}

juce::String ThemeSheet::baseName() const
{
    if (selected.builtIn) return selected.name;
    if (ThemeStore::isBuiltInName (selected.basedOn)) return selected.basedOn;
    return ThemeStore::kDefaultName;
}

void ThemeSheet::saveTheme()
{
    Theme t;
    t.name = nameBox.getText().trim();
    t.note = selected.builtIn ? juce::String() : selected.note;
    t.basedOn = baseName();
    // Only what differs from the base goes into the file, so it reads as what it changes.
    const auto base = ThemeStore::resolve (ThemeStore::find (t.basedOn));
    for (const auto& kv : working.colours)
    {
        const auto it = base.find (kv.first);
        if (it == base.end() || it->second != kv.second) t.colours[kv.first] = kv.second;
    }
    juce::String why;
    if (! ThemeStore::saveUser (t, why)) { if (onToast) onToast (why); return; }
    edited = false;
    refresh();
    chooseTheme (t.name);
    if (onToast) onToast ("Saved the theme \"" + t.name + "\".");
}

void ThemeSheet::resetTheme()
{
    select (selected);
}

void ThemeSheet::deleteTheme()
{
    if (selected.builtIn) return;
    const auto name = selected.name;
    if (! ThemeStore::removeUser (name)) { if (onToast) onToast ("The theme could not be removed."); return; }
    refresh();
    chooseTheme (ThemeStore::kDefaultName);
    if (onToast) onToast ("Removed \"" + name + "\". Back to " + juce::String (ThemeStore::kDefaultName) + ".");
}

void ThemeSheet::importTheme()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a DLIVE theme file",
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
                                                   "*" + juce::String (ThemeStore::kExtension) + ";*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        Theme imported; juce::String why;
        if (! ThemeStore::importFile (file, imported, why)) { if (onToast) onToast (why); return; }
        refresh();
        chooseTheme (imported.name);
        if (onToast) onToast ("Imported \"" + imported.name + "\".");
    });
}

void ThemeSheet::exportTheme()
{
    const auto name = nameBox.getText().trim().isNotEmpty() ? nameBox.getText().trim() : working.name;
    chooser = std::make_unique<juce::FileChooser> ("Export the theme",
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                       .getChildFile (ThemeStore::safeFileName (name) + ThemeStore::kExtension),
                                                   "*" + juce::String (ThemeStore::kExtension));
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, name] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        // Complete, with no base: the file stands on its own wherever it goes.
        Theme t = working;
        t.name = name;
        t.basedOn = {};
        t.builtIn = false;
        if (onToast) onToast (ThemeStore::save (t, file) ? "Exported to " + file.getFileName() : "The theme could not be written.");
    });
}

void ThemeSheet::revealFolder()
{
    ThemeStore::folder().createDirectory();
    ThemeStore::folder().revealToUser();
}

void ThemeSheet::updateButtons()
{
    saveButton.setEnabled (nameBox.getText().trim().isNotEmpty());
    resetButton.setEnabled (edited);
    deleteButton.setVisible (! selected.builtIn);
}

void ThemeSheet::lookAndFeelChanged()
{
    Dine::styleTextEditor (nameBox, Dine::control);
    keyMute.setTint (Dine::keyMute);
    keySolo.setTint (Dine::keySolo);
    keyRec.setTint (Dine::keyRec);
    keyMon.setTint (Dine::keyMon);
}

// ---------------------------------------------------------------- geometry
juce::Rectangle<int> ThemeSheet::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60), juce::jmin (kCardH, getHeight() - 40));
}

void ThemeSheet::resized()
{
    auto r = cardBounds().reduced (26, 26);

    auto head = r.removeFromTop (Dine::Metric::button);
    closeButton.setBounds (head.removeFromRight (closeButton.idealWidth()).withSizeKeepingCentre (closeButton.idealWidth(), Dine::Metric::button));
    head.removeFromRight (8);
    folderButton.setBounds (head.removeFromRight (folderButton.idealWidth()).withSizeKeepingCentre (folderButton.idealWidth(), Dine::Metric::button));
    r.removeFromTop (16 + 14);

    auto rail = r.removeFromLeft (kRailW);
    auto railFoot = rail.removeFromBottom (Dine::Metric::button);
    importButton.setBounds (railFoot.removeFromLeft (importButton.idealWidth()));
    railFoot.removeFromLeft (8);
    exportButton.setBounds (railFoot.removeFromLeft (exportButton.idealWidth()));
    rail.removeFromBottom (12);
    rail.removeFromTop (22);
    railView.setBounds (rail);
    {
        int y = 0;
        bool mine = false;
        for (auto& row : rows)
        {
            if (! row->theme.builtIn && ! mine) { mine = true; y += 26; }
            row->setBounds (0, y, rail.getWidth() - 4, kRowH);
            y += kRowH;
        }
        railContent.setSize (rail.getWidth() - 4, y);
    }

    r.removeFromLeft (24);
    auto line = r.removeFromTop (Dine::Metric::button);
    if (deleteButton.isVisible())
    {
        deleteButton.setBounds (line.removeFromRight (deleteButton.idealWidth()));
        line.removeFromRight (8);
    }
    resetButton.setBounds (line.removeFromRight (resetButton.idealWidth()));
    line.removeFromRight (8);
    saveButton.setBounds (line.removeFromRight (saveButton.idealWidth()));
    line.removeFromRight (12);
    nameBox.setBounds (line.withSizeKeepingCentre (juce::jmin (line.getWidth(), 360), Dine::Metric::button).withX (line.getX()));
    r.removeFromTop (8 + 16 + 12);

    // The sample strip.
    auto sample = r.removeFromTop (56).reduced (12, 10);
    for (auto* k : { &keyMute, &keySolo, &keyRec, &keyMon }) { k->setBounds (sample.removeFromLeft (26).withSizeKeepingCentre (26, 22)); sample.removeFromLeft (4); }
    sample.removeFromLeft (12);
    sampleFilled.setBounds (sample.removeFromLeft (sampleFilled.idealWidth()).withSizeKeepingCentre (sampleFilled.idealWidth(), Dine::Metric::button));
    sample.removeFromLeft (8);
    sampleStandard.setBounds (sample.removeFromLeft (sampleStandard.idealWidth()).withSizeKeepingCentre (sampleStandard.idealWidth(), Dine::Metric::button));
    sample.removeFromLeft (14);
    samplePan.setBounds (sample.removeFromLeft (90).withSizeKeepingCentre (90, 14));
    r.removeFromTop (14);

    gridView.setBounds (r);
    grid->layout (r.getWidth() - 12);
}

void ThemeSheet::paint (juce::Graphics& g)
{
    // A lighter scrim than the other sheets: the console behind it is the preview.
    g.fillAll (Dine::desk.withAlpha (0.5f));
    const auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), 14.0f);

    auto r = card.reduced (26, 26);
    auto head = r.removeFromTop (Dine::Metric::button);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (19.0f, 600));
    g.drawText ("Appearance", head, juce::Justification::centredLeft);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (12.0f));
    g.drawText ("Pick a theme, or make one of your own. Every window follows; the mix is never touched.",
                r.removeFromTop (16), juce::Justification::centredLeft);
    r.removeFromTop (14);

    auto rail = r.removeFromLeft (kRailW);
    Dine::drawCaption (g, rail.removeFromTop (18), "THEMES");
    // The MINE caption sits inside the scrolling rail, at the first of the user's rows.
    {
        int y = 0;
        for (auto& row : rows)
        {
            if (! row->theme.builtIn)
            {
                auto label = juce::Rectangle<int> (railView.getX(), railView.getY() + y - railView.getViewPositionY(), rail.getWidth(), 20);
                if (label.getY() >= railView.getY() && label.getBottom() <= railView.getBottom())
                    Dine::drawCaption (g, label.withY (label.getY() + 4), "MINE");
                break;
            }
            y += kRowH;
        }
    }

    r.removeFromLeft (24);
    r.removeFromTop (Dine::Metric::button + 8);
    const auto file = selected.file != juce::File() ? "~/Music/DLIVE/Themes/" + selected.file.getFileName() : juce::String();
    juce::String note;
    if (selected.builtIn && ! edited)
        note = "One of DLIVE's own themes. Change a colour and Save keeps the change as a theme of your own, based on this one.";
    else if (selected.builtIn)
        note = "Changed " + dot() + " Save writes it as \"" + nameBox.getText().trim() + "\" in ~/Music/DLIVE/Themes, based on " + selected.name + ".";
    else
        note = "Yours, based on " + baseName() + " " + dot() + " " + file + (edited ? " " + dot() + " changed, not yet saved" : "");
    g.setColour (edited ? Dine::warn : Dine::ink3);
    g.setFont (Dine::text (11.5f));
    g.drawText (note, r.removeFromTop (16), juce::Justification::centredLeft, true);
    r.removeFromTop (12);

    // The sample strip's ground, and the parts drawn by hand: a meter and two chips.
    auto sample = r.removeFromTop (56);
    Dine::fillRounded (g, sample.toFloat(), Dine::console, Dine::Radius::card);
    auto inner = sample.reduced (12, 10);
    inner.removeFromLeft (4 * 30 + 12 + sampleFilled.getWidth() + 8 + sampleStandard.getWidth() + 14 + 90 + 14);
    auto meter = inner.removeFromLeft (110).withSizeKeepingCentre (110, 10).toFloat();
    Dine::fillMeter (g, meter, 0.78f, false, false, 3.0f);
    inner.removeFromLeft (14);
    Dine::drawStatusChip (g, inner.removeFromLeft (62).withSizeKeepingCentre (62, 20).toFloat(), "HEALTHY", Dine::ok);
    inner.removeFromLeft (6);
    Dine::drawStatusChip (g, inner.removeFromLeft (66).withSizeKeepingCentre (66, 20).toFloat(), "CLIPPING", Dine::crit);
    inner.removeFromLeft (12);
    // The group tints, as the bands along the top of a mixer column.
    for (int b = 0; b < int (MixBus::Count) && inner.getWidth() >= 12; ++b)
    {
        auto band = inner.removeFromLeft (12).withSizeKeepingCentre (8, 20).toFloat();
        Dine::fillRounded (g, band, Dine::busTint (MixBus (b)), 2.0f);
    }
}

void ThemeSheet::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && ! cardBounds().contains (e.getPosition()) && onClose) onClose();
}

} // namespace livemix
