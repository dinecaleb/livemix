#pragma once
#include <array>
#include <string>
#include <vector>
#include "SessionState.h"

namespace livemix
{

class MixController;
class SampleLibrary;

// DRUM KITS (2026-10-05): one choice that picks the kick, the snare and the toms together.
//
// A kit is a convenience above the per-strip Sound choice, never a replacement for it. It is a
// list of sound *names* - the identity a SampleChoice already stores - so it does not care
// which slot a sound landed in, how many sounds a family holds, or whether a session was saved
// before kits existed. Applying one changes `replaceSound` on each drum strip and nothing else:
// not whether the stage is on, not BLEND, SENSITIVITY, ALIGN or anything TUNE fitted. It is one
// mix-history entry, so one Cmd+Z puts every drum back.
//
// Which kit a session is on is never stored. It is read off the strips' own choices
// (currentDrumKit), so a hand-picked snare, an undo, an import or a session from last month all
// answer the question correctly with nothing to keep in step: no kit matches = "Custom".
//
// Every name below is a file in app/Samples (licensed: app/Samples/README.md). A kit may only
// name a sound DINE ships; more kits need more licensed sounds, not more names.
struct DrumKit
{
    std::string name;                      // what the picker says: plain, one word
    std::string kick, snare;               // built-in sound names
    std::vector<std::string> rackToms;     // first rack tom strip, second, third...; the last repeats
    std::string floorTom;
    std::string sentence;                  // what it is, for the picker's menu
};

const std::vector<DrumKit>& builtInDrumKits();
const DrumKit* findDrumKit (const std::string& name);

// What every kick, snare and tom strip of this session plays under `kit`, by name. Strips with
// no Sample stage, and a hi-hat (a kit has no hi-hat sounds), are left unset.
std::array<SampleChoice, kMaxStrips> drumKitChoices (const DrumKit& kit, const MixController&);

// The kit the drum strips are on now: its name, "Custom" when no kit matches every one of them,
// or "" when the session has no kick, snare or tom strip at all. `now` is readSampleChoices.
std::string currentDrumKit (const std::array<SampleChoice, kMaxStrips>& now, const MixController&);

// Applies `kit` as one edit. Returns the sentence that says what happened, including any sound
// this Mac does not have (that strip keeps the sound it had). Message thread.
std::string applyDrumKit (const DrumKit& kit, const SampleLibrary&, MixController&);

} // namespace livemix
