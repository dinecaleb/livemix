#include "Telemetry.h"
#include "Intelligence/HttpJson.h"
#include <cmath>
#include <csignal>
#include <cxxabi.h>
#include <exception>
#include <typeinfo>

#if JUCE_MAC || JUCE_LINUX
 #include <execinfo.h>
 #include <fcntl.h>
 #include <unistd.h>
#endif
#if JUCE_MAC
 #include <mach/mach.h>
 #include <sys/sysctl.h>
#endif

namespace livemix
{

namespace
{
    constexpr int kMaxQueued = 2000;           // about a week of heavy use; older rows go first
    constexpr int kBatch = 50;
    constexpr juce::uint32 kSendAfterMs = 20000;   // a row waits at most this long for company
    constexpr int kFirstBackoffMs = 30000;
    constexpr int kMaxBackoffMs = 15 * 60 * 1000;
    constexpr juce::uint32 kHeartbeatMs = 5 * 60 * 1000;
    constexpr juce::uint32 kDropoutWindowMs = 60 * 1000;
    constexpr juce::uint32 kCpuReportEveryMs = 10 * 60 * 1000;
    constexpr double kCpuHot = 0.85;
    constexpr int kCpuHotTicks = 5;
    constexpr double kMemoryHighMb = 3072.0;

    std::atomic<Telemetry*> installed { nullptr };

    juce::String osVersion()
    {
       #if JUCE_MAC
        // "Mac OSX 15.4" is what JUCE says; the number is the part a chart groups by.
        return juce::SystemStats::getOperatingSystemName().fromLastOccurrenceOf (" ", false, false);
       #else
        return juce::SystemStats::getOperatingSystemName();
       #endif
    }

    juce::String osName()
    {
       #if JUCE_MAC
        return "macOS";
       #elif JUCE_WINDOWS
        return "Windows";
       #else
        return "Linux";
       #endif
    }

    juce::String architecture()
    {
       #if defined (__aarch64__) || defined (__arm64__)
        return "arm64";
       #else
        #if JUCE_MAC
        int translated = 0;
        size_t size = sizeof (translated);
        if (sysctlbyname ("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0 && translated == 1)
            return "x86_64-rosetta";
        #endif
        return "x86_64";
       #endif
    }

    double residentMb()
    {
       #if JUCE_MAC
        mach_task_basic_info info {};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) == KERN_SUCCESS)
            return double (info.resident_size) / (1024.0 * 1024.0);
       #endif
        return -1.0;
    }

    int defaultSend (const juce::String& url, const juce::String& body, const juce::String& key,
                     const std::atomic<bool>* cancel)
    {
        // One call to public.ingest_events: it inserts the batch and skips an event_id that is
        // already there (a retry after a timeout that had in fact landed). The anon key can
        // call it and cannot touch the table (supabase/migrations/*_ingest.sql).
        const auto r = http::postJson (url.trimCharactersAtEnd ("/") + "/rest/v1/rpc/ingest_events",
                                       body, key, 15, cancel, "apikey: " + key + "\r\n");
        return r.error.isNotEmpty() ? 0 : r.status;
    }

    juce::String demangle (const juce::String& symbol)
    {
        int status = 0;
        char* out = abi::__cxa_demangle (symbol.toRawUTF8(), nullptr, nullptr, &status);
        if (out == nullptr) return symbol;
        juce::String s (out);
        std::free (out);
        return s;
    }

    juce::String featureFor (const juce::String& event, const juce::NamedValueSet& p)
    {
        const auto word = [&p] (const char* k) { return p.getWithDefault (k, {}).toString(); };
        if (event == "tune_result")
            return word ("outcome") == "proposal" || word ("outcome") == "no_change" ? "tune_" + word ("scope") : juce::String();
        if (event == "tune_decision")         return word ("decision") == "kept_some" ? "keep_some" : juce::String();
        if (event == "autopilot")             return word ("state") == "on" ? "autopilot" : juce::String();
        if (event == "sample_replacement_on") return "sample_replacement";
        if (event == "preset_saved")          return "preset_" + word ("kind");
        if (event == "preset_applied")        return "preset_recall";
        if (event == "mix_buddy_used")        return "mix_buddy";
        if (event == "recording_started")     return "recording";
        if (event == "live_view_opened")      return "live_view";
        if (event == "live_safe")             return (bool) p.getWithDefault ("on", false) ? "live_safe" : juce::String();
        if (event == "speech_priority")       return (bool) p.getWithDefault ("on", false) ? "speech_priority" : juce::String();
        if (event == "auto_mix")              return (bool) p.getWithDefault ("on", false) ? "auto_mix" : juce::String();
        return {};
    }

    // ---- the crash file: written from a signal handler, so nothing here may allocate, lock
    // or call anything that is not async-signal-safe. The descriptor is opened at start().
    int crashFd = -1;

    void writeRaw (const char* s)
    {
        if (crashFd < 0 || s == nullptr) return;
        size_t n = 0;
        while (s[n] != 0) ++n;
        [[maybe_unused]] const auto written = ::write (crashFd, s, n);
    }

    void writeInt (int v)
    {
        char buf[16];
        int i = 15;
        buf[i] = 0;
        const bool negative = v < 0;
        unsigned int u = negative ? unsigned (-v) : unsigned (v);
        do { buf[--i] = char ('0' + u % 10); u /= 10; } while (u != 0 && i > 1);
        if (negative) buf[--i] = '-';
        writeRaw (buf + i);
    }

    extern "C" void onFatalSignal (int sig)
    {
       #if JUCE_MAC || JUCE_LINUX
        if (crashFd >= 0)
        {
            // backtrace_symbols_fd can wait on the loader's lock, which the crash may be holding.
            // A hung handler is a frozen app on a service screen instead of a crash macOS reports
            // and the next launch recovers from - so the handler gets two seconds, then SIGALRM's
            // default action ends the process anyway. alarm() is async-signal-safe.
            ::alarm (2);
            writeRaw ("signal=");
            writeInt (sig);
            writeRaw ("\n");
            void* frames[64];
            const int n = backtrace (frames, 64);
            backtrace_symbols_fd (frames, n, crashFd);
            ::fsync (crashFd);
        }
       #endif
        // Handed back to the system as it was, so macOS still writes its own crash report.
        ::signal (sig, SIG_DFL);
        ::raise (sig);
    }

    std::terminate_handler previousTerminate = nullptr;

    void onTerminate()
    {
        // The type, never what(): an exception's message can carry a file name or anything else.
        if (const auto* type = abi::__cxa_current_exception_type())
        {
            writeRaw ("exception=");
            writeRaw (type->name());
            writeRaw ("\n");
        }
        else writeRaw ("exception=terminate\n");
        if (previousTerminate != nullptr) previousTerminate();
        std::abort();
    }

    void armCrashFile (const juce::File& file)
    {
       #if JUCE_MAC || JUCE_LINUX
        if (crashFd >= 0) return;
        crashFd = ::open (file.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (crashFd < 0) return;
        void* warm[4];
        backtrace (warm, 4);                  // the first call loads what it needs; not inside a crash
        for (const int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS })
        {
            struct sigaction sa {};
            sa.sa_handler = onFatalSignal;
            sigemptyset (&sa.sa_mask);
            sa.sa_flags = SA_RESETHAND | SA_NODEFER;
            sigaction (sig, &sa, nullptr);
        }
        previousTerminate = std::set_terminate (onTerminate);
       #else
        juce::ignoreUnused (file);
       #endif
    }

    juce::String signalName (int sig)
    {
        switch (sig)
        {
            case SIGSEGV: return "SIGSEGV";
            case SIGBUS:  return "SIGBUS";
            case SIGILL:  return "SIGILL";
            case SIGFPE:  return "SIGFPE";
            case SIGABRT: return "SIGABRT";
           #ifdef SIGSYS
            case SIGSYS:  return "SIGSYS";
           #endif
            default:      return "SIG" + juce::String (sig);
        }
    }
}

// ---------------------------------------------------------------------------

const std::vector<Telemetry::Milestone>& Telemetry::milestones()
{
    static const std::vector<Milestone> all {
        { "tuned_in",      "You're tuned in",         "Your first channel tune." },
        { "drums_fire",    "Drums sounding fire",     "Your first drum tune." },
        { "bass_locked",   "Bass locked in",          "Your first bass tune." },
        { "your_sound",    "That's your sound",       "Your first saved mix." },
        { "one_hour",      "One hour live",           "An hour of mixing with DLIVE." },
        { "ten_hours",     "Ten hours behind the mix", "Ten hours of mixing with DLIVE." },
    };
    return all;
}

Telemetry* Telemetry::instance() noexcept { return installed.load(); }

void trackEvent (const juce::String& event, const juce::NamedValueSet& props)
{
    if (auto* t = Telemetry::instance()) t->track (event, props);
}

void trackError (const juce::String& area, const juce::String& code, bool recovered, const juce::NamedValueSet& props)
{
    if (auto* t = Telemetry::instance()) t->error (area, code, recovered, props);
}

Telemetry::Telemetry (Config c)
    : juce::Thread ("DLIVE telemetry"),
      config (std::move (c)),
      sessionId (juce::Uuid().toDashedString()),
      startedAt (juce::Time::getCurrentTime())
{
    config.folder.createDirectory();

    // The install: who this is, anonymously, and what it has already done. Read here so a
    // milestone never has to wait for anything.
    const auto v = juce::JSON::parse (stateFile());
    if (auto* o = v.getDynamicObject())
    {
        installId = o->getProperty ("install_id").toString();
        firstSeen = juce::Time::fromISO8601 (o->getProperty ("first_seen").toString());
        launches = (int) o->getProperty ("launches");
        sharing = o->hasProperty ("sharing") ? (bool) o->getProperty ("sharing") : true;
        noticeShown = (bool) o->getProperty ("notice_shown");
        mixingSeconds = (double) o->getProperty ("mixing_seconds");
        failedSends = (int) o->getProperty ("failed_sends");
        droppedRows = (int) o->getProperty ("dropped_rows");
        for (const auto* key : { "milestones", "features" })
            if (auto* m = o->getProperty (key).getDynamicObject())
                for (const auto& p : m->getProperties())
                    (juce::String (key) == "milestones" ? milestonesGot : featuresUsed)[p.name.toString()] = p.value.toString();
    }
    if (installId.isEmpty() || ! juce::Uuid (installId).toDashedString().equalsIgnoreCase (installId))
    {
        installId = juce::Uuid().toDashedString();
        firstSeen = startedAt;
        launches = 0;
        stateDirty = true;
    }
}

Telemetry::~Telemetry()
{
    if (installed.load() == this) installed.store (nullptr);
    stopTimer();
    stopping = true;
    stopThread (3000);
    writeFiles();
}

bool Telemetry::isSharing() const { const juce::ScopedLock sl (lock); return sharing; }
bool Telemetry::needsNotice() const { const juce::ScopedLock sl (lock); return isConfigured() && ! noticeShown; }
void Telemetry::markNoticeShown()
{
    const juce::ScopedLock sl (lock);
    if (noticeShown) return;
    noticeShown = true;
    stateDirty = true;
}
juce::String Telemetry::getInstallId() const { const juce::ScopedLock sl (lock); return installId; }
bool Telemetry::hasMilestone (const juce::String& id) const { const juce::ScopedLock sl (lock); return milestonesGot.count (id) > 0; }
bool Telemetry::hasUsed (const juce::String& f) const { const juce::ScopedLock sl (lock); return featuresUsed.count (f) > 0; }
double Telemetry::getMixingSeconds() const { const juce::ScopedLock sl (lock); return mixingSeconds; }
int Telemetry::getPending() const { const juce::ScopedLock sl (lock); return int (queue.size()); }
int Telemetry::getFailedSends() const { const juce::ScopedLock sl (lock); return failedSends; }

void Telemetry::setSharing (bool on)
{
    {
        const juce::ScopedLock sl (lock);
        if (sharing == on) return;
        sharing = on;
        if (! on) { queue.clear(); queueDirty = true; }
        stateDirty = true;
    }
    if (config.background) notify();
}

// ---------------------------------------------------------------------------
// Launch and goodbye

void Telemetry::start()
{
    if (started) return;
    started = true;
    readPreviousRun();

    int launchNumber = 0;
    bool firstRun = false;
    double days = 0.0;
    {
        const juce::ScopedLock sl (lock);
        firstRun = launches == 0;
        launchNumber = ++launches;
        days = (startedAt - firstSeen).inDays();
        stateDirty = true;
        runDirty = true;
        lastContext = runJson();
    }
    // The signal handlers are the process's, so only the real application installs them.
    if (config.background) armCrashFile (crashFile());
    installed.store (this);

    track ("app_started", { { "first_run", firstRun },
                            { "launch", launchNumber },
                            { "days_since_install", juce::roundToInt (days) },
                            { "previous_run_crashed", previousCrashed },
                            { "mac_model", juce::SystemStats::getDeviceDescription() },
                            { "cpu_cores", juce::SystemStats::getNumPhysicalCpus() },
                            { "ram_gb", juce::roundToInt (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0) } });

    if (config.background)
    {
        startThread (juce::Thread::Priority::background);
        startTimer (1000);
    }
    else writeFiles();
}

void Telemetry::end()
{
    if (! started || ended) return;
    ended = true;
    stopTimer();
    track ("app_ended", { { "uptime_s", juce::roundToInt ((juce::Time::getCurrentTime() - startedAt).inSeconds()) },
                          { "mixing_s", juce::roundToInt (runMixingSeconds) },
                          { "xruns", runXruns },
                          { "cpu_peak", juce::roundToInt (cpuPeak * 100.0) },
                          { "rss_mb", juce::roundToInt (residentMb()) } });

    // The goodbye: without a run file, the next launch knows this one was not a crash.
    {
        const juce::ScopedLock sl (lock);
        runDirty = false;
    }
    if (config.background)
    {
        flushWanted = true;
        notify();
        // One short chance to send; whatever is left is on disk and goes next launch.
        const int failedBefore = getFailedSends();
        const auto until = juce::Time::getMillisecondCounter() + 1500;
        while (juce::Time::getMillisecondCounter() < until && getPending() > 0 && getFailedSends() == failedBefore
               && isConfigured() && isSharing() && isThreadRunning())
            juce::Thread::sleep (20);
        stopping = true;
        http::abort (&stopping);           // a send reading its reply stops now, not at its timeout
        stopThread (2500);
    }
    writeFiles();
    runFile().deleteFile();
   #if JUCE_MAC || JUCE_LINUX
    if (crashFd >= 0) { ::close (crashFd); crashFd = -1; }
   #endif
    crashFile().deleteFile();
    if (installed.load() == this) installed.store (nullptr);
}

void Telemetry::readPreviousRun()
{
    // The queue that did not get sent last time goes first.
    if (queueFile().existsAsFile())
    {
        juce::StringArray lines;
        lines.addLines (queueFile().loadFileAsString());
        const juce::ScopedLock sl (lock);
        if (sharing)
            for (const auto& l : lines)
                if (l.trim().isNotEmpty()) queue.push_back ({ nextSeq++, l.trim() });
        while (int (queue.size()) > kMaxQueued) { queue.pop_front(); ++droppedRows; }
        queueDirty = true;
    }

    const auto run = juce::JSON::parse (runFile());
    const auto crashText = crashFile().loadFileAsString();
    runFile().deleteFile();
    crashFile().deleteFile();

    auto* prev = run.getDynamicObject();
    if (prev == nullptr && crashText.trim().isEmpty()) return;
    previousCrashed = true;

    // What the run was doing when it went, from its own last word.
    juce::NamedValueSet p;
    if (prev != nullptr)
        for (const auto& kv : prev->getProperties())
            if (kv.name.toString() != "session_id" && kv.name.toString() != "app_version")
                p.set (kv.name, kv.value);

    int sig = 0;
    juce::String exceptionType, stackLines;
    juce::StringArray lines;
    lines.addLines (crashText);
    for (const auto& l : lines)
    {
        if (l.startsWith ("signal="))         sig = l.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        else if (l.startsWith ("exception=")) exceptionType = demangle (l.fromFirstOccurrenceOf ("=", false, false).trim());
        else if (l.trim().isNotEmpty())       stackLines << l << "\n";
    }
    p.set ("detected", sig != 0 || exceptionType.isNotEmpty() ? "signal" : "no_goodbye");
    if (sig != 0) p.set ("signal", signalName (sig));
    if (exceptionType.isNotEmpty()) p.set ("exception_type", exceptionType);
    if (stackLines.isNotEmpty()) p.set ("stack", sanitiseStack (stackLines));

    // It belongs to the run that died: its session, its version.
    const auto session = prev != nullptr ? prev->getProperty ("session_id").toString() : juce::String();
    const auto version = prev != nullptr ? prev->getProperty ("app_version").toString() : config.appVersion;
    const juce::ScopedLock sl (lock);
    if (sharing && isConfigured())
    {
        queue.push_back ({ nextSeq++, makeRow ("app_crash", p, session.isNotEmpty() ? session : juce::Uuid().toDashedString(),
                                               version.isNotEmpty() ? version : config.appVersion) });
        queueDirty = true;
    }
}

// ---------------------------------------------------------------------------
// Events

juce::var Telemetry::sanitiseValue (const juce::Identifier& key, const juce::var& v)
{
    if (v.isBool() || v.isInt() || v.isInt64()) return v;
    if (v.isDouble()) return std::isfinite ((double) v) ? juce::var (std::round ((double) v * 1000.0) / 1000.0) : juce::var();
    if (! v.isString()) return {};
    const auto s = v.toString();
    const int limit = key.toString() == "stack" ? 6000 : 80;
    // A path is somebody's folder names; nothing in the schema is one.
    if (key.toString() != "stack" && (s.containsChar ('/') || s.containsChar ('\\') || s.contains ("~")))
        return "redacted";
    juce::String out;
    for (auto c : s)
        if (c == '\n' || (c >= 32 && c < 127)) out += c;
    return out.substring (0, limit);
}

juce::String Telemetry::sanitiseStack (const juce::String& raw)
{
    // "12  DLIVE   0x0000000102f8c3a4 _ZN7livemix13MixController4pollEv + 123"
    //  -> "12 DLIVE livemix::MixController::poll() + 123". The address changes with every
    // launch and says nothing without the slide; the module and the symbol are the report.
    juce::StringArray out;
    juce::StringArray lines;
    lines.addLines (raw);
    // The handler that wrote the file, and the kernel's trampoline into it, are not the crash.
    for (int i = lines.size(); --i >= 0;)
        if (lines[i].contains ("_sigtramp")) { lines.removeRange (0, i + 1); break; }
    for (const auto& l : lines)
    {
        juce::StringArray t;
        t.addTokens (l.trim(), " \t", {});
        t.removeEmptyStrings();
        if (t.size() < 4) continue;
        const auto frame = t[0];
        auto module = t[1];
        if (module.containsChar ('/')) module = module.fromLastOccurrenceOf ("/", false, false);
        juce::String symbol;
        for (int i = 3; i < t.size(); ++i) symbol << (i > 3 ? " " : "") << t[i];
        auto name = symbol.upToFirstOccurrenceOf (" + ", false, false);
        const auto offset = symbol.fromFirstOccurrenceOf (" + ", true, false);
        name = demangle (name);
        if (name.containsChar ('/')) name = "redacted";
        out.add ((frame + " " + module + " " + name + offset).substring (0, 240));
        if (out.size() >= 48) break;
    }
    return out.joinIntoString ("\n");
}

juce::String Telemetry::makeRow (const juce::String& event, const juce::NamedValueSet& props,
                                 const juce::String& session, const juce::String& version) const
{
    auto* p = new juce::DynamicObject();
    for (const auto& kv : props)
    {
        const auto clean = sanitiseValue (kv.name, kv.value);
        if (! clean.isVoid()) p->setProperty (kv.name, clean);
    }
    auto* row = new juce::DynamicObject();
    row->setProperty ("event_id", juce::Uuid().toDashedString());
    row->setProperty ("install_id", installId);
    row->setProperty ("session_id", session);
    row->setProperty ("event", event.substring (0, 64));
    row->setProperty ("ts", juce::Time::getCurrentTime().toISO8601 (true));
    row->setProperty ("app_version", version);
    row->setProperty ("os", osName());
    row->setProperty ("os_version", osVersion());
    row->setProperty ("arch", architecture());
    row->setProperty ("props", juce::var (p));
    return juce::JSON::toString (juce::var (row), true);
}

void Telemetry::enqueue (juce::String json)
{
    bool wake = false;
    {
        const juce::ScopedLock sl (lock);
        if (! sharing || ! isConfigured()) return;
        if (queue.empty()) oldestQueuedMs = juce::Time::getMillisecondCounter();
        queue.push_back ({ nextSeq++, std::move (json) });
        while (int (queue.size()) > kMaxQueued) { queue.pop_front(); ++droppedRows; stateDirty = true; }
        queueDirty = true;
        wake = int (queue.size()) >= kBatch;
    }
    if (wake && config.background) notify();
}

void Telemetry::track (const juce::String& event, const juce::NamedValueSet& props)
{
    juce::String row;
    {
        const juce::ScopedLock sl (lock);
        if (sharing && isConfigured()) row = makeRow (event, props, sessionId, config.appVersion);
    }
    if (row.isNotEmpty()) enqueue (std::move (row));
    // The milestones and the first uses are decided whether or not anything is shared: they
    // are the user's, and they live on this Mac.
    learn (event, props);
}

void Telemetry::error (const juce::String& area, const juce::String& code, bool recovered,
                       const juce::NamedValueSet& props)
{
    auto p = context();
    for (const auto& kv : props) p.set (kv.name, kv.value);
    p.set ("area", area);
    p.set ("code", code);
    p.set ("recovered", recovered);
    track ("error", p);
}

juce::NamedValueSet Telemetry::context() const
{
    juce::NamedValueSet p;
    if (! haveLast) return p;
    p.set ("activity", last.activity);
    p.set ("audio_running", last.audioRunning);
    if (last.deviceKind.isNotEmpty()) p.set ("device_kind", last.deviceKind);
    if (last.deviceModel.isNotEmpty()) p.set ("device_model", last.deviceModel);
    if (last.sampleRate > 0) p.set ("sample_rate", juce::roundToInt (last.sampleRate));
    if (last.bufferSize > 0) p.set ("buffer", last.bufferSize);
    p.set ("inputs", last.inputs);
    p.set ("recording", last.recording);
    return p;
}

void Telemetry::learn (const juce::String& event, const juce::NamedValueSet& props)
{
    if (event == "feature_first_use" || event == "milestone_unlocked") return;
    if (const auto f = featureFor (event, props); f.isNotEmpty()) firstUse (f);

    const auto word = [&props] (const char* k) { return props.getWithDefault (k, {}).toString(); };
    if (event == "tune_result" && (word ("outcome") == "proposal" || word ("outcome") == "no_change"))
    {
        const auto scope = word ("scope");
        if (scope == "channel") unlock ("tuned_in");
        if ((scope == "channel" && word ("kind") == "drums") || (scope == "group" && word ("group") == "DRUMS")) unlock ("drums_fire");
        if ((scope == "channel" && word ("kind") == "bass") || (scope == "group" && word ("group") == "BASS")) unlock ("bass_locked");
    }
    if (event == "preset_saved" && (word ("kind") == "scene" || word ("kind") == "favourite")) unlock ("your_sound");
}

void Telemetry::firstUse (const juce::String& feature)
{
    double days = 0.0;
    int launch = 0;
    {
        const juce::ScopedLock sl (lock);
        if (featuresUsed.count (feature) > 0) return;
        featuresUsed[feature] = juce::Time::getCurrentTime().toISO8601 (true);
        stateDirty = true;
        days = (juce::Time::getCurrentTime() - firstSeen).inDays();
        launch = launches;
    }
    track ("feature_first_use", { { "feature", feature }, { "days_since_install", juce::roundToInt (days) },
                                  { "launch", launch } });
}

void Telemetry::unlock (const juce::String& id)
{
    const Milestone* m = nullptr;
    for (const auto& candidate : milestones())
        if (id == candidate.id) m = &candidate;
    if (m == nullptr) return;
    double hours = 0.0;
    {
        const juce::ScopedLock sl (lock);
        if (milestonesGot.count (id) > 0) return;
        milestonesGot[id] = juce::Time::getCurrentTime().toISO8601 (true);
        stateDirty = true;
        hours = mixingSeconds / 3600.0;
    }
    track ("milestone_unlocked", { { "milestone", id }, { "mixing_hours", std::round (hours * 10.0) / 10.0 } });
    if (onMilestone) onMilestone (*m);
}

// ---------------------------------------------------------------------------
// Once a second, on the message thread

void Telemetry::timerCallback()
{
    if (probe) tick (probe());
}

void Telemetry::tick (const Probe& now)
{
    const auto ms = juce::Time::getMillisecondCounter();
    // A sleeping Mac is not a mix: a gap longer than a few seconds counts as nothing.
    const double dt = lastTickMs == 0 ? 0.0 : juce::jlimit (0.0, 5.0, (ms - lastTickMs) / 1000.0);
    lastTickMs = ms;
    if (windowStartMs == 0) windowStartMs = ms;
    if (lastHeartbeatMs == 0) lastHeartbeatMs = ms;

    const Probe was = last;
    const bool hadLast = haveLast;
    last = now;
    haveLast = true;

    // ---- mixing time, and the milestones it earns ----
    if (now.audioRunning && now.inputs > 0 && dt > 0.0)
    {
        runMixingSeconds += dt;
        double total = 0.0;
        {
            const juce::ScopedLock sl (lock);
            mixingSeconds += dt;
            total = mixingSeconds;
            stateDirty = true;
        }
        if (total >= 3600.0) unlock ("one_hour");
        if (total >= 36000.0) unlock ("ten_hours");
    }

    // ---- the device ----
    if (now.audioRunning)
    {
        const auto key = now.deviceModel + "|" + now.deviceKind + "|" + juce::String (juce::roundToInt (now.sampleRate))
                       + "|" + juce::String (now.bufferSize) + "|" + juce::String (now.deviceInputs);
        if (lostAtMs != 0)
        {
            track ("device_returned", { { "down_s", juce::roundToInt ((ms - lostAtMs) / 1000.0) },
                                        { "device_kind", now.deviceKind } });
            lostAtMs = 0;
        }
        if (key != deviceKey)
        {
            deviceKey = key;
            track ("device_opened", { { "device_kind", now.deviceKind },
                                      { "device_model", now.deviceModel },
                                      { "sample_rate", juce::roundToInt (now.sampleRate) },
                                      { "buffer", now.bufferSize },
                                      { "device_inputs", now.deviceInputs },
                                      { "device_outputs", now.deviceOutputs },
                                      { "inputs", now.inputs } });
        }
    }
    else deviceKey.clear();
    if (hadLast && now.deviceStopped && ! was.deviceStopped)
    {
        lostAtMs = ms == 0 ? 1 : ms;
        error ("audio_device", "device_lost", false, { { "was_recording", was.recording } });
    }

    // ---- dropouts: counted, and reported at most once a minute ----
    if (now.xruns < lastXruns) lastXruns = 0;           // a reopened device starts again at zero
    if (now.audioRunning && hadLast && was.audioRunning)
    {
        const int d = now.xruns - lastXruns;
        runXruns += juce::jmax (0, d);
        windowXruns += juce::jmax (0, d);
    }
    lastXruns = now.xruns;
    if (ms - windowStartMs >= kDropoutWindowMs)
    {
        if (windowXruns > 0)
            track ("audio_dropouts", { { "count", windowXruns },
                                       { "window_s", juce::roundToInt ((ms - windowStartMs) / 1000.0) },
                                       { "sample_rate", juce::roundToInt (now.sampleRate) },
                                       { "buffer", now.bufferSize },
                                       { "cpu", juce::roundToInt (now.cpu * 100.0) },
                                       { "inputs", now.inputs },
                                       { "activity", now.activity } });
        windowXruns = 0;
        windowStartMs = ms;
    }

    // ---- CPU and memory ----
    if (now.cpu > cpuPeak) cpuPeak = now.cpu;
    cpuHotTicks = now.cpu >= kCpuHot ? cpuHotTicks + 1 : 0;
    if (cpuHotTicks == kCpuHotTicks && (lastCpuReportMs == 0 || ms - lastCpuReportMs >= kCpuReportEveryMs))
    {
        lastCpuReportMs = ms;
        track ("cpu_high", { { "cpu", juce::roundToInt (now.cpu * 100.0) },
                             { "sample_rate", juce::roundToInt (now.sampleRate) },
                             { "buffer", now.bufferSize },
                             { "inputs", now.inputs },
                             { "activity", now.activity } });
    }

    // ---- recording ----
    if (hadLast && now.recording && ! was.recording)
        track ("recording_started", { { "armed", now.armed }, { "inputs", now.inputs },
                                      { "sample_rate", juce::roundToInt (now.sampleRate) } });
    if (hadLast && ! now.recording && was.recording)
    {
        const bool failed = now.recordError.isNotEmpty() || now.deviceStopped;
        track ("recording_completed", { { "seconds", juce::roundToInt (was.recordingSeconds) },
                                        { "armed", was.armed },
                                        { "ok", ! failed } });
        if (failed)
            error ("recording", now.recordError.isNotEmpty() ? now.recordError : juce::String ("device_lost"), false,
                   { { "seconds", juce::roundToInt (was.recordingSeconds) } });
    }

    // ---- the heartbeat: a run that dies still has a length ----
    if (ms - lastHeartbeatMs >= kHeartbeatMs)
    {
        lastHeartbeatMs = ms;
        const double rss = residentMb();
        track ("session_heartbeat", { { "uptime_s", juce::roundToInt ((juce::Time::getCurrentTime() - startedAt).inSeconds()) },
                                      { "mixing_s", juce::roundToInt (runMixingSeconds) },
                                      { "xruns", runXruns },
                                      { "cpu_peak", juce::roundToInt (cpuPeak * 100.0) },
                                      { "rss_mb", juce::roundToInt (rss) },
                                      { "inputs", now.inputs },
                                      { "tracks", now.tracks } });
        if (! reportedMemory && rss > kMemoryHighMb)
        {
            reportedMemory = true;
            track ("memory_high", { { "rss_mb", juce::roundToInt (rss) }, { "inputs", now.inputs }, { "tracks", now.tracks } });
        }
    }

    // ---- what the next launch reads if this one never says goodbye ----
    {
        const juce::ScopedLock sl (lock);
        if (! ended)
        {
            const auto body = runJson();
            if (body != lastContext) { lastContext = body; runDirty = true; }
        }
    }
}

juce::String Telemetry::runJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("session_id", sessionId);
    o->setProperty ("app_version", config.appVersion);
    // Rounded to half a minute, so the file is rewritten twice a minute rather than every tick.
    o->setProperty ("uptime_s", 30 * int ((juce::Time::getCurrentTime() - startedAt).inSeconds() / 30.0));
    o->setProperty ("mixing_s", 30 * int (runMixingSeconds / 30.0));
    o->setProperty ("xruns", runXruns);
    if (haveLast)
    {
        o->setProperty ("activity", last.activity);
        if (last.deviceKind.isNotEmpty()) o->setProperty ("device_kind", last.deviceKind);
        if (last.deviceModel.isNotEmpty()) o->setProperty ("device_model", last.deviceModel);
        o->setProperty ("sample_rate", juce::roundToInt (last.sampleRate));
        o->setProperty ("buffer", last.bufferSize);
        o->setProperty ("inputs", last.inputs);
        o->setProperty ("recording", last.recording);
    }
    return juce::JSON::toString (juce::var (o), true);
}

// ---------------------------------------------------------------------------
// The worker: files and the network, never anywhere else

void Telemetry::run()
{
    while (! threadShouldExit() && ! stopping)
    {
        wait (1000);
        if (threadShouldExit() || stopping) break;
        writeFiles();

        bool due = false;
        {
            const juce::ScopedLock sl (lock);
            const auto now = juce::Time::getMillisecondCounter();
            due = sharing && noticeShown && isConfigured() && ! queue.empty()
               && (nextAttemptMs == 0 || now >= nextAttemptMs)
               && (int (queue.size()) >= kBatch || now - oldestQueuedMs >= kSendAfterMs || flushWanted.load());
        }
        if (due) sendBatch();
    }
}

bool Telemetry::flushNow() { return sendBatch() > 0; }

int Telemetry::sendBatch()
{
    std::vector<Row> batch;
    {
        const juce::ScopedLock sl (lock);
        if (! sharing || ! isConfigured()) return 0;
        for (size_t i = 0; i < queue.size() && int (batch.size()) < kBatch; ++i) batch.push_back (queue[i]);
    }
    if (batch.empty()) return 0;

    juce::String body = "{\"rows\":[";
    for (size_t i = 0; i < batch.size(); ++i) body << (i > 0 ? "," : "") << batch[i].json;
    body << "]}";

    const int status = config.send ? config.send (config.url, body, config.anonKey)
                                   : defaultSend (config.url, body, config.anonKey, &stopping);
    const bool accepted = status >= 200 && status < 300;
    // A 4xx other than "slow down" is a batch the server will never take (a schema that moved
    // on): dropped and counted, so one bad row cannot hold every later one back for good.
    const bool rejected = status >= 400 && status < 500 && status != 408 && status != 429;

    int healthFailed = 0, healthDropped = 0;
    {
        const juce::ScopedLock sl (lock);
        if (accepted || rejected)
        {
            const auto lastSeq = batch.back().seq;
            while (! queue.empty() && queue.front().seq <= lastSeq) queue.pop_front();
            if (rejected) droppedRows += int (batch.size());
            queueDirty = true;
            nextAttemptMs = 0;
            backoffMs = 0;
            oldestQueuedMs = juce::Time::getMillisecondCounter();
            if (queue.empty()) flushWanted = false;
            // A failure of the telemetry itself is reported apart from the product's, once
            // it has something to say and a way to say it.
            if (accepted && (failedSends > 0 || droppedRows > 0))
            {
                healthFailed = failedSends;
                healthDropped = droppedRows;
                failedSends = 0;
                droppedRows = 0;
                stateDirty = true;
            }
        }
        else
        {
            ++failedSends;
            stateDirty = true;
            backoffMs = backoffMs == 0 ? kFirstBackoffMs : juce::jmin (kMaxBackoffMs, backoffMs * 2);
            nextAttemptMs = juce::Time::getMillisecondCounter() + juce::uint32 (backoffMs);
            flushWanted = false;
        }
    }
    if (healthFailed > 0 || healthDropped > 0)
        enqueue (makeRow ("telemetry_health", { { "failed_sends", healthFailed }, { "dropped_rows", healthDropped },
                                                { "last_status", status } },
                          sessionId, config.appVersion));
    return accepted ? int (batch.size()) : (rejected ? 0 : -1);
}

juce::String Telemetry::stateJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("install_id", installId);
    o->setProperty ("first_seen", firstSeen.toISO8601 (true));
    o->setProperty ("launches", launches);
    o->setProperty ("sharing", sharing);
    o->setProperty ("notice_shown", noticeShown);
    o->setProperty ("mixing_seconds", std::round (mixingSeconds));
    o->setProperty ("failed_sends", failedSends);
    o->setProperty ("dropped_rows", droppedRows);
    auto* m = new juce::DynamicObject();
    for (const auto& kv : milestonesGot) m->setProperty (kv.first, kv.second);
    o->setProperty ("milestones", juce::var (m));
    auto* f = new juce::DynamicObject();
    for (const auto& kv : featuresUsed) f->setProperty (kv.first, kv.second);
    o->setProperty ("features", juce::var (f));
    return juce::JSON::toString (juce::var (o));
}

void Telemetry::writeFilesNow() { writeFiles(); }

void Telemetry::writeFiles()
{
    // Taken under the lock, written outside it: the message thread never waits on a disk.
    juce::String state, queued, runBody;
    bool writeState = false, writeQueue = false, writeRun = false;
    {
        const juce::ScopedLock sl (lock);
        if (stateDirty) { state = stateJson(); writeState = true; stateDirty = false; }
        if (queueDirty)
        {
            for (const auto& r : queue) queued << r.json << "\n";
            writeQueue = true;
            queueDirty = false;
        }
        if (runDirty && ! ended) { runBody = lastContext; writeRun = true; runDirty = false; }
    }
    const auto atomically = [] (const juce::File& f, const juce::String& text)
    {
        const auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
        if (tmp.replaceWithText (text)) tmp.moveFileTo (f);
    };
    if (writeState) atomically (stateFile(), state);
    if (writeQueue)
    {
        if (queued.isEmpty()) queueFile().deleteFile();
        else atomically (queueFile(), queued);
    }
    if (writeRun) atomically (runFile(), runBody);
}

} // namespace livemix
