#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace livemix
{

// ---------------------------------------------------------------------------
// USAGE, STABILITY AND MILESTONES - one service, docs/ANALYTICS.md is its contract.
//
// What people actually do with DINE, what went wrong while they did it, and the small
// "first time" moments worth a word on screen. Every event is a fixed name with a few
// fixed-vocabulary fields; no audio, no recording, no channel name, no file name, no chat
// text, no sentence the user typed ever becomes one (a string field that looks like a path
// is replaced before it is queued).
//
// NEVER ON THE AUDIO THREAD, AND NEVER IN THE WAY OF IT. track() takes a lock for as long as it
// takes to append a row; the files and the network are the worker thread's, which sleeps
// between batches. A network that is down, a Supabase that is not configured, or a user who
// has switched sharing off all mean the same thing to the rest of the app: nothing. The
// milestones are decided here from local state, so they appear the moment they are earned and
// never wait on a round-trip.
//
// A run that did not say goodbye leaves its run file behind (and, for a signal or an uncaught
// exception, a crash file with a symbolised stack). The next start() turns that into one
// app_crash event about the run that died.
// ---------------------------------------------------------------------------
class Telemetry : private juce::Thread,
                  private juce::Timer
{
public:
    struct Config
    {
        juce::String url;              // https://<project>.supabase.co ; empty = nothing leaves the Mac
        juce::String anonKey;          // the project's anon key: may insert events, may read nothing
        juce::File folder;             // install ID, milestones, the queue, the run and crash files
        juce::String appVersion;
        // One batch out: the HTTP status, 0 for a transport failure. Worker thread. The
        // default is an HTTPS POST to PostgREST; the tests put a fake here.
        std::function<int (const juce::String& url, const juce::String& body, const juce::String& key)> send;
        bool background = true;        // false: no worker, no timer - the tests drive flushNow() and tick()
    };

    // What the app is doing, read once a second on the message thread. Numbers and fixed
    // words only; the device model is the maker's, and only for hardware.
    struct Probe
    {
        bool audioRunning = false;
        bool deviceStopped = false;    // the device went away without the app closing it
        double sampleRate = 0.0;
        int bufferSize = 0;
        int xruns = 0;                 // cumulative, as the device reports it
        double cpu = -1.0;             // 0..1 of the callback's budget, -1 unknown
        int deviceInputs = 0, deviceOutputs = 0;
        int inputs = 0;                // the session's channels
        int tracks = 0;
        juce::String deviceKind;       // interface / builtin / bluetooth / display / virtual / aggregate
        juce::String deviceModel;      // empty unless the kind is real hardware
        bool recording = false;
        int armed = 0;
        double recordingSeconds = 0.0;
        juce::String recordError;      // Recorder::getErrorCode()
        juce::String activity { "idle" };   // idle / mixing / tuning / tune_live / autopilot / recording
    };

    struct Milestone { const char* id; const char* title; const char* sentence; };
    static const std::vector<Milestone>& milestones();

    explicit Telemetry (Config);
    ~Telemetry() override;

    // Launch. Reads what the last run left behind and reports it, counts this launch, arms the
    // crash file and sends app_started. Message thread, once.
    void start();
    // A clean goodbye: app_ended, the queue on disk, the run file gone, one last short try to
    // send. Never takes more than about two seconds.
    void end();

    // Any thread but the audio thread. `props`: fixed words and numbers.
    void track (const juce::String& event, const juce::NamedValueSet& props = {});
    // A failure somewhere in the product: area (audio_device, recording, session, tune, ...),
    // a fixed code, whether DINE carried on by itself, and the context of the moment.
    void error (const juce::String& area, const juce::String& code, bool recovered,
                const juce::NamedValueSet& props = {});
    // Message thread, once a second (the timer calls it with probe() when there is one).
    void tick (const Probe&);

    std::function<Probe()> probe;
    std::function<void (const Milestone&)> onMilestone;      // message thread

    bool isConfigured() const noexcept { return config.url.isNotEmpty() && config.anonKey.isNotEmpty(); }
    bool isSharing() const;
    void setSharing (bool on);      // off also empties the queue: nothing already waiting is sent
    // TOLD BEFORE ANYTHING LEAVES. Sharing is on by default, so the first run says so - what
    // is sent, what never is, and where to switch it off - and the worker sends nothing until
    // it has been said. True until markNoticeShown(); kept in telemetry.json.
    bool needsNotice() const;
    void markNoticeShown();
    juce::String getInstallId() const;
    juce::String getSessionId() const { return sessionId; }
    bool previousRunEndedBadly() const noexcept { return previousCrashed; }
    bool hasMilestone (const juce::String& id) const;
    bool hasUsed (const juce::String& feature) const;
    double getMixingSeconds() const;
    int getPending() const;
    int getFailedSends() const;

    // One batch now, on the calling thread. Used by the tests; returns true when it was sent.
    bool flushNow();
    // Read the files back, for the tests.
    void writeFilesNow();

    // A backtrace as it goes into a report: frame, module and demangled symbol, nothing else.
    static juce::String sanitiseStack (const juce::String& raw);
    // A string field as it goes into a report: printable, short, and never a path.
    static juce::var sanitiseValue (const juce::Identifier& key, const juce::var& v);

    static Telemetry* instance() noexcept;

private:
    struct Row { juce::int64 seq = 0; juce::String json; };

    void run() override;
    void timerCallback() override;

    juce::String makeRow (const juce::String& event, const juce::NamedValueSet& props,
                          const juce::String& session, const juce::String& version) const;
    void enqueue (juce::String json);
    void learn (const juce::String& event, const juce::NamedValueSet& props);
    void unlock (const juce::String& id);
    void firstUse (const juce::String& feature);
    juce::NamedValueSet context() const;
    void readPreviousRun();
    int sendBatch();                 // rows sent, -1 on failure
    void writeFiles();
    juce::String stateJson() const;
    juce::String runJson() const;

    juce::File stateFile() const  { return config.folder.getChildFile ("telemetry.json"); }
    juce::File queueFile() const  { return config.folder.getChildFile ("telemetry-queue.jsonl"); }
    juce::File runFile() const    { return config.folder.getChildFile ("telemetry-run.json"); }
    juce::File crashFile() const  { return config.folder.getChildFile ("telemetry-crash.txt"); }

    Config config;
    const juce::String sessionId;
    const juce::Time startedAt;

    mutable juce::CriticalSection lock;
    // ---- under the lock ----
    std::deque<Row> queue;
    juce::int64 nextSeq = 1;
    bool queueDirty = false, stateDirty = false, runDirty = false;
    juce::String installId;
    juce::Time firstSeen;
    int launches = 0;
    bool sharing = true;
    bool noticeShown = false;
    double mixingSeconds = 0.0;     // every run, the whole life of the install
    std::map<juce::String, juce::String> milestonesGot, featuresUsed;
    int failedSends = 0, droppedRows = 0;
    juce::String lastContext;        // the run file's body, re-written when it changes
    // ---- worker ----
    std::atomic<bool> stopping { false };
    juce::uint32 nextAttemptMs = 0;
    int backoffMs = 0;
    juce::uint32 oldestQueuedMs = 0;
    std::atomic<bool> flushWanted { false };

    // ---- message thread ----
    Probe last;
    bool haveLast = false;
    bool previousCrashed = false;
    double runMixingSeconds = 0.0;
    int runXruns = 0, windowXruns = 0, lastXruns = 0;
    double cpuPeak = 0.0;
    int cpuHotTicks = 0;
    juce::uint32 lastTickMs = 0, windowStartMs = 0, lastCpuReportMs = 0, lastHeartbeatMs = 0;
    juce::String deviceKey;
    juce::uint32 lostAtMs = 0;
    bool reportedMemory = false;
    bool started = false, ended = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Telemetry)
};

// The helper the rest of the app calls. Goes nowhere when no service is installed (the
// snapshot tool, the tests), so a call site never has to ask.
void trackEvent (const juce::String& event, const juce::NamedValueSet& props = {});
void trackError (const juce::String& area, const juce::String& code, bool recovered,
                 const juce::NamedValueSet& props = {});

} // namespace livemix
