#pragma once
#include <array>
#include <cctype>
#include <string>
#include <vector>
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"

namespace livemix
{

// Capacity of one DINE mix. Everything on the audio thread is sized from these.
inline constexpr int kMaxInputs = 64;   // device input channels DINE will look at
inline constexpr int kMaxStrips = 64;   // assigned inputs (each side of a stereo pair is its own strip)

// THE GROUP BUSES DINE builds on its own. The user never creates them.
//
// Each one exists because an operator has to be able to find it and move it without touching
// the others. SPEECH never sits inside the voices: a preaching microphone is levelled, muted
// and sent somewhere else at different moments of a service. AMBIENCE (the sixth, 2026-09) is
// the building - the congregation singing back, the response, the applause - and a broadcast
// without it sounds like a studio recording of a band rather than like a service.
//
// LEAD is the seventh (2026-09-28), and it is there for the same reason again, plus one:
// the lead is what the mix is *built around*, and the backing voices are a texture that sits
// under it. Summed into one group, "bring the voices up" brought the thing the lead has to
// stay above up with it, and the one relationship the whole hierarchy hangs on - lead against
// backing - could not be set at all.
//
// APPENDED TO, NEVER REORDERED: the enum is stored. A new group goes in before MASTER,
// everything that walks the groups uses `b < int (MixBus::Master)`, and SessionStore remaps an
// older file by the bus count it actually has (the last stored slot has always been the
// master). That moved the stored indices for the fifth, the sixth and now the seventh, which
// is why SessionStore is on version 6. It is also why LEAD is not beside VOCALS here:
// `mixBusInDisplayOrder` is where the console's own order lives.
enum class MixBus : int { Drums = 0, Bass, Music, Vocals, Speech, Ambience, Lead, Master, Count };

// VOCALS is called BGV on screen: with LEAD split out it is the backing voices and nothing
// else, and "VOCALS" beside "LEAD" reads as though one of them contained the other. The enum
// name and the stored index are untouched.
inline constexpr std::array<const char*, int (MixBus::Count)> kMixBusNames { "DRUMS", "BASS", "MUSIC", "BGV", "SPEECH", "AMBIENCE", "LEAD", "MASTER" };
inline constexpr const char* mixBusName (MixBus b) noexcept
{
    const int i = int (b);
    return (i >= 0 && i < int (MixBus::Count)) ? kMixBusNames[size_t (i)] : "?";
}

// The order a console reads the groups in, which is not the order they are stored in: the
// lead sits with the voices, where an engineer looks for it. Every list a person sees - the
// group tiles on TUNE and LIVE, the pinned rail on MIXER - walks this; everything that
// iterates buses to *compute* something walks the enum, because the enum is the storage.
inline constexpr std::array<MixBus, int (MixBus::Master)> kMixBusDisplayOrder {
    MixBus::Drums, MixBus::Bass, MixBus::Music, MixBus::Lead, MixBus::Vocals, MixBus::Speech, MixBus::Ambience
};
inline constexpr MixBus mixBusInDisplayOrder (int i) noexcept
{
    return (i >= 0 && i < int (MixBus::Master)) ? kMixBusDisplayOrder[size_t (i)] : MixBus::Master;
}

// The effect returns DINE builds on its own. Each is one FxChain fed by sends.
// Stored by index (sends, returns): appended to, never reordered. BAND HALL (2026-10-06) is
// the musicians' own space - keys, pads and guitars - kept apart from the voices' hall so the
// band's room and the singers' can be ridden separately.
enum class FxSlot : int { VocalPlate = 0, VocalDelay, BgvHall, SnarePlate, DrumRoom, BandHall, Count };

inline constexpr std::array<const char*, int (FxSlot::Count)> kFxSlotNames {
    "Vocal Plate", "Vocal Delay", "Backing Hall", "Snare Plate", "Drum Room", "Band Hall"
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
    // THE FOCAL SOURCE. The one the mix is built around: the lead singer during a song, and
    // whoever the engineer says otherwise. DINE picks the loudest lead vocal it heard when
    // nobody has said, which is right most Sundays and wrong on the one where the second
    // microphone is the one being sung into. Pinning it here settles it: that source is the
    // reference every balance, every hierarchy rule and every pocket cut is measured against,
    // and it is never the one held back to make room for another. At most one input carries
    // it (MixSession::setFocus). Last again, so brace-initialised sessions keep working.
    bool focus = false;
    // WHAT IT WAS ON THE OTHER SIDE OF SINGING / SPEAKING. A backing singer who is handed the
    // announcements and then sings again is a backing singer again, and a lapel that led a
    // song and then preaches is a lapel again: the role it last had in the other job, stored
    // as a ChannelRole index, -1 when it has never had one. Set by MixController::setInputRole,
    // read by roleForJob / roleForSinging. A label for the switch, never routing.
    int otherVoiceRole = -1;
    // ONE SIDE OF A STEREO PAIR. A stereo source is two channels on the console and two tracks
    // on the timeline - each recorded to its own mono file, each with its own meter - and the
    // two are linked (their faders and their solo move together, MixParameters::linkGroup).
    // -1 = the left side, +1 = the right side, which is the input straight after its left;
    // 0 = not part of a pair. A label for the pair, never routing: each side is a mono input.
    int stereoSide = 0;

    // `inputB` is what a stereo strip used to be: one input carrying both sides. Nothing makes
    // one any more - splitStereoInputs() turns one into a linked pair wherever it arrives from
    // (a session saved before, a saved patch) - but the engine still plays one correctly.
    bool isStereo() const noexcept { return inputB >= 0; }
    int numChannels() const noexcept { return isStereo() ? 2 : 1; }
};

// ---------------------------------------------------------------------------
// HOW LOUD THE FINISHED MIX SHOULD BE
//
// This is the single number that decides whether a DINE master sounds competitive next to
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
    Loud,              // -12 LUFS: as loud as DINE will aim without squashing the mix
    Count
};

inline constexpr std::array<const char*, int (DeliveryLoudness::Count)> kDeliveryLoudnessNames {
    "Match the purpose", "Broadcast (EBU R128)", "Broadcast (ATSC A/85)", "Podcast / archive",
    "Streaming", "YouTube / Facebook / Spotify", "As loud as it goes"
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
        case DeliveryLoudness::Loud:          return "As far as DINE will push without squashing the mix. Use when a stream has to cut through.";
        case DeliveryLoudness::FromPurpose:
        case DeliveryLoudness::Count:
        default:                              return "Whatever the mix's purpose asks for.";
    }
}

// ---------------------------------------------------------------------------
// The master's voicing: who the finished mix is for.
//
// A stream is heard on a phone speaker, on earbuds, in a car and on a television, and none
// of them hears the same balance. A voicing is a small, bounded tilt on the master - a low
// shelf, a presence band, a high shelf and a touch of density - chosen for the listener
// rather than for the room. It sits on top of the tuned mix the way a macro does: applied
// when the parameters are composed, never written into the kept mix, so TUNE MIX and REVERT
// are untouched by it and switching it is instant and reversible. Neutral is exactly the
// tuned mix. The numbers live in MixProfileData (MixProfile::voicing).
// ---------------------------------------------------------------------------
enum class MasterVoicing : int
{
    Neutral = 0,     // as tuned
    Warm,            // fuller low end, a softer top: living rooms, hi-fi, older ears
    Bright,          // more air and presence: laptops and small monitors that lack it
    VoiceFirst,      // the words first: a sermon, a podcast, a spoken service
    PhoneSpeakers,   // what a phone can reproduce, made to carry
    Earbuds,         // earbuds are bright and close: a little body, a little less edge
    Car,             // road noise takes the quiet parts: weight and presence
    TvSoundbar,      // a television or a soundbar: clear speech, no boom
    Count
};

inline constexpr std::array<const char*, int (MasterVoicing::Count)> kMasterVoicingNames {
    "As tuned", "Warm", "Bright", "Voice first", "Phone speakers", "Earbuds", "Car", "TV / soundbar"
};

inline constexpr const char* masterVoicingName (MasterVoicing v) noexcept
{
    const int i = int (v);
    return (i >= 0 && i < int (MasterVoicing::Count)) ? kMasterVoicingNames[size_t (i)] : "?";
}

inline const char* masterVoicingHint (MasterVoicing v) noexcept
{
    switch (v)
    {
        case MasterVoicing::Warm:          return "Fuller low end and a softer top. For living rooms, hi-fi and older ears.";
        case MasterVoicing::Bright:        return "More air and presence. For laptops and small speakers that have none of their own.";
        case MasterVoicing::VoiceFirst:    return "The words come first. For a sermon, a podcast or a spoken service.";
        case MasterVoicing::PhoneSpeakers: return "What a phone can actually reproduce, made to carry. The low end it cannot play is taken out of its way.";
        case MasterVoicing::Earbuds:       return "Earbuds are bright and close. A little more body, a little less edge.";
        case MasterVoicing::Car:           return "Road noise eats the quiet parts. Weight underneath and presence on top.";
        case MasterVoicing::TvSoundbar:    return "Clear speech and no boom, for a television or a soundbar.";
        case MasterVoicing::Neutral:
        case MasterVoicing::Count:
        default:                           return "Exactly the mix TUNE MIX built.";
    }
}

// Everything the user decided: which inputs are what, what the mix is for, which sound.
struct MixSession
{
    std::string name = "Sunday";
    StyleProfileId profile = StyleProfileId::ModernGospel;
    // A church streams. "Church Broadcast" is the television spec (-23 LUFS, EBU R128) and it is
    // about 9 dB under what a platform normalises to, which on a phone is simply quiet - so the
    // mix a session starts with is the stream's (-14 LUFS, -1 dBTP), and the broadcast spec is
    // there for the feeds that actually have to meet one. The purpose page says which is which.
    MixPurpose purpose = MixPurpose::Livestream;
    // SPEECH PRIORITY: while somebody is speaking, the band steps back. Off by default, on
    // purpose - a mix that moves on its own is a mix an engineer has to trust before they can
    // use it, and most services do not need it. Numbers in MixProfile::speechPriority.
    bool speechPriority = false;
    // SHARE THE MICS: the speaking microphones as an automatic mixer - the one speaking open,
    // the others back (MixParameters::AutoMix). Off by default for the same reason speech
    // priority is. Numbers in MixProfile::autoMix.
    bool autoMix = false;
    // How loud the finished mix should be. FromPurpose keeps the delivery role's own standard,
    // which is what every session made before this setting existed had, so nothing about an
    // old session changes when it is opened.
    DeliveryLoudness delivery = DeliveryLoudness::FromPurpose;
    // Who the finished mix is for. Neutral is the tuned mix untouched, which is what every
    // session made before this setting existed had.
    MasterVoicing voicing = MasterVoicing::Neutral;
    std::vector<InputAssignment> inputs;    // at most kMaxStrips are used

    ChannelRole masterRole() const noexcept { return masterRoleFor (purpose); }
    // The delivery target this session actually aims at, or 0 for "the role's own".
    float deliveryTargetLufs() const noexcept { return deliveryLoudnessLufs (delivery); }
    // The pinned focal source, or -1 for "whatever the listen says".
    int focusInput() const noexcept
    {
        for (size_t i = 0; i < inputs.size(); ++i) if (inputs[i].focus) return int (i);
        return -1;
    }
    // Pinning one unpins the rest: a mix has one thing it is built around. -1 clears it.
    void setFocus (int strip) noexcept
    {
        for (size_t i = 0; i < inputs.size(); ++i) inputs[i].focus = (int (i) == strip);
    }
    int numStrips() const noexcept { return int (inputs.size()) < kMaxStrips ? int (inputs.size()) : kMaxStrips; }
};

// The name each side of a stereo pair gets: "Keys" -> "Keys L" / "Keys R". A name that already
// says its side ("Keys L", "OH-R", "Piano Left") has that taken off first, so splitting a pair
// that is already named for its sides never makes "Keys L L".
inline std::string stereoSideName (const std::string& name, int side)
{
    std::string base = name;
    auto endsWith = [&] (const std::string& tail)
    {
        if (base.size() <= tail.size()) return false;
        for (size_t i = 0; i < tail.size(); ++i)
            if (std::tolower ((unsigned char) base[base.size() - tail.size() + i]) != tail[i]) return false;
        return true;
    };
    for (const char* tail : { " left", " right", " l", " r", "-l", "-r", "_l", "_r", ".l", ".r" })
        if (endsWith (tail)) { base.resize (base.size() - std::string (tail).size()); break; }
    if (base.empty()) base = name;
    return base + (side < 0 ? " L" : " R");
}

// Every stereo input made into two mono inputs, left then right, marked as a pair. The left
// keeps the input's place, its focus and its device channel; the right is its second channel.
// Returns whether anything was split. Idempotent: a session with no stereo input is untouched.
inline bool splitStereoInputs (MixSession& session)
{
    bool any = false;
    std::vector<InputAssignment> out;
    out.reserve (session.inputs.size() + 4);
    for (const auto& in : session.inputs)
    {
        if (! in.isStereo()) { out.push_back (in); continue; }
        any = true;
        InputAssignment left = in, right = in;
        left.inputB = -1;
        left.stereoSide = -1;
        left.name = stereoSideName (in.name, -1);
        right.inputA = in.inputB;
        right.inputB = -1;
        right.stereoSide = 1;
        right.focus = false;
        right.name = stereoSideName (in.name, 1);
        out.push_back (left);
        out.push_back (right);
    }
    if (any) session.inputs = std::move (out);
    return any;
}

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

// Whether an input is a strip on the console. An input with no source said (a multitrack file
// DINE could not name), one with no device channel, and a finished mix are listed but never
// processed - so an input's place in the list and its strip on the console are two different
// numbers whenever one of those sits above it. RoutingGraph::build decides by this, and so must
// everything that turns one into the other (stripsOfInputs).
inline bool inputHasStrip (const InputAssignment& in) noexcept
{
    return in.enabled && in.inputA >= 0 && roleFamily (in.role) != RoleFamily::Master;
}

// The strip each input is on, or -1 for one that has none: the same walk RoutingGraph::build makes.
inline std::vector<int> stripsOfInputs (const MixSession& session)
{
    std::vector<int> out (session.inputs.size(), -1);
    int next = 0;
    for (size_t i = 0; i < session.inputs.size() && next < kMaxStrips; ++i)
        if (inputHasStrip (session.inputs[i])) out[i] = next++;
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
        case RoleFamily::Bus:
        // Percussion is played with the kit and balanced against it, so it belongs on DRUMS -
        // pulling the drums down and leaving the congas where they were is not a balance.
        case RoleFamily::Percussion:
        case RoleFamily::Shaker:
        case RoleFamily::DrumPad:        return MixBus::Drums;
        case RoleFamily::ElectricBass:
        case RoleFamily::SynthBass:
        case RoleFamily::BassBus:        return MixBus::Bass;
        // The lead is its own group. Everything else that sings - the backing voices, the
        // choir, a vocal subgroup off the desk - is BGV.
        case RoleFamily::LeadVocal:      return MixBus::Lead;
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
        case RoleFamily::Saxophone:
        case RoleFamily::Brass:
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
        // The lead group is one voice (or a handful of them on one microphone each), so it is
        // glued like a vocal group rather than processed like a vocal channel: the de-essing,
        // the boom cut and the presence lift were already done on the microphone itself.
        case MixBus::Lead:   return ChannelRole::VocalBus;
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
