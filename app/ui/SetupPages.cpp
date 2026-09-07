#include "SetupPages.h"
#include "UI/LiveMixLookAndFeel.h"
#include "Core/ProductDefinition.h"

namespace livemix
{

namespace
{
    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& step, const juce::String& title, const juce::String& subtitle)
    {
        g.setColour (Tokens::accentText.withAlpha (0.85f));
        g.setFont (LiveMixLookAndFeel::condensed (11.5f, 600, 0.14f));
        g.drawText (step, area.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (Tokens::textHi);
        g.setFont (LiveMixLookAndFeel::condensed (32.0f, 700, 0.01f));
        g.drawText (title, area.removeFromTop (40), juce::Justification::centredLeft);
        g.setColour (Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::body (14.5f));
        g.drawFittedText (subtitle, area.removeFromTop (24), juce::Justification::centredLeft, 2);
    }

    // The roles a volunteer can pick, grouped the way the mix is built.
    struct RoleGroup { const char* name; std::vector<ChannelRole> roles; };
    const std::vector<RoleGroup>& roleGroups()
    {
        static const std::vector<RoleGroup> groups {
            { "Drums", { ChannelRole::KickIn, ChannelRole::KickOut, ChannelRole::SnareTop, ChannelRole::SnareBottom, ChannelRole::HiHat,
                         ChannelRole::RackTom, ChannelRole::FloorTom, ChannelRole::Overhead, ChannelRole::OverheadLeft, ChannelRole::OverheadRight, ChannelRole::Room, ChannelRole::DrumBus } },
            { "Bass",  { ChannelRole::BassDI, ChannelRole::BassAmp, ChannelRole::SynthBass } },
            { "Music", { ChannelRole::Piano, ChannelRole::ElectricPiano, ChannelRole::Organ, ChannelRole::SynthPad, ChannelRole::SynthLead,
                         ChannelRole::AcousticGuitar, ChannelRole::ElectricGuitarClean, ChannelRole::ElectricGuitarDrive } },
            { "Vocals", { ChannelRole::LeadVocal, ChannelRole::BackingVocal, ChannelRole::Choir, ChannelRole::Speech } },
        };
        return groups;
    }

    // Plain words for the menu: "Tracks" is a synth pad, "Pastor" is speech.
    juce::String friendlyRoleName (ChannelRole r)
    {
        switch (r)
        {
            case ChannelRole::SynthPad:  return "Synth Pad / Tracks";
            case ChannelRole::Speech:    return "Pastor / Speech";
            case ChannelRole::Overhead:  return "Overheads (stereo pair)";
            case ChannelRole::DrumBus:   return "Drum mix (stereo, from the console)";
            case ChannelRole::BassDI:    return "Bass (DI)";
            case ChannelRole::BassAmp:   return "Bass (amp mic)";
            default:                     return channelRoleName (r);
        }
    }
}

// ============================================================================ DevicePage

class DevicePage::DeviceRow : public juce::Button
{
public:
    DeviceRow (const juce::String& name, int channels) : juce::Button (name), deviceName (name), inputChannels (channels) {}
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        LiveMixLookAndFeel::drawElevated (g, b, on ? Tokens::accentDim.withAlpha (0.4f) : (over ? Tokens::raised : Tokens::panel),
                                          on ? Tokens::accentStroke : (over ? Tokens::hairHover : Tokens::hair2), Tokens::Radius::card);
        auto r = getLocalBounds().reduced (16, 0);
        g.setColour (on ? Tokens::textHi : Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::body (15.0f, on ? 500 : 400));
        g.drawText (deviceName, r, juce::Justification::centredLeft);
        g.setColour (on ? Tokens::accentText : Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::mono (12.0f));
        g.drawText (inputChannels > 0 ? juce::String (inputChannels) + (inputChannels == 1 ? " input" : " inputs") : "no inputs", r, juce::Justification::centredRight);
    }
    juce::String deviceName;
    int inputChannels;
};

DevicePage::DevicePage (MixController& c, AppServices& s) : controller (c), services (s)
{
    viewport.setViewedComponent (&listHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    addAndMakeVisible (outputButton);
    addAndMakeVisible (continueButton);
    addAndMakeVisible (rescanButton);
    addAndMakeVisible (recordingButton);
    outputButton.onClick = [this] { chooseOutput (outputButton); };
    rescanButton.onClick = [this] { refresh(); };
    recordingButton.setTooltip ("Play a folder of recorded stems (AIFF / WAV / FLAC) as the inputs, so you can go through the whole app without a band.");
    recordingButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder of multitrack stems", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (folder.isDirectory()) openRecording (folder);
        });
    };
    continueButton.onClick = [this]
    {
        if (selected < 0 || selected >= inputs.size()) return;
        error = services.openDevices (inputs[selected].name, outputName);
        if (error.isNotEmpty()) { repaint(); return; }
        if (onContinue) onContinue();
    };
    refresh();
}

DevicePage::~DevicePage() = default;

void DevicePage::refresh()
{
    inputs = services.inputDevices();
    outputs = services.outputDevices();
    rows.clear();
    listHolder.removeAllChildren();
    const juce::String current = services.currentInputDevice();
    selected = -1;
    for (int i = 0; i < inputs.size(); ++i)
    {
        auto row = std::make_unique<DeviceRow> (inputs[i].name, inputs[i].inputChannels);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { select (i); };
        listHolder.addAndMakeVisible (*row);
        if (inputs[i].name == current) selected = i;
        rows.push_back (std::move (row));
    }
    if (selected < 0 && ! inputs.isEmpty())
    {
        // Prefer the device with the most inputs: that is the console or Dante, not the built-in mic.
        int best = 0;
        for (int i = 1; i < inputs.size(); ++i) if (inputs[i].inputChannels > inputs[best].inputChannels) best = i;
        selected = best;
    }
    outputName = services.currentOutputDevice();
    if (outputName.isEmpty() && selected >= 0)
    {
        // Same device when it has outputs, else the first output device.
        for (const auto& o : outputs) if (o.name == inputs[selected].name) outputName = o.name;
        if (outputName.isEmpty() && ! outputs.isEmpty()) outputName = outputs[0].name;
    }
    outputButton.setValue (outputName.isEmpty() ? "none" : outputName);
    for (int i = 0; i < int (rows.size()); ++i) rows[size_t (i)]->setToggleState (i == selected, juce::dontSendNotification);
    continueButton.setEnabled (selected >= 0);
    resized();
    repaint();
}

void DevicePage::openRecording (const juce::File& folder)
{
    error = services.openRecording (folder, outputName);
    if (error.isNotEmpty()) { repaint(); return; }
    controller.setSession (services.recordingSuggestion (controller.getSession()));
    repaint();
    if (onContinueToAssign) onContinueToAssign();
}

void DevicePage::select (int index)
{
    selected = index;
    for (int i = 0; i < int (rows.size()); ++i) rows[size_t (i)]->setToggleState (i == selected, juce::dontSendNotification);
    continueButton.setEnabled (selected >= 0);
    repaint();
}

void DevicePage::chooseOutput (juce::Component& anchor)
{
    juce::PopupMenu m;
    for (int i = 0; i < outputs.size(); ++i) m.addItem (i + 1, outputs[i].name, true, outputs[i].name == outputName);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this] (int id)
    {
        if (id <= 0 || id > outputs.size()) return;
        outputName = outputs[id - 1].name;
        outputButton.setValue (outputName);
        resized();
    });
}

void DevicePage::paint (juce::Graphics& g)
{
    auto area = AppStyle::contentArea (getLocalBounds());
    drawTitle (g, area.removeFromTop (92), "STEP 1 OF 3", "Audio input", "Choose the device your inputs arrive on - Dante, a USB console, or an interface.");
    if (services.isPlayingRecording())
    {
        auto line = area.removeFromTop (30);
        g.setColour (Tokens::accentText);
        g.setFont (LiveMixLookAndFeel::condensed (14.0f, 600, 0.04f));
        g.drawText ("PLAYING " + services.currentInputDevice().toUpperCase() + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + juce::String (services.numInputChannels()) + " INPUTS", line, juce::Justification::centredLeft);
    }
    else if (selected >= 0 && selected < inputs.size())
    {
        auto line = area.removeFromTop (30);
        g.setColour (Tokens::okText);
        g.setFont (LiveMixLookAndFeel::condensed (14.0f, 600, 0.04f));
        g.drawText (juce::String (inputs[selected].inputChannels) + " inputs detected", line, juce::Justification::centredLeft);
    }
    if (error.isNotEmpty())
    {
        g.setColour (Tokens::critText);
        g.setFont (LiveMixLookAndFeel::body (13.0f));
        g.drawFittedText (error, getLocalBounds().reduced (AppStyle::kMargin).removeFromBottom (60).withTrimmedBottom (40), juce::Justification::centredLeft, 1);
    }
}

void DevicePage::resized()
{
    auto area = AppStyle::contentArea (getLocalBounds());
    area.removeFromTop (92 + 30);
    auto bottom = area.removeFromBottom (44);
    continueButton.setBounds (bottom.removeFromRight (150));
    bottom.removeFromRight (12);
    outputButton.setBounds (bottom.removeFromRight (juce::jmax (220, outputButton.getIdealWidth())));
    rescanButton.setBounds (bottom.removeFromLeft (100));
    bottom.removeFromLeft (8);
    recordingButton.setBounds (bottom.removeFromLeft (juce::jmax (190, recordingButton.getIdealWidth())));
    area.removeFromBottom (16);
    viewport.setBounds (area);
    const int rowH = 46, gap = 8;
    listHolder.setSize (area.getWidth() - (rows.size() * (rowH + gap) > size_t (area.getHeight()) ? 12 : 0), int (rows.size()) * (rowH + gap));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), rowH); y += rowH + gap; }
}

// ============================================================================ AssignPage

class AssignPage::Row : public juce::Component
{
public:
    Row (AssignPage& owner, int index) : page (owner), input (index)
    {
        addAndMakeVisible (name);
        name.setFont (LiveMixLookAndFeel::body (14.0f, 500));
        name.setIndents (10, 0);
        name.setColour (juce::TextEditor::backgroundColourId, Tokens::inset);
        name.setColour (juce::TextEditor::textColourId, Tokens::textHi);
        name.setColour (juce::TextEditor::outlineColourId, Tokens::hair2);
        name.setColour (juce::TextEditor::focusedOutlineColourId, Tokens::accentStroke);
        name.setColour (juce::TextEditor::highlightedTextColourId, Tokens::textHi);
        name.setColour (juce::CaretComponent::caretColourId, Tokens::accentText);
        name.setTextToShowWhenEmpty ("Channel name", Tokens::textDim);
        name.setSelectAllWhenFocused (true);
        name.onTextChange = [this] { page.entries[size_t (input)].name = name.getText(); };
        name.onReturnKey = [this] { name.giveAwayKeyboardFocus(); page.commit(); };
        name.onFocusLost = [this] { page.commit(); };

        addAndMakeVisible (source);
        source.setCaption ({}); // single-line pop-up look
        source.onClick = [this] { page.showSourceMenu (input, source); };

        addAndMakeVisible (link);
        link.setFontPx (11.0f);
        link.setSpacing (0.06f);
        link.onClick = [this]
        {
            auto& e = page.entries[size_t (input)];
            if (input + 1 >= int (page.entries.size())) return;
            e.linkedToNext = ! e.linkedToNext;
            page.entries[size_t (input) + 1].linkedFromPrevious = e.linkedToNext;
            page.commit();
            page.rebuild();
        };
    }

    void refresh()
    {
        const auto& e = page.entries[size_t (input)];
        name.setText (e.name, false);
        if (e.assigned)
            source.setValue (friendlyRoleName (e.role));
        else
            source.setValue ("Choose source");
        source.setEnabled (! e.linkedFromPrevious);
        name.setEnabled (! e.linkedFromPrevious);
        link.setButtonText (e.linkedToNext ? "STEREO" : "LINK");
        link.setStyle (e.linkedToNext ? FlatButton::Style::Accent : FlatButton::Style::Outline);
        link.setVisible (! e.linkedFromPrevious && input + 1 < int (page.entries.size()));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& e = page.entries[size_t (input)];
        auto b = getLocalBounds().toFloat().reduced (0.5f);

        if (e.linkedFromPrevious)
        {
            LiveMixLookAndFeel::fillSurface (g, b, Tokens::inset.withAlpha (0.55f), Tokens::Radius::control);
            LiveMixLookAndFeel::strokeSurface (g, b, Tokens::hair, Tokens::Radius::control);
            auto r = getLocalBounds().reduced (14, 0).withTrimmedLeft (86);
            g.setColour (Tokens::textDim);
            g.setFont (LiveMixLookAndFeel::body (12.5f, 400));
            g.drawText ("Right of input " + juce::String (input).paddedLeft ('0', 2) + "  ·  stereo pair",
                        r, juce::Justification::centredLeft);
            return;
        }

        LiveMixLookAndFeel::drawElevated (g, b,
                                          e.assigned ? Tokens::raised : Tokens::panel,
                                          e.assigned ? Tokens::hair2 : Tokens::hair,
                                          Tokens::Radius::control);

        // Device input chip — the physical channel number an engineer looks for first.
        auto bounds = getLocalBounds();
        auto chipArea = bounds.removeFromLeft (86).reduced (12, 0);
        juce::String number = "IN " + juce::String (input + 1);
        if (e.linkedToNext) number = "IN " + juce::String (input + 1) + "/" + juce::String (input + 2);
        const float chipW = LiveMixLookAndFeel::chipWidth (number, 10.0f);
        auto chip = chipArea.withSizeKeepingCentre (int (chipW), 22).toFloat();
        LiveMixLookAndFeel::drawChip (g, chip, number,
                                      e.assigned ? Tokens::accentText : Tokens::textMid,
                                      e.assigned ? Tokens::accent.withAlpha (0.45f) : Tokens::hair2,
                                      e.assigned ? Tokens::accentDim.withAlpha (0.35f) : Tokens::inset, 10.0f);

        if (e.assigned)
        {
            auto right = getLocalBounds().reduced (10, 0);
            right.removeFromRight (72 + 8); // LINK button + gap
            auto busArea = right.removeFromRight (50);
            g.setColour (Tokens::textLow);
            g.setFont (LiveMixLookAndFeel::condensed (11.0f, 600, 0.1f));
            g.drawText (mixBusName (mixBusForRole (e.role)), busArea, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromLeft (76); // IN chip
        const bool linked = page.entries[size_t (input)].linkedFromPrevious;
        link.setBounds (r.removeFromRight (72).withSizeKeepingCentre (72, 28));
        r.removeFromRight (8);
        r.removeFromRight (50); // bus label gutter
        source.setBounds (r.removeFromRight (200).withHeight (32).withY (r.getCentreY() - 16));
        r.removeFromRight (12);
        name.setBounds (r.withHeight (32).withY (r.getCentreY() - 16));
        name.setVisible (! linked);
        source.setVisible (! linked);
    }

private:
    AssignPage& page;
    int input;
    juce::TextEditor name;
    DropdownButton source { "SOURCE" };
    FlatButton link { "LINK", FlatButton::Style::Outline };
};

AssignPage::AssignPage (MixController& c, AppServices& s) : controller (c), services (s)
{
    viewport.setViewedComponent (&listHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    addAndMakeVisible (continueButton);
    addAndMakeVisible (backButton);
    addAndMakeVisible (clearButton);
    continueButton.onClick = [this] { commit(); if (assignedCount() > 0 && onContinue) onContinue(); };
    backButton.onClick = [this] { if (onBack) onBack(); };
    clearButton.onClick = [this] { clearAll(); };
}

AssignPage::~AssignPage() = default;

void AssignPage::refresh()
{
    numInputs = juce::jlimit (0, kMaxInputs, services.numInputChannels());
    entries.assign (size_t (numInputs), Entry {});
    for (const auto& in : controller.getSession().inputs)
    {
        if (in.inputA < 0 || in.inputA >= numInputs) continue;
        auto& e = entries[size_t (in.inputA)];
        e.name = in.name;
        e.assigned = in.enabled;
        e.role = in.role;
        if (in.inputB >= 0 && in.inputB < numInputs) { e.linkedToNext = in.inputB == in.inputA + 1; entries[size_t (in.inputB)].linkedFromPrevious = e.linkedToNext; }
    }
    rebuild();
}

void AssignPage::rebuild()
{
    rows.clear();
    listHolder.removeAllChildren();
    for (int i = 0; i < numInputs; ++i)
    {
        auto row = std::make_unique<Row> (*this, i);
        listHolder.addAndMakeVisible (*row);
        row->refresh();
        rows.push_back (std::move (row));
    }
    continueButton.setEnabled (assignedCount() > 0);
    resized();
    repaint();
}

void AssignPage::commit()
{
    MixSession s = controller.getSession();
    s.inputs.clear();
    for (int i = 0; i < numInputs; ++i)
    {
        const auto& e = entries[size_t (i)];
        if (! e.assigned || e.linkedFromPrevious) continue;
        InputAssignment a;
        a.name = e.name.isEmpty() ? juce::String (channelRoleName (e.role)).toStdString() : e.name.toStdString();
        a.role = e.role;
        a.inputA = i;
        a.inputB = e.linkedToNext && i + 1 < numInputs ? i + 1 : -1;
        a.enabled = true;
        s.inputs.push_back (a);
    }
    controller.setSession (s);
    continueButton.setEnabled (assignedCount() > 0);
    repaint();
}

int AssignPage::assignedCount() const
{
    int n = 0;
    for (const auto& e : entries) if (e.assigned && ! e.linkedFromPrevious) ++n;
    return n;
}

void AssignPage::assign (int input, ChannelRole role, const juce::String& name, bool linkWithNext)
{
    if (input < 0 || input >= numInputs) return;
    auto& e = entries[size_t (input)];
    e.assigned = true;
    e.role = role;
    e.name = name;
    e.linkedToNext = linkWithNext && input + 1 < numInputs;
    if (e.linkedToNext) entries[size_t (input) + 1].linkedFromPrevious = true;
    commit();
    rebuild();
}

void AssignPage::clearAll()
{
    for (auto& e : entries) e = Entry {};
    commit();
    rebuild();
}

void AssignPage::showSourceMenu (int input, juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addItem (1, "Not used", true, ! entries[size_t (input)].assigned);
    m.addSeparator();

    int id = 100;
    std::vector<ChannelRole> byId;
    // Hierarchical menus read as native macOS — not one endless flat list.
    for (const auto& group : roleGroups())
    {
        juce::PopupMenu sub;
        for (auto r : group.roles)
        {
            const bool on = entries[size_t (input)].assigned && entries[size_t (input)].role == r;
            sub.addItem (id++, friendlyRoleName (r), true, on);
            byId.push_back (r);
        }
        m.addSubMenu (group.name, sub, true);
    }

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetComponent (&anchor)
                         .withMinimumWidth (220)
                         .withStandardItemHeight (32),
                     [this, input, byId] (int chosen)
                     {
                         if (chosen <= 0 || input >= int (entries.size())) return;
                         auto& e = entries[size_t (input)];
                         if (chosen == 1) { e.assigned = false; }
                         else
                         {
                             e.assigned = true;
                             e.role = byId[size_t (chosen - 100)];
                             if (e.name.isEmpty()) e.name = channelRoleName (e.role);
                             if ((e.role == ChannelRole::Overhead || e.role == ChannelRole::DrumBus || e.role == ChannelRole::Piano
                                  || e.role == ChannelRole::ElectricPiano || e.role == ChannelRole::SynthPad)
                                 && input + 1 < numInputs && ! entries[size_t (input) + 1].assigned && ! e.linkedToNext)
                             {
                                 e.linkedToNext = true;
                                 entries[size_t (input) + 1].linkedFromPrevious = true;
                             }
                         }
                         commit();
                         rebuild();
                     });
}

void AssignPage::paint (juce::Graphics& g)
{
    auto area = AppStyle::contentArea (getLocalBounds());
    drawTitle (g, area.removeFromTop (92), "STEP 2 OF 3", "Input assignment",
               juce::String (numInputs) + " inputs on " + services.currentInputDevice()
                   + ". Name each one, pick what it is, link stereo pairs.");
    auto line = area.removeFromTop (28);
    g.setColour (assignedCount() > 0 ? Tokens::okText : Tokens::textLow);
    g.setFont (LiveMixLookAndFeel::condensed (13.0f, 600, 0.08f));
    g.drawText (juce::String (assignedCount()) + (assignedCount() == 1 ? " input assigned" : " inputs assigned"),
                line, juce::Justification::centredLeft);

    auto header = area.removeFromTop (22);
    header.removeFromLeft (10);
    g.setColour (Tokens::textDim);
    g.setFont (LiveMixLookAndFeel::condensed (10.5f, 600, 0.12f));
    g.drawText ("INPUT", header.removeFromLeft (76), juce::Justification::centredLeft);
    auto right = header;
    right.removeFromRight (10);
    g.drawText ("LINK", right.removeFromRight (72), juce::Justification::centred);
    right.removeFromRight (8);
    g.drawText ("BUS", right.removeFromRight (50), juce::Justification::centredLeft);
    g.drawText ("SOURCE", right.removeFromRight (200), juce::Justification::centredLeft);
    right.removeFromRight (12);
    g.drawText ("NAME", right, juce::Justification::centredLeft);
}

void AssignPage::resized()
{
    auto area = AppStyle::contentArea (getLocalBounds());
    area.removeFromTop (92 + 28 + 22);
    auto bottom = area.removeFromBottom (44);
    continueButton.setBounds (bottom.removeFromRight (150));
    backButton.setBounds (bottom.removeFromLeft (90));
    bottom.removeFromLeft (8);
    clearButton.setBounds (bottom.removeFromLeft (110));
    area.removeFromBottom (16);
    viewport.setBounds (area);
    const int rowH = 48, gap = 6;
    const int total = int (rows.size()) * (rowH + gap);
    listHolder.setSize (area.getWidth() - (total > area.getHeight() ? 12 : 0), juce::jmax (total, area.getHeight()));
    int y = 0;
    for (auto& r : rows) { r->setBounds (0, y, listHolder.getWidth(), rowH); y += rowH + gap; }
}

// ============================================================================ PurposePage

class PurposePage::Tile : public juce::Button
{
public:
    Tile (const juce::String& title, const juce::String& line) : juce::Button (title), heading (title), detail (line) {}
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        LiveMixLookAndFeel::drawSurface (g, b, on ? Tokens::accentDim.withAlpha (0.35f) : (over ? Tokens::raised : Tokens::panel), on ? Tokens::accentStroke : (over ? Tokens::hairHover : Tokens::hair), Tokens::Radius::card);
        auto r = getLocalBounds().reduced (18, 14);
        g.setColour (on ? Tokens::textHi : Tokens::textMid);
        g.setFont (LiveMixLookAndFeel::condensed (17.0f, 600, 0.02f));
        g.drawText (heading, r.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (on ? Tokens::textMid : Tokens::textLow);
        g.setFont (LiveMixLookAndFeel::body (12.5f));
        g.drawFittedText (detail, r, juce::Justification::topLeft, 3);
        if (on) LiveMixLookAndFeel::drawIcon (g, LiveMixLookAndFeel::Icon::Check, getLocalBounds().removeFromRight (36).removeFromTop (36).toFloat().reduced (10.0f), Tokens::accentText);
    }
    juce::String heading, detail;
};

PurposePage::PurposePage (MixController& c) : controller (c)
{
    const char* purposeLines[] = {
        "Loudness for TV and platform broadcast, true peaks held under -2 dB.",
        "YouTube, Facebook and church streaming platforms; a little louder, peaks under -1 dB.",
        "A clean mix to keep; loudness left natural for editing later.",
        "Rehearsal or a listening feed; the mix, not the delivery, is the point."
    };
    for (int i = 0; i < int (MixPurpose::Count); ++i)
    {
        auto t = std::make_unique<Tile> (mixPurposeName (MixPurpose (i)), purposeLines[i]);
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setPurpose (MixPurpose (i)); refresh(); };
        addAndMakeVisible (*t);
        purposeTiles.push_back (std::move (t));
    }
    const char* soundLines[] = {
        "Deep controlled low end, forward defined attack, dense but breathing dynamics, smooth top, room for keys, bass and a choir.",
        "A documented variation of Modern Gospel: guitars and pads closer to the voices, drums a little tighter, more hall on the backing vocals."
    };
    for (int i = 0; i < int (StyleProfileId::Count); ++i)
    {
        auto t = std::make_unique<Tile> (styleProfileName (StyleProfileId (i)), soundLines[i]);
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { controller.setProfile (StyleProfileId (i)); refresh(); };
        addAndMakeVisible (*t);
        soundTiles.push_back (std::move (t));
    }
    addAndMakeVisible (continueButton);
    addAndMakeVisible (backButton);
    continueButton.onClick = [this] { if (onContinue) onContinue(); };
    backButton.onClick = [this] { if (onBack) onBack(); };
    refresh();
}

PurposePage::~PurposePage() = default;

void PurposePage::refresh()
{
    for (int i = 0; i < int (purposeTiles.size()); ++i) purposeTiles[size_t (i)]->setToggleState (int (controller.getSession().purpose) == i, juce::dontSendNotification);
    for (int i = 0; i < int (soundTiles.size()); ++i) soundTiles[size_t (i)]->setToggleState (int (controller.getSession().profile) == i, juce::dontSendNotification);
    repaint();
}

void PurposePage::paint (juce::Graphics& g)
{
    auto area = AppStyle::contentArea (getLocalBounds());
    drawTitle (g, area.removeFromTop (92), "STEP 3 OF 3", "What are we mixing?", "This sets the loudness and ceiling of the output. Then choose the sound.");
    area.removeFromTop (8 + 120 + 28);
    g.setColour (Tokens::textLow);
    g.setFont (LiveMixLookAndFeel::condensed (12.0f, 600, 0.08f));
    g.drawText ("SOUND", area.removeFromTop (18), juce::Justification::centredLeft);
}

void PurposePage::resized()
{
    auto area = AppStyle::contentArea (getLocalBounds());
    area.removeFromTop (92 + 8);
    auto row = area.removeFromTop (120);
    const int gap = 12;
    const int w = (row.getWidth() - gap * (int (purposeTiles.size()) - 1)) / int (purposeTiles.size());
    for (auto& t : purposeTiles) { t->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
    area.removeFromTop (28 + 18 + 8);
    auto row2 = area.removeFromTop (110);
    const int w2 = (row2.getWidth() - gap) / 2;
    for (auto& t : soundTiles) { t->setBounds (row2.removeFromLeft (w2)); row2.removeFromLeft (gap); }
    auto bottom = getLocalBounds().reduced (AppStyle::kMargin).removeFromBottom (44);
    if (bottom.getWidth() > AppStyle::kMaxContentWidth) bottom = bottom.withSizeKeepingCentre (AppStyle::kMaxContentWidth, bottom.getHeight());
    continueButton.setBounds (bottom.removeFromRight (150));
    backButton.setBounds (bottom.removeFromLeft (90));
}

} // namespace livemix
