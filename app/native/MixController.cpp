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
    kept = plan->before;
    plan.reset();
    stage = restingStage();
    compare = Compare::After;
    publish();
    if (onMixChanged) onMixChanged();
}

// ---- Macros ----

void MixController::setMacro (MixMacro m, float value)
{
    macros.set (m, value);
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::resetMacros()
{
    macros = MixMacroValues {};
    publish();
    if (onMixChanged) onMixChanged();
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
    if (onMixChanged) onMixChanged();
}

void MixController::setStripInputGain (int strip, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].inputGainDb = clamp (db, -24.0f, 24.0f);
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
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setStripSend (int strip, FxSlot slot, float db)
{
    if (! validStrip (kept, strip)) return;
    kept.strips[size_t (strip)].sendDb[size_t (slot)] = db <= -60.0f ? kSilenceDb : clamp (db, -60.0f, 6.0f);
    if (plan && stage == Stage::Preview) plan->proposed.strips[size_t (strip)].sendDb[size_t (slot)] = kept.strips[size_t (strip)].sendDb[size_t (slot)];
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusFader (MixBus bus, float db)
{
    kept.buses[size_t (bus)].faderDb = clamp (db, -60.0f, 12.0f);
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].faderDb = kept.buses[size_t (bus)].faderDb;
    publish();
    if (onMixChanged) onMixChanged();
}

void MixController::setBusSolo (MixBus bus, bool solo)
{
    if (bus == MixBus::Master || bus == MixBus::Count) return;
    kept.buses[size_t (bus)].solo = solo;
    if (plan && stage == Stage::Preview) plan->proposed.buses[size_t (bus)].solo = solo;
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
    if (plan && stage == Stage::Preview)
    {
        for (int i = 0; i < plan->proposed.numStrips; ++i) plan->proposed.strips[size_t (i)].solo = false;
        for (int b = 0; b < int (MixBus::Count); ++b) plan->proposed.buses[size_t (b)].solo = false;
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

// ---- Health ----

namespace
{
    // The three things the last listen can say about an input.
    struct HealthCount { int assigned = 0, good = 0, faint = 0, silent = 0, preamp = 0; };

    HealthCount countHealth (const MixPlan& plan)
    {
        HealthCount c;
        for (const auto& sp : plan.strips)
        {
            ++c.assigned;
            if (sp.faint) { ++c.faint; continue; }
            if (! sp.heard) { ++c.silent; continue; }
            // Heard. Healthy when the level reaching the chain is inside the profile's range (after the digital gain
            // the plan chose); otherwise the console preamp still has to move.
            const bool healthy = ! sp.tune.valid || sp.tune.report.inputHealth == "Healthy" || sp.bleedOnly;
            if (healthy) ++c.good; else ++c.preamp;
        }
        return c;
    }
}

int MixController::getMixHealthPercent() const
{
    if (! prepared || engine.getNumStrips() == 0 || ! plan) return 0;
    const HealthCount c = countHealth (*plan);
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
    const HealthCount c = countHealth (*plan);
    auto plural = [] (int n, const char* one, const char* many) { return std::to_string (n) + " " + (n == 1 ? one : many); };
    if (c.faint > 0)  notes.push_back (plural (c.faint, "input barely reached DINELIVE: check its mic and cable.", "inputs barely reached DINELIVE: check their mics and cables."));
    if (c.silent > 0) notes.push_back (plural (c.silent, "input was not heard: RE-TUNE while it plays.", "inputs were not heard: RE-TUNE while they play."));
    if (c.preamp > 0) notes.push_back (plural (c.preamp, "input still wants a preamp change at the console (see Advanced).", "inputs still want a preamp change at the console (see Advanced)."));
    if (notes.empty() && c.good == c.assigned) notes.push_back ("Every input was heard at a healthy level.");
    return notes;
}

} // namespace livemix
