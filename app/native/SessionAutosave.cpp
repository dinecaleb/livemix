#include "SessionAutosave.h"
#include "SessionStore.h"

namespace livemix
{

namespace
{
    // ~2 s after the last change. A knob drag is one write, not thirty; a service that is
    // being mixed hard still lands on disk inside a breath.
    constexpr int kQuietMs = 2000;

    juce::String beside (const juce::File& document, const char* suffix)
    {
        // "<name>.dlive.json" -> "<name>.dlive.<suffix>". The document's own extension stays in
        // the name, so a folder lists the three files together and it is obvious what they are.
        return document.getFileName().upToLastOccurrenceOf (".json", false, false) + suffix;
    }

    // Written whole or not at all: a temporary file beside the target, then a rename. A
    // session file must never be half a session, whatever the power does in the middle.
    bool writeAtomically (const juce::File& target, const juce::String& text)
    {
        target.getParentDirectory().createDirectory();
        juce::TemporaryFile temp (target);
        {
            auto stream = temp.getFile().createOutputStream();
            if (stream == nullptr) return false;
            if (! stream->writeText (text, false, false, nullptr)) return false;
            stream->flush();
            if (stream->getStatus().failed()) return false;
        }
        return temp.overwriteTargetFileWithTemporary();
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

SessionAutosave::SessionAutosave() : juce::Thread ("DLIVE autosave") {}

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
    // that is still here next time means DLIVE was killed rather than closed.
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
    pending = std::make_unique<SessionState> (state);
    dueAt = juce::Time::getMillisecondCounter() + (immediately ? 0u : (juce::uint32) kQuietMs);
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
    if (! writeAtomically (target, juce::JSON::toString (SessionStore::toVar (state), false))) return;
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
    const auto held = heldFor (document);
    if (held.existsAsFile())
    {
        if (held.getLastModificationTime() > r.documentWhen)
        {
            r.autosave = held;
            r.when = held.getLastModificationTime();
            r.offer = true;
            r.sentence = "DLIVE found work from " + whenSentence (r.when) + " that was not saved.";
            return r;
        }
        held.deleteFile();              // the document has been saved since: nothing is owed
    }

    r.autosave = autosaveFor (document);
    if (! r.autosave.existsAsFile()) return r;
    r.when = r.autosave.getLastModificationTime();

    // A marker still here means DLIVE was killed rather than closed. On its own that is a
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
    r.sentence = "DLIVE found work from " + whenSentence (r.when) + " that was not saved.";
    return r;
}

} // namespace livemix
