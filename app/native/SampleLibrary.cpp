#include "SampleLibrary.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <map>

namespace livemix
{

namespace
{
    // The folder names a bank is filed under. "toms" is what a drummer says; "tom" is accepted.
    bool familyForFolder (const juce::String& name, RoleFamily& out)
    {
        const auto n = name.trim().toLowerCase();
        if (n == "kick" || n == "kicks") { out = RoleFamily::Kick; return true; }
        if (n == "snare" || n == "snares") { out = RoleFamily::Snare; return true; }
        if (n == "tom" || n == "toms") { out = RoleFamily::Tom; return true; }
        return false;
    }
}

juce::File SampleLibrary::builtInFolder()
{
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    const auto bundled = app.getChildFile ("Contents").getChildFile ("Resources").getChildFile ("Samples");
    if (bundled.isDirectory()) return bundled;
#ifdef DLIVE_SAMPLES_DIR
    const juce::File source (DLIVE_SAMPLES_DIR);
    if (source.isDirectory()) return source;
#endif
    return {};
}

juce::File SampleLibrary::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DLIVE").getChildFile ("Samples");
}

void SampleLibrary::load()
{
    owned.clear();
    banks = SampleBankTable {};
    counts.fill (0);
    sources.clear();

    const auto builtIn = builtInFolder();
    if (builtIn.isDirectory()) loadFolder (builtIn, true);
    const auto user = userFolder();
    if (user.isDirectory() && user != builtIn) loadFolder (user, false);

    // A family with nothing to play gets the synthesised placeholders.
    for (auto family : { RoleFamily::Kick, RoleFamily::Snare, RoleFamily::Tom })
    {
        if (counts[size_t (family)] > 0) continue;
        for (int variant = 0; variant < 3; ++variant)
        {
            auto b = std::make_unique<SampleBank> (synthesizeBank (family, variant, 48000.0));
            banks.set (family, variant, b.get());
            owned.push_back (std::move (b));
        }
        counts[size_t (family)] = 3;
        sources.add ("synthesised placeholders for " + juce::String (family == RoleFamily::Kick ? "kick" : family == RoleFamily::Snare ? "snare" : "toms"));
    }
}

void SampleLibrary::loadFolder (const juce::File& root, bool builtIn)
{
    int loaded = 0;
    for (const auto& familyDir : root.findChildFiles (juce::File::findDirectories, false))
    {
        RoleFamily family;
        if (! familyForFolder (familyDir.getFileName(), family)) continue;
        // Sounds: loose .wav files and sub-folders, by name.
        juce::Array<juce::File> entries;
        for (const auto& f : familyDir.findChildFiles (juce::File::findFilesAndDirectories, false))
            if (f.isDirectory() || f.hasFileExtension ("wav;aif;aiff;flac")) entries.add (f);
        std::sort (entries.begin(), entries.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
        for (const auto& entry : entries)
        {
            int& slot = counts[size_t (family)];
            if (slot >= SampleBankTable::kSounds) break;
            auto bank = decodeSound (entry, entry.getFileNameWithoutExtension());
            if (bank == nullptr) continue;
            banks.set (family, slot, bank.get());
            owned.push_back (std::move (bank));
            ++slot;
            ++loaded;
        }
    }
    if (loaded > 0) sources.add (juce::String (builtIn ? "built-in: " : "yours: ") + root.getFullPathName());
}

std::unique_ptr<SampleBank> SampleLibrary::decodeSound (const juce::File& entry, const juce::String& name)
{
    auto bank = std::make_unique<SampleBank>();
    bank->name = name.toStdString();
    juce::Array<juce::File> files;
    if (entry.isDirectory())
    {
        for (const auto& f : entry.findChildFiles (juce::File::findFiles, false))
            if (f.hasFileExtension ("wav;aif;aiff;flac")) files.add (f);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    }
    else files.add (entry);

    for (const auto& f : files)
    {
        std::vector<float> mono;
        double rate = 48000.0;
        if (! decodeHit (f, mono, rate)) continue;
        prepareHit (mono);
        if (mono.empty()) continue;
        if (bank->layers.empty()) bank->sampleRate = rate;
        else if (std::abs (rate - bank->sampleRate) > 1.0) continue;   // one rate per bank: a layer at another is left out
        SampleBank::Layer layer;
        layer.hits.push_back (std::move (mono));
        bank->layers.push_back (std::move (layer));
    }
    if (bank->layers.empty()) return nullptr;
    bank->fundamentalHz = measureFundamental (bank->layers.back().hits[0], bank->sampleRate);
    return bank;
}

bool SampleLibrary::decodeHit (const juce::File& file, std::vector<float>& mono, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0) return false;
    // Ten seconds is more drum than anyone triggers; a longer file is a mistake, and its start is enough.
    const int length = int (std::min<juce::int64> (reader->lengthInSamples, juce::int64 (reader->sampleRate * 10.0)));
    juce::AudioBuffer<float> buffer (int (reader->numChannels), length);
    if (! reader->read (&buffer, 0, length, 0, true, true)) return false;
    sampleRate = reader->sampleRate;
    mono.assign (size_t (length), 0.0f);
    const float scale = 1.0f / float (buffer.getNumChannels());
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* x = buffer.getReadPointer (ch);
        for (int i = 0; i < length; ++i) mono[size_t (i)] += x[i] * scale;
    }
    return true;
}

juce::StringArray SampleLibrary::soundNames (RoleFamily family) const
{
    juce::StringArray out;
    for (int i = 0; i < SampleBankTable::kSounds; ++i)
        if (const auto* b = banks.bank (family, i)) out.add (juce::String (b->name));
    return out;
}

int SampleLibrary::numSounds (RoleFamily family) const
{
    return int (family) >= 0 && int (family) < int (RoleFamily::Count) ? counts[size_t (family)] : 0;
}

} // namespace livemix
