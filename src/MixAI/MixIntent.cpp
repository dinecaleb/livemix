#include "MixIntent.h"
#include <algorithm>
#include <cmath>

namespace livemix
{

namespace
{
    struct ObjectiveNames { const char* id; const char* label; };

    constexpr ObjectiveNames kObjectiveNames[int (MixObjectiveType::Count)] = {
        { "presence",          "Presence" },
        { "warmth",            "Warmth" },
        { "body",              "Body" },
        { "brightness",        "Brightness" },
        { "clarity",           "Clarity" },
        { "dynamic_stability", "Dynamic stability" },
        { "punch",             "Punch" },
        { "cleanup",           "Clean-up" },
        { "sibilance",         "Sibilance" },
        { "spatial_depth",     "Depth" },
        { "width",             "Width" },
        { "level",             "Level" },
        { "separation",        "Separation" },
        { "character",         "Character" },
    };

    float clampStrength (float v)
    {
        if (! std::isfinite (v)) return 0.0f;
        return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }

    Confidence confidenceFromText (const std::string& s, Confidence fallback)
    {
        if (s == "HIGH" || s == "high") return Confidence::High;
        if (s == "MEDIUM" || s == "medium") return Confidence::Medium;
        if (s == "LOW" || s == "low") return Confidence::Low;
        return fallback;
    }
}

const char* mixObjectiveId (MixObjectiveType t) noexcept
{
    const int i = int (t);
    return (i >= 0 && i < int (MixObjectiveType::Count)) ? kObjectiveNames[i].id : "unknown";
}

const char* mixObjectiveLabel (MixObjectiveType t) noexcept
{
    const int i = int (t);
    return (i >= 0 && i < int (MixObjectiveType::Count)) ? kObjectiveNames[i].label : "Unknown";
}

MixObjectiveType mixObjectiveFromId (const std::string& id) noexcept
{
    for (int i = 0; i < int (MixObjectiveType::Count); ++i)
        if (id == kObjectiveNames[i].id) return MixObjectiveType (i);
    return MixObjectiveType::Count;
}

json::Value MixObjective::toJson() const
{
    auto v = json::Value::object();
    v.set ("type", mixObjectiveId (type));
    v.set ("strength", strength);
    if (hasAgainst()) v.set ("against", against.id());
    if (! character.empty()) v.set ("character", character);
    if (preserveArticulation) v.set ("preserveArticulation", true);
    if (preserveTransients) v.set ("preserveTransients", true);
    return v;
}

json::Value MixTargetIntent::toJson() const
{
    auto v = json::Value::object();
    v.set ("target", target.id());
    if (! targetName.empty()) v.set ("name", targetName);
    auto os = json::Value::array();
    for (const auto& o : objectives) os.add (o.toJson());
    v.set ("objectives", std::move (os));
    if (! reason.empty()) v.set ("reason", reason);
    v.set ("confidence", confidenceName (confidence));
    return v;
}

int MixIntent::objectiveCount() const noexcept
{
    int n = 0;
    for (const auto& t : targets) n += int (t.objectives.size());
    return n;
}

json::Value MixIntent::toJson() const
{
    auto v = json::Value::object();
    v.set ("schema", "dlive.mixIntent");
    v.set ("schemaVersion", schemaVersion);
    v.set ("noChangeRequired", noChangeRequired);
    v.set ("summary", summary);
    auto ts = json::Value::array();
    for (const auto& t : targets) ts.add (t.toJson());
    v.set ("targets", std::move (ts));
    if (! unsupportedRequests.empty())
    {
        auto us = json::Value::array();
        for (const auto& u : unsupportedRequests) us.add (u);
        v.set ("unsupportedRequests", std::move (us));
    }
    return v;
}

MixIntent parseMixIntent (const json::Value& root, std::vector<std::string>* problems)
{
    MixIntent out;
    auto problem = [&] (std::string s) { if (problems != nullptr) problems->push_back (std::move (s)); };

    if (! root.isObject()) { problem ("The reply was not an object."); return out; }

    const int version = root["schemaVersion"].asInt (kMixIntentSchemaVersion);
    if (version > kMixIntentSchemaVersion)
    {
        problem ("The reply claims intent schema " + std::to_string (version) + "; this build reads "
                 + std::to_string (kMixIntentSchemaVersion) + ".");
        return out;
    }
    out.schemaVersion = version;
    out.summary = root["summary"].asString();
    out.noChangeRequired = root["noChangeRequired"].asBool (false);

    const auto& unsupported = root["unsupportedRequests"];
    for (int i = 0; i < unsupported.size(); ++i)
        if (! unsupported[i].asString().empty()) out.unsupportedRequests.push_back (unsupported[i].asString());

    const auto& targets = root["targets"];
    if (! targets.isArray() && ! out.noChangeRequired)
    {
        problem ("The reply carried no targets.");
        return out;
    }

    for (int i = 0; i < targets.size(); ++i)
    {
        const auto& t = targets[i];
        MixTargetIntent ti;
        ti.target = MixTargetRef::parse (t["target"].asString());
        ti.targetName = t["name"].asString();
        ti.reason = t["reason"].asString();
        ti.confidence = confidenceFromText (t["confidence"].asString(), Confidence::Medium);
        if (ti.target.index < 0)
        {
            problem ("A target named \"" + t["target"].asString() + "\" is not part of this mix.");
            continue;
        }

        const auto& objectives = t["objectives"];
        for (int j = 0; j < objectives.size(); ++j)
        {
            const auto& o = objectives[j];
            MixObjective mo;
            mo.type = mixObjectiveFromId (o["type"].asString());
            if (mo.type == MixObjectiveType::Count)
            {
                problem ("Unknown objective \"" + o["type"].asString() + "\" on " + ti.target.id() + ".");
                continue;
            }
            mo.strength = clampStrength (o["strength"].asFloat (0.0f));
            if (o.has ("against")) mo.against = MixTargetRef::parse (o["against"].asString());
            mo.character = o["character"].asString();
            mo.preserveArticulation = o["preserveArticulation"].asBool (false);
            mo.preserveTransients = o["preserveTransients"].asBool (false);
            ti.objectives.push_back (std::move (mo));
        }

        if (ti.objectives.empty()) continue;
        out.targets.push_back (std::move (ti));
    }

    out.valid = out.noChangeRequired || ! out.targets.empty();
    if (! out.valid) problem ("Nothing in the reply could be turned into an objective.");
    return out;
}

MixIntent parseMixIntentText (const std::string& jsonText, std::vector<std::string>* problems)
{
    std::string object = jsonText;
    // A reply wrapped in prose or a fence is still a usable reply; a reply whose numbers
    // only exist in the prose is not, and that is what the structured object is for.
    if (! jsonText.empty() && jsonText.find ('{') != 0) json::extractObject (jsonText, object);
    std::string error;
    const auto root = json::parse (object, &error);
    if (root.isNull())
    {
        if (problems != nullptr) problems->push_back (error.empty() ? "The reply was not valid JSON." : error);
        return {};
    }
    return parseMixIntent (root, problems);
}

MixIntent validateMixIntent (const MixIntent& in, const DspCapabilityRegistry& registry, std::vector<std::string>* problems)
{
    MixIntent out = in;
    out.targets.clear();
    auto problem = [&] (std::string s) { if (problems != nullptr) problems->push_back (std::move (s)); };

    for (const auto& t : in.targets)
    {
        const auto* caps = registry.find (t.target);
        if (caps == nullptr)
        {
            problem (t.target.id() + " is not part of this mix.");
            continue;
        }

        MixTargetIntent ti = t;
        ti.targetName = caps->name;
        ti.objectives.clear();
        for (auto o : t.objectives)
        {
            o.strength = clampStrength (o.strength);
            if (std::fabs (o.strength) < 0.02f && o.type != MixObjectiveType::Character) continue;   // nothing asked for
            if (o.type == MixObjectiveType::Separation && ! o.hasAgainst())
            {
                problem ("Separation on " + caps->name + " does not say what it is making room for.");
                continue;
            }
            if (o.hasAgainst() && registry.find (o.against) == nullptr)
            {
                problem ("Separation on " + caps->name + " names " + o.against.id() + ", which is not in this mix.");
                continue;
            }
            if (o.type == MixObjectiveType::Character && o.character.empty())
            {
                problem ("A character objective on " + caps->name + " does not say what character.");
                continue;
            }
            ti.objectives.push_back (std::move (o));
        }
        if (ti.objectives.empty()) continue;
        out.targets.push_back (std::move (ti));
    }

    out.valid = out.noChangeRequired || ! out.targets.empty();
    return out;
}

} // namespace livemix
