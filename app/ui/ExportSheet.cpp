#include "ExportSheet.h"
#include "UI/Widgets.h"

namespace livemix
{

namespace
{
    constexpr int kCardW = 620, kPadX = 30, kPadY = 26;
    constexpr int kRowGap = 20, kCapH = 16, kCapGap = 6;

    juce::String clock (double seconds)
    {
        if (seconds < 0.0) seconds = 0.0;
        const int total = int (seconds);
        return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

ExportSheet::ExportSheet (MixController& c, AppServices& s) : controller (c), services (s)
{
    const char* whats[3] = { "Stereo mix", "Group stems", "Raw multitrack" };
    for (int i = 0; i < 3; ++i)
    {
        whatTabs[size_t (i)] = std::make_unique<DineButton> (whats[i], DineButton::Style::Segment);
        whatTabs[size_t (i)]->setFontPx (12.0f);
        whatTabs[size_t (i)]->onClick = [this, i] { what = i; updateControls(); repaint(); };
        whatTrack.addAndMakeVisible (*whatTabs[size_t (i)]);
    }
    whatTabs[0]->setTooltip ("One file: the kept mix, exactly as the room and the stream hear it.");
    whatTabs[1]->setTooltip ("A folder: one stereo file per group, each at its fader, so the set of them is the mix "
                             "in the parts you would re-balance it from.");
    whatTabs[2]->setTooltip ("A folder: one file per input, straight off the disk. No trim, no chain, no fader - "
                             "what the microphones heard, for somebody who wants to mix it themselves.");
    addAndMakeVisible (whatTrack);

    const juce::String louds[3] = { "As mixed", "Stream " + juce::String (Glyph::minus()) + "14",
                                    "Podcast " + juce::String (Glyph::minus()) + "16" };
    for (int i = 0; i < 3; ++i)
    {
        loudnessTabs[size_t (i)] = std::make_unique<DineButton> (louds[i], DineButton::Style::Segment);
        loudnessTabs[size_t (i)]->setFontPx (12.0f);
        loudnessTabs[size_t (i)]->onClick = [this, i] { loudness = i; updateControls(); repaint(); };
        loudnessTrack.addAndMakeVisible (*loudnessTabs[size_t (i)]);
    }
    loudnessTabs[0]->setTooltip ("Where the mix already sits. It was built to the target Purpose and sound was given, "
                                 "and it carries it.");
    loudnessTabs[1]->setTooltip ("Measured, then one gain over the whole render so it lands at -14 LUFS. Nothing is "
                                 "compressed or limited on the way out.");
    loudnessTabs[2]->setTooltip ("The same, at -16 LUFS.");
    addAndMakeVisible (loudnessTrack);

    const char* ranges[3] = { "Whole session", "Loop", "Between markers" };
    for (int i = 0; i < 3; ++i)
    {
        rangeTabs[size_t (i)] = std::make_unique<DineButton> (ranges[i], DineButton::Style::Segment);
        rangeTabs[size_t (i)]->setFontPx (12.0f);
        rangeTabs[size_t (i)]->onClick = [this, i] { range = Range (i); updateControls(); repaint(); };
        rangeTrack.addAndMakeVisible (*rangeTabs[size_t (i)]);
    }
    const char* formats[3] = { "WAV", "AIFF", "MP3 320" };
    for (int i = 0; i < 3; ++i)
    {
        formatTabs[size_t (i)] = std::make_unique<DineButton> (formats[i], DineButton::Style::Segment);
        formatTabs[size_t (i)]->setFontPx (12.0f);
        formatTabs[size_t (i)]->onClick = [this, i] { format = i; updateControls(); repaint(); };
        formatTrack.addAndMakeVisible (*formatTabs[size_t (i)]);
    }
    addAndMakeVisible (rangeTrack);
    addAndMakeVisible (formatTrack);

    folder = services.sessionFolder() != juce::File() ? services.sessionFolder().getChildFile ("Exports")
                                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    folderButton.setFontPx (12.0f);
    folderButton.setTooltip ("Where the file goes. The session's own Exports folder is made for you.");
    folderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Where should the export go?", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto picked = fc.getResult();
                                  if (picked == juce::File()) return;
                                  folder = picked;
                                  repaint();
                              });
    };
    addAndMakeVisible (folderButton);

    cancelButton.setFontPx (12.5f);
    cancelButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (cancelButton);

    exportButton.setFontPx (12.5f);
    exportButton.onClick = [this]
    {
        if (onExport)
        {
            Request req;
            req.dest = destination();
            req.format = format == 2 ? AppServices::ExportFormat::Mp3
                       : format == 1 ? AppServices::ExportFormat::Aiff
                                     : AppServices::ExportFormat::Wav;
            req.what = what == 1 ? AppServices::ExportWhat::GroupStems
                     : what == 2 ? AppServices::ExportWhat::RawMultitrack
                                 : AppServices::ExportWhat::StereoMix;
            req.loudness = loudness == 1 ? AppServices::ExportLoudness::Stream14
                         : loudness == 2 ? AppServices::ExportLoudness::Podcast16
                                         : AppServices::ExportLoudness::AsMixed;
            req.from = fromSample();
            req.to = toSample();
            onExport (req);
        }
        if (onClose) onClose();
    };
    addAndMakeVisible (exportButton);

    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
    updateControls();
}

ExportSheet::~ExportSheet() = default;

void ExportSheet::refresh() { updateControls(); }

bool ExportSheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (key == juce::KeyPress::returnKey && exportButton.isEnabled()) { exportButton.onClick(); return true; }
    return false;
}

juce::Rectangle<int> ExportSheet::cardBounds() const
{
    const int w = juce::jmin (kCardW, getWidth() - 60);
    // What / Range / Format / Loudness, then Save to, then the estimate and the two buttons.
    const int h = kPadY + 28 + 2 + 18 + 22
                + (kCapH + kCapGap + Dine::Metric::control + kRowGap) * 4
                + kCapH + kCapGap + 20 + kRowGap
                + 18 + 14 + Dine::Metric::button + kPadY;
    return juce::Rectangle<int> (w, juce::jmin (h, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}

juce::int64 ExportSheet::fromSample() const
{
    const auto& project = services.daw().getProject();
    if (range == Range::Loop) return project.loopStart;
    if (range == Range::BetweenMarkers && project.markers.size() >= 2)
    {
        auto sorted = project.markers;
        std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        return sorted.front().position;
    }
    return 0;
}

juce::int64 ExportSheet::toSample() const
{
    const auto& project = services.daw().getProject();
    if (range == Range::Loop) return project.loopEnd;
    if (range == Range::BetweenMarkers && project.markers.size() >= 2)
    {
        auto sorted = project.markers;
        std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        return sorted.back().position;
    }
    return 0;      // to the end of the recording
}

juce::String ExportSheet::rangeNote() const
{
    const auto& project = services.daw().getProject();
    if (range == Range::Loop) return project.loopEnd > project.loopStart ? juce::String ("The marked loop")
                                                                         : juce::String ("No loop is set yet");
    if (range == Range::BetweenMarkers)
    {
        if (project.markers.size() < 2) return "Fewer than two markers";
        auto sorted = project.markers;
        std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        return juce::String (sorted.front().name) + " " + Glyph::arrow() + " " + juce::String (sorted.back().name);
    }
    return "Everything that was recorded";
}

// What the export will actually leave on the disk, said before it is pressed: a file, or a
// folder with a count of what goes in it.
juce::String ExportSheet::whatNote() const
{
    if (what == 0) return {};
    if (what == 2)
        return juce::String (int (controller.getSession().inputs.size())) + " files, one per input";
    int groups = 0;
    if (controller.isPrepared())
        for (int b = 0; b < int (MixBus::Master); ++b)
            if (controller.getEngine().isBusUsed (MixBus (b))) ++groups;
    return juce::String (groups) + (groups == 1 ? " file, one per group" : " files, one per group");
}

juce::File ExportSheet::destination() const
{
    juce::String name = services.currentSessionName().isNotEmpty() ? services.currentSessionName() : "DLIVE mix";
    if (range == Range::BetweenMarkers)
    {
        const auto& project = services.daw().getProject();
        if (project.markers.size() >= 2)
        {
            auto sorted = project.markers;
            std::sort (sorted.begin(), sorted.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
            name = juce::String (sorted.front().name);
        }
    }
    // Stems and a multitrack are a folder named after this, made beside it; MixBounce does it.
    const char* ext = format == 2 ? "mp3" : format == 1 ? "aiff" : "wav";
    return folder.getChildFile (juce::File::createLegalFileName (name)).withFileExtension (ext);
}

void ExportSheet::updateControls()
{
    for (int i = 0; i < 3; ++i) whatTabs[size_t (i)]->setToggleState (what == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i) rangeTabs[size_t (i)]->setToggleState (int (range) == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i) formatTabs[size_t (i)]->setToggleState (format == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i) loudnessTabs[size_t (i)]->setToggleState (loudness == i, juce::dontSendNotification);

    // A set of parts is written as audio, and its levels are left where the mix put them: MP3
    // is a delivery format for a finished mix, and moving stems apart from each other would
    // stop them being parts of the same thing.
    formatTabs[2]->setEnabled (! writesFolder());
    if (writesFolder() && format == 2) format = 0;
    for (int i = 1; i < 3; ++i) loudnessTabs[size_t (i)]->setEnabled (! writesFolder());
    if (writesFolder() && loudness != 0) loudness = 0;
    for (int i = 0; i < 3; ++i) formatTabs[size_t (i)]->setToggleState (format == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i) loudnessTabs[size_t (i)]->setToggleState (loudness == i, juce::dontSendNotification);

    const auto& project = services.daw().getProject();
    rangeTabs[1]->setEnabled (project.loopEnd > project.loopStart);
    rangeTabs[2]->setEnabled (project.markers.size() >= 2);
    if ((range == Range::Loop && ! rangeTabs[1]->isEnabled())
        || (range == Range::BetweenMarkers && ! rangeTabs[2]->isEnabled()))
        range = Range::Whole;

    const auto length = toSample() > fromSample() ? toSample() - fromSample()
                                                  : juce::jmax ((juce::int64) 0, project.lengthSamples() - fromSample());
    exportButton.setEnabled (length > 0);
    resized();
}

void ExportSheet::paint (juce::Graphics& g)
{
    g.fillAll (Dine::desk.withAlpha (0.86f));
    auto card = cardBounds();
    Dine::drawSheet (g, card.toFloat(), Dine::Radius::card);

    auto r = card.reduced (kPadX, kPadY);
    g.setColour (Dine::ink);
    g.setFont (Dine::text (22.0f, 600));
    Dine::drawText (g, "Export", r.removeFromTop (28), juce::Justification::centredLeft, true);
    r.removeFromTop (2);
    g.setColour (Dine::ink3);
    g.setFont (Dine::text (13.0f));
    Dine::drawText (g, "Bounced offline from the kept mix. What the room and the stream hear is not touched.",
                    r.removeFromTop (18), juce::Justification::centredLeft, true);
    r.removeFromTop (22);

    auto caption = [&g, &r] (const juce::String& text)
    {
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (11.0f, 500));
        Dine::drawText (g, text, r.removeFromTop (kCapH), juce::Justification::centredLeft, true);
        r.removeFromTop (kCapGap);
    };
    // The note beside a segment row: what the answer above means, in a sentence.
    auto note = [&g, &r] (const juce::Component& track, const juce::String& text)
    {
        auto row = r.removeFromTop (Dine::Metric::control);
        row.removeFromLeft (track.getWidth() + 16);
        g.setColour (Dine::ink2);
        g.setFont (Dine::text (12.0f, 500));
        Dine::drawText (g, text, row, juce::Justification::centredLeft, true);
        r.removeFromTop (kRowGap);
    };

    caption ("What");
    note (whatTrack, whatNote());

    caption ("Range");
    note (rangeTrack, rangeNote());

    caption ("Format");
    r.removeFromTop (Dine::Metric::control + kRowGap);

    caption ("Loudness");
    note (loudnessTrack, writesFolder() ? "Parts keep the levels the mix gave them" : juce::String());

    caption ("Save to");
    {
        auto row = r.removeFromTop (20);
        row.removeFromRight (folderButton.getWidth() + 14);
        g.setColour (Dine::ink2);
        g.setFont (Dine::mono (11.0f, 500));
        Dine::drawText (g, destination().getFullPathName(), row, juce::Justification::centredLeft, true);
        r.removeFromTop (kRowGap);
    }

    // How long the render will take, because "about twenty seconds" is the difference
    // between waiting and thinking it has hung.
    {
        const auto& project = services.daw().getProject();
        const double rate = juce::jmax (1.0, project.sampleRate);
        const auto length = toSample() > fromSample() ? toSample() - fromSample()
                                                      : juce::jmax ((juce::int64) 0, project.lengthSamples() - fromSample());
        const double seconds = double (length) / rate;
        // Stems and a multitrack write N files from the same walk, so they take about N times
        // as long to put on the disk; the loudness choice adds a second pass over the render.
        int files = 1;
        if (what == 2) files = juce::jmax (1, int (controller.getSession().inputs.size()));
        else if (what == 1)
        {
            files = 0;
            if (controller.isPrepared())
                for (int b = 0; b < int (MixBus::Master); ++b)
                    if (controller.getEngine().isBusUsed (MixBus (b))) ++files;
            files = juce::jmax (1, files);
        }
        const double per = seconds / 12.0;
        const int estimate = juce::jmax (1, int (std::ceil (per * (0.35 + 0.65 * files) * (loudness != 0 ? 1.4 : 1.0))));
        g.setColour (Dine::ink3);
        g.setFont (Dine::text (12.0f));
        Dine::drawText (g, length <= 0 ? juce::String ("There is nothing recorded in that range.")
                                       : clock (seconds) + " of audio  " + Glyph::dot() + "  about "
                                             + juce::String (estimate) + (estimate == 1 ? " second" : " seconds"),
                        r.removeFromTop (18), juce::Justification::centredLeft, true);
        // A folder is a different thing from a file, and it is worth saying before it is made.
        if (writesFolder() && length > 0)
        {
            g.setColour (Dine::ink4);
            g.setFont (Dine::text (11.5f));
            Dine::drawText (g, "A folder is made beside the name above, with the files inside it.",
                            r.removeFromTop (14), juce::Justification::centredLeft, true);
        }
    }
}

void ExportSheet::resized()
{
    auto card = cardBounds();
    auto r = card.reduced (kPadX, kPadY);
    r.removeFromTop (28 + 2 + 18 + 22);

    // One segment row: its track sized to the segments, the rest of the line left for a note.
    auto segments = [&r] (DineSegmentRow& track, std::array<std::unique_ptr<DineButton>, 3>& tabs, int least)
    {
        r.removeFromTop (kCapH + kCapGap);
        auto row = r.removeFromTop (Dine::Metric::control);
        int total = 0, widths[3] {};
        for (int i = 0; i < 3; ++i) { widths[i] = juce::jmax (least, tabs[size_t (i)]->idealWidth()); total += widths[i]; }
        track.setBounds (row.removeFromLeft (juce::jmin (row.getWidth(), total + 4)).expanded (0, 2));
        auto inner = track.getLocalBounds().reduced (2, 2);
        for (int i = 0; i < 3; ++i) tabs[size_t (i)]->setBounds (inner.removeFromLeft (widths[i]));
        r.removeFromTop (kRowGap);
    };

    segments (whatTrack, whatTabs, 96);
    segments (rangeTrack, rangeTabs, 104);
    segments (formatTrack, formatTabs, 72);
    segments (loudnessTrack, loudnessTabs, 84);

    r.removeFromTop (kCapH + kCapGap);
    {
        auto row = r.removeFromTop (20);
        const int w = juce::jmax (84, folderButton.idealWidth());
        folderButton.setBounds (row.removeFromRight (w).withSizeKeepingCentre (w, Dine::Metric::control));
    }

    auto foot = card.reduced (kPadX, kPadY).removeFromBottom (Dine::Metric::button);
    const int ew = juce::jmax (84, exportButton.idealWidth());
    exportButton.setBounds (foot.removeFromRight (ew));
    foot.removeFromRight (10);
    const int cw = juce::jmax (80, cancelButton.idealWidth());
    cancelButton.setBounds (foot.removeFromRight (cw));
}

} // namespace livemix
