#pragma once
#include <array>
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"
#include "Mix/MixSession.h"
#include "FX/FxParameters.h"

namespace livemix
{

// Mix-level profile data: how DINELIVE routes, pans, sends and balances a whole
// band. Numbers only (MixProfileData.cpp); the decisions that use them live in
// src/Mix. Per-source targets stay in ProfileData.cpp.
namespace MixProfile
{
    // Which reverb / delay type each internal return runs.
    FxType fxTypeForSlot (FxSlot slot);

    // Post-fader send from a source family into a return, dB. kSilenceDb = no send.
    float defaultSendDb (StyleProfileId profile, RoleFamily family, FxSlot slot);

    // Return level into the master, dB.
    float defaultReturnDb (StyleProfileId profile, FxSlot slot);

    // Where a source sits left-right when there is one of it (-1..1). Several
    // sources of the same role are spread around this by RoutingGraph.
    float defaultPan (ChannelRole role);

    // How far apart several sources of the same role are spread (0 = all centre, 1 = hard left to hard right).
    float spreadForRole (ChannelRole role);

    // Bus fader starting point, dB.
    float defaultBusFaderDb (StyleProfileId profile, MixBus bus);

    // The processed, pre-fader peak level (dBFS) a source of this family should
    // reach at its fader for the profile's balance. The lead vocal is the reference.
    float mixLevelTargetDb (StyleProfileId profile, RoleFamily family);

    // ---- Relationships (used by MixPlanner) ----
    struct Relationships
    {
        // Kick <-> bass: who owns the sub. The bass high-pass is never placed below
        // bassHpfMinHz nor above bassHpfMaxHz; it rises when the bass carries more
        // sub than the kick.
        float bassHpfMinHz = 35.0f;
        float bassHpfMaxHz = 55.0f;
        float subOverlapToleranceDb = 3.0f;

        // Lead vocal <-> music: the pocket the music makes for the voice.
        float vocalPocketHz = 2800.0f;
        float vocalPocketQ = 1.2f;
        float vocalPocketMaxCutDb = 2.5f;
        float maskingToleranceDb = 2.0f;   // music presence energy above the vocal's, before a cut

        // Lead vocal <-> backing vocals: hierarchy in dB (backing below lead at their faders).
        float backingBelowLeadDb = 5.0f;
        float choirBelowLeadDb = 4.0f;

        // Toms <-> overheads: with overheads present the tom gates stay gentler.
        float tomGateMaxRangeWithOverheadsDb = 18.0f;

        // Drum room return <-> real room microphones: when the room is already in the mix the
        // artificial room steps back by this much.
        float drumRoomSendCutWithRoomMicsDb = 6.0f;

        // Backing vocals <-> lead: N voices add up. The group is held this far under the lead.
        float backingGroupBelowLeadDb = 3.0f;

        // Bus balance: each bus's processed peak relative to the vocal bus, dB.
        std::array<float, int (MixBus::Count)> busBelowVocalsDb {};
        float busBalanceToleranceDb = 2.0f;
        float maxInputGainDb = 24.0f;      // digital input gain Tune Mix may add or remove per input
        float inputPeakCeilingDb = -6.0f;  // the chain input never gets pushed above this peak by the gain
        float maxFaderMoveDb = 18.0f;      // a quiet capture still gets a balanced mix; the preamp note says what to fix at the console
        float faintInputDb = -38.0f;       // a raw peak (at the device) that never got above this during the listen is a faint input: the
                                           // source did not really play, or the microphone / cable / preamp is the problem. It is not tuned,
                                           // raised or balanced; the mix says to check it.
        // How long a source's peak takes to arrive once a note or hit starts, ms. A compressor with attack `a` has only reached
        // 1 - exp(-rise / a) of its static reduction by then, so the processed peak lands higher than the static curve says.
        // Used to predict where a strip will peak under the proposed chain when fitting faders (measured on the church stems:
        // close drum mics let nearly the whole hit through, vocals and keys see about 60 % of the static reduction).
        float compPeakRisePercussiveMs = 3.0f;   // kick, snare, toms, hi-hat, overheads, drum mix
        float compPeakRiseRoomMs = 5.0f;         // room mics: the onset arrives smeared
        float compPeakRiseSustainedMs = 10.0f;   // voices, bass, keys, guitars
        // Over a whole listen a compressor's average reduction (what moves loudness, and what the buses and master
        // receive) follows a level between the RMS and the peaks: the release holds the reduction between syllables and
        // hits. 0 = the reduction at the RMS level, 1 = at the peaks. A single source has gaps the release recovers in;
        // a bus or the master is dense and the reduction is held.
        float compDetectorCrestShareStrip = 0.25f;
        float compDetectorCrestShareBus = 0.4f;
    };
    const Relationships& relationships (StyleProfileId profile);

    // The peak rise time (ms) of a source, from Relationships (see compPeakRise*Ms).
    float compPeakRiseMs (StyleProfileId profile, ChannelRole role);

    // ---- Mix macros (the five controls of the overview; 50 = the plan as Tune Mix left it) ----
    struct MacroRanges
    {
        // VOCALS  Warm <-> Bright: vocal bus shelves
        float vocalLowShelfHz = 200.0f, vocalWarmLowDb = 2.0f, vocalBrightLowDb = -1.0f;
        float vocalHighShelfHz = 10000.0f, vocalBrightHighDb = 2.5f, vocalWarmHighDb = -1.5f;
        // DRUMS  Tight <-> Big: drum bus
        float drumLowShelfHz = 80.0f, drumBigLowDb = 2.5f, drumTightLowDb = -1.5f;
        float drumTightRatioScale = 0.6f;      // ratio x (1 + scale) at full Tight
        float drumTightAttackScale = 0.5f;     // attack x (1 - scale) at full Tight
        float drumBigReleaseScale = 0.6f;      // release x (1 + scale) at full Big
        float drumBigSatDrive = 0.12f;
        // BASS  Clean <-> Huge: bass bus
        float bassLowShelfHz = 70.0f, bassHugeLowDb = 3.0f, bassCleanLowDb = -1.5f;
        float bassHugeSatDrive = 0.2f;         // added at full Huge; Clean removes the drive
        // SPACE  Dry <-> Wet: every send
        float spaceSendRangeDb = 12.0f;
        // ENERGY  Natural <-> Polished: master
        float energyPolishedThresholdDb = 5.0f;   // master threshold lowered by this at full Polished
        float energyNaturalRatioScale = 0.5f;     // ratio pulled toward 1 by this share at full Natural
        float energyPolishedSatDrive = 0.08f;
    };
    const MacroRanges& macroRanges (StyleProfileId profile);
}

} // namespace livemix
