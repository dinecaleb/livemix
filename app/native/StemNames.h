#pragma once
#include <juce_core/juce_core.h>
#include "Core/ChannelRole.h"

namespace livemix
{

// Guesses what a recorded stem is from its file name ("Kick#09", "Vox2", "Full Drums", "Pastor").
// Shared by the offline stems tool and the app's recording playback. A guess is a starting point:
// the assignment page shows it and the user confirms or changes it.
namespace StemNames
{
    // A needle is matched as a substring unless `whole` is set, in which case it has to be a word of
    // its own ("OV L", "OV 1", "OVL"). Live desks label channels in abbreviations, and a two-letter
    // abbreviation matched anywhere inside a name is a trap: "ld" is inside "world", "bd" inside
    // "bdrum" is fine but inside "Holdback" is not.
    struct Guess { const char* needle; ChannelRole role; bool whole = false; };

    // Order matters: "tom l" before "tom", "full drums" before "drums", short words only as words.
    inline const Guess* guesses (int& count)
    {
        static const Guess table[] = {
            { "kick",  ChannelRole::KickIn }, { "bd", ChannelRole::KickIn, true },
            // Playback first: "tracks" has "rack" inside it, and a backing track read as a rack
            // tom got a gate, a tom's EQ and the drum room (the Praise stems, 2026-09-30).
            { "track", ChannelRole::SynthPad },
            { "snare", ChannelRole::SnareTop }, { "snr", ChannelRole::SnareTop }, { "sn", ChannelRole::SnareTop, true },
            { "hat",   ChannelRole::HiHat }, { "hh", ChannelRole::HiHat, true },
            { "tom l", ChannelRole::RackTom }, { "rack", ChannelRole::RackTom, true }, { "racktom", ChannelRole::RackTom }, { "tom 1", ChannelRole::RackTom }, { "tom1", ChannelRole::RackTom },
            { "tom r", ChannelRole::FloorTom }, { "floor", ChannelRole::FloorTom }, { "tom 2", ChannelRole::FloorTom }, { "tom2", ChannelRole::FloorTom },
            { "tom",   ChannelRole::RackTom },
            // Overheads. Live desks write them "OH", "OV", "OVH" or spell them out; the cymbal mics
            // (ride, crash) belong with them. Without these the kit arrives with no cymbals at all,
            // which is most of what a drum kit contributes above 8 kHz.
            { "overhead", ChannelRole::Overhead }, { "overhd", ChannelRole::Overhead }, { "ovhd", ChannelRole::Overhead },
            { "ovh", ChannelRole::Overhead, true }, { "ov", ChannelRole::Overhead, true }, { "oh", ChannelRole::Overhead, true },
            { "cymbal", ChannelRole::Overhead }, { "ride", ChannelRole::Overhead }, { "crash", ChannelRole::Overhead },
            { "full drums", ChannelRole::DrumBus }, { "drum mix", ChannelRole::DrumBus }, { "drums", ChannelRole::DrumBus },
            // "Room" on a live desk almost always means the drum room microphone, and it belongs
            // with the kit. Anything aimed at the people does not: a congregation microphone is
            // its own source with its own bus (2026-09), because it is never gated, never
            // pushed forward and turned up and down at moments nothing else moves at.
            { "congregation", ChannelRole::CrowdMic }, { "audience", ChannelRole::CrowdMic },
            { "crowd", ChannelRole::CrowdMic }, { "house mic", ChannelRole::CrowdMic },
            { "ambience", ChannelRole::AmbienceMic }, { "ambient", ChannelRole::AmbienceMic },
            { "amb", ChannelRole::AmbienceMic, true }, { "aud", ChannelRole::CrowdMic, true },
            { "room mic", ChannelRole::AmbienceMic },
            { "room",  ChannelRole::Room },
            // Horns. A saxophone is not a keyboard: it has its own family, its own honk to cut
            // and its own dynamics (see AmbienceStrategies.cpp).
            // Percussion and brass. "conga" before "bongo" matters no more than any other
            // order here, but "tamb" must come before nothing else claims it, and "horn"
            // stays last so "french horn" and "horns" both land on the section.
            { "conga", ChannelRole::Congas }, { "bongo", ChannelRole::Bongos },
            { "djembe", ChannelRole::Djembe }, { "cajon", ChannelRole::Djembe },
            { "timbale", ChannelRole::Timbales }, { "timbs", ChannelRole::Timbales },
            { "shaker", ChannelRole::Shaker }, { "tambourine", ChannelRole::Shaker },
            { "tamb", ChannelRole::Shaker }, { "egg", ChannelRole::Shaker },
            { "perc", ChannelRole::Congas },
            { "trumpet", ChannelRole::Trumpet }, { "tpt", ChannelRole::Trumpet },
            { "trombone", ChannelRole::Trombone }, { "tbone", ChannelRole::Trombone },
            { "brass", ChannelRole::BrassSection }, { "horns", ChannelRole::BrassSection },
            { "alto sax", ChannelRole::SaxAlto }, { "tenor sax", ChannelRole::SaxTenor },
            { "bari sax", ChannelRole::SaxBari }, { "baritone sax", ChannelRole::SaxBari },
            { "alto", ChannelRole::SaxAlto }, { "tenor", ChannelRole::SaxTenor }, { "bari", ChannelRole::SaxBari },
            { "saxophone", ChannelRole::SaxTenor }, { "sax", ChannelRole::SaxTenor },
            { "bass",  ChannelRole::BassDI },
            // A sample pad (Roland SPD, "Drum Pad", "Samples"): electronic hits and loops played with the kit.
            // Before "pad", which is a synth pad on its own.
            { "spd", ChannelRole::DrumPad, true }, { "drum pad", ChannelRole::DrumPad }, { "drumpad", ChannelRole::DrumPad },
            { "sample pad", ChannelRole::DrumPad }, { "sampler", ChannelRole::DrumPad }, { "samples", ChannelRole::DrumPad },
            { "organ", ChannelRole::Organ }, { "keys", ChannelRole::Piano }, { "piano", ChannelRole::Piano }, { "pad", ChannelRole::SynthPad },
            // Playback from the stage or the booth: a loop, a backing track, a click, the computer feed
            // a desk calls "Computer Audio" or "USB". It is music, so it joins the music bus.
            { "playback", ChannelRole::SynthPad }, { "loop", ChannelRole::SynthPad },
            { "click", ChannelRole::SynthPad }, { "synth", ChannelRole::SynthLead },
            { "computer", ChannelRole::SynthPad }, { "usb", ChannelRole::SynthPad, true }, { "media", ChannelRole::SynthPad },
            { "video", ChannelRole::SynthPad }, { "laptop", ChannelRole::SynthPad },
            { "acoustic", ChannelRole::AcousticGuitar }, { "gtr", ChannelRole::ElectricGuitarClean }, { "guit", ChannelRole::ElectricGuitarClean },
            { "lead",  ChannelRole::LeadVocal }, { "ld", ChannelRole::LeadVocal, true },
            { "choir", ChannelRole::Choir },
            { "vox",   ChannelRole::BackingVocal }, { "bgv", ChannelRole::BackingVocal }, { "bv", ChannelRole::BackingVocal, true }, { "vocal", ChannelRole::BackingVocal },
            // WHAT IT IS SPOKEN INTO COMES FIRST. A stage plot that reads "Pastor lapel" is two
            // facts, and the second one is the one that changes the chain. The table is walked
            // in order and the first needle found in the name wins, so these have to be met
            // before "pastor" is.
            //
            // Only words that mean one thing on a stage plot are here: no "hh" (a hi-hat, and
            // it is spoken for above), no "clip" (a clip is a piece of audio), and no
            // microphone brands - a DPA is a headset on one plot and a piano mic on the next.
            { "lapel", ChannelRole::SpeechLapel }, { "lavalier", ChannelRole::SpeechLapel },
            { "lav", ChannelRole::SpeechLapel, true },
            { "headset", ChannelRole::SpeechHeadset }, { "headworn", ChannelRole::SpeechHeadset },
            { "earset", ChannelRole::SpeechHeadset }, { "countryman", ChannelRole::SpeechHeadset },
            { "handheld", ChannelRole::SpeechHandheld }, { "roving", ChannelRole::SpeechHandheld },
            { "podium", ChannelRole::SpeechLectern }, { "pulpit", ChannelRole::SpeechLectern },
            { "lectern", ChannelRole::SpeechLectern }, { "gooseneck", ChannelRole::SpeechLectern },
            // Anyone who talks to the room: the pastor, the host, the MC, whoever is at the
            // lectern - when the name does not say what they are holding.
            { "pastor", ChannelRole::Speech }, { "speech", ChannelRole::Speech }, { "spk", ChannelRole::Speech, true },
            { "talk", ChannelRole::Speech }, { "preach", ChannelRole::Speech }, { "sermon", ChannelRole::Speech },
            { "host", ChannelRole::Speech, true }, { "mc", ChannelRole::Speech, true }, { "emcee", ChannelRole::Speech },
            { "announc", ChannelRole::Speech },
        };
        count = int (sizeof (table) / sizeof (table[0]));
        return table;
    }

    // Calls fn for every word in the name: a run of letters or digits ("OV L #3" -> "ov", "l").
    template <typename Fn>
    inline void forEachWord (const juce::String& name, Fn&& fn)
    {
        const int n = name.length();
        for (int i = 0; i < n;)
        {
            while (i < n && ! juce::CharacterFunctions::isLetterOrDigit (name[i])) ++i;
            const int start = i;
            while (i < n && juce::CharacterFunctions::isLetterOrDigit (name[i])) ++i;
            if (i > start && fn (name.substring (start, i))) return;
        }
    }

    // "OV" matches "OV", "OV L", "OV 2" and "OVL" - a channel number or a side letter is still that word.
    inline bool containsWord (const juce::String& name, const juce::String& needle)
    {
        bool found = false;
        forEachWord (name, [&] (const juce::String& word)
        {
            if (word == needle) { found = true; return true; }
            if (word.startsWith (needle))
            {
                const juce::String rest = word.substring (needle.length());
                if (rest == "l" || rest == "r" || rest.containsOnly ("0123456789")) { found = true; return true; }
            }
            return false;
        });
        return found;
    }

    // -1 left, +1 right, 0 neither: "OV L", "OH Left", "OVR".
    inline int sideOf (const juce::String& name, const juce::String& needle)
    {
        int side = 0;
        forEachWord (name, [&] (const juce::String& word)
        {
            if (word == "l" || word == "left")  { side = -1; return true; }
            if (word == "r" || word == "right") { side =  1; return true; }
            if (word.startsWith (needle))
            {
                const juce::String rest = word.substring (needle.length());
                if (rest == "l") { side = -1; return true; }
                if (rest == "r") { side =  1; return true; }
            }
            return false;
        });
        return side;
    }

    // "Kick#09" -> "Kick": the display name without a take or track number suffix.
    inline juce::String cleanName (const juce::String& fileNameWithoutExtension)
    {
        return fileNameWithoutExtension.upToLastOccurrenceOf ("#", false, false).trim();
    }

    inline bool guessRole (const juce::String& fileNameWithoutExtension, ChannelRole& role)
    {
        const juce::String name = cleanName (fileNameWithoutExtension).toLowerCase();
        int count = 0;
        const Guess* table = guesses (count);
        for (int i = 0; i < count; ++i)
        {
            const juce::String needle (table[i].needle);
            if (table[i].whole ? ! containsWord (name, needle) : ! name.contains (needle)) continue;
            role = table[i].role;
            // The snare's other microphone: "SNR BM", "Snare Bottom", "SN Btm", "Snare Under".
            if (role == ChannelRole::SnareTop)
            {
                bool bottom = false;
                forEachWord (name, [&] (const juce::String& w)
                {
                    bottom = w == "bm" || w == "bot" || w == "btm" || w == "bottom" || w == "under" || w == "bt";
                    return bottom;
                });
                if (bottom) role = ChannelRole::SnareBottom;
            }
            // A pair of mono overheads is named by its side, and the side is where it belongs in the image.
            if (role == ChannelRole::Overhead)
            {
                const int side = sideOf (name, needle);
                if (side < 0) role = ChannelRole::OverheadLeft;
                else if (side > 0) role = ChannelRole::OverheadRight;
            }
            return true;
        }
        return false;
    }
}

} // namespace livemix
