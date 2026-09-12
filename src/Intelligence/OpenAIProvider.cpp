#include "OpenAIProvider.h"
#include "Profiles/StyleProfile.h"
#include "State/ParameterSpecs.h"
#include "State/ParameterIDs.h"
#include "Core/ProductDefinition.h"
#include "HttpJson.h"

namespace livemix
{

namespace
{
    constexpr const char* kEndpoint = "https://api.openai.com/v1/chat/completions";

    juce::var num (float v) { return juce::var (double (v)); }

    bool isReasoningModel (const juce::String& model)
    {
        const auto m = model.trim().toLowerCase();
        return m.startsWith ("gpt-5") || m.startsWith ("o1") || m.startsWith ("o3") || m.startsWith ("o4");
    }

    juce::var analysisToVar (const AnalysisResult& a)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("durationSeconds", num (a.durationSeconds));
        o->setProperty ("numChannels", a.numChannels);
        o->setProperty ("peakDbfs", num (a.peakDb));
        o->setProperty ("rmsDbfs", num (a.rmsDb));
        o->setProperty ("crestFactorDb", num (a.crestFactorDb));
        o->setProperty ("hitLevelDbfs_p95", num (a.hitLevelDb));
        o->setProperty ("noiseFloorDbfs_p10", num (a.noiseFloorDb));
        o->setProperty ("dynamicRangeDb", num (a.dynamicRangeDb));
        o->setProperty ("silencePercent", num (a.silencePercent));
        o->setProperty ("dcOffset", num (a.dcOffset));
        o->setProperty ("clippedSamples", a.clipCount);
        o->setProperty ("transientCount", a.transientCount);
        o->setProperty ("transientsPerSecond", num (a.transientsPerSecond));
        o->setProperty ("meanTransientRiseDb", num (a.meanTransientRiseDb));
        o->setProperty ("spectralCentroidHz", num (a.spectralCentroidHz));
        o->setProperty ("highFrequencyRatioDb_above5k", num (a.highFrequencyRatioDb));
        o->setProperty ("bleedEstimate_0to1", num (a.bleedEstimate));
        o->setProperty ("stereoBalanceDb_LminusR", num (a.stereoBalanceDb));
        auto* bands = new juce::DynamicObject();
        for (int b = 0; b < int (Band::Count); ++b)
            bands->setProperty (juce::String (kBandNames[size_t (b)]) + " (" + juce::String (int (kBandEdgesHz[size_t (b)])) + "-" + juce::String (int (kBandEdgesHz[size_t (b + 1)])) + " Hz) dB re total",
                                num (a.bandEnergyDb[size_t (b)]));
        o->setProperty ("bandEnergy", juce::var (bands));
        juce::Array<juce::var> third;
        for (int i = 0; i < kNumThirdOctaveBands; ++i)
        {
            auto* t = new juce::DynamicObject();
            t->setProperty ("hz", num (thirdOctaveCentreHz (i)));
            t->setProperty ("db", num (a.thirdOctaveDb[size_t (i)]));
            third.add (juce::var (t));
        }
        o->setProperty ("thirdOctaveSpectrumDbReTotal", third);
        juce::Array<juce::var> res;
        for (const auto& r : a.resonances)
        {
            auto* t = new juce::DynamicObject();
            t->setProperty ("hz", num (r.frequencyHz));
            t->setProperty ("prominenceDb", num (r.prominenceDb));
            res.add (juce::var (t));
        }
        o->setProperty ("resonances", res);
        return juce::var (o);
    }

    juce::var recommendationsToVar (const RecommendationResult& r)
    {
        juce::Array<juce::var> items;
        for (const auto& i : r.items)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("what", juce::String (i.what));
            o->setProperty ("why", juce::String (i.why));
            o->setProperty ("confidence", confidenceName (i.confidence));
            juce::Array<juce::var> changes;
            for (const auto& c : i.changes)
            {
                auto* ch = new juce::DynamicObject();
                ch->setProperty ("paramId", juce::String (c.paramId));
                ch->setProperty ("value", num (c.value));
                changes.add (juce::var (ch));
            }
            o->setProperty ("changes", changes);
            items.add (juce::var (o));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("inputHealth", juce::String (r.inputHealth));
        o->setProperty ("suggestedCaptureGainDb", num (r.suggestedCaptureGainDb));
        o->setProperty ("items", items);
        return juce::var (o);
    }

    juce::var parametersToVar (const ChannelParameters& p)
    {
        auto* o = new juce::DynamicObject();
        ChannelParameters copy = p;
        forEachDspParameter (copy, [&] (const std::string& id, auto& v) { o->setProperty (juce::Identifier (id), num (float (v))); });
        return juce::var (o);
    }

    juce::var allowedParametersToVar (Product product)
    {
        juce::Array<juce::var> list;
        const auto& def = productDefinition (product);
        for (const auto& s : channelParameterSpecs (product))
        {
            if (! s.automatable) continue;
            const juce::String id (s.id);
            bool isMacro = false;
            for (const auto& m : def.macros) if (id == m.id) isMacro = true;
            if (id == ParamID::bypass || id == ParamID::abMatch || id == ParamID::inputTrim || id == ParamID::polarity || isMacro)
                continue;
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("name", juce::String (s.name));
            o->setProperty ("min", num (s.minValue));
            o->setProperty ("max", num (s.maxValue));
            if (! s.unit.empty()) o->setProperty ("unit", juce::String (s.unit));
            if (s.type == ParameterSpec::Type::Bool) o->setProperty ("type", "bool (0 or 1)");
            else if (s.type == ParameterSpec::Type::Choice)
            {
                juce::StringArray choices;
                for (size_t i = 0; i < s.choices.size(); ++i) choices.add (juce::String (int (i)) + "=" + s.choices[i]);
                o->setProperty ("type", "choice index: " + choices.joinIntoString (", "));
            }
            list.add (juce::var (o));
        }
        return list;
    }

    juce::var targetsToVar (const RoleTargets& t)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("capturePeakRangeDbfs", juce::String (t.capturePeakMinDb, 0) + " to " + juce::String (t.capturePeakMaxDb, 0));
        o->setProperty ("crestFactorMaxDb", num (t.crestFactorMaxDb));
        o->setProperty ("gateAppropriate", t.gateAppropriate);
        o->setProperty ("transientAppropriate", t.transientAppropriate);
        o->setProperty ("mixPeakTargetDbfs", num (t.mixPeakTargetDb));
        o->setProperty ("boxinessHz", num (t.boxinessHz));
        o->setProperty ("attackHz", num (t.attackHz));
        o->setProperty ("bodyHz", num (t.bodyHz));
        auto* bands = new juce::DynamicObject();
        for (int b = 0; b < int (Band::Count); ++b)
            bands->setProperty (kBandNames[size_t (b)], juce::String (t.bandTargetDb[size_t (b)], 0) + " dB (tolerance " + juce::String (t.bandToleranceDb[size_t (b)], 0) + ")");
        o->setProperty ("bandTargetsDbReTotal", juce::var (bands));
        return juce::var (o);
    }

    // Strict JSON schema (every property required, no additional properties).
    juce::var outputSchema()
    {
        auto obj = [] (std::initializer_list<std::pair<const char*, juce::var>> props, juce::StringArray required)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("type", "object");
            auto* p = new juce::DynamicObject();
            for (auto& kv : props) p->setProperty (kv.first, kv.second);
            o->setProperty ("properties", juce::var (p));
            juce::Array<juce::var> req; for (auto& r : required) req.add (r);
            o->setProperty ("required", req);
            o->setProperty ("additionalProperties", false);
            return juce::var (o);
        };
        auto str = [] { auto* o = new juce::DynamicObject(); o->setProperty ("type", "string"); return juce::var (o); };
        auto numT = [] { auto* o = new juce::DynamicObject(); o->setProperty ("type", "number"); return juce::var (o); };
        auto enumT = [] (juce::StringArray values) { auto* o = new juce::DynamicObject(); o->setProperty ("type", "string"); juce::Array<juce::var> e; for (auto& v : values) e.add (v); o->setProperty ("enum", e); return juce::var (o); };
        auto arr = [] (juce::var items) { auto* o = new juce::DynamicObject(); o->setProperty ("type", "array"); o->setProperty ("items", items); return juce::var (o); };

        auto change = obj ({ { "paramId", str() }, { "value", numT() } }, { "paramId", "value" });
        auto rec = obj ({ { "kind", enumT ({ "CaptureGain", "MixGain", "Gate", "Compression", "EQ", "Transient", "Filter", "Info" }) },
                          { "what", str() }, { "why", str() },
                          { "confidence", enumT ({ "LOW", "MEDIUM", "HIGH" }) },
                          { "changes", arr (change) } },
                        { "kind", "what", "why", "confidence", "changes" });
        return obj ({ { "interpretation", str() }, { "sourceAssessment", str() }, { "recommendations", arr (rec) } },
                    { "interpretation", "sourceAssessment", "recommendations" });
    }

    juce::String systemPromptFor (Product product)
    {
        const auto& def = productDefinition (product);
        const juce::String what = product == Product::Drums ? "drum channel" : product == Product::Vocals ? "vocal channel"
                                : product == Product::Keys ? "keys channel" : product == Product::Guitar ? "guitar channel" : product == Product::Bass ? "bass channel" : "master bus";
        const juce::String engineer = product == Product::Drums ? "drum" : product == Product::Vocals ? "vocal"
                                    : product == Product::Keys ? "keys" : product == Product::Guitar ? "guitar" : product == Product::Bass ? "bass" : "mastering / broadcast";
        return juce::String ("You are the soundcheck assistant inside ") + def.name + ", a low-latency " + what + " plugin used by church broadcast teams, "
        "volunteers and small venues. You think like an experienced live and broadcast " + engineer + " engineer.\n\n"
        "You receive deterministic DSP measurements of ONE " + what + " captured during soundcheck (levels, dynamics, transients, "
        "spectrum, bleed, sibilance, stereo correlation, loudness), the musical target profile, the current processing settings, the plugin's own rule-based recommendations, "
        "and, when available, the latest measurements of the other channels in the same group.\n\n"
        "Your job: interpret what the source most likely sounds like and what is wrong with it, then refine or add recommendations. "
        "The measurements are ground truth; never contradict them. Explain in plain language a volunteer can act on (WHAT, then WHY). "
        "Distinguish capture gain (a physical preamp/console change: kind CaptureGain, empty changes) from plugin-side changes. "
        "Only use parameter ids from allowed_parameters, in natural units, inside their ranges. Prefer cuts over boosts, small moves, "
        "and no more than six recommendations. Do not repeat the standard recommendations verbatim; refine them, disagree with a reason, "
        "or add what they miss (masking between channels, bleed relationships, gate/compressor timing, sibilance, stereo image, loudness for the style). "
        "Keep interpretation to three sentences. State confidence honestly. Respond only with JSON matching the schema.";
    }
}

std::string OpenAIProvider::getName() const
{
    return ("OpenAI (" + AISettings::load().model + ")").toStdString();
}

juce::String OpenAIProvider::buildRequestBody (const IntelligenceRequest& r, const AISettings& settings)
{
    auto* ctx = new juce::DynamicObject();
    ctx->setProperty ("channelRole", channelRoleName (r.role));
    ctx->setProperty ("styleProfile", styleProfileName (r.style));
    ctx->setProperty ("group", juce::String (r.groupName));
    ctx->setProperty ("analysis", analysisToVar (r.analysis));
    if (r.output.valid)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("peakDbfs", num (r.output.peakDb));
        o->setProperty ("rmsDbfs", num (r.output.rmsDb));
        ctx->setProperty ("processedOutputDuringCapture", juce::var (o));
    }
    ctx->setProperty ("profileTargets", targetsToVar (StyleProfile::targets (r.role, r.style)));
    ctx->setProperty ("currentParameters", parametersToVar (r.currentParameters));
    ctx->setProperty ("standardRecommendations", recommendationsToVar (r.standardRecommendations));
    juce::Array<juce::var> kit;
    for (const auto& m : r.kitContext)
    {
        if (! m.analysis.valid) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (m.name));
        o->setProperty ("role", channelRoleName (m.role));
        o->setProperty ("peakDbfs", num (m.analysis.peakDb));
        o->setProperty ("hitLevelDbfs", num (m.analysis.hitLevelDb));
        o->setProperty ("noiseFloorDbfs", num (m.analysis.noiseFloorDb));
        o->setProperty ("bleedEstimate", num (m.analysis.bleedEstimate));
        auto* bands = new juce::DynamicObject();
        for (int b = 0; b < int (Band::Count); ++b) bands->setProperty (kBandNames[size_t (b)], num (m.analysis.bandEnergyDb[size_t (b)]));
        o->setProperty ("bandEnergyDbReTotal", juce::var (bands));
        o->setProperty ("outputTrimDb", num (m.outputTrimDb));
        kit.add (juce::var (o));
    }
    ctx->setProperty ("otherKitChannels", kit);
    ctx->setProperty ("allowed_parameters", allowedParametersToVar (productOf (r.role)));

    const juce::String userText = "Interpret this soundcheck capture and return recommendations as JSON.\n\n"
                                + juce::JSON::toString (juce::var (ctx), true);

    auto* body = new juce::DynamicObject();
    body->setProperty ("model", settings.model.trim());
    body->setProperty ("max_completion_tokens", 6000);
    if (isReasoningModel (settings.model))
        body->setProperty ("reasoning_effort", settings.effort);

    auto* schemaWrapper = new juce::DynamicObject();
    schemaWrapper->setProperty ("name", "livemix_recommendations");
    schemaWrapper->setProperty ("strict", true);
    schemaWrapper->setProperty ("schema", outputSchema());
    auto* format = new juce::DynamicObject();
    format->setProperty ("type", "json_schema");
    format->setProperty ("json_schema", juce::var (schemaWrapper));
    body->setProperty ("response_format", juce::var (format));

    auto* sys = new juce::DynamicObject();
    sys->setProperty ("role", "system");
    sys->setProperty ("content", systemPromptFor (productOf (r.role)));
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("role", "user");
    msg->setProperty ("content", userText);
    juce::Array<juce::var> messages; messages.add (juce::var (sys)); messages.add (juce::var (msg));
    body->setProperty ("messages", messages);
    return juce::JSON::toString (juce::var (body), true);
}

juce::String OpenAIProvider::post (const juce::String& body, const AISettings& settings, int& statusOut, juce::String& errorOut,
                                   const std::atomic<bool>* shouldCancel)
{
    const auto r = http::postJson (kEndpoint, body, settings.apiKey, settings.timeoutSeconds, shouldCancel);
    statusOut = r.status;
    errorOut = r.error;
    return r.body;
}

IntelligenceResponse OpenAIProvider::parseResponse (const juce::String& body, int status, const juce::String& model)
{
    IntelligenceResponse r;
    r.providerName = ("OpenAI (" + model + ")").toStdString();
    auto json = juce::JSON::parse (body);
    if (status != 200)
    {
        juce::String msg = "HTTP " + juce::String (status);
        if (auto* err = json.getProperty ("error", juce::var()).getDynamicObject())
            msg += ": " + err->getProperty ("message").toString();
        r.error = msg.toStdString();
        return r;
    }
    auto* choices = json.getProperty ("choices", juce::var()).getArray();
    if (choices == nullptr || choices->isEmpty()) { r.error = "no choices in response"; return r; }
    const auto choice = (*choices)[0];
    const auto message = choice.getProperty ("message", juce::var());
    const auto refusal = message.getProperty ("refusal", juce::var());
    if (refusal.isString() && refusal.toString().isNotEmpty()) { r.error = ("the model declined: " + refusal.toString()).toStdString(); return r; }
    if (choice.getProperty ("finish_reason", "").toString() == "length") { r.error = "response was truncated"; return r; }

    const juce::String text = message.getProperty ("content", "").toString();
    if (text.isEmpty()) { r.error = "empty response"; return r; }

    auto parsed = juce::JSON::parse (text);
    if (! parsed.isObject()) { r.error = "response was not valid JSON"; return r; }

    r.interpretation = parsed.getProperty ("interpretation", "").toString().toStdString();
    const auto assessment = parsed.getProperty ("sourceAssessment", "").toString();
    if (assessment.isNotEmpty()) r.interpretation += (r.interpretation.empty() ? "" : " ") + assessment.toStdString();

    if (auto* recs = parsed.getProperty ("recommendations", juce::var()).getArray())
    {
        for (const auto& item : *recs)
        {
            Recommendation rec;
            const auto kind = item.getProperty ("kind", "Info").toString();
            rec.kind = kind == "CaptureGain" ? Recommendation::Kind::CaptureGain
                     : kind == "MixGain" ? Recommendation::Kind::MixGain
                     : kind == "Gate" ? Recommendation::Kind::Gate
                     : kind == "Compression" ? Recommendation::Kind::Compression
                     : kind == "EQ" ? Recommendation::Kind::EQ
                     : kind == "Transient" ? Recommendation::Kind::Transient
                     : kind == "Filter" ? Recommendation::Kind::Filter
                     : Recommendation::Kind::Info;
            rec.what = item.getProperty ("what", "").toString().toStdString();
            rec.why = item.getProperty ("why", "").toString().toStdString();
            const auto conf = item.getProperty ("confidence", "MEDIUM").toString();
            rec.confidence = conf == "HIGH" ? Confidence::High : conf == "LOW" ? Confidence::Low : Confidence::Medium;
            if (auto* changes = item.getProperty ("changes", juce::var()).getArray())
                for (const auto& c : *changes)
                {
                    const auto id = c.getProperty ("paramId", "").toString();
                    if (id.isNotEmpty()) rec.changes.push_back ({ id.toStdString(), float (double (c.getProperty ("value", 0.0))) });
                }
            if (rec.kind == Recommendation::Kind::CaptureGain) rec.changes.clear(); // physical action only
            if (! rec.what.empty()) r.recommendations.push_back (rec);
        }
    }
    r.valid = true;
    return r;
}

IntelligenceResponse OpenAIProvider::interpret (const IntelligenceRequest& request, const std::atomic<bool>& shouldCancel)
{
    const AISettings settings = AISettings::load();
    IntelligenceResponse r;
    r.providerName = getName();
    if (! settings.hasKey()) { r.error = "no API key configured"; return r; }
    if (shouldCancel.load()) { r.error = "cancelled"; return r; }

    int status = 0;
    juce::String error;
    const juce::String body = post (buildRequestBody (request, settings), settings, status, error, &shouldCancel);
    if (shouldCancel.load()) { r.error = "cancelled"; return r; }
    if (error.isNotEmpty()) { r.error = error.toStdString(); return r; }
    return parseResponse (body, status, settings.model);
}

bool OpenAIProvider::testConnection (const AISettings& settings, juce::String& messageOut, const std::atomic<bool>* shouldCancel)
{
    if (! settings.hasKey()) { messageOut = "No API key entered."; return false; }
    auto* body = new juce::DynamicObject();
    body->setProperty ("model", settings.model.trim());
    body->setProperty ("max_completion_tokens", 64);
    if (isReasoningModel (settings.model)) body->setProperty ("reasoning_effort", "low");
    auto* msg = new juce::DynamicObject(); msg->setProperty ("role", "user"); msg->setProperty ("content", "Reply with the single word OK.");
    juce::Array<juce::var> messages; messages.add (juce::var (msg));
    body->setProperty ("messages", messages);

    int status = 0;
    juce::String error;
    AISettings quick = settings; quick.timeoutSeconds = 20;
    const auto response = post (juce::JSON::toString (juce::var (body), true), quick, status, error, shouldCancel);
    if (error.isNotEmpty()) { messageOut = error; return false; }
    auto json = juce::JSON::parse (response);
    if (status != 200)
    {
        messageOut = "HTTP " + juce::String (status);
        if (auto* err = json.getProperty ("error", juce::var()).getDynamicObject()) messageOut += ": " + err->getProperty ("message").toString();
        return false;
    }
    messageOut = "Connected. Model " + json.getProperty ("model", settings.model).toString() + " answered.";
    return true;
}

} // namespace livemix
