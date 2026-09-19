#pragma once
#include <string>
#include <cmath>
#include <cstdio>
#include "MixSession.h"

namespace livemix
{

// ---------------------------------------------------------------------------
// LIVE SAFE
//
// What it is: a lock for the twenty minutes when a mistake is public. A service is
// running, the stream is up, and the difference between a good Sunday and a bad one is
// whether anything can jump. LIVE SAFE decides what may still happen and how far.
//
// The rule behind every number below is the same: *an operator must always be able to act
// in an emergency, and nothing may change the sound by more than they meant.* So it never
// locks a mute, a fader, the transport or the monitor - those are the emergency controls -
// and it never locks anything that only the engineer hears. What it locks is everything
// that could change the mix wholesale or interrupt the audio: a re-tune, a re-route, a
// device change, a timeline edit. What it limits is the size of the moves that are still
// allowed, so a fader cannot be thrown 30 dB by a slip of the trackpad and the AI cannot
// decide the sermon needs 4 dB more presence halfway through it.
//
// This header is the whole policy. The UI reads it to say what is locked; MixController
// enforces it. Neither invents rules of its own - that is how a lock ends up meaning one
// thing in a dialog and another in the code.
// ---------------------------------------------------------------------------

enum class LiveAction : int
{
    // Always allowed: the emergency controls.
    Fader = 0,          // limited in size, never locked
    MasterFader,        // limited harder: it is the broadcast
    Mute,
    Solo,               // monitoring only; the broadcast cannot hear it
    MonitorControl,     // monitor level, dim, PFL/AFL, monitor routing
    Transport,          // play, stop, locate
    Record,             // a service is the one thing you must never fail to capture

    // Allowed, but bounded.
    InputGain,          // a digital preamp move mid-service is audible; small steps only
    Pan,
    Send,
    ChannelProcessing,  // an Inspector edit
    Macro,

    // Locked: these change the mix wholesale, or stop the audio.
    Tune,               // TUNE MIX / RE-TUNE: a whole new mix, mid-song
    TuneChannel,
    TuneLive,           // the reasoning layer
    ReferenceMatch,
    KeepPlan,
    RevertPlan,
    Bypass,             // BYPASS drops every chain at once: the mix changes completely
    Routing,            // assignments, source changes, re-ordering: the graph is rebuilt and audio stops
    OutputRouting,      // moving the broadcast to a different pair of outputs
    DeviceChange,
    TimelineEdit,
    SessionChange,      // new / open / import
    Count
};

struct LiveSafePolicy
{
    bool on = false;

    // How far one move may go while the service is running. These are per-action limits, not
    // rate limits: a fader can still be walked anywhere, one sensible step at a time.
    float maxFaderStepDb = 6.0f;
    float maxMasterStepDb = 3.0f;      // the master is what everyone hears; it moves in small steps
    float maxInputGainStepDb = 6.0f;
    float maxPanStep = 0.34f;
    float maxSendStepDb = 8.0f;

    // What a reasoning pass (TUNE LIVE MIX, AI chat) may do while live. Tuning itself is
    // locked, so this only applies to the chat, which is allowed because the engineer asked
    // for each change by name and sees it before it lands.
    float maxAiFaderMoveDb = 1.5f;
    float maxAiEqDeltaDb = 2.0f;
    float maxAiMasterMoveDb = 1.0f;

    // The master must never lose its headroom to an edit made in a hurry.
    float minMasterHeadroomDb = 0.5f;

    // How far a mix control (the macro pads on TUNE) may lean away from the plan while the
    // service is running, on the 0..100 scale where 50 is the plan. A macro at the end of its
    // travel is a 6 dB shelf and a wet reverb on the whole group - the kind of wholesale
    // change LIVE SAFE exists to stop - so the pads are fenced to 50 +/- this and draw the
    // fence. Ceiling and floor together, because the plan is in the middle.
    float maxMacroExcursion = 25.0f;

    static LiveSafePolicy off() noexcept { return {}; }
    static LiveSafePolicy armed() noexcept { LiveSafePolicy p; p.on = true; return p; }
};

namespace liveSafe
{
    // One line of the "what LIVE SAFE does" list, and the answer to "may I do this now".
    struct Verdict
    {
        bool allowed = true;
        bool limited = false;         // allowed, but the move was made smaller
        std::string reason;           // empty when nothing needed saying
    };

    inline const char* actionName (LiveAction a) noexcept
    {
        switch (a)
        {
            case LiveAction::Fader:             return "a channel fader";
            case LiveAction::MasterFader:       return "the master fader";
            case LiveAction::Mute:              return "a mute";
            case LiveAction::Solo:              return "solo";
            case LiveAction::MonitorControl:    return "the monitor";
            case LiveAction::Transport:         return "the transport";
            case LiveAction::Record:            return "recording";
            case LiveAction::InputGain:         return "an input gain";
            case LiveAction::Pan:               return "a pan";
            case LiveAction::Send:              return "a send";
            case LiveAction::ChannelProcessing: return "a processing change";
            case LiveAction::Macro:             return "a mix control";
            case LiveAction::Tune:              return "TUNE MIX";
            case LiveAction::TuneChannel:       return "TUNE CHANNEL";
            case LiveAction::TuneLive:          return "TUNE LIVE MIX";
            case LiveAction::ReferenceMatch:    return "MATCH TO REFERENCE";
            case LiveAction::KeepPlan:          return "KEEP";
            case LiveAction::RevertPlan:        return "REVERT";
            case LiveAction::Bypass:            return "BYPASS";
            case LiveAction::Routing:           return "changing the routing";
            case LiveAction::OutputRouting:     return "changing the outputs";
            case LiveAction::DeviceChange:      return "changing the audio device";
            case LiveAction::TimelineEdit:      return "editing the timeline";
            case LiveAction::SessionChange:     return "opening or starting a session";
            case LiveAction::Count:
            default:                            return "that";
        }
    }

    // Is this action refused outright while LIVE SAFE is on?
    inline constexpr bool isLocked (LiveAction a) noexcept
    {
        switch (a)
        {
            case LiveAction::Tune:
            case LiveAction::TuneChannel:
            case LiveAction::TuneLive:
            case LiveAction::ReferenceMatch:
            case LiveAction::KeepPlan:
            case LiveAction::RevertPlan:
            case LiveAction::Bypass:
            case LiveAction::Routing:
            case LiveAction::OutputRouting:
            case LiveAction::DeviceChange:
            case LiveAction::TimelineEdit:
            case LiveAction::SessionChange:
                return true;
            default:
                return false;
        }
    }

    // Why it is locked, in the words the toast prints. One sentence, and it says what the
    // risk actually is rather than "this is locked".
    inline const char* lockReason (LiveAction a) noexcept
    {
        switch (a)
        {
            case LiveAction::Tune:
            case LiveAction::TuneChannel:
            case LiveAction::TuneLive:
            case LiveAction::ReferenceMatch:
                return "a new mix would land in the middle of a song";
            case LiveAction::KeepPlan:      return "it would swap the whole mix at once";
            case LiveAction::RevertPlan:    return "it would throw the running mix away at once";
            case LiveAction::Bypass:        return "it would drop every channel's processing at once";
            case LiveAction::Routing:       return "rebuilding the routing stops the audio for a moment";
            case LiveAction::OutputRouting: return "the broadcast would move to different outputs";
            case LiveAction::DeviceChange:  return "the audio device would have to be re-opened";
            case LiveAction::TimelineEdit:  return "an edit could remove what is being recorded";
            case LiveAction::SessionChange: return "the service that is running would be closed";
            default:                        return "it could interrupt the service";
        }
    }

    // What is still allowed, in the words the LIVE page prints under the button.
    inline const char* allowedSummary() noexcept
    {
        return "Faders, mutes, solo, the monitor, the transport and recording all keep working.";
    }

    inline const char* lockedSummary() noexcept
    {
        return "Tuning, KEEP / REVERT, BYPASS, routing, outputs, the device and timeline edits are refused.";
    }

    // The range a macro may take under this policy: the whole travel when LIVE SAFE is off.
    struct MacroRange { float lo = 0.0f, hi = 100.0f; };
    inline MacroRange macroRange (const LiveSafePolicy& p) noexcept
    {
        if (! p.on) return {};
        const float e = p.maxMacroExcursion < 0.0f ? 0.0f : (p.maxMacroExcursion > 50.0f ? 50.0f : p.maxMacroExcursion);
        return { 50.0f - e, 50.0f + e };
    }
    inline float clampMacro (const LiveSafePolicy& p, float v) noexcept
    {
        const auto r = macroRange (p);
        return v < r.lo ? r.lo : (v > r.hi ? r.hi : v);
    }
    inline std::string macroLimitReason (const LiveSafePolicy& p)
    {
        if (! p.on) return {};
        char buf[160];
        std::snprintf (buf, sizeof (buf), "LIVE SAFE keeps each control within %d of the plan, so one move cannot change the sound wholesale mid-service.",
                       int (macroRange (p).hi - 50.0f + 0.5f));
        return buf;
    }

    inline Verdict check (const LiveSafePolicy& p, LiveAction a)
    {
        if (! p.on || ! isLocked (a)) return {};
        Verdict v;
        v.allowed = false;
        v.reason = std::string ("LIVE SAFE: ") + actionName (a) + " is locked because " + lockReason (a)
                 + ". Turn LIVE SAFE off on the LIVE page.";
        return v;
    }

    // How far one move of this kind may go in one go. Returns the value to actually use.
    inline float limitStepDb (const LiveSafePolicy& p, LiveAction a, float fromDb, float toDb, Verdict& v)
    {
        if (! p.on) return toDb;
        float maxStep = 0.0f;
        switch (a)
        {
            case LiveAction::Fader:        maxStep = p.maxFaderStepDb; break;
            case LiveAction::MasterFader:  maxStep = p.maxMasterStepDb; break;
            case LiveAction::InputGain:    maxStep = p.maxInputGainStepDb; break;
            case LiveAction::Send:         maxStep = p.maxSendStepDb; break;
            default:                       return toDb;
        }
        const float delta = toDb - fromDb;
        if (std::fabs (delta) <= maxStep) return toDb;
        v.limited = true;
        char buf[160];
        std::snprintf (buf, sizeof (buf),
                       "LIVE SAFE: %s moves at most %.0f dB at a time while the service is running. Move it again to go further.",
                       actionName (a), double (maxStep));
        v.reason = buf;
        return fromDb + (delta > 0.0f ? maxStep : -maxStep);
    }

    inline float limitStep (const LiveSafePolicy& p, LiveAction a, float from, float to, Verdict& v)
    {
        if (! p.on || a != LiveAction::Pan) return to;
        const float delta = to - from;
        if (std::fabs (delta) <= p.maxPanStep) return to;
        v.limited = true;
        v.reason = "LIVE SAFE: a pan moves a little at a time while the service is running.";
        return from + (delta > 0.0f ? p.maxPanStep : -p.maxPanStep);
    }
}

} // namespace livemix
