// Mix-level profile numbers for Modern Gospel (every other profile inherits them with
// the documented deltas beside each rule). Routing, sends, pans, balance and the
// relationship bounds MixPlanner works inside. Decision logic lives in src/Mix.
#include "MixProfileData.h"
#include "Core/Constants.h"
#include <algorithm>

namespace livemix
{

namespace MixProfile
{

FxType fxTypeForSlot (FxSlot slot)
{
    switch (slot)
    {
        case FxSlot::VocalPlate: return FxType::VocalPlate;
        case FxSlot::VocalDelay: return FxType::EighthDelay;
        case FxSlot::BgvHall:    return FxType::VocalHall;
        case FxSlot::SnarePlate: return FxType::SnarePlate;
        case FxSlot::DrumRoom:
        default:                 return FxType::DrumRoom;
    }
}

float defaultSendDb (StyleProfileId profile, RoleFamily family, FxSlot slot)
{
    // Modern Gospel: the lead sits in a plate with a short tempo delay tucked behind it,
    // the backing vocals and choir share a hall, the snare gets its plate, the toms a
    // room. Speech, bass, keys and guitars stay dry (the band already has space).
    float db = kSilenceDb;
    switch (family)
    {
        case RoleFamily::LeadVocal:
            if (slot == FxSlot::VocalPlate) db = -10.0f;
            if (slot == FxSlot::VocalDelay) db = -16.0f;
            break;
        case RoleFamily::BackingVocal:
            if (slot == FxSlot::BgvHall) db = -10.0f;
            break;
        case RoleFamily::Choir:
            if (slot == FxSlot::BgvHall) db = -9.0f;
            break;
        case RoleFamily::Snare:
            if (slot == FxSlot::SnarePlate) db = -14.0f;
            break;
        case RoleFamily::Tom:
            if (slot == FxSlot::DrumRoom) db = -12.0f;
            break;
        default: break;
    }
    if (db <= kSilenceDb) return db;
    switch (profile)
    {
        case StyleProfileId::ModernWorship:
            // Worship: a little more hall on the backing vocals, a touch less delay on the lead.
            if (slot == FxSlot::BgvHall) db += 1.5f;
            if (slot == FxSlot::VocalDelay) db -= 2.0f;
            break;
        case StyleProfileId::RockBand:
            // Rock: the plate is shorter and further back, the drum room bigger, the delay tucked.
            if (slot == FxSlot::VocalPlate) db -= 2.0f;
            if (slot == FxSlot::VocalDelay) db -= 3.0f;
            if (slot == FxSlot::DrumRoom) db += 2.0f;
            break;
        case StyleProfileId::RnbHipHop:
            // R&B: the delay is part of the arrangement and the snare plate is the snare's tail.
            if (slot == FxSlot::VocalDelay) db += 3.0f;
            if (slot == FxSlot::SnarePlate) db += 2.0f;
            if (slot == FxSlot::DrumRoom) db = kSilenceDb;      // a tight kit, no artificial room
            break;
        case StyleProfileId::JazzAcoustic:
            // Jazz: no delay on a voice, a lighter plate, no plate on the snare: the real room does it.
            if (slot == FxSlot::VocalDelay) db = kSilenceDb;
            if (slot == FxSlot::VocalPlate) db -= 4.0f;
            if (slot == FxSlot::BgvHall) db -= 3.0f;
            if (slot == FxSlot::SnarePlate) db = kSilenceDb;
            if (slot == FxSlot::DrumRoom) db -= 4.0f;
            break;
        case StyleProfileId::TalkPodcast:
            // Talk: a voice is dry. The band in the breaks keeps a little of its plate.
            if (slot == FxSlot::VocalDelay) db = kSilenceDb;
            if (slot == FxSlot::VocalPlate) db -= 6.0f;
            if (slot == FxSlot::BgvHall) db -= 6.0f;
            break;
        case StyleProfileId::ModernGospel:
        case StyleProfileId::Count:
        default: break;
    }
    return db;
}

float defaultReturnDb (StyleProfileId, FxSlot)
{
    return 0.0f; // the sends carry the amount; returns stay at unity so SPACE scales one place
}

float reverbBeats (StyleProfileId profile, FxSlot slot)
{
    // Modern Gospel is dense and quick: the lead's plate has to be gone before the next line, the backing
    // hall may bloom a little longer because it sits behind them, and a drum room is an ambience, not a tail.
    float beats;
    switch (slot)
    {
        case FxSlot::VocalPlate: beats = 3.0f; break;
        case FxSlot::BgvHall:    beats = 4.0f; break;
        case FxSlot::SnarePlate: beats = 2.0f; break;
        case FxSlot::DrumRoom:   beats = 1.5f; break;
        default:                 beats = 0.0f; break;   // delays have no tail to fit
    }
    if (beats > 0.0f)
    {
        if (profile == StyleProfileId::ModernWorship) beats += 1.0f;                     // worship breathes: the tails run a beat longer
        if (profile == StyleProfileId::JazzAcoustic) beats += 1.0f;                      // a jazz room is allowed to ring
        if (profile == StyleProfileId::RockBand) beats = std::max (1.0f, beats - 1.0f);   // rock tails stay out of the next hit
        if (profile == StyleProfileId::TalkPodcast) beats = std::max (1.0f, beats - 1.0f);
    }
    return beats;
}

float defaultPan (ChannelRole role)
{
    switch (role)
    {
        case ChannelRole::OverheadLeft:  return -0.9f;
        case ChannelRole::OverheadRight: return  0.9f;
        case ChannelRole::RackTom:       return -0.3f;
        case ChannelRole::FloorTom:      return  0.35f;
        case ChannelRole::HiHat:         return -0.35f;
        default:                         return 0.0f;
    }
}

float spreadForRole (ChannelRole role)
{
    switch (roleFamily (role))
    {
        case RoleFamily::BackingVocal:   return 0.7f;   // BGVs fan out behind the lead
        case RoleFamily::Choir:          return 0.6f;
        case RoleFamily::Tom:            return 0.7f;   // several toms walk left to right
        case RoleFamily::Overhead:       return 0.9f;   // two mono overheads = a pair, wide but off the wall
        case RoleFamily::Room:           return 0.8f;
        // Two crowd microphones are a pair across the room: as wide as the overheads, and for
        // the same reason - it is the width that makes a congregation sound like one.
        case RoleFamily::Ambience:       return 0.9f;
        case RoleFamily::AcousticGuitar:
        case RoleFamily::ElectricGuitar: return 0.5f;
        case RoleFamily::Piano:
        case RoleFamily::ElectricPiano:
        case RoleFamily::Organ:
        case RoleFamily::Synth:          return 0.3f;   // two keyboard players sit slightly apart
        default:                         return 0.0f;   // kick, snare, bass, lead, speech stay centred
    }
}

float defaultBusFaderDb (StyleProfileId, MixBus)
{
    return 0.0f;
}

float mixLevelTargetDb (StyleProfileId profile, RoleFamily family)
{
    // Processed pre-fader active RMS (dBFS) that gives the Modern Gospel balance when every fader
    // sits at 0: the lead vocal is the reference at -18, the kick and bass just under it, the snare
    // with them, keys and guitars under the voices, cymbals and room low. A backing voice is set on
    // its own here; the group rule below then holds all of them together behind the lead.
    float db;
    switch (family)
    {
        case RoleFamily::LeadVocal:      db = -18.0f; break;
        case RoleFamily::Speech:         db = -18.0f; break;
        case RoleFamily::BackingVocal:   db = -26.0f; break;
        case RoleFamily::Choir:          db = -25.0f; break;
        case RoleFamily::Kick:           db = -20.0f; break;
        case RoleFamily::Snare:          db = -21.0f; break;
        case RoleFamily::Tom:            db = -24.0f; break;
        case RoleFamily::HiHat:          db = -29.0f; break;
        case RoleFamily::Overhead:       db = -27.0f; break;
        case RoleFamily::Room:           db = -31.0f; break;
        // Ambience is felt before it is heard. Well under everything on the stage, so the
        // broadcast gets the building without the building competing with the band.
        case RoleFamily::Ambience:       db = -33.0f; break;
        // A horn sits with the band, a little under the voices it shares a band with.
        case RoleFamily::Saxophone:      db = -24.0f; break;
        case RoleFamily::ElectricBass:
        case RoleFamily::SynthBass:      db = -20.0f; break;
        case RoleFamily::Piano:
        case RoleFamily::ElectricPiano:  db = -25.0f; break;
        case RoleFamily::Organ:          db = -26.0f; break;
        case RoleFamily::Synth:          db = -28.0f; break;
        case RoleFamily::AcousticGuitar:
        case RoleFamily::ElectricGuitar: db = -27.0f; break;
        default:                         db = -24.0f; break;
    }
    switch (profile)
    {
        case StyleProfileId::ModernWorship:
            // Worship: guitars and pads a touch more forward, the choir a little further back.
            if (family == RoleFamily::AcousticGuitar || family == RoleFamily::ElectricGuitar || family == RoleFamily::Synth) db += 1.5f;
            if (family == RoleFamily::Choir) db -= 1.0f;
            // Worship wants more of the room: it is what stops a stream sounding like a rehearsal.
            if (family == RoleFamily::Ambience) db += 2.0f;
            break;
        case StyleProfileId::RockBand:
            // Rock: the guitars carry the song and the kit hits; keys and the room step back.
            if (family == RoleFamily::ElectricGuitar) db += 3.0f;
            if (family == RoleFamily::AcousticGuitar) db += 1.0f;
            if (family == RoleFamily::Kick || family == RoleFamily::Snare) db += 1.0f;
            if (family == RoleFamily::Overhead) db += 1.0f;
            if (family == RoleFamily::Piano || family == RoleFamily::ElectricPiano || family == RoleFamily::Organ || family == RoleFamily::Synth) db -= 1.0f;
            if (family == RoleFamily::Ambience) db -= 2.0f;
            break;
        case StyleProfileId::RnbHipHop:
            // R&B: the kick and the bass are the song, the keys wide and behind, the guitars back.
            if (family == RoleFamily::Kick) db += 1.5f;
            if (family == RoleFamily::ElectricBass || family == RoleFamily::SynthBass) db += 1.5f;
            if (family == RoleFamily::Snare) db += 0.5f;
            if (family == RoleFamily::AcousticGuitar || family == RoleFamily::ElectricGuitar) db -= 2.0f;
            if (family == RoleFamily::Overhead || family == RoleFamily::HiHat) db -= 1.0f;
            if (family == RoleFamily::Ambience) db -= 2.0f;
            break;
        case StyleProfileId::JazzAcoustic:
            // Jazz: the overheads carry the kit, the piano is forward, the voice sits with the band rather than over it.
            if (family == RoleFamily::Overhead) db += 3.0f;
            if (family == RoleFamily::Room) db += 2.0f;
            if (family == RoleFamily::Kick || family == RoleFamily::Snare) db -= 2.0f;
            if (family == RoleFamily::Tom) db -= 1.0f;
            if (family == RoleFamily::Piano) db += 3.0f;
            if (family == RoleFamily::AcousticGuitar) db += 2.0f;
            if (family == RoleFamily::ElectricBass) db -= 1.0f;
            if (family == RoleFamily::Saxophone) db += 2.0f;
            if (family == RoleFamily::LeadVocal) db -= 1.0f;
            if (family == RoleFamily::Ambience) db += 3.0f;
            break;
        case StyleProfileId::TalkPodcast:
            // Talk: the speaking voice is the reference and everything with strings or sticks is a bed under it.
            if (family == RoleFamily::Speech) db += 2.0f;
            if (family == RoleFamily::LeadVocal) db += 1.0f;
            if (family == RoleFamily::Kick || family == RoleFamily::Snare || family == RoleFamily::Tom
                || family == RoleFamily::HiHat || family == RoleFamily::Overhead || family == RoleFamily::Room) db -= 4.0f;
            if (family == RoleFamily::ElectricBass || family == RoleFamily::SynthBass) db -= 3.0f;
            if (family == RoleFamily::Piano || family == RoleFamily::ElectricPiano || family == RoleFamily::Organ || family == RoleFamily::Synth
                || family == RoleFamily::AcousticGuitar || family == RoleFamily::ElectricGuitar || family == RoleFamily::Saxophone) db -= 3.0f;
            if (family == RoleFamily::Ambience) db += 1.0f;
            break;
        case StyleProfileId::ModernGospel:
        case StyleProfileId::Count:
        default: break;
    }
    return db;
}

float stripPeakCeilingDb (StyleProfileId)
{
    return -3.0f;
}

const Relationships& relationships (StyleProfileId profile)
{
    static const Relationships gospel = []
    {
        Relationships r;
        r.busBelowVocalsDb[size_t (MixBus::Drums)]  = -1.0f;
        r.busBelowVocalsDb[size_t (MixBus::Bass)]   = -3.0f;
        r.busBelowVocalsDb[size_t (MixBus::Music)]  = -5.0f;
        r.busBelowVocalsDb[size_t (MixBus::Vocals)] = 0.0f;
        // The spoken word is the reason the room is there: when the pastor is on, the speech
        // group sits level with the singing group, never under it.
        r.busBelowVocalsDb[size_t (MixBus::Speech)] = 0.0f;
        // The room sits well under the singing: audible, never a competitor. This is the
        // number that decides whether a broadcast sounds like a service or like a crowd.
        r.busBelowVocalsDb[size_t (MixBus::Ambience)] = -12.0f;
        r.busBelowVocalsDb[size_t (MixBus::Master)] = 0.0f;
        return r;
    }();
    static const Relationships worship = []
    {
        Relationships r = gospel;
        r.busBelowVocalsDb[size_t (MixBus::Music)] = -3.5f;   // the band sits closer to the voices
        r.busBelowVocalsDb[size_t (MixBus::Ambience)] = -10.0f;
        r.backingBelowLeadDb = 4.0f;
        return r;
    }();
    static const Relationships rock = []
    {
        Relationships r = gospel;
        r.busBelowVocalsDb[size_t (MixBus::Drums)] = 0.0f;      // the kit sits level with the voices
        r.busBelowVocalsDb[size_t (MixBus::Bass)] = -2.0f;
        r.busBelowVocalsDb[size_t (MixBus::Music)] = -3.0f;     // the guitars are the song
        r.busBelowVocalsDb[size_t (MixBus::Ambience)] = -14.0f;
        r.backingBelowLeadDb = 6.0f;
        r.vocalPocketMaxCutDb = 3.0f;                            // the guitars have to make room, or nobody hears the words
        return r;
    }();
    static const Relationships rnb = []
    {
        Relationships r = gospel;
        r.busBelowVocalsDb[size_t (MixBus::Drums)] = 0.0f;
        r.busBelowVocalsDb[size_t (MixBus::Bass)] = -1.0f;      // the bass is the song
        r.busBelowVocalsDb[size_t (MixBus::Music)] = -5.0f;
        r.busBelowVocalsDb[size_t (MixBus::Ambience)] = -14.0f;
        r.bassHpfMinHz = 30.0f;                                  // the sub belongs to the bass here
        r.bassHpfMaxHz = 45.0f;
        r.backingBelowLeadDb = 4.0f;
        return r;
    }();
    static const Relationships jazz = []
    {
        Relationships r = gospel;
        r.busBelowVocalsDb[size_t (MixBus::Drums)] = -3.0f;
        r.busBelowVocalsDb[size_t (MixBus::Bass)] = -3.0f;
        r.busBelowVocalsDb[size_t (MixBus::Music)] = -2.0f;     // the band is the point
        r.busBelowVocalsDb[size_t (MixBus::Ambience)] = -9.0f;  // the room is welcome
        r.backingBelowLeadDb = 3.0f;
        r.vocalPocketMaxCutDb = 1.5f;                            // the piano keeps its tone
        r.tomGateMaxRangeWithOverheadsDb = 0.0f;                 // no gates on a jazz kit (the profile says so; this keeps a hand-set one gentle)
        return r;
    }();
    static const Relationships talk = []
    {
        Relationships r = gospel;
        r.busBelowVocalsDb[size_t (MixBus::Drums)] = -8.0f;     // a bed, not a band
        r.busBelowVocalsDb[size_t (MixBus::Bass)] = -8.0f;
        r.busBelowVocalsDb[size_t (MixBus::Music)] = -8.0f;
        r.busBelowVocalsDb[size_t (MixBus::Speech)] = 0.0f;
        r.busBelowVocalsDb[size_t (MixBus::Ambience)] = -12.0f;
        r.vocalPocketMaxCutDb = 3.0f;
        r.backingBelowLeadDb = 2.0f;                             // on a panel every voice is a lead
        return r;
    }();
    switch (profile)
    {
        case StyleProfileId::ModernWorship: return worship;
        case StyleProfileId::RockBand:      return rock;
        case StyleProfileId::RnbHipHop:     return rnb;
        case StyleProfileId::JazzAcoustic:  return jazz;
        case StyleProfileId::TalkPodcast:   return talk;
        case StyleProfileId::ModernGospel:
        case StyleProfileId::Count:
        default:                            return gospel;
    }
}

float compPeakRiseMs (StyleProfileId profile, ChannelRole role)
{
    const auto& r = relationships (profile);
    if (role == ChannelRole::DrumBus) return r.compPeakRisePercussiveMs;   // a drum mix from the console is still hits
    switch (roleFamily (role))
    {
        case RoleFamily::Kick:
        case RoleFamily::Snare:
        case RoleFamily::Tom:      return r.compPeakRiseCloseDrumMs;
        case RoleFamily::HiHat:
        case RoleFamily::Overhead: return r.compPeakRisePercussiveMs;
        case RoleFamily::Room:
        case RoleFamily::Ambience:
        case RoleFamily::AmbienceBus: return r.compPeakRiseRoomMs;
        default:                   return r.compPeakRiseSustainedMs;
    }
}

const AiRanges& aiRanges (StyleProfileId profile)
{
    static const AiRanges gospel;
    static const AiRanges worship = []
    {
        AiRanges a;
        // Modern Worship asks for less of everything at the extremes: the band sits closer
        // together, so a full-strength objective is a smaller move than it is in gospel.
        a.presenceDb = 2.0f;
        a.brightnessDb = 2.0f;
        a.bodyDb = 2.5f;
        a.faderDb = 2.0f;
        a.sendDb = 4.0f;
        return a;
    }();
    static const AiRanges jazz = []
    {
        AiRanges a;
        // Jazz and acoustic: a full-strength move is a small one; the band is not to be re-mixed by a sentence.
        a.presenceDb = 2.0f;
        a.bodyDb = 2.0f;
        a.faderDb = 2.0f;
        a.sendDb = 4.0f;
        a.compThresholdDb = 3.0f;
        return a;
    }();
    static const AiRanges talk = []
    {
        AiRanges a;
        // Talk: the moves that matter are on the voices, and they are small; the bed can move further.
        a.presenceDb = 3.0f;
        a.clarityDb = 3.5f;
        a.deEssRangeDb = 5.0f;
        a.faderDb = 3.0f;
        return a;
    }();
    switch (profile)
    {
        case StyleProfileId::ModernWorship: return worship;
        case StyleProfileId::JazzAcoustic:  return jazz;
        case StyleProfileId::TalkPodcast:   return talk;
        case StyleProfileId::RockBand:
        case StyleProfileId::RnbHipHop:
        case StyleProfileId::ModernGospel:
        case StyleProfileId::Count:
        default:                            return gospel;
    }
}

const AiBounds& aiBounds()
{
    // One set of bounds for every profile: a bound is a safety limit, not a sound.
    static const AiBounds bounds;
    return bounds;
}

const ReferenceBounds& referenceBounds (StyleProfileId profile)
{
    // One set of bounds for both profiles: how far a reference may pull a mix is a safety
    // limit, not a style. What differs between gospel and worship is where the mix starts.
    static const ReferenceBounds bounds;
    (void) profile;
    return bounds;
}

const MacroRanges& macroRanges (StyleProfileId profile)
{
    static const MacroRanges gospel;
    static const MacroRanges worship = []
    {
        MacroRanges m;
        m.drumBigLowDb = 2.0f;          // worship drums stay a little tighter even at Big
        m.energyPolishedThresholdDb = 4.0f;
        return m;
    }();
    static const MacroRanges rock = []
    {
        MacroRanges m;
        m.drumBigLowDb = 3.0f;
        m.drumBigSatDrive = 0.18f;      // Big on a rock kit is allowed to be dirty
        m.energyPolishedSatDrive = 0.12f;
        return m;
    }();
    static const MacroRanges rnb = []
    {
        MacroRanges m;
        m.bassHugeLowDb = 4.0f;         // Huge means the sub
        m.bassLowShelfHz = 60.0f;
        m.drumBigLowDb = 3.0f;
        return m;
    }();
    static const MacroRanges jazz = []
    {
        MacroRanges m;
        m.drumBigLowDb = 1.5f;
        m.drumBigSatDrive = 0.0f;       // nothing on a jazz kit is ever driven
        m.bassHugeSatDrive = 0.0f;
        m.energyPolishedSatDrive = 0.0f;
        m.energyPolishedThresholdDb = 3.0f;
        return m;
    }();
    static const MacroRanges talk = []
    {
        MacroRanges m;
        m.vocalBrightHighDb = 2.0f;
        m.energyPolishedThresholdDb = 4.0f;
        m.energyPolishedSatDrive = 0.0f;
        return m;
    }();
    switch (profile)
    {
        case StyleProfileId::ModernWorship: return worship;
        case StyleProfileId::RockBand:      return rock;
        case StyleProfileId::RnbHipHop:     return rnb;
        case StyleProfileId::JazzAcoustic:  return jazz;
        case StyleProfileId::TalkPodcast:   return talk;
        case StyleProfileId::ModernGospel:
        case StyleProfileId::Count:
        default:                            return gospel;
    }
}

const Voicing& voicing (StyleProfileId profile, MasterVoicing which)
{
    (void) profile;      // one table for both profiles today; the seam is here for when they differ
    static const std::array<Voicing, int (MasterVoicing::Count)> table = []
    {
        std::array<Voicing, int (MasterVoicing::Count)> t {};
        auto& warm = t[size_t (MasterVoicing::Warm)];
        warm.lowShelfHz = 180.0f;  warm.lowShelfDb = 1.5f;
        warm.highShelfHz = 8000.0f; warm.highShelfDb = -1.5f;
        auto& bright = t[size_t (MasterVoicing::Bright)];
        bright.lowShelfHz = 150.0f; bright.lowShelfDb = -1.0f;
        bright.presenceHz = 3000.0f; bright.presenceDb = 1.0f; bright.presenceQ = 0.8f;
        bright.highShelfHz = 6500.0f; bright.highShelfDb = 1.5f;
        auto& voice = t[size_t (MasterVoicing::VoiceFirst)];
        voice.lowShelfHz = 160.0f; voice.lowShelfDb = -1.5f;
        voice.presenceHz = 2600.0f; voice.presenceDb = 2.0f; voice.presenceQ = 0.9f;
        voice.highShelfHz = 9000.0f; voice.highShelfDb = -1.0f;
        auto& phone = t[size_t (MasterVoicing::PhoneSpeakers)];
        phone.lowShelfHz = 120.0f; phone.lowShelfDb = -2.5f;
        phone.presenceHz = 2400.0f; phone.presenceDb = 1.5f; phone.presenceQ = 0.8f;
        phone.highShelfHz = 7000.0f; phone.highShelfDb = 1.0f;
        phone.satDrive = 0.08f;
        auto& buds = t[size_t (MasterVoicing::Earbuds)];
        buds.lowShelfHz = 140.0f; buds.lowShelfDb = 1.0f;
        buds.presenceHz = 3500.0f; buds.presenceDb = -0.8f; buds.presenceQ = 1.0f;
        buds.highShelfHz = 10000.0f; buds.highShelfDb = -0.8f;
        auto& car = t[size_t (MasterVoicing::Car)];
        car.lowShelfHz = 110.0f; car.lowShelfDb = 2.0f;
        car.presenceHz = 2800.0f; car.presenceDb = 1.0f; car.presenceQ = 0.9f;
        car.highShelfHz = 7000.0f; car.highShelfDb = 1.0f;
        car.satDrive = 0.06f;
        auto& tv = t[size_t (MasterVoicing::TvSoundbar)];
        tv.lowShelfHz = 90.0f; tv.lowShelfDb = -1.5f;
        tv.presenceHz = 2500.0f; tv.presenceDb = 1.2f; tv.presenceQ = 0.9f;
        tv.highShelfHz = 9000.0f; tv.highShelfDb = -0.5f;
        return t;
    }();
    const int i = int (which);
    return table[size_t (i >= 0 && i < int (MasterVoicing::Count) ? i : 0)];
}

const LoudnessLift& loudnessLift()
{
    static const LoudnessLift lift;
    return lift;
}

} // namespace MixProfile
} // namespace livemix
