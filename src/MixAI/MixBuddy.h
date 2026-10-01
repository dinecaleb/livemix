#pragma once
#include <array>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"
#include "Mix/MixSession.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// MIX BUDDY: help, not a second mixing engine
//
// Three things in DINE have a say about a mix, and each has one job:
//
//   TUNE MIX    "help me improve this mix"        - listens, proposes, you KEEP or REVERT
//   AUTOPILOT   "hold the mix while I am away"    - small, slow, bounded group moves
//   MIX BUDDY   "help me understand and fix it"   - explains, diagnoses, shows you where
//
// So Mix Buddy never changes the mix. A question goes in, and what comes out is an answer: a
// sentence, the facts it was read from, and buttons for the next step. Every button is
// something the engineer presses on purpose - show me where, open the channel, solo it (which
// only ever reaches the engineer's own listen), run TUNE, or ask TUNE LIVE MIX for the change
// in so many words, which arrives on BEFORE / AFTER like any other proposal and is refused
// while LIVE SAFE is on. Nothing in this file can reach a parameter.
//
// It is deterministic and offline: the same question about the same state gives the same
// answer, with no account and no network, because the booth Mac is very often not on the
// internet and the service starts anyway. What it knows about DINE is written against the
// controls that exist (see MixBuddy.cpp); a control that does not exist is never named.
// ---------------------------------------------------------------------------

// Where a "Show me where" button goes. The workspaces and set-up pages a person can be sent to.
enum class BuddyPage { Tracks, Mixer, Tune, Live, Inspector, Routing, Assign, Device, Purpose, Outputs, Sessions };

enum class BuddyActionKind
{
    ShowPage,           // go to a workspace or a set-up page
    OpenInspector,      // one channel, in full
    SoloStrip,          // the engineer's own listen only: never the room, never the stream
    OpenCheckInputs,    // CHECK INPUTS
    OpenHistory,        // the Mix history
    RunTuneMix,         // start TUNE MIX (it listens, then proposes; nothing lands until KEEP)
    RunTuneChannel,     // TUNE one channel
    AskForChange,       // TUNE LIVE MIX with this request: a proposal on BEFORE / AFTER, never kept by itself
};

struct BuddyAction
{
    BuddyActionKind kind = BuddyActionKind::ShowPage;
    std::string label;                  // the button: "Show me where", "Open Lead", "Solo Lead"
    BuddyPage page = BuddyPage::Mixer;
    int strip = -1;
    std::string request;                // AskForChange: the change, in the words TUNE LIVE MIX reads
};

// What Mix Buddy can see: the state of the session right now, read by MixController. Nothing
// here is a handle on the engine - it is a copy, so an answer can never change anything.
struct BuddyStrip
{
    std::string name;
    ChannelRole role = ChannelRole::LeadVocal;
    MixBus bus = MixBus::Music;
    int input = -1;                     // the device input, from 1
    float inputRmsDb = -120.0f;         // arriving at the channel, before anything DINE does
    float inputPeakDb = -120.0f;
    bool clipped = false;
    float inputGainDb = 0.0f;           // DINE's digital gain
    float faderDb = 0.0f;
    bool mute = false, solo = false;
    bool compOn = false;
    float compReductionDb = 0.0f;       // how hard the compressor is working now, dB (positive)
    bool gateOn = false;
    float gateReductionDb = 0.0f;
    bool sampleOn = false;              // the drum sound is being played on this channel
};

struct BuddyGroup
{
    bool used = false;
    float faderDb = 0.0f;
    bool mute = false, solo = false;
};

struct BuddySnapshot
{
    bool running = false;               // a mix is built and the device is open
    bool heard = false;                 // TUNE MIX has listened to this band
    bool bypass = false, broadcastMute = false, broadcastDim = false;
    bool liveSafe = false, autopilot = false, speechPriority = false;
    bool soloInPlace = false;           // the solo mode that does change the main mix
    bool monitorOutput = false;         // somewhere for solo to go
    float speechDuckDb = 0.0f;          // what speech priority is taking off the band right now
    std::vector<BuddyStrip> strips;
    std::array<BuddyGroup, int (MixBus::Count)> groups {};
    // The master, as the meters see it.
    bool limiterOn = false;
    float shortLufs = -120.0f, integratedLufs = -120.0f, targetLufs = -14.0f;
    float truePeakDb = -120.0f, ceilingDb = -1.0f, limiterReductionDb = 0.0f;
    bool masterClipped = false;
    int outputHeldBlocks = 0;           // times the last-resort guard held an output at full scale
    int nonFiniteBlocks = 0;            // times a stage produced something that was not a number
};

struct BuddyAnswer
{
    std::string text;                   // the answer, in sentences
    std::vector<std::string> detail;    // the facts it was read from, a line each
    std::vector<BuddyAction> actions;   // what to do next, as buttons
    bool notUnderstood = false;
};

namespace MixBuddy
{
    // The whole of it: a pure function of the question and the state. Never changes anything.
    BuddyAnswer answer (const std::string& question, const BuddySnapshot& state);

    // Questions it answers, for the empty panel. Every one of them is guaranteed an answer.
    std::vector<std::string> examples();

    // The channel a question is about ("channel 14", "the lead vocal", "pastor", a name), or
    // -1. Public because the tests pin it, and because the words have to mean one thing.
    int findStrip (const std::string& question, const BuddySnapshot& state);
}

} // namespace livemix
