#include "MixController.h"
#include "Core/DbUtils.h"
#include <cmath>
#include <cstdio>

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
    prepared = false;
    stage = Stage::Setup;
    plan.reset();
}

void MixController::setPurpose (MixPurpose p) { session.purpose = p; }
void MixController::setProfile (StyleProfileId p) { session.profile = p; }

void MixController::prepare (double sr, int maxBlockSize)
{
    capture.abort();
    engine.setTap (nullptr);
    sampleRate = sr;
    blockSize = maxBlockSize;
    engine.prepare (sr, maxBlockSize, session);
    capture.prepare (sr, engine.getGraph());
    engine.setTap (&capture);
    kept = startingPoint (session, engine.getGraph());
    atCapture = kept;
    plan.reset();
    compare = Compare::After;
    macros = MixMacroValues {};
    tuneCount = 0;
    mixed = false;
    stage = engine.getNumStrips() > 0 ? Stage::Ready : Stage::Setup;
    prepared = true;
    publish();
}

MixParameters MixController::compose() const
{
    const MixParameters& base = (plan && stage == Stage::Preview) ? (compare == Compare::Before ? plan->before : plan->proposed) : kept;
    return MixMacros::apply (base, macros, engine.getGraph(), session.profile);
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
    if (! prepared || stage == Stage::Listening || stage == Stage::Planning) return;
    if (stage == Stage::Preview) keepPlan();   // a new listen starts from what is audible now
    listen = s;
    atCapture = running;                        // the faders and gains the listen will run with
    MixCapture::Settings cs;
    cs.seconds = s.seconds;
    cs.triggerDb = s.triggerDb;
    cs.maxWaitSeconds = s.maxWaitSeconds;
    capture.start (cs);
    stage = Stage::Listening;
}

void MixController::abortTuneMix()
{
    if (stage != Stage::Listening && stage != Stage::Planning) return;
    capture.abort();
    stage = restingStage();
}

void MixController::poll()
{
    if (stage != Stage::Listening) return;
    const auto s = capture.getState();
    if (s == MixCapture::State::Complete)
    {
        stage = Stage::Planning;
        MixPlanContext ctx;
        ctx.session = session;
        ctx.graph = engine.getGraph();
        ctx.current = kept;
        ctx.atCapture = atCapture;
        ctx.capture = capture.getResult();
        // The listen ran with the macros applied; the plan is built on the macro-free mix, and the macros
        // stay where the user left them (50 = the plan).
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
    else if (s == MixCapture::State::Failed)
    {
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
    switch (stage)
    {
        case Stage::Setup:     return "Assign your inputs to begin.";
        case Stage::Ready:     return "Press TUNE MIX and have the band play normally.";
        case Stage::Listening:
            if (isWaitingForBand()) return "Waiting for the band...";
            return "LISTENING... " + std::to_string (int (std::round (getListenProgress() * listen.seconds))) + " of " + std::to_string (int (listen.seconds)) + " s";
        case Stage::Planning:  return "Building the mix...";
        case Stage::Preview:   return plan ? plan->headline + (compare == Compare::Before ? "  (hearing BEFORE)" : "  (hearing AFTER)") : "";
        case Stage::Mixed:
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
    kept = plan->proposed;
    mixed = true;
    stage = Stage::Mixed;
    compare = Compare::After;
    publish();
}

void MixController::revertPlan()
{
    if (! plan || stage != Stage::Preview) return;
    kept = plan->before;
    plan.reset();
    stage = restingStage();
    compare = Compare::After;
    publish();
}

// ---- Macros ----

void MixController::setMacro (MixMacro m, float value)
{
    macros.set (m, value);
    publish();
}

void MixController::resetMacros()
{
    macros = MixMacroValues {};
    publish();
}

// ---- Advanced edits ----

namespace
{
    bool validStrip (const MixParameters& p, int strip) { return strip >= 0 && strip < p.numStrips; }
}

void MixController::setStripFader (int strip, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].faderDb = clamp (db, -60.0f, 12.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].faderDb = kept.strips[size_t (strip)].faderDb;
    publish();
}

void MixController::setStripInputGain (int strip, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].inputGainDb = clamp (db, -24.0f, 24.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].inputGainDb = kept.strips[size_t (strip)].inputGainDb;
    publish();
}

void MixController::setStripMute (int strip, bool mute)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].mute = mute;
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].mute = mute;
    publish();
}

void MixController::setStripSend (int strip, FxSlot slot, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].sendDb[size_t (slot)] = db <= -60.0f ? kSilenceDb : clamp (db, -60.0f, 6.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].sendDb[size_t (slot)] = kept.strips[size_t (strip)].sendDb[size_t (slot)];
    publish();
}

void MixController::setBusFader (MixBus bus, float db)
{
    kept.buses[size_t (bus)].faderDb = clamp (db, -60.0f, 12.0f);
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].faderDb = kept.buses[size_t (bus)].faderDb;
    publish();
}

void MixController::setKept (const MixParameters& p)
{
    kept = p;
    kept.numStrips = std::min (kept.numStrips, engine.getNumStrips());
    mixed = true;
    if (stage == Stage::Ready) stage = Stage::Mixed;
    publish();
}

// ---- Health ----

int MixController::getMixHealthPercent() const
{
    if (! prepared || engine.getNumStrips() == 0) return 0;
    if (! plan && ! mixed) return 0;
    // Heard sources tuned: the base. Inputs that need a preamp move or stayed silent cost points.
    float score = 55.0f;
    if (plan)
    {
        const int n = int (plan->strips.size());
        int heard = 0, healthy = 0;
        for (const auto& sp : plan->strips)
        {
            if (! sp.heard) continue;
            ++heard;
            if (sp.tune.valid && (sp.tune.report.inputHealth == "Healthy" || std::fabs (sp.tune.report.suggestedCaptureGainDb) < 3.0f)) ++healthy;
        }
        if (n > 0) score += 25.0f * float (heard) / float (n);
        if (heard > 0) score += 15.0f * float (healthy) / float (heard);
        const auto& master = plan->buses[size_t (MixBus::Master)];
        bool loudnessOk = false;
        if (master.tune.valid)
            for (const auto& item : master.tune.report.items)
                if (item.what.find ("on target") != std::string::npos) loudnessOk = true;
        if (loudnessOk) score += 5.0f;
    }
    else score = 70.0f;
    return int (clamp (score, 0.0f, 100.0f));
}

} // namespace livemix
