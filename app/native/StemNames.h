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
    struct Guess { const char* needle; ChannelRole role; };

    // Order matters: "tom l" before "tom", "full drums" before "drums", "oh" only as a word.
    inline const Guess* guesses (int& count)
    {
        static const Guess table[] = {
            { "kick",  ChannelRole::KickIn }, { "bd", ChannelRole::KickIn },
            { "snare", ChannelRole::SnareTop }, { "sn ", ChannelRole::SnareTop },
            { "hat",   ChannelRole::HiHat },
            { "tom l", ChannelRole::RackTom }, { "rack", ChannelRole::RackTom }, { "tom 1", ChannelRole::RackTom }, { "tom1", ChannelRole::RackTom },
            { "tom r", ChannelRole::FloorTom }, { "floor", ChannelRole::FloorTom }, { "tom 2", ChannelRole::FloorTom }, { "tom2", ChannelRole::FloorTom },
            { "tom",   ChannelRole::RackTom },
            { "overhead", ChannelRole::Overhead }, { "oh", ChannelRole::Overhead },
            { "full drums", ChannelRole::DrumBus }, { "drum mix", ChannelRole::DrumBus }, { "drums", ChannelRole::DrumBus },
            { "room",  ChannelRole::Room },
            { "bass",  ChannelRole::BassDI },
            { "organ", ChannelRole::Organ }, { "keys", ChannelRole::Piano }, { "piano", ChannelRole::Piano }, { "pad", ChannelRole::SynthPad },
            { "track", ChannelRole::SynthPad }, { "synth", ChannelRole::SynthLead },
            { "acoustic", ChannelRole::AcousticGuitar }, { "gtr", ChannelRole::ElectricGuitarClean }, { "guitar", ChannelRole::ElectricGuitarClean },
            { "lead",  ChannelRole::LeadVocal }, { "ld", ChannelRole::LeadVocal },
            { "choir", ChannelRole::Choir },
            { "vox",   ChannelRole::BackingVocal }, { "bgv", ChannelRole::BackingVocal }, { "bv", ChannelRole::BackingVocal }, { "vocal", ChannelRole::BackingVocal },
            { "pastor", ChannelRole::Speech }, { "speech", ChannelRole::Speech }, { "spk", ChannelRole::Speech }, { "talk", ChannelRole::Speech }, { "preach", ChannelRole::Speech },
        };
        count = int (sizeof (table) / sizeof (table[0]));
        return table;
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
            if (needle == "oh")
            {
                if (name == "oh" || name.startsWith ("oh ") || name.endsWith (" oh") || name.contains (" oh ")) { role = table[i].role; return true; }
                continue;
            }
            if (name.contains (needle)) { role = table[i].role; return true; }
        }
        return false;
    }
}

} // namespace livemix
