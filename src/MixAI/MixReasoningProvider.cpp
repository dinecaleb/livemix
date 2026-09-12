#include "MixReasoningProvider.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace livemix
{

namespace
{
    std::string num (const char* fmt, double v) { char b[96]; std::snprintf (b, sizeof (b), fmt, v); return b; }

    const MixRelationship* findRelation (const MixContext& c, const std::string& metric, int a = -1)
    {
        for (const auto& r : c.relationships)
            if (r.metric == metric && (a < 0 || r.stripA == a)) return &r;
        return nullptr;
    }

    const MixContextTrack* trackById (const MixContext& c, int id)
    {
        for (const auto& t : c.tracks) if (t.id == id) return &t;
        return nullptr;
    }

    MixTargetIntent* targetFor (MixIntent& intent, const MixContext& c, int strip)
    {
        const MixTargetRef ref { MixTargetKind::Strip, strip };
        for (auto& t : intent.targets) if (t.target == ref) return &t;
        MixTargetIntent t;
        t.target = ref;
        const auto* track = trackById (c, strip);
        if (track != nullptr) t.targetName = track->name;
        intent.targets.push_back (std::move (t));
        return &intent.targets.back();
    }

    MixTargetIntent* busTarget (MixIntent& intent, MixBus bus)
    {
        const MixTargetRef ref { MixTargetKind::Bus, int (bus) };
        for (auto& t : intent.targets) if (t.target == ref) return &t;
        MixTargetIntent t;
        t.target = ref;
        t.targetName = mixBusName (bus);
        intent.targets.push_back (std::move (t));
        return &intent.targets.back();
    }

    float strengthFrom (float excess, float full)
    {
        if (full <= 0.0f) return 0.0f;
        return std::clamp (excess / full, 0.0f, 1.0f);
    }
}

std::string mixEngineerInstructions (StyleProfileId profile, MixPurpose purpose)
{
    std::string s;
    s += "You are a conservative, highly experienced live and broadcast mix engineer working inside DLIVE.\n"
         "You are given measurements of a short soundcheck and the list of processors DLIVE actually has.\n"
         "You answer with sonic intent - what each source should sound like, and why - never with parameter values.\n"
         "DLIVE works out how to achieve the intent with the tools it has, and refuses anything unsafe.\n\n"
         "How you work:\n"
         "- A professional mix has already been built from these measurements. You are refining it, not replacing it.\n"
         "- Solve a conflict where the conflict is. If the lead is buried and the lead itself is fine, make room in\n"
         "  whatever is covering it rather than pushing the lead.\n"
         "- Most channels should be left alone. NO CHANGE REQUIRED is a professional answer and often the right one.\n"
         "- More processing is not better. Every objective you give has to earn its place.\n"
         "- Keep the lead vocal in front of the backing vocals. Keep the spoken word intelligible.\n"
         "- Preserve headroom. Do not chase loudness; the master's delivery target is already set.\n"
         "- A quiet or distorted input is a capture problem at the console preamp, not something a fader fixes.\n"
         "  Say so instead of asking for level.\n"
         "- Do not ask for processing on a source that was not playing during the listen.\n"
         "- Ask for the sound you want even when you are not sure DLIVE has the exact effect. DLIVE will build the\n"
         "  closest honest thing it can, or tell the user it cannot.\n\n";
    s += "The mix is for: " + std::string (mixPurposeName (purpose)) + ".\n";
    s += "The sonic profile is " + std::string (styleProfileName (profile)) + ".\n";
    if (profile == StyleProfileId::ModernGospel)
        s += "Modern Gospel: big, clean, punchy drums that stay controlled; a deep, tight kick that shares the low end\n"
             "with a large, warm, defined bass; a full snare with body and crack that never turns harsh; musical keys and\n"
             "organ with their low mids in check; a forward, polished, powerful lead vocal that is still warm; backing\n"
             "vocals that are smooth, cohesive, a little wider and clearly behind the lead; spacious, musical effects; and a\n"
             "cohesive, energetic, broadcast-ready master with real headroom left. These are intentions to listen for, not\n"
             "EQ values - the measurements in front of you decide what this band actually needs.\n";
    else
        s += "Modern Worship: the same craft with the band sitting closer together - less separation between the voices\n"
             "and the instruments, a slightly softer top end, and drums that stay tighter than gospel's.\n";
    return s;
}

// ---------------------------------------------------------------------------
// The offline engineer.
//
// It reasons from the relationships the analysis measured, in the order an engineer would:
// gain staging first (which it never fixes, only reports), then what is covering the voice,
// then the hierarchy of the voices, then the kit, then the master. Deterministic by
// construction - it reads measurements and thresholds, never a random number and never the
// current parameter values - so a second pass over the same listen asks for the same thing.
// ---------------------------------------------------------------------------
MixReasoningResponse LocalMixReasoningProvider::reason (const MixReasoningRequest& request, const std::atomic<bool>& shouldCancel)
{
    MixReasoningResponse out;
    out.providerName = getName();
    out.model = "relationships-v1";

    const MixContext& c = request.hasVerification ? request.verification : request.context;
    if (shouldCancel.load (std::memory_order_relaxed)) { out.error = "Cancelled."; return out; }

    if (! c.adequacy.sufficient)
    {
        out.error = c.adequacy.reason.empty() ? "The listen was not enough to mix from." : c.adequacy.reason;
        return out;
    }

    MixIntent intent;
    std::vector<std::string> said;

    // ---- The voice against everything covering it ----
    int lead = -1;
    for (const auto& t : c.tracks) if (t.family == "LeadVocal" && t.heard) { lead = t.id; break; }

    int maskingCount = 0;
    for (const auto& r : c.relationships)
    {
        if (r.kind != MixRelationKind::LeadAndMusic || ! r.concern || r.stripA < 0) continue;
        // The lead has enough presence of its own: the answer is in what is covering it.
        auto* t = targetFor (intent, c, r.stripA);
        MixObjective o;
        o.type = MixObjectiveType::Separation;
        o.against = { MixTargetKind::Strip, r.stripB >= 0 ? r.stripB : lead };
        o.strength = strengthFrom (r.value - r.tolerance, 4.0f);
        o.preserveArticulation = true;
        t->objectives.push_back (o);
        t->reason = r.headline + " Room was made there rather than pushing the voice.";
        t->confidence = Confidence::High;
        ++maskingCount;
    }
    if (maskingCount > 0)
        said.push_back (maskingCount == 1 ? "Made room for the lead vocal in the one source covering it."
                                          : "Made room for the lead vocal in " + std::to_string (maskingCount) + " sources covering it.");

    // ---- The hierarchy of the voices ----
    if (const auto* r = findRelation (c, "lead_above_backing_group_db"); r != nullptr && r->concern && r->stripB >= 0)
    {
        auto* t = targetFor (intent, c, r->stripB);
        MixObjective level;
        level.type = MixObjectiveType::Level;
        level.strength = -strengthFrom (r->tolerance - r->value, 3.0f);
        t->objectives.push_back (level);
        // Further back, not just quieter - where there is a stereo image to open out. Asking a
        // mono microphone to be wide is a request DLIVE would have to refuse, and an engineer
        // who can see the source is mono does not make it.
        const auto* backing = trackById (c, r->stripB);
        const bool stereo = backing != nullptr && backing->measurements.numChannels > 1;
        if (stereo)
        {
            MixObjective width;
            width.type = MixObjectiveType::Width;
            width.strength = 0.4f;
            t->objectives.push_back (width);
        }
        t->reason = r->headline + (stereo ? " The group was stepped back and opened out instead of simply turned down."
                                          : " The group was stepped back behind the lead.");
        t->confidence = Confidence::High;
        said.push_back ("Set the backing vocals behind the lead.");
    }

    // ---- The voice's own depth against its words ----
    if (const auto* r = findRelation (c, "lead_send_db"); r != nullptr && r->concern && r->stripA >= 0)
    {
        auto* t = targetFor (intent, c, r->stripA);
        MixObjective presence;
        presence.type = MixObjectiveType::Presence;
        presence.strength = 0.45f;
        t->objectives.push_back (presence);
        MixObjective depth;
        depth.type = MixObjectiveType::SpatialDepth;
        depth.strength = -0.3f;
        depth.preserveArticulation = true;
        t->objectives.push_back (depth);
        t->reason = r->headline + " The voice was given its own articulation before it was given more space.";
        said.push_back ("Traded a little of the lead's reverb for its own clarity.");
    }

    // ---- The kit ----
    for (const auto& r : c.relationships)
    {
        if (r.kind != MixRelationKind::CloseAndOverheads || r.metric != "close_above_overhead_db" || ! r.concern || r.stripA < 0) continue;
        auto* t = targetFor (intent, c, r.stripA);
        MixObjective level;
        level.type = MixObjectiveType::Level;
        level.strength = strengthFrom (-r.value - r.tolerance, 4.0f);
        t->objectives.push_back (level);
        t->reason = r.headline;
        said.push_back ("Brought the close drum microphones back up against the overheads.");
        break;      // one at a time: the kit is balanced by the loudest disagreement, not all at once
    }

    // ---- What arrives at the master ----
    if (const auto* r = findRelation (c, "master_crest_db"); r != nullptr && r->concern)
    {
        auto* t = busTarget (intent, MixBus::Master);
        MixObjective o;
        o.type = MixObjectiveType::DynamicStability;
        o.strength = -0.4f;                 // let it breathe: the mix arrived flattened already
        o.preserveTransients = true;
        t->objectives.push_back (o);
        t->reason = r->headline + " The master was eased off rather than asked to hold it down further.";
        said.push_back ("Eased the master off: the mix was arriving already flattened.");
    }

    // ---- Things measurement can see and a mix cannot fix ----
    if (const auto* r = findRelation (c, "overhead_correlation"); r != nullptr && r->concern)
        intent.unsupportedRequests.push_back (r->headline + " Phase between a microphone pair is fixed by moving the "
                                                            "microphones or flipping polarity at the console, not by mixing.");
    for (const auto& t : c.tracks)
        if (t.heard && std::fabs (t.consoleMoveDb) >= 3.0f)
            intent.unsupportedRequests.push_back (t.name + " needs about " + num ("%.0f dB", double (t.consoleMoveDb))
                                                  + " at the console preamp. No fader inside DLIVE can do that without "
                                                    "raising the preamp's noise with it.");

    intent.noChangeRequired = intent.targets.empty();
    if (intent.noChangeRequired)
        intent.summary = request.refinement
            ? "The mix held together on the second listen. No further changes."
            : "The deterministic mix already balances this band. Nothing more is worth changing.";
    else
    {
        for (size_t i = 0; i < said.size(); ++i) intent.summary += (i > 0 ? " " : "") + said[i];
    }
    intent.valid = true;

    // A refinement pass is a correction, not a second mix: everything asked for again is
    // asked for more gently, and anything the first pass fixed simply does not come back.
    if (request.refinement)
        for (auto& t : intent.targets)
            for (auto& o : t.objectives) o.strength *= 0.5f;

    out.intent = std::move (intent);
    out.valid = true;
    return out;
}

} // namespace livemix
