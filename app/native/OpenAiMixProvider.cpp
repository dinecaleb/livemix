#include "OpenAiMixProvider.h"
#include "Intelligence/HttpJson.h"

namespace livemix
{

namespace
{
    constexpr const char* kEndpoint = "https://api.openai.com/v1/chat/completions";

    bool isReasoningModel (const juce::String& model)
    {
        const auto m = model.trim().toLowerCase();
        return m.startsWith ("gpt-5") || m.startsWith ("o1") || m.startsWith ("o3") || m.startsWith ("o4");
    }

    juce::var stringArray (std::initializer_list<const char*> items)
    {
        juce::Array<juce::var> a;
        for (const char* s : items) a.add (juce::String (s));
        return a;
    }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> members)
    {
        auto* o = new juce::DynamicObject();
        for (const auto& m : members) o->setProperty (juce::Identifier (m.first), m.second);
        return juce::var (o);
    }

    // Every objective the resolver knows how to build. Sent as an enum so the model cannot
    // ask for a kind of change DLIVE has no way to express.
    juce::var objectiveTypes()
    {
        juce::Array<juce::var> a;
        for (int i = 0; i < int (MixObjectiveType::Count); ++i) a.add (juce::String (mixObjectiveId (MixObjectiveType (i))));
        return a;
    }

    // The schema the reply has to match. Machine actions never come from prose: if the reply
    // does not fit this shape it is rejected whole, and the deterministic mix stands.
    juce::var intentSchema()
    {
        const auto objective = object ({
            { "type", "object" },
            { "additionalProperties", false },
            { "required", stringArray ({ "type", "strength", "against", "character", "preserveArticulation", "preserveTransients" }) },
            { "properties", object ({
                { "type", object ({ { "type", "string" }, { "enum", objectiveTypes() },
                                    { "description", "The sonic outcome wanted. Never a processor." } }) },
                { "strength", object ({ { "type", "number" },
                                        { "description", "-1 to 1. The sign is the direction: +1 is as far towards the objective "
                                                         "as one Tune will go, -1 is as far the other way, 0 is leave it alone." } }) },
                { "against", object ({ { "type", "string" },
                                       { "description", "For separation only: the target id this is making room for, e.g. \"strip:8\". "
                                                        "Empty otherwise." } }) },
                { "character", object ({ { "type", "string" },
                                         { "description", "For character only, on an effect return: the kind of space wanted, in your own "
                                                          "words (\"warm medium plate\", \"short vintage spring\"). Empty otherwise. Ask for what "
                                                          "you want; DLIVE will build the closest honest thing it has, or say it cannot." } }) },
                { "preserveArticulation", object ({ { "type", "boolean" },
                                                    { "description", "Depth must not cost the words." } }) },
                { "preserveTransients", object ({ { "type", "boolean" },
                                                  { "description", "Control must not cost the attack." } }) } }) } });

        const auto target = object ({
            { "type", "object" },
            { "additionalProperties", false },
            { "required", stringArray ({ "target", "name", "reason", "confidence", "objectives" }) },
            { "properties", object ({
                { "target", object ({ { "type", "string" },
                                      { "description", "A target id from the capabilities list: \"strip:N\", \"bus:VOCALS\", \"fx:Vocal Plate\"." } }) },
                { "name", object ({ { "type", "string" } }) },
                { "reason", object ({ { "type", "string" },
                                      { "description", "One or two plain sentences a church volunteer can read. This is shown to the user." } }) },
                { "confidence", object ({ { "type", "string" }, { "enum", stringArray ({ "LOW", "MEDIUM", "HIGH" }) } }) },
                { "objectives", object ({ { "type", "array" }, { "items", objective } }) } }) } });

        return object ({
            { "type", "object" },
            { "additionalProperties", false },
            { "required", stringArray ({ "schemaVersion", "summary", "noChangeRequired", "targets", "unsupportedRequests" }) },
            { "properties", object ({
                { "schemaVersion", object ({ { "type", "integer" } }) },
                { "summary", object ({ { "type", "string" },
                                       { "description", "One or two plain sentences about the whole mix." } }) },
                { "noChangeRequired", object ({ { "type", "boolean" },
                                                { "description", "True when the mix is already right. This is a professional answer and "
                                                                 "often the correct one." } }) },
                { "targets", object ({ { "type", "array" }, { "items", target } }) },
                { "unsupportedRequests", object ({ { "type", "array" }, { "items", object ({ { "type", "string" } }) },
                                                   { "description", "Anything you judged the mix needs that is not a DLIVE mix decision - "
                                                                    "a preamp that is set wrong, a microphone out of phase, a player who "
                                                                    "needs to be told. Said plainly; the user is shown these." } }) } }) } });
    }
}

std::string OpenAiMixProvider::getName() const
{
    return ("OpenAI (" + AISettings::load().model + ")").toStdString();
}

juce::String OpenAiMixProvider::buildRequestBody (const MixReasoningRequest& request, const AISettings& settings)
{
    juce::String user;
    user << "THE MIX\n" << juce::String (request.context.write (true)) << "\n\n";
    user << "WHAT DLIVE CAN DO\n" << juce::String (request.capabilities.write (true)) << "\n\n";

    if (request.refinement && request.hasVerification)
    {
        user << "WHAT YOU ASKED FOR LAST TIME\n" << juce::String (request.previousIntent.write (true)) << "\n\n";
        user << "WHAT WAS ACTUALLY APPLIED\n";
        for (const auto& a : request.applied) user << "- " << juce::String (a) << "\n";
        user << "\nWHAT THE SECOND LISTEN MEASURED AFTER IT WAS APPLIED\n"
             << juce::String (request.verification.write (true)) << "\n\n";
        user << "This is a refinement pass. Compare before and after: did the masking improve, did the hierarchy "
                "improve, did headroom improve, did anything new appear, did any of it go too far? Make small "
                "corrections only, or answer noChangeRequired. Do not start the mix again.\n";
    }
    else
    {
        user << "Give the sonic intent for this mix. Leave alone anything that does not need changing.\n";
        if (! request.userRequest.empty())
            user << "\nThe engineer also asked for this, in their own words: \"" << juce::String (request.userRequest) << "\"\n";
    }

    auto* schemaWrapper = new juce::DynamicObject();
    schemaWrapper->setProperty ("name", "dlive_mix_intent");
    schemaWrapper->setProperty ("strict", true);
    schemaWrapper->setProperty ("schema", intentSchema());

    auto* format = new juce::DynamicObject();
    format->setProperty ("type", "json_schema");
    format->setProperty ("json_schema", juce::var (schemaWrapper));

    auto* system = new juce::DynamicObject();
    system->setProperty ("role", "system");
    system->setProperty ("content", juce::String (request.instructions)
                                    + "\nAnswer only with JSON matching the schema. Use the target ids from the "
                                      "capabilities list exactly. Never ask for a processor by name - ask for the sound.");
    auto* message = new juce::DynamicObject();
    message->setProperty ("role", "user");
    message->setProperty ("content", user);

    juce::Array<juce::var> messages;
    messages.add (juce::var (system));
    messages.add (juce::var (message));

    auto* body = new juce::DynamicObject();
    body->setProperty ("model", settings.model.trim());
    body->setProperty ("max_completion_tokens", 8000);
    if (isReasoningModel (settings.model)) body->setProperty ("reasoning_effort", settings.effort);
    body->setProperty ("response_format", juce::var (format));
    body->setProperty ("messages", messages);
    return juce::JSON::toString (juce::var (body), true);
}

MixReasoningResponse OpenAiMixProvider::parseResponse (const juce::String& body, int status, const juce::String& model)
{
    MixReasoningResponse out;
    out.providerName = ("OpenAI (" + model + ")").toStdString();
    out.model = model.toStdString();

    const auto json = juce::JSON::parse (body);
    if (status != 200)
    {
        juce::String message = "The mix engineer could not be reached (HTTP " + juce::String (status) + ")";
        if (auto* error = json.getProperty ("error", juce::var()).getDynamicObject())
            message += ": " + error->getProperty ("message").toString();
        message += ".";
        out.error = message.toStdString();
        return out;
    }

    auto* choices = json.getProperty ("choices", juce::var()).getArray();
    if (choices == nullptr || choices->isEmpty()) { out.error = "The reply carried no answer."; return out; }
    const auto choice = (*choices)[0];
    const auto message = choice.getProperty ("message", juce::var());
    const auto refusal = message.getProperty ("refusal", juce::var());
    if (refusal.isString() && refusal.toString().isNotEmpty())
    {
        out.error = ("The mix engineer declined: " + refusal.toString()).toStdString();
        return out;
    }
    if (choice.getProperty ("finish_reason", "").toString() == "length")
    {
        out.error = "The reply was cut off before it finished.";
        return out;
    }

    const juce::String text = message.getProperty ("content", "").toString();
    if (text.isEmpty()) { out.error = "The reply was empty."; return out; }

    out.intent = parseMixIntentText (text.toStdString(), &out.problems);
    if (! out.intent.valid)
    {
        out.error = out.problems.empty() ? "The reply did not describe a mix." : out.problems.front();
        return out;
    }
    out.valid = true;
    return out;
}

MixReasoningResponse OpenAiMixProvider::reason (const MixReasoningRequest& request, const std::atomic<bool>& shouldCancel)
{
    const AISettings settings = AISettings::load();
    MixReasoningResponse out;
    out.providerName = getName();
    out.model = settings.model.toStdString();

    if (! settings.hasKey())
    {
        out.error = "No API key is configured for the cloud mix engineer.";
        return out;
    }
    if (shouldCancel.load()) { out.error = "Cancelled."; return out; }

    const auto body = buildRequestBody (request, settings);
    const auto result = http::postJson (kEndpoint, body, settings.apiKey, settings.timeoutSeconds, &shouldCancel);
    if (shouldCancel.load()) { out.error = "Cancelled."; return out; }
    if (result.error.isNotEmpty())
    {
        out.error = result.error == "cancelled"
                        ? "Cancelled."
                        : "Internet connection unavailable. DLIVE can go on mixing and recording normally; "
                          "TUNE LIVE MIX needs a connection for the cloud engineer.";
        return out;
    }

    auto parsed = parseResponse (result.body, result.status, settings.model);
    parsed.requestBytes = body.getNumBytesAsUTF8() > 0 ? int (body.getNumBytesAsUTF8()) : 0;
    return parsed;
}

} // namespace livemix
