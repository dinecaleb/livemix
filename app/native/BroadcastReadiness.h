#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "Mix/MixSession.h"

namespace livemix
{

// Broadcast readiness is for a stream or church broadcast mix. Live Recording and Worship
// Session keep their own paths later; Podcast-style delivery is not this checklist.
inline bool broadcastReadinessApplies (MixPurpose purpose) noexcept
{
    return purpose == MixPurpose::ChurchBroadcast || purpose == MixPurpose::Livestream;
}

// ---------------------------------------------------------------------------
// BROADCAST READINESS
//
// A lightweight, optional checklist for the livestream mix. Operators confirm the
// broadcast path by hand; DINE may show observations beside a check, but never
// treats a tick as proof that the stream sounds good. Checking an item never
// changes routing, gain, processing, mutes, solos or recording.
//
// Message-thread only. Nothing here is read or written from the audio thread.
// ---------------------------------------------------------------------------

enum class ReadinessStatus : int
{
    Pending = 0,
    Checked,
    NeedsAttention,
    NotNeeded
};

enum class ReadinessItemId : int
{
    SourcesMapped = 0,
    InputLevels,
    BroadcastOutput,
    SpeechClear,
    LeadVocals,
    InstrumentsBalanced,
    DynamicsNatural,
    RoomSound,
    OutputLevel,
    NoUnintendedSoloMute,
    StreamListened,
    AvSync,
    MusicSpeechTransitions,
    RecoverySnapshot,
    RecordingConfirmed,
    Count
};

enum class ReadinessGroup : int
{
    SignalAndRouting = 0,
    BroadcastMix,
    FinalStreamCheck
};

inline constexpr int kReadinessItemCount = int (ReadinessItemId::Count);

struct ReadinessItemDef
{
    ReadinessItemId id;
    ReadinessGroup group;
    const char* label;       // short, sentence case
    const char* guidance;    // practical help for a volunteer
    bool allowsNotNeeded;    // room sound, recording
};

const ReadinessItemDef& readinessItemDef (ReadinessItemId) noexcept;
const char* readinessGroupName (ReadinessGroup) noexcept;
const char* readinessStatusName (ReadinessStatus) noexcept;

struct ReadinessItemState
{
    ReadinessStatus status = ReadinessStatus::Pending;
    std::string note;
    long long confirmedMs = 0;     // wall clock when status last set; 0 = never
    bool needsReview = false;      // flagged after a routing / mapping / device change
};

struct ReadinessProgress
{
    int checked = 0;
    int applicable = 0;            // everything except Not needed
    int notNeeded = 0;
    int needsAttention = 0;
    int pending = 0;
    int needsReview = 0;

    // "4 of 13 done, 1 problem" - never "Ready" from ticks alone.
    std::string summaryLine() const;
};

struct ReadinessRecord
{
    std::string id;                // stable; survives reset
    std::string name;              // editable service / set name
    long long startedMs = 0;
    long long finishedMs = 0;      // 0 while in progress
    std::string operatorName;      // optional; local reuse, no accounts
    bool finished = false;
    std::array<ReadinessItemState, kReadinessItemCount> items {};

    ReadinessProgress progress() const noexcept;
    bool hasWork() const noexcept;           // anything other than a blank checklist
};

// Sets one item on any record - the active one, or a past one being corrected. "Not needed"
// on an item that does not allow it is taken as back to "to do".
void setReadinessItem (ReadinessRecord&, ReadinessItemId, ReadinessStatus);

// What changed enough to ask the operator to look again at an active check.
enum class ReadinessChange : int
{
    InputMapping = 0,
    BroadcastRouting,
    BroadcastDevice
};

struct ReadinessSummary
{
    int broadcasts = 0;
    int checked = 0;
    int applicable = 0;
    std::array<int, kReadinessItemCount> pendingCount {};
    std::array<int, kReadinessItemCount> attentionCount {};
    std::vector<std::string> relatedNotes;   // compact sample of notes (newest first)
};

struct BroadcastReadiness
{
    ReadinessRecord active;
    std::vector<ReadinessRecord> history;    // finished (and kept) records, newest first
    std::vector<std::string> knownOperators;

    void ensureActive();                     // first open: create an in-progress record
    ReadinessRecord newService (const std::string& name = {},
                                const std::string& operatorName = {});
    void finishActive();                     // keeps Pending / Needs attention as they are
    void reopenActive();                     // a finished service, editable again
    void resetActiveStatuses();              // same id and notes; statuses back to Pending
    void setItem (ReadinessItemId, ReadinessStatus, const std::string& note = {});
    void setItemNote (ReadinessItemId, const std::string& note);
    void setActiveName (const std::string&);
    void setActiveOperator (const std::string&);
    void flagForReview (ReadinessChange);    // active only; history untouched
    void rememberOperator (const std::string&);

    // A past record, for an explicit correction (never by moving it into `active`).
    ReadinessRecord* historyMutable (const std::string& id);
    const ReadinessRecord* historyFind (const std::string& id) const noexcept;

    std::vector<const ReadinessRecord*> filterHistory (long long fromMs, long long toMs,
                                                       const std::string& operatorFilter) const;
    ReadinessSummary summarise (long long fromMs, long long toMs,
                                const std::string& operatorFilter) const;
};

ReadinessRecord makeFreshReadinessRecord (const std::string& name = {},
                                          const std::string& operatorName = {});

bool readinessEquals (const BroadcastReadiness& a, const BroadcastReadiness& b) noexcept;

} // namespace livemix
