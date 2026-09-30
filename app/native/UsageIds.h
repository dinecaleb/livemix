#pragma once
#include "Core/ChannelRole.h"

namespace livemix
{

// The words the usage events use for a source, fixed for good. They are what a query groups
// by for "which channels do people tune", so they are data the way a parameter ID is data:
// appended to, never renamed. Not the display names - a display name can be reworded without
// splitting a chart in two. docs/ANALYTICS.md lists every event that carries them.
inline constexpr const char* roleFamilyId (RoleFamily f) noexcept
{
    switch (f)
    {
        case RoleFamily::Kick:           return "kick";
        case RoleFamily::Snare:          return "snare";
        case RoleFamily::HiHat:          return "hihat";
        case RoleFamily::Tom:            return "tom";
        case RoleFamily::Overhead:       return "overhead";
        case RoleFamily::Room:           return "drum_room";
        case RoleFamily::Bus:            return "drum_bus";
        case RoleFamily::LeadVocal:      return "lead_vocal";
        case RoleFamily::BackingVocal:   return "backing_vocal";
        case RoleFamily::Choir:          return "choir";
        case RoleFamily::Speech:         return "speech";
        case RoleFamily::VocalBus:       return "vocal_bus";
        case RoleFamily::Piano:          return "piano";
        case RoleFamily::ElectricPiano:  return "electric_piano";
        case RoleFamily::Organ:          return "organ";
        case RoleFamily::Synth:          return "synth";
        case RoleFamily::KeysBus:        return "keys_bus";
        case RoleFamily::Master:         return "master";
        case RoleFamily::AcousticGuitar: return "acoustic_guitar";
        case RoleFamily::ElectricGuitar: return "electric_guitar";
        case RoleFamily::GuitarBus:      return "guitar_bus";
        case RoleFamily::ElectricBass:   return "bass";
        case RoleFamily::SynthBass:      return "synth_bass";
        case RoleFamily::BassBus:        return "bass_bus";
        case RoleFamily::Ambience:       return "ambience";
        case RoleFamily::AmbienceBus:    return "ambience_bus";
        case RoleFamily::Saxophone:      return "saxophone";
        case RoleFamily::Percussion:     return "percussion";
        case RoleFamily::Shaker:         return "shaker";
        case RoleFamily::Brass:          return "brass";
        case RoleFamily::DrumPad:        return "drum_pad";
        case RoleFamily::Count:          break;
    }
    return "other";
}

// The instrument the way a person would say it: the coarse grouping a milestone and a
// "most tuned" chart are about. Also fixed words.
inline constexpr const char* roleKindId (RoleFamily f) noexcept
{
    switch (f)
    {
        case RoleFamily::Kick: case RoleFamily::Snare: case RoleFamily::HiHat: case RoleFamily::Tom:
        case RoleFamily::Overhead: case RoleFamily::Room: case RoleFamily::Bus:
        case RoleFamily::Percussion: case RoleFamily::Shaker: case RoleFamily::DrumPad:
            return "drums";
        case RoleFamily::ElectricBass: case RoleFamily::SynthBass: case RoleFamily::BassBus:
            return "bass";
        case RoleFamily::LeadVocal: case RoleFamily::BackingVocal: case RoleFamily::Choir: case RoleFamily::VocalBus:
            return "vocals";
        case RoleFamily::Speech:
            return "speech";
        case RoleFamily::Piano: case RoleFamily::ElectricPiano: case RoleFamily::Organ: case RoleFamily::Synth:
        case RoleFamily::KeysBus:
            return "keys";
        case RoleFamily::AcousticGuitar: case RoleFamily::ElectricGuitar: case RoleFamily::GuitarBus:
            return "guitar";
        case RoleFamily::Saxophone: case RoleFamily::Brass:
            return "horns";
        case RoleFamily::Ambience: case RoleFamily::AmbienceBus:
            return "room";
        case RoleFamily::Master: case RoleFamily::Count:
            break;
    }
    return "other";
}

} // namespace livemix
