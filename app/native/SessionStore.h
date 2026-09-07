#pragma once
#include <juce_core/juce_core.h>
#include "Mix/MixSession.h"
#include "Mix/MixParameters.h"
#include "Mix/MixMacros.h"

namespace livemix
{

// Local-first, versioned JSON for one DINELIVE session: the device, the assignments,
// purpose and sound, the five macros and the kept mix (every strip, bus and return).
// Sonic profiles and targets are code data, never stored here. House Sound (targets and
// preferences rather than frozen values) will be a separate, later document.
namespace SessionStore
{
    inline constexpr int kVersion = 1;

    struct Document
    {
        MixSession session;
        juce::String inputDevice, outputDevice;
        MixMacroValues macros;
        bool hasMix = false;
        MixParameters mix;          // the kept mix (without macros); valid when hasMix
        int tuneCount = 0;
    };

    juce::var toVar (const Document& d);
    bool fromVar (const juce::var& v, Document& d);   // false when the file is not a DINELIVE session

    juce::File sessionsFolder();                        // ~/Library/Application Support/DINELIVE/Sessions
    juce::File fileFor (const juce::String& sessionName);
    bool save (const Document& d, const juce::File& file);
    bool load (const juce::File& file, Document& d);
}

} // namespace livemix
