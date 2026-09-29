#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <string>
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

    // One loaded sound, in the slot order the engine's `replaceSound` indexes. `user` and
    // `path` are what makes a slot identifiable across a reload: the index moves whenever the
    // folders change, the name does not. See SampleChoice in SessionState.h.
    struct Sound
    {
        std::string name;        // the file (or folder) name, without its extension
        bool user = false;       // from the user folder or this session's own, not the bundle
        std::string path;        // relative to that folder ("Snare/My Snare.wav"); empty for a placeholder
        bool inSession = false;  // ... and it is this session's own copy, so it travels with it
    };

    // WHERE THIS SESSION KEEPS ITS OWN SOUNDS. A sound imported from a strip is copied in
    // here - <session>/Samples/<kick|snare|toms>/ - rather than into ~/Music, so the session
    // folder is the whole session: hand it to somebody else, or open it on the booth Mac next
    // Sunday, and the kick it was mixed with is in it. Set before load(); empty means a
    // session with no folder of its own yet, and an import then goes to the user folder.
    void setSessionFolder (const juce::File&);
    const juce::File& sessionFolder() const noexcept { return session; }

    // Bring a file in as a sound of this family: copied (never referenced), named from the
    // file, and the library reloaded so the engine can play it. Returns the name it was filed
    // under, or an empty string with `problem` saying why not. Message thread.
    juce::String importSound (RoleFamily family, const juce::File& source, juce::String& problem);

    // Decodes everything it finds. Message thread; touches files and allocates.
    void load();

    const SampleBankTable* table() const noexcept { return &banks; }
    juce::StringArray soundNames (RoleFamily family) const;
    int numSounds (RoleFamily family) const;
    // In slot order, so `sounds(family)[i]` is what `replaceSound == i` plays.
    const std::vector<Sound>& sounds (RoleFamily family) const;
    // The slot that holds this sound now, or -1 when it is not loaded. A user sound is matched
    // on its relative path when it has one (two folders may hold a "Kick" each) and on its
    // name otherwise; a built-in is matched on its name, which is its identity in the bundle.
    int slotFor (RoleFamily family, const std::string& name, bool user, const std::string& path = {}) const;
    // True when the family is full, so the next sound found would be dropped.
    bool familyFull (RoleFamily family) const;
    juce::StringArray whereLoadedFrom() const { return sources; }   // for the log and the tests
    // "The kick folder holds 11 sounds; DLIVE loads the first 8." One line per family that
    // overflowed, so the limit is said out loud rather than silently applied.
    juce::StringArray whatWasLeftOut() const { return overflowed; }

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
    std::array<std::vector<Sound>, int (RoleFamily::Count)> catalogue;
    std::array<int, int (RoleFamily::Count)> counts {};
    juce::File session;             // this session's own Samples folder, when it has one
    juce::StringArray sources;
    juce::StringArray overflowed;   // the families whose folders hold more than kSounds
};

// The folder word a family's sounds are filed under, which is what a SessionState stores to
// name a sound independently of its slot: "kick", "snare", "toms", or empty for a family with
// no Sample stage. And the way back.
const char* sampleFamilyFolder (RoleFamily family) noexcept;
RoleFamily sampleFamilyFromFolder (const std::string& folder) noexcept;

} // namespace livemix
