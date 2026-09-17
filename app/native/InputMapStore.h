#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "Mix/MixSession.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// INPUT MAPS
//
// A church patches the same desk the same way every Sunday: input 1 is the kick, 9 is the
// bass, 17 is the lead, 18 to 23 are the singers. Rebuilding that by hand for every session
// is the single most tedious thing DLIVE asks of a volunteer, and it is exactly the kind of
// thing a computer should remember.
//
// A map is the patch and nothing else: which device channel is what, what it is called, what
// it is, whether it is a stereo pair. It deliberately does *not* carry a mix - no faders, no
// chains, no plan - because a patch outlives a mix and because restoring somebody else's
// fader moves onto your console is not a kindness.
//
// The one thing a map must never do is route audio to the wrong place. A map made on a
// 32-channel desk applied to an 8-channel interface cannot honour inputs 9 and up, so those
// inputs come back **disabled and named**, with a sentence saying what is missing. Silently
// moving them onto channels that exist would put the pastor's microphone on the drum kit.
// ---------------------------------------------------------------------------

struct InputMap
{
    static constexpr int kSchemaVersion = 1;

    juce::String name;
    juce::String note;                     // "Main hall, Sunday" - the user's own words
    juce::Time modified;
    // What it was made on, so applying it can say "this map wants 24 inputs; this device has 8".
    juce::String deviceName;
    int deviceInputCount = 0;
    std::vector<InputAssignment> inputs;
    // The map remembers what the session it came from was for, because a patch and a purpose
    // usually travel together ("the Sunday broadcast rig"). Applying it is opt-in.
    bool hasSound = false;
    StyleProfileId profile = StyleProfileId::ModernGospel;
    MixPurpose purpose = MixPurpose::ChurchBroadcast;
    DeliveryLoudness delivery = DeliveryLoudness::FromPurpose;

    // The highest device channel this map needs, 1-based. 0 = nothing patched.
    int channelsNeeded() const noexcept
    {
        int n = 0;
        for (const auto& in : inputs)
        {
            n = juce::jmax (n, in.inputA + 1);
            n = juce::jmax (n, in.inputB + 1);
        }
        return n;
    }
    int numStereo() const noexcept
    {
        int n = 0;
        for (const auto& in : inputs) if (in.isStereo()) ++n;
        return n;
    }
    bool valid() const noexcept { return ! inputs.empty(); }
};

namespace InputMapStore
{
    // Where maps live: beside the sessions, so backing up one folder backs up both.
    juce::File folder();
    juce::File fileFor (const juce::String& mapName);

    struct Listing
    {
        juce::String name;
        juce::String note;
        juce::File file;
        juce::Time modified;
        int inputCount = 0;
        int channelsNeeded = 0;
        juce::String deviceName;
        // How the inputs fall across the groups, for the little stacked bar in the list.
        std::array<int, int (MixBus::Count)> perBus {};
    };
    juce::Array<Listing> list();

    // Build a map from what is patched now.
    InputMap fromSession (const MixSession&, const juce::String& name, const juce::String& deviceName,
                          int deviceInputCount, bool includeSound);

    juce::var toVar (const InputMap&);
    bool fromVar (const juce::var&, InputMap&);

    bool save (const InputMap&);                                   // into folder(), named by the map
    bool saveAs (const InputMap&, const juce::File& destination);   // Export: anywhere the user likes
    bool load (const juce::File&, InputMap&);
    bool remove (const juce::String& mapName);
    juce::String rename (const juce::String& from, const juce::String& to);      // "" on success
    juce::String duplicate (const juce::String& from, const juce::String& to);   // "" on success

    // ---- Applying a map ----
    //
    // The result is a session and an honest account of what could not be honoured. Nothing is
    // ever quietly moved to a channel that happens to exist: an input the device cannot
    // provide comes back disabled, keeping its name and its source, so the ASSIGN page shows
    // it greyed with the reason rather than routing the wrong audio through it.
    struct ApplyResult
    {
        MixSession session;
        std::vector<juce::String> problems;   // one sentence each; shown before anything is applied
        int restored = 0;
        int unavailable = 0;                  // inputs the device cannot provide
        int conflicts = 0;                    // two inputs wanting the same device channel
        bool ok() const noexcept { return problems.empty(); }
    };
    // `availableInputs` is how many input channels the open device really has; 0 means "no
    // device open", in which case nothing is disabled and the caller is told to check later.
    ApplyResult apply (const InputMap&, const MixSession& current, int availableInputs,
                       const juce::String& deviceName, bool applySound);
}

} // namespace livemix
