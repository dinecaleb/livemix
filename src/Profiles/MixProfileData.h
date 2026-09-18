#pragma once
#include <array>
#include "Core/ChannelRole.h"
#include "Core/StyleId.h"
#include "Mix/MixSession.h"
#include "FX/FxParameters.h"

namespace livemix
{

// Mix-level profile data: how DLIVE routes, pans, sends and balances a whole
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

    // How long a return's tail may run, in beats, once the tempo is known. A reverb whose decay outlasts
    // the phrase turns a busy gospel arrangement to wash: the tail of one line is still sounding under the
    // next. The FX profile's own decay is the character of the effect and stays the ceiling - this only
    // shortens it, and only when the song is quick enough to need it. 0 = leave the tail alone (the big
    // halls and ambiences are meant to outlast the bar).
    float reverbBeats (StyleProfileId profile, FxSlot slot);

    // Where a source sits left-right when there is one of it (-1..1). Several
    // sources of the same role are spread around this by RoutingGraph.
    float defaultPan (ChannelRole role);

    // How far apart several sources of the same role are spread (0 = all centre, 1 = hard left to hard right).
    float spreadForRole (ChannelRole role);

    // Bus fader starting point, dB.
    float defaultBusFaderDb (StyleProfileId profile, MixBus bus);

    // How loud a source of this family should be while it is playing (processed, pre-fader
    // active RMS in dBFS) for the profile's balance. The lead vocal is the reference.
    //
    // This is a loudness, not a peak, because a peak is not what a fader sets. Desk multitrack
    // exports routinely carry isolated clicks - one sample 25 dB above anything musical on the
    // track - and a peak-fitted fader follows the click instead of the instrument. Loudness is
    // also what the ear balances: a tom and a voice at the same peak are nowhere near the same
    // level in the mix.
    float mixLevelTargetDb (StyleProfileId profile, RoleFamily family);

    // A processed strip is never allowed to peak above this before its fader, so a loud transient
    // still has somewhere to go inside the bus. Headroom only: the balance is set by loudness.
    float stripPeakCeilingDb (StyleProfileId profile);

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
        // Gain staging is the first move in a mix and the console is the right place for it.
        // DLIVE will make a quiet input work digitally, but past this much digital gain the
        // preamp itself is wrong (a digital raise lifts the preamp's noise with the source), so
        // the app says so and names the input. Advice only: nothing about the mix changes.
        float digitalGainAdviceDb = 9.0f;
        // A close microphone on a drum hears the whole kit. Raising it raises that bleed with it, so the
        // balance lifts one only this far and the mix says to turn the preamp up instead - which raises the
        // instrument without raising what leaks into it. Counted over the input gain and the fader together,
        // because a digital raise and a fader raise lift the bleed identically.
        float maxCloseMicRaiseDb = 6.0f;
        float faintInputDb = -38.0f;       // a raw peak (at the device) that never got above this during the listen is a faint input: the
                                           // source did not really play, or the microphone / cable / preamp is the problem. It is not tuned,
                                           // raised or balanced; the mix says to check it.
        // How long a source's peak takes to arrive once a note or hit starts, ms. A compressor with attack `a` has only reached
        // 1 - exp(-rise / a) of its static reduction by then, so the processed peak lands higher than the static curve says.
        // Used to predict where a strip will peak under the proposed chain when fitting faders (measured on the church stems:
        // close drum mics let nearly the whole hit through, vocals and keys see about 60 % of the static reduction).
        // A microphone at the impact and one a metre away are not the same onset: a stick on a head reaches its
        // peak within a millisecond, so however far the static curve says the reduction should go, almost none
        // of it has happened yet. Sharing one number with the overheads had the model expect 11 dB off a snare
        // peak where the compressor delivers 1, and the fitted fader landed the drum bus with no room left.
        float compPeakRiseCloseDrumMs = 1.0f;    // kick, snare, toms: the stick is on the head
        float compPeakRisePercussiveMs = 3.0f;   // hi-hat, overheads, drum mix: the kit swells into the microphone
        float compPeakRiseRoomMs = 5.0f;         // room mics: the onset arrives smeared
        float compPeakRiseSustainedMs = 10.0f;   // voices, bass, keys, guitars
        // Over a whole listen a compressor's average reduction (what moves loudness, and what the buses and master
        // receive) follows a level between the RMS and the peaks: the release holds the reduction between syllables and
        // hits. 0 = the reduction at the RMS level, 1 = at the peaks. A single source has gaps the release recovers
        // in; a bus or the master is dense and the reduction is held, so the bus share is the higher of the two.
        // Fitted by comparing the predicted master loudness against the second listen on both church multitracks:
        // the value has to serve a quiet capture (faders up ~10 dB) and a hot one at once, and 0.32 lands both
        // inside 1 LU of the -23 target. Re-check the `after` LUFS line on both folders when changing it.
        float compDetectorCrestShareStrip = 0.25f;
        float compDetectorCrestShareBus = 0.32f;
    };
    const Relationships& relationships (StyleProfileId profile);

    // The peak rise time (ms) of a source, from Relationships (see compPeakRise*Ms).
    float compPeakRiseMs (StyleProfileId profile, ChannelRole role);

    // ---- TUNE LIVE MIX: how far one sonic objective moves one control ----
    // The reasoning layer works in intent ("bring the lead forward", strength 0.65), never in
    // dB. These are the numbers that turn a full-strength objective into a move, so an AI pass
    // that asks for everything at once still lands inside a professional Tune. They are deltas
    // on top of the deterministic plan, which is why they are small: the mix is already good
    // when they are applied.
    struct AiRanges
    {
        int version = 1;                     // stored with a session; a change to these numbers is a change to the mix

        // Tone. Frequencies name the band each objective works in; the EQ writes the profile's
        // own tone bands, so a hand edit afterwards lands on the same controls.
        float bodyHz = 90.0f,       bodyDb = 3.0f;          // low shelf: weight
        float warmthHz = 180.0f,    warmthDb = 2.5f;        // low shelf: body without boom
        float clarityHz = 350.0f,   clarityDb = 3.0f, clarityQ = 1.1f;   // cut the mud
        float presenceHz = 3000.0f, presenceDb = 2.5f, presenceQ = 1.0f; // forward
        float brightnessHz = 8000.0f, brightnessDb = 2.5f;  // high shelf: air

        // Making room for a named source, in that source's own pocket (vocalPocketHz / Q).
        float separationCutDb = 2.5f;

        // Dynamics.
        float compThresholdDb = 5.0f;        // how far the threshold travels at full strength
        float compRatioDelta = 1.0f;
        float transientAttack = 0.35f;       // -1..1 control
        float transientSustain = 0.25f;
        float gateRangeDb = 12.0f;
        float deEssRangeDb = 4.0f;

        // Place in the mix. A fader move at full strength is deliberately small: the balance
        // was already fitted from the listen, and this is a refinement of it, not a re-do.
        float faderDb = 2.5f;
        float sendDb = 5.0f;
        float panDelta = 0.25f;
        float widthDelta = 0.3f;

        // Building a space out of the reverb DLIVE has, rather than asking for a preset.
        float reverbDecayScale = 0.5f;       // +-50 % of the return's own character at full strength
        float reverbPreDelayMs = 30.0f;
        float reverbDampingDelta = 25.0f;    // %
        float reverbHighCutOctaves = 1.0f;   // darker / brighter tail
        float reverbLowCutHz = 120.0f;
        float reverbSizeDelta = 20.0f;       // %
        float reverbDiffusionDelta = 15.0f;  // %
        float reverbModDepth = 35.0f;        // % added when a modulated, spring-like character is asked for
        // A return that has to keep the words intelligible gets its pre-delay opened up
        // instead of its level pulled down: the tail arrives after the consonant.
        float articulationPreDelayMs = 25.0f;
    };
    const AiRanges& aiRanges (StyleProfileId profile);

    // ---- TUNE LIVE MIX: what the reasoning layer is never allowed to do ----
    // Bounds, not targets. Every action is checked against these after it is resolved and
    // before it is applied, so a malformed or over-enthusiastic plan cannot reach the audio.
    struct AiBounds
    {
        int version = 1;
        float maxEqGainDb = 4.0f;            // absolute, on any one band
        float minEqGainDb = -6.0f;
        float maxEqDeltaDb = 3.5f;           // from the deterministic plan
        float maxFaderMoveDb = 3.0f;         // from the deterministic plan
        float maxSendMoveDb = 6.0f;
        float maxPanMove = 0.35f;
        float maxWidthDelta = 0.35f;
        float maxCompThresholdMoveDb = 6.0f;
        float maxCompRatio = 8.0f;
        float maxGateRangeDb = 30.0f;
        float maxSatDrive = 0.5f;
        float maxDeEssRangeDb = 10.0f;
        float masterCeilingMinDb = -6.0f;
        float masterCeilingMaxDb = -0.3f;
        // The master must keep this much room under its ceiling after the plan. A mix that
        // arrives at the limiter with nothing left is not finished, it is squashed.
        float minMasterHeadroomDb = 0.5f;
        int maxActions = 160;
        // Capture gain is the console's and the planner's. A reasoning pass that moves the
        // digital preamp is moving the noise floor with the source and hiding a bad capture.
        bool allowInputGain = false;
        bool allowRoutingChanges = false;
    };
    const AiBounds& aiBounds();

    // ---- REFERENCE MIX: how far "make it sound like this" is allowed to go ----
    // Bounds, not a sound. A reference is somebody else's finished record: it is worth
    // aiming at, and it is not worth handing the mix over to. Every number here is the
    // distance between "aimed at the reference" and "no longer a professional mix".
    struct ReferenceBounds
    {
        int version = 1;
        float maxTargetShiftDb = 3.0f;     // how far a reference may move one band target off the profile's
        float toleranceDb = 1.5f;          // matching aims closer than the profile's own "that will do"
        float maxWidthDelta = 0.25f;       // the master image may follow the reference this far, no further
        float maxCompTargetShiftDb = 1.5f; // how much harder (or softer) the master glue may be asked to work
        float minSeconds = 8.0f;           // shorter than this is a clip, not a mix
        float minLoudnessLufs = -45.0f;    // quieter than this measured a silence
        float maxSilencePercent = 70.0f;   // mostly silence: the measurement is of the gaps
        float noteDb = 1.0f;               // a difference smaller than this is not worth a sentence
    };
    const ReferenceBounds& referenceBounds (StyleProfileId profile);

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

    // The master's voicing: a bounded tilt for the listener. Every field is a delta on top of
    // the tuned master - 0 everywhere is exactly "as tuned" - and the whole table is small on
    // purpose: a voicing that could be heard as a re-mix is not a voicing.
    struct Voicing
    {
        float lowShelfHz = 150.0f,  lowShelfDb = 0.0f;
        float presenceHz = 2800.0f, presenceDb = 0.0f, presenceQ = 0.9f;
        float highShelfHz = 8000.0f, highShelfDb = 0.0f;
        float satDrive = 0.0f;                 // added density, 0..0.5 on the saturator
    };
    const Voicing& voicing (StyleProfileId profile, MasterVoicing which);

    // Raising the loudness to the delivery target from where the master is actually reading:
    // how far one press may move the master, and below what move it says "already there".
    struct LoudnessLift
    {
        float maxRaiseDb = 12.0f;              // one press never adds more than this
        float maxCutDb = 6.0f;                 // ... nor takes more than this away
        float atTargetToleranceDb = 0.4f;      // closer than this is "at the target"
        float maxLimiterGrDb = 6.0f;           // a lift that would ask the limiter for more than this is a squash, and is capped
    };
    const LoudnessLift& loudnessLift();
}

} // namespace livemix
