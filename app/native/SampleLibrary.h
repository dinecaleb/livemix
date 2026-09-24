#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include "DSP/SampleBank.h"

namespace livemix
{

// The sounds the Sample stage can play, decoded once on the message thread and published to
// the engine as a SampleBankTable. The built-in bank ships in the app bundle (app/Samples,
// copied to Contents/Resources/Samples by the build; a developer build reads the source
// folder); the engineer's own sounds come from ~/Music/DLIVE/Samples/<kick|snare|toms>. A
// `.wav` is one sound with one layer; a sub-folder of `.wav` files is one sound with several
// velocity layers, softest first by name. A family with nothing to load falls back to the
// synthesised placeholders so the stage always has something to play.
//
// Every bank lives for as long as the library does, and the library outlives the engine:
// the audio thread reads bank pointers and never a freed one.
class SampleLibrary
{
public:
    SampleLibrary() = default;

    // Decodes everything it finds. Message thread; touches files and allocates.
    void load();

    const SampleBankTable* table() const noexcept { return &banks; }
    juce::StringArray soundNames (RoleFamily family) const;
    int numSounds (RoleFamily family) const;
    juce::StringArray whereLoadedFrom() const { return sources; }   // for the log and the tests

    // Where the built-in bank is: the bundle's Resources, else the source folder the build was
    // configured from. Empty when neither exists (a test on a machine without the repo).
    static juce::File builtInFolder();
    static juce::File userFolder();                                 // ~/Music/DLIVE/Samples

private:
    void loadFolder (const juce::File& root, bool builtIn);
    std::unique_ptr<SampleBank> decodeSound (const juce::File& fileOrFolder, const juce::String& name);
    bool decodeHit (const juce::File& file, std::vector<float>& mono, double& sampleRate);

    std::vector<std::unique_ptr<SampleBank>> owned;
    SampleBankTable banks;
    std::array<int, int (RoleFamily::Count)> counts {};
    juce::StringArray sources;
};

} // namespace livemix
