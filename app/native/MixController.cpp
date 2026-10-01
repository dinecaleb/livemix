#include "MixController.h"
#include "State/ParameterSpecs.h"
#include "MixAI/RelationshipEngine.h"
#include "Core/DbUtils.h"
#include "Profiles/MixProfileData.h"
#include "UsageIds.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cctype>

namespace livemix
{

MixController::MixController()
{
    for (int i = 0; i < kMixScenes; ++i) scenes[size_t (i)].name = defaultSceneName (i);
}
MixController::~MixController()
{
    cancelPlanning();
    if (measuredPlanning.valid()) measuredPlanning.wait();
    engine.setTap (nullptr);
    capture.abort();
}

void MixController::setSession (const MixSession& s)
{
    cancelPlanning();
    session = s;
    // The document has moved ahead of the graph, which is a different thing from having no
    // graph. The engine keeps running the one it was prepared with until a host calls
    // prepare(), so the sound carries on.
    //
    // This used to clear `prepared`, and `prepared` is what the audio callback checks before
    // it does anything at all - so assigning one more input in the middle of a service went
    // silent, and stayed silent until the user happened to press the button that rebuilt the
    // graph. Losing the sound is never the right answer to an edit that has not reached the
    // audio yet.
    graphStale = true;
    stateStale = true;
    // The document and the mix move together: rebuild() carries every surviving strip's
    // chain, gain, fader and sends onto the new assignments. The *audio* still runs the graph
    // it was prepared with until a host calls prepare(), which is why losing the sound is
    // never the answer to an edit that has not reached the audio yet.
    const bool wasListening = stage == Stage::Listening || stage == Stage::Planning || stage == Stage::Preview;
    if (wasListening) capture.abort();
    rebuild();
    mark ("The inputs changed");
}

// ---------------------------------------------------------------------------
// THE SESSION'S OWN STATE
//
// Pure: no device, no sample rate, nothing published. Everything here used to be split
// between prepare() (which reset the mix to its baselines) and app/Main.cpp (which pushed a
// `pending` Document back afterwards to put it back). One place now, and it is a place the
// tests compile.
// ---------------------------------------------------------------------------
void MixController::rebuild()
{
    cancelPlanning();
    capture.abort();
    const MixSession previous = builtSession;
    const bool had = built;

    graph = RoutingGraph::build (session);
    const MixParameters baseline = startingPoint (session, graph);

    // The engineer's own listen belongs to the device and the person at the desk, not to the
    // mix: rebuilding must not reach into their headphones and put the level, the tap point
    // and the solo mode back to factory. It used to, so changing the assignments quietly
    // undid whatever they had set up to hear with.
    const MonitorState listen = kept.monitor;
    kept = had ? carryMix (kept, previous, baseline, session) : baseline;
    kept.monitor = listen;
    kept.numStrips = std::min (kept.numStrips, graph.numStrips());
    if (had) stripHistory = carriedStripHistory (previous);
    atCapture = kept;

    // A rebuilt graph is a different mix, so everything that *described the old one* goes:
    // the listen it was measured through, the plan built from it, an undo step that would put
    // the wrong chain on the wrong input, and the chat's answers. What survives is everything
    // that belongs to the session rather than to the graph - the mix itself, the macros, the
    // tune count, the scenes, the reference, the output feeds and the track history.
    plan.reset();
    planSelection.reset();
    compare = Compare::After;
    clearTuningScope();
    lastCapture = MixCapture::Result {};
    listened = false;
    history.clear();
    future.clear();
    liveKept = false;
    tuneLive.clearAnswers();

    builtSession = session;
    built = true;
    stateStale = false;
    stage = graph.numStrips() > 0 ? restingStage() : Stage::Setup;
    // Different inputs are a different balance between the groups: learn it again before
    // holding anything, rather than hold the new mix to the old one's ratios.
    if (had) autopilotRelearn();
}

// A different document altogether. Everything that belongs to a session goes; the device and
// the rate it is running at do not, because they belong to this Mac and this moment.
void MixController::resetDocument()
{
    cancelPlanning();
    capture.abort();
    // Autopilot holds the mix it was engaged on. Another session is not that mix, so it goes
    // off - said, and with its history written - rather than carrying its target across.
    if (autopilot.on)
    {
        setAutopilot (false);
        if (onMessage) onMessage ("Autopilot is off: a different session is open. Turn it on again to hold this one.");
    }
    session = MixSession {};
    builtSession = MixSession {};
    graph = RoutingGraph {};
    built = false;
    kept = MixParameters {};
    atCapture = kept;
    running = kept;
    macros = MixMacroValues {};
    outputs = OutputFeeds {};
    reference = ReferenceProfile {};
    safety = LiveSafePolicy {};
    for (int i = 0; i < kMixScenes; ++i) { scenes[size_t (i)] = MixScene {}; scenes[size_t (i)].name = defaultSceneName (i); }
    for (auto& h : stripHistory) h.clear();
    history.clear();
    future.clear();
    plan.reset();
    planSelection.reset();
    compare = Compare::After;
    lastCapture = MixCapture::Result {};
    lastCaptureAt = MixParameters {};
    lastCaptureRetune = false;
    listened = false;
    tuneCount = 0;
    mixed = false;
    bypassed = false;
    liveKept = false;
    broadcastDim = false;
    broadcastMute = false;
    chat.clear();
    tuneLive.clearAnswers();
    clearTuningScope();
    stage = Stage::Setup;
    graphStale = true;
    stateStale = true;
}

// The track records carried onto the rebuilt session: each follows its own input, by the same
// identity carryMix uses, so a channel that moved keeps its history and one that became a
// different source loses it with the chain it described.
std::array<std::vector<StripTuneRecord>, kMaxStrips> MixController::carriedStripHistory (const MixSession& previous) const
{
    std::vector<StripTuneRecord> flat;
    for (int s = 0; s < kMaxStrips; ++s) for (const auto& r : stripHistory[size_t (s)]) flat.push_back (r);
    std::array<std::vector<StripTuneRecord>, kMaxStrips> out;
    for (const auto& r : livemix::carryStripHistory (flat, previous, session))
        if (r.strip >= 0 && r.strip < kMaxStrips) out[size_t (r.strip)].push_back (r);
    return out;
}

void MixController::setInputName (int strip, const std::string& name)
{
    if (strip < 0 || strip >= int (session.inputs.size()) || name.empty()) return;
    const auto was = inputNamesNow();
    session.inputs[size_t (strip)].name = name;
    touch();
    // `strip` is the input; its place on the console is whichever strip listens to it.
    for (int i = 0; i < graph.numStrips(); ++i)
        if (graph.strips[size_t (i)].input == strip)
        {
            graph.strips[size_t (i)].name = name;
            engine.setStripName (i, name);
        }
    if (strip < int (builtSession.inputs.size())) builtSession.inputs[size_t (strip)].name = name;

    // A RENAME IS THE SAME CONSOLE. Scenes, favourites and the mix history know the inputs
    // they were kept with by name, so renaming BV1 to this week's singer made every one of
    // them "a different set of inputs". Whatever was kept with the old names takes the new.
    const auto now = inputNamesNow();
    for (auto& sc : scenes) if (sc.inputs == was) sc.inputs = now;
    for (auto& cp : checkpoints) if (cp.inputs == was) cp.inputs = now;
}

void MixController::setInputIcon (int strip, const std::string& icon)
{
    if (strip < 0 || strip >= int (session.inputs.size())) return;
    session.inputs[size_t (strip)].icon = icon;
    touch();
    for (int i = 0; i < graph.numStrips(); ++i)
        if (graph.strips[size_t (i)].input == strip)
        {
            graph.strips[size_t (i)].icon = icon;
            engine.setStripIcon (i, icon);
        }
    if (strip < int (builtSession.inputs.size())) builtSession.inputs[size_t (strip)].icon = icon;
}

// Pinning one unpins the rest, and pinning the one that is already pinned clears it. Nothing
// about the running mix moves: it is what the *next* tune is built around.
void MixController::setFocusInput (int strip)
{
    const int was = session.focusInput();
    session.setFocus (strip == was ? -1 : strip);
    if (onMessage)
    {
        const int now = session.focusInput();
        onMessage (now >= 0 ? session.inputs[size_t (now)].name + " is what the mix is built around. TUNE MIX to hear it."
                            : std::string ("The mix is built around whichever lead microphone DLIVE hears being sung into."));
    }
    touch();
}

// SPEECH PRIORITY: the band steps back while somebody is speaking. The only thing in DLIVE
// that moves a level on its own, which is why it is off until somebody asks for it.
void MixController::setSpeechPriority (bool on)
{
    if (session.speechPriority == on) return;
    session.speechPriority = on;
    usage ({ "speech_priority", { { "on", on ? "true" : "false" } }, {} });
    publish();
    if (onMessage)
        onMessage (on ? "Speech priority is on: the band steps back " + std::to_string (int (std::round (MixProfile::speechPriority (session.profile).depthDb)))
                            + " dB while somebody is speaking, and comes back when they stop. Your listen never ducks."
                      : std::string ("Speech priority is off: the balance stays where TUNE put it."));
    touch();
}

void MixController::setPurpose (MixPurpose p) { session.purpose = p; touch(); }

void MixController::setDelivery (DeliveryLoudness d)
{
    if (session.delivery == d) return;
    session.delivery = d;
    if (onMessage)
    {
        const float target = session.deliveryTargetLufs();
        onMessage (target < 0.0f
                       ? "The mix will aim at " + std::to_string (int (std::round (target))) + " LUFS. Run TUNE MIX to fit the whole mix to it."
                       : std::string ("The mix will aim at whatever its purpose asks for. Run TUNE MIX to fit it."));
    }
    touch();
}

void MixController::setVoicing (MasterVoicing v)
{
    if (session.voicing == v) return;
    session.voicing = v;
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    if (onMessage)
        onMessage (v == MasterVoicing::Neutral ? std::string ("Master sound: as tuned.")
                                               : std::string ("Master sound: ") + masterVoicingName (v) + ". " + masterVoicingHint (v));
    touch();
}

MixController::LoudnessMove MixController::previewLoudnessMove() const
{
    LoudnessMove m;
    const auto loud = getMasterLoudness();
    m.targetLufs = loud.targetLufs;
    if (graph.numStrips() == 0) { m.why = "Assign your inputs first: there is no mix to raise yet."; return m; }
    if (! prepared) { m.why = "No audio device is open, so DLIVE cannot hear how loud the mix is yet."; return m; }
    if (bypassed) { m.why = "BYPASS is on: switch it off to raise the mix."; return m; }
    // The integrated reading is the honest one; the short-term one stands in until it exists.
    const float from = loud.integratedLufs > -60.0f ? loud.integratedLufs
                     : loud.shortTermLufs > -60.0f ? loud.shortTermLufs : -120.0f;
    if (from <= -100.0f) { m.why = "Nothing has played yet. Have the band play for a few seconds, then press it."; return m; }
    const auto& L = MixProfile::loudnessLift();
    m.fromLufs = from;
    m.moveDb = clamp (loud.targetLufs - from, -L.maxCutDb, L.maxRaiseDb);
    // A lift the limiter would have to hold down is a squash, not a lift: it is capped so
    // the limiter is asked for no more than a few dB even at the loudest moment.
    const float peakAfter = loud.truePeakDb + m.moveDb;
    if (peakAfter > loud.ceilingDb + L.maxLimiterGrDb) m.moveDb = loud.ceilingDb + L.maxLimiterGrDb - loud.truePeakDb;
    if (std::abs (m.moveDb) < L.atTargetToleranceDb)
    {
        m.why = "Already at " + std::to_string (int (std::round (loud.targetLufs))) + " LUFS: nothing to raise.";
        return m;
    }
    m.possible = true;
    return m;
}

std::string MixController::raiseLoudnessToTarget()
{
    auto m = previewLoudnessMove();
    if (! m.possible) { if (onMessage) onMessage (m.why); return m.why; }

    auto& c = kept.master().channel;
    liveSafe::Verdict v;
    const float wantTrim = liveSafe::limitStepDb (safety, LiveAction::MasterFader, c.outputTrimDb,
                                                  clamp (c.outputTrimDb + m.moveDb, -24.0f, 24.0f), v);
    const float applied = wantTrim - c.outputTrimDb;
    if (std::abs (applied) < 0.05f)
    {
        const std::string why = v.limited ? v.reason : std::string ("The master's trim is already at its limit.");
        if (onMessage) onMessage (why);
        return why;
    }

    markMixChange ("loudness " + std::string (applied > 0.0f ? "+" : "") + std::to_string (int (std::round (applied))) + " dB");
    // The ceiling the delivery asks for: -1 dBTP for a stream, -1.5 for a broadcast feed. The
    // limiter is what makes the promise "without clipping" true, so it is switched on here.
    const auto loud = getMasterLoudness();
    c.outputTrimDb = wantTrim;
    c.limiterEnabled = true;
    c.limiterCeilingDb = std::min (c.limiterCeilingDb, loud.ceilingDb);
    if (plan && stage == Stage::Preview) plan->proposed.master().channel = c;
    // The integrated reading starts again, so the meter shows what the lift did rather than
    // an average that still remembers the level before it.
    engine.getBus (MixBus::Master).getLoudness().resetIntegrated();
    publish();
    touch();

    char buf[200];
    std::snprintf (buf, sizeof (buf), "Master %s%.1f dB: from %.1f LUFS toward %d LUFS, limited at %.1f dBTP so it cannot clip.%s",
                   applied > 0.0f ? "+" : "", applied, m.fromLufs, int (std::round (loud.targetLufs)), c.limiterCeilingDb,
                   v.limited ? " LIVE SAFE kept the step small: press again for more." : "");
    const std::string sentence (buf);
    if (onMessage) onMessage (sentence);
    return sentence;
}

// Everything about the master's level in one answer. The target comes from the session's own
// delivery setting when it has one, and from the delivery role's standard when it does not,
// so what the meter is measured against is always the thing TUNE MIX aimed at.
MixController::MasterLoudness MixController::getMasterLoudness() const
{
    MasterLoudness m;
    const auto targets = Profiles::targets (session.profile, session.masterRole());
    m.targetLufs = targets.targetLufs;
    m.toleranceLu = targets.loudnessToleranceLu;
    m.ceilingDb = targets.truePeakCeilingDb;
    const float wanted = session.deliveryTargetLufs();
    if (wanted < 0.0f && targets.loudnessTargetAppropriate)
    {
        m.targetLufs = wanted;
        m.ceilingDb = std::min (m.ceilingDb, wanted >= -15.0f ? -1.0f : -1.5f);
    }
    if (! prepared) return m;

    const auto& master = engine.getBus (MixBus::Master);
    const auto& loud = master.getLoudness();
    m.known = true;
    m.integratedLufs = loud.getIntegratedLufs();
    m.shortTermLufs = loud.getShortTermLufs();
    m.momentaryLufs = loud.getMomentaryLufs();
    m.truePeakDb = loud.getTruePeakDb();
    m.limiterReductionDb = master.getLimiter().getGainReductionDb();
    // The running mix decides the ceiling that is actually in force; the target above is what
    // the plan was fitted to. When a hand edit has moved the limiter, the meter says so.
    const auto& running = getRunning();
    if (running.master().channel.limiterEnabled) m.ceilingDb = running.master().channel.limiterCeilingDb;
    m.headroomDb = m.ceilingDb - master.getOutputMeter().getMaxPeakDb();
    return m;
}
void MixController::setProfile (StyleProfileId p) { session.profile = p; touch(); }

void MixController::mark (const std::string& what)
{
    lastMilestone = what;
    ++milestone;
    // Every milestone is a place to come back to. Taking it here rather than at each call site
    // is what stops the list and the "write it down now" signal ever disagreeing about what
    // happened.
    checkpoint (what, what.rfind ("TUNE", 0) == 0 || what.rfind ("RE-TUNE", 0) == 0);
    touch();
}

void MixController::checkpoint (const std::string& what, bool fromTune)
{
    if (! built || graph.numStrips() == 0) return;
    MixCheckpoint c;
    c.whenMs = (long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                   std::chrono::system_clock::now().time_since_epoch()).count();
    c.what = what;
    c.fromTune = fromTune;
    c.tuneCount = tuneCount;
    c.mix = kept;
    c.macros = macros;
    c.inputs = inputNamesNow();
    checkpoints.push_back (std::move (c));
    pruneCheckpoints (checkpoints);
    lastCheckpointMs = checkpoints.back().whenMs;
    atLastCheckpoint = kept;
}

std::vector<MixCheckpoint> MixController::getCheckpointsNewestFirst() const
{
    std::vector<MixCheckpoint> out (checkpoints.rbegin(), checkpoints.rend());
    return out;
}

void MixController::restoreCheckpoints (const std::vector<MixCheckpoint>& list)
{
    checkpoints = list;
    pruneCheckpoints (checkpoints);
    lastCheckpointMs = checkpoints.empty() ? 0 : checkpoints.back().whenMs;
    atLastCheckpoint = kept;
}

bool MixController::restoreCheckpoint (int index)
{
    if (index < 0 || index >= int (checkpoints.size()) || ! built) return false;
    const MixCheckpoint taking = checkpoints[size_t (index)];     // by value: the list is about to grow
    if (taking.inputs != inputNamesNow() || taking.mix.numStrips != kept.numStrips)
    {
        if (onMessage)
            onMessage ("\"" + taking.what + "\" was from a different set of inputs, so it cannot be put back onto this one.");
        return false;
    }
    if (liveSafeRefuses (LiveAction::KeepPlan)) return false;

    markMixChange ("going back to " + taking.what);
    autopilotRelearn();
    autopilot.movedDb.fill (0.0f);
    // Where the mix is now, before it goes: coming back from a way back is the same request.
    checkpoint ("Before going back to " + taking.what);

    const MixParameters was = kept;
    kept = taking.mix;
    kept.numStrips = std::min (kept.numStrips, graph.numStrips());
    // Monitoring is the engineer's and not the history's: what solo goes to, and how loud,
    // stays exactly where they left it.
    kept.monitor = was.monitor;
    for (int i = 0; i < kept.numStrips; ++i) kept.strips[size_t (i)].solo = was.strips[size_t (i)].solo;
    for (int b = 0; b < int (MixBus::Count); ++b) kept.buses[size_t (b)].solo = was.buses[size_t (b)].solo;
    for (int f = 0; f < int (FxSlot::Count); ++f) kept.fx[size_t (f)].solo = was.fx[size_t (f)].solo;
    macros = taking.macros;
    tuneCount = std::max (tuneCount, taking.tuneCount);
    mixed = true;
    if (stage == Stage::Ready) stage = Stage::Mixed;
    publish();
    if (onMessage) onMessage ("Back to \"" + taking.what + "\". UNDO takes it forward again.");
    touch();
    return true;
}

void MixController::prepare (double sr, int maxBlockSize)
{
    // A listen the device restarted under cannot finish: the capture is thrown away below, and
    // poll() only ever leaves Listening on a capture that completed or failed - so TUNE sat on
    // "waiting for the band" with Autopilot paused until somebody pressed Cancel.
    const bool listenCut = stage == Stage::Listening;
    capture.abort();
    engine.setTap (nullptr);
    sampleRate = sr;
    blockSize = maxBlockSize;
    // The state comes first, always: the graph the DSP is built for and the graph the mix
    // belongs to are the same graph, or a channel ends up with another channel's chain.
    if (! built || stateStale) rebuild();
    engine.prepare (sr, maxBlockSize, session);
    capture.prepare (sr, graph);
    engine.setTap (&capture);
    bypassed = false;                       // a way of listening, not a setting: never restored
    prepared = true;
    graphStale = false;                     // the graph is the document again
    engine.setOutputFeeds (outputs);        // routing belongs to the device, not to the mix
    publish();
    if (listenCut)
    {
        abortTuneMix();
        if (onMessage) onMessage ("The audio device restarted in the middle of the listen, so it was stopped. Nothing was changed - TUNE again.");
    }
}

MixParameters MixController::compose() const
{
    // What is audible: the kept mix, or - while a proposal is on preview - BEFORE, or AFTER
    // narrowed to whatever KEEP SOME has selected. One place decides it (getBase), so what is
    // heard and what KEEP applies can never be two different mixes.
    const MixParameters& base = getBase();
    if (bypassed)
    {
        // The console feed: no processing, no fader moves, no returns. Only the listening
        // controls (mute / solo) survive, so soloing one source still works while comparing.
        auto raw = startingPoint (session, graph);
        raw.bypassProcessing = true;
        for (int i = 0; i < raw.numStrips && i < base.numStrips; ++i)
        {
            raw.strips[size_t (i)].mute = base.strips[size_t (i)].mute;
            raw.strips[size_t (i)].solo = base.strips[size_t (i)].solo;
        }
        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            raw.buses[size_t (b)].mute = base.buses[size_t (b)].mute;
            raw.buses[size_t (b)].solo = base.buses[size_t (b)].solo;
        }
        for (int f = 0; f < int (FxSlot::Count); ++f) raw.fx[size_t (f)].solo = base.fx[size_t (f)].solo;
        raw.monitor = base.monitor;      // the engineer's listen is not part of the mix being bypassed
        // The emergency keys are not part of the mix either. MUTE pressed during a howl and then
        // BYPASS pressed to compare must not put the howl back on the air.
        raw.broadcastDim = broadcastDim;
        raw.broadcastMute = broadcastMute;
        return raw;
    }
    auto out = MixMacros::applyVoicing (MixMacros::apply (base, macros, graph, session.profile),
                                        session.voicing, session.profile);
    // SPEECH PRIORITY is a way of working rather than a balance: it is set from the session and
    // the profile on every publish, never kept with a mix and never saved inside one.
    {
        const auto& sp = MixProfile::speechPriority (session.profile);
        out.speechDuck.enabled = session.speechPriority;
        out.speechDuck.depthDb = sp.depthDb;
        out.speechDuck.thresholdDb = sp.thresholdDb;
        out.speechDuck.attackMs = sp.attackMs;
        out.speechDuck.releaseMs = sp.releaseMs;
        out.speechDuck.holdMs = sp.holdMs;
    }
    out.broadcastDim = broadcastDim;
    out.broadcastMute = broadcastMute;
    return out;
}

// ---- The emergency keys ----

void MixController::setBroadcastDim (bool on)
{
    if (broadcastDim == on) return;
    broadcastDim = on;
    publish();
    if (onMessage) onMessage (on ? "Broadcast dimmed 20 dB. Your own listen is unchanged; press DIM again to bring it back."
                                 : "Broadcast back to full level.");
}

void MixController::setBroadcastMute (bool on)
{
    if (broadcastMute == on) return;
    broadcastMute = on;
    publish();
    if (onMessage) onMessage (on ? "Broadcast muted. Your own listen is unchanged; press MUTE again to bring it back."
                                 : "Broadcast unmuted.");
}

// ---- Scenes ----

std::vector<std::string> MixController::inputNamesNow() const
{
    std::vector<std::string> names;
    for (const auto& in : session.inputs) names.push_back (in.name);
    return names;
}

const MixScene& MixController::getScene (int slot) const
{
    static const MixScene none;
    if (slot < 0 || slot >= kMixScenes) return none;
    return scenes[size_t (slot)];
}

void MixController::keepScene (int slot)
{
    if (slot < 0 || slot >= kMixScenes || ! built) return;
    auto& s = scenes[size_t (slot)];
    if (s.name.empty()) s.name = defaultSceneName (slot);
    s.kept = true;
    s.mix = kept;
    s.macros = macros;
    s.inputs = inputNamesNow();
    if (onMessage) onMessage ("Kept as " + s.name + ". One press on it brings this whole mix back.");
    usage ({ "preset_saved", { { "kind", "scene" } }, {} });
    mark ("Scene kept: " + s.name);
}

bool MixController::recallScene (int slot)
{
    // Bounded by the list rather than by the four fixed slots: a favourite is a scene past
    // them and is brought back by exactly this code, refusals and strip records included.
    if (slot < 0 || slot >= int (scenes.size()) || ! built) return false;
    const auto& s = scenes[size_t (slot)];
    if (! s.kept)
    {
        if (onMessage) onMessage (s.name.empty() ? std::string ("Nothing is kept there yet.") : s.name + " has nothing kept yet. Set the mix, then KEEP it there.");
        return false;
    }
    if (s.inputs != inputNamesNow() || s.mix.numStrips != kept.numStrips)
    {
        if (onMessage) onMessage (s.name + " was kept with a different set of inputs. Set the mix and KEEP it again.");
        return false;
    }
    markMixChange ("recalling " + s.name);
    autopilotRelearn();
    autopilot.movedDb.fill (0.0f);
    const MixParameters was = kept;
    kept = s.mix;
    kept.numStrips = std::min (kept.numStrips, graph.numStrips());
    // Monitoring is the engineer's, not the scene's: what solo goes to and how loud stays.
    kept.monitor = was.monitor;
    for (int i = 0; i < kept.numStrips; ++i) kept.strips[size_t (i)].solo = was.strips[size_t (i)].solo;
    for (int b = 0; b < int (MixBus::Count); ++b) kept.buses[size_t (b)].solo = was.buses[size_t (b)].solo;
    macros = s.macros;
    mixed = true;
    plan.reset();
    clearTuningScope();
    compare = Compare::After;
    stage = restingStage();
    for (int i = 0; i < kept.numStrips && i < was.numStrips; ++i)
        recordStripTune (i, "Scene: " + s.name, was.strips[size_t (i)], kept.strips[size_t (i)]);
    publish();
    usage ({ "preset_applied", { { "kind", s.favourite ? "favourite" : "scene" } }, {} });
    if (onMessage) onMessage (s.name + " is back.");
    mark ("Scene: " + s.name);
    return true;
}

void MixController::renameScene (int slot, const std::string& name)
{
    if (slot < 0 || slot >= kMixScenes) return;
    scenes[size_t (slot)].name = name.empty() ? defaultSceneName (slot) : name;
    touch();
}

std::vector<MixScene> MixController::getScenes() const
{
    return std::vector<MixScene> (scenes.begin(), scenes.end());
}

void MixController::restoreScenes (const std::vector<MixScene>& list)
{
    scenes.assign (size_t (kMixScenes), MixScene {});
    for (int i = 0; i < kMixScenes; ++i) scenes[size_t (i)] = i < int (list.size()) ? list[size_t (i)] : MixScene {};
    for (int i = 0; i < kMixScenes; ++i)
    {
        if (scenes[size_t (i)].name.empty()) scenes[size_t (i)].name = defaultSceneName (i);
        scenes[size_t (i)].favourite = false;      // the four slots are never favourites
    }
    // Everything past the four slots is a favourite, whatever a file happened to call it.
    for (size_t i = size_t (kMixScenes); i < list.size(); ++i)
    {
        auto favourite = list[i];
        favourite.favourite = true;
        if (favourite.name.empty()) favourite.name = "Favourite " + std::to_string (i - size_t (kMixScenes) + 1);
        scenes.push_back (favourite);
    }
}

// ---------------------------------------------------------------------------
// FAVOURITE MIXES
// ---------------------------------------------------------------------------
int MixController::numFavourites() const noexcept
{
    return int (scenes.size()) - kMixScenes;
}

const MixScene& MixController::getFavourite (int index) const
{
    static const MixScene none;
    if (index < 0 || index >= numFavourites()) return none;
    return scenes[size_t (kMixScenes + index)];
}

// WHAT THE MIX THAT IS RUNNING ACTUALLY SOUNDS LIKE. Measured from the listen DLIVE already
// has - the relationships, where each group lands against the master, and what the master
// itself measured. Never read off a fader: a fader at -6 dB means nothing without knowing
// what arrived at it, which is the whole reason this exists.
MixFingerprint MixController::measureNow() const
{
    MixFingerprint f;
    if (! listened || ! lastCapture.valid) return f;

    MixPlanContext ctx;
    ctx.session = session;
    ctx.graph = graph;
    ctx.current = kept;
    ctx.atCapture = lastCaptureAt;
    ctx.capture = lastCapture;
    ctx.reference = reference;
    ctx.retune = lastCaptureRetune;
    for (const auto& r : RelationshipEngine::measure (ctx))
        if (! r.metric.empty()) f.metrics.push_back ({ r.metric, r.value });

    const auto& master = lastCapture.buses[size_t (MixBus::Master)];
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const auto& bus = lastCapture.buses[size_t (b)];
        if (! bus.valid || bus.rmsDb <= -100.0f || ! master.valid || master.rmsDb <= -100.0f) continue;
        f.busBelowMasterDb[size_t (b)] = bus.rmsDb - master.rmsDb;
        f.busMeasured[size_t (b)] = true;
    }

    const auto& out = lastCapture.masterOutput;
    if (out.valid)
    {
        f.masterLufs = out.loudnessLufs;
        f.masterCrestDb = out.crestFactorDb;
        f.masterTruePeakDb = out.truePeakDb;
        f.masterCorrelation = out.stereoCorrelation;
        f.masterBandDb = out.bandEnergyDb;
        f.valid = true;
    }
    else if (! f.metrics.empty()) f.valid = true;
    return f;
}

bool MixController::markFavourite (const std::string& name)
{
    if (! built) return false;
    MixScene s;
    s.name = name.empty() ? "Favourite " + std::to_string (numFavourites() + 1) : name;
    s.kept = true;
    s.favourite = true;
    s.mix = kept;
    s.macros = macros;
    s.inputs = inputNamesNow();
    s.whenMs = (long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                   std::chrono::system_clock::now().time_since_epoch()).count();
    s.sound = measureNow();
    scenes.push_back (s);
    if (onMessage)
        onMessage (s.sound.valid
                       ? s.name + " is a favourite. DLIVE measured what it sounds like, so a later mix can be "
                                  "aimed at it the way it is aimed at a record."
                       : s.name + " is a favourite. Nothing has been listened to yet, so the mix is kept but what "
                                  "it sounds like is not measured - TUNE MIX once and mark it again to aim at it.");
    usage ({ "preset_saved", { { "kind", "favourite" } }, {} });
    mark ("Favourite: " + s.name);
    return true;
}

bool MixController::recallFavourite (int index)
{
    if (index < 0 || index >= numFavourites()) return false;
    return recallScene (kMixScenes + index);
}

void MixController::renameFavourite (int index, const std::string& name)
{
    if (index < 0 || index >= numFavourites() || name.empty()) return;
    scenes[size_t (kMixScenes + index)].name = name;
    touch();
}

void MixController::removeFavourite (int index)
{
    if (index < 0 || index >= numFavourites()) return;
    const auto name = scenes[size_t (kMixScenes + index)].name;
    scenes.erase (scenes.begin() + long (kMixScenes + index));
    if (onMessage) onMessage (name + " is no longer a favourite. The mix it held is still in the mix history.");
    mark ("Favourite removed: " + name);
}

bool MixController::useFavouriteAsReference (int index)
{
    if (index < 0 || index >= numFavourites()) return false;
    const auto& f = getFavourite (index);
    if (! f.sound.valid || f.sound.masterLufs <= -100.0f)
    {
        if (onMessage)
            onMessage (f.name + " was kept before DLIVE had listened to anything, so there is nothing measured to "
                                "aim at. Mark the mix again once it has been tuned.");
        return false;
    }

    // A favourite becomes a reference through exactly the path a record goes through: same
    // profile, same bounds, same MATCH TO REFERENCE. There is no second target system.
    ReferenceProfile r;
    r.valid = true;
    r.name = f.name;
    r.path = "this session's own mix";
    r.channels = 2;
    r.bandEnergyDb = f.sound.masterBandDb;
    r.crestFactorDb = f.sound.masterCrestDb;
    r.loudnessLufs = f.sound.masterLufs;
    r.truePeakDb = f.sound.masterTruePeakDb;
    r.stereoCorrelation = f.sound.masterCorrelation;
    setReference (r);
    if (onMessage)
        onMessage ("Aimed at " + f.name + ". The next TUNE MIX, or MATCH TO REFERENCE, moves the master towards how "
                   "that mix sounded - inside the profile's own bounds, as it does for a record.");
    return true;
}

void MixController::setOutputFeeds (const OutputFeeds& f)
{
    // Moving the broadcast to a different pair of outputs mid-service is the one routing
    // change that is silent until it is too late. Changing only the *monitor* feed is always
    // allowed - it is the engineer's own listen and nobody else hears it.
    // Judged on the routing as it will actually land: a listen moved onto a pair another feed
    // uses is taken off the device by normaliseOutputs, and that must never be the way a
    // room or stream feed changes under LIVE SAFE.
    // The broadcast and the engineer's listen are always a real stereo pair, whatever set
    // them - the sheet, a restored session, or the host wiring up two devices.
    auto next = f;
    normaliseOutputs (next);
    if (safety.on && ! onlyMonitorChanged (outputs, next) && liveSafeRefuses (LiveAction::OutputRouting)) return;
    outputs = next;
    if (prepared) engine.setOutputFeeds (outputs);
    touch();        // the session remembers where the cue goes
}

void MixController::setBypass (bool on)
{
    if (bypassed == on) return;
    if (on && liveSafeRefuses (LiveAction::Bypass)) return;
    bypassed = on;
    publish();                       // the kept mix is not touched, so there is nothing to save
}

const std::vector<MixController::VoiceJob>& MixController::voiceJobs()
{
    // Plain words. "Speech", "lead vocal" and "backing vocal" are the engine's names for
    // these; what a volunteer is asked is what the person holding the microphone is doing.
    static const std::vector<VoiceJob> jobs = {
        { ChannelRole::Speech,       "SPEAKING",
          "Preaching, hosting, announcements. Levelled to a spoken target, held steady, the boom cut "
          "and the S sounds tamed - and it goes to the SPEECH group, which has its own fader." },
        { ChannelRole::LeadVocal,    "SINGING LEAD",
          "The voice the mix is built around. Levelled to a sung target, never gated, given a pocket "
          "in the band - and it goes to the LEAD group." },
        { ChannelRole::BackingVocal, "SINGING BACKING",
          "A voice that sits under the lead and moves with it. It goes to the BGV group, which is "
          "held under the lead by the profile." },
        { ChannelRole::Choir,        "CHOIR",
          "A section rather than a soloist: gentler, wider, and further under the lead than a single "
          "backing voice. It goes to the BGV group." },
    };
    return jobs;
}

ChannelRole MixController::roleForJob (int strip, ChannelRole job) const
{
    if (strip < 0 || strip >= int (session.inputs.size())) return job;
    const auto current = session.inputs[size_t (strip)].role;
    return roleFamily (current) == roleFamily (job) ? current : job;
}

bool MixController::isVoiceChannel (int strip) const
{
    if (strip < 0 || strip >= int (session.inputs.size())) return false;
    switch (roleFamily (session.inputs[size_t (strip)].role))
    {
        case RoleFamily::LeadVocal:
        case RoleFamily::BackingVocal:
        case RoleFamily::Choir:
        case RoleFamily::Speech:     return true;
        default:                     return false;
    }
}

bool MixController::setInputRole (int strip, ChannelRole role)
{
    if (strip < 0 || strip >= int (session.inputs.size())) return false;
    if (session.inputs[size_t (strip)].role == role) return false;
    if (liveSafeRefuses (LiveAction::Routing)) return false;

    // A rebuild clears UNDO (a graph change is a different mix), so the way back is the mix
    // history rather than a snapshot: a checkpoint before, named after what is about to happen.
    const std::string name = session.inputs[size_t (strip)].name;
    checkpoint ("Before " + name + " became " + channelRoleName (role), false);

    auto next = session;
    next.inputs[size_t (strip)].role = role;
    setSession (next);            // rebuilds the graph and carries every other strip's mix across

    // ...and this one strip takes the profile's own starting point for what it now is. The
    // engineer's listening state is not part of what the channel is.
    const MixParameters raw = startingPoint (session, graph);
    if (strip < raw.numStrips && strip < kept.numStrips)
    {
        const bool mute = kept.strips[size_t (strip)].mute;
        const bool solo = kept.strips[size_t (strip)].solo;
        kept.strips[size_t (strip)] = raw.strips[size_t (strip)];
        kept.strips[size_t (strip)].mute = mute;
        kept.strips[size_t (strip)].solo = solo;
        atCapture = kept;
    }
    publish();
    mark (name + ": " + channelRoleName (role));
    if (onMessage)
        onMessage (name + " is set up for " + std::string (channelRoleName (role)) + ". It is a starting point, not a "
                   "preset: everything is still editable, and the next TUNE plans it as what it now is.");
    return true;
}

// ---------------------------------------------------------------------------
// AUTOPILOT
// ---------------------------------------------------------------------------
AutopilotReading MixController::readAutopilotMeters() const
{
    // Every group where it LANDS: its own meter (after its chain, before its fader) plus its
    // fader, a muted one silent. The mix is those summed. See AutopilotReading for why this is
    // not the master meter and not the group meter alone.
    AutopilotReading r;
    if (! prepared) return r;
    double mixMs = 0.0;
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        if (! engine.isBusUsed (MixBus (b))) continue;
        const auto& bus = kept.buses[size_t (b)];
        const float meter = engine.getBus (MixBus (b)).getOutputMeter().getMaxRmsDb();
        const float lands = bus.mute || ! std::isfinite (meter) || meter <= -100.0f ? kSilenceDb : meter + bus.faderDb;
        r.busRmsDb[size_t (b)] = lands;
        r.busActive[size_t (b)] = lands > autopilotLimits.quietGroupDb;
        if (lands > kSilenceDb) mixMs += std::pow (10.0, double (lands) / 10.0);
    }
    r.masterRmsDb = mixMs > 1.0e-12 ? float (10.0 * std::log10 (mixMs)) : kSilenceDb;
    const auto loud = getMasterLoudness();
    r.masterShortLufs = loud.shortTermLufs;
    r.masterTruePeakDb = loud.truePeakDb;
    r.clipping = engine.getBus (MixBus::Master).getOutputMeter().hasClipped();
    return r;
}

namespace
{
    // Seconds of audio, measured by the engine, never by the clock on the wall.
    constexpr double kAutopilotLearnSeconds = 8.0;     // to learn a mix before holding it
    constexpr double kAutopilotSettleSeconds = 6.0;    // after the arrangement it knows comes back
    constexpr double kAutopilotAverageSeconds = 6.0;   // how slow its ear is
}

void MixController::autopilotRelearn()
{
    if (! autopilot.on) return;
    autopilotTarget.valid = false;
    autopilotLearn = kAutopilotLearnSeconds;
    autopilotSettle = 0.0;
    autopilotAvgMs.fill (0.0);
    autopilotMixMs = 0.0;
    autopilotAvgSeconds = 0.0;
    autopilotSinceDecide = 0.0;
}

void MixController::autopilotFlushHistory (const char* why)
{
    // EVERY MOVE IS A MIX HISTORY ENTRY: whatever had not yet added up to an entry of its own
    // is written now, so nothing Autopilot did is missing from the history.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const float d = autopilotSinceHistoryDb[size_t (b)];
        if (std::fabs (d) < 0.05f) continue;
        char line[96];
        std::snprintf (line, sizeof (line), "Autopilot: %s %+.1f dB", mixBusName (MixBus (b)), double (d));
        checkpoint (std::string (line) + ". " + why, false);
        autopilotSinceHistoryDb[size_t (b)] = 0.0f;
    }
}

bool MixController::setAutopilot (bool on)
{
    if (on == autopilot.on) return on;
    if (! on)
    {
        autopilotFlushHistory ("Written as Autopilot was switched off.");
        autopilot = AutopilotState {};
        autopilotTarget = AutopilotTarget {};
        autopilotSinceHistoryDb.fill (0.0f);
        usage ({ "autopilot", { { "state", "off" } }, {} });
        if (onMessage) onMessage ("Autopilot off. Every fader is where it is; nothing goes back.");
        mark ("Autopilot off");
        return false;
    }
    if (! built || ! prepared)
    {
        usage ({ "autopilot", { { "state", "refused" }, { "reason", "no_mix" } }, {} });
        if (onMessage) onMessage ("There is no mix running for Autopilot to hold yet.");
        return false;
    }

    // WHERE THE ENGINEER LEFT IT, measured. Never a fader position: what they set was each
    // group *against the rest of it*, and that is the thing to hold an hour later.
    const auto now = readAutopilotMeters();
    if (now.masterRmsDb <= -100.0f)
    {
        usage ({ "autopilot", { { "state", "refused" }, { "reason", "silent" } }, {} });
        if (onMessage)
            onMessage ("Nothing is playing, so there is no mix to hold. Engage Autopilot while the band is going "
                       "and it will keep that mix.");
        return false;
    }
    autopilot = AutopilotState {};
    autopilot.on = true;
    autopilot.holding = true;
    autopilotSinceHistoryDb.fill (0.0f);
    autopilotLastMs = 0;
    autopilotLastSamples = -1;
    for (int b = 0; b < int (MixBus::Master); ++b) autopilotPlaying[size_t (b)] = now.busActive[size_t (b)];
    autopilotFlipFor.fill (0.0);
    autopilotRelearn();        // the target is learnt over the next seconds, not taken from one reading
    checkpoint ("Before Autopilot", false);
    if (onMessage)
        onMessage ("Autopilot is listening to this mix for a few seconds, then holds it. It moves group faders "
                   "only, by the smallest step, and never more than " + std::to_string (int (autopilotLimits.maxTotalDb))
                   + " dB from here. Touch a fader and that group is yours again.");
    usage ({ "autopilot", { { "state", "on" } }, {} });
    mark ("Autopilot on");
    return true;
}

// Called from poll(), on the message thread, a few times a second. Never the audio thread.
void MixController::pollAutopilot()
{
    if (! autopilot.on || ! prepared) return;     // no target yet is learning, not nothing to do

    // A few times a second is plenty: a mix drifts over minutes, and a fader that moves at
    // video rate is a fader somebody can hear moving.
    const long long nowMs = (long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                                std::chrono::steady_clock::now().time_since_epoch()).count();
    if (autopilotLastMs != 0 && nowMs - autopilotLastMs < autopilotIntervalMs) return;
    autopilotLastMs = nowMs;

    // It never works against something the engineer is in the middle of: a listen, a plan on
    // preview, a live run or BYPASS all mean the mix on screen is not the mix being held.
    // The audio clock is kept moving through the pause, so the first reading after it counts
    // for one poll's worth of audio rather than for the whole pause.
    if (stage == Stage::Listening || stage == Stage::Planning || stage == Stage::Preview || liveRun || bypassed)
    {
        autopilotLastSamples = engine.getProcessedSamples();
        return;
    }

    // A meter that is not moving is not a reading: the device stopped, a Dante clock went
    // away. Acting on the last values it held would walk the faders on a frozen picture.
    const long long samples = engine.getProcessedSamples();
    const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    // Never more than a second: a stalled message thread must not make one reading the average.
    const double elapsed = autopilotLastSamples < 0 ? 0.0 : std::min (1.0, double (samples - autopilotLastSamples) / sr);
    const bool stale = autopilotLastSamples >= 0 && samples == autopilotLastSamples;
    autopilotLastSamples = samples;
    // A solo in place takes everything else out of the mix, so the mix is not the one to hold.
    bool soloInPlace = false;
    if (kept.monitor.mode == SoloMode::InPlace)
    {
        for (int i = 0; i < kept.numStrips; ++i) soloInPlace = soloInPlace || kept.strips[size_t (i)].solo;
        for (int b = 0; b < int (MixBus::Count); ++b) soloInPlace = soloInPlace || kept.buses[size_t (b)].solo;
    }
    if (stale || soloInPlace) { autopilot.holding = false; return; }

    auto now = readAutopilotMeters();
    autopilot.holding = now.masterRmsDb > -100.0f;

    // PLAYING, WITH A MEMORY. A group stops after two seconds below the quiet line and starts
    // after one above it, so the gaps between hits and between lines are not arrangements.
    for (int b = 0; b < int (MixBus::Master); ++b)
    {
        const bool meter = now.busActive[size_t (b)];
        auto& playing = autopilotPlaying[size_t (b)];
        auto& flip = autopilotFlipFor[size_t (b)];
        if (meter == playing) flip = 0.0;
        else if ((flip += elapsed) >= (playing ? 2.0 : 1.0)) { playing = meter; flip = 0.0; }
        now.busActive[size_t (b)] = playing;
    }

    // THE SLOW EAR. A mix drifts over minutes; a chorus is a few seconds of the same mix
    // getting bigger. Everything decided is decided on an average over several seconds of
    // audio - the audio's seconds, so a stalled UI thread does not shorten it. Straight after a
    // reset it is the plain mean of what it has heard so far, so the first reading is never
    // the whole of it; once it holds a full window it becomes the slow average.
    const double alpha = 1.0 - std::exp (-elapsed / kAutopilotAverageSeconds);
    const double k = autopilotAvgSeconds + elapsed > 0.0 ? std::max (alpha, elapsed / (autopilotAvgSeconds + elapsed)) : 1.0;
    auto ms = [] (float db) { return db > kSilenceDb && std::isfinite (db) ? std::pow (10.0, double (db) / 10.0) : 0.0; };
    auto average = [&]
    {
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            auto& a = autopilotAvgMs[size_t (b)];
            a += k * (ms (now.busRmsDb[size_t (b)]) - a);
        }
        autopilotMixMs += k * (ms (now.masterRmsDb) - autopilotMixMs);
        autopilotAvgSeconds += elapsed;
    };

    // A different arrangement from the one it learnt: hold still, and start the average again,
    // so the gap is not in it when the arrangement it knows comes back.
    bool arrangementChanged = false;
    if (autopilotTarget.valid)
        for (int b = 0; b < int (MixBus::Master); ++b)
            arrangementChanged = arrangementChanged || autopilotTarget.playing[size_t (b)] != now.busActive[size_t (b)];
    if (arrangementChanged)
    {
        autopilotAvgSeconds = 0.0;
        autopilotSettle = kAutopilotSettleSeconds;
        return;
    }
    average();

    AutopilotReading reading = now;
    auto db = [] (double m) { return m > 1.0e-12 ? float (10.0 * std::log10 (m)) : kSilenceDb; };
    for (int b = 0; b < int (MixBus::Master); ++b) reading.busRmsDb[size_t (b)] = db (autopilotAvgMs[size_t (b)]);
    reading.masterRmsDb = db (autopilotMixMs);

    if (autopilotLearn > 0.0)
    {
        // LEARNING: nothing moves. When it is done, what it heard is the mix it holds.
        autopilotLearn -= std::max (elapsed, 1.0e-6);
        if (autopilotLearn > 0.0) return;
        if (reading.masterRmsDb <= -100.0f) { autopilotLearn = 1.0e-6; return; }   // learn once something plays
        AutopilotTarget target;
        target.valid = true;
        target.deliveryLufs = deliveryLoudnessLufs (session.delivery);
        for (int b = 0; b < int (MixBus::Master); ++b)
        {
            target.playing[size_t (b)] = now.busActive[size_t (b)];
            if (! now.busActive[size_t (b)] || reading.busRmsDb[size_t (b)] <= autopilotLimits.quietGroupDb) continue;
            target.busBelowMasterDb[size_t (b)] = reading.busRmsDb[size_t (b)] - reading.masterRmsDb;
            target.measured[size_t (b)] = true;
        }
        autopilotTarget = target;
        return;
    }
    if (autopilotSettle > 0.0) { autopilotSettle -= std::max (elapsed, 1.0e-6); return; }
    // One decision per 400 ms of audio at most, whatever the clock on the wall did.
    autopilotSinceDecide += elapsed;
    if (autopilotSinceDecide < 0.4) return;
    autopilotSinceDecide = 0.0;

    // A group the engineer has taken back is not offered to the decision at all.
    auto target = autopilotTarget;
    for (int b = 0; b < int (MixBus::Master); ++b)
        if (autopilot.released[size_t (b)]) target.measured[size_t (b)] = false;

    std::array<bool, int (MixBus::Count)> correcting {};
    for (int b = 0; b < int (MixBus::Count); ++b) correcting[size_t (b)] = std::fabs (autopilot.movedDb[size_t (b)]) > 0.001f;

    for (const auto& move : Autopilot::decide (target, reading, autopilot.movedDb, correcting, autopilotLimits))
    {
        const size_t b = size_t (move.bus);
        const float before = kept.buses[b].faderDb;
        autopilotMoving = true;
        setBusFader (move.bus, before + move.deltaDb);
        autopilotMoving = false;
        const float applied = kept.buses[b].faderDb - before;
        if (std::fabs (applied) < 0.005f) continue;          // LIVE SAFE, or the end of the fader

        // ITS OWN MOVE IS KNOWN EXACTLY, so the slow ear hears it at once: where the group lands
        // moved by `applied`, and so did its share of the mix. Waiting seconds for the average
        // to catch up is how a slow loop overshoots - it keeps stepping on an old picture.
        {
            const double scale = std::pow (10.0, double (applied) / 10.0);
            const double was = autopilotAvgMs[b];
            autopilotAvgMs[b] = was * scale;
            autopilotMixMs = std::max (0.0, autopilotMixMs + was * (scale - 1.0));
        }

        if (std::fabs (autopilot.movedDb[b]) < 0.001f) ++autopilot.groupsCorrected;
        autopilot.movedDb[b] += applied;
        autopilotSinceHistoryDb[b] += applied;
        autopilot.largestMoveDb = std::max (autopilot.largestMoveDb, std::fabs (autopilot.movedDb[b]));
        autopilot.lastWhat = move.what;
        autopilot.lastWhy = move.why;

        // EVERY MOVE IS A MIX HISTORY ENTRY WITH ITS REASON - written when the correction on
        // that group has added up to something worth reading, so an hour of half-decibel steps
        // is a handful of entries a person can follow rather than four hundred.
        if (std::fabs (autopilotSinceHistoryDb[b]) >= 1.0f)
        {
            char line[96];
            std::snprintf (line, sizeof (line), "Autopilot: %s %+.1f dB", mixBusName (move.bus), double (autopilotSinceHistoryDb[b]));
            checkpoint (std::string (line) + ". " + move.why, false);
            autopilotSinceHistoryDb[b] = 0.0f;
            // Said when it adds up to something, not on every half-decibel step: a toast every
            // 400 ms is noise on the one screen the volunteer is watching.
            if (onMessage) onMessage (std::string (line) + ". " + move.why);
        }
    }
}

bool MixController::resetMixToRaw()
{
    autopilotRelearn();
    if (! built) return false;
    if (liveSafeRefuses (LiveAction::ResetMix)) return false;

    // A place to come back to, first: this is the one change in DLIVE that throws away
    // everything it has decided, so it is never a one-way door.
    checkpoint ("Before reset to raw", false);
    markMixChange ("reset the mix to raw");

    const MixParameters raw = startingPoint (session, graph);
    auto next = raw;
    // The engineer's listening state is not a mix decision and does not belong to the reset,
    // exactly as it does not belong to BYPASS.
    for (int i = 0; i < next.numStrips && i < kept.numStrips; ++i)
    {
        next.strips[size_t (i)].mute = kept.strips[size_t (i)].mute;
        next.strips[size_t (i)].solo = kept.strips[size_t (i)].solo;
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        next.buses[size_t (b)].mute = kept.buses[size_t (b)].mute;
        next.buses[size_t (b)].solo = kept.buses[size_t (b)].solo;
    }
    for (int f = 0; f < int (FxSlot::Count); ++f) next.fx[size_t (f)].solo = kept.fx[size_t (f)].solo;
    next.monitor = kept.monitor;

    kept = next;
    macros = MixMacroValues {};
    plan.reset();
    planSelection.reset();
    clearTuningScope();
    compare = Compare::After;
    liveKept = false;
    mixed = false;
    tuneCount = 0;
    stage = restingStage();
    publish();
    mark ("Reset to raw");
    if (onMessage)
        onMessage ("The mix is back to where the session started. Your takes, your names, your routing, your "
                   "scenes and the whole mix history are untouched - UNDO, or Mix history, brings the mix back.");
    return true;
}

void MixController::publish()
{
    if (! prepared) return;
    running = compose();
    // THE ENGINE PLAYS THE GRAPH IT WAS PREPARED WITH. A change of inputs rebuilds the graph
    // here at once (rebuild()) but reaches the audio only when the device is reconfigured;
    // parameters are handed over by strip position, so handing them over as they are would
    // give every channel after an inserted input its neighbour's gain, gate, EQ and fader -
    // the kick's gate on the pastor's microphone. So in between, every strip the engine is
    // playing takes the settings of the strip that listens to the same device channels, and
    // everything that does not belong to one input - the groups, the master, the returns and
    // above all MUTE and DIM - goes through as it is. Refusing to publish instead froze the
    // whole console, emergency keys included, until something happened to re-open the device.
    onEngine = engineHasThisGraph() ? running : onEngineGraph (running);
    engine.setParameters (onEngine);
}

MixParameters MixController::onEngineGraph (const MixParameters& p) const
{
    const auto& playing = engine.getGraph();
    MixParameters out = p;
    out.numStrips = std::min (playing.numStrips(), int (kMaxStrips));
    // Two passes: the same device channels under the same name first (two lines of the
    // document can share a channel), then the same channels alone (a renamed input).
    std::array<int, kMaxStrips> from {};
    std::array<bool, kMaxStrips> taken {};
    from.fill (-1);
    for (int pass = 0; pass < 2; ++pass)
        for (int e = 0; e < out.numStrips; ++e)
        {
            if (from[size_t (e)] >= 0) continue;
            const auto& heard = playing.strips[size_t (e)];
            for (int s = 0; s < graph.numStrips() && s < p.numStrips; ++s)
            {
                const auto& mine = graph.strips[size_t (s)];
                if (taken[size_t (s)] || mine.inputA != heard.inputA || mine.inputB != heard.inputB) continue;
                if (pass == 0 && mine.name != heard.name) continue;
                from[size_t (e)] = s;
                taken[size_t (s)] = true;
                break;
            }
        }
    for (int e = 0; e < out.numStrips; ++e)
    {
        if (from[size_t (e)] >= 0)
            out.strips[size_t (e)] = p.strips[size_t (from[size_t (e)])];
        else if (e < onEngine.numStrips)
            out.strips[size_t (e)] = onEngine.strips[size_t (e)];   // an input the document no longer has plays on as it was
    }
    return out;
}

bool MixController::engineHasThisGraph() const noexcept
{
    const auto& playing = engine.getGraph();
    if (playing.numStrips() != graph.numStrips()) return false;
    for (int i = 0; i < graph.numStrips(); ++i)
    {
        const auto& a = playing.strips[size_t (i)];
        const auto& b = graph.strips[size_t (i)];
        if (a.inputA != b.inputA || a.inputB != b.inputB || a.bus != b.bus || a.role != b.role) return false;
    }
    return true;
}

// ---- TUNE MIX ----

void MixController::startTuneMix (const ListenSettings& s)
{
    if (liveSafeRefuses (LiveAction::Tune)) return;
    liveKept = false;
    startListening (s, -1);
}

// One source on its own. The listen is the same listen - every input is measured, so the
// channel is still decided in mix context - it just waits for this channel to play and
// keeps only this channel's part of the plan.
void MixController::startTuneChannel (int strip, const ListenSettings& s)
{
    if (strip < 0 || strip >= graph.numStrips()) return;
    if (liveSafeRefuses (LiveAction::TuneChannel)) return;
    liveKept = false;
    startListening (s, strip);
}

// One group on its own. Again the same listen and the same planner - the whole console is
// measured, so the group is still decided in mix context - narrowed to this bus when the
// plan is made. The band can be tuned during the song and the pastor during the sermon, and
// neither moves the other or the master.
void MixController::startTuneBus (MixBus bus, const ListenSettings& s)
{
    if (int (bus) < 0 || int (bus) >= int (MixBus::Master)) return;
    if (! graph.busUsed[size_t (bus)])
    {
        if (onMessage) onMessage (std::string (mixBusName (bus)) + " has nothing assigned to it, so there is nothing to tune.");
        return;
    }
    if (liveSafeRefuses (LiveAction::TuneBus)) return;
    liveKept = false;
    startListening (s, -1, int (bus));
}

// A set of channels on their own. The listen waits for any of them; the plan is narrowed to
// exactly those strips when it is made.
void MixController::startTuneStrips (const std::vector<int>& strips, const ListenSettings& s)
{
    std::vector<int> wanted;
    for (const int i : strips)
        if (i >= 0 && i < graph.numStrips()
            && std::find (wanted.begin(), wanted.end(), i) == wanted.end())
            wanted.push_back (i);
    std::sort (wanted.begin(), wanted.end());

    if (wanted.empty())
    {
        if (onMessage) onMessage ("Pick the channels to tune first.");
        return;
    }
    // One channel is TUNE CHANNEL, which has a shorter listen and a plan of its own. Saying
    // so here means the picker never has to.
    if (wanted.size() == 1) { startTuneChannel (wanted.front(), MixController::channelListen()); return; }
    if (liveSafeRefuses (LiveAction::TuneChannel)) return;
    liveKept = false;
    startListening (s, -1, -1, wanted);
}

void MixController::startListening (const ListenSettings& s, int strip, int bus, const std::vector<int>& strips)
{
    if (! prepared || stage == Stage::Listening || stage == Stage::Planning) return;
    // NOTHING IS KEPT BY STARTING SOMETHING ELSE. A proposal waiting on BEFORE / AFTER is the
    // engineer's to KEEP or REVERT; a new TUNE (the button, a group tile, TUNE CHANNEL, a
    // Mix Buddy chip) used to keep it on the way in, so the room jumped to AFTER from a press
    // that meant "try again". The verify listen of a live run is the one listen that is meant
    // to hear a proposal nobody has kept yet.
    if (stage == Stage::Preview && ! liveVerifying)
    {
        if (onMessage) onMessage ("A proposal is still waiting on BEFORE / AFTER. KEEP it or REVERT it first, then TUNE again.");
        return;
    }
    // AN ORDINARY TUNE IS NOT THE LAST LIVE RUN. The coordinator stays Ready after TUNE LIVE
    // MIX finishes, and the result card reads that to decide what it is a card about - so a
    // TUNE DRUMS started afterwards would title itself TUNE LIVE MIX and list the reasoning
    // layer's sentences about the voices. The live run is over the moment a new listen that
    // is not part of one begins, and its review goes with it.
    if (! liveRun) tuneLive.reset();
    listen = s;
    clearTuningScope();
    tuningStrip = strip;
    tuningBus = bus;
    tuningStrips = strips;
    lastScope = scopeWords();
    atCapture = running;                        // the faders and gains the listen will run with
    MixCapture::Settings cs;
    cs.seconds = s.seconds;
    cs.triggerDb = s.triggerDb;
    cs.maxWaitSeconds = s.maxWaitSeconds;
    cs.triggerStrip = strip;
    // TUNE <GROUP> waits for anything on the group rather than for the band: the sermon
    // starts the window when the pastor speaks, not when somebody touches a drum.
    if (bus >= 0)
    {
        const auto& g = graph;
        unsigned long long mask = 0;
        for (int i = 0; i < g.numStrips() && i < 64; ++i)
            if (g.strips[size_t (i)].bus == MixBus (bus)) mask |= 1ULL << i;
        cs.triggerStrips = mask;
    }
    // ... and a picked set waits for any of the ones that were picked, for the same reason.
    else if (! strips.empty())
    {
        unsigned long long mask = 0;
        for (const int i : strips) if (i >= 0 && i < 64) mask |= 1ULL << i;
        cs.triggerStrips = mask;
    }
    capture.start (cs);
    stage = Stage::Listening;
}

void MixController::clearTuningScope() noexcept
{
    tuningStrip = -1;
    tuningBus = -1;
    tuningStrips.clear();
    planSelection.reset();
}

std::string MixController::getTuningName() const
{
    if (tuningBus >= 0 && tuningBus < int (MixBus::Count)) return mixBusName (MixBus (tuningBus));
    if (! tuningStrips.empty()) return std::to_string (tuningStrips.size()) + " channels";
    if (tuningStrip < 0 || tuningStrip >= int (session.inputs.size())) return {};
    return session.inputs[size_t (tuningStrip)].name;
}

void MixController::addTuneScope (UsageEvent& e) const
{
    const char* scope = chatRun || ! buddyRequest.empty() ? "mix_buddy" : liveRun || liveKept ? "live"
                      : tuningStrip >= 0 ? "channel" : tuningBus >= 0 ? "group"
                      : ! tuningStrips.empty() ? "channels" : "mix";
    e.words.push_back ({ "scope", scope });
    if (tuningStrip >= 0 && tuningStrip < int (session.inputs.size()))
    {
        const auto family = roleFamily (session.inputs[size_t (tuningStrip)].role);
        e.words.push_back ({ "family", roleFamilyId (family) });
        e.words.push_back ({ "kind", roleKindId (family) });
    }
    if (tuningBus >= 0 && tuningBus < int (MixBus::Master)) e.words.push_back ({ "group", mixBusName (MixBus (tuningBus)) });
    if (! tuningStrips.empty()) e.numbers.push_back ({ "channels", double (tuningStrips.size()) });
    e.numbers.push_back ({ "inputs", double (session.inputs.size()) });
}

// The scope in the words the Tune card says it in. One place, so the picker, the listen card
// and the result card can never disagree about what a tune was about.
std::string MixController::scopeWords() const
{
    if (tuningBus >= 0 && tuningBus < int (MixBus::Count)) return mixBusName (MixBus (tuningBus));
    if (! tuningStrips.empty())
    {
        std::string names;
        for (size_t i = 0; i < tuningStrips.size() && i < 3; ++i)
        {
            const int strip = tuningStrips[i];
            if (strip < 0 || strip >= int (session.inputs.size())) continue;
            if (! names.empty()) names += ", ";
            names += session.inputs[size_t (strip)].name;
        }
        if (tuningStrips.size() > 3) names += " and " + std::to_string (tuningStrips.size() - 3) + " more";
        return names.empty() ? std::to_string (tuningStrips.size()) + " channels" : names;
    }
    if (tuningStrip >= 0 && tuningStrip < int (session.inputs.size())) return session.inputs[size_t (tuningStrip)].name;
    return "the whole mix";
}

// ---------------------------------------------------------------------------
// REFERENCE MIX: "make it sound like this"
// ---------------------------------------------------------------------------
void MixController::setReference (const ReferenceProfile& p)
{
    reference = p;
    mark (p.valid ? "Aimed at " + p.name : std::string ("Reference"));   // the session remembers what it is aimed at
}

void MixController::clearReference()
{
    if (! reference.valid) return;
    reference = ReferenceProfile {};
    mark ("Reference cleared");
}

void MixController::startReferenceMatch()
{
    if (! prepared || stage == Stage::Listening || stage == Stage::Planning || liveRun) return;
    if (liveSafeRefuses (LiveAction::ReferenceMatch)) return;
    if (! listened || ! lastCapture.valid)
    {
        // Nothing heard yet. The listen is the same listen; the reference simply comes with
        // it when the plan is made, so this is TUNE MIX and not a mode of its own.
        startTuneMix();
        return;
    }
    if (stage == Stage::Preview) keepPlan();   // a new proposal starts from what is audible now

    MixPlanContext ctx;
    ctx.session = session;
    ctx.graph = graph;
    ctx.current = kept;
    ctx.atCapture = lastCaptureAt;
    ctx.capture = lastCapture;
    ctx.reference = reference;
    ctx.retune = lastCaptureRetune;
    clearTuningScope();
    launchPlanning (ctx, true);
}

void MixController::abortTuneMix()
{
    cancelPlanning();
    if (liveRun)
    {
        // Cancelling a live run never leaves half a mix behind: nothing was ever applied to
        // the kept mix, only previewed, so dropping the preview is the whole of the undo.
        // Cancelling *after* the first pass was applied keeps the proposal in BEFORE / AFTER
        // so the user can still decide between KEEP and REVERT.
        capture.abort();
        tuneLive.cancel();
        endTuneLive ("TUNE LIVE MIX was stopped. " + std::string (tuneLive.hasProposal()
                         ? "The mix it had built is still on BEFORE / AFTER - keep it or revert it."
                         : "Your mix has not been changed."),
                     tuneLive.hasProposal(), "cancelled");
        return;
    }
    if (stage != Stage::Listening && stage != Stage::Planning) return;
    capture.abort();
    clearTuningScope();
    stage = restingStage();
}

// ---------------------------------------------------------------------------
// TUNE LIVE MIX
// ---------------------------------------------------------------------------
void MixController::setReasoningProvider (std::shared_ptr<MixReasoningProvider> p)
{
    tuneLive.setProvider (std::move (p));
}

void MixController::startTuneLiveMix (const LiveTuneSettings& s)
{
    if (! prepared || stage == Stage::Listening || stage == Stage::Planning || liveRun) return;
    if (liveSafeRefuses (LiveAction::TuneLive)) return;
    // NOTHING IS KEPT BY STARTING SOMETHING ELSE. A proposal waiting on BEFORE / AFTER is the
    // engineer's to decide. Another reading of TUNE LIVE MIX replaces the one on preview (it
    // is an alternative to it, from the same starting point); anything else waiting is left
    // for KEEP or REVERT, and this run does not start.
    if (stage == Stage::Preview && plan)
    {
        if (! liveKept)
        {
            if (onMessage) onMessage ("A TUNE proposal is waiting on BEFORE / AFTER. KEEP it or REVERT it first.");
            return;
        }
        kept = plan->before;
        plan.reset();
        liveKept = false;
        buddyRequest.clear();
        clearTuningScope();
        stage = restingStage();
        compare = Compare::After;
        publish();
    }

    liveSettings = s;
    liveBefore = kept;                            // the complete pre-Tune snapshot: what REVERT goes back to
    liveRun = true;
    liveVerifying = false;

    TuneLiveCoordinator::Settings ts;
    ts.refinementPass = s.refinementPass;
    ts.userRequest = s.userRequest;
    ts.variation = s.variation;
    // The Mix Buddy conversation does not steer TUNE LIVE MIX: a question asked there is help,
    // not an instruction. Only the request a button asked for (userRequest) reaches it.
    tuneLive.setSettings (ts);
    tuneLive.beginListening (session.name);

    // Working from the listen DLIVE already has: the band does not play again, and two
    // readings are compared against the same performance rather than two different ones.
    if (s.reuseListen && listened && lastCapture.valid)
    {
        MixPlanContext ctx;
        ctx.session = session;
        ctx.graph = graph;
        ctx.current = kept;
        ctx.atCapture = lastCaptureAt;
        ctx.capture = lastCapture;
        ctx.reference = reference;
        ctx.retune = lastCaptureRetune;
        launchPlanning (ctx);
        return;
    }
    startListening (s.initial, -1);
}

void MixController::tryAnotherMix()
{
    if (! canTryAnotherMix())
    {
        if (onMessage) onMessage ("Nothing has been listened to yet. Run TUNE LIVE MIX first.");
        return;
    }
    auto s = liveSettings;
    s.variation = liveSettings.variation + 1;
    s.reuseListen = true;
    // A second reading is a refinement of a judgement, not of the audio: the verify listen
    // would ask the band to play again for nothing.
    s.refinementPass = false;
    if (onMessage) onMessage ("Another reading of the same listen (" + std::to_string (s.variation) + ")...");
    startTuneLiveMix (s);
}

// The proposal becomes the AFTER of an ordinary plan, so BEFORE / AFTER, KEEP, REVERT, the
// mixer, the Inspector's "what DINE set" and the chain strips all work on it without knowing
// that a reasoning layer was involved. One source of truth, as for every other Tune.
void MixController::applyLiveProposal()
{
    if (! plan) return;
    plan->before = liveBefore;
    plan->proposed = tuneLive.getProposed();
    MixPlanner::refreshSummary (*plan);
    stage = Stage::Preview;
    compare = Compare::After;
    liveVerifying = false;
    publish();
    tuneLive.onApplied();
}

void MixController::endTuneLive (const std::string& message, bool keepProposal, const char* outcome)
{
    {
        UsageEvent e { "tune_result", { { "outcome", outcome } }, {} };
        addTuneScope (e);
        if (plan && keepProposal) e.numbers.push_back ({ "changes", double (plan->parametersChanged) });
        usage (std::move (e));
    }
    liveRun = false;
    liveVerifying = false;
    if (! keepProposal)
    {
        plan.reset();
        stage = restingStage();
    }
    else { stage = Stage::Preview; liveKept = true; }
    clearTuningScope();
    publish();

    // A chat turn answers in the chat, not in a toast that scrolls away: what DLIVE decided,
    // a line each, is what the engineer reviews before pressing KEEP.
    if (chatRun)
    {
        chatRun = false;
        ChatTurn reply;
        reply.fromEngineer = false;
        reply.applied = keepProposal;
        reply.failed = ! keepProposal;
        if (keepProposal)
        {
            const auto& intent = tuneLive.getIntent();
            reply.text = intent.summary.empty() ? std::string ("Here is what that changes.") : intent.summary;
            for (const auto& line : tuneLive.getReviewLines()) reply.detail.push_back (line);
            if (plan && plan->noChangeRequired)
            {
                reply.text += " Nothing needed changing for that - the mix already does it.";
                reply.applied = false;
            }
        }
        else
        {
            reply.text = message.empty() ? std::string ("DLIVE could not do that.") : message;
        }
        if (! reply.applied) buddyRequest.clear();
        else reply.text += " It is on BEFORE / AFTER now: KEEP makes it part of the mix, REVERT puts it back.";
        chat.push_back (reply);
        return;                      // the conversation already said it; no toast on top
    }

    if (onMessage && ! message.empty()) onMessage (message);
}

void MixController::pollTuneLive()
{
    tuneLive.poll();
    using State = TuneLiveCoordinator::State;
    switch (tuneLive.getState())
    {
        case State::Applying:
        case State::ApplyingRefinement:
            applyLiveProposal();
            break;

        case State::CapturingVerify:
            // Listen again to what was applied. `liveVerifying` keeps the proposal audible
            // through the listen, which is the whole point of a verify pass.
            if (stage != Stage::Listening)
            {
                liveVerifying = true;
                atCapture = running;
                startListening (liveSettings.verify, -1);
            }
            break;

        case State::Ready:
        {
            const auto& p = tuneLive.getPlan();
            std::string headline = "LIVE MIX READY";
            if (plan)
            {
                plan->headline = headline;
                const auto lines = tuneLive.getReviewLines();
                for (const auto& l : lines) plan->notes.push_back (l);
            }
            endTuneLive (p.countApplied() > 0 || tuneLive.hasProposal()
                             ? headline
                             : "LIVE MIX READY - the mix was already right; nothing was changed.",
                         tuneLive.hasProposal(),
                         p.countApplied() > 0 || tuneLive.hasProposal() ? "proposal" : "no_change");
            break;
        }

        case State::Failed:
            // The reasoning layer failed. The deterministic mix from the same listen is
            // already sitting in `plan`, so the user is left with a professional mix and a
            // sentence saying what happened - never with a stopped mix and never with nothing.
            endTuneLive (tuneLive.getFailure(), plan.has_value(), "failed");
            break;

        case State::Cancelled:
            endTuneLive ("TUNE LIVE MIX was stopped. Your mix has not been changed.", tuneLive.hasProposal(), "cancelled");
            break;

        default: break;
    }
}

void MixController::launchPlanning (const MixPlanContext& ctx, bool masterOnly)
{
    cancelPlanning();
    if (measuredPlanning.valid()) measuredPlanning.wait();
    planningCancel = std::make_shared<std::atomic<bool>> (false);
    planningContext = ctx;
    planningRevision = revision;
    const auto flag = planningCancel;
    const auto* liveBanks = engine.getSampleBanks();
    const auto banks = liveBanks ? std::optional<SampleBankTable> (*liveBanks) : std::nullopt;
    auto selection = MixPlanner::PlanSelection::all (ctx.graph.numStrips());
    if (masterOnly) { selection = MixPlanner::PlanSelection::none(); selection.buses[size_t (MixBus::Master)] = true; }
    else if (tuningBus >= 0) selection = MixPlanner::PlanSelection::group (ctx.graph, MixBus (tuningBus));
    else if (tuningStrip >= 0 || ! tuningStrips.empty())
    {
        selection = MixPlanner::PlanSelection::none();
        if (tuningStrip >= 0)
        {
            selection.strips[size_t (tuningStrip)] = true;
            const auto group = ctx.current.strips[size_t (tuningStrip)].linkGroup;
            for (int i = 0; group != 0 && i < ctx.current.numStrips; ++i)
                if (ctx.current.strips[size_t (i)].linkGroup == group
                    && roleFamily (ctx.graph.strips[size_t (i)].role) == roleFamily (ctx.graph.strips[size_t (tuningStrip)].role)) selection.strips[size_t (i)] = true;
        }
        else for (int i : tuningStrips) if (i >= 0 && i < ctx.current.numStrips) selection.strips[size_t (i)] = true;
    }
    stage = Stage::Planning;
    measuredPlanning = std::async (std::launch::async, [ctx, flag, banks, selection]
    {
        const auto baseline = MixPlanner::plan (ctx);
        try { return MeasuredMix::plan (ctx, banks ? &*banks : nullptr, flag.get(), &selection); }
        catch (...) { MeasuredMix::Result result; result.plan = MixPlanner::restrictTo (baseline, selection, ctx.graph, ctx.session.profile);
                      result.plan.notes.push_back ("Offline measurement unavailable; retained the deterministic proposal."); return result; }
    });
}

void MixController::poll()
{
    // AUTOPILOT, if it is on: the operator's own mix, held where they left it. Message thread,
    // group faders only, and within tolerance it does nothing.
    pollAutopilot();

    // The slow beat of the mix history. A morning of small moves - a fader here, a send there -
    // is a mix that drifted a long way from the one TUNE MIX built, with no single moment in it
    // worth marking. So one is marked: every few minutes, if anything actually changed.
    if (built && graph.numStrips() > 0 && ! checkpoints.empty())
    {
        const long long now = (long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                                  std::chrono::system_clock::now().time_since_epoch()).count();
        if (now - lastCheckpointMs >= kCheckpointBeatMs && stage != Stage::Listening && stage != Stage::Planning
            && stage != Stage::Preview && ! liveRun
            && MixPlanner::countParameterChanges (kept, atLastCheckpoint) > 0)
            checkpoint ("While mixing");
    }

    // A live run spends most of its time somewhere other than a listen - reasoning, resolving,
    // checking, applying - so its state machine is advanced whatever the stage says.
    if (liveRun && stage != Stage::Listening && stage != Stage::Planning) { pollTuneLive(); return; }
    if (stage != Stage::Listening && stage != Stage::Planning) return;
    const bool collecting = stage == Stage::Listening;
    const auto s = ! collecting && measuredPlanning.valid() ? MixCapture::State::Complete : capture.getState();
    if (s == MixCapture::State::Complete)
    {
        stage = Stage::Planning;
        MixPlanContext ctx;
        ctx.session = session;
        ctx.graph = graph;
        // A verify listen measured the applied proposal, so that - not the kept mix - is what
        // it has to be read against.
        ctx.current = (liveVerifying && plan) ? plan->proposed : kept;
        ctx.atCapture = atCapture;
        ctx.capture = capture.getResult();
        ctx.reference = reference;
        // A first mix is free to put everything where it belongs; a later one is a correction
        // to something the room is already listening to, and moves one fader only so far.
        ctx.retune = mixed;
        if (collecting)
        {
            launchPlanning (ctx);
            return;
        }
        if (! measuredPlanning.valid() || measuredPlanning.wait_for (std::chrono::seconds (0)) != std::future_status::ready) return;
        auto measured = measuredPlanning.get();
        if (planningCancel->load() || revision != planningRevision)
        {
            if (liveRun) { tuneLive.cancel(); endTuneLive ("The mix changed while it was being checked. Tune again to use the new settings.", false, "cancelled"); }
            else { stage = restingStage(); if (onMessage) onMessage ("The mix changed while it was being checked. Tune again to use the new settings."); }
            return;
        }
        ctx = planningContext;
        // Keep the listen. A reference added afterwards, and any re-plan, work from what the
        // band already played rather than asking them to play it again.
        lastCapture = ctx.capture;
        lastCaptureAt = ctx.atCapture;
        lastCaptureRetune = ctx.retune;
        listened = lastCapture.valid;

        if (liveRun && liveVerifying)
        {
            // The second listen of a live run. The deterministic plan made from it is only
            // context for the refinement - the proposal already on BEFORE / AFTER is untouched.
            const auto verifyBaseline = MixPlanner::plan (ctx);
            liveVerifying = false;
            stage = Stage::Preview;
            tuneLive.onVerifyComplete (ctx, verifyBaseline);
            pollTuneLive();
            publish();
            return;
        }

        // The listen ran with the macros applied; the plan is built on the macro-free mix, and the macros
        // stay where the user left them (50 = the plan).
        plan = std::move (measured.plan);

        if (liveRun)
        {
            // The professional mix is built first and always: whatever happens to the reasoning
            // pass from here, this is what the user is left with.
            if (! plan->valid || plan->stripsHeard == 0)
            {
                const std::string headline = plan ? plan->headline : std::string ("MIX: NO SIGNAL");
                plan.reset();
                tuneLive.cancel();
                endTuneLive (headline + " Nothing was changed.", false, "no_signal");
                return;
            }
            ++tuneCount;
            stage = Stage::Preview;
            compare = Compare::After;
            publish();
            tuneLive.onListenComplete (ctx, *plan, engine.getSampleBanks());
            pollTuneLive();
            return;
        }

        // TUNE CHANNEL and TUNE <GROUP> keep only their own part of it; everything else is
        // left exactly where it is, so what is proposed is what the mix becomes when it is
        // kept. A group plan's stripsHeard is how many of its sources really played.
        const int channel = tuningStrip;
        const int group = tuningBus;
        if (channel >= 0)    plan = MixPlanner::channelOnly (*plan, channel, session.profile);
        else if (group >= 0) plan = MixPlanner::busOnly (*plan, MixBus (group), graph, session.profile);
        else if (! tuningStrips.empty())
        {
            // The picked channels and nothing else, through the same narrowing KEEP SOME uses:
            // anything not picked is `before`, so what is proposed is exactly what the mix
            // becomes when it is kept.
            MixPlanner::PlanSelection picked;
            for (const int i : tuningStrips)
                if (i >= 0 && i < kMaxStrips) picked.strips[size_t (i)] = true;
            plan = MixPlanner::restrictTo (*plan, picked, graph, session.profile);
        }

        const bool heardOneOfTheSet = [&]
        {
            if (tuningStrips.empty() || ! plan->valid) return false;
            for (const int i : tuningStrips)
                if (i >= 0 && i < int (plan->strips.size()) && plan->strips[size_t (i)].heard) return true;
            return false;
        }();
        const bool heardIt = plan->valid && (! tuningStrips.empty() ? heardOneOfTheSet
                                             : channel < 0 ? plan->stripsHeard > 0
                                                           : channel < int (plan->strips.size()) && plan->strips[size_t (channel)].heard);
        {
            UsageEvent e { "tune_result", { { "outcome", heardIt ? (plan->noChangeRequired ? "no_change" : "proposal")
                                                     : isTuningPart() && plan->valid ? "nothing_heard" : "no_signal" } }, {} };
            addTuneScope (e);
            if (heardIt) e.numbers.push_back ({ "changes", double (plan->parametersChanged) });
            e.numbers.push_back ({ "heard", double (plan->stripsHeard) });
            usage (std::move (e));
        }
        if (heardIt)
        {
            ++tuneCount;
            stage = Stage::Preview;
            compare = Compare::After;
            if (onMessage) onMessage (plan->headline);
        }
        else if (isTuningPart() && plan->valid)
        {
            // The channel or the group said nothing, but the listen still measured every
            // input: the plan is kept for what it knows (gain staging, mix health) and
            // nothing is proposed.
            if (onMessage) onMessage (plan->headline);
            stage = restingStage();
        }
        else
        {
            if (onMessage) onMessage (plan ? plan->headline : "MIX: NO SIGNAL");
            plan.reset();
            clearTuningScope();
            stage = restingStage();
        }
        publish();
    }
    else if (s == MixCapture::State::Failed)
    {
        if (liveRun)
        {
            tuneLive.cancel();
            endTuneLive ("The listen was too short to measure. TUNE LIVE MIX again while the band plays. "
                         "Your mix has not been changed.", liveVerifying && plan.has_value(), "too_short");
            return;
        }
        {
            UsageEvent e { "tune_result", { { "outcome", "too_short" } }, {} };
            addTuneScope (e);
            usage (std::move (e));
        }
        if (onMessage) onMessage ("The listen was too short to measure. Tune Mix again while the band plays.");
        stage = restingStage();
    }
}

bool MixController::busHeard (MixBus bus) const noexcept
{
    const auto& g = graph;
    for (int i = 0; i < g.numStrips(); ++i)
        if (g.strips[size_t (i)].bus == bus && capture.stripHeard (i)) return true;
    return false;
}

std::string MixController::getStatusText() const
{
    if (bypassed) return "BYPASS: hearing the inputs as they arrive.";
    switch (stage)
    {
        case Stage::Setup:     return "Assign your inputs to begin.";
        case Stage::Ready:     return "Press TUNE LIVE MIX and have the band play normally.";
        case Stage::Listening:
            if (isWaitingForBand()) return isTuningChannel() ? "Waiting for " + getTuningName() + "..." : "Waiting for the band...";
            return "LISTENING... " + std::to_string (int (std::round (getListenProgress() * listen.seconds))) + " of " + std::to_string (int (listen.seconds)) + " s";
        case Stage::Planning:  return "Building the mix...";
        case Stage::Preview:   return plan ? plan->headline + (compare == Compare::Before ? "  (hearing BEFORE)" : "  (hearing AFTER)") : "";
        case Stage::Mixed:
        {
            const auto notes = getMixHealthNotes();
            if (notes.empty()) return "READY";
            std::string s;
            for (const auto& n : notes) s += (s.empty() ? "" : "  ") + n;
            return s;
        }
        default:               return "READY";
    }
}

// ---- Preview ----

void MixController::setCompare (Compare c)
{
    if (compare == c) return;
    compare = c;
    publish();
}

void MixController::keepPlan()
{
    autopilotRelearn();
    if (! plan || stage != Stage::Preview) return;
    if (liveSafeRefuses (LiveAction::KeepPlan)) return;
    // What AFTER is playing is what KEEP applies: with a KEEP SOME selection that is the
    // narrowed proposal, and the rest of the mix stays exactly as the listen found it.
    const MixParameters& taking = planSelection ? selectedProposed : plan->proposed;
    // KEEP replaces the whole mix, which is exactly the kind of change somebody wants a way
    // back from: one undo step, named for what asked for it.
    const bool fromBuddy = ! buddyRequest.empty();
    markMixChange (isTuningPart() ? "tune " + getTuningName()
                 : fromBuddy ? "Mix Buddy: " + buddyRequest : std::string ("TUNE MIX"));
    // Every channel the plan moved remembers it: what did it, what the strip was, what it is now.
    {
        std::string what = isTuningChannel() ? std::string ("TUNE CHANNEL")
                         : isTuningBus() ? "TUNE " + getTuningName()
                         : fromBuddy ? "Mix Buddy: " + buddyRequest
                         : liveKept ? std::string ("TUNE LIVE MIX") : std::string ("TUNE MIX");
        const int n = std::min (plan->before.numStrips, taking.numStrips);
        for (int i = 0; i < n; ++i)
            recordStripTune (i, what, plan->before.strips[size_t (i)], taking.strips[size_t (i)]);
    }
    {
        UsageEvent e { "tune_decision", { { "decision", planSelection ? "kept_some" : "kept" } }, {} };
        addTuneScope (e);
        e.numbers.push_back ({ "changes", double (plan->parametersChanged) });
        usage (std::move (e));
        // Tune can turn a drum's sample replacement on; that is the feature being used too.
        const int n = std::min (plan->before.numStrips, taking.numStrips);
        for (int i = 0; i < n && i < int (session.inputs.size()); ++i)
            if (! plan->before.strips[size_t (i)].channel.replaceEnabled && taking.strips[size_t (i)].channel.replaceEnabled)
                usage ({ "sample_replacement_on", { { "instrument", roleFamilyId (roleFamily (session.inputs[size_t (i)].role)) },
                                                    { "by", "tune" } }, {} });
    }
    liveKept = false;
    buddyRequest.clear();
    kept = taking;
    // The proposal is kept for the Inspector to read "what DLIVE set" from; the selection
    // that narrowed it has done its work and never outlives the decision.
    planSelection.reset();
    mixed = true;
    stage = Stage::Mixed;
    compare = Compare::After;
    publish();
    mark (isTuningChannel() ? std::string ("TUNE CHANNEL")
        : isTuningBus() ? "TUNE " + getTuningName()
        : fromBuddy ? std::string ("Mix Buddy")
        : tuneCount > 1 ? std::string ("RE-TUNE") : std::string ("TUNE MIX"));
}

// ---- KEEP SOME ----
// A selection is a filter on a proposal that already exists: nothing is re-decided, and what
// AFTER plays is exactly what KEEP will apply. It lives only while that proposal is on
// preview - every new listen starts with the whole of it selected again.
void MixController::refreshSelection()
{
    if (! plan || ! planSelection) { selectedProposed = MixParameters {}; return; }
    selectedProposed = MixPlanner::restrictTo (*plan, *planSelection, graph, session.profile).proposed;
}

void MixController::setPlanSelection (const PlanSelection& sel)
{
    if (! plan || stage != Stage::Preview) return;
    if (sel.everything (plan->before.numStrips)) { clearPlanSelection(); return; }
    planSelection = sel;
    refreshSelection();
    publish();
}

void MixController::clearPlanSelection()
{
    if (! planSelection) return;
    planSelection.reset();
    selectedProposed = MixParameters {};
    publish();
}

void MixController::keepPlanSelection (const PlanSelection& sel)
{
    autopilotRelearn();
    if (! plan || stage != Stage::Preview) return;
    setPlanSelection (sel);
    keepPlan();
}

void MixController::revertPlan()
{
    if (! plan || stage != Stage::Preview) return;
    if (liveSafeRefuses (LiveAction::RevertPlan)) return;
    {
        UsageEvent e { "tune_decision", { { "decision", "reverted" } }, {} };
        addTuneScope (e);
        e.numbers.push_back ({ "changes", double (plan->parametersChanged) });
        usage (std::move (e));
    }
    kept = plan->before;
    plan.reset();
    liveKept = false;
    buddyRequest.clear();
    clearTuningScope();
    stage = restingStage();
    compare = Compare::After;
    publish();
    touch();
}

// ---- Macros ----

void MixController::setMacro (MixMacro m, float value)
{
    macros.set (m, liveSafe::clampMacro (safety, value));
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    touch();
}

void MixController::resetMacros()
{
    macros = MixMacroValues {};
    publish();
    touch();
}

// ---------------------------------------------------------------------------
// Mix history, and AI MIX CHAT
// ---------------------------------------------------------------------------
MixController::MixSnapshot MixController::snapshotNow (const std::string& what) const
{
    MixSnapshot s;
    s.mix = kept;
    s.macros = macros;
    s.what = what;
    s.mixedThen = mixed;
    return s;
}

void MixController::markMixChange (const std::string& what)
{
    if (! built) return;
    history.push_back (snapshotNow (what));
    if (history.size() > kMaxHistory) history.erase (history.begin());
    // A new change ends the redo line: there is no going forward to a future that has been
    // replaced. This is how every undo stack behaves and it is what people expect.
    future.clear();
}

void MixController::applySnapshot (const MixSnapshot& s)
{
    autopilotRelearn();
    autopilot.movedDb.fill (0.0f);          // a different mix: its bound starts from here
    kept = s.mix;
    kept.numStrips = std::min (kept.numStrips, graph.numStrips());
    macros = s.macros;
    mixed = s.mixedThen;
    // Undoing while a plan is on BEFORE / AFTER would leave a preview of something that no
    // longer exists. The preview goes; the mix that came back is what is heard.
    plan.reset();
    clearTuningScope();
    compare = Compare::After;
    stage = restingStage();
    publish();
    touch();
}

void MixController::undoMix()
{
    if (history.empty())
    {
        if (onMessage) onMessage ("There is nothing to undo.");
        return;
    }
    const auto what = history.back().what;
    future.push_back (snapshotNow (what));
    const auto back = history.back();
    history.pop_back();
    applySnapshot (back);
    if (onMessage) onMessage (what.empty() ? std::string ("Undone.") : "Undone: " + what + ".");
}

void MixController::redoMix()
{
    if (future.empty())
    {
        if (onMessage) onMessage ("There is nothing to redo.");
        return;
    }
    const auto forward = future.back();
    future.pop_back();
    history.push_back (snapshotNow (forward.what));
    applySnapshot (forward);
    if (onMessage) onMessage (forward.what.empty() ? std::string ("Redone.") : "Redone: " + forward.what + ".");
}

void MixController::askBuddy (const std::string& text)
{
    if (text.find_first_not_of (" \t\n") == std::string::npos) return;
    chat.push_back ({ true, text, {}, false, false, {} });
    // Read from a copy, answered at once, and nothing about the mix changes: no parameter, no
    // history entry, no undo step. That is the whole contract (see MixBuddy.h).
    const auto a = MixBuddy::answer (text, buddySnapshot());
    ChatTurn reply;
    reply.fromEngineer = false;
    reply.text = a.text;
    reply.detail = a.detail;
    reply.actions = a.actions;
    reply.failed = a.notUnderstood;
    chat.push_back (std::move (reply));
    usage ({ "mix_buddy_used", {}, {} });
}

BuddySnapshot MixController::buddySnapshot() const
{
    BuddySnapshot b;
    b.running = prepared && built;
    b.heard = listened && lastCapture.valid;
    b.bypass = bypassed;
    b.broadcastMute = broadcastMute;
    b.broadcastDim = broadcastDim;
    b.liveSafe = safety.on;
    b.autopilot = autopilot.on;
    b.speechPriority = session.speechPriority;
    b.monitorOutput = hasMonitorOutput();
    const auto& base = getBase();
    bool anySolo = false;
    for (int i = 0; i < base.numStrips; ++i) anySolo = anySolo || base.strips[size_t (i)].solo;
    for (int g = 0; g < int (MixBus::Count); ++g) anySolo = anySolo || base.buses[size_t (g)].solo;
    b.soloInPlace = anySolo && base.monitor.mode == SoloMode::InPlace;
    if (prepared) b.speechDuckDb = engine.getSpeechDuckDb();

    for (int i = 0; i < int (session.inputs.size()) && i < base.numStrips; ++i)
    {
        const auto& in = session.inputs[size_t (i)];
        const auto& sp = base.strips[size_t (i)];
        BuddyStrip s;
        s.name = in.name;
        s.role = in.role;
        s.bus = i < graph.numStrips() ? graph.strips[size_t (i)].bus : MixBus::Music;
        s.input = in.inputA >= 0 ? in.inputA + 1 : -1;
        s.inputGainDb = sp.inputGainDb;
        s.faderDb = sp.faderDb;
        s.mute = sp.mute;
        s.solo = sp.solo;
        s.compOn = sp.channel.compEnabled;
        s.gateOn = sp.channel.gateEnabled;
        s.sampleOn = sp.channel.replaceEnabled;
        if (prepared && i < engine.getNumStrips())
        {
            const auto& p = engine.getStrip (i);
            s.inputRmsDb = p.getInputMeter().getMaxRmsDb();
            s.inputPeakDb = p.getInputMeter().getMaxPeakDb();
            s.clipped = p.getInputMeter().hasClipped();
            s.compReductionDb = std::fabs (p.getCompressor().getGainReductionDb());
            s.gateReductionDb = std::fabs (p.getGate().getGainReductionDb());
        }
        b.strips.push_back (s);
    }
    for (int g = 0; g < int (MixBus::Count); ++g)
    {
        auto& grp = b.groups[size_t (g)];
        grp.used = g == int (MixBus::Master) || (built && graph.busUsed[size_t (g)]);
        grp.faderDb = base.buses[size_t (g)].faderDb;
        grp.mute = base.buses[size_t (g)].mute;
        grp.solo = base.buses[size_t (g)].solo;
    }
    b.limiterOn = base.master().channel.limiterEnabled;
    const auto loud = getMasterLoudness();
    b.shortLufs = loud.shortTermLufs;
    b.integratedLufs = loud.integratedLufs;
    b.targetLufs = loud.targetLufs;
    b.truePeakDb = loud.truePeakDb;
    b.ceilingDb = loud.ceilingDb;
    b.limiterReductionDb = loud.limiterReductionDb;
    if (prepared)
    {
        b.masterClipped = engine.getBus (MixBus::Master).getOutputMeter().hasClipped();
        b.outputHeldBlocks = engine.getClampedOutputBlocks();
        b.nonFiniteBlocks = engine.getNonFiniteBlocks();
    }
    return b;
}

bool MixController::askForChange (const std::string& text)
{
    auto refuse = [this] (const std::string& why)
    {
        chat.push_back ({ false, why, {}, true, false, {} });
        return false;
    };
    if (text.find_first_not_of (" \t\n") == std::string::npos) return false;
    if (! prepared) return refuse ("No mix is running yet.");
    if (liveRun || stage == Stage::Listening || stage == Stage::Planning)
        return refuse ("DLIVE is busy. Wait for it to finish, then ask again.");
    if (stage == Stage::Preview)
        return refuse ("A proposal is already waiting on BEFORE / AFTER. KEEP it or REVERT it first - "
                       "nothing is kept for you.");
    if (safety.on)
        return refuse ("LIVE SAFE is on, so the mix is not changed from here. Turn LIVE SAFE off first "
                       "if this is not the middle of a service.");
    if (! (listened && lastCapture.valid))
        return refuse ("DLIVE has not heard the band yet. Run TUNE MIX once while they play; a change "
                       "is worked out from what it heard.");

    LiveTuneSettings s = liveSettings;
    s.userRequest = text;
    s.reuseListen = true;         // the band does not play again for every request
    s.refinementPass = false;     // one request, one change, reviewed by the person who asked
    s.variation = 0;
    chatRun = true;
    buddyRequest = text;
    startTuneLiveMix (s);
    if (! liveRun && stage != Stage::Preview)
    {
        chatRun = false;
        buddyRequest.clear();
        return refuse ("That could not be worked out right now.");
    }
    return true;
}

// ---- LIVE SAFE ----
//
// The lock lives here rather than in the UI. Every way the mix can change - a menu item, a
// keyboard shortcut, a drag on a fader, a macro, the reasoning layer, the chat - ends up in
// one of the setters below, and each of them asks the policy first. A guard in a menu
// handler only covers the menu.

void MixController::setLiveSafe (bool on)
{
    if (safety.on == on) return;
    safety.on = on;
    usage ({ "live_safe", { { "on", on ? "true" : "false" } }, {} });
    if (on)
    {
        // Going safe never changes the sound. It does end anything mid-flight that would
        // have: a listen in progress would land a whole new mix inside the service.
        if (stage == Stage::Listening || liveRun) abortTuneMix();
    }
    if (onMessage)
        onMessage (on ? std::string ("LIVE SAFE on. ") + liveSafe::lockedSummary() + " " + liveSafe::allowedSummary()
                      : std::string ("LIVE SAFE off. Everything is available again."));
    touch();
}

void MixController::setLiveSafePolicy (const LiveSafePolicy& p) { safety = p; touch(); }

liveSafe::Verdict MixController::checkLiveSafe (LiveAction a) const { return liveSafe::check (safety, a); }

bool MixController::liveSafeRefuses (LiveAction a)
{
    const auto v = checkLiveSafe (a);
    if (v.allowed) return false;
    if (onMessage) onMessage (v.reason);
    return true;
}

// ---- The monitor (solo) bus ----

bool MixController::anySolo() const noexcept { return numSoloed() > 0; }

int MixController::numSoloed() const noexcept
{
    int n = 0;
    for (int i = 0; i < kept.numStrips; ++i) if (kept.strips[size_t (i)].solo) ++n;
    for (int b = 0; b < int (MixBus::Master); ++b) if (kept.buses[size_t (b)].solo) ++n;
    for (int f = 0; f < int (FxSlot::Count); ++f) if (kept.fx[size_t (f)].solo) ++n;
    return n;
}

std::vector<MixController::SoloedItem> MixController::getSoloed() const
{
    std::vector<SoloedItem> out;
    for (int i = 0; i < kept.numStrips; ++i)
    {
        if (! kept.strips[size_t (i)].solo) continue;
        const std::string name = i < int (session.inputs.size()) && ! session.inputs[size_t (i)].name.empty()
                                     ? session.inputs[size_t (i)].name
                                     : "Channel " + std::to_string (i + 1);
        out.push_back ({ SoloedItem::Kind::Strip, i, name });
    }
    for (int b = 0; b < int (MixBus::Master); ++b)
        if (kept.buses[size_t (b)].solo) out.push_back ({ SoloedItem::Kind::Bus, b, mixBusName (MixBus (b)) });
    for (int f = 0; f < int (FxSlot::Count); ++f)
        if (kept.fx[size_t (f)].solo) out.push_back ({ SoloedItem::Kind::Fx, f, fxSlotName (FxSlot (f)) });
    return out;
}

void MixController::setSoloMode (SoloMode m)
{
    if (kept.monitor.mode == m) return;
    // Solo in place puts every solo on the air. Under LIVE SAFE that is exactly the kind of
    // one-press change it exists to stop; going back to the engineer's own listen never is.
    if (m == SoloMode::InPlace && liveSafeRefuses (LiveAction::SoloInPlace)) return;
    kept.monitor.mode = m;
    if (plan) { plan->proposed.monitor.mode = m; plan->before.monitor.mode = m; }
    publish();
    if (onMessage)
        onMessage (m == SoloMode::InPlace
                       ? std::string ("Careful: solo is now heard by everyone, not just you. That is for mixing a "
                                      "recording, not for a service.")
                       : std::string ("Solo goes to your own device only. The room and the stream never hear it."));
    touch();
}

void MixController::setSoloPoint (SoloPoint pt)
{
    if (kept.monitor.point == pt) return;
    kept.monitor.point = pt;
    if (plan) { plan->proposed.monitor.point = pt; plan->before.monitor.point = pt; }
    publish();
    touch();
}

void MixController::setMonitorGain (float db)
{
    kept.monitor.gainDb = clamp (db, -60.0f, 12.0f);
    if (plan) { plan->proposed.monitor.gainDb = kept.monitor.gainDb; plan->before.monitor.gainDb = kept.monitor.gainDb; }
    publish();
    touch();
}

void MixController::setMonitorDim (bool on)
{
    if (kept.monitor.dim == on) return;
    kept.monitor.dim = on;
    if (plan) { plan->proposed.monitor.dim = on; plan->before.monitor.dim = on; }
    publish();
    touch();
}

void MixController::setMonitorMute (bool on)
{
    if (kept.monitor.mute == on) return;
    kept.monitor.mute = on;
    if (plan) { plan->proposed.monitor.mute = on; plan->before.monitor.mute = on; }
    publish();
    touch();
}

void MixController::setMonitorSource (MixBus b)
{
    if (b == MixBus::Count || kept.monitor.source == b) return;
    kept.monitor.source = b;
    if (plan) { plan->proposed.monitor.source = b; plan->before.monitor.source = b; }
    publish();
    touch();
}

bool MixController::anyFxSolo() const noexcept
{
    for (int f = 0; f < int (FxSlot::Count); ++f) if (kept.fx[size_t (f)].solo) return true;
    return false;
}

void MixController::setFxSoloAll (bool solo)
{
    const auto& used = graph.fxUsed;
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        const bool want = solo && used[size_t (f)];
        kept.fx[size_t (f)].solo = want;
        bothSides ([&] (MixParameters& m) { m.fx[size_t (f)].solo = want; });
    }
    publish();
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    touch();
}

void MixController::setFxSolo (FxSlot slot, bool solo)
{
    if (int (slot) < 0 || int (slot) >= int (FxSlot::Count)) return;
    kept.fx[size_t (slot)].solo = solo;
    bothSides ([&] (MixParameters& m) { m.fx[size_t (slot)].solo = solo; });
    publish();
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    touch();
}

// ---- Advanced edits ----

namespace
{
    bool validStrip (const MixParameters& p, int strip) { return strip >= 0 && strip < p.numStrips; }
}

void MixController::setStripFader (int strip, float db, bool withLink)
{
    if (! validStrip (kept, strip)) return;
    if (autopilot.on) autopilotRelearn();        // the engineer is setting a new balance: learn it
    float want = clamp (db, -60.0f, 12.0f);
    liveSafe::Verdict v;
    const float from = kept.strips[size_t (strip)].faderDb;
    want = liveSafe::limitStepDb (safety, LiveAction::Fader, from, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.strips[size_t (strip)].faderDb = want;
    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].faderDb = kept.strips[size_t (strip)].faderDb; });
    // The link: the same move, in dB, on every other member. The step was already limited on
    // the held strip, so under LIVE SAFE no member moves further than it could have on its own.
    // A member at the end of its travel stops there; the others keep going, the way a console's
    // fader group does - a link keeps a balance, it cannot invent headroom.
    const float delta = want - from;
    if (withLink && std::fabs (delta) > 1e-4f)
        for (int other : linkedWith (strip))
        {
            auto& s = kept.strips[size_t (other)];
            s.faderDb = clamp (s.faderDb + delta, -60.0f, 12.0f);
            bothSides ([&] (MixParameters& m) { m.strips[size_t (other)].faderDb = s.faderDb; });
        }
    publish();
    touch();
}

// ---- Linked faders ----

int MixController::getStripLink (int strip) const noexcept
{
    return validStrip (kept, strip) ? kept.strips[size_t (strip)].linkGroup : 0;
}

std::vector<int> MixController::linkedWith (int strip) const
{
    std::vector<int> out;
    const int group = getStripLink (strip);
    if (group == 0) return out;
    for (int i = 0; i < kept.numStrips; ++i)
        if (i != strip && kept.strips[size_t (i)].linkGroup == group) out.push_back (i);
    return out;
}

std::string MixController::linkedNames (int strip) const
{
    std::string out;
    const auto& inputs = built ? builtSession.inputs : session.inputs;
    for (int other : linkedWith (strip))
    {
        if (! out.empty()) out += ", ";
        out += other < int (inputs.size()) ? inputs[size_t (other)].name : "channel " + std::to_string (other + 1);
    }
    return out;
}

int MixController::linkStrips (const std::vector<int>& strips)
{
    std::vector<int> members;
    for (int s : strips)
        if (validStrip (kept, s) && std::find (members.begin(), members.end(), s) == members.end()) members.push_back (s);
    if (members.size() < 2) return 0;

    // A member that is already linked brings its whole group along: linking the left overhead
    // to a room microphone when it is already linked to the right one makes three, not a new
    // pair that silently steals it. The lowest existing group wins, else a fresh number.
    int group = 0, highest = 0;
    for (int i = 0; i < kept.numStrips; ++i) highest = std::max (highest, kept.strips[size_t (i)].linkGroup);
    for (int s : members)
        if (const int g = kept.strips[size_t (s)].linkGroup; g != 0 && (group == 0 || g < group)) group = g;
    if (group == 0) group = highest + 1;

    markMixChange ("linking faders");
    std::vector<int> joining;
    for (int s : members)
        if (const int g = kept.strips[size_t (s)].linkGroup; g != 0 && g != group) joining.push_back (g);
    for (int i = 0; i < kept.numStrips; ++i)
    {
        auto& s = kept.strips[size_t (i)];
        if (std::find (joining.begin(), joining.end(), s.linkGroup) != joining.end()) s.linkGroup = group;
    }
    for (int s : members) kept.strips[size_t (s)].linkGroup = group;
    // Linking starts the members level: a pair of overheads linked at +1.5 and -6.9 is not a
    // pair yet. Every member of the group takes the level of the channel the link was made
    // from (the first one asked for), under the LIVE SAFE step like any other fader move, so
    // mid-service the far one comes as close as the policy allows and says so.
    const float lead = kept.strips[size_t (members.front())].faderDb;
    bool limited = false;
    liveSafe::Verdict verdict;
    for (int i = 0; i < kept.numStrips; ++i)
    {
        auto& s = kept.strips[size_t (i)];
        if (s.linkGroup != group || i == members.front()) continue;
        liveSafe::Verdict v;
        s.faderDb = liveSafe::limitStepDb (safety, LiveAction::Fader, s.faderDb, lead, v);
        if (v.limited) { limited = true; verdict = v; }
        bothSides ([&] (MixParameters& m) { m.strips[size_t (i)].faderDb = s.faderDb; });
    }
    // TWO MONO CHANNELS OF ONE INSTRUMENT ARE ITS LEFT AND RIGHT. "keys1L" and "keys1r" linked
    // are a stereo keyboard, and a stereo keyboard panned to the middle is a mono one: the
    // pair is spread to its own width when the link is made. Which side is which comes from the
    // name ("L", "Left", "R"), else from the patch order. Anything else - three microphones, two
    // different instruments - is a level link only, as it always was.
    {
        std::vector<int> pair;
        for (int i = 0; i < kept.numStrips; ++i) if (kept.strips[size_t (i)].linkGroup == group) pair.push_back (i);
        if (pair.size() == 2 && pair[0] < graph.numStrips() && pair[1] < graph.numStrips())
        {
            const auto& a = graph.strips[size_t (pair[0])];
            const auto& b = graph.strips[size_t (pair[1])];
            const auto side = [] (const std::string& name) -> int
            {
                std::string n;
                for (char c : name) n += char (std::tolower ((unsigned char) c));
                while (! n.empty() && (n.back() == ' ' || std::isdigit ((unsigned char) n.back()))) n.pop_back();
                if (n.size() >= 4 && n.compare (n.size() - 4, 4, "left") == 0) return -1;
                if (n.size() >= 5 && n.compare (n.size() - 5, 5, "right") == 0) return 1;
                if (! n.empty() && n.back() == 'l') return -1;
                if (! n.empty() && n.back() == 'r') return 1;
                return 0;
            };
            const bool mono = a.inputB < 0 && b.inputB < 0;
            if (mono && roleFamily (a.role) == roleFamily (b.role))
            {
                int left = pair[0], right = pair[1];
                const int sa = side (a.name), sb = side (b.name);
                if ((sa > 0 && sb <= 0) || (sb < 0 && sa >= 0)) std::swap (left, right);
                const float width = MixProfile::stereoPairWidth (roleFamily (a.role));
                for (const auto& [strip, want] : { std::pair<int, float> { left, -width }, std::pair<int, float> { right, width } })
                {
                    liveSafe::Verdict v;
                    auto& s = kept.strips[size_t (strip)];
                    s.pan = liveSafe::limitStep (safety, LiveAction::Pan, s.pan, want, v);
                    if (v.limited) { limited = true; verdict = v; }
                    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].pan = s.pan; });
                }
                if (onMessage && ! limited)
                    onMessage (a.name + " and " + b.name + " are now one stereo source: panned left and right, "
                               "moved together, and tuned as one.");
            }
        }
    }
    if (limited && onMessage) onMessage (verdict.reason);
    if (plan) for (int i = 0; i < kept.numStrips; ++i)
    {
        plan->proposed.strips[size_t (i)].linkGroup = kept.strips[size_t (i)].linkGroup;
        plan->before.strips[size_t (i)].linkGroup = kept.strips[size_t (i)].linkGroup;
    }
    publish();
    touch();
    return group;
}

void MixController::unlinkStrip (int strip)
{
    if (! validStrip (kept, strip) || kept.strips[size_t (strip)].linkGroup == 0) return;
    markMixChange ("unlinking a fader");
    const int group = kept.strips[size_t (strip)].linkGroup;
    kept.strips[size_t (strip)].linkGroup = 0;
    // A group of one is not a group.
    int left = 0, last = -1;
    for (int i = 0; i < kept.numStrips; ++i)
        if (kept.strips[size_t (i)].linkGroup == group) { ++left; last = i; }
    if (left == 1) kept.strips[size_t (last)].linkGroup = 0;
    if (plan) for (int i = 0; i < kept.numStrips; ++i)
    {
        plan->proposed.strips[size_t (i)].linkGroup = kept.strips[size_t (i)].linkGroup;
        plan->before.strips[size_t (i)].linkGroup = kept.strips[size_t (i)].linkGroup;
    }
    publish();
    touch();
}

void MixController::setStripPan (int strip, float pan)
{
    if (! validStrip (kept, strip)) return;
    float want = clamp (pan, -1.0f, 1.0f);
    liveSafe::Verdict v;
    want = liveSafe::limitStep (safety, LiveAction::Pan, kept.strips[size_t (strip)].pan, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.strips[size_t (strip)].pan = want;
    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].pan = kept.strips[size_t (strip)].pan; });
    publish();
    touch();
}

void MixController::setStripInputGain (int strip, float db)
{
    if (! validStrip (kept, strip)) return;
    float want = clamp (db, -24.0f, 24.0f);
    liveSafe::Verdict v;
    want = liveSafe::limitStepDb (safety, LiveAction::InputGain, kept.strips[size_t (strip)].inputGainDb, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.strips[size_t (strip)].inputGainDb = want;
    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].inputGainDb = kept.strips[size_t (strip)].inputGainDb; });
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    touch();
}

void MixController::setStripMute (int strip, bool mute)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].mute = mute;
    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].mute = mute; });
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    touch();
}

void MixController::setStripSolo (int strip, bool solo)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].solo = solo;
    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].solo = solo; });
    // Solo follows the link: S on one overhead means "the overheads", from either member.
    for (int other : linkedWith (strip))
    {
        kept.strips[size_t (other)].solo = solo;
        bothSides ([&] (MixParameters& m) { m.strips[size_t (other)].solo = solo; });
    }
    publish();
    // Solo is safe (it never reaches the master) but it is only *useful* when a monitor
    // output exists. Saying so once beats an S key that appears to do nothing.
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    touch();
}

void MixController::setStripSend (int strip, FxSlot slot, float db)
{
    if (! validStrip (kept, strip)) return;
    float want = db <= -60.0f ? kSilenceDb : clamp (db, -60.0f, 6.0f);
    if (safety.on && want > kSilenceDb)
    {
        liveSafe::Verdict v;
        const float from = kept.strips[size_t (strip)].sendDb[size_t (slot)];
        want = liveSafe::limitStepDb (safety, LiveAction::Send, from > kSilenceDb ? from : -60.0f, want, v);
        if (v.limited && onMessage) onMessage (v.reason);
    }
    kept.strips[size_t (strip)].sendDb[size_t (slot)] = want;
    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].sendDb[size_t (slot)] = kept.strips[size_t (strip)].sendDb[size_t (slot)]; });
    publish();
    touch();
}

bool MixController::stripCanHaveEffects (int strip) const
{
    if (strip < 0 || strip >= graph.numStrips()) return false;
    const auto role = graph.strips[size_t (strip)].role;
    if (productOf (role) != Product::Vocals || isBusFamily (roleFamily (role))) return false;
    for (int f = 0; f < int (FxSlot::Count); ++f)
        if (graph.fxUsed[size_t (f)]) return true;
    return false;
}

bool MixController::stripEffectsOn (int strip) const
{
    if (! validStrip (kept, strip)) return false;
    const auto& s = kept.strips[size_t (strip)];
    if (s.effectsOff) return false;
    for (int f = 0; f < int (FxSlot::Count); ++f)
        if (graph.fxUsed[size_t (f)] && s.sendDb[size_t (f)] > kSilenceDb) return true;
    return false;
}

void MixController::setStripEffects (int strip, bool on)
{
    if (! validStrip (kept, strip) || ! stripCanHaveEffects (strip)) return;
    if (stripEffectsOn (strip) == on) return;
    auto& s = kept.strips[size_t (strip)];
    s.effectsOff = ! on;

    // ON for a channel that has never had a send: a speaking microphone is dry by profile, so
    // there is nothing to un-gate. Seed it from what this profile gives a lead vocal - the same
    // table TUNE plans from - and only for the returns the session actually has. From then on
    // the levels are the engineer's and this switch only gates them.
    if (on)
    {
        bool anySend = false;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (graph.fxUsed[size_t (f)] && s.sendDb[size_t (f)] > kSilenceDb) anySend = true;
        if (! anySend)
            for (int f = 0; f < int (FxSlot::Count); ++f)
                if (graph.fxUsed[size_t (f)])
                    s.sendDb[size_t (f)] = MixProfile::defaultSendDb (session.profile, RoleFamily::LeadVocal, FxSlot (f));
    }

    bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].effectsOff = s.effectsOff; m.strips[size_t (strip)].sendDb = s.sendDb; });
    publish();
    touch();
}

void MixController::setBusFader (MixBus bus, float db)
{
    if (bus == MixBus::Count) return;
    // THE PERSON AT THE DESK OUTRANKS THE MACHINE STANDING IN FOR THEM. A move on a group
    // Autopilot has been holding hands that group straight back: it stops correcting it for
    // this engagement, and says so once.
    if (autopilot.on && ! autopilotMoving && int (bus) < int (MixBus::Master)
        && ! autopilot.released[size_t (bus)])
    {
        autopilot.released[size_t (bus)] = true;
        if (onMessage)
            onMessage (std::string (mixBusName (bus)) + " is yours again. Autopilot stops correcting it until "
                       "you engage it afresh.");
    }
    // Any move by the engineer is the new mix: learn it, rather than pull the other groups back
    // towards a balance the person at the desk has just changed.
    if (autopilot.on && ! autopilotMoving) autopilotRelearn();
    float want = clamp (db, -60.0f, 12.0f);
    liveSafe::Verdict v;
    want = liveSafe::limitStepDb (safety, bus == MixBus::Master ? LiveAction::MasterFader : LiveAction::Fader,
                                  kept.buses[size_t (bus)].faderDb, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.buses[size_t (bus)].faderDb = want;
    bothSides ([&] (MixParameters& m) { m.buses[size_t (bus)].faderDb = kept.buses[size_t (bus)].faderDb; });
    publish();
    touch();
}

void MixController::setBusMute (MixBus bus, bool mute)
{
    if (bus == MixBus::Count) return;
    kept.buses[size_t (bus)].mute = mute;
    bothSides ([&] (MixParameters& m) { m.buses[size_t (bus)].mute = mute; });
    publish();
    touch();
}

void MixController::setFxReturn (float db)
{
    kept.fxReturnDb = clamp (db, -60.0f, 12.0f);
    bothSides ([&] (MixParameters& m) { m.fxReturnDb = kept.fxReturnDb; });
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    touch();
}

void MixController::setFxMute (bool mute)
{
    kept.fxMute = mute;
    bothSides ([&] (MixParameters& m) { m.fxMute = mute; });
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    touch();
}

void MixController::setBusSolo (MixBus bus, bool solo)
{
    if (bus == MixBus::Master || bus == MixBus::Count) return;
    kept.buses[size_t (bus)].solo = solo;
    bothSides ([&] (MixParameters& m) { m.buses[size_t (bus)].solo = solo; });
    publish();
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    touch();
}

void MixController::setStripChannel (int strip, const ChannelParameters& c)
{
    if (! validStrip (kept, strip)) return;
    markMixChange ("a processing change");
    const StripParameters was = kept.strips[size_t (strip)];
    if (! was.channel.replaceEnabled && c.replaceEnabled && strip < int (session.inputs.size()))
        usage ({ "sample_replacement_on", { { "instrument", roleFamilyId (roleFamily (session.inputs[size_t (strip)].role)) },
                                            { "by", "hand" } }, {} });
    auto safe = c;
    sanitizeChannelParameters (safe);          // the fence every value crosses on its way to the audio
    kept.strips[size_t (strip)].channel = safe;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].channel = safe;
    recordStripTune (strip, "Inspector edit", was, kept.strips[size_t (strip)]);
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    touch();
}

// The engine reads the table from a pointer on the audio thread, and every strip picks its
// own bank out of it when a parameter snapshot arrives - so a table published on its own
// (the engineer imported a sound) reaches nobody until the mix is published again.
void MixController::setSampleBanks (const SampleBankTable* table, bool publishNow)
{
    engine.setSampleBanks (table);
    if (publishNow) publish();
}

void MixController::repointSamples (const std::vector<std::pair<int, ChannelParameters>>& changes)
{
    for (const auto& [strip, c] : changes)
    {
        if (! validStrip (kept, strip)) continue;
        auto safe = c;
        sanitizeChannelParameters (safe);
        kept.strips[size_t (strip)].channel = safe;
        atCapture.strips[size_t (strip)].channel.replaceSound = safe.replaceSound;
        atCapture.strips[size_t (strip)].channel.replaceEnabled = safe.replaceEnabled;
        bothSides ([&] (MixParameters& m) { m.strips[size_t (strip)].channel.replaceSound = safe.replaceSound;
                                            m.strips[size_t (strip)].channel.replaceEnabled = safe.replaceEnabled; });
    }
    publish();
    if (! changes.empty()) touch();
}

bool MixController::auditionSample (int strip)
{
    if (! prepared || ! validStrip (kept, strip) || strip >= graph.numStrips()) return false;
    const auto& route = graph.strips[size_t (strip)];
    const SampleBankTable* table = engine.getSampleBanks();
    const SampleBank* bank = table != nullptr ? table->bank (roleFamily (route.role), kept.strips[size_t (strip)].channel.replaceSound) : nullptr;
    if (bank == nullptr || bank->empty())
    {
        if (onMessage) onMessage ("There is no sound to hear on " + route.name + ".");
        return false;
    }
    // No private listen: with solo in place (everyone hears solo - a rehearsal, never under
    // LIVE SAFE) the sound plays where solo does, on the main output. Otherwise it is refused
    // with the sentence that says what to do.
    const bool onMain = ! hasMonitorOutput() && kept.monitor.mode == SoloMode::InPlace && ! safety.on;
    if (! hasMonitorOutput() && ! onMain)
    {
        if (onMessage)
        {
            const std::string here = " To hear it on this device, choose \"Here - everyone hears solo\" in the Solo picker on LIVE "
                                     "(not during a service); for a listen only you hear, plug in headphones or an interface and pick it there.";
            if (hasMonitorFeed (outputs) && engine.getDeviceOutputs() > 0)
            {
                // Routed, but past the end of the device that is open: say which pair and how many there are.
                int left = -1;
                for (int i = 0; i < outputs.count && i < kMaxOutputFeeds; ++i)
                    if (outputs.feeds[size_t (i)].monitor && outputs.feeds[size_t (i)].routed()) { left = std::max (outputs.feeds[size_t (i)].left, 0); break; }
                onMessage ("Your own listen is set to outputs " + std::to_string (left + 1) + "-" + std::to_string (left + 2)
                           + ", and the device that is open has only " + std::to_string (engine.getDeviceOutputs()) + "." + here);
            }
            else
                onMessage ("Solo has nowhere to go yet, so there is nowhere to hear it." + here);
        }
        return false;
    }
    engine.auditionSample (bank, kept.strips[size_t (strip)].channel.replaceGainDb, onMain);
    return true;
}

// ---- Track history ----

const std::vector<StripTuneRecord>& MixController::getStripHistory (int strip) const
{
    static const std::vector<StripTuneRecord> none;
    if (strip < 0 || strip >= kMaxStrips) return none;
    return stripHistory[size_t (strip)];
}

void MixController::recordStripTune (int strip, const std::string& what, const StripParameters& before, const StripParameters& after)
{
    if (strip < 0 || strip >= kMaxStrips || ! stripTuneDiffers (before, after)) return;
    StripTuneRecord r;
    r.strip = strip;
    r.what = what;
    r.tune = tuneCount;
    r.whenMs = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
    r.before = before;
    r.after = after;
    // Keys are not a setting: what is put back never mutes, solos or links a channel.
    r.before.mute = r.after.mute = false;
    r.before.solo = r.after.solo = false;
    r.before.linkGroup = r.after.linkGroup = 0;
    auto& list = stripHistory[size_t (strip)];
    list.push_back (std::move (r));
    if (int (list.size()) > kMaxStripHistory) list.erase (list.begin());
}

bool MixController::restoreStripTune (int strip, int record)
{
    if (! validStrip (kept, strip)) return false;
    const auto& list = stripHistory[size_t (strip)];
    if (record < 0 || record >= int (list.size())) return false;
    // A put-back is an Inspector edit in every way that matters to LIVE SAFE: the chain is
    // let through, and the level and the gain move by no more than a step would.
    if (liveSafeRefuses (LiveAction::ChannelProcessing)) return false;
    const StripTuneRecord chosen = list[size_t (record)];      // a copy: the list grows below
    auto& s = kept.strips[size_t (strip)];
    const StripParameters was = s;
    const std::string name = strip < session.numStrips() ? session.inputs[size_t (strip)].name : std::string ("this channel");
    markMixChange ("putting " + name + " back");

    liveSafe::Verdict v;
    s.channel = chosen.after.channel;
    s.inputGainDb = liveSafe::limitStepDb (safety, LiveAction::InputGain, s.inputGainDb, chosen.after.inputGainDb, v);
    const bool gainLimited = v.limited;
    s.faderDb = liveSafe::limitStepDb (safety, LiveAction::Fader, s.faderDb, chosen.after.faderDb, v);
    const bool faderLimited = v.limited;
    s.pan = chosen.after.pan;
    s.sendDb = chosen.after.sendDb;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)] = s;
    recordStripTune (strip, "Put back: " + chosen.what, was, s);
    publish();
    mark ("Put back on " + graph.strips[size_t (strip)].name);
    if (onMessage)
    {
        std::string m = name + " is back to what " + chosen.what + " set.";
        if (gainLimited || faderLimited) m += " LIVE SAFE kept the level move to one step; press again for the rest.";
        onMessage (m);
    }
    return true;
}

std::vector<StripTuneRecord> MixController::getAllStripHistory() const
{
    std::vector<StripTuneRecord> out;
    for (const auto& list : stripHistory) out.insert (out.end(), list.begin(), list.end());
    return out;
}

void MixController::restoreStripHistory (const std::vector<StripTuneRecord>& records)
{
    for (auto& h : stripHistory) h.clear();
    for (const auto& r : records)
    {
        if (r.strip < 0 || r.strip >= kMaxStrips) continue;
        auto& list = stripHistory[size_t (r.strip)];
        list.push_back (r);
        if (int (list.size()) > kMaxStripHistory) list.erase (list.begin());
    }
}

void MixController::carryStripHistory (const std::vector<StripTuneRecord>& records, const MixSession& previousSession)
{
    restoreStripHistory (livemix::carryStripHistory (records, previousSession, session));
}

void MixController::setBusChannel (MixBus bus, const ChannelParameters& c)
{
    if (bus == MixBus::Count) return;
    // The master limiter is the ceiling on everything that goes out. Under LIVE SAFE it stays
    // on, and the master is never bypassed, whatever else about its chain may change.
    if (bus == MixBus::Master && safety.on)
    {
        const auto& now = kept.master().channel;
        if ((now.limiterEnabled && ! c.limiterEnabled) || (! now.bypassAll && c.bypassAll))
        {
            if (onMessage) onMessage ("LIVE SAFE: the master limiter stays on while the service is running - it is the only "
                                      "ceiling on what goes out.");
            return;
        }
    }
    markMixChange (std::string (mixBusName (bus)) + " processing");
    auto safe = c;
    sanitizeChannelParameters (safe);
    kept.buses[size_t (bus)].channel = safe;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].channel = safe;
    autopilotRelearn();                 // the engineer changed the mix: hold the new one, never fight it
    publish();
    touch();
}

void MixController::clearSolos()
{
    bool changed = false;
    for (int i = 0; i < kept.numStrips; ++i)
        if (kept.strips[size_t (i)].solo) { kept.strips[size_t (i)].solo = false; changed = true; }
    for (int b = 0; b < int (MixBus::Count); ++b)
        if (kept.buses[size_t (b)].solo) { kept.buses[size_t (b)].solo = false; changed = true; }
    for (int f = 0; f < int (FxSlot::Count); ++f)
        if (kept.fx[size_t (f)].solo) { kept.fx[size_t (f)].solo = false; changed = true; }
    if (plan && stage == Stage::Preview)
    {
        for (int i = 0; i < plan->proposed.numStrips; ++i) plan->proposed.strips[size_t (i)].solo = false;
        for (int b = 0; b < int (MixBus::Count); ++b) plan->proposed.buses[size_t (b)].solo = false;
        for (int f = 0; f < int (FxSlot::Count); ++f) plan->proposed.fx[size_t (f)].solo = false;
    }
    if (! changed) return;
    publish();
    touch();
}

void MixController::setKept (const MixParameters& p)
{
    kept = p;
    kept.numStrips = std::min (kept.numStrips, graph.numStrips());
    // Whether a return exists belongs to the routing, not to whatever mix is being put back: a
    // document saved while the carry-across bug held every return off opens with them working.
    for (int f = 0; f < int (FxSlot::Count); ++f) kept.fx[size_t (f)].enabled = graph.fxUsed[size_t (f)];
    mixed = true;
    if (stage == Stage::Ready) stage = Stage::Mixed;
    publish();
}

void MixController::restoreKept (const MixParameters& p, int tunes)
{
    setKept (p);
    tuneCount = std::max (tuneCount, tunes);
}

void MixController::carryKept (const MixParameters& previousMix, const MixSession& previousSession, int tunes)
{
    // `kept` is the rebuilt session's baselines at this point, so an input that is new to the
    // session - or one that became a different source - starts from its own rather than from
    // whatever happened to be at that index before.
    restoreKept (carryMix (previousMix, previousSession, kept, session), tunes);
}

// ---- Gain staging ----

namespace
{
    std::string upperCase (std::string s)
    {
        for (auto& c : s) c = (char) std::toupper ((unsigned char) c);
        return s;
    }
}

MixController::InputAdvice MixController::liveCaptureAdvice (ChannelRole role, float peakHoldDb) const
{
    InputAdvice a;
    a.known = true;
    a.capturePeakDb = peakHoldDb;

    auto move = [] (float db)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.0f", std::fabs (db));
        return std::string (buf);
    };

    if (peakHoldDb <= -70.0f)
    {
        a.level = InputAdvice::Level::NotHeard;
        a.headline = "NO SIGNAL";
        a.detail = "Nothing has arrived on this input. Check that it is plugged in where you think it is, that the "
                   "channel is not muted at the desk, and that phantom power is on if the microphone needs it.";
        return a;
    }

    // The same band Tune measures against, for this source, out of the profile.
    const auto t = Profiles::targets (session.profile, role);
    const float centre = 0.5f * (t.capturePeakMinDb + t.capturePeakMaxDb);
    const float step = t.captureGainMaxStepDb;
    const float wanted = std::max (-step, std::min (step, std::round (centre - peakHoldDb)));
    a.consoleMoveDb = wanted;

    if (peakHoldDb > -0.5f)
    {
        a.level = InputAdvice::Level::Clipping;
        a.headline = "CLIPPING - PREAMP DOWN " + move (wanted) + " dB";
        a.detail = "This input has already reached full scale. Digital clipping cannot be repaired after the "
                   "converter, so it has to come down at the desk before anything else is worth doing.";
    }
    else if (peakHoldDb > t.capturePeakMaxDb)
    {
        a.level = InputAdvice::Level::Hot;
        a.headline = "PREAMP DOWN " + move (wanted) + " dB";
        a.detail = "It is louder than this source's safe range and has no room left for the loudest moment of the "
                   "service. Turn the preamp down at the desk.";
    }
    else if (peakHoldDb < -55.0f)
    {
        a.level = InputAdvice::Level::Faint;
        a.headline = "CHECK THIS INPUT";
        a.detail = "Something is arriving, but far too quietly to be a source that is really playing. Check the "
                   "microphone, the cable and the preamp before turning anything up.";
        a.consoleMoveDb = 0.0f;
    }
    else if (peakHoldDb < t.capturePeakMinDb)
    {
        a.level = InputAdvice::Level::Low;
        a.headline = "PREAMP UP " + move (wanted) + " dB";
        a.detail = "It is quieter than this source's safe range. Turn the preamp up at the desk rather than here: "
                   "gain added after the converter lifts the preamp's own noise with the source.";
    }
    else
    {
        a.level = InputAdvice::Level::Healthy;
        a.headline = "OK";
        a.consoleMoveDb = 0.0f;
        a.detail = "This input is arriving at a level the processing can work with.";
    }
    return a;
}

MixController::InputAdvice MixController::getInputAdvice (int strip) const
{
    InputAdvice a;
    if (! plan || strip < 0 || strip >= int (plan->strips.size())) return a;
    const auto& sp = plan->strips[size_t (strip)];
    a.known = true;
    a.capturePeakDb = sp.capturePeakDb;
    a.digitalGainDb = sp.inputGainDb;

    auto sentence = [&] (Recommendation::Kind kind) -> std::string
    {
        for (const auto& r : sp.mixItems) if (r.kind == kind) return r.why;
        if (sp.tune.valid) for (const auto& r : sp.tune.report.items) if (r.kind == kind) return r.why;
        return {};
    };

    if (sp.faint)
    {
        a.level = InputAdvice::Level::Faint;
        a.headline = "CHECK THIS INPUT";
        a.detail = sentence (Recommendation::Kind::Info);
        if (a.detail.empty())
            a.detail = "Its loudest moment during the listen was too quiet to be a source that is really playing. "
                       "Check the microphone, the cable and the preamp, then TUNE MIX again.";
        return a;
    }
    if (sp.bleedOnly)
    {
        a.level = InputAdvice::Level::Bleed;
        a.headline = "HEARD AS SPILL";
        a.detail = sentence (Recommendation::Kind::Info);
        return a;
    }
    if (! sp.heard)
    {
        a.level = InputAdvice::Level::NotHeard;
        a.headline = "NOT HEARD";
        a.detail = "Nothing played on this input during the listen, so nothing was decided about it. RE-TUNE while it plays.";
        return a;
    }

    // Heard. The report is the one taken after DLIVE's own digital gain, so what is left
    // is the console preamp itself.
    const auto& report = sp.tune.report;
    a.consoleMoveDb = sp.tune.valid ? report.suggestedCaptureGainDb : 0.0f;
    const std::string health = sp.tune.valid ? report.inputHealth : std::string ("Healthy");
    auto move = [] (float db)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.0f", std::fabs (db));
        return std::string (buf);
    };

    if (health == "Clipping")
    {
        a.level = InputAdvice::Level::Clipping;
        a.headline = "CLIPPING - TURN THE PREAMP DOWN " + move (a.consoleMoveDb) + " dB";
    }
    else if (health == "Hot")
    {
        a.level = InputAdvice::Level::Hot;
        a.headline = "TURN THE PREAMP DOWN " + move (a.consoleMoveDb) + " dB";
    }
    else if (health == "Low")
    {
        a.level = InputAdvice::Level::Low;
        a.headline = "TURN THE PREAMP UP " + move (a.consoleMoveDb) + " dB";
    }
    else if (std::fabs (sp.inputGainDb) >= MixProfile::relationships (session.profile).digitalGainAdviceDb)
    {
        // The level works, but only because DLIVE moved it a long way digitally. Doing it at
        // the desk instead keeps the noise floor down, so the app says so rather than hiding it.
        const bool up = sp.inputGainDb > 0.0f;
        a.level = InputAdvice::Level::Digital;
        a.consoleMoveDb = sp.inputGainDb;
        a.headline = std::string ("PREAMP ") + (up ? "UP " : "DOWN ") + move (sp.inputGainDb) + " dB AT THE DESK";
        a.detail = std::string ("DLIVE is running ") + (up ? "+" : "-") + move (sp.inputGainDb)
                 + " dB of digital input gain to bring this source up to a level the processing can work with. "
                 + (up ? "That works, but it lifts the preamp's own noise with the source: set the gain on the desk instead, then RE-TUNE."
                       : "Set the gain on the desk instead and the converter keeps its headroom, then RE-TUNE.");
        return a;
    }
    else
    {
        a.level = InputAdvice::Level::Healthy;
        a.headline = "HEALTHY";
        a.consoleMoveDb = 0.0f;
        a.detail = std::fabs (sp.inputGainDb - sp.inputGainBeforeDb) >= 0.5f
                       ? sentence (Recommendation::Kind::CaptureGain)
                       : std::string ("This input arrives at a level the processing can work with. Nothing to do.");
        if (a.detail.empty()) a.detail = "This input arrives at a level the processing can work with. Nothing to do.";
        return a;
    }
    a.detail = sentence (Recommendation::Kind::CaptureGain);
    if (a.detail.empty())
        a.detail = "The level reaching DLIVE is outside the healthy range for this source. Set the preamp on the desk, "
                   "then TUNE MIX again: gain staging comes before anything else in the mix.";
    return a;
}

// ---- Health ----

namespace
{
    // The three things the last listen can say about an input.
    struct HealthCount { int assigned = 0, good = 0, faint = 0, silent = 0, preamp = 0; };

    HealthCount countHealth (const MixPlan& plan, float digitalGainAdviceDb)
    {
        HealthCount c;
        for (const auto& sp : plan.strips)
        {
            ++c.assigned;
            if (sp.faint) { ++c.faint; continue; }
            if (! sp.heard) { ++c.silent; continue; }
            // Heard. Healthy when the level reaching the chain is inside the profile's range AND DLIVE did not
            // have to move it a long way digitally to get there: gain staging belongs on the desk, so an input that
            // only works because of a big digital raise is not a healthy input, it is a preamp waiting to be set.
            const bool healthy = sp.bleedOnly || ! sp.tune.valid
                                 || (sp.tune.report.inputHealth == "Healthy"
                                     && std::fabs (sp.inputGainDb) < digitalGainAdviceDb);
            if (healthy) ++c.good; else ++c.preamp;
        }
        return c;
    }
}

int MixController::getMixHealthPercent() const
{
    if (graph.numStrips() == 0 || ! plan) return 0;
    const HealthCount c = countHealth (*plan, MixProfile::relationships (session.profile).digitalGainAdviceDb);
    if (c.assigned == 0) return 0;
    return int (std::round (100.0f * float (c.good) / float (c.assigned)));
}

std::vector<std::string> MixController::getMixHealthNotes() const
{
    std::vector<std::string> notes;
    if (graph.numStrips() == 0) return notes;
    if (! plan)
    {
        if (mixed) notes.push_back ("Mix restored from the last session. RE-TUNE when the band plays.");
        return notes;
    }
    const HealthCount c = countHealth (*plan, MixProfile::relationships (session.profile).digitalGainAdviceDb);
    auto plural = [] (int n, const char* one, const char* many) { return std::to_string (n) + " " + (n == 1 ? one : many); };
    if (c.faint > 0)  notes.push_back (plural (c.faint, "input barely reached DLIVE: check its mic and cable.", "inputs barely reached DLIVE: check their mics and cables."));
    if (c.silent > 0) notes.push_back (plural (c.silent, "input was not heard: RE-TUNE while it plays.", "inputs were not heard: RE-TUNE while they play."));
    if (c.preamp > 0)
    {
        // Gain staging is the first move, so the note names the inputs rather than counting them.
        std::string names;
        int listed = 0;
        for (int i = 0; i < int (plan->strips.size()); ++i)
        {
            const auto advice = getInputAdvice (i);
            if (advice.level != InputAdvice::Level::Low && advice.level != InputAdvice::Level::Hot
                && advice.level != InputAdvice::Level::Clipping && advice.level != InputAdvice::Level::Digital) continue;
            if (listed < 3) names += (listed == 0 ? "" : ", ") + upperCase (plan->strips[size_t (i)].name);
            ++listed;
        }
        if (listed > 3) names += " and " + std::to_string (listed - 3) + " more";
        notes.push_back ("Set the preamp for " + names + " at the console, then RE-TUNE: gain staging comes first.");
    }
    if (notes.empty() && c.good == c.assigned) notes.push_back ("Every input was heard at a healthy level.");
    return notes;
}

} // namespace livemix
