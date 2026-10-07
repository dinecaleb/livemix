#include "MixBuddy.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>

namespace livemix
{

namespace
{
    std::string lower (std::string s)
    {
        for (auto& c : s) c = char (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    bool has (const std::string& q, const char* word) { return q.find (word) != std::string::npos; }

    bool any (const std::string& q, std::initializer_list<const char*> words)
    {
        for (const char* w : words) if (has (q, w)) return true;
        return false;
    }

    std::string dB (float v)
    {
        char b[32];
        std::snprintf (b, sizeof (b), "%+.1f dB", double (v));
        return b;
    }

    std::string num (float v, const char* fmt = "%.1f")
    {
        char b[32];
        std::snprintf (b, sizeof (b), fmt, double (v));
        return b;
    }

    // What a group is called on screen (BGV, not VOCALS: see kMixBusNames).
    std::string groupName (MixBus b) { return mixBusName (b); }

    BuddyAction show (BuddyPage p, std::string label = "Show me where")
    {
        BuddyAction a;
        a.kind = BuddyActionKind::ShowPage;
        a.page = p;
        a.label = std::move (label);
        return a;
    }

    BuddyAction act (BuddyActionKind k, std::string label, int strip = -1, std::string request = {})
    {
        BuddyAction a;
        a.kind = k;
        a.label = std::move (label);
        a.strip = strip;
        a.request = std::move (request);
        return a;
    }

    // ---- Which channel a question is about ----

    // The words a person uses for a role, most specific first. A name typed on the Assign page
    // beats all of them (see findStrip).
    struct RoleWords { std::initializer_list<const char*> words; std::function<bool (ChannelRole)> is; };

    bool isFamily (ChannelRole r, std::initializer_list<RoleFamily> fams)
    {
        const auto f = roleFamily (r);
        for (auto x : fams) if (f == x) return true;
        return false;
    }

    bool isBandGroup (MixBus b) { return b == MixBus::Drums || b == MixBus::Bass || b == MixBus::Music; }
}

int MixBuddy::findStrip (const std::string& question, const BuddySnapshot& st)
{
    const auto q = lower (question);

    // "channel 14", "input 3", "ch 7": the number on the console.
    for (const char* key : { "channel ", "input ", "ch ", "ch." })
    {
        const auto at = q.find (key);
        if (at == std::string::npos) continue;
        size_t i = at + std::string (key).size();
        while (i < q.size() && q[i] == ' ') ++i;
        int n = 0, digits = 0;
        while (i < q.size() && std::isdigit (static_cast<unsigned char> (q[i]))) { n = n * 10 + (q[i] - '0'); ++i; ++digits; }
        if (digits == 0) continue;
        // The console's number is the device input the channel is patched to; a channel with
        // no input of that number is taken to be the n-th strip.
        for (size_t s = 0; s < st.strips.size(); ++s) if (st.strips[s].input == n) return int (s);
        if (n >= 1 && n <= int (st.strips.size())) return n - 1;
    }

    // The name somebody gave it on the Assign page: the longest match wins, so "Vox 2" is not
    // taken for "Vox".
    int best = -1;
    size_t bestLen = 0;
    for (size_t s = 0; s < st.strips.size(); ++s)
    {
        const auto name = lower (st.strips[s].name);
        if (name.size() >= 2 && q.find (name) != std::string::npos && name.size() > bestLen) { best = int (s); bestLen = name.size(); }
    }
    if (best >= 0) return best;

    // The role, in the words people use.
    const RoleWords table[] = {
        { { "pastor", "preacher", "sermon", "speech", "lectern", "pulpit", "talking", "announcement" },
          [] (ChannelRole r) { return r == ChannelRole::Speech; } },
        { { "crowd", "congregation", "audience", "room mic", "ambience" },
          [] (ChannelRole r) { return r == ChannelRole::CrowdMic || r == ChannelRole::AmbienceMic; } },
        { { "acoustic" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::AcousticGuitar }); } },
        { { "guitar" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::ElectricGuitar, RoleFamily::AcousticGuitar }); } },
        { { "lead vocal", "lead vox", "lead singer", "worship leader", "lead" },
          [] (ChannelRole r) { return r == ChannelRole::LeadVocal; } },
        { { "bgv", "backing", "background vocal", "harmony", "singers" },
          [] (ChannelRole r) { return r == ChannelRole::BackingVocal; } },
        { { "choir" }, [] (ChannelRole r) { return r == ChannelRole::Choir; } },
        { { "vocal", "voice", "singer", "vox", "mic " },
          [] (ChannelRole r) { return isFamily (r, { RoleFamily::LeadVocal, RoleFamily::BackingVocal, RoleFamily::Choir }); } },
        { { "kick", "bass drum" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Kick }); } },
        { { "snare" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Snare }); } },
        { { "tom" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Tom }); } },
        { { "hat", "hi-hat", "hihat" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::HiHat }); } },
        { { "overhead", "cymbal" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Overhead }); } },
        { { "bass" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::ElectricBass, RoleFamily::SynthBass }); } },
        { { "piano", "keys", "keyboard" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Piano, RoleFamily::ElectricPiano }); } },
        { { "organ" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Organ }); } },
        { { "synth", "pad" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Synth }); } },
        { { "sax", "horn" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Saxophone, RoleFamily::Brass }); } },
        { { "trumpet", "trombone", "brass" }, [] (ChannelRole r) { return isFamily (r, { RoleFamily::Brass }); } },
    };
    for (const auto& row : table)
    {
        bool said = false;
        for (const char* w : row.words) said = said || has (q, w);
        if (! said) continue;
        for (size_t s = 0; s < st.strips.size(); ++s)
            if (row.is (st.strips[s].role)) return int (s);
    }
    return -1;
}

namespace
{
    // ---------------------------------------------------------------------------------------
    // WHY CAN'T I HEAR IT: the signal path walked from the outside in, the first thing that
    // explains it said first - the way an engineer checks it with a finger on each stage.
    // ---------------------------------------------------------------------------------------
    BuddyAnswer whyQuiet (int s, const BuddySnapshot& st)
    {
        BuddyAnswer a;
        const auto& c = st.strips[size_t (s)];
        const auto& g = st.groups[size_t (c.bus)];
        const std::string who = c.name.empty() ? std::string ("That channel") : c.name;
        a.detail.push_back ("Input " + (c.input > 0 ? std::to_string (c.input) : std::string ("-")) + ": "
                            + (c.inputRmsDb > -100.0f ? num (c.inputRmsDb) + " dBFS" : std::string ("nothing")) + " arriving");
        a.detail.push_back ("Channel fader " + dB (c.faderDb) + (c.mute ? ", muted" : "") + "; digital gain " + dB (c.inputGainDb));
        a.detail.push_back (groupName (c.bus) + " group fader " + dB (g.faderDb) + (g.mute ? ", muted" : ""));
        if (c.compOn) a.detail.push_back ("Compressor taking " + num (c.compReductionDb) + " dB off right now");

        auto finish = [&] (std::string text)
        {
            a.text = std::move (text);
            a.actions.push_back (act (BuddyActionKind::OpenInspector, "Open " + who, s));
            if (st.monitorOutput) a.actions.push_back (act (BuddyActionKind::SoloStrip, "Solo " + who + " (your headphones only)", s));
            return a;
        };

        if (st.broadcastMute) return finish ("MUTE is on, so nothing is going out at all - it is not this channel. Press MUTE again to bring the broadcast back.");
        if (st.bypass) return finish ("BYPASS is on: every fader, gain and effect is out of the way and you are hearing the raw inputs. Turn BYPASS off to hear the mix.");
        if (c.inputRmsDb <= -70.0f)
            return finish (who + " has no signal arriving at input " + (c.input > 0 ? std::to_string (c.input) : std::string ("?"))
                           + ". That is before DINE does anything: check the cable, the microphone's switch or battery, phantom power, "
                             "and that the console is sending that channel. CHECK INPUTS shows every input at once.");
        if (c.mute) return finish (who + " is muted. Press its M to bring it back.");
        if (g.mute) return finish ("The " + groupName (c.bus) + " group is muted, and " + who + " is in it. Unmute the group.");
        if (st.soloInPlace && ! c.solo && ! g.solo)
            return finish ("Something else is soloed IN PLACE, which takes everything else out of the main mix - " + who
                           + " included. Clear the solo, or switch solo back to your headphones only.");
        if (c.faderDb <= -30.0f) return finish (who + "'s fader is right down at " + dB (c.faderDb) + ". Bring it up.");
        if (g.faderDb <= -20.0f)
            return finish ("The " + groupName (c.bus) + " group fader is down at " + dB (g.faderDb) + ", so everything in it is low, "
                           + who + " included. Bring the group up rather than every channel in it.");
        if (isBandGroup (c.bus) && st.speechPriority && st.speechDuckDb < -1.0f)
            return finish ("Speech priority is holding the band " + num (-st.speechDuckDb) + " dB down while somebody is speaking, and "
                           + who + " is in the band. That is what it is for; it comes back when the speaking stops.");
        if (c.inputRmsDb < -45.0f)
            return finish (who + " is arriving very quietly (" + num (c.inputRmsDb) + " dBFS). Turn the preamp up at the console "
                           "rather than the fader here: more fader on a weak signal is more noise, not more voice.");
        if (c.compOn && c.compReductionDb >= 9.0f)
        {
            a = finish (who + "'s compressor is taking " + num (c.compReductionDb) + " dB off right now. That much squeezes the life "
                        "out of it and can make it sit behind the band. Ease the compressor off, or let TUNE set it again.");
            a.actions.push_back (act (BuddyActionKind::RunTuneChannel, "TUNE " + who, s));
            return a;
        }
        a = finish ("Nothing on " + who + "'s path is switched off or pulled down: signal is arriving, the fader and the group are up. "
                    "When a source is there but still hard to hear, it is usually being covered by something in the same range - "
                    "keys or guitars under a voice, cymbals over it. TUNE MIX listens for exactly that and cuts the competitor "
                    "rather than just pushing this one up.");
        a.actions.push_back (act (BuddyActionKind::RunTuneMix, "Run TUNE MIX"));
        return a;
    }

    BuddyAnswer whyTooLoud (int s, const BuddySnapshot& st)
    {
        BuddyAnswer a;
        const auto& c = st.strips[size_t (s)];
        const std::string who = c.name.empty() ? std::string ("That channel") : c.name;
        a.detail.push_back ("Input: " + num (c.inputPeakDb) + " dBFS peak" + (c.clipped ? ", has clipped" : ""));
        a.detail.push_back ("Channel fader " + dB (c.faderDb) + "; digital gain " + dB (c.inputGainDb));
        if (c.clipped || c.inputPeakDb > -3.0f)
            a.text = who + " is arriving too hot" + std::string (c.clipped ? " and has clipped" : "") + ". That is at the console, "
                     "before DINE: turn the preamp down there. Nothing after it can take a clip back out.";
        else
            a.text = who + " arrives at a healthy level, so this is a balance question: bring its fader down a little, or run TUNE on "
                     "it so the level is set against the rest of the mix.";
        a.actions.push_back (act (BuddyActionKind::OpenInspector, "Open " + who, s));
        a.actions.push_back (act (BuddyActionKind::RunTuneChannel, "TUNE " + who, s));
        return a;
    }

    BuddyAnswer whyClipping (const BuddySnapshot& st)
    {
        BuddyAnswer a;
        std::vector<std::string> hot;
        for (const auto& c : st.strips) if (c.clipped) hot.push_back (c.name);
        a.detail.push_back ("Master: " + num (st.truePeakDb) + " dBTP against a " + num (st.ceilingDb) + " dBTP ceiling; limiter "
                            + (st.limiterOn ? "on, taking " + num (st.limiterReductionDb) + " dB" : std::string ("off")));
        if (! hot.empty())
        {
            std::string list;
            for (size_t i = 0; i < hot.size() && i < 4; ++i) list += (i ? ", " : "") + hot[i];
            a.text = "The clipping is at the inputs: " + list + (hot.size() > 4 ? " and more" : "")
                     + (hot.size() == 1 ? " has" : " have") + " been arriving over full scale. Turn the preamp down at the console. "
                     "A clip cannot be fixed after it happens, so no fader or limiter here will remove it.";
            a.actions.push_back (act (BuddyActionKind::OpenCheckInputs, "CHECK INPUTS"));
            return a;
        }
        if (! st.limiterOn)
        {
            a.text = "The master limiter is off, so nothing is holding the peaks under the ceiling. Turn on the Limiter stage "
                     "(LOUD) in the Inspector with MASTER picked, or let TUNE MIX set it.";
            a.actions.push_back (show (BuddyPage::Inspector));
            return a;
        }
        if (st.outputHeldBlocks > 0)
        {
            a.text = "The output guard has had to hold the device at full scale " + std::to_string (st.outputHeldBlocks) + " times. "
                     "Something after the master limiter is too loud - usually an output feed's own gain, or a group feed with "
                     "no limiter of its own. Check the Level column in the Outputs section of ROUTING.";
            a.actions.push_back (show (BuddyPage::Outputs));
            return a;
        }
        if (st.limiterReductionDb > 6.0f)
        {
            a.text = "Nothing is clipping, but the master limiter is taking " + num (st.limiterReductionDb) + " dB off - it is being "
                     "asked to make the mix loud rather than to catch the odd peak, and that flattens it. Bring the mix down into "
                     "the limiter (the group faders, or the master) until it is only catching peaks.";
            return a;
        }
        a.text = "The inputs are clean and the limiter is holding the master under its ceiling, so nothing is clipping right now. "
                 "If you saw a red light, it may have been a single input for a moment - CHECK INPUTS keeps the clip lights.";
        a.actions.push_back (act (BuddyActionKind::OpenCheckInputs, "CHECK INPUTS"));
        return a;
    }

    BuddyAnswer loudness (const BuddySnapshot& st)
    {
        BuddyAnswer a;
        a.detail.push_back ("Short-term " + num (st.shortLufs) + " LUFS, since the start " + num (st.integratedLufs)
                            + " LUFS, aiming at " + num (st.targetLufs) + " LUFS");
        a.detail.push_back ("True peak " + num (st.truePeakDb) + " dBTP, ceiling " + num (st.ceilingDb) + " dBTP");
        const float delta = st.integratedLufs > -100.0f ? st.integratedLufs - st.targetLufs : 0.0f;
        std::string where = st.integratedLufs <= -100.0f ? std::string ("DINE has not measured enough of the mix yet to say. ")
                          : std::fabs (delta) <= 1.0f ? "The mix is on its target. "
                          : delta < 0.0f ? "The mix is " + num (-delta) + " LU under its target. "
                                         : "The mix is " + num (delta) + " LU over its target. ";
        a.text = where + "Streaming platforms play everything back at about the same loudness, so a stream that sounds quieter than "
                 "others is usually one aimed at a lower target, not one that needs more master fader. The target is under \"How "
                 "loud it should end up\" on Purpose and sound, and in Mix > Loudness Target: \"YouTube / Facebook / Spotify\" "
                 "aims at -14 LUFS. Louder than the platform's target only means it is turned down again - with the peaks already "
                 "squashed. On the Tune page, \"Raise loudness to target\" brings the mix up to it safely.";
        a.actions.push_back (show (BuddyPage::Purpose, "Open Purpose and sound"));
        return a;
    }

    // ---------------------------------------------------------------------------------------
    // A SOUND THAT IS WRONG: what usually causes it and where DINE would fix it. Mix Buddy
    // explains; the change itself is TUNE's, or the engineer's.
    // ---------------------------------------------------------------------------------------
    BuddyAnswer tone (const std::string& q, int s, const BuddySnapshot& st)
    {
        BuddyAnswer a;
        const bool haveStrip = s >= 0;
        const std::string who = haveStrip ? st.strips[size_t (s)].name : std::string ("it");
        std::string why, words;
        if (any (q, { "muddy", "mud", "boomy", "boom", "boxy", "woolly", "wooly" }))
        {
            why = "Mud is too much in the low-mids, roughly 200 to 500 Hz, where every instrument and voice overlaps. The fix is "
                  "usually a cut there - on " + who + ", and often on the keys or guitars sharing that range - plus a high-pass "
                  "below what the source really plays. Boosting the top end instead only makes it harsh and muddy.";
            words = "less boom";
        }
        else if (any (q, { "harsh", "piercing", "shrill", "brittle", "cymbal", "tinny" }))
        {
            why = "Harshness lives around 2 to 5 kHz, and cymbals add their own above that. The fix is a narrow cut where it bites "
                  "(on the overheads or hi-hat for cymbals), not less top end everywhere - that just makes the whole mix dull.";
            words = "less harsh";
        }
        else if (any (q, { "sibilan", "essy", "ess ", "s sounds", "hissy" }))
        {
            why = "Sharp S sounds are the de-esser's job: it turns down only the 5 to 9 kHz burst of an S, so the voice stays bright "
                  "between them. An EQ cut there would dull every word.";
            words = "less harsh";
        }
        else if (any (q, { "thin", "weak", "small" }))
        {
            why = "Thin is usually a high-pass set too high, or a microphone too far from the source. Check the high-pass first; if "
                  "the singer has stepped back, no EQ will bring the body back - ask them to come in to the microphone.";
            words = "warmer";
        }
        else if (any (q, { "clear", "clarity", "intelligib", "understand", "words" }))
        {
            const bool speech = haveStrip && st.strips[size_t (s)].role == ChannelRole::Speech;
            why = speech
                ? "Speech is understood through its consonants, around 2 to 4 kHz, not its low end. The usual fixes, in order: the "
                  "microphone closer to the mouth (nothing after it can make up for distance); a high-pass around 100 Hz to take "
                  "out rumble and chest boom; a cut where it sounds boxy; steady compression of a few dB, not a squash - too much "
                  "makes a voice flat and tiring; and the de-esser (SMOOTH) for sharp S sounds. Speech priority (Mix menu) steps the "
                  "band back while somebody is talking."
                : "Clarity is usually space, not level: something in the same range - keys or guitars under a voice - is covering "
                  "it. A cut on the competitor around 2 to 4 kHz, and less low-mid mud on both, does more than pushing this one up.";
            words = speech ? "clearer" : "brighter";
        }
        else if (any (q, { "dull", "dark", "muffled" }))
        {
            why = "Dull is a lack of presence, around 3 to 6 kHz, or something in front of it covering that range. Try a gentle lift "
                  "there, or cut the competitor.";
            words = "brighter";
        }
        else
        {
            why = "That is a question about the tone.";
            words = "";
        }
        a.text = why + " TUNE listens for exactly this and sets it against the rest of the mix; you can also see and change every "
                 "stage in the Inspector.";
        if (haveStrip)
        {
            a.actions.push_back (act (BuddyActionKind::OpenInspector, "Open " + who, s));
            a.actions.push_back (act (BuddyActionKind::RunTuneChannel, "TUNE " + who, s));
            if (! words.empty() && st.heard)
                a.actions.push_back (act (BuddyActionKind::AskForChange, "Propose \"" + who + " " + words + "\"", s, who + " " + words));
        }
        else a.actions.push_back (act (BuddyActionKind::RunTuneMix, "Run TUNE MIX"));
        return a;
    }
}

namespace
{
    // ---------------------------------------------------------------------------------------
    // HOW DO I: DINE's own controls, by the names on the screen. Every label here was checked
    // against app/ui; a control that does not exist is never named, and where DINE does not
    // do something (a new bus, a shortcut for mix undo) the answer says so.
    // ---------------------------------------------------------------------------------------
    struct HowTo
    {
        std::initializer_list<const char*> words;      // any of these
        std::initializer_list<const char*> also;       // ... and, when not empty, one of these too
        const char* text;
        std::vector<BuddyAction> (*actions)();
    };

    std::vector<BuddyAction> none() { return {}; }

    const HowTo kHowTo[] = {
        { { "tune mix", "autopilot", "mix buddy" }, { "difference", "vs", "versus", "which", "what is", "what's", "compare" },
          "TUNE MIX improves the mix: it listens to the band for thirty seconds, proposes every level, tone and effect, and "
          "nothing lands until you press KEEP. Autopilot holds a mix you are happy with while you are busy: it learns it for a "
          "few seconds, then makes small, slow moves on the group faders only - never a channel, never EQ, never more than "
          "4 dB - and a group you touch is yours again. Mix Buddy is this panel: it explains and shows you where, and never "
          "changes the mix by itself.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Tune, "Open TUNE") }; } },
        { { "autopilot" }, {},
          "Turn Autopilot on from AUTOPILOT in the toolbar, or Mix > \"Autopilot: hold this mix\", while the band is playing "
          "the mix you want kept. It listens for a few seconds, then holds each group where it sits against the rest, by the "
          "smallest move, group faders only and never more than 4 dB. It holds still when the arrangement changes (the band "
          "stops for the sermon, a group is muted). Move a group fader yourself and that group is yours again; one press on "
          "AUTOPILOT turns it off, and every move it made is in the Mix history.",
          none },
        { { "save" }, {},
          "File > Save (Cmd-S), or Save As... (Cmd-Shift-S) for a copy under a new name. DINE also writes an autosave beside "
          "the session a couple of seconds after every change, and if it is ever closed without saying goodbye, the next "
          "launch asks \"Recover session?\" with the unsaved work.",
          none },
        { { "yesterday", "last week", "last sunday", "previous session", "open a session", "open session", "old session", "restore", "recover" }, {},
          "A whole session: File > Open Session... (Cmd-O) takes you to Sessions, with search and \"Recent\". An earlier mix "
          "inside this session: the Mix history lists every place to go back to - press \"Restore\" on one. After a crash, "
          "DINE offers the unsaved work itself when it opens: \"Recover\", \"Open last saved\" or \"Keep both\".",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Sessions, "Open Sessions"), act (BuddyActionKind::OpenHistory, "Open Mix history") }; } },
        { { "undo", "go back", "take back" }, {},
          "Mix > Undo takes the mix back one change, and the TUNE page has \"Undo mix\" and \"Redo mix\". Cmd-Z undoes edits on "
          "the timeline only, not the mix. For anything further back, the Mix history keeps every tune, scene and Autopilot "
          "move with \"Restore\".",
          [] { return std::vector<BuddyAction> { act (BuddyActionKind::OpenHistory, "Open Mix history") }; } },
        { { "solo" }, {},
          "Press S on a channel or a group on the MIXER, or on a group tile on LIVE. Solo goes to your headphones only - never "
          "to the room or the stream - as long as LIVE is set to MONITOR SOLO. SOLO IN PLACE is the other setting and it does "
          "change the main mix; keep it for rehearsal. Pick where you listen with \"Choose headphones\" on LIVE. What is soloed "
          "shows along the top of every workspace; its cross clears it.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Live, "Open LIVE") }; } },
        { { "bus", "group", "route", "routing" }, {},
          "A channel's group comes from what it is: set \"What it is\" for each input on the Inputs page, and the Group column "
          "follows - a backing singer goes to BGV, a pastor's microphone to SPEECH, a crowd microphone to AMBIENCE. The groups "
          "are fixed: DRUMS, BASS, MUSIC, BGV, LEAD, SPEECH and AMBIENCE, into MASTER. DINE does not make new buses, and a "
          "channel cannot be sent to a group its role does not belong to. The group faders are on the MIXER beside the master "
          "and on LIVE.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Assign, "Open Inputs"), show (BuddyPage::Mixer, "Open MIXER") }; } },
        { { "sample", "trigger", "replace" }, {},
          "Drum sounds live in the Inspector's Sample stage, on kick, snare and tom channels. Pick a sound, set Blend (how much "
          "of it against the microphone), and press HEAR IT to audition it. \"Import a sound...\" brings in your own. If it does "
          "not fire, the hits are under Sensitivity - lower it, and check the drum microphone is arriving healthily (CHECK "
          "INPUTS). If it fires twice, raise Mask; if other drums set it off, raise Sensitivity or narrow Listen above / Listen "
          "below. Align lines the sound up with the microphone.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Inspector, "Open the Inspector"), act (BuddyActionKind::OpenCheckInputs, "CHECK INPUTS") }; } },
        { { "interface", "audio device", "device", "sound card", "console" }, {},
          "ROUTING > Audio device lists the devices on this Mac: pick the one your console connects through, and \"Rescan "
          "devices\" if it has only just been plugged in. The sample rate and buffer are shown there too. If the device is "
          "pulled out while you are live, DINE says so in the status foot and reopens it by itself when it comes back.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Device, "Open Audio device") }; } },
        { { "record" }, {},
          "Set each track to record with its R (Track > \"Set Every Track to Record\" does all of them), then press record on the "
          "transport, or R. Every input is kept on its own track in the session's Audio Files folder, and a take survives a "
          "crash. The Disk cell in the status foot shows how much time is left, and turns to a warning under fifteen minutes.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Tracks, "Open TRACKS") }; } },
        { { "crowd", "congregation", "audience", "room mic" }, {},
          "Set each crowd microphone's role to Crowd mic on the Inputs page, so it goes to AMBIENCE. TUNE never gates it and "
          "keeps it wide, with a high-pass to keep the stage's low end out of it. Its job is to make the stream sound like a "
          "service: bring the AMBIENCE group up for congregational singing and applause, and down for the sermon - DINE does "
          "not ride it for you, and speech priority leaves it alone.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Assign, "Open Inputs") }; } },
        { { "speech priority", "duck" }, {},
          "Mix > \"Speech Priority: the band steps back while somebody speaks\". While the SPEECH group is open it takes DRUMS, "
          "BASS and MUSIC down a few dB in the broadcast, and brings them back when the speaking stops. It never touches the "
          "voices, the room, the effects or your headphones. It is off until you turn it on.",
          none },
        { { "live safe", "lock" }, {},
          "LIVE SAFE, in the toolbar, locks the things that could go wrong by accident while you are live - routing, a full "
          "re-tune, big jumps on a fader - and keeps every move a small one. Turn it on before you go live; LIVE shows "
          "\"What's locked\".",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Live, "Open LIVE") }; } },
        { { "scene" }, {},
          "Scenes are on LIVE: BAND, SPEECH, WORSHIP and CUSTOM. Set the mix, press \"Keep\" to store it in the one selected, and "
          "press a scene to bring it back. A scene recall is one change, so Mix > Undo takes it back.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Live, "Open LIVE") }; } },
        { { "sunday", "prepare", "before the service", "checklist", "get ready", "soundcheck", "line check" }, {},
          "Before you go live: open the session (Sessions); check the console is the audio device (ROUTING > Audio device); run "
          "CHECK INPUTS while each source plays and fix anything SILENT, LOW or CLIP at the console; run TUNE MIX during the "
          "rehearsal and KEEP what you like; keep your mixes as scenes on LIVE; set every track to record and check the Disk "
          "time; choose your headphones on LIVE; then turn LIVE SAFE on. Autopilot and speech priority are there if you want "
          "them.",
          [] { return std::vector<BuddyAction> { act (BuddyActionKind::OpenCheckInputs, "CHECK INPUTS"), show (BuddyPage::Tune, "Open TUNE") }; } },
        { { "warning", "dropped", "dropout", "device lost", "red", "status" }, {},
          "The status foot along the bottom says what is wrong in one word each. \"Dropped\" counts audio buffers the Mac did "
          "not get to in time - a glitch; close other apps or raise the buffer on Audio device. \"Device lost\" means the "
          "interface went away; DINE reopens it when it returns. \"Disk\" turns to a warning under fifteen minutes of recording "
          "room. In CHECK INPUTS, SILENT, LOW, HOT and CLIP describe the signal arriving from the console.",
          [] { return std::vector<BuddyAction> { act (BuddyActionKind::OpenCheckInputs, "CHECK INPUTS") }; } },
        { { "compressor", "compression", "steady" }, {},
          "The compressor (STEADY) turns the loud moments down so the level stays even. The threshold is where it starts; the "
          "ratio is how hard it pushes back; attack and release are how quickly it grabs and lets go; make-up puts the level "
          "back. A few dB of reduction is a steadier voice; more than about 8 dB all the time is a squashed, lifeless one. The "
          "Inspector shows how much it is taking off.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Inspector, "Open the Inspector") }; } },
        { { "eq", "equaliser", "equalizer" }, {},
          "Each channel has two EQs in the Inspector: Corrective EQ takes out what is wrong (a boom, a ring), and Tone EQ "
          "(WARMTH and CLARITY) shapes what is left. Cutting what is in the way usually works better than boosting what you "
          "want: a vocal buried in the keys is often fixed on the keys.",
          [] { return std::vector<BuddyAction> { show (BuddyPage::Inspector, "Open the Inspector") }; } },
        { { "tutorial", "getting started", "learn", "teach", "first time", "new to" }, {},
          "Help > \"Getting started\" walks through DINE one workspace at a time, and Help > \"Show the guides again\" brings back "
          "the card on each workspace. The short version: Inputs (what each channel is), CHECK INPUTS, TUNE MIX, KEEP, then "
          "LIVE.",
          none },
        { { "shortcut", "keyboard", "hotkey" }, {},
          "Space plays and stops, R records, B is BYPASS, T tunes the selected channel, Cmd-1 to Cmd-5 are TRACKS, MIXER, TUNE, "
          "LIVE and the Inspector, Cmd-S saves, Esc closes a sheet. Mix undo, the Mix history and CHECK INPUTS have no "
          "shortcut; they are in the Mix and View menus.",
          none },
        { { "input", "check input" }, {},
          "View > \"Check Inputs...\" (or \"Check inputs\" on TUNE) shows every input's level now, its peak and a word: SILENT, "
          "LOW, HOT, CLIP or OK. It only reads - nothing there changes the mix. Fix LOW and CLIP at the console's preamp, not "
          "with a fader here.",
          [] { return std::vector<BuddyAction> { act (BuddyActionKind::OpenCheckInputs, "CHECK INPUTS") }; } },
    };

    bool matches (const std::string& q, const HowTo& h)
    {
        bool word = false;
        for (const char* w : h.words) word = word || has (q, w);
        if (! word) return false;
        if (h.also.size() == 0) return true;
        for (const char* w : h.also) if (has (q, w)) return true;
        return false;
    }

    bool asksWhy (const std::string& q)
    {
        return any (q, { "why", "can't hear", "cant hear", "cannot hear", "can not hear", "not hearing", "no sound", "missing",
                         "buried", "disappear", "where is", "where's", "what happened", "gone", "not coming" });
    }
    bool saysQuiet (const std::string& q)
    {
        return any (q, { "quiet", "can't hear", "cant hear", "cannot hear", "can not hear", "not hearing", "no sound", "missing",
                         "buried", "low", "soft", "disappear", "gone", "lost", "not coming" });
    }
    bool saysLoud (const std::string& q) { return any (q, { "too loud", "loud", "overpowering", "on top", "blasting", "too much" }); }
    bool saysTone (const std::string& q)
    {
        return any (q, { "muddy", "mud", "boomy", "boom", "boxy", "woolly", "wooly", "harsh", "piercing", "shrill", "brittle",
                         "tinny", "sibilan", "essy", "hissy", "thin", "dull", "dark", "muffled", "clear", "clarity" });
    }
    bool asksToChange (const std::string& q)
    {
        // An instruction rather than a question: a verb that moves something, and no "why" or "how".
        const bool verb = any (q, { "turn ", "bring ", "make ", "boost", "cut ", "add ", "more ", "less ", "louder", "quieter",
                                    "raise ", "lower ", "push ", "pull ", "take out", "take some" });
        const bool question = any (q, { "why", "how ", "what", "where", "which", "should", "can i", "?" });
        return verb && ! question;
    }
}

std::vector<std::string> MixBuddy::examples()
{
    return {
        "Why can't I hear channel 14?",
        "Why is my lead vocal quiet?",
        "Why is my mix clipping?",
        "Why does my stream sound quieter than others?",
        "What's the difference between TUNE MIX and Autopilot?",
        "How do I use sample replacement?",
        "How should I prepare for Sunday?",
    };
}

BuddyAnswer MixBuddy::answer (const std::string& question, const BuddySnapshot& st)
{
    const auto q = lower (question);
    const int strip = findStrip (question, st);

    // A mix that is not running is the first thing to know, whatever was asked about it.
    if (! st.running && (asksWhy (q) || saysQuiet (q)))
    {
        BuddyAnswer a;
        a.text = "No mix is running yet: DINE needs its inputs assigned and an audio device open before there is anything to "
                 "hear. Start on the Inputs page, then ROUTING > Audio device.";
        a.actions.push_back (show (BuddyPage::Assign, "Open Inputs"));
        return a;
    }

    // A CHANGE, ASKED FOR. Mix Buddy does not make it; it says where, and offers to have TUNE
    // LIVE MIX propose it - which the engineer then hears on BEFORE / AFTER and decides.
    if (asksToChange (q))
    {
        BuddyAnswer a;
        a.text = "Mix Buddy does not change the mix by itself. You can make that change on the MIXER or in the Inspector";
        if (st.heard && ! st.liveSafe)
        {
            a.text += ", or have TUNE LIVE MIX propose it from what it heard: it arrives on BEFORE / AFTER and nothing is kept until "
                      "you press KEEP.";
            a.actions.push_back (act (BuddyActionKind::AskForChange, "Propose it", strip, question));
        }
        else if (st.liveSafe) a.text += ". LIVE SAFE is on, so nothing is proposed from here while you are live.";
        else a.text += ". Once TUNE MIX has heard the band, Mix Buddy can also have it proposed for you.";
        if (strip >= 0) a.actions.push_back (act (BuddyActionKind::OpenInspector, "Open " + st.strips[size_t (strip)].name, strip));
        else a.actions.push_back (show (BuddyPage::Mixer, "Open MIXER"));
        return a;
    }

    // THE MIX, AS IT IS: these are answered from the state, before any how-to.
    if (any (q, { "clip", "distort", "overload", "red light", "crackl" })) return whyClipping (st);
    if (any (q, { "lufs", "loudness", "stream", "youtube", "facebook", "loud enough", "quieter than", "louder than" })
        && ! (strip >= 0 && ! any (q, { "stream", "lufs", "loudness" })))
        return loudness (st);
    if (strip >= 0 && saysTone (q)) return tone (q, strip, st);
    if (strip >= 0 && saysLoud (q) && ! saysQuiet (q)) return whyTooLoud (strip, st);
    if (strip >= 0 && (saysQuiet (q) || asksWhy (q))) return whyQuiet (strip, st);
    if (strip < 0 && saysTone (q) && ! any (q, { "how do", "what is", "what's" })) return tone (q, -1, st);
    if (strip < 0 && (asksWhy (q) || saysQuiet (q)) && any (q, { "hear", "channel", "input", "mic", "vocal", "sound" }))
    {
        BuddyAnswer a;
        a.text = "Which one? Say its name as it is on the Inputs page, or its number (\"channel 14\"), and Mix Buddy will walk its "
                 "path from the input to the master. CHECK INPUTS shows every input arriving at once.";
        a.actions.push_back (act (BuddyActionKind::OpenCheckInputs, "CHECK INPUTS"));
        return a;
    }

    // HOW DO I.
    for (const auto& h : kHowTo)
        if (matches (q, h))
        {
            BuddyAnswer a;
            a.text = h.text;
            a.actions = h.actions();
            // Where the question named a channel, the next step is that channel.
            if (strip >= 0) a.actions.insert (a.actions.begin(), act (BuddyActionKind::OpenInspector, "Open " + st.strips[size_t (strip)].name, strip));
            return a;
        }

    BuddyAnswer a;
    a.notUnderstood = true;
    a.text = "Mix Buddy did not follow that. Ask how to do something in DINE (\"how do I solo a group?\"), or why something "
             "sounds the way it does (\"why is the pastor's microphone quiet?\").";
    return a;
}

} // namespace livemix
