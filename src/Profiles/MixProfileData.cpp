// Mix-level profile numbers for Modern Gospel (Modern Worship inherits them with
// the documented deltas at the bottom). Routing, sends, pans, balance and the
// relationship bounds MixPlanner works inside. Decision logic lives in src/Mix.
#include "MixProfileData.h"
#include "Core/Constants.h"

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
    if (profile == StyleProfileId::ModernWorship && db > kSilenceDb)
    {
        // Worship: a little more hall on the backing vocals, a touch less delay on the lead.
        if (slot == FxSlot::BgvHall) db += 1.5f;
        if (slot == FxSlot::VocalDelay) db -= 2.0f;
    }
    return db;
}

float defaultReturnDb (StyleProfileId, FxSlot)
{
    return 0.0f; // the sends carry the amount; returns stay at unity so SPACE scales one place
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
        case RoleFamily::Overhead:       return 1.0f;   // two mono overheads = a pair
        case RoleFamily::Room:           return 0.8f;
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
    // Processed pre-fader peaks (dBFS) that give the Modern Gospel balance when every
    // fader sits at 0: the lead vocal is the reference, the kick and snare just under
    // it, bass a little below, keys and guitars under the vocals, cymbals and room low.
    float db;
    switch (family)
    {
        case RoleFamily::LeadVocal:      db = -8.0f;  break;
        case RoleFamily::Speech:         db = -8.0f;  break;
        case RoleFamily::BackingVocal:   db = -14.0f; break;
        case RoleFamily::Choir:          db = -13.0f; break;
        case RoleFamily::Kick:           db = -9.0f;  break;
        case RoleFamily::Snare:          db = -10.0f; break;
        case RoleFamily::Tom:            db = -13.0f; break;
        case RoleFamily::HiHat:          db = -22.0f; break;
        case RoleFamily::Overhead:       db = -17.0f; break;
        case RoleFamily::Room:           db = -22.0f; break;
        case RoleFamily::ElectricBass:
        case RoleFamily::SynthBass:      db = -10.0f; break;
        case RoleFamily::Piano:
        case RoleFamily::ElectricPiano:  db = -13.0f; break;
        case RoleFamily::Organ:          db = -14.0f; break;
        case RoleFamily::Synth:          db = -16.0f; break;
        case RoleFamily::AcousticGuitar:
        case RoleFamily::ElectricGuitar: db = -15.0f; break;
        default:                         db = -12.0f; break;
    }
    if (profile == StyleProfileId::ModernWorship)
    {
        // Worship: guitars and pads a touch more forward, the choir a little further back.
        if (family == RoleFamily::AcousticGuitar || family == RoleFamily::ElectricGuitar || family == RoleFamily::Synth) db += 1.5f;
        if (family == RoleFamily::Choir) db -= 1.0f;
    }
    return db;
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
        r.busBelowVocalsDb[size_t (MixBus::Master)] = 0.0f;
        return r;
    }();
    static const Relationships worship = []
    {
        Relationships r = gospel;
        r.busBelowVocalsDb[size_t (MixBus::Music)] = -3.5f;   // the band sits closer to the voices
        r.backingBelowLeadDb = 4.0f;
        return r;
    }();
    return profile == StyleProfileId::ModernWorship ? worship : gospel;
}

float compPeakRiseMs (StyleProfileId profile, ChannelRole role)
{
    const auto& r = relationships (profile);
    if (role == ChannelRole::DrumBus) return r.compPeakRisePercussiveMs;   // a drum mix from the console is still hits
    switch (roleFamily (role))
    {
        case RoleFamily::Kick:
        case RoleFamily::Snare:
        case RoleFamily::Tom:
        case RoleFamily::HiHat:
        case RoleFamily::Overhead: return r.compPeakRisePercussiveMs;
        case RoleFamily::Room:     return r.compPeakRiseRoomMs;
        default:                   return r.compPeakRiseSustainedMs;
    }
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
    return profile == StyleProfileId::ModernWorship ? worship : gospel;
}

} // namespace MixProfile
} // namespace livemix
