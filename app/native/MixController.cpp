#include "MixController.h"
#include "Core/DbUtils.h"
#include "Profiles/MixProfileData.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace livemix
{

MixController::MixController() = default;
MixController::~MixController()
{
    engine.setTap (nullptr);
    capture.abort();
}

void MixController::setSession (const MixSession& s)
{
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
    plan.reset();
    tuningStrip = -1;
    if (! prepared) stage = Stage::Setup;
    else if (stage == Stage::Listening || stage == Stage::Planning || stage == Stage::Preview)
    {
        // A listen or a preview described the graph that is being replaced, so it cannot
        // stand - but the mix underneath it is still audible and still the user's.
        capture.abort();
        stage = restingStage();
    }
}

void MixController::setInputName (int strip, const std::string& name)
{
    if (strip < 0 || strip >= int (session.inputs.size()) || name.empty()) return;
    session.inputs[size_t (strip)].name = name;
    engine.setStripName (strip, name);
}

void MixController::setInputIcon (int strip, const std::string& icon)
{
    if (strip < 0 || strip >= int (session.inputs.size())) return;
    session.inputs[size_t (strip)].icon = icon;
    engine.setStripIcon (strip, icon);
}

void MixController::setPurpose (MixPurpose p) { session.purpose = p; }

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
    if (onMixChanged) onMixChanged();
}

void MixController::setVoicing (MasterVoicing v)
{
    if (session.voicing == v) return;
    session.voicing = v;
    publish();
    if (onMessage)
        onMessage (v == MasterVoicing::Neutral ? std::string ("Master sound: as tuned.")
                                               : std::string ("Master sound: ") + masterVoicingName (v) + ". " + masterVoicingHint (v));
    if (onMixChanged) onMixChanged();
}

MixController::LoudnessMove MixController::previewLoudnessMove() const
{
    LoudnessMove m;
    const auto loud = getMasterLoudness();
    m.targetLufs = loud.targetLufs;
    if (! prepared || engine.getNumStrips() == 0) { m.why = "Assign your inputs first: there is no mix to raise yet."; return m; }
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
    if (onMixChanged) onMixChanged();

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
void MixController::setProfile (StyleProfileId p) { session.profile = p; }

void MixController::prepare (double sr, int maxBlockSize)
{
    capture.abort();
    engine.setTap (nullptr);
    sampleRate = sr;
    blockSize = maxBlockSize;
    engine.prepare (sr, maxBlockSize, session);
    preparedSession = session;          // the graph and the mix below now belong to this session
    capture.prepare (sr, engine.getGraph());
    engine.setTap (&capture);
    // The engineer's own listen belongs to the device and the person at the desk, not to the
    // mix - exactly like the output feeds restored at the end of this function. Rebuilding the
    // graph must not reach into their headphones and put the level, the tap point and the solo
    // mode back to factory. It used to, so changing the assignments quietly undid whatever
    // they had set up to hear with.
    const MonitorState listen = kept.monitor;
    kept = startingPoint (session, engine.getGraph());
    kept.monitor = listen;
    atCapture = kept;
    plan.reset();
    compare = Compare::After;
    macros = MixMacroValues {};
    bypassed = false;
    tuneCount = 0;
    tuningStrip = -1;
    mixed = false;
    // The listen described the graph that has just been replaced, so it cannot be re-planned
    // from. The reference is a target rather than a measurement of this session, and survives.
    lastCapture = MixCapture::Result {};
    listened = false;
    // A rebuilt graph is a different mix: an undo step from before it would put the wrong
    // chain on the wrong input. The chat's answers go with it for the same reason.
    history.clear();
    future.clear();
    tuneLive.clearAnswers();
    stage = engine.getNumStrips() > 0 ? Stage::Ready : Stage::Setup;
    prepared = true;
    graphStale = false;          // the graph is the document again
    engine.setOutputFeeds (outputs);        // routing survives a rebuild; it belongs to the device, not the mix
    publish();
}

MixParameters MixController::compose() const
{
    // The verify listen of a TUNE LIVE MIX run has to hear what was applied, so the proposal
    // stays audible across it even though the stage says Listening.
    const bool previewing = plan && (stage == Stage::Preview || liveVerifying);
    const MixParameters& base = previewing ? (compare == Compare::Before ? plan->before : plan->proposed) : kept;
    if (bypassed)
    {
        // The console feed: no processing, no fader moves, no returns. Only the listening
        // controls (mute / solo) survive, so soloing one source still works while comparing.
        auto raw = startingPoint (session, engine.getGraph());
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
        return raw;
    }
    return MixMacros::applyVoicing (MixMacros::apply (base, macros, engine.getGraph(), session.profile),
                                    session.voicing, session.profile);
}

void MixController::setOutputFeeds (const OutputFeeds& f)
{
    // Moving the broadcast to a different pair of outputs mid-service is the one routing
    // change that is silent until it is too late. Changing only the *monitor* feed is always
    // allowed - it is the engineer's own listen and nobody else hears it.
    if (safety.on && ! onlyMonitorChanged (outputs, f) && liveSafeRefuses (LiveAction::OutputRouting)) return;
    outputs = f;
    // The broadcast and the engineer's listen are always a real stereo pair, whatever set
    // them - the sheet, a restored session, or the host wiring up two devices.
    normaliseOutputs (outputs);
    if (prepared) engine.setOutputFeeds (outputs);
    if (onMixChanged) onMixChanged();        // the session remembers where the cue goes
}

void MixController::setBypass (bool on)
{
    if (bypassed == on) return;
    if (on && liveSafeRefuses (LiveAction::Bypass)) return;
    bypassed = on;
    publish();                       // the kept mix is not touched, so there is nothing to save
}

void MixController::publish()
{
    if (! prepared) return;
    running = compose();
    engine.setParameters (running);
}

// ---- TUNE MIX ----

void MixController::startTuneMix (const ListenSettings& s)
{
    if (liveSafeRefuses (LiveAction::Tune)) return;
    startListening (s, -1);
}

// One source on its own. The listen is the same listen - every input is measured, so the
// channel is still decided in mix context - it just waits for this channel to play and
// keeps only this channel's part of the plan.
void MixController::startTuneChannel (int strip, const ListenSettings& s)
{
    if (strip < 0 || strip >= engine.getNumStrips()) return;
    if (liveSafeRefuses (LiveAction::TuneChannel)) return;
    startListening (s, strip);
}

void MixController::startListening (const ListenSettings& s, int strip)
{
    if (! prepared || stage == Stage::Listening || stage == Stage::Planning) return;
    // A new listen starts from what is audible now - except the verify listen of a live run,
    // which is deliberately listening to a proposal the user has not kept yet.
    if (stage == Stage::Preview && ! liveVerifying) keepPlan();
    listen = s;
    tuningStrip = strip;
    atCapture = running;                        // the faders and gains the listen will run with
    MixCapture::Settings cs;
    cs.seconds = s.seconds;
    cs.triggerDb = s.triggerDb;
    cs.maxWaitSeconds = s.maxWaitSeconds;
    cs.triggerStrip = strip;
    capture.start (cs);
    stage = Stage::Listening;
}

std::string MixController::getTuningName() const
{
    if (tuningStrip < 0 || tuningStrip >= int (session.inputs.size())) return {};
    return session.inputs[size_t (tuningStrip)].name;
}

// ---------------------------------------------------------------------------
// REFERENCE MIX: "make it sound like this"
// ---------------------------------------------------------------------------
void MixController::setReference (const ReferenceProfile& p)
{
    reference = p;
    if (onMixChanged) onMixChanged();      // the session remembers what it is aimed at
}

void MixController::clearReference()
{
    if (! reference.valid) return;
    reference = ReferenceProfile {};
    if (onMixChanged) onMixChanged();
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
    ctx.graph = engine.getGraph();
    ctx.current = kept;
    ctx.atCapture = lastCaptureAt;
    ctx.capture = lastCapture;
    ctx.reference = reference;
    tuningStrip = -1;
    stage = Stage::Planning;
    plan = MixPlanner::plan (ctx);

    if (plan->valid && plan->stripsHeard > 0)
    {
        ++tuneCount;
        stage = Stage::Preview;
        compare = Compare::After;
        if (onMessage) onMessage (plan->headline);
    }
    else
    {
        if (onMessage) onMessage (plan ? plan->headline : "MIX: NO SIGNAL");
        plan.reset();
        stage = restingStage();
    }
    publish();
}

void MixController::abortTuneMix()
{
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
                     tuneLive.hasProposal());
        return;
    }
    if (stage != Stage::Listening && stage != Stage::Planning) return;
    capture.abort();
    tuningStrip = -1;
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
    if (stage == Stage::Preview) keepPlan();     // a new run starts from what is audible now

    liveSettings = s;
    liveBefore = kept;                            // the complete pre-Tune snapshot: what REVERT goes back to
    liveRun = true;
    liveVerifying = false;

    TuneLiveCoordinator::Settings ts;
    ts.refinementPass = s.refinementPass;
    ts.userRequest = s.userRequest;
    ts.variation = s.variation;
    for (const auto& turn : chat)
    {
        if (turn.failed) continue;
        ts.conversation.push_back ({ turn.fromEngineer, turn.text });
    }
    tuneLive.setSettings (ts);
    tuneLive.beginListening (session.name);

    // Working from the listen DLIVE already has: the band does not play again, and two
    // readings are compared against the same performance rather than two different ones.
    if (s.reuseListen && listened && lastCapture.valid)
    {
        MixPlanContext ctx;
        ctx.session = session;
        ctx.graph = engine.getGraph();
        ctx.current = kept;
        ctx.atCapture = lastCaptureAt;
        ctx.capture = lastCapture;
        ctx.reference = reference;
        stage = Stage::Planning;
        plan = MixPlanner::plan (ctx);
        if (! plan || ! plan->valid || plan->stripsHeard == 0)
        {
            const std::string headline = plan ? plan->headline : std::string ("MIX: NO SIGNAL");
            plan.reset();
            tuneLive.cancel();
            endTuneLive (headline + " Nothing was changed.", false);
            return;
        }
        ++tuneCount;
        stage = Stage::Preview;
        compare = Compare::After;
        publish();
        tuneLive.onListenComplete (ctx, *plan);
        pollTuneLive();
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
    plan->parametersChanged = MixPlanner::countParameterChanges (plan->before, plan->proposed);
    plan->fadersChanged = 0;
    plan->gainsChanged = 0;
    plan->sendsChanged = 0;
    for (int i = 0; i < plan->proposed.numStrips && i < plan->before.numStrips; ++i)
    {
        const auto& a = plan->before.strips[size_t (i)];
        const auto& b = plan->proposed.strips[size_t (i)];
        if (std::fabs (a.faderDb - b.faderDb) >= 0.1f) ++plan->fadersChanged;
        if (std::fabs (a.inputGainDb - b.inputGainDb) >= 0.1f) ++plan->gainsChanged;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (std::fabs (a.sendDb[size_t (f)] - b.sendDb[size_t (f)]) >= 0.1f) { ++plan->sendsChanged; break; }
    }
    plan->noChangeRequired = plan->parametersChanged == 0 && plan->fadersChanged == 0 && plan->sendsChanged == 0;
    stage = Stage::Preview;
    compare = Compare::After;
    liveVerifying = false;
    publish();
    tuneLive.onApplied();
}

void MixController::endTuneLive (const std::string& message, bool keepProposal)
{
    liveRun = false;
    liveVerifying = false;
    if (! keepProposal)
    {
        plan.reset();
        stage = restingStage();
    }
    else stage = Stage::Preview;
    tuningStrip = -1;
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
        chat.push_back (reply);
        // A request that changed nothing should not leave an undo step that undoes nothing.
        if (! reply.applied && ! history.empty()) history.pop_back();
        return;                      // the chat already said it; no toast on top
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
                         tuneLive.hasProposal());
            break;
        }

        case State::Failed:
            // The reasoning layer failed. The deterministic mix from the same listen is
            // already sitting in `plan`, so the user is left with a professional mix and a
            // sentence saying what happened - never with a stopped mix and never with nothing.
            endTuneLive (tuneLive.getFailure(), plan.has_value());
            break;

        case State::Cancelled:
            endTuneLive ("TUNE LIVE MIX was stopped. Your mix has not been changed.", tuneLive.hasProposal());
            break;

        default: break;
    }
}

void MixController::poll()
{
    // A live run spends most of its time somewhere other than a listen - reasoning, resolving,
    // checking, applying - so its state machine is advanced whatever the stage says.
    if (liveRun && stage != Stage::Listening) { pollTuneLive(); return; }
    if (stage != Stage::Listening) return;
    const auto s = capture.getState();
    if (s == MixCapture::State::Complete)
    {
        stage = Stage::Planning;
        MixPlanContext ctx;
        ctx.session = session;
        ctx.graph = engine.getGraph();
        // A verify listen measured the applied proposal, so that - not the kept mix - is what
        // it has to be read against.
        ctx.current = (liveVerifying && plan) ? plan->proposed : kept;
        ctx.atCapture = atCapture;
        ctx.capture = capture.getResult();
        ctx.reference = reference;
        // Keep the listen. A reference added afterwards, and any re-plan, work from what the
        // band already played rather than asking them to play it again.
        lastCapture = ctx.capture;
        lastCaptureAt = ctx.atCapture;
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
        plan = MixPlanner::plan (ctx);

        if (liveRun)
        {
            // The professional mix is built first and always: whatever happens to the reasoning
            // pass from here, this is what the user is left with.
            if (! plan->valid || plan->stripsHeard == 0)
            {
                const std::string headline = plan ? plan->headline : std::string ("MIX: NO SIGNAL");
                plan.reset();
                tuneLive.cancel();
                endTuneLive (headline + " Nothing was changed.", false);
                return;
            }
            ++tuneCount;
            stage = Stage::Preview;
            compare = Compare::After;
            publish();
            tuneLive.onListenComplete (ctx, *plan);
            pollTuneLive();
            return;
        }

        // TUNE CHANNEL keeps only this channel's part of it; everything else is left exactly
        // where it is, so what is proposed is what the mix becomes when it is kept.
        const int channel = tuningStrip;
        if (channel >= 0) plan = MixPlanner::channelOnly (*plan, channel, session.profile);

        const bool heardIt = plan->valid && (channel < 0 ? plan->stripsHeard > 0
                                                         : channel < int (plan->strips.size()) && plan->strips[size_t (channel)].heard);
        if (heardIt)
        {
            ++tuneCount;
            stage = Stage::Preview;
            compare = Compare::After;
            if (onMessage) onMessage (plan->headline);
        }
        else if (channel >= 0 && plan->valid)
        {
            // The channel said nothing, but the listen still measured every input: the plan is
            // kept for what it knows (gain staging, mix health) and nothing is proposed.
            if (onMessage) onMessage (plan->headline);
            stage = restingStage();
        }
        else
        {
            if (onMessage) onMessage (plan ? plan->headline : "MIX: NO SIGNAL");
            plan.reset();
            tuningStrip = -1;
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
                         "Your mix has not been changed.", liveVerifying && plan.has_value());
            return;
        }
        if (onMessage) onMessage ("The listen was too short to measure. Tune Mix again while the band plays.");
        stage = restingStage();
    }
}

bool MixController::busHeard (MixBus bus) const noexcept
{
    const auto& g = engine.getGraph();
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
    if (! plan || stage != Stage::Preview) return;
    if (liveSafeRefuses (LiveAction::KeepPlan)) return;
    // KEEP replaces the whole mix, which is exactly the kind of change somebody wants a way
    // back from. A chat turn has already marked its own step, so it does not mark a second.
    if (! chatRun) markMixChange (isTuningChannel() ? "tune " + getTuningName() : std::string ("TUNE MIX"));
    kept = plan->proposed;
    mixed = true;
    stage = Stage::Mixed;
    compare = Compare::After;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::revertPlan()
{
    if (! plan || stage != Stage::Preview) return;
    if (liveSafeRefuses (LiveAction::RevertPlan)) return;
    kept = plan->before;
    plan.reset();
    tuningStrip = -1;
    stage = restingStage();
    compare = Compare::After;
    publish();
    if (onMixChanged) onMixChanged();
}

// ---- Macros ----

void MixController::setMacro (MixMacro m, float value)
{
    macros.set (m, liveSafe::clampMacro (safety, value));
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::resetMacros()
{
    macros = MixMacroValues {};
    publish();
    if (onMixChanged) onMixChanged();
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
    if (! prepared) return;
    history.push_back (snapshotNow (what));
    if (history.size() > kMaxHistory) history.erase (history.begin());
    // A new change ends the redo line: there is no going forward to a future that has been
    // replaced. This is how every undo stack behaves and it is what people expect.
    future.clear();
}

void MixController::applySnapshot (const MixSnapshot& s)
{
    kept = s.mix;
    kept.numStrips = std::min (kept.numStrips, engine.getNumStrips());
    macros = s.macros;
    mixed = s.mixedThen;
    // Undoing while a plan is on BEFORE / AFTER would leave a preview of something that no
    // longer exists. The preview goes; the mix that came back is what is heard.
    plan.reset();
    tuningStrip = -1;
    compare = Compare::After;
    stage = restingStage();
    publish();
    if (onMixChanged) onMixChanged();
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

bool MixController::sendChatRequest (const std::string& text)
{
    if (text.find_first_not_of (" \t\n") == std::string::npos) return false;
    if (! prepared)
    {
        if (onMessage) onMessage ("No mix is running yet.");
        return false;
    }
    // The chat is a change to the mix, so LIVE SAFE decides whether it may happen. It is
    // allowed while locked - each change is asked for by name and shown before it lands -
    // but everything the reasoning layer proposes is still bounded by the policy.
    if (liveRun || stage == Stage::Listening || stage == Stage::Planning)
    {
        if (onMessage) onMessage ("DLIVE is busy. Wait for it to finish, then ask again.");
        return false;
    }
    if (! canChat())
    {
        chat.push_back ({ true, text, {}, false, false });
        chat.push_back ({ false, "DLIVE has not heard the band yet. Run TUNE MIX (or TUNE LIVE MIX) once, "
                                 "then ask for anything you like - the chat works from what it heard.", {}, true, false });
        if (onMessage) onMessage ("Run TUNE MIX first: the chat works from what DLIVE heard.");
        return false;
    }

    chat.push_back ({ true, text, {}, false, false });

    LiveTuneSettings s = liveSettings;
    s.userRequest = text;
    s.reuseListen = true;         // the band does not play again for every sentence
    s.refinementPass = false;     // one request, one change, reviewed by the person who asked
    s.variation = 0;
    chatRun = true;
    markMixChange (text);
    startTuneLiveMix (s);
    if (! liveRun)
    {
        // startTuneLiveMix refused (LIVE SAFE, or nothing heard): take the history entry back
        // so an undo does not point at a change that never happened.
        chatRun = false;
        if (! history.empty()) history.pop_back();
        chat.push_back ({ false, "That could not be done right now.", {}, true, false });
        return false;
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
    if (on)
    {
        // Going safe never changes the sound. It does end anything mid-flight that would
        // have: a listen in progress would land a whole new mix inside the service.
        if (stage == Stage::Listening || liveRun) abortTuneMix();
    }
    if (onMessage)
        onMessage (on ? std::string ("LIVE SAFE on. ") + liveSafe::lockedSummary() + " " + liveSafe::allowedSummary()
                      : std::string ("LIVE SAFE off. Everything is available again."));
}

void MixController::setLiveSafePolicy (const LiveSafePolicy& p) { safety = p; }

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

void MixController::setSoloMode (SoloMode m)
{
    if (kept.monitor.mode == m) return;
    kept.monitor.mode = m;
    if (plan) { plan->proposed.monitor.mode = m; plan->before.monitor.mode = m; }
    publish();
    if (onMessage)
        onMessage (m == SoloMode::InPlace
                       ? std::string ("Careful: solo is now heard by everyone, not just you. That is for mixing a "
                                      "recording, not for a service.")
                       : std::string ("Solo goes to your own device only. The room and the stream never hear it."));
    if (onMixChanged) onMixChanged();
}

void MixController::setSoloPoint (SoloPoint pt)
{
    if (kept.monitor.point == pt) return;
    kept.monitor.point = pt;
    if (plan) { plan->proposed.monitor.point = pt; plan->before.monitor.point = pt; }
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setMonitorGain (float db)
{
    kept.monitor.gainDb = clamp (db, -60.0f, 12.0f);
    if (plan) { plan->proposed.monitor.gainDb = kept.monitor.gainDb; plan->before.monitor.gainDb = kept.monitor.gainDb; }
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setMonitorDim (bool on)
{
    if (kept.monitor.dim == on) return;
    kept.monitor.dim = on;
    if (plan) { plan->proposed.monitor.dim = on; plan->before.monitor.dim = on; }
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setMonitorMute (bool on)
{
    if (kept.monitor.mute == on) return;
    kept.monitor.mute = on;
    if (plan) { plan->proposed.monitor.mute = on; plan->before.monitor.mute = on; }
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setMonitorSource (MixBus b)
{
    if (b == MixBus::Count || kept.monitor.source == b) return;
    kept.monitor.source = b;
    if (plan) { plan->proposed.monitor.source = b; plan->before.monitor.source = b; }
    publish();
    if (onMixChanged) onMixChanged();
}

bool MixController::anyFxSolo() const noexcept
{
    for (int f = 0; f < int (FxSlot::Count); ++f) if (kept.fx[size_t (f)].solo) return true;
    return false;
}

void MixController::setFxSoloAll (bool solo)
{
    const auto& used = prepared ? getGraph().fxUsed : std::array<bool, int (FxSlot::Count)> {};
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        const bool want = solo && used[size_t (f)];
        kept.fx[size_t (f)].solo = want;
        if (plan && stage == Stage::Preview) plan->proposed.fx[size_t (f)].solo = want;
    }
    publish();
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    if (onMixChanged) onMixChanged();
}

void MixController::setFxSolo (FxSlot slot, bool solo)
{
    if (int (slot) < 0 || int (slot) >= int (FxSlot::Count)) return;
    kept.fx[size_t (slot)].solo = solo;
    if (plan && stage == Stage::Preview) plan->proposed.fx[size_t (slot)].solo = solo;
    publish();
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    if (onMixChanged) onMixChanged();
}

// ---- Advanced edits ----

namespace
{
    bool validStrip (const MixParameters& p, int strip) { return strip >= 0 && strip < p.numStrips; }
}

void MixController::setStripFader (int strip, float db, bool withLink)
{
    if (! validStrip (kept, strip)) return;
    float want = clamp (db, -60.0f, 12.0f);
    liveSafe::Verdict v;
    const float from = kept.strips[size_t (strip)].faderDb;
    want = liveSafe::limitStepDb (safety, LiveAction::Fader, from, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.strips[size_t (strip)].faderDb = want;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].faderDb = kept.strips[size_t (strip)].faderDb;
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
            if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (other)].faderDb = s.faderDb;
        }
    publish();
    if (onMixChanged) onMixChanged();
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
    const auto& inputs = prepared ? preparedSession.inputs : session.inputs;
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
        if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (i)].faderDb = s.faderDb;
    }
    if (limited && onMessage) onMessage (verdict.reason);
    if (plan) for (int i = 0; i < kept.numStrips; ++i)
    {
        plan->proposed.strips[size_t (i)].linkGroup = kept.strips[size_t (i)].linkGroup;
        plan->before.strips[size_t (i)].linkGroup = kept.strips[size_t (i)].linkGroup;
    }
    publish();
    if (onMixChanged) onMixChanged();
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
    if (onMixChanged) onMixChanged();
}

void MixController::setStripPan (int strip, float pan)
{
    if (! validStrip (kept, strip)) return;
    float want = clamp (pan, -1.0f, 1.0f);
    liveSafe::Verdict v;
    want = liveSafe::limitStep (safety, LiveAction::Pan, kept.strips[size_t (strip)].pan, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.strips[size_t (strip)].pan = want;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].pan = kept.strips[size_t (strip)].pan;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripInputGain (int strip, float db)
{
    if (! validStrip (kept, strip)) return;
    float want = clamp (db, -24.0f, 24.0f);
    liveSafe::Verdict v;
    want = liveSafe::limitStepDb (safety, LiveAction::InputGain, kept.strips[size_t (strip)].inputGainDb, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.strips[size_t (strip)].inputGainDb = want;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].inputGainDb = kept.strips[size_t (strip)].inputGainDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripMute (int strip, bool mute)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].mute = mute;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].mute = mute;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripSolo (int strip, bool solo)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].solo = solo;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].solo = solo;
    // Solo follows the link: S on one overhead means "the overheads", from either member.
    for (int other : linkedWith (strip))
    {
        kept.strips[size_t (other)].solo = solo;
        if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (other)].solo = solo;
    }
    publish();
    // Solo is safe (it never reaches the master) but it is only *useful* when a monitor
    // output exists. Saying so once beats an S key that appears to do nothing.
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    if (onMixChanged) onMixChanged();
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
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].sendDb[size_t (slot)] = kept.strips[size_t (strip)].sendDb[size_t (slot)];
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusFader (MixBus bus, float db)
{
    if (bus == MixBus::Count) return;
    float want = clamp (db, -60.0f, 12.0f);
    liveSafe::Verdict v;
    want = liveSafe::limitStepDb (safety, bus == MixBus::Master ? LiveAction::MasterFader : LiveAction::Fader,
                                  kept.buses[size_t (bus)].faderDb, want, v);
    if (v.limited && onMessage) onMessage (v.reason);
    kept.buses[size_t (bus)].faderDb = want;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].faderDb = kept.buses[size_t (bus)].faderDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusMute (MixBus bus, bool mute)
{
    if (bus == MixBus::Count) return;
    kept.buses[size_t (bus)].mute = mute;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].mute = mute;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setFxReturn (float db)
{
    kept.fxReturnDb = clamp (db, -60.0f, 12.0f);
    if (plan && stage == Stage::Preview) plan->proposed.fxReturnDb = kept.fxReturnDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setFxMute (bool mute)
{
    kept.fxMute = mute;
    if (plan && stage == Stage::Preview) plan->proposed.fxMute = mute;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusSolo (MixBus bus, bool solo)
{
    if (bus == MixBus::Master || bus == MixBus::Count) return;
    kept.buses[size_t (bus)].solo = solo;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].solo = solo;
    publish();
    if (solo && ! hasMonitorOutput() && kept.monitor.mode == SoloMode::Monitor && onMessage)
        onMessage ("Solo has nowhere to go yet. Pick the device you listen on: the Solo picker on LIVE, or Outputs > Solo.");
    if (onMixChanged) onMixChanged();
}

void MixController::setStripChannel (int strip, const ChannelParameters& c)
{
    if (! validStrip (kept, strip)) return;
    markMixChange ("a processing change");
    kept.strips[size_t (strip)].channel = c;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].channel = c;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusChannel (MixBus bus, const ChannelParameters& c)
{
    if (bus == MixBus::Count) return;
    markMixChange (std::string (mixBusName (bus)) + " processing");
    kept.buses[size_t (bus)].channel = c;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].channel = c;
    publish();
    if (onMixChanged) onMixChanged();
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
    if (onMixChanged) onMixChanged();
}

void MixController::setKept (const MixParameters& p)
{
    kept = p;
    kept.numStrips = std::min (kept.numStrips, engine.getNumStrips());
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
    if (! prepared || engine.getNumStrips() == 0 || ! plan) return 0;
    const HealthCount c = countHealth (*plan, MixProfile::relationships (session.profile).digitalGainAdviceDb);
    if (c.assigned == 0) return 0;
    return int (std::round (100.0f * float (c.good) / float (c.assigned)));
}

std::vector<std::string> MixController::getMixHealthNotes() const
{
    std::vector<std::string> notes;
    if (! prepared || engine.getNumStrips() == 0) return notes;
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
