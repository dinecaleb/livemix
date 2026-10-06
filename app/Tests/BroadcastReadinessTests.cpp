#include "TestFramework.h"
#include "native/BroadcastReadiness.h"
#include "native/MixController.h"
#include "native/SessionState.h"
#include "native/SessionStore.h"
#include "native/DawEngine.h"
#include "Mix/MixPlanner.h"
#include <juce_core/juce_core.h>

using namespace livemix;

TEST_CASE ("BroadcastReadiness: only Church Broadcast and Livestream apply")
{
    CHECK (broadcastReadinessApplies (MixPurpose::ChurchBroadcast));
    CHECK (broadcastReadinessApplies (MixPurpose::Livestream));
    CHECK (! broadcastReadinessApplies (MixPurpose::LiveRecording));
    CHECK (! broadcastReadinessApplies (MixPurpose::WorshipSession));
}

TEST_CASE ("BroadcastReadiness: progress counts applicable items honestly")
{
    BroadcastReadiness r;
    r.ensureActive();
    auto p0 = r.active.progress();
    CHECK (p0.checked == 0);
    CHECK (p0.applicable == kReadinessItemCount);
    CHECK (p0.notNeeded == 0);

    r.setItem (ReadinessItemId::SourcesMapped, ReadinessStatus::Checked);
    r.setItem (ReadinessItemId::RoomSound, ReadinessStatus::NotNeeded);
    r.setItem (ReadinessItemId::SpeechClear, ReadinessStatus::NeedsAttention);
    r.setItem (ReadinessItemId::RecordingConfirmed, ReadinessStatus::NotNeeded);

    const auto p = r.active.progress();
    CHECK (p.checked == 1);
    CHECK (p.notNeeded == 2);
    CHECK (p.needsAttention == 1);
    CHECK (p.applicable == kReadinessItemCount - 2);
    const auto line = p.summaryLine();
    CHECK (line == "1 of 13 done, 1 problem, 2 skipped");
    // Never claim the broadcast is "Ready" from ticks alone.
    CHECK (line.find ("Ready") == std::string::npos);
}

TEST_CASE ("BroadcastReadiness: Not needed refused on items that disallow it")
{
    BroadcastReadiness r;
    r.ensureActive();
    r.setItem (ReadinessItemId::SpeechClear, ReadinessStatus::NotNeeded);
    CHECK (r.active.items[size_t (ReadinessItemId::SpeechClear)].status == ReadinessStatus::Pending);
}

TEST_CASE ("BroadcastReadiness: separate records per service and reset isolation")
{
    BroadcastReadiness r;
    r.ensureActive();
    r.setActiveName ("Sunday 9am");
    r.setActiveOperator ("Alex");
    r.setItem (ReadinessItemId::SourcesMapped, ReadinessStatus::Checked, "Kick on ch 1");
    r.setItem (ReadinessItemId::SpeechClear, ReadinessStatus::NeedsAttention, "Gate bites");
    const auto firstId = r.active.id;
    REQUIRE (! firstId.empty());

    r.newService ("Sunday 11am", "Blake");
    CHECK (r.active.id != firstId);
    CHECK (r.active.name == "Sunday 11am");
    CHECK (r.active.progress().checked == 0);
    REQUIRE (r.history.size() == 1);
    CHECK (r.history[0].id == firstId);
    CHECK (r.history[0].items[size_t (ReadinessItemId::SourcesMapped)].status == ReadinessStatus::Checked);
    CHECK (r.history[0].items[size_t (ReadinessItemId::SourcesMapped)].note == "Kick on ch 1");

    // Reset active only: identity and notes stay; history untouched.
    r.setItem (ReadinessItemId::InputLevels, ReadinessStatus::Checked, "Hot but ok");
    const auto secondId = r.active.id;
    r.resetActiveStatuses();
    CHECK (r.active.id == secondId);
    CHECK (r.active.items[size_t (ReadinessItemId::InputLevels)].status == ReadinessStatus::Pending);
    CHECK (r.active.items[size_t (ReadinessItemId::InputLevels)].note == "Hot but ok");
    CHECK (r.history[0].items[size_t (ReadinessItemId::SourcesMapped)].status == ReadinessStatus::Checked);

    r.finishActive();
    CHECK (r.active.finished);
    CHECK (r.active.items[size_t (ReadinessItemId::SpeechClear)].status == ReadinessStatus::Pending); // was never set on second
}

TEST_CASE ("BroadcastReadiness: routing / mapping flags active checks for review")
{
    BroadcastReadiness r;
    r.ensureActive();
    r.setItem (ReadinessItemId::SourcesMapped, ReadinessStatus::Checked);
    r.setItem (ReadinessItemId::BroadcastOutput, ReadinessStatus::Checked);
    r.setItem (ReadinessItemId::SpeechClear, ReadinessStatus::Checked);

    r.flagForReview (ReadinessChange::InputMapping);
    CHECK (r.active.items[size_t (ReadinessItemId::SourcesMapped)].needsReview);
    CHECK (! r.active.items[size_t (ReadinessItemId::BroadcastOutput)].needsReview);
    CHECK (! r.active.items[size_t (ReadinessItemId::SpeechClear)].needsReview);
    // Previous confirmation details preserved.
    CHECK (r.active.items[size_t (ReadinessItemId::SourcesMapped)].status == ReadinessStatus::Checked);

    r.flagForReview (ReadinessChange::BroadcastRouting);
    CHECK (r.active.items[size_t (ReadinessItemId::BroadcastOutput)].needsReview);

    // Finished active is not re-flagged; history identity is preserved.
    r.finishActive();
    REQUIRE (! r.history.empty());
    const bool histReview = r.history[0].items[size_t (ReadinessItemId::SourcesMapped)].needsReview;
    r.flagForReview (ReadinessChange::BroadcastDevice);
    CHECK (r.active.finished);
    CHECK (r.history[0].items[size_t (ReadinessItemId::SourcesMapped)].needsReview == histReview);
}

TEST_CASE ("BroadcastReadiness: history filter and summary")
{
    BroadcastReadiness r;
    r.newService ("A", "Alex");
    r.setItem (ReadinessItemId::AvSync, ReadinessStatus::NeedsAttention, "Stream delayed");
    r.finishActive();
    r.newService ("B", "Blake");
    r.setItem (ReadinessItemId::AvSync, ReadinessStatus::Pending);
    r.finishActive();
    r.newService ("C", "Alex");
    r.setItem (ReadinessItemId::AvSync, ReadinessStatus::NeedsAttention, "Again");
    r.finishActive();

    const auto alex = r.filterHistory (0, 0, "Alex");
    CHECK (alex.size() == 2);
    const auto sum = r.summarise (0, 0, "Alex");
    CHECK (sum.broadcasts == 2);
    CHECK (sum.attentionCount[size_t (ReadinessItemId::AvSync)] == 2);
    CHECK (! sum.relatedNotes.empty());
}

TEST_CASE ("BroadcastReadiness: save / restore through SessionStore")
{
    BroadcastReadiness r;
    r.ensureActive();
    r.setActiveName ("Wednesday");
    r.setActiveOperator ("Casey");
    r.setItem (ReadinessItemId::StreamListened, ReadinessStatus::Checked, "OBS headphones");
    r.rememberOperator ("Casey");
    r.finishActive();
    r.newService ("Thursday", "Casey");

    SessionState state;
    state.session.name = "Test";
    state.readiness = r;

    const auto v = SessionStore::toVar (state);
    CHECK (int (v.getProperty ("version", 0)) == SessionStore::kVersion);

    SessionState back;
    REQUIRE (SessionStore::fromVar (v, back));
    CHECK (readinessEquals (r, back.readiness));
    CHECK (back.readiness.active.name == "Thursday");
    REQUIRE (back.readiness.history.size() >= 1);
    CHECK (back.readiness.history[0].name == "Wednesday");
    CHECK (back.readiness.history[0].items[size_t (ReadinessItemId::StreamListened)].note == "OBS headphones");
}

TEST_CASE ("BroadcastReadiness: MixController flags on broadcast routing change and leaves audio alone")
{
    MixController c;
    MixSession s;
    s.name = "Ready";
    InputAssignment in;
    in.name = "Pastor";
    in.role = ChannelRole::SpeechLectern;
    in.inputA = 0;
    s.inputs.push_back (in);
    c.setSession (s);

    c.editReadiness().ensureActive();
    c.editReadiness().setItem (ReadinessItemId::BroadcastOutput, ReadinessStatus::Checked);
    c.touch();

    const auto beforeMix = c.getKept();
    const auto beforeMute = beforeMix.numStrips > 0 ? beforeMix.strips[0].mute : false;
    const auto beforeFader = beforeMix.numStrips > 0 ? beforeMix.strips[0].faderDb : 0.0f;

    auto feeds = c.getOutputFeeds();
    // Move broadcast away from the default pair without touching only-monitor.
    if (feeds.count < 1) feeds = OutputFeeds::mainOnly();
    feeds.feeds[0].left = 4;
    feeds.feeds[0].right = 5;
    c.setOutputFeeds (feeds);

    CHECK (c.getReadiness().active.items[size_t (ReadinessItemId::BroadcastOutput)].needsReview);
    CHECK (c.getReadiness().active.items[size_t (ReadinessItemId::BroadcastOutput)].status
           == ReadinessStatus::Checked);

    const auto after = c.getKept();
    if (after.numStrips > 0)
    {
        CHECK (after.strips[0].mute == beforeMute);
        CHECK_NEAR (after.strips[0].faderDb, beforeFader, 1e-4f);
    }
}

TEST_CASE ("BroadcastReadiness: checklist item set does not change mix or outputs")
{
    MixController c;
    MixSession s;
    InputAssignment in;
    in.name = "Kick";
    in.role = ChannelRole::KickIn;
    in.inputA = 0;
    s.inputs.push_back (in);
    c.setSession (s);

    const auto feedsBefore = c.getOutputFeeds();
    const auto mixBefore = c.getKept();
    const auto revBefore = c.getRevision();

    c.editReadiness().ensureActive();
    c.editReadiness().setItem (ReadinessItemId::LeadVocals, ReadinessStatus::Checked);
    c.touch();

    CHECK (c.getOutputFeeds().count == feedsBefore.count);
    CHECK (c.getOutputFeeds().feeds[0].left == feedsBefore.feeds[0].left);
    CHECK (MixPlanner::countParameterChanges (mixBefore, c.getKept()) == 0);
    CHECK (c.getRevision() > revBefore);
}

TEST_CASE ("BroadcastReadiness: capture / apply session round-trip")
{
    MixController c;
    DawEngine daw (c);
    MixSession s;
    InputAssignment in;
    in.name = "Lead";
    in.role = ChannelRole::LeadVocal;
    in.inputA = 1;
    s.inputs.push_back (in);
    c.setSession (s);
    daw.setSession (s);

    c.editReadiness().ensureActive();
    c.editReadiness().setActiveName ("Sat rehearsal");
    c.editReadiness().setItem (ReadinessItemId::DynamicsNatural, ReadinessStatus::NeedsAttention, "Pumping");

    DeviceChoice devices;
    devices.consoleInput = "In";
    devices.broadcastOutput = "Out";
    auto state = captureSession (c, daw, devices, 0);
    CHECK (state.readiness.active.name == "Sat rehearsal");

    MixController fresh;
    DawEngine freshDaw (fresh);
    applySession (state, fresh, freshDaw);
    CHECK (fresh.getReadiness().active.name == "Sat rehearsal");
    CHECK (fresh.getReadiness().active.items[size_t (ReadinessItemId::DynamicsNatural)].status
           == ReadinessStatus::NeedsAttention);
    CHECK (fresh.getReadiness().active.items[size_t (ReadinessItemId::DynamicsNatural)].note == "Pumping");
}

TEST_CASE ("BroadcastReadiness: correcting a past service never touches the one in progress")
{
    // The old sheet moved a past record into `active` to edit it, and the service being checked
    // right then was gone.
    BroadcastReadiness r;
    r.ensureActive();
    r.setItem (ReadinessItemId::SourcesMapped, ReadinessStatus::Checked);
    r.finishActive();
    const auto pastId = r.active.id;
    r.newService ("Evening", "Sam");
    r.setItem (ReadinessItemId::InputLevels, ReadinessStatus::NeedsAttention);
    const auto activeId = r.active.id;

    auto* past = r.historyMutable (pastId);
    REQUIRE (past != nullptr);
    setReadinessItem (*past, ReadinessItemId::AvSync, ReadinessStatus::Checked);
    CHECK (past->items[size_t (ReadinessItemId::AvSync)].status == ReadinessStatus::Checked);
    CHECK (r.active.id == activeId);
    CHECK (r.active.name == "Evening");
    CHECK (r.active.items[size_t (ReadinessItemId::InputLevels)].status == ReadinessStatus::NeedsAttention);
    CHECK (r.active.items[size_t (ReadinessItemId::AvSync)].status == ReadinessStatus::Pending);
}

TEST_CASE ("BroadcastReadiness: a finished service is read-only until it is reopened, and Finish puts it back once")
{
    BroadcastReadiness r;
    r.ensureActive();
    r.setItem (ReadinessItemId::SpeechClear, ReadinessStatus::Checked);
    r.finishActive();
    r.setItem (ReadinessItemId::LeadVocals, ReadinessStatus::Checked);          // refused while finished
    CHECK (r.active.items[size_t (ReadinessItemId::LeadVocals)].status == ReadinessStatus::Pending);
    r.reopenActive();
    r.setItem (ReadinessItemId::LeadVocals, ReadinessStatus::Checked);
    CHECK (r.active.items[size_t (ReadinessItemId::LeadVocals)].status == ReadinessStatus::Checked);
    r.finishActive();
    REQUIRE (r.history.size() == 1);                                             // replaced, not duplicated
    CHECK (r.history[0].items[size_t (ReadinessItemId::LeadVocals)].status == ReadinessStatus::Checked);
    // A blank checklist is not a service: starting another from nothing keeps no empty record.
    r.newService();
    r.newService();
    CHECK (r.history.size() == 1);
}

TEST_CASE ("BroadcastReadiness: DINE never calls an input healthy that nobody has measured")
{
    MixSession s;
    s.name = "Hints";
    s.purpose = MixPurpose::Livestream;
    s.inputs = { { "Pastor", ChannelRole::Speech, 0, -1 }, { "Kick", ChannelRole::KickIn, 1, -1 } };
    MixController c;
    c.setSession (s);
    c.prepare (48000.0, 128);
    bool found = false;
    for (const auto& h : c.readinessHints())
    {
        if (h.item != ReadinessItemId::InputLevels) continue;
        found = true;
        CHECK (h.text.find ("Not measured") != std::string::npos);
        CHECK (h.text.find ("healthy") == std::string::npos);
    }
    CHECK (found);
    c.setStripMute (1, true);
    for (const auto& h : c.readinessHints())
        if (h.item == ReadinessItemId::NoUnintendedSoloMute)
        {
            CHECK (h.text.find ("Kick") != std::string::npos);
            CHECK (h.concerning);
        }
}
