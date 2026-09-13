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
            { "snare", ChannelRole::SnareTop }, { "sn", ChannelRole::SnareTop, true },
            { "hat",   ChannelRole::HiHat }, { "hh", ChannelRole::HiHat, true },
            { "tom l", ChannelRole::RackTom }, { "rack", ChannelRole::RackTom }, { "tom 1", ChannelRole::RackTom }, { "tom1", ChannelRole::RackTom },
            { "tom r", ChannelRole::FloorTom }, { "floor", ChannelRole::FloorTom }, { "tom 2", ChannelRole::FloorTom }, { "tom2", ChannelRole::FloorTom },
            { "tom",   ChannelRole::RackTom },
            // Overheads. Live desks write them "OH", "OV", "OVH" or spell them out; the cymbal mics
            // (ride, crash) belong with them. Without these the kit arrives with no cymbals at all,
            // which is most of what a drum kit contributes above 8 kHz.
            { "overhead", ChannelRole::Overhead }, { "overhd", ChannelRole::Overhead }, { "ovhd", ChannelRole::Overhead },
            { "ovh", ChannelRole::Overhead, true }, { "ov", ChannelRole::Overhead, true }, { "oh", ChannelRole::Overhead, true },
            { "cymbal", ChannelRole::Overhead }, { "ride", ChannelRole::Overhead }, { "crash", ChannelRole::Overhead },
            { "full drums", ChannelRole::DrumBus }, { "drum mix", ChannelRole::DrumBus }, { "drums", ChannelRole::DrumBus },
            { "room",  ChannelRole::Room },
            // A congregation / audience microphone is a room microphone: it is what the broadcast hears
            // of the building. (It joins the drum bus with the other room mics - see mixBusForFamily.)
            { "crowd", ChannelRole::Room }, { "congregation", ChannelRole::Room }, { "audience", ChannelRole::Room },
            { "ambience", ChannelRole::Room }, { "ambient", ChannelRole::Room },
            { "bass",  ChannelRole::BassDI },
            { "organ", ChannelRole::Organ }, { "keys", ChannelRole::Piano }, { "piano", ChannelRole::Piano }, { "pad", ChannelRole::SynthPad },
            // Playback from the stage or the booth: a loop, a backing track, a click, the computer feed
            // a desk calls "Computer Audio" or "USB". It is music, so it joins the music bus.
            { "track", ChannelRole::SynthPad }, { "playback", ChannelRole::SynthPad }, { "loop", ChannelRole::SynthPad },
            { "click", ChannelRole::SynthPad }, { "synth", ChannelRole::SynthLead },
            { "computer", ChannelRole::SynthPad }, { "usb", ChannelRole::SynthPad, true }, { "media", ChannelRole::SynthPad },
            { "video", ChannelRole::SynthPad }, { "laptop", ChannelRole::SynthPad },
            { "acoustic", ChannelRole::AcousticGuitar }, { "gtr", ChannelRole::ElectricGuitarClean }, { "guitar", ChannelRole::ElectricGuitarClean },
            { "lead",  ChannelRole::LeadVocal }, { "ld", ChannelRole::LeadVocal, true },
            { "choir", ChannelRole::Choir },
            { "vox",   ChannelRole::BackingVocal }, { "bgv", ChannelRole::BackingVocal }, { "bv", ChannelRole::BackingVocal, true }, { "vocal", ChannelRole::BackingVocal },
            // Anyone who talks to the room: the pastor, the host, the MC, whoever is at the lectern.
            { "pastor", ChannelRole::Speech }, { "speech", ChannelRole::Speech }, { "spk", ChannelRole::Speech, true },
            { "talk", ChannelRole::Speech }, { "preach", ChannelRole::Speech }, { "sermon", ChannelRole::Speech },
            { "host", ChannelRole::Speech, true }, { "mc", ChannelRole::Speech, true }, { "emcee", ChannelRole::Speech },
            { "announc", ChannelRole::Speech }, { "podium", ChannelRole::Speech }, { "pulpit", ChannelRole::Speech }, { "lectern", ChannelRole::Speech },
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
