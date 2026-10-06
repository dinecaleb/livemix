#pragma once
#include <array>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include "Mix/MixSession.h"
#include "Mix/MixParameters.h"
#include "Mix/MixMacros.h"
#include "Mix/ReferenceMix.h"
#include "Mix/OutputFeeds.h"
#include "Mix/LiveSafe.h"
#include "MixHistory.h"
#include "BroadcastReadiness.h"
#include "Project.h"

namespace livemix
{

class MixController;
class DawEngine;
class SampleLibrary;

// ---------------------------------------------------------------------------
// THE SESSION, AS ONE THING
//
// Everything that can change what a DINE session sounds like or how it is wired, in one
// owned model. This is the only thing SessionStore serialises, and there are exactly two
// functions that move it in and out of the live objects: captureSession() and
// applySession(), at the bottom of this header. Nothing else assembles a session out of
// getters.
//
// That rule is the whole point. The old shape had three partial copies - the document built
// by hand in Main.cpp, a `pending` snapshot taken across device changes, and the controller
// itself - and every new feature had to be threaded through all three. The copies drifted,
// and because the code that built them lived in Main.cpp (which no test compiles) the drift
// was invisible. docs/SESSION-STATE.md has the audit.
//
// Two things are deliberately NOT here, because they are ways of listening rather than parts
// of the mix, and a session must not open in one of them: BYPASS, and the DIM / MUTE
// emergency keys. The plan on preview is not here either - it is a proposal, not a session.
// ---------------------------------------------------------------------------

// The devices the engineer chose, as they chose them - never the combined device DINE
// builds around them, which is rebuilt on opening so a session survives a Mac that lost it.
struct DeviceChoice
{
    juce::String consoleInput;       // the desk / interface / Dante the inputs arrive on
    juce::String broadcastOutput;    // where the mix leaves for OBS, Ecamm, the recorder
    juce::String soloOutput;         // the engineer's own listen; empty = solo has nowhere to go
    // Which physical unit, as CoreAudio names it for good. A name is a label the maker chose, and
    // two interfaces of the same model share one: the name finds the device, the UID says
    // whether it is the same one. Empty for a session saved before these were kept.
    juce::String consoleInputUid, broadcastOutputUid;

    bool operator== (const DeviceChoice& o) const noexcept
    {
        return consoleInput == o.consoleInput && broadcastOutput == o.broadcastOutput && soloOutput == o.soloOutput
            && consoleInputUid == o.consoleInputUid && broadcastOutputUid == o.broadcastOutputUid;
    }
    bool operator!= (const DeviceChoice& o) const noexcept { return ! (*this == o); }
};

// WHICH SOUND A DRUM STRIP PLAYS, BY NAME.
//
// `ChannelParameters::replaceSound` is a released parameter ID and stays exactly what it has
// always been: an index, 0..7, into the family's loaded banks. But the list those banks sit
// in is built from two folders at launch (built-ins, then ~/Music/DINE/Samples), sorted by
// name - so the index means "the fourth kick I happened to find today", which is not an
// identity. Shipping a new built-in, renaming a file, or a sample that fails to decode this
// morning all move it, and the session plays a different drum without saying so.
//
// So the name is stored beside the index and the index is resolved from it at load. When the
// sound is gone the strip says so in a sentence and its Sample stage is switched off, rather
// than silently playing whatever now sits in that slot.
struct SampleChoice
{
    std::string family;      // "kick", "snare", "toms" - the folder a bank is filed under
    std::string name;        // the sound's name, which is its file name without the extension
    bool user = false;       // true: one of the engineer's own, from ~/Music/DINE/Samples
    std::string path;        // relative to that folder, for a user sound ("Snare/My Snare.wav")

    bool set() const noexcept { return ! name.empty(); }
    bool operator== (const SampleChoice& o) const noexcept
    {
        return family == o.family && name == o.name && user == o.user && path == o.path;
    }
    bool operator!= (const SampleChoice& o) const noexcept { return ! (*this == o); }
};

struct SessionState
{
    // ---- who is what, and what the mix is for
    MixSession session;

    // ---- the timeline: tracks, takes, clips, markers, loop, LIVE SAFE's on/off
    Project project;

    // ---- the devices, as chosen
    DeviceChoice devices;

    // ---- the kept mix: every strip, bus, return, the master, the monitor, the tempo
    //
    // `hasMix` is false only for a session that has never been built - not for one whose
    // device is unplugged. A mix exists as soon as the inputs are assigned, because
    // MixController::rebuild() needs neither a device nor a sample rate to build it.
    bool hasMix = false;
    MixParameters mix;
    MixMacroValues macros;
    int tuneCount = 0;

    // ---- where the sound leaves the device (monitoring and routing, never the mix)
    OutputFeeds outputs;

    // ---- LIVE SAFE. `project.liveSafe` is the authority for whether it is on; this carries
    // the limits, which used to reset to their defaults on every launch.
    LiveSafePolicy safety;

    // ---- what the mix is aimed at, already measured (the file itself is never stored)
    ReferenceProfile reference;

    // ---- the scenes: the whole mix kept for one part of the service. Empty slots by name.
    std::vector<MixScene> scenes;

    // ---- the track history: every tune and hand edit on each channel, before and after
    std::vector<StripTuneRecord> history;

    // ---- the mix history: the whole mix as it was at each moment worth coming back to
    std::vector<MixCheckpoint> checkpoints;

    // ---- the last TUNE LIVE MIX run, as a record. Reading only: the mix itself is in `mix`,
    // so opening yesterday's session sounds as it did without contacting any provider, ever.
    juce::var tuneLive;

    // ---- which sound each drum strip plays, by name (see SampleChoice)
    std::array<SampleChoice, kMaxStrips> samples {};

    // ---- layout the engineer set once and expects back
    int trackPanelWidth = 0;         // 0 = the page's own default

    // ---- Broadcast readiness: optional checklist for the livestream mix. Confirmations only;
    // never routing, gain or recording. Active progress and finished history travel with the
    // session; restored ticks are previous confirmations, not proof of current readiness.
    BroadcastReadiness readiness;
};

// ---------------------------------------------------------------------------
// The only two functions that move a session between the file and the live objects.
// ---------------------------------------------------------------------------

// Reads the session out of the controller, the engine and the chosen devices. Never asks
// whether an audio device is open: a mix that exists is a mix that is saved.
SessionState captureSession (const MixController&, const DawEngine&,
                             const DeviceChoice&, int trackPanelWidth);

// Puts it back. Needs no device either - the assignments build the graph, the graph builds
// the mix, and a device (when one arrives) only decides the sample rate the DSP runs at.
// The caller opens the devices afterwards; `state.devices` says which.
void applySession (const SessionState&, MixController&, DawEngine&);

// Which sound each drum strip is playing, by name, read from the library the engine is using.
// Called on capture; a strip with no sample stage, or whose index does not resolve, is left
// unset.
void readSampleChoices (const MixController&, const SampleLibrary&,
                        std::array<SampleChoice, kMaxStrips>& out);

// The other direction: turn each stored name back into the index the engine reads, in the
// library as it is now. Returns one sentence per strip whose sound has gone (and switches
// that strip's Sample stage off), so the caller can say so; empty when everything resolved.
// `unresolved`, when given, receives the stored choice of every strip whose sound has gone -
// so the next save can write the name that was asked for instead of forgetting it.
// The inputs whose clips point at audio that is not on the disk (moved, renamed, a drive not
// plugged in): their names, once each. Those tracks play silence.
std::vector<std::string> missingAudio (const Project& project, const MixSession& session);

std::vector<std::string> resolveSampleChoices (const std::array<SampleChoice, kMaxStrips>&,
                                               const SampleLibrary&, MixController&,
                                               std::array<SampleChoice, kMaxStrips>* unresolved = nullptr);

// A choice that did not resolve on this Mac is still the session's choice. On capture, every
// strip whose stored sound was missing, and whose Sample stage nobody has switched back on
// since, keeps the name it was saved with; a strip whose stage is on again has been given a
// sound by hand, and forgets the old one.
void keepUnresolvedSampleChoices (std::array<SampleChoice, kMaxStrips>& unresolved, const MixController&,
                                  std::array<SampleChoice, kMaxStrips>& out);

} // namespace livemix
