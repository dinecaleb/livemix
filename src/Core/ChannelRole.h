#pragma once
#include <array>

namespace livemix
{

// Every source a Dine channel product can be assigned to. The first twelve are
// the drum roles (their indices are released in Dine Drums sessions and must
// never move); vocal, keys, master, guitar and bass sources follow. Processing baselines and
// Tune targets are defined per RoleFamily and refined per role where it matters.
enum class ChannelRole : int
{
    // ---- Dine Drums ----
    KickIn = 0,
    KickOut,
    SnareTop,
    SnareBottom,
    HiHat,
    RackTom,
    FloorTom,
    Overhead,
    OverheadLeft,
    OverheadRight,
    Room,
    DrumBus,
    // ---- Dine Vocals ----
    LeadVocal,
    BackingVocal,
    Choir,
    Speech,
    VocalBus,
    // ---- Dine Keys ----
    Piano,
    ElectricPiano,
    Organ,
    SynthPad,
    SynthLead,
    KeysBus,
    // ---- Dine Master (the role is the delivery destination) ----
    MasterBroadcast,
    MasterStream,
    MasterRecording,
    MasterRoom,
    // ---- Dine Guitar ----
    AcousticGuitar,
    ElectricGuitarClean,
    ElectricGuitarDrive,
    GuitarBus,
    // ---- Dine Bass ----
    BassDI,
    BassAmp,
    SynthBass,
    BassBus,
    Count
};

enum class RoleFamily : int
{
    Kick = 0,
    Snare,
    HiHat,
    Tom,
    Overhead,
    Room,
    Bus,
    LeadVocal,
    BackingVocal,
    Choir,
    Speech,
    VocalBus,
    Piano,
    ElectricPiano,
    Organ,
    Synth,
    KeysBus,
    Master,
    AcousticGuitar,
    ElectricGuitar,
    GuitarBus,
    ElectricBass,
    SynthBass,
    BassBus,
    Count
};

// Which Dine product a source belongs to. Dine FX is a separate engine (src/FX).
enum class Product : int { Drums = 0, Vocals, Keys, Master, Guitar, Bass, Count };

inline constexpr std::array<const char*, int (ChannelRole::Count)> kChannelRoleNames {
    "Kick In", "Kick Out", "Snare Top", "Snare Bottom", "Hi-Hat", "Rack Tom",
    "Floor Tom", "Overhead", "Overhead L", "Overhead R", "Room", "Drum Bus",
    "Lead Vocal", "Backing Vocal", "Choir", "Speech", "Vocal Bus",
    "Piano", "Electric Piano", "Organ", "Synth Pad", "Synth Lead", "Keys Bus",
    "Broadcast", "Livestream", "Recording", "Room PA",
    "Acoustic Guitar", "Electric Clean", "Electric Drive", "Guitar Bus",
    "Bass DI", "Bass Amp", "Synth Bass", "Bass Bus"
};

inline constexpr const char* channelRoleName (ChannelRole r) noexcept
{
    const int i = int (r);
    return (i >= 0 && i < int (ChannelRole::Count)) ? kChannelRoleNames[size_t (i)] : "Unknown";
}

inline constexpr RoleFamily roleFamily (ChannelRole r) noexcept
{
    switch (r)
    {
        case ChannelRole::KickIn:
        case ChannelRole::KickOut:        return RoleFamily::Kick;
        case ChannelRole::SnareTop:
        case ChannelRole::SnareBottom:    return RoleFamily::Snare;
        case ChannelRole::HiHat:          return RoleFamily::HiHat;
        case ChannelRole::RackTom:
        case ChannelRole::FloorTom:       return RoleFamily::Tom;
        case ChannelRole::Overhead:
        case ChannelRole::OverheadLeft:
        case ChannelRole::OverheadRight:  return RoleFamily::Overhead;
        case ChannelRole::Room:           return RoleFamily::Room;
        case ChannelRole::DrumBus:        return RoleFamily::Bus;
        case ChannelRole::LeadVocal:      return RoleFamily::LeadVocal;
        case ChannelRole::BackingVocal:   return RoleFamily::BackingVocal;
        case ChannelRole::Choir:          return RoleFamily::Choir;
        case ChannelRole::Speech:         return RoleFamily::Speech;
        case ChannelRole::VocalBus:       return RoleFamily::VocalBus;
        case ChannelRole::Piano:          return RoleFamily::Piano;
        case ChannelRole::ElectricPiano:  return RoleFamily::ElectricPiano;
        case ChannelRole::Organ:          return RoleFamily::Organ;
        case ChannelRole::SynthPad:
        case ChannelRole::SynthLead:      return RoleFamily::Synth;
        case ChannelRole::KeysBus:        return RoleFamily::KeysBus;
        case ChannelRole::MasterBroadcast:
        case ChannelRole::MasterStream:
        case ChannelRole::MasterRecording:
        case ChannelRole::MasterRoom:     return RoleFamily::Master;
        case ChannelRole::AcousticGuitar:      return RoleFamily::AcousticGuitar;
        case ChannelRole::ElectricGuitarClean:
        case ChannelRole::ElectricGuitarDrive: return RoleFamily::ElectricGuitar;
        case ChannelRole::GuitarBus:           return RoleFamily::GuitarBus;
        case ChannelRole::BassDI:
        case ChannelRole::BassAmp:             return RoleFamily::ElectricBass;
        case ChannelRole::SynthBass:           return RoleFamily::SynthBass;
        case ChannelRole::BassBus:             return RoleFamily::BassBus;
        case ChannelRole::Count:
        default:                          return RoleFamily::Kick;
    }
}

inline constexpr Product productOf (RoleFamily f) noexcept
{
    switch (f)
    {
        case RoleFamily::LeadVocal:
        case RoleFamily::BackingVocal:
        case RoleFamily::Choir:
        case RoleFamily::Speech:
        case RoleFamily::VocalBus:     return Product::Vocals;
        case RoleFamily::Piano:
        case RoleFamily::ElectricPiano:
        case RoleFamily::Organ:
        case RoleFamily::Synth:
        case RoleFamily::KeysBus:      return Product::Keys;
        case RoleFamily::Master:       return Product::Master;
        case RoleFamily::AcousticGuitar:
        case RoleFamily::ElectricGuitar:
        case RoleFamily::GuitarBus:    return Product::Guitar;
        case RoleFamily::ElectricBass:
        case RoleFamily::SynthBass:
        case RoleFamily::BassBus:      return Product::Bass;
        default:                       return Product::Drums;
    }
}

inline constexpr Product productOf (ChannelRole r) noexcept { return productOf (roleFamily (r)); }

inline constexpr bool isBusFamily (RoleFamily f) noexcept
{
    return f == RoleFamily::Bus || f == RoleFamily::VocalBus || f == RoleFamily::KeysBus || f == RoleFamily::GuitarBus || f == RoleFamily::BassBus || f == RoleFamily::Master;
}

inline constexpr ChannelRole channelRoleFromIndex (int index) noexcept
{
    return (index >= 0 && index < int (ChannelRole::Count)) ? ChannelRole (index) : ChannelRole::KickIn;
}

} // namespace livemix
