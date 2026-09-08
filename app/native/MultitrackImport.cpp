#include "MultitrackImport.h"
#include "StemNames.h"
#include <algorithm>

namespace livemix
{

MultitrackImport::Result MultitrackImport::fromFolder (const juce::File& folder, const MixSession& base)
{
    Result result;
    result.session = base;
    result.session.inputs.clear();
    result.session.name = folder.getFileName().toStdString();
    result.project = Project {};
    // Imported audio stays where it is (the clips hold absolute paths). The project has no
    // folder of its own until it is saved, which is also when recording becomes possible.

    if (! folder.isDirectory())
    {
        result.error = "That is not a folder.";
        return result;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto files = folder.findChildFiles (juce::File::findFiles, false, formats.getWildcardForAllFormats());
    files.sort();
    if (files.isEmpty())
    {
        result.error = "No audio files in " + folder.getFileName() + ".";
        return result;
    }

    int nextInput = 0;
    for (const auto& file : files)
    {
        if (nextInput >= kMaxInputs || int (result.session.inputs.size()) >= kMaxStrips) break;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0) continue;

        const int channels = juce::jlimit (1, 2, int (reader->numChannels));
        const auto display = StemNames::cleanName (file.getFileNameWithoutExtension());

        InputAssignment in;
        in.name = display.toStdString();
        in.inputA = nextInput;
        in.inputB = channels > 1 ? nextInput + 1 : -1;
        ChannelRole role = ChannelRole::LeadVocal;
        in.enabled = StemNames::guessRole (file.getFileNameWithoutExtension(), role);
        in.role = role;                 // an unrecognised file is listed but left for the user to assign
        result.session.inputs.push_back (in);

        TrackState track;
        AudioClip clip;
        clip.name = display;
        clip.file = file.getFullPathName();
        clip.start = 0;
        clip.offset = 0;
        clip.fileSampleRate = reader->sampleRate;
        clip.length = reader->lengthInSamples;
        track.clips.push_back (clip);
        track.monitor = MonitorMode::Auto;
        result.project.tracks.push_back (track);

        if (result.sampleRate <= 0.0) result.sampleRate = reader->sampleRate;
        nextInput += channels;
        ++result.files;
    }

    if (result.session.inputs.empty())
    {
        result.error = "None of the files in " + folder.getFileName() + " could be read.";
        return result;
    }

    if (result.sampleRate > 0.0) result.project.sampleRate = result.sampleRate;
    // The clips were measured in their own files' samples; the player resamples per clip,
    // so the timeline length is the longest file expressed at the project's rate.
    for (size_t i = 0; i < result.project.tracks.size(); ++i)
        for (auto& clip : result.project.tracks[i].clips)
            if (clip.fileSampleRate > 0.0 && result.project.sampleRate > 0.0)
                clip.length = juce::int64 (double (clip.length) * result.project.sampleRate / clip.fileSampleRate);

    result.project.loopStart = 0;
    result.project.loopEnd = juce::jmax ((juce::int64) 1, result.project.lengthSamples());
    return result;
}

} // namespace livemix
