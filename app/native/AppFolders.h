#pragma once
#include <juce_core/juce_core.h>

// Where DINE keeps what belongs to the person rather than to a session: ~/Music/DINE (sessions,
// input maps, themes, samples, preferences) and ~/Library/DINE (the last-session pointer and
// telemetry's state). Every caller goes through here, so the two names live in one place.
//
// THE APP WAS CALLED DLIVE until 2026-10-01, and before that DINELIVE. migrateFromDlive() runs
// once at launch, before anything reads either folder: a DLIVE folder with no DINE beside it is
// moved across whole (one rename on the same disk, the audio is never copied) and a DLIVE alias
// is left where it was, so a session that wrote an absolute path into the old folder - a sample,
// an imported stem - still finds it. When both exist nothing is moved: the old sessions are
// still listed and still open where they are (SessionStore::listSessions).
namespace livemix::AppFolders
{
inline juce::File music()   { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DINE"); }
inline juce::File library() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DINE"); }

// The folders as they were called before, for reading only.
inline juce::File formerMusic()   { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DLIVE"); }
inline juce::File formerLibrary() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DLIVE"); }

// Moves `from` to `to` and leaves an alias behind. False when there was nothing to do or it
// could not be done; either way nothing is lost, because nothing is deleted.
inline bool moveAcross (const juce::File& from, const juce::File& to)
{
    if (! from.isDirectory() || from.isSymbolicLink() || to.exists()) return false;
    if (! from.moveFileTo (to)) return false;
    to.createSymbolicLink (from, false);
    return true;
}

inline void migrateFromDlive()
{
    moveAcross (formerMusic(), music());
    moveAcross (formerLibrary(), library());
}
} // namespace livemix::AppFolders
