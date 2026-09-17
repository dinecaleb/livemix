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
// The group buses, in the order a console reads left to right, then the master. SPEECH is
// its own group and never sits inside VOCALS: a preaching microphone is not a singer - it is
// levelled, muted and sent somewhere else at different moments of a service, and an operator
// has to be able to find it and move it without touching the singers. Everything that walks
// the group buses uses `b < int (MixBus::Master)`, so the order here is what the mixer bands,
// the TUNE meters, the LIVE tiles and the output feeds all follow.
// AMBIENCE is the sixth group and the newest (2026-09). A broadcast mix that carries only
// the stage sounds like a studio recording of a band; what makes a stream sound like a
// service is the building - the congregation singing back, the response, the applause. Those
// microphones need their own fader for the same reason SPEECH does: they are turned up and
// down at different moments from everything else, and an operator has to be able to find
// them. Everything that walks the group buses uses `b < int (MixBus::Master)`, so a new bus
// goes in before MASTER - and doing that moved the stored indices again, which is why
// SessionStore is version 4.
enum class MixBus : int { Drums = 0, Bass, Music, Vocals, Speech, Ambience, Master, Count };

inline constexpr std::array<const char*, int (MixBus::Count)> kMixBusNames { "DRUMS", "BASS", "MUSIC", "VOCALS", "SPEECH", "AMBIENCE", "MASTER" };
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

// ---------------------------------------------------------------------------
// HOW LOUD THE FINISHED MIX SHOULD BE
//
// This is the single number that decides whether a DLIVE master sounds competitive next to
// everything else the viewer watches, and until it was made visible it was a hidden
// consequence of the purpose: "Church Broadcast" quietly meant EBU R128, which is -23 LUFS,
// which is about 9 dB under what a stream is expected to be. That is the correct number for
// a television feed and the wrong one for almost every church, and nothing in the app said so.
//
// So it is a setting now, with its number printed beside it. The whole gain structure aims
// at it: the strips are fitted from it through the bus balance, the master's own compressor
// is fitted under it, and the limiter holds the ceiling rather than being asked to make up
// the difference. Turning it up does not mean "push the limiter harder" - it moves the
// target every stage is fitted against.
enum class DeliveryLoudness : int
{
    FromPurpose = 0,   // whatever the delivery role asks for: the professional default
    Broadcast,         // -23 LUFS, EBU R128: a television or radio feed with a loudness spec
    BroadcastUS,       // -24 LUFS, ATSC A/85
    Podcast,           // -18 LUFS: spoken word and archive
    Streaming,         // -16 LUFS: the conservative streaming number
    StreamingLoud,     // -14 LUFS: YouTube, Spotify, Facebook - what a church stream competes with
    Loud,              // -12 LUFS: as loud as DLIVE will aim without squashing the mix
    Count
};

inline constexpr std::array<const char*, int (DeliveryLoudness::Count)> kDeliveryLoudnessNames {
    "Match the purpose", "Broadcast (EBU R128)", "Broadcast (ATSC A/85)", "Podcast / archive",
    "Streaming", "Streaming (loud)", "As loud as it goes"
};

inline constexpr const char* deliveryLoudnessName (DeliveryLoudness d) noexcept
{
    const int i = int (d);
    return (i >= 0 && i < int (DeliveryLoudness::Count)) ? kDeliveryLoudnessNames[size_t (i)] : "?";
}

// The target itself. 0 means "whatever the delivery role already asks for", which is the one
// value that is not a number.
inline constexpr float deliveryLoudnessLufs (DeliveryLoudness d) noexcept
{
    switch (d)
    {
        case DeliveryLoudness::Broadcast:     return -23.0f;
        case DeliveryLoudness::BroadcastUS:   return -24.0f;
        case DeliveryLoudness::Podcast:       return -18.0f;
        case DeliveryLoudness::Streaming:     return -16.0f;
        case DeliveryLoudness::StreamingLoud: return -14.0f;
        case DeliveryLoudness::Loud:          return -12.0f;
        case DeliveryLoudness::FromPurpose:
        case DeliveryLoudness::Count:
        default:                              return 0.0f;
    }
}

inline const char* deliveryLoudnessHint (DeliveryLoudness d) noexcept
{
    switch (d)
    {
        case DeliveryLoudness::Broadcast:     return "For a feed that has to meet a European broadcast spec. Quiet on a phone.";
        case DeliveryLoudness::BroadcastUS:   return "For a feed that has to meet the American broadcast spec. Quiet on a phone.";
        case DeliveryLoudness::Podcast:       return "Spoken word and archive: plenty of headroom, easy to listen to for an hour.";
        case DeliveryLoudness::Streaming:     return "Safe for every platform. A little under what most channels sit at.";
        case DeliveryLoudness::StreamingLoud: return "What YouTube, Facebook and Spotify normalise to. The right answer for most churches.";
        case DeliveryLoudness::Loud:          return "As far as DLIVE will push without squashing the mix. Use when a stream has to cut through.";
        case DeliveryLoudness::FromPurpose:
        case DeliveryLoudness::Count:
        default:                              return "Whatever the mix's purpose asks for.";
    }
}

// Everything the user decided: which inputs are what, what the mix is for, which sound.
struct MixSession
{
    std::string name = "Sunday";
    StyleProfileId profile = StyleProfileId::ModernGospel;
    MixPurpose purpose = MixPurpose::ChurchBroadcast;
    // How loud the finished mix should be. FromPurpose keeps the delivery role's own standard,
    // which is what every session made before this setting existed had, so nothing about an
    // old session changes when it is opened.
    DeliveryLoudness delivery = DeliveryLoudness::FromPurpose;
    std::vector<InputAssignment> inputs;    // at most kMaxStrips are used

    ChannelRole masterRole() const noexcept { return masterRoleFor (purpose); }
    // The delivery target this session actually aims at, or 0 for "the role's own".
    float deliveryTargetLufs() const noexcept { return deliveryLoudnessLufs (delivery); }
    int numStrips() const noexcept { return int (inputs.size()) < kMaxStrips ? int (inputs.size()) : kMaxStrips; }
};

// Which input in `previous` each input in `next` used to be, or -1 for one that is new to
// the session. An input's identity is the device channel it arrives on, then its name - not
// its position in the list - so the same answer serves everything that has to follow an
// input when the assignments are rebuilt: the timeline's clips (Project::syncTracks), the
// kept mix (carryMix), and anything added later. Two lists that decide this separately are
// two lists that will one day disagree about which input is which.
inline std::vector<int> matchInputs (const MixSession& previous, const MixSession& next)
{
    std::vector<int> out (next.inputs.size(), -1);
    std::vector<bool> taken (previous.inputs.size(), false);

    auto find = [&] (auto match) -> int
    {
        for (size_t i = 0; i < previous.inputs.size(); ++i)
            if (! taken[i] && match (previous.inputs[i])) return int (i);
        return -1;
    };

    for (size_t n = 0; n < next.inputs.size(); ++n)
    {
        const auto& in = next.inputs[n];
        int was = find ([&] (const InputAssignment& then) { return then.inputA >= 0 && then.inputA == in.inputA; });
        if (was < 0)
            was = find ([&] (const InputAssignment& then) { return ! then.name.empty() && then.name == in.name; });
        if (was < 0) continue;
        out[n] = was;
        taken[size_t (was)] = true;
    }
    return out;
}

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
        case RoleFamily::VocalBus:       return MixBus::Vocals;
        case RoleFamily::Speech:         return MixBus::Speech;
        case RoleFamily::Ambience:
        case RoleFamily::AmbienceBus:    return MixBus::Ambience;
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
        // The speech group is still a bus of voices: it takes the vocal bus baseline (gentle
        // glue, light tone), not the speech *channel* chain - the de-essing, the boom cut and
        // the presence lift were already done on the microphone itself.
        case MixBus::Speech: return ChannelRole::VocalBus;
        // The ambience group is glued like a room, not like a band: gentle, slow, and with
        // the same rule that governs every ambience source - it is never gated and never
        // pushed forward, because what is between the sounds is the point of it.
        case MixBus::Ambience: return ChannelRole::AmbienceBus;
        case MixBus::Master:
        default:             return masterRoleFor (purpose);
    }
}

} // namespace livemix
