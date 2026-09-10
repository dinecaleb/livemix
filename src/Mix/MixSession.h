#pragma once
#include <array>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"

namespace livemix
{

// Capacity of one DLIVE mix. Everything on the audio thread is sized from these.
inline constexpr int kMaxInputs = 64;   // device input channels DLIVE will look at
inline constexpr int kMaxStrips = 64;   // assigned inputs (a stereo pair is one strip)

// The internal buses DLIVE builds on its own. The user never creates them.
enum class MixBus : int { Drums = 0, Bass, Music, Vocals, Master, Count };

inline constexpr std::array<const char*, int (MixBus::Count)> kMixBusNames { "DRUMS", "BASS", "MUSIC", "VOCALS", "MASTER" };
inline constexpr const char* mixBusName (MixBus b) noexcept
{
    const int i = int (b);
    return (i >= 0 && i < int (MixBus::Count)) ? kMixBusNames[size_t (i)] : "?";
}

// The effect returns DLIVE builds on its own. Each is one FxChain fed by sends.
enum class FxSlot : int { VocalPlate = 0, VocalDelay, BgvHall, SnarePlate, DrumRoom, Count };

inline constexpr std::array<const char*, int (FxSlot::Count)> kFxSlotNames {
    "Vocal Plate", "Vocal Delay", "Backing Hall", "Snare Plate", "Drum Room"
};
inline constexpr const char* fxSlotName (FxSlot s) noexcept
{
    const int i = int (s);
    return (i >= 0 && i < int (FxSlot::Count)) ? kFxSlotNames[size_t (i)] : "?";
}

// What the mix is for. Chooses the master's delivery role (loudness target, ceiling).
enum class MixPurpose : int { ChurchBroadcast = 0, Livestream, LiveRecording, WorshipSession, Count };

inline constexpr std::array<const char*, int (MixPurpose::Count)> kMixPurposeNames {
    "Church Broadcast", "Livestream", "Live Recording", "Worship Session"
};
inline constexpr const char* mixPurposeName (MixPurpose p) noexcept
{
    const int i = int (p);
    return (i >= 0 && i < int (MixPurpose::Count)) ? kMixPurposeNames[size_t (i)] : "?";
}

inline constexpr ChannelRole masterRoleFor (MixPurpose p) noexcept
{
    switch (p)
    {
        case MixPurpose::ChurchBroadcast: return ChannelRole::MasterBroadcast;
        case MixPurpose::LiveRecording:   return ChannelRole::MasterRecording;
        case MixPurpose::Livestream:
        case MixPurpose::WorshipSession:
        default:                          return ChannelRole::MasterStream;
    }
}

// One assigned input: a device channel (or a stereo-linked pair) with a name and a source role.
struct InputAssignment
{
    std::string name;                       // "Kick", "Keys", "Pastor"
    ChannelRole role = ChannelRole::KickIn;
    int inputA = -1;                        // 0-based device input index
    int inputB = -1;                        // -1 = mono; otherwise the right channel of a stereo pair
    bool enabled = true;
    // What the source is drawn as. Empty means "whatever the role says", which is right
    // almost always; a key from Dine::iconChoices() overrides it for the times it is not -
    // a pad running backing tracks, a DI that is really a talkback mic. A label, never
    // routing: nothing about the mix reads it. Last, so the brace-initialised sessions all
    // over the tests keep working.
    std::string icon;

    bool isStereo() const noexcept { return inputB >= 0; }
    int numChannels() const noexcept { return isStereo() ? 2 : 1; }
};

// Everything the user decided: which inputs are what, what the mix is for, which sound.
struct MixSession
{
    std::string name = "Sunday";
    StyleProfileId profile = StyleProfileId::ModernGospel;
    MixPurpose purpose = MixPurpose::ChurchBroadcast;
    std::vector<InputAssignment> inputs;    // at most kMaxStrips are used

    ChannelRole masterRole() const noexcept { return masterRoleFor (purpose); }
    int numStrips() const noexcept { return int (inputs.size()) < kMaxStrips ? int (inputs.size()) : kMaxStrips; }
};

// Which internal bus a source belongs to. Bass runs to the master on its own bus, as the brief asks.
inline constexpr MixBus mixBusForFamily (RoleFamily f) noexcept
{
    switch (f)
    {
        case RoleFamily::Kick:
        case RoleFamily::Snare:
        case RoleFamily::HiHat:
        case RoleFamily::Tom:
        case RoleFamily::Overhead:
        case RoleFamily::Room:
        case RoleFamily::Bus:            return MixBus::Drums;
        case RoleFamily::ElectricBass:
        case RoleFamily::SynthBass:
        case RoleFamily::BassBus:        return MixBus::Bass;
        case RoleFamily::LeadVocal:
        case RoleFamily::BackingVocal:
        case RoleFamily::Choir:
        case RoleFamily::Speech:
        case RoleFamily::VocalBus:       return MixBus::Vocals;
        case RoleFamily::Master:         return MixBus::Master;
        case RoleFamily::Piano:
        case RoleFamily::ElectricPiano:
        case RoleFamily::Organ:
        case RoleFamily::Synth:
        case RoleFamily::KeysBus:
        case RoleFamily::AcousticGuitar:
        case RoleFamily::ElectricGuitar:
        case RoleFamily::GuitarBus:
        default:                         return MixBus::Music;
    }
}

inline constexpr MixBus mixBusForRole (ChannelRole r) noexcept { return mixBusForFamily (roleFamily (r)); }

// The source role whose profile family gives each bus its processing baseline and Tune targets.
inline constexpr ChannelRole busRole (MixBus b, MixPurpose purpose) noexcept
{
    switch (b)
    {
        case MixBus::Drums:  return ChannelRole::DrumBus;
        case MixBus::Bass:   return ChannelRole::BassBus;
        case MixBus::Music:  return ChannelRole::KeysBus;
        case MixBus::Vocals: return ChannelRole::VocalBus;
        case MixBus::Master:
        default:             return masterRoleFor (purpose);
    }
}

} // namespace livemix
