#pragma once
#include <array>
#include <juce_core/juce_core.h>
#include "SessionState.h"

namespace livemix
{

// Local-first, versioned JSON for one DLIVE session. The session itself is SessionState
// (SessionState.h) - the one owned model - and this file does nothing but write it down and
// read it back. Sonic profiles and targets are code data, never stored here. House Sound
// (targets and preferences rather than frozen values) will be a separate, later document.
//
// A session is a folder, so the recordings live beside the document:
//   ~/Music/DLIVE/Sunday Service/Sunday Service.dlive.json
//   ~/Music/DLIVE/Sunday Service/Audio Files/Kick_001.wav ...
// Every earlier version still opens; see kVersion.
namespace SessionStore
{
    // 3 added the SPEECH group bus between VOCALS and MASTER; 4 added AMBIENCE the same way
    // (and with it the monitor / solo bus, which is new state rather than a moved index).
    // 5 is the canonical session: the same mix, plus the LIVE SAFE limits, which used to go
    // back to their defaults on every launch, and a name for each drum strip's sound, so a
    // reopened session plays the sound it was given rather than whatever now sits in that
    // slot. Every earlier version still opens: busFromStoredIndex remaps whatever layout it
    // finds, and a sample with no name stored is resolved from its index against the library
    // as it is at load time.
    // 6 added the LEAD group bus the same way SPEECH and AMBIENCE went in: immediately before
    // MASTER, which moves the master's stored index and nothing else's. A session saved before
    // it opens with an empty LEAD group and every lead microphone still on BGV, where it was;
    // the next TUNE MIX routes it where it belongs, because a routing graph is built from the
    // assignments rather than stored.
    inline constexpr int kVersion = 6;

    // The old name for SessionState, kept because it reads well at the call sites that mean
    // "the thing on disk".
    using Document = SessionState;

    struct Listing
    {
        juce::String name;
        juce::File file;
        juce::Time modified;
    };

    // What the Sessions library shows about a saved session without opening it: the header
    // fields only. The audio lives beside the document in its own folder and is never
    // walked; the document itself is small, so a library of a hundred services still lists
    // instantly. `valid` is false for a file that is not a DLIVE session.
    struct Summary
    {
        bool valid = false;
        StyleProfileId profile = StyleProfileId::ModernGospel;
        MixPurpose purpose = MixPurpose::ChurchBroadcast;
        int inputs = 0;          // assigned inputs
        int tracks = 0;          // timeline tracks that carry a take
        int tuneCount = 0;
        bool hasMix = false;
        juce::String inputDevice;
        std::array<int, int (MixBus::Master)> perBus {};   // how the inputs fall across the group buses
    };
    Summary summarise (const juce::File&);

    juce::var toVar (const Document& d);
    bool fromVar (const juce::var& v, Document& d);   // false when the file is not a DLIVE session

    juce::File sessionsFolder();                        // ~/Music/DLIVE
    juce::File legacyFolder();                          // ~/Library/Application Support/DLIVE/Sessions (version 1)
    juce::File formerNameFolder();                      // ~/Music/DINELIVE, from before the rename: read, never written
    juce::File folderFor (const juce::String& sessionName);
    juce::File fileFor (const juce::String& sessionName);
    bool save (const Document& d, const juce::File& file);

    // Written whole or not at all: a temporary file beside the target, flushed to the disk and
    // checked, then renamed over it. False when any byte did not land - a full disk leaves the
    // file that was there, never half of the new one. JUCE's own replaceWithText ignores the
    // write and renames anyway, which is why nothing in DLIVE that must survive uses it.
    bool writeTextAtomically (const juce::File& target, const juce::String& text);
    bool load (const juce::File& file, Document& d);
    juce::Array<Listing> listSessions();                // newest first
}

} // namespace livemix
