// USAGE, STABILITY AND MILESTONES (app/native/Telemetry, docs/ANALYTICS.md). No network: the
// sender is a fake that records what it was given and answers with whatever status the test
// wants. No worker and no timer either - the tests drive tick() and flushNow() themselves.
#include "TestFramework.h"
#include "native/Telemetry.h"
#include <juce_core/juce_core.h>

using namespace livemix;

namespace
{
    juce::File scratch (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dlive-telemetry-tests").getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    struct FakeServer
    {
        juce::StringArray bodies;
        int status = 201;
    };

    Telemetry::Config config (const juce::File& folder, FakeServer* server)
    {
        Telemetry::Config c;
        c.folder = folder;
        c.appVersion = "9.9.9";
        c.background = false;
        if (server != nullptr)
        {
            c.url = "https://example.supabase.co";
            c.anonKey = "anon";
            c.send = [server] (const juce::String&, const juce::String& body, const juce::String&)
            {
                server->bodies.add (body);
                return server->status;
            };
        }
        return c;
    }

    // Every row the fake server was sent, parsed.
    juce::Array<juce::var> rowsSent (const FakeServer& s)
    {
        juce::Array<juce::var> out;
        for (const auto& b : s.bodies)
        {
            const auto parsed = juce::JSON::parse (b)["rows"];
            if (auto* arr = parsed.getArray())
                for (const auto& r : *arr) out.add (r);
        }
        return out;
    }

    int countEvent (const juce::Array<juce::var>& rows, const juce::String& event)
    {
        int n = 0;
        for (const auto& r : rows) if (r["event"].toString() == event) ++n;
        return n;
    }

    juce::var findEvent (const juce::Array<juce::var>& rows, const juce::String& event)
    {
        for (const auto& r : rows) if (r["event"].toString() == event) return r;
        return {};
    }

    void drain (Telemetry& t) { for (int i = 0; i < 20 && t.getPending() > 0; ++i) if (! t.flushNow()) break; }

    juce::NamedValueSet drumTune()
    {
        return { { "outcome", "proposal" }, { "scope", "channel" }, { "family", "kick" }, { "kind", "drums" } };
    }
}

TEST_CASE ("Telemetry: with no Supabase configured nothing is queued, and the milestones still work")
{
    const auto dir = scratch ("unconfigured");
    Telemetry t (config (dir, nullptr));
    t.start();
    CHECK (! t.isConfigured());
    int shown = 0;
    t.onMilestone = [&shown] (const Telemetry::Milestone&) { ++shown; };
    t.track ("tune_result", drumTune());
    CHECK (t.getPending() == 0);
    CHECK (t.hasMilestone ("tuned_in"));
    CHECK (t.hasMilestone ("drums_fire"));
    CHECK (! t.hasMilestone ("bass_locked"));
    CHECK (shown == 2);
    CHECK (t.hasUsed ("tune_channel"));
    t.end();
    CHECK (! dir.getChildFile ("telemetry-queue.jsonl").existsAsFile());
}

TEST_CASE ("Telemetry: a milestone is earned once, and remembered across launches")
{
    const auto dir = scratch ("milestones");
    juce::String install;
    {
        Telemetry t (config (dir, nullptr));
        t.start();
        install = t.getInstallId();
        int shown = 0;
        t.onMilestone = [&shown] (const Telemetry::Milestone&) { ++shown; };
        t.track ("tune_result", drumTune());
        t.track ("tune_result", drumTune());
        CHECK (shown == 2);                      // tuned_in and drums_fire, each once
        t.track ("tune_result", { { "outcome", "no_signal" }, { "scope", "channel" }, { "kind", "bass" } });
        CHECK (! t.hasMilestone ("bass_locked"));   // a tune that heard nothing earns nothing
        t.track ("tune_result", { { "outcome", "proposal" }, { "scope", "group" }, { "group", "BASS" } });
        CHECK (t.hasMilestone ("bass_locked"));
        t.track ("preset_saved", { { "kind", "input_map" } });
        CHECK (! t.hasMilestone ("your_sound"));    // a patch is not a sound
        t.track ("preset_saved", { { "kind", "favourite" } });
        CHECK (t.hasMilestone ("your_sound"));
        t.end();
    }
    Telemetry again (config (dir, nullptr));
    again.start();
    CHECK (again.getInstallId() == install);
    int shown = 0;
    again.onMilestone = [&shown] (const Telemetry::Milestone&) { ++shown; };
    again.track ("tune_result", drumTune());
    again.track ("preset_saved", { { "kind", "scene" } });
    CHECK (shown == 0);
    CHECK (again.hasMilestone ("drums_fire"));
    again.end();
}

TEST_CASE ("Telemetry: rows carry the install, the run and the platform, and never a path")
{
    const auto dir = scratch ("rows");
    FakeServer server;
    Telemetry t (config (dir, &server));
    t.start();
    t.track ("session_opened", { { "source", "user" }, { "inputs", 24 }, { "note", "/Users/somebody/Music/Sunday.dlive" } });
    drain (t);
    const auto rows = rowsSent (server);
    CHECK (countEvent (rows, "app_started") == 1);
    const auto row = findEvent (rows, "session_opened");
    REQUIRE (row.isObject());
    CHECK (row["install_id"].toString() == t.getInstallId());
    CHECK (row["session_id"].toString() == t.getSessionId());
    CHECK (row["app_version"].toString() == "9.9.9");
    CHECK (row["os"].toString().isNotEmpty());
    CHECK (row["arch"].toString().isNotEmpty());
    CHECK (row["event_id"].toString().length() == 36);
    CHECK ((int) row["props"]["inputs"] == 24);
    CHECK (row["props"]["note"].toString() == "redacted");
    for (const auto& b : server.bodies) CHECK (! b.contains ("/Users/"));
    t.end();
}

TEST_CASE ("Telemetry: a network that is down keeps the rows, and says so once it is back")
{
    const auto dir = scratch ("offline");
    FakeServer server;
    server.status = 0;
    Telemetry t (config (dir, &server));
    t.start();
    t.track ("autopilot", { { "state", "on" } });
    CHECK (! t.flushNow());
    CHECK (t.getPending() == 3);                // app_started, autopilot, feature_first_use
    CHECK (t.getFailedSends() == 1);
    t.writeFilesNow();
    CHECK (dir.getChildFile ("telemetry-queue.jsonl").existsAsFile());

    server.status = 201;
    server.bodies.clear();
    drain (t);
    CHECK (t.getPending() == 0);
    const auto rows = rowsSent (server);
    CHECK (countEvent (rows, "autopilot") == 1);
    const auto health = findEvent (rows, "telemetry_health");
    REQUIRE (health.isObject());
    CHECK ((int) health["props"]["failed_sends"] == 1);
    t.end();
}

TEST_CASE ("Telemetry: rows that were never sent go out on the next launch")
{
    const auto dir = scratch ("queue-survives");
    FakeServer server;
    server.status = 503;
    {
        Telemetry t (config (dir, &server));
        t.start();
        t.track ("recording_started", { { "armed", 8 } });
        t.flushNow();
        t.end();
    }
    server.status = 201;
    server.bodies.clear();
    CHECK (dir.getChildFile ("telemetry-queue.jsonl").existsAsFile());
    Telemetry next (config (dir, &server));
    next.start();
    CHECK (next.getPending() >= 4);
    drain (next);
    CHECK (countEvent (rowsSent (server), "recording_started") == 1);
    next.end();
}

TEST_CASE ("Telemetry: over the server's rate limit (429) the rows wait and back off, they are not dropped")
{
    const auto dir = scratch ("rate-limited");
    FakeServer server;
    server.status = 429;
    Telemetry t (config (dir, &server));
    t.start();
    CHECK (! t.flushNow());
    CHECK (t.getPending() == 1);
    CHECK (t.getFailedSends() == 1);
    t.end();
}

TEST_CASE ("Telemetry: a batch the server refuses outright is dropped, not retried for ever")
{
    const auto dir = scratch ("rejected");
    FakeServer server;
    server.status = 400;
    Telemetry t (config (dir, &server));
    t.start();
    t.flushNow();
    CHECK (t.getPending() == 0);
    t.end();
}

TEST_CASE ("Telemetry: sharing off sends nothing and forgets what was waiting")
{
    const auto dir = scratch ("sharing-off");
    FakeServer server;
    Telemetry t (config (dir, &server));
    t.start();
    t.setSharing (false);
    CHECK (t.getPending() == 0);
    t.track ("mix_buddy_used");
    CHECK (t.getPending() == 0);
    CHECK (! t.flushNow());
    CHECK (server.bodies.isEmpty());
    CHECK (t.hasUsed ("mix_buddy"));             // the Mac still remembers; nothing leaves it
    t.end();

    Telemetry again (config (dir, &server));
    CHECK (! again.isSharing());
}

TEST_CASE ("Telemetry: a run that never said goodbye is reported by the next one, as itself")
{
    const auto dir = scratch ("crash");
    FakeServer server;
    juce::String crashedRun;
    {
        Telemetry t (config (dir, &server));
        t.start();
        crashedRun = t.getSessionId();
        Telemetry::Probe p;
        p.audioRunning = true;
        p.sampleRate = 48000.0;
        p.bufferSize = 128;
        p.inputs = 24;
        p.deviceKind = "interface";
        p.deviceModel = "Test Interface";
        p.activity = "recording";
        p.recording = true;
        t.tick (p);
        t.writeFilesNow();
        // No end(): this is the run that died.
    }
    // What the signal handler would have left beside it.
    dir.getChildFile ("telemetry-crash.txt").replaceWithText (
        "signal=11\n"
        "0   DLIVE                               0x0000000102f8c000 onFatalSignal + 252\n"
        "1   libsystem_platform.dylib            0x000000018f5617a4 _sigtramp + 56\n"
        "0   DLIVE                               0x0000000102f8c3a4 _ZN7livemix13MixController4pollEv + 123\n"
        "1   /Users/somebody/Library/Frameworks/X.dylib 0x0000000102f8c3a5 main + 4\n");

    server.bodies.clear();
    Telemetry next (config (dir, &server));
    next.start();
    CHECK (next.previousRunEndedBadly());
    drain (next);
    const auto rows = rowsSent (server);
    const auto crash = findEvent (rows, "app_crash");
    REQUIRE (crash.isObject());
    CHECK (crash["session_id"].toString() == crashedRun);
    CHECK (crash["props"]["signal"].toString() == "SIGSEGV");
    CHECK (crash["props"]["activity"].toString() == "recording");
    CHECK ((int) crash["props"]["sample_rate"] == 48000);
    CHECK (crash["props"]["device_model"].toString() == "Test Interface");
    const auto stack = crash["props"]["stack"].toString();
    CHECK (stack.contains ("livemix::MixController::poll()"));
    CHECK (! stack.contains ("0x0000000102f8c3a4"));
    CHECK (! stack.contains ("onFatalSignal"));
    CHECK (! stack.contains ("/Users/"));
    bool saidSo = false;
    for (const auto& r : rows)
        if (r["event"].toString() == "app_started" && r["session_id"].toString() == next.getSessionId())
            saidSo = (bool) r["props"]["previous_run_crashed"];
    CHECK (saidSo);
    next.end();

    // ... and a clean goodbye leaves nothing to report.
    server.bodies.clear();
    Telemetry third (config (dir, &server));
    third.start();
    CHECK (! third.previousRunEndedBadly());
    drain (third);
    CHECK (countEvent (rowsSent (server), "app_crash") == 0);
    third.end();
}

TEST_CASE ("Telemetry: the device, a dropout, a lost device and a failed take become events")
{
    const auto dir = scratch ("probe");
    FakeServer server;
    Telemetry t (config (dir, &server));
    t.start();
    Telemetry::Probe p;
    p.audioRunning = true;
    p.sampleRate = 48000.0;
    p.bufferSize = 64;
    p.inputs = 16;
    p.deviceKind = "interface";
    p.deviceModel = "Test Interface";
    t.tick (p);
    t.tick (p);                                        // the same device again is not a new one
    p.recording = true;
    p.armed = 16;
    p.recordingSeconds = 12.0;
    t.tick (p);
    p.deviceStopped = true;
    p.audioRunning = false;
    p.recording = false;
    t.tick (p);
    p.deviceStopped = false;
    p.audioRunning = true;
    t.tick (p);
    drain (t);
    const auto rows = rowsSent (server);
    CHECK (countEvent (rows, "device_opened") == 2);   // before it went, and when it came back
    CHECK (countEvent (rows, "device_returned") == 1);
    CHECK (countEvent (rows, "recording_started") == 1);
    const auto take = findEvent (rows, "recording_completed");
    REQUIRE (take.isObject());
    CHECK (! (bool) take["props"]["ok"]);
    CHECK ((int) take["props"]["seconds"] == 12);
    int lost = 0, failedTake = 0;
    for (const auto& r : rows)
        if (r["event"].toString() == "error")
        {
            if (r["props"]["code"].toString() == "device_lost" && r["props"]["area"].toString() == "audio_device") ++lost;
            if (r["props"]["area"].toString() == "recording") ++failedTake;
            CHECK ((int) r["props"]["buffer"] == 64);        // every error carries the moment it happened in
        }
    CHECK (lost == 1);
    CHECK (failedTake == 1);
    t.end();
}

TEST_CASE ("Telemetry: an hour of mixing is an hour, whenever it was earned")
{
    const auto dir = scratch ("hours");
    // An install that is one second short of its first hour.
    dir.getChildFile ("telemetry.json").replaceWithText (
        R"({"install_id":"6f1c1e0e-7c0e-4f6b-9a33-3c2d8d1e0a11","first_seen":"2026-09-01T10:00:00.000Z","launches":3,"mixing_seconds":3599})");
    Telemetry t (config (dir, nullptr));
    t.start();
    CHECK (t.getInstallId() == "6f1c1e0e-7c0e-4f6b-9a33-3c2d8d1e0a11");
    Telemetry::Probe p;
    p.audioRunning = true;
    p.inputs = 8;
    t.tick (p);
    CHECK (! t.hasMilestone ("one_hour"));
    juce::Thread::sleep (1100);
    t.tick (p);
    CHECK (t.hasMilestone ("one_hour"));
    CHECK (! t.hasMilestone ("ten_hours"));
    // With nothing to mix, time does not count.
    const auto before = t.getMixingSeconds();
    p.inputs = 0;
    juce::Thread::sleep (300);
    t.tick (p);
    CHECK (t.getMixingSeconds() == before);
    t.end();
}

TEST_CASE ("Telemetry: a stack is frame, module and symbol - never an address or a folder")
{
    const auto s = Telemetry::sanitiseStack (
        "3   DLIVE                               0x0000000100a1b2c3 _ZN7livemix9DawEngine13stopRecordingEv + 44\n"
        "4   libsystem_c.dylib                   0x0000000190a1b2c3 abort + 180\n"
        "garbage\n");
    CHECK_MESSAGE (s.contains ("3 DLIVE livemix::DawEngine::stopRecording() + 44"), s.toStdString());
    CHECK (s.contains ("4 libsystem_c.dylib abort + 180"));
    CHECK (! s.contains ("0x"));
    CHECK (! s.contains ("garbage"));
    CHECK (Telemetry::sanitiseValue ("model", "C:\\Users\\x").toString() == "redacted");
    CHECK (Telemetry::sanitiseValue ("model", "Scarlett 18i20").toString() == "Scarlett 18i20");
    CHECK (Telemetry::sanitiseValue ("model", juce::String::repeatedString ("a", 300)).toString().length() == 80);
}
