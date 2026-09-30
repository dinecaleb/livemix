#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include "SessionState.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// AUTOSAVE, AND WHAT HAPPENS WHEN DLIVE DOES NOT GET TO SAY GOODBYE
//
// A service is the one recording nobody gets to make twice, and the twenty minutes when a
// mistake is public are exactly the twenty minutes an operator is least able to remember to
// press Save. So DLIVE writes the session down for them, continuously, and keeps enough
// beside it to tell a crash from a clean quit.
//
// Three files live in a session's folder:
//
//   <name>.dlive.json                 the document. Written by Save, by Save As, by a clean
//                                     quit, and before the document is replaced.
//   <name>.dlive.autosave.json        the autosave. Written ~2 s after the session last
//                                     changed, and immediately after the moments worth not
//                                     losing (a tune, a scene recall, a new reference).
//   <name>.dlive.open                 the marker. Written while the session is open and
//                                     removed on a clean quit, so one that is still there on
//                                     the next launch *is* the detection - the same trick the
//                                     Recorder plays with its take sidecars, for the same
//                                     reason: a flag that has to be cleared cannot lie about
//                                     a process that was killed.
//
// Everything is written from a worker thread, from an immutable snapshot taken on the message
// thread, and landed by writing a temporary file and renaming it - so a session file is never
// half a session, whatever happens in the middle. Nothing here is on the audio thread, and
// nothing here blocks the UI: a thirty-two channel document with a morning of track history
// is a real serialise, and it has no business inside a 30 Hz tick.
// ---------------------------------------------------------------------------
class SessionAutosave : private juce::Thread
{
public:
    SessionAutosave();
    ~SessionAutosave() override;

    // Which session is open. Writes the marker; an empty file stops autosaving (there is
    // nowhere to put a sidecar for a session that has never had a folder).
    void open (const juce::File& document);
    juce::File document() const;

    // The session changed. `immediately` skips the wait, for the moments worth not losing.
    void note (const SessionState& state, bool immediately = false);

    // A clean quit: finish whatever is owed, then take the marker and the autosave with us.
    // After this, the next launch has nothing to recover, which is the whole point.
    void closeCleanly();

    // Whether anything is still owed to the disk (for a test, and for the status line).
    bool isIdle() const;
    // When the last autosave actually landed on the disk, for the toolbar and the status foot
    // to say so. A default-constructed Time means nothing has been written this session.
    juce::Time lastWrite() const;
    // Blocks until everything owed has landed. Message thread, only on the way out.
    void flush (int timeoutMs = 4000);

    static juce::File autosaveFor (const juce::File& document);
    static juce::File markerFor (const juce::File& document);

    // WHAT THE LAST RUN LEFT BEHIND.
    //
    // `offer` is true only when DLIVE did not get to say goodbye *and* the autosave holds work
    // the document does not. A marker on its own is a crash that lost nothing; an autosave
    // older than the document is a session that was saved after it was written.
    struct Recovery
    {
        bool offer = false;
        juce::File autosave;
        juce::Time when;              // when the unsaved work was written
        juce::Time documentWhen;      // when the document was last saved
        juce::String sentence;        // "DLIVE found work from 8:42 PM that was not saved."
    };
    //
    // When it offers, the autosave is first copied aside to `heldFor(document)`, and `autosave`
    // names that copy. Everything between the check and the answer - the session reopening,
    // the devices it touches, the autosave worker writing the session that was just loaded -
    // may write the ordinary autosave, and none of it may touch the work being offered back.
    // A held copy nobody answered (DLIVE quit, or crashed again, with the question on screen)
    // is offered again next time, for as long as it is newer than the document.
    static Recovery check (const juce::File& document);
    // The user answered the recovery question: the held copy and the autosave it came from go.
    // The open session's marker stays, so a crash later this morning is still caught.
    static void dismissRecovery (const juce::File& document);
    // A clean goodbye: the autosave and the marker go. A held copy nobody has answered stays.
    static void discard (const juce::File& document);
    static juce::File heldFor (const juce::File& document);

private:
    void run() override;
    void writeNow (const SessionState& state, const juce::File& file);

    mutable juce::CriticalSection lock;
    juce::File file;                                  // the document; the sidecars hang off it
    std::unique_ptr<SessionState> pending;            // the snapshot still owed to the disk
    juce::uint32 dueAt = 0;                           // millisecond counter; 0 = nothing owed
    bool writing = false;
    // Written by the worker, read by the message thread: when the last write landed.
    std::atomic<juce::int64> wroteAt { 0 };
};

} // namespace livemix
