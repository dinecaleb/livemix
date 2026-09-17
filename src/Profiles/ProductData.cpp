// The Dine channel products: which sources each one handles, the five Simple
// knobs and the plain words shown to the user. Numbers for the sound live in
// ProfileData.cpp; the knob-to-parameter mapping lives in MacroMapping.cpp.
#include "Core/ProductDefinition.h"
#include "State/ParameterIDs.h"

namespace livemix
{

namespace
{
    MacroSpec macro (const char* id, const char* label, const char* tooltip, float lo = 0.0f, float hi = 100.0f, float def = 50.0f)
    {
        MacroSpec m; m.id = id; m.label = label; m.tooltip = tooltip; m.minValue = lo; m.maxValue = hi; m.defaultValue = def; return m;
    }

    ProductDefinition buildDrums()
    {
        ProductDefinition d;
        d.product = Product::Drums;
        d.name = "Dine Drums"; d.shortName = "DRUMS"; d.sourceCaption = "SOURCE";
        d.stateType = "LiveMixDrumsState"; d.presetType = "LiveMixPreset"; d.presetFolder = "Drums";
        d.defaultGroup = "Drum Kit 1";
        d.playerPrompt = "have the drummer play normally";
        d.sourceNoun = "the drum"; d.mixNoun = "a drum mix"; d.eventNoun = "hit";
        d.hasKit = true;
        d.defaultRole = ChannelRole::KickIn;
        for (int i = int (ChannelRole::KickIn); i <= int (ChannelRole::DrumBus); ++i) d.roles.push_back (ChannelRole (i));
        d.macros = {
            macro (ParamID::punch,     "PUNCH",  "How hard the drum hits you. Up = more slam and snap, down = softer and more natural. 50 is the Dine starting point."),
            macro (ParamID::body,      "BODY",   "Weight and fullness. Up = bigger and rounder, down = lighter and tighter."),
            macro (ParamID::attack,    "ATTACK", "How much you hear the stick or beater. Up = more click and definition, down = softer."),
            macro (ParamID::character, "TONE",   "Clean to aggressive. Left = smooth and clean, right = gritty and forward.", 0.0f, 1.0f, 0.35f),
            macro (ParamID::bleed,     "BLEED",  "How much of the other drums and cymbals leak into this mic. Up = cleaner, down = more natural. 0 switches the gate off.")
        };
        d.stages = { ChainStage::Input, ChainStage::Gate, ChainStage::EQ, ChainStage::Comp, ChainStage::Transient, ChainStage::Output };
        return d;
    }

    ProductDefinition buildVocals()
    {
        ProductDefinition d;
        d.product = Product::Vocals;
        d.name = "Dine Vocals"; d.shortName = "VOCALS"; d.sourceCaption = "SOURCE";
        d.stateType = "LiveMixVocalsState"; d.presetType = "LiveMixVocalsPreset"; d.presetFolder = "Vocals";
        d.defaultGroup = "Vocals 1";
        d.playerPrompt = "have the singer sing normally";
        d.sourceNoun = "the voice"; d.mixNoun = "a vocal mix"; d.eventNoun = "phrase";
        d.defaultRole = ChannelRole::LeadVocal;
        d.roles = { ChannelRole::LeadVocal, ChannelRole::BackingVocal, ChannelRole::Choir, ChannelRole::Speech, ChannelRole::VocalBus };
        d.macros = {
            macro (ParamID::warmth,  "WARMTH",   "Fullness of the voice. Up = rounder and warmer, down = thinner and lighter. 50 is the Dine starting point."),
            macro (ParamID::clarity, "CLARITY",  "How clearly the words come through. Up = more presence and air, down = softer and darker."),
            macro (ParamID::smooth,  "SMOOTH",   "Takes the sharp edge off S sounds and harsh notes. Up = smoother, down = more bite. 0 switches the S control off."),
            macro (ParamID::steady,  "STEADY",   "Keeps quiet and loud words at the same level. Up = more even, down = more natural ups and downs."),
            macro (ParamID::cleanup, "CLEAN-UP", "Removes stage rumble and other sounds picked up between phrases. Up = cleaner, down = more open. 0 switches the clean-up off.")
        };
        d.stages = { ChainStage::Input, ChainStage::Gate, ChainStage::EQ, ChainStage::DeEss, ChainStage::Comp, ChainStage::Output };
        return d;
    }

    ProductDefinition buildKeys()
    {
        ProductDefinition d;
        d.product = Product::Keys;
        d.name = "Dine Keys"; d.shortName = "KEYS"; d.sourceCaption = "SOURCE";
        d.stateType = "LiveMixKeysState"; d.presetType = "LiveMixKeysPreset"; d.presetFolder = "Keys";
        d.defaultGroup = "Keys 1";
        d.playerPrompt = "have the keys player play normally";
        d.sourceNoun = "the keys"; d.mixNoun = "a keys mix"; d.eventNoun = "note";
        d.defaultRole = ChannelRole::Piano;
        d.roles = { ChannelRole::Piano, ChannelRole::ElectricPiano, ChannelRole::Organ, ChannelRole::SynthPad, ChannelRole::SynthLead, ChannelRole::KeysBus };
        d.macros = {
            macro (ParamID::warmth,  "WARMTH",   "Low-end weight. Up = fuller and warmer, down = lighter. 50 is the Dine starting point."),
            macro (ParamID::shine,   "SHINE",    "Sparkle on top. Up = brighter and more open, down = softer and darker."),
            macro (ParamID::cleanup, "CLEAN-UP", "Clears mud so the keys don't fight the bass and vocals. Up = cleaner, down = fuller."),
            macro (ParamID::steady,  "STEADY",   "Keeps soft and loud playing at the same level. Up = more even, down = more natural."),
            macro (ParamID::width,   "WIDTH",    "How wide the keys sit in the stereo picture. Down = narrower and more focused, up = wider.")
        };
        d.stages = { ChainStage::Input, ChainStage::EQ, ChainStage::Comp, ChainStage::Width, ChainStage::Output };
        return d;
    }

    ProductDefinition buildMaster()
    {
        ProductDefinition d;
        d.product = Product::Master;
        d.name = "Dine Master"; d.shortName = "MASTER"; d.sourceCaption = "OUTPUT";
        d.stateType = "LiveMixMasterState"; d.presetType = "LiveMixMasterPreset"; d.presetFolder = "Master";
        d.defaultGroup = "Master";
        d.playerPrompt = "have the whole band play a song";
        d.sourceNoun = "the band"; d.mixNoun = "the final mix"; d.eventNoun = "peak";
        d.hasLoudness = true;
        d.defaultRole = ChannelRole::MasterStream;
        d.roles = { ChannelRole::MasterStream, ChannelRole::MasterBroadcast, ChannelRole::MasterRecording, ChannelRole::MasterRoom };
        d.macros = {
            macro (ParamID::warmth,  "WARMTH",  "Low-end weight of the whole mix. Up = fuller, down = leaner. 50 is the Dine starting point."),
            macro (ParamID::clarity, "CLARITY", "Openness on top. Up = more detail and air, down = smoother."),
            macro (ParamID::glue,    "GLUE",    "Makes the band sound like one unit. Up = tighter and more together, down = more open and dynamic."),
            macro (ParamID::loud,    "LOUD",    "Overall loudness sent out. Up = louder (the safety limiter keeps it from clipping), down = more headroom."),
            macro (ParamID::width,   "WIDTH",   "Stereo width of the mix. Down = narrower and safer for mono, up = wider.")
        };
        d.stages = { ChainStage::Input, ChainStage::EQ, ChainStage::Comp, ChainStage::Width, ChainStage::Limiter, ChainStage::Output };
        return d;
    }

    ProductDefinition buildGuitar()
    {
        ProductDefinition d;
        d.product = Product::Guitar;
        d.name = "Dine Guitar"; d.shortName = "GUITAR"; d.sourceCaption = "SOURCE";
        d.stateType = "LiveMixGuitarState"; d.presetType = "LiveMixGuitarPreset"; d.presetFolder = "Guitar";
        d.defaultGroup = "Guitars 1";
        d.playerPrompt = "have the guitarist play normally";
        d.sourceNoun = "the guitar"; d.mixNoun = "a guitar mix"; d.eventNoun = "note";
        d.defaultRole = ChannelRole::AcousticGuitar;
        d.roles = { ChannelRole::AcousticGuitar, ChannelRole::ElectricGuitarClean, ChannelRole::ElectricGuitarDrive, ChannelRole::GuitarBus };
        d.macros = {
            macro (ParamID::warmth,  "WARMTH",   "Body and fullness. Up = rounder and warmer, down = lighter and tighter. 50 is the Dine starting point."),
            macro (ParamID::clarity, "CLARITY",  "How clearly the picking and the chords come through. Up = more definition and sparkle, down = softer and darker."),
            macro (ParamID::smooth,  "SMOOTH",   "Takes the edge off pick click, pickup quack and amp fizz. Up = smoother, down = more bite."),
            macro (ParamID::steady,  "STEADY",   "Keeps strums and single notes at the same level. Up = more even, down = more natural ups and downs."),
            macro (ParamID::cleanup, "CLEAN-UP", "Removes amp hum, stage rumble and noise between phrases. Up = cleaner, down = more open. 0 switches the clean-up off.")
        };
        d.stages = { ChainStage::Input, ChainStage::Gate, ChainStage::EQ, ChainStage::Comp, ChainStage::Width, ChainStage::Output };
        return d;
    }

    ProductDefinition buildBass()
    {
        ProductDefinition d;
        d.product = Product::Bass;
        d.name = "Dine Bass"; d.shortName = "BASS"; d.sourceCaption = "SOURCE";
        d.stateType = "LiveMixBassState"; d.presetType = "LiveMixBassPreset"; d.presetFolder = "Bass";
        d.defaultGroup = "Bass 1";
        d.playerPrompt = "have the bass player play normally";
        d.sourceNoun = "the bass"; d.mixNoun = "a bass mix"; d.eventNoun = "note";
        d.defaultRole = ChannelRole::BassDI;
        d.roles = { ChannelRole::BassDI, ChannelRole::BassAmp, ChannelRole::SynthBass, ChannelRole::BassBus };
        d.macros = {
            macro (ParamID::warmth,  "WARMTH",   "Low-end weight. Up = deeper and fuller, down = tighter and leaner. 50 is the Dine starting point."),
            macro (ParamID::clarity, "CLARITY",  "How clearly each note and the fingers come through. Up = more definition, down = rounder and darker."),
            macro (ParamID::grit,    "GRIT",     "A little drive so the bass is heard on small speakers and phones. Up = more growl, down = cleaner. 0 switches the drive off."),
            macro (ParamID::steady,  "STEADY",   "Keeps every note at the same level so the low end never jumps. Up = more even, down = more natural."),
            macro (ParamID::cleanup, "CLEAN-UP", "Removes rumble below the lowest note and hum or noise between notes. Up = cleaner, down = more open. 0 switches the clean-up off.")
        };
        d.stages = { ChainStage::Input, ChainStage::Gate, ChainStage::EQ, ChainStage::Comp, ChainStage::Output };
        return d;
    }
}

const ProductDefinition& productDefinition (Product p)
{
    static const ProductDefinition drums = buildDrums();
    static const ProductDefinition vocals = buildVocals();
    static const ProductDefinition keys = buildKeys();
    static const ProductDefinition master = buildMaster();
    static const ProductDefinition guitar = buildGuitar();
    static const ProductDefinition bass = buildBass();
    switch (p)
    {
        case Product::Vocals: return vocals;
        case Product::Keys:   return keys;
        case Product::Master: return master;
        case Product::Guitar: return guitar;
        case Product::Bass:   return bass;
        case Product::Drums:
        default:              return drums;
    }
}

const char* roleHint (ChannelRole r) noexcept
{
    switch (r)
    {
        case ChannelRole::SynthPad:        return "Soft and wide · sits behind the band";
        case ChannelRole::SynthLead:       return "Clear and forward · never harsh";
        case ChannelRole::MasterBroadcast: return "Broadcast loudness · true-peak safe · consistent";
        case ChannelRole::MasterStream:    return "Livestream loudness · clear and full on phones and TVs";
        case ChannelRole::MasterRecording: return "Headroom kept · gentle glue · nothing squashed";
        case ChannelRole::MasterRoom:      return "Room feed · safety limiting only";
        case ChannelRole::ElectricGuitarClean: return "Clean and clear · no boom · sits beside the keys";
        case ChannelRole::ElectricGuitarDrive: return "Full amp tone · fizz tamed · level held";
        case ChannelRole::BassAmp:             return "Full amp tone · no boom · every note the same level";
        case ChannelRole::CrowdMic:            return "The congregation · never gated · felt, not loud";
        case ChannelRole::AmbienceMic:         return "The room itself · depth under the stage · never gated";
        case ChannelRole::SaxAlto:             return "Bright and singing · honk removed · sits beside the voice";
        case ChannelRole::SaxTenor:            return "Warm and reedy · honk removed · wails stay in control";
        case ChannelRole::SaxBari:             return "Weight and growl · honk removed · clear of the bass";
        default: break;
    }
    switch (roleFamily (r))
    {
        case RoleFamily::Kick:         return "Tight low end · controlled beater click · deep body";
        case RoleFamily::Snare:        return "Balanced attack · controlled cymbal bleed · full body";
        case RoleFamily::HiHat:        return "Crisp detail · no harshness · bleed kept in check";
        case RoleFamily::Tom:          return "Round tone · controlled sympathetic ring";
        case RoleFamily::Overhead:     return "Open cymbals · natural kit image";
        case RoleFamily::Room:         return "Depth and glue without wash";
        case RoleFamily::Bus:          return "Cohesive kit · punch preserved";
        case RoleFamily::LeadVocal:    return "Every word clear · warm · never harsh · steady level";
        case RoleFamily::BackingVocal: return "Blends behind the lead · smooth · controlled";
        case RoleFamily::Choir:        return "Big and natural · rumble removed · gentle control";
        case RoleFamily::Speech:       return "Clear speech · no boom · no sharp S · even level";
        case RoleFamily::VocalBus:     return "All voices as one · smooth top · gentle glue";
        case RoleFamily::Piano:        return "Natural and wide · no mud · room to breathe";
        case RoleFamily::ElectricPiano:return "Warm and round · bark controlled";
        case RoleFamily::Organ:        return "Full and warm · drive kept musical";
        case RoleFamily::Synth:        return "Clear · controlled · fits around the band";
        case RoleFamily::KeysBus:      return "All keys as one · clear of the vocals";
        case RoleFamily::Master:       return "Loud enough · never clipped · one mix everywhere";
        case RoleFamily::AcousticGuitar: return "Bright and natural · boom controlled · strums stay even";
        case RoleFamily::ElectricGuitar: return "Amp tone kept · mud and fizz tamed · steady level";
        case RoleFamily::GuitarBus:    return "All guitars as one · clear of the vocals";
        case RoleFamily::ElectricBass: return "Deep and even · every note clear · heard on small speakers";
        case RoleFamily::SynthBass:    return "Sub kept clean · never jumps · fits with the kick";
        case RoleFamily::BassBus:      return "All the bass as one · solid low end";
        case RoleFamily::Ambience:     return "The room and the people in it · never gated · felt, not loud";
        case RoleFamily::AmbienceBus:  return "The whole room as one · lifted between songs, down under the sermon";
        case RoleFamily::Saxophone:    return "Reedy and singing · honk removed · shares the voice's air";
        case RoleFamily::Count:
        default:                       return "";
    }
}

int roleIndexInProduct (const ProductDefinition& d, ChannelRole r) noexcept
{
    for (size_t i = 0; i < d.roles.size(); ++i) if (d.roles[i] == r) return int (i);
    return 0;
}

ChannelRole roleFromProductIndex (const ProductDefinition& d, int index) noexcept
{
    if (index >= 0 && index < int (d.roles.size())) return d.roles[size_t (index)];
    return d.defaultRole;
}

} // namespace livemix
