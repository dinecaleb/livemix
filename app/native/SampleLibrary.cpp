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

    // The folder name a family is filed under - the one word a SampleChoice stores, and the
    // one a sentence about it uses.
    const char* familyFolderName (RoleFamily f) noexcept
    {
        switch (f)
        {
            case RoleFamily::Kick:  return "kick";
            case RoleFamily::Snare: return "snare";
            case RoleFamily::Tom:   return "toms";
            default:                return "";
        }
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
    overflowed.clear();
    for (auto& c : catalogue) c.clear();

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
            catalogue[size_t (family)].push_back ({ b->name, false, {} });
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
        int dropped = 0;
        for (const auto& entry : entries)
        {
            int& slot = counts[size_t (family)];
            // The cap is real, so it is said out loud: a ninth kick used to be dropped in
            // silence, which is indistinguishable from a file DLIVE could not read.
            if (slot >= SampleBankTable::kSounds) { ++dropped; continue; }
            auto bank = decodeSound (entry, entry.getFileNameWithoutExtension());
            if (bank == nullptr) continue;
            banks.set (family, slot, bank.get());
            catalogue[size_t (family)].push_back ({ bank->name, ! builtIn,
                                                   entry.getRelativePathFrom (root).toStdString() });
            owned.push_back (std::move (bank));
            ++slot;
            ++loaded;
        }
        if (dropped > 0)
            overflowed.add (juce::String (familyDir.getFullPathName()) + " holds " + juce::String (entries.size())
                            + " sounds and DLIVE plays " + juce::String (SampleBankTable::kSounds)
                            + " of each drum, so " + juce::String (dropped)
                            + " of them are not loaded. Take some out of the folder to reach the rest.");
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

const std::vector<SampleLibrary::Sound>& SampleLibrary::sounds (RoleFamily family) const
{
    static const std::vector<Sound> none;
    if (int (family) < 0 || int (family) >= int (RoleFamily::Count)) return none;
    return catalogue[size_t (family)];
}

int SampleLibrary::slotFor (RoleFamily family, const std::string& name, bool user, const std::string& path) const
{
    if (name.empty()) return -1;
    const auto& list = sounds (family);
    // A user sound's relative path is the stronger identity when the document carries one:
    // a built-in and a personal sound may share a name, and so may two of the engineer's own
    // in different sub-folders.
    if (user && ! path.empty())
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].user && list[i].path == path) return int (i);
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].user == user && list[i].name == name) return int (i);
    return -1;
}

bool SampleLibrary::familyFull (RoleFamily family) const
{
    return numSounds (family) >= SampleBankTable::kSounds;
}

// The folder word a SampleChoice stores for this family ("kick", "snare", "toms"); empty for
// a family that has no Sample stage.
const char* sampleFamilyFolder (RoleFamily family) noexcept { return familyFolderName (family); }

RoleFamily sampleFamilyFromFolder (const std::string& folder) noexcept
{
    RoleFamily f = RoleFamily::Count;
    familyForFolder (juce::String (folder), f);
    return f;
}

} // namespace livemix
