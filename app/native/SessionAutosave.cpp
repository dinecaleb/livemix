#include "SessionAutosave.h"
#include "SessionStore.h"

namespace livemix
{

namespace
{
    // ~2 s after the last change. A knob drag is one write, not thirty; a service that is
    // being mixed hard still lands on disk inside a breath.
    constexpr int kQuietMs = 2000;
    // ...but never longer than this after the first change it owes: a mix being moved without a
    // pause (a fader ridden through a song, Autopilot at work) used to put the write off for as
    // long as the moving went on.
    constexpr int kMostMs = 10000;

    juce::String beside (const juce::File& document, const char* suffix)
    {
        // "<name>.dine.json" -> "<name>.dine.<suffix>". The document's own extension stays in
        // the name, so a folder lists the three files together and it is obvious what they are.
        return document.getFileName().upToLastOccurrenceOf (".json", false, false) + suffix;
    }

    juce::String whenSentence (juce::Time when)
    {
        const auto today = juce::Time::getCurrentTime();
        const bool sameDay = when.getYear() == today.getYear() && when.getDayOfYear() == today.getDayOfYear();
        const auto clock = when.toString (false, true, false, true);      // "8:42:10 pm" -> trimmed below
        const auto hhmm = clock.upToLastOccurrenceOf (":", false, false)
                        + clock.fromLastOccurrenceOf (":", false, false).fromFirstOccurrenceOf (" ", true, false);
        return sameDay ? hhmm : when.toString (true, true, false, true);
    }
}

SessionAutosave::SessionAutosave() : juce::Thread ("DINE autosave") {}

SessionAutosave::~SessionAutosave()
{
    stopThread (4000);
}

juce::File SessionAutosave::autosaveFor (const juce::File& document)
{
    if (document == juce::File()) return {};
    return document.getSiblingFile (beside (document, ".autosave.json"));
}

juce::File SessionAutosave::heldFor (const juce::File& document)
{
    if (document == juce::File()) return {};
    return document.getSiblingFile (beside (document, ".recovered.json"));
}

juce::File SessionAutosave::markerFor (const juce::File& document)
{
    if (document == juce::File()) return {};
    return document.getSiblingFile (beside (document, ".open"));
}

void SessionAutosave::open (const juce::File& document)
{
    {
        const juce::ScopedLock sl (lock);
        if (file == document) return;      // called on every change: the same session is not news
        file = document;
        pending.reset();
        dueAt = 0;
    }
    if (document == juce::File()) return;
    // The marker says "a session is open here". Removing it is what a clean quit does, so one
    // that is still here next time means DINE was killed rather than closed.
    markerFor (document).replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));
    if (! isThreadRunning()) startThread (juce::Thread::Priority::background);
}

juce::File SessionAutosave::document() const
{
    const juce::ScopedLock sl (lock);
    return file;
}

void SessionAutosave::note (const SessionState& state, bool immediately)
{
    const juce::ScopedLock sl (lock);
    if (file == juce::File()) return;
    // The snapshot is taken here, on the message thread, and the worker never looks at the
    // live objects: what lands on disk is one coherent moment, not a session being edited.
    const auto now = juce::Time::getMillisecondCounter();
    if (pending == nullptr) owedSince = now;
    pending = std::make_unique<SessionState> (state);
    dueAt = immediately ? now : std::min (now + (juce::uint32) kQuietMs, owedSince + (juce::uint32) kMostMs);
    notify();
}

bool SessionAutosave::isIdle() const
{
    const juce::ScopedLock sl (lock);
    return pending == nullptr && ! writing;
}

void SessionAutosave::flush (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);
    for (;;)
    {
        {
            const juce::ScopedLock sl (lock);
            if (pending != nullptr) dueAt = juce::Time::getMillisecondCounter();   // stop waiting: we are leaving
        }
        notify();
        if (isIdle() || juce::Time::getMillisecondCounter() >= deadline) return;
        juce::Thread::sleep (10);
    }
}

void SessionAutosave::closeCleanly()
{
    flush();
    juce::File document;
    {
        const juce::ScopedLock sl (lock);
        document = file;
        file = juce::File();
        pending.reset();
        dueAt = 0;
    }
    discard (document);
}

void SessionAutosave::discard (const juce::File& document)
{
    if (document == juce::File()) return;
    autosaveFor (document).deleteFile();
    markerFor (document).deleteFile();
}

void SessionAutosave::dismissRecovery (const juce::File& document)
{
    if (document == juce::File()) return;
    heldFor (document).deleteFile();
    autosaveFor (document).deleteFile();
}

void SessionAutosave::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<SessionState> due;
        juce::File target;
        {
            const juce::ScopedLock sl (lock);
            if (pending != nullptr && juce::Time::getMillisecondCounter() >= dueAt)
            {
                due = std::move (pending);
                target = file;
                writing = true;
            }
        }
        if (due != nullptr)
        {
            if (target != juce::File()) writeNow (*due, autosaveFor (target));
            const juce::ScopedLock sl (lock);
            writing = false;
            continue;                      // straight round again: another change may already be owed
        }
        wait (200);                        // woken by note() and flush(); the poll is the safety net
    }
}

void SessionAutosave::writeNow (const SessionState& state, const juce::File& target)
{
    if (target == juce::File()) return;
    // Only a write that landed is reported as one: the status foot saying "autosaved" over a
    // full disk would be the one lie that matters.
    if (! SessionStore::writeTextAtomically (target, juce::JSON::toString (SessionStore::toVar (state), false)))
    {
        failing.store (true, std::memory_order_release);
        return;
    }
    failing.store (false, std::memory_order_release);
    wroteAt.store (juce::Time::getCurrentTime().toMilliseconds(), std::memory_order_release);
}

juce::Time SessionAutosave::lastWrite() const
{
    return juce::Time (wroteAt.load (std::memory_order_acquire));
}

SessionAutosave::Recovery SessionAutosave::check (const juce::File& document)
{
    Recovery r;
    if (document == juce::File()) return r;
    r.documentWhen = document.existsAsFile() ? document.getLastModificationTime() : juce::Time (0);

    // A question asked before and never answered comes first: it holds the work from the
    // crash it was asked about, and the ordinary autosave has been written since by a session
    // that only ever held the document.
    // Only an answer puts it away (dismissRecovery). Never a date: quitting writes the
    // document, so a question left on screen at quit would otherwise find the document
    // "newer" next launch and delete the only copy of the crash's work without a word.
    const auto held = heldFor (document);
    if (held.existsAsFile())
    {
        r.autosave = held;
        r.when = held.getLastModificationTime();
        r.offer = true;
        r.sentence = "DINE found work from " + whenSentence (r.when) + " that was not saved.";
        return r;
    }

    r.autosave = autosaveFor (document);
    if (! r.autosave.existsAsFile()) return r;
    r.when = r.autosave.getLastModificationTime();

    // A marker still here means DINE was killed rather than closed. On its own that is a
    // crash that lost nothing; what makes it worth asking about is an autosave holding work
    // the document does not.
    const bool unclean = markerFor (document).existsAsFile();
    if (! unclean || r.when <= r.documentWhen) return r;

    // Set aside before anything else can write the autosave (see heldFor). A copy that fails
    // leaves the offer pointing at the autosave itself, which is what it always did.
    if (r.autosave.copyFileTo (held))
    {
        held.setLastModificationTime (r.when);
        r.autosave = held;
    }
    r.offer = true;
    r.sentence = "DINE found work from " + whenSentence (r.when) + " that was not saved.";
    return r;
}

} // namespace livemix
