#include "MixRequestParser.h"
#include <algorithm>
#include <cctype>

namespace livemix
{

namespace
{
    std::string lower (const std::string& s)
    {
        std::string out = s;
        for (auto& c : out) c = char (std::tolower (static_cast<unsigned char> (c)));
        return out;
    }

    bool has (const std::string& hay, const char* needle) { return hay.find (needle) != std::string::npos; }

    // Whole-word containment, so "bass" does not match inside "bassoon" and "amb" does not
    // match inside "ambulance". Short words are exactly where a substring match goes wrong.
    bool hasWord (const std::string& hay, const std::string& word)
    {
        if (word.empty()) return false;
        size_t from = 0;
        for (;;)
        {
            const size_t at = hay.find (word, from);
            if (at == std::string::npos) return false;
            const bool leftOk = at == 0 || ! std::isalnum (static_cast<unsigned char> (hay[at - 1]));
            const size_t end = at + word.size();
            const bool rightOk = end >= hay.size() || ! std::isalnum (static_cast<unsigned char> (hay[end]));
            if (leftOk && rightOk) return true;
            from = at + 1;
        }
    }

    // ---- direction -------------------------------------------------------
    // "more", "less", "too much", "not enough". The sign of every objective comes from here,
    // and getting it wrong is the one mistake that makes a chat unusable, so the negatives are
    // checked first: "not bright enough" is a request for MORE brightness.
    struct Direction { float sign = 1.0f; bool explicitly = false; };

    Direction directionOf (const std::string& t, bool complaintIsExcess)
    {
        Direction d;
        // "too X" and "X is harsh" are complaints: the fix runs against the quality named.
        if (has (t, "too much") || has (t, "too many")) { d.sign = -1.0f; d.explicitly = true; return d; }
        if (has (t, "not enough") || has (t, "more of") || has (t, "needs more")) { d.sign = 1.0f; d.explicitly = true; return d; }
        if (hasWord (t, "less") || hasWord (t, "reduce") || hasWord (t, "lower") || hasWord (t, "cut")
            || hasWord (t, "pull") || hasWord (t, "back") || hasWord (t, "down") || hasWord (t, "quieter")
            || hasWord (t, "softer") || hasWord (t, "remove") || hasWord (t, "tame"))
        {
            d.sign = -1.0f; d.explicitly = true; return d;
        }
        if (hasWord (t, "more") || hasWord (t, "add") || hasWord (t, "raise") || hasWord (t, "boost")
            || hasWord (t, "up") || hasWord (t, "louder") || hasWord (t, "forward") || hasWord (t, "front"))
        {
            d.sign = 1.0f; d.explicitly = true; return d;
        }
        // No direction word. A complaint ("the vocals are harsh") asks for less of the thing
        // complained about; a wish ("punchier") asks for more.
        d.sign = complaintIsExcess ? -1.0f : 1.0f;
        return d;
    }

    // ---- what was asked for ---------------------------------------------
    struct Phrase
    {
        const char* words;              // any one of these, separated by '|'
        MixObjectiveType objective;
        bool complaintIsExcess;         // the word names a fault, so the default direction is "less"
        float strength;                 // how far a plain, undirected request goes
        const char* readAs;             // what DLIVE says it heard
    };

    const std::vector<Phrase>& phrases()
    {
        static const std::vector<Phrase> p {
            // Complaints: the word names something wrong, so the default is to take it away.
            { "harsh|harshness|piercing|shrill|brittle|edgy|ear|fatiguing", MixObjectiveType::Brightness, true, 0.55f, "less harshness" },
            { "muddy|mud|boxy|boxiness|cloudy|woolly|congested", MixObjectiveType::Clarity, false, 0.6f, "less mud, more clarity" },
            { "boomy|boom|thick|tubby|bloated", MixObjectiveType::Warmth, true, 0.55f, "less low-mid weight" },
            { "sibilant|sibilance|essy|hissy|sharp s|lispy", MixObjectiveType::Sibilance, true, 0.6f, "sharp S sounds under control" },
            { "thin|weak|small|lightweight", MixObjectiveType::Body, false, 0.5f, "more weight" },
            { "dull|lifeless|muffled|veiled|covered|dark", MixObjectiveType::Brightness, false, 0.5f, "more air" },
            { "buried|lost|hidden|swamped|can't hear|cannot hear|drowned", MixObjectiveType::Presence, false, 0.65f, "further forward" },
            { "jumpy|uneven|inconsistent|all over|dipping|disappearing", MixObjectiveType::DynamicStability, false, 0.55f, "a steadier level" },
            { "noisy|noise|hiss|rumble|bleed|spill|hum", MixObjectiveType::Cleanup, false, 0.5f, "less of what is between the notes" },
            { "squashed|crushed|lifeless dynamics|over compressed|overcompressed", MixObjectiveType::DynamicStability, true, 0.5f, "more dynamic life" },

            // Wishes: the word names something wanted.
            { "forward|front|in front|up front|closer|prominent|feature", MixObjectiveType::Presence, false, 0.6f, "further forward" },
            { "punch|punchy|punchier|impact|hit|smack|attack|snappy|snap", MixObjectiveType::Punch, false, 0.6f, "more punch" },
            { "warm|warmer|warmth|fuller|full|rich|richer", MixObjectiveType::Warmth, false, 0.55f, "more warmth" },
            { "bright|brighter|air|airy|crisp|sparkle|shine|top end|sheen", MixObjectiveType::Brightness, false, 0.55f, "more air" },
            { "clear|clearer|clarity|define|defined|definition|articulate|intelligib", MixObjectiveType::Clarity, false, 0.55f, "more clarity" },
            { "weight|body|bottom|low end|deeper|deep|fat|fatter", MixObjectiveType::Body, false, 0.55f, "more weight" },
            { "space|room|reverb|wet|ambience|distant|far|behind|verb", MixObjectiveType::SpatialDepth, false, 0.55f, "more space" },
            { "dry|drier|dryer|tighter space|close|intimate", MixObjectiveType::SpatialDepth, true, 0.55f, "less space" },
            { "wide|wider|width|spread|big stereo", MixObjectiveType::Width, false, 0.5f, "a wider image" },
            { "narrow|narrower|mono|centre|center|tighter image", MixObjectiveType::Width, true, 0.5f, "a narrower image" },
            { "steady|steadier|even|consistent|controlled|control|smooth out|glue", MixObjectiveType::DynamicStability, false, 0.55f, "a steadier level" },
            { "louder|loud|turn up|bring up|level up", MixObjectiveType::Level, false, 0.55f, "more level" },
            { "quieter|turn down|bring down|pull down|level down", MixObjectiveType::Level, true, 0.55f, "less level" },
            { "clean|cleaner|clean up|tidy", MixObjectiveType::Cleanup, false, 0.5f, "a cleaner channel" },
        };
        return p;
    }

    bool phraseMatches (const Phrase& p, const std::string& t, std::string& matched)
    {
        std::string word;
        for (const char* c = p.words; ; ++c)
        {
            if (*c == '|' || *c == '\0')
            {
                if (! word.empty() && (word.find (' ') != std::string::npos ? has (t, word.c_str()) : hasWord (t, word)))
                {
                    matched = word;
                    return true;
                }
                word.clear();
                if (*c == '\0') return false;
            }
            else word += *c;
        }
    }

    // ---- who it was about ------------------------------------------------
    // A target is found by what it is called ("Pastor"), by its role ("lead vocal"), by its
    // group ("the drums"), or by a word an engineer would use for it ("kick", "BVs").
    struct Named { MixTargetRef ref; std::string name; int score = 0; };

    void addSynonyms (const std::string& role, std::vector<std::string>& out)
    {
        const std::string r = lower (role);
        auto add = [&] (const char* s) { out.push_back (s); };
        if (r == "lead vocal") { add ("lead"); add ("lead vocal"); add ("lead vox"); add ("main vocal"); add ("singer"); add ("vocal"); }
        else if (r == "backing vocal") { add ("backing"); add ("bgv"); add ("bgvs"); add ("bv"); add ("bvs"); add ("backing vocals"); add ("backups"); }
        else if (r == "choir") { add ("choir"); }
        else if (r == "speech") { add ("pastor"); add ("speech"); add ("preacher"); add ("sermon"); add ("talk"); add ("host"); }
        else if (r == "kick in" || r == "kick out") { add ("kick"); add ("bass drum"); }
        else if (r == "snare top" || r == "snare bottom") { add ("snare"); }
        else if (r == "hi-hat") { add ("hat"); add ("hihat"); add ("hi-hat"); }
        else if (r == "rack tom" || r == "floor tom") { add ("tom"); add ("toms"); }
        else if (r.rfind ("overhead", 0) == 0) { add ("overheads"); add ("overhead"); add ("cymbals"); }
        else if (r == "room") { add ("room"); }
        else if (r == "crowd mic") { add ("crowd"); add ("congregation"); add ("audience"); }
        else if (r == "ambience mic") { add ("ambience"); add ("ambient"); add ("room mic"); }
        else if (r == "bass di" || r == "bass amp" || r == "synth bass") { add ("bass"); }
        else if (r == "piano") { add ("piano"); add ("keys"); }
        else if (r == "electric piano") { add ("rhodes"); add ("electric piano"); add ("ep"); }
        else if (r == "organ") { add ("organ"); }
        else if (r == "synth pad" || r == "synth lead") { add ("synth"); add ("pad"); add ("tracks"); }
        else if (r == "acoustic guitar") { add ("acoustic"); add ("acoustic guitar"); }
        else if (r.rfind ("electric", 0) == 0) { add ("guitar"); add ("electric"); }
        else if (r == "alto sax" || r == "tenor sax" || r == "baritone sax") { add ("sax"); add ("saxophone"); add ("horn"); }
    }

    std::vector<Named> findTargets (const std::string& t, const MixContext& c, const DspCapabilityRegistry& reg)
    {
        std::vector<Named> found;
        auto push = [&] (MixTargetRef ref, const std::string& name, int score)
        {
            for (auto& f : found) if (f.ref == ref) { f.score = std::max (f.score, score); return; }
            if (reg.find (ref) == nullptr) return;         // never name something that is not there
            found.push_back ({ ref, name, score });
        };

        // A channel by its own name first: it is what the engineer wrote on the desk.
        for (const auto& track : c.tracks)
        {
            const std::string name = lower (track.name);
            if (name.size() >= 2 && hasWord (t, name))
                push ({ MixTargetKind::Strip, track.id }, track.name, 100 + int (name.size()));
        }
        // Then by what it is.
        for (const auto& track : c.tracks)
        {
            std::vector<std::string> syn;
            addSynonyms (track.role, syn);
            for (const auto& s : syn)
                if (s.find (' ') != std::string::npos ? has (t, s.c_str()) : hasWord (t, s))
                    push ({ MixTargetKind::Strip, track.id }, track.name, 50 + int (s.size()));
        }
        // Groups. "the drums" and "the vocals" mean the bus, which is usually what is meant
        // when a whole family is named - and it is one move rather than six.
        struct BusWord { const char* word; MixBus bus; };
        static const BusWord busWords[] = {
            { "drums", MixBus::Drums }, { "kit", MixBus::Drums },
            { "bass group", MixBus::Bass },
            { "music", MixBus::Music }, { "band", MixBus::Music },
            { "vocals", MixBus::Vocals }, { "voices", MixBus::Vocals }, { "singers", MixBus::Vocals },
            { "speech", MixBus::Speech },
            { "crowd", MixBus::Ambience }, { "congregation", MixBus::Ambience }, { "room mics", MixBus::Ambience },
            { "master", MixBus::Master }, { "mix", MixBus::Master }, { "output", MixBus::Master },
        };
        for (const auto& bw : busWords)
        {
            if (! (std::string (bw.word).find (' ') != std::string::npos ? has (t, bw.word) : hasWord (t, bw.word))) continue;
            const auto& bus = c.buses[size_t (bw.bus)];
            if (! bus.used && bw.bus != MixBus::Master) continue;
            push ({ MixTargetKind::Bus, int (bw.bus) }, mixBusName (bw.bus), 70);
        }
        // Effect returns, so "less reverb on the backing vocals" can reach the return itself.
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            const std::string name = lower (fxSlotName (FxSlot (f)));
            if (has (t, name.c_str())) push ({ MixTargetKind::FxSlot, f }, fxSlotName (FxSlot (f)), 60);
        }

        std::stable_sort (found.begin(), found.end(), [] (const Named& a, const Named& b) { return a.score > b.score; });
        return found;
    }

    // "the mix sounds muddy" / "make the master louder" with nobody named: the master is what
    // a statement about the whole mix is about.
    bool aboutTheWholeMix (const std::string& t)
    {
        return has (t, "the mix") || has (t, "everything") || has (t, "overall")
            || has (t, "whole mix") || hasWord (t, "master") || has (t, "it sounds");
    }
}

std::vector<std::string> mixRequestExamples()
{
    return {
        "Bring the lead vocal forward",
        "The vocals sound harsh",
        "Make the drums punchier",
        "Give the sax more space",
        "The mix sounds muddy",
        "Make the master louder without clipping",
        "Less reverb on the backing vocals",
        "The pastor's mic is boomy",
        "Turn the crowd up a little",
    };
}

MixRequestReading readMixRequest (const std::string& request, const MixContext& context,
                                  const DspCapabilityRegistry& registry)
{
    MixRequestReading out;
    const std::string t = lower (request);
    if (t.find_first_not_of (" \t\n") == std::string::npos)
    {
        out.failure = "Say what you would like changed - \"bring the lead vocal forward\", \"the drums need more punch\".";
        return out;
    }

    // Things a mix cannot fix, recognised so they are answered honestly rather than acted on.
    static const char* notOurs[] = { "feedback", "phase", "off mic", "off-mic", "cable", "buzzing", "buzz",
                                     "batteries", "battery", "out of tune", "sing louder", "monitor wedge" };
    for (const char* n : notOurs)
        if (has (t, n))
            out.notMixDecisions.push_back (std::string ("\"") + n + "\" is a problem at the source, not in the mix. "
                                           "DLIVE will not try to hide it with processing.");

    auto targets = findTargets (t, context, registry);
    const bool wholeMix = targets.empty() && aboutTheWholeMix (t);
    if (wholeMix) targets.push_back ({ { MixTargetKind::Bus, int (MixBus::Master) }, "MASTER", 10 });

    // Everything matched in the sentence. One request can carry two wishes ("warmer and
    // further forward") and both are honoured.
    struct Wish { MixObjectiveType type; float strength; std::string readAs; };
    std::vector<Wish> wishes;
    for (const auto& p : phrases())
    {
        std::string matched;
        if (! phraseMatches (p, t, matched)) continue;
        const auto dir = directionOf (t, p.complaintIsExcess);
        float strength = p.strength * dir.sign;
        // "a little", "slightly", "a bit" halve it; "a lot", "much", "way" push it up.
        if (has (t, "a little") || hasWord (t, "slightly") || has (t, "a bit") || hasWord (t, "touch")) strength *= 0.5f;
        if (has (t, "a lot") || hasWord (t, "much") || hasWord (t, "way") || hasWord (t, "really") || hasWord (t, "very")) strength *= 1.4f;
        strength = strength < -1.0f ? -1.0f : (strength > 1.0f ? 1.0f : strength);
        if (std::fabs (strength) < 0.05f) continue;

        // A complaint word already means "take it away"; a direction word on top of it would
        // otherwise flip it twice ("less harsh" must not mean "more harsh").
        if (p.complaintIsExcess && ! dir.explicitly) strength = -std::fabs (strength);

        bool duplicate = false;
        for (auto& w : wishes) if (w.type == p.objective) { duplicate = true; if (std::fabs (strength) > std::fabs (w.strength)) w.strength = strength; }
        if (! duplicate) wishes.push_back ({ p.objective, strength, p.readAs });
    }

    // Two named sources and a "make room" word is the one request that is about a
    // relationship rather than about a channel: "the keys are covering the lead". That *is*
    // the wish, so it is recognised before anything is refused for not naming one.
    const bool separation = targets.size() >= 2
        && (has (t, "room for") || has (t, "covering") || has (t, "on top of") || has (t, "fighting")
            || has (t, "in the way") || has (t, "masking") || has (t, "out of the way"));

    if (wishes.empty() && ! separation)
    {
        out.failure = targets.empty()
            ? "DLIVE did not follow that. Name a channel and what you want from it - \"the lead vocal needs more presence\", \"the drums are muddy\"."
            : "DLIVE understood which channel you mean, but not what you want from it. Try \"forward\", \"warmer\", "
              "\"brighter\", \"punchier\", \"more space\", \"steadier\" or \"cleaner\".";
        return out;
    }
    if (targets.empty())
    {
        out.failure = "DLIVE understood what you want, but not which channel. Name it - \"the lead vocal\", \"the drums\", "
                      "\"the pastor's mic\" - or say \"the mix\" for the whole thing.";
        return out;
    }

    out.intent.valid = true;
    out.intent.schemaVersion = kMixIntentSchemaVersion;

    // At most three targets from one sentence: a chat turn is a small, reviewable change, not
    // a re-mix. The best match is the one the engineer named most specifically.
    const size_t limit = std::min<size_t> (targets.size(), separation ? 2 : 3);
    for (size_t i = 0; i < limit; ++i)
    {
        MixTargetIntent ti;
        ti.target = targets[i].ref;
        ti.targetName = targets[i].name;
        ti.confidence = targets[i].score >= 50 ? Confidence::High : Confidence::Medium;

        std::string sentence;
        if (separation && i == 0 && wishes.empty())
        {
            // The source being made room for is not changed: the fix goes where the conflict
            // is, which is in whatever is covering it.
            continue;
        }
        if (separation && i == 1)
        {
            MixObjective o;
            o.type = MixObjectiveType::Separation;
            o.strength = 0.6f;
            o.against = targets[0].ref;
            ti.objectives.push_back (o);
            sentence = targets[1].name + " makes room for " + targets[0].name + ".";
            out.readAs.push_back (targets[1].name + " - make room for " + targets[0].name);
        }
        else
        {
            for (const auto& w : wishes)
            {
                MixObjective o;
                o.type = w.type;
                o.strength = w.strength;
                // Two rules a live engineer would never break, carried on every objective the
                // chat makes so the resolver keeps them: depth must not cost the words, and
                // control must not cost the attack.
                o.preserveArticulation = true;
                o.preserveTransients = w.type == MixObjectiveType::DynamicStability || w.type == MixObjectiveType::Punch;
                ti.objectives.push_back (o);
                out.readAs.push_back (targets[i].name + " - " + w.readAs);
                sentence += (sentence.empty() ? std::string() : std::string(" ")) + "You asked for " + w.readAs + ".";
            }
        }
        ti.reason = sentence.empty() ? std::string ("Asked for in the chat.") : sentence;
        out.intent.targets.push_back (ti);
    }

    // "Louder without clipping" is a real request and a real refusal at once: DLIVE will aim
    // the level where it is asked to and will not spend the master's headroom doing it.
    if (has (t, "without clipping") || has (t, "no clipping") || has (t, "don't clip") || has (t, "dont clip"))
        out.notMixDecisions.push_back ("The master keeps its headroom: the limiter holds the ceiling and is not "
                                       "pushed harder to make the number bigger. To aim the whole mix louder, set "
                                       "the delivery loudness on PURPOSE AND SOUND and run TUNE MIX.");

    out.intent.summary = "From the chat: " + request;
    out.understood = ! out.intent.targets.empty();
    if (! out.understood) out.failure = "DLIVE did not follow that.";
    return out;
}

} // namespace livemix
