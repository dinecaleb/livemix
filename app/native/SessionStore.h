#pragma once
#include <array>
#include <juce_core/juce_core.h>
#include "Mix/MixSession.h"
#include "Mix/MixParameters.h"
#include "Mix/MixMacros.h"
#include "Mix/OutputFeeds.h"
#include "Project.h"

namespace livemix
{

// Local-first, versioned JSON for one DLIVE session: the device, the assignments,
// purpose and sound, the five macros, the kept mix (every strip, bus and return) and the
// timeline (tracks, takes, markers). Sonic profiles and targets are code data, never
// stored here. House Sound (targets and preferences rather than frozen values) will be a
// separate, later document.
//
// A session is a folder, so the recordings live beside the document:
//   ~/Music/DLIVE/Sunday Service/Sunday Service.dlive.json
//   ~/Music/DLIVE/Sunday Service/Audio Files/Kick_001.wav ...
// Version 1 documents (a single file, no timeline) still open.
namespace SessionStore
{
    inline constexpr int kVersion = 3;   // 3 added the SPEECH group bus between VOCALS and MASTER

    struct Document
    {
        MixSession session;
        Project project;            // timeline: tracks, clips, markers, loop
        juce::String inputDevice, outputDevice;
        MixMacroValues macros;
        bool hasMix = false;
        MixParameters mix;          // the kept mix (without macros); valid when hasMix
        OutputFeeds outputs;        // where the sound leaves the device (monitoring, not mix)
        int tuneCount = 0;
        // The last TUNE LIVE MIX run, as a record: what it intended, what it built and what
        // it refused. Reading only - the mix itself is in `mix`, so opening yesterday's
        // session sounds exactly as it did without contacting any provider, ever. Null when
        // no live run has been made.
        juce::var tuneLive;
    };

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
    bool load (const juce::File& file, Document& d);
    juce::Array<Listing> listSessions();                // newest first
}

} // namespace livemix
