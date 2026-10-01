#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>
#include "Mix/MixEngine.h"
#include "Mix/MixCapture.h"
#include "Mix/MixPlanner.h"
#include "Mix/MeasuredMix.h"
#include "Mix/MixMacros.h"
#include "Mix/OutputFeeds.h"
#include "Mix/LiveSafe.h"
#include "Mix/MonitorBus.h"
#include "Mix/Autopilot.h"
#include "MixAI/TuneLiveCoordinator.h"
#include "MixAI/MixBuddy.h"
#include "MixHistory.h"

namespace livemix
{

// The message-thread owner of one DLIVE mix: the session (who is what), the engine,
// the listen (TUNE MIX), the plan and its BEFORE / AFTER preview, the five macros and
// Advanced edits. The UI talks only to this class and reads only plain data from it;
// the audio thread touches nothing here except process(). No JUCE.
class MixController
{
public:
    enum class Stage : int { Setup = 0, Ready, Listening, Planning, Preview, Mixed };
    enum class Compare : int { After = 0, Before };

    MixController();
    ~MixController();

    // ---- Session (Setup) ----
    const MixSession& getSession() const noexcept { return session; }
    void setSession (const MixSession& s);            // audio must be stopped or reconfigure() called afterwards
    void setSessionName (const std::string& name) { session.name = name; touch(); }
    // Correcting what an input is called. A name is a label, not routing, so this does not
    // rebuild the graph: the kept mix, the plan, the listen and the timeline's clips all
    // survive it, and TRACKS, MIXER and the Inspector are renamed together because the
    // graph's copy is set here too. Changing which source an input *is* goes through
    // setSession + prepare(), because that changes the bus and the baseline chain.
    void setInputName (int strip, const std::string& name);
    // What the source is drawn as. Empty goes back to the role's own icon. Like a rename
    // this is a label: no rebuild, and nothing about the mix or the plan changes.
    void setInputIcon (int strip, const std::string& icon);
    // THE FOCAL SOURCE: the one the mix is built around. Unpinned, DLIVE picks the lead
    // microphone somebody is really singing into; pinning settles it when there is more than
    // one and the loudest is not the one. It changes nothing you can hear until the next TUNE.
    void setFocusInput (int strip);
    // SPEECH PRIORITY: while the speech group carries somebody speaking, the band steps back
    // into the broadcast. Off by default; the engineer's listen never ducks, so what they hear
    // is always what is really there. How far and how slowly is the profile's (MixProfile).
    void setSpeechPriority (bool on);
    bool getSpeechPriority() const noexcept { return session.speechPriority; }
    float getSpeechDuckDb() const noexcept { return engine.getSpeechDuckDb(); }
    int getFocusInput() const noexcept { return session.focusInput(); }
    void setPurpose (MixPurpose p);
    void setProfile (StyleProfileId p);
    // How loud the finished mix should be. This is the number the whole gain structure is
    // fitted against, not a gain added at the end: changing it changes nothing until the next
    // TUNE MIX, and then every fader, bus and the master's own compressor are fitted to it.
    void setDelivery (DeliveryLoudness d);
    DeliveryLoudness getDelivery() const noexcept { return session.delivery; }

    // Who the finished mix is for. Applied when the parameters are composed, like a macro:
    // instant, reversible, never written into the kept mix, saved with the session.
    void setVoicing (MasterVoicing v);
    MasterVoicing getVoicing() const noexcept { return session.voicing; }

    // Raise (or trim) the master to the delivery target from where it is actually reading,
    // now, without waiting for a TUNE MIX - and without clipping: the move goes into the
    // master's output trim ahead of the limiter, which is switched on at the delivery
    // ceiling, so the true peak can never pass it. The move is bounded (MixProfile::
    // loudnessLift), LIVE SAFE limits it the way it limits the master fader, and it is an
    // ordinary edit on the kept mix: UNDO takes it back and the next TUNE MIX refits it.
    // Returns the sentence that says what happened, or why nothing did.
    std::string raiseLoudnessToTarget();
    // What one press would do right now, for the button and the sentence beside it.
    struct LoudnessMove { bool possible = false; float fromLufs = -120.0f, targetLufs = -23.0f, moveDb = 0.0f; std::string why; };
    LoudnessMove previewLoudnessMove() const;

    // ---- THE SESSION'S OWN STATE: rebuilt from the assignments, with no device in sight ----
    //
    // rebuild() is what makes a DLIVE session a document rather than a side effect of an open
    // audio device. It builds the routing graph from the assignments and carries the kept mix
    // across it - every strip that survived keeps its chain, its gain, its fader and its sends,
    // found by the same identity the timeline uses for its clips - and it needs neither a
    // device nor a sample rate to do it (RoutingGraph::build and startingPoint() take neither).
    //
    // This used to live half in prepare() and half in app/Main.cpp, as a `pending` Document
    // snapshotted before a device change and pushed back afterwards. That is why a session
    // opened without its console had no mix, and why saving one wiped what was on disk.
    // docs/SESSION-STATE.md has the whole story.
    //
    // setSession() calls this, so the mix always matches the assignments. It does not publish:
    // the audio thread keeps running the graph it was prepared with until prepare() says
    // otherwise, exactly as before.
    void rebuild();
    bool isBuilt() const noexcept { return built; }

    // Put the document back to blank, then load a different one on top. A session opened from
    // a file, or a new empty one, carries nothing across from whatever was loaded before: a
    // different session is a different mix, and "carry the mix onto the new assignments" is
    // exactly the wrong thing to do when the assignments belong to somebody else's Sunday.
    // applySession() in SessionState.h is the only caller, and the only way in.
    void resetDocument();

    // ---- Engine lifecycle (AudioHost calls these with the device stopped) ----
    // Builds the *audio graph* for the session at this rate, and publishes. It never decides
    // whether the session's state exists - rebuild() owns that - so opening a device no longer
    // resets the mix, and closing one no longer loses it.
    void prepare (double sampleRate, int maxBlockSize);
    // Does the engine have a graph it can run? This is what the audio callback asks before it
    // does anything, so it must mean exactly that - and in particular it must not go false
    // just because the *document* changed, or editing the assignments would silence the room.
    bool isPrepared() const noexcept { return prepared; }
    // The document has been changed and the graph has not caught up yet. The mix keeps
    // playing the graph it has; a host calls prepare() when the user is ready for the change.
    bool needsReconfigure() const noexcept { return graphStale; }
    // The session `graph` and `kept` were built for. setSession() replaces the document and
    // rebuilds in one breath, so these agree again immediately; the name is from when "prepared"
    // could only mean "the session a device happened to be opened for".
    const MixSession& getPreparedSession() const noexcept { return builtSession; }
    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    const MixEngine& getEngine() const noexcept { return engine; }
    // The session's graph, which is the one the UI and the planner mean. The engine holds an
    // identical one built from the same pure function; it is a step behind only between an
    // assignment change and the next prepare(), and MixEngine::getStrip() is safe across that.
    const RoutingGraph& getGraph() const noexcept { return graph; }
    // Sample replacement: the sounds the drum strips can play (app/native/SampleLibrary owns
    // them for the app's lifetime). Message thread; the engine reads a pointer, never a copy.
    // Publishing a new table (the engineer imported a sound) re-applies the mix so every
    // drum strip picks its bank out of the new table on the next block.
    void setSampleBanks (const SampleBankTable* table, bool publishNow = true);
    // The library moved under names the session stored (an import re-sorted the slots, a session
    // opened on another Mac): each strip is pointed at where its sound now is. Not an edit - no
    // history, no UNDO step, no Autopilot re-learn - and published once with the table, so no
    // block ever plays a slot from the old table against the new one.
    void repointSamples (const std::vector<std::pair<int, ChannelParameters>>& changes);
    const SampleBankTable* getSampleBanks() const noexcept { return engine.getSampleBanks(); }
    // HEAR IT: play the strip's chosen sound once, where solo goes, at the level the stage would
    // play it. Monitoring, never mix: the broadcast does not hear it and nothing is kept. False,
    // with the sentence in onMessage, when solo has nowhere to go or the strip has no sound.
    bool auditionSample (int strip);

    // Audio thread.
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept
    {
        engine.process (inputs, numInputs, outputs, numOutputs, numSamples);
    }

    // ---- TUNE MIX ----
    struct ListenSettings { float seconds = 30.0f; float triggerDb = -45.0f; float maxWaitSeconds = 30.0f; };
    void startTuneMix (const ListenSettings& s);
    void startTuneMix() { startTuneMix (ListenSettings {}); }

    // ---- TUNE CHANNEL: one source, on click ----
    // The same listen and the same planner as TUNE MIX - a channel is never tuned by rules
    // of its own, and it is still decided in mix context - narrowed to one strip when the
    // plan is made (MixPlanner::channelOnly): only that strip's chain, input gain, fader
    // and sends move, the buses and the master stay where they are, and BEFORE / AFTER,
    // KEEP and REVERT work exactly as they do for a mix. The listen waits for that channel
    // rather than for the band, and it is shorter: one source needs less to be measured.
    void startTuneChannel (int strip, const ListenSettings& s);
    void startTuneChannel (int strip) { startTuneChannel (strip, channelListen()); }
    static ListenSettings channelListen() { return { 12.0f, -45.0f, 20.0f }; }
    int getTuningStrip() const noexcept { return tuningStrip; }      // the strip this listen / plan is about; -1 = the whole mix
    bool isTuningChannel() const noexcept { return tuningStrip >= 0; }
    std::string getTuningName() const;                               // that channel's (or group's) name, empty for a mix

    // ---- TUNE <GROUP>: TUNE DRUMS, TUNE VOCALS, TUNE SPEECH ... ----
    // The same listen and the same planner again, narrowed to one group bus when the plan is
    // made (MixPlanner::busOnly): every strip on the group moves - chain, input gain, fader,
    // sends - and so does the group's own chain; the other groups, their strips and the master
    // stay where they are. So the band can be tuned during the song and the pastor during the
    // sermon, and neither touches the other. The listen waits for anything on the group.
    void startTuneBus (MixBus bus, const ListenSettings& s);
    void startTuneBus (MixBus bus) { startTuneBus (bus, busListen()); }
    static ListenSettings busListen() { return { 20.0f, -45.0f, 25.0f }; }
    bool isTuningBus() const noexcept { return tuningBus >= 0; }
    MixBus getTuningBus() const noexcept { return tuningBus >= 0 ? MixBus (tuningBus) : MixBus::Count; }

    // ---- TUNE <these channels>: the ones somebody picked, and nothing else ----
    // The third scope, beside the whole mix and one group. Again the same listen and the same
    // planner - every input is measured, so the choice is still made in mix context - narrowed
    // to this set of strips when the plan is made, through the same PlanSelection that KEEP
    // SOME uses. The buses, the master and every strip not in the set stay where they are.
    // The listen waits for any of them: "the three backing vocals" starts when one of them
    // sings, not when the drummer moves.
    void startTuneStrips (const std::vector<int>& strips, const ListenSettings& s);
    void startTuneStrips (const std::vector<int>& strips) { startTuneStrips (strips, busListen()); }
    const std::vector<int>& getTuningStrips() const noexcept { return tuningStrips; }
    bool isTuningStrips() const noexcept { return ! tuningStrips.empty(); }

    // A channel, a group or a set of channels: the listen and the plan are about part of the
    // mix, not all of it.
    bool isTuningPart() const noexcept { return tuningStrip >= 0 || tuningBus >= 0 || ! tuningStrips.empty(); }

    // WHAT THE LAST TUNE RAN ON, in the words the card says it in: "the whole mix", "DRUMS",
    // "Lead Vocal", "3 channels". Kept after the listen ends, because the Tune card has to be
    // able to say what it is a card about long after the scope was cleared.
    const std::string& getLastTuneScope() const noexcept { return lastScope; }

    // ---- TUNE LIVE MIX: the AI mix engineer ----
    // The same listen, the same deterministic plan and the same BEFORE / AFTER as TUNE MIX,
    // with a reasoning pass on top: DLIVE listens, builds the professional mix it always
    // builds, asks a mix engineer what this band still needs, resolves that into changes it
    // can actually make, checks every one of them, applies them, listens again and makes one
    // conservative correction. The audio path is untouched by any of it - the reasoning runs
    // on a worker inside TuneLiveCoordinator, and a plan only ever reaches the engine as one
    // whole MixParameters snapshot through the same publish() every fader move uses. If the
    // provider is unreachable, slow, or answers with nonsense, the deterministic mix is what
    // you are left with and the audio never stops.
    struct LiveTuneSettings
    {
        ListenSettings initial { 30.0f, -45.0f, 30.0f };   // listen to the band
        ListenSettings verify { 15.0f, -45.0f, 20.0f };    // listen again to what was applied
        bool refinementPass = true;                        // one correction, never an open loop
        std::string userRequest;                           // "make the drums bigger" - usually empty
        // Repeatability. 0 is the mix the TUNE LIVE MIX button always asks for: the same
        // band, the same listen and the same settings land on the same mix. TRY ANOTHER MIX
        // asks for 1, 2, 3 ... - a different reading of the same measurements, requested by
        // name instead of arrived at by surprise.
        int variation = 0;
        // Work from the listen DLIVE already has rather than asking the band to play again.
        // This is what makes TRY ANOTHER MIX instant, and it is also what makes the comparison
        // fair: two readings of the *same* performance, not of two different ones.
        bool reuseListen = false;
    };
    void startTuneLiveMix (const LiveTuneSettings& s);
    void startTuneLiveMix() { startTuneLiveMix (LiveTuneSettings {}); }
    // A different professional reading of the listen DLIVE already has. No new listen, no
    // waiting for the band: the measurements are the same, the interpretation is not.
    void tryAnotherMix();
    int getMixVariation() const noexcept { return liveSettings.variation; }
    // Can another reading be asked for? Only once a live run has heard something.
    bool canTryAnotherMix() const noexcept { return listened && lastCapture.valid && ! liveRun && prepared; }
    // Off by default is the offline engineer, which needs no network and no configuration.
    // Passing nullptr goes back to it.
    void setReasoningProvider (std::shared_ptr<MixReasoningProvider>);
    const TuneLiveCoordinator& getTuneLive() const noexcept { return tuneLive; }
    bool isTuningLive() const noexcept { return liveRun; }
    // What the sheet reads while a run is going: the state machine's own words.
    std::string getTuneLiveStatus() const { return tuneLive.getStatusText(); }

    // ---- REFERENCE MIX: "make it sound like this" ----
    // A finished recording the mix is aimed at. It is a target, not a move: setting one
    // changes nothing you can hear until the next TUNE MIX, RE-TUNE or MATCH TO REFERENCE.
    // It is stored with the session, already measured, so reopening a service never has to
    // find the file again - and never depends on it still being there.
    void setReference (const ReferenceProfile&);
    void clearReference();
    const ReferenceProfile& getReference() const noexcept { return reference; }
    bool hasReference() const noexcept { return reference.valid; }
    // Aim the mix at the reference using the listen DLIVE already has, so "sound like this"
    // does not cost another 30 seconds of the band's time. With no listen to work from it
    // starts one, and the reference is used when that listen lands.
    bool hasListened() const noexcept { return listened; }
    const MixCapture::Result& getLastListen() const noexcept { return lastCapture; }
    void startReferenceMatch();

    void abortTuneMix();                  // cancels whichever listen is running - the mix's or a channel's
    void poll();                                        // message thread, ~30 Hz: advances Listening -> Planning -> Preview
    Stage getStage() const noexcept { return stage; }
    bool isListening() const noexcept { return stage == Stage::Listening; }
    bool isWaitingForBand() const noexcept { return capture.getState() == MixCapture::State::Waiting; }
    float getListenProgress() const noexcept { return capture.getProgress(); }
    // How long the listen that is running was asked for. The card counts down from it, so it
    // has to be the real number rather than the default for the scope.
    float getListenSeconds() const noexcept { return listen.seconds; }
    bool stripHeard (int strip) const noexcept { return capture.stripHeard (strip); }
    bool busHeard (MixBus bus) const noexcept;         // any strip on the bus heard (for "Drums ✓")
    std::string getStatusText() const;                  // "LISTENING... 12 s" / "READY" / "Play the band"

    // ---- Plan preview: BEFORE / AFTER, KEEP, REVERT ----
    bool hasPlan() const noexcept { return plan.has_value(); }
    const MixPlan* getPlan() const noexcept { return plan ? &*plan : nullptr; }
    void setCompare (Compare c);
    Compare getCompare() const noexcept { return compare; }
    void keepPlan();
    void revertPlan();
    // KEEP SOME. A selection narrows what the preview plays and what KEEP applies
    // (MixPlanner::restrictTo): pick the groups, or the channels, whose changes are wanted and
    // hear exactly that on AFTER; keepPlanSelection keeps it and nothing else, and the rest of
    // the mix is exactly as it was before the listen. Clearing the selection puts the whole
    // proposal back on AFTER. The selection lives only while a plan is on preview.
    using PlanSelection = MixPlanner::PlanSelection;
    void setPlanSelection (const PlanSelection&);
    void clearPlanSelection();
    bool hasPlanSelection() const noexcept { return planSelection.has_value(); }
    const PlanSelection* getPlanSelection() const noexcept { return planSelection ? &*planSelection : nullptr; }
    void keepPlanSelection (const PlanSelection&);
    int getTuneCount() const noexcept { return tuneCount; }

    // ---- Mix history: UNDO and REDO on the mix itself ----
    //
    // KEEP and REVERT decide one plan. This is the other thing an engineer needs: a way back
    // from *any* change to the mix - a chat request, a macro, a hand edit, a whole TUNE -
    // without having to remember what it was before. One entry per change that is worth
    // undoing, each with the sentence that says what it was, so the menu reads
    // "Undo: bring the lead vocal forward" rather than "Undo".
    //
    // It is the kept mix that is remembered, never the running one: macros, BYPASS and the
    // BEFORE / AFTER preview are ways of *listening*, and undoing a way of listening would be
    // a surprise. LIVE SAFE lets both through, because going back to the mix that was working
    // a minute ago is exactly what an operator needs most in the middle of a service.
    void markMixChange (const std::string& what);   // call before making the change
    bool canUndoMix() const noexcept { return ! history.empty(); }
    bool canRedoMix() const noexcept { return ! future.empty(); }
    std::string undoMixLabel() const { return history.empty() ? std::string() : history.back().what; }
    std::string redoMixLabel() const { return future.empty() ? std::string() : future.back().what; }
    void undoMix();
    void redoMix();
    void clearMixHistory() { history.clear(); future.clear(); }

    // ---- The emergency keys: DIM and MUTE on the broadcast ----
    // One press pulls every feed but the engineer's listen down 20 dB, or silences it. Not a
    // mix change: nothing is kept, saved or undone - the key is lit while it is on, and pressing
    // it again is the way back. LIVE SAFE always allows both; they are what it is for.
    void setBroadcastDim (bool on);
    void setBroadcastMute (bool on);
    bool isBroadcastDimmed() const noexcept { return broadcastDim; }
    bool isBroadcastMuted() const noexcept { return broadcastMute; }

    // ---- Scenes: the whole mix kept for one part of the service, back in one press ----
    // KEEP writes the kept mix and the macros into a slot under the inputs' names; RECALL puts
    // them back as one undoable mix change (LIVE SAFE lets it through: returning to a mix that
    // was working is what a service needs) and is refused with a sentence when the inputs are
    // not the ones it was kept with. Saved with the session; carried across a rebuild by name.
    int numScenes() const noexcept { return kMixScenes; }
    const MixScene& getScene (int slot) const;
    void keepScene (int slot);
    bool recallScene (int slot);
    void renameScene (int slot, const std::string& name);
    std::vector<MixScene> getScenes() const;
    void restoreScenes (const std::vector<MixScene>& scenes);

    // ---- FAVOURITE MIXES: the ones that worked, kept and measured ----
    //
    // Not a third store. A favourite *is* a scene - the whole kept mix, the macros and the
    // input names - with two things added: it is named by the engineer rather than by a slot,
    // and it carries a `MixFingerprint` of what it actually sounded like. The scene list simply
    // grows past its four fixed slots, so everything that saves, restores, refuses onto a
    // different console and records a recall keeps working untouched.
    //
    // THE FINGERPRINT IS THE POINT. A mix is fader positions and chains, and neither of those
    // is what anybody means when they say they liked it: what they liked is where things
    // *landed* - the lead over the band, the backing under the lead, the kit against the bass,
    // how loud the master was and how much of it was peaks. Those are measured from the listen
    // (RelationshipEngine, and the capture's own bus and master measurements), never read off a
    // fader, because a fader at -6 means nothing without knowing what arrived at it. Marking a
    // favourite with nothing heard yet keeps the mix and says the sound is not measured.
    //
    // AND IT IS AIMABLE AT. `useFavouriteAsReference` builds a ReferenceProfile out of the
    // fingerprint's master measurements and hands it to setReference, so TUNE aims at a mix
    // this church liked through exactly the path it aims at a record - no second target
    // system, no second set of bounds.
    int numFavourites() const noexcept;
    const MixScene& getFavourite (int index) const;
    // Keep what is running now, by name. False when there is nothing built to keep.
    bool markFavourite (const std::string& name);
    bool recallFavourite (int index);
    void renameFavourite (int index, const std::string& name);
    void removeFavourite (int index);
    // Aim the mix at one, the way it is aimed at a record. False when it was never measured.
    bool useFavouriteAsReference (int index);

    // ---- Track history: what changed on one channel, and any earlier setting put back ----
    //
    // UNDO walks the whole mix back one change at a time. This is the other way an engineer
    // looks at it: one channel, every tune that landed on it - TUNE MIX, TUNE CHANNEL, TUNE
    // LIVE MIX, a Mix Buddy request - and every hand edit of its chain, oldest first, each with
    // what the strip was and what it became. Putting one back restores that record's chain,
    // input gain, level, pan and sends on that one channel and nothing else. It is an ordinary
    // mix change: LIVE SAFE bounds the level and gain steps the way it bounds any other, UNDO
    // takes it back, it is remembered here as a record of its own, and the next TUNE replaces
    // it the way it replaces a fader. The history is saved with the session and follows its
    // input across a rearrangement, so Sunday's records are there on Monday.
    const std::vector<StripTuneRecord>& getStripHistory (int strip) const;
    bool restoreStripTune (int strip, int record);     // false when refused or out of range; the sentence goes to onMessage
    std::vector<StripTuneRecord> getAllStripHistory() const;                                      // session save
    void restoreStripHistory (const std::vector<StripTuneRecord>& records);                     // session restore (same layout)
    void carryStripHistory (const std::vector<StripTuneRecord>& records, const MixSession& previousSession);
    static constexpr int kMaxStripHistory = 24;         // per channel; the oldest goes first

    // ---- MIX BUDDY ----
    // Help, not a second mixing engine (see src/MixAI/MixBuddy.h). A question is answered from
    // a copy of the session's state and never changes it: no parameter, no history entry, no
    // undo step. What it offers are buttons, each pressed on purpose; the one that can change
    // the mix, AskForChange, is TUNE LIVE MIX with the request in words, which arrives on
    // BEFORE / AFTER like any other proposal, is refused under LIVE SAFE, and is never kept by
    // anything but KEEP.
    struct ChatTurn
    {
        bool fromEngineer = true;
        std::string text;
        std::vector<std::string> detail;    // the facts an answer was read from, or what a change did
        bool failed = false;
        bool applied = false;
        std::vector<BuddyAction> actions;   // what to do next, as buttons
    };
    // Ask a question. Answered at once, deterministically, and nothing about the mix changes.
    void askBuddy (const std::string& text);
    // What Mix Buddy reads: the session right now, copied.
    BuddySnapshot buddySnapshot() const;
    // AskForChange, pressed: the request goes to TUNE LIVE MIX as a proposal. Returns false,
    // with the reason in the conversation, when it cannot run (LIVE SAFE, a proposal already
    // waiting for KEEP or REVERT, nothing heard yet, DLIVE busy).
    bool askForChange (const std::string& request);
    // A proposal Mix Buddy asked for is on BEFORE / AFTER, waiting for KEEP or REVERT.
    bool hasBuddyProposal() const noexcept { return ! buddyRequest.empty() && plan.has_value() && stage == Stage::Preview; }
    const std::vector<ChatTurn>& getChat() const noexcept { return chat; }
    void clearChat() { chat.clear(); }
    bool isChatBusy() const noexcept { return liveRun && chatRun; }
    // Can a request be made at all? The chat works from the listen DLIVE already has.
    bool canChat() const noexcept { return prepared && listened && lastCapture.valid; }

    // ---- BYPASS: hear the inputs with nothing DLIVE does ----
    // Every chain is bypassed, faders and input gains go back to their starting point and
    // the returns go silent, so what comes out is the console feed itself. Nothing about
    // the kept mix changes: switch it off and the mix is exactly as it was. Mutes and solos
    // are carried across so you can still audition one source while comparing.
    void setBypass (bool on);
    bool isBypassed() const noexcept { return bypassed; }

    // ---- WHAT THIS MICROPHONE IS DOING ----
    //
    // A church has three or four microphones that do two jobs. The handheld is the pastor's in
    // the sermon and the worship leader's in the last song; the lapel is a host introducing the
    // service and then an MC; the spare at the back is a guest, and nobody knows which kind of
    // guest until they open their mouth. Those are not the same channel: a preaching microphone
    // is levelled to a spoken target, gated, de-essed hard and cut under the boom; a lead vocal
    // is levelled to a sung target, never gated, and given a pocket in the band.
    //
    // So it is one press. `setInputRole` changes what the input *is*: the graph is rebuilt (the
    // source moves to SPEECH or LEAD or BGV), and that one strip takes the profile's own
    // starting point for its new role. Every other channel is exactly where it was.
    //
    // THESE ARE STARTING POINTS, NOT PRESETS. What lands is what `Profiles` says a source of
    // that kind starts from - the same table TUNE plans from - so everything stays editable, a
    // TUNE MIX or a RE-TUNE plans the channel as what it now is, and the choice survives both.
    //
    // The graph is rebuilt, so LIVE SAFE refuses it with a sentence and the host has to call
    // prepare() afterwards, exactly as it does for any assignment change. Returns false when
    // it was refused, out of range, or already that.
    bool setInputRole (int strip, ChannelRole role);
    // The jobs a voice microphone can be given, in the words a volunteer uses. Empty for a
    // channel that is not a microphone somebody talks or sings into.
    struct VoiceJob { ChannelRole role; const char* name; const char* what; };
    static const std::vector<VoiceJob>& voiceJobs();
    // Is this strip one of them? A kick drum is not offered a job.
    bool isVoiceChannel (int strip) const;
    // The input a console strip listens to. A strip is a position on the console and an input is
    // a line of the document; they are the same number only until an input before it is off.
    int inputOfStrip (int strip) const noexcept
    {
        return strip >= 0 && strip < graph.numStrips() ? graph.strips[size_t (strip)].input : -1;
    }
    // WHAT THIS MICROPHONE IS DOING is a family, not a role: a lapel and a lectern gooseneck
    // are both SPEAKING, and choosing SPEAKING on one of them must not turn it into the
    // other. Returns the role to store for `job` on `strip`: the one it already has when it
    // is already doing that job, and the job's own role when it is not.
    ChannelRole roleForJob (int strip, ChannelRole job) const;

    // ---- AUTOPILOT: the operator's own mix, held where they left it ----
    //
    // The rules it lives under are in CLAUDE.md. The shape of it here: engaging measures where
    // each group sits against the mix, *learnt* over the first seconds (a single meter reading
    // taken on a fill or a breath is not a mix), and keeps that as the target; `poll()` reads
    // the engine's meters a few times a second, averages them over several seconds so a
    // section of a song is not a drift, asks `Autopilot::decide` (a pure function in src/Mix,
    // with no AI in it anywhere) and applies whatever comes back through `setBusFader` - the
    // same path a hand uses, so LIVE SAFE is already in it. Group faders only. Never a
    // channel, a chain, the master fader, the returns or the engineer's listen, and never the
    // audio thread.
    //
    // WITHIN TOLERANCE IT DOES NOTHING, which is the usual answer. It also does nothing while
    // the arrangement differs from the one it learnt, while it is settling after one comes
    // back, while the meters are not moving (the device has stopped), and during a solo in
    // place.
    //
    // THE PERSON AT THE DESK OUTRANKS IT. A move on a group fader from anywhere but here
    // releases that group, and any fader move by the engineer - group or channel - is the new
    // mix to hold, so Autopilot learns it again rather than pulling the others back to the
    // old balance. A mix replaced whole (UNDO, a scene, KEEP, going back in the history) is
    // learnt afresh the same way.
    struct AutopilotState
    {
        bool on = false;
        bool holding = false;            // it has a target and something is playing
        int groupsCorrected = 0;         // how many it has had to move since it was engaged
        float largestMoveDb = 0.0f;
        std::string lastWhat, lastWhy;   // the last thing it did, in the words the history has
        std::array<float, int (MixBus::Count)> movedDb {};
        std::array<bool, int (MixBus::Count)> released {};   // the engineer took this one back
    };
    // Engaging snapshots the mix that is running. Returns false when there is nothing to hold
    // (nothing built, or nothing playing to measure), with the reason on onMessage.
    bool setAutopilot (bool on);
    bool isAutopilotOn() const noexcept { return autopilot.on; }
    const AutopilotState& getAutopilot() const noexcept { return autopilot; }
    const AutopilotLimits& getAutopilotLimits() const noexcept { return autopilotLimits; }
    void setAutopilotLimits (const AutopilotLimits& l) { autopilotLimits = l; }
    // How often it looks, ms. 400 in the product; a test sets 0 so every poll() is a look and
    // a closed loop can be run through the real engine faster than the wall clock.
    void setAutopilotIntervalMs (int ms) noexcept { autopilotIntervalMs = ms < 0 ? 0 : ms; }
    // What Autopilot can see right now, for the panel that shows it and for the tests.
    AutopilotReading readAutopilotMeters() const;
    float autopilotDrift (MixBus bus) const noexcept { return Autopilot::driftDb (autopilotTarget, readAutopilotMeters(), bus); }

    // ---- RESET MIX TO RAW ----
    //
    // Everything DLIVE has decided about the sound, taken back: every strip's chain, gain,
    // fader, pan and sends, every group's chain and fader, the returns, the master and the
    // macros, all the way back to the session's own baseline - the mix a service starts from,
    // before anything has been listened to. Sample replacement goes with it, because a
    // replaced kick is something DLIVE decided.
    //
    // NOT BYPASS. Bypass is a way of *listening*: it leaves the kept mix alone and switching it
    // off puts everything back. This throws the kept mix away and is meant to.
    //
    // What it keeps is everything that is not a mix decision: the audio on disk and the clips
    // on the timeline, the names and the assignments and the routing, the scenes, the
    // reference, the per-channel records, and the whole mix history - including a "Before
    // reset" checkpoint taken first, so it is never a one-way door. UNDO takes it back too.
    // Mutes and solos are the engineer's listening state and survive, as they do through BYPASS.
    //
    // Refused under LIVE SAFE with a sentence: putting the whole mix back to where it started
    // is exactly what should not happen mid-service. Returns false when it was refused.
    bool resetMixToRaw();

    // ---- LIVE SAFE: the lock for the twenty minutes when a mistake is public ----
    // The policy itself is src/Mix/LiveSafe.h - what is refused, what is only made smaller,
    // and what is never touched because an operator must be able to act in an emergency. It
    // is enforced *here*, on every path that can change the mix, rather than in the UI: a
    // lock that only exists in a menu handler is not a lock, and the AI, the chat, a macro
    // and a keyboard shortcut all reach the mix without passing a menu.
    void setLiveSafe (bool on);
    bool isLiveSafe() const noexcept { return safety.on; }
    const LiveSafePolicy& getLiveSafePolicy() const noexcept { return safety; }
    void setLiveSafePolicy (const LiveSafePolicy&);
    // "May this happen now?" - with the sentence that says why not. The UI asks so it can grey
    // a button out and say why; every setter below asks again before it acts.
    liveSafe::Verdict checkLiveSafe (LiveAction) const;
    // The same, and it reports the refusal through onMessage. Returns true when it was refused.
    bool liveSafeRefuses (LiveAction);

    // ---- The monitor (solo) bus: what the engineer hears, and nobody else ----
    // Solo lands on the monitor output. The live master never changes, which is the whole
    // point (src/Mix/MonitorBus.h). Everything here is monitoring: no plan, no export and no
    // macro reads any of it.
    const MonitorState& getMonitor() const noexcept { return kept.monitor; }
    void setSoloMode (SoloMode);
    void setSoloPoint (SoloPoint);
    void setMonitorGain (float db);
    void setMonitorDim (bool);
    void setMonitorMute (bool);
    void setMonitorSource (MixBus);
    void setFxSolo (FxSlot, bool);
    // The returns as one group: S on the FX RETURNS tile solos every return the session uses,
    // so the engineer hears just the reverbs and delays - what the sends are actually adding.
    // Monitoring like every other solo: in the normal (monitor) mode the sources keep feeding
    // the sends and only the returns reach the headphones.
    void setFxSoloAll (bool);
    bool anyFxSolo() const noexcept;
    // Is a monitor output actually routed? Solo with nowhere to go is an S key that does
    // nothing audible, so the app says so instead of letting it happen quietly.
    // Routed, and to a pair the device that is open actually has.
    bool hasMonitorOutput() const noexcept { return monitorFeedReaches (outputs, prepared ? engine.getDeviceOutputs() : 0); }
    bool anySolo() const noexcept;
    int numSoloed() const noexcept;
    // WHAT IS SOLOED, by name. Solo is the one state that changes what the engineer hears and
    // nothing about what the room hears, which is exactly why it is the state most easily left
    // on by accident - so the window says what is down, from every workspace, and it asks here
    // rather than walking three arrays of its own.
    struct SoloedItem
    {
        enum class Kind { Strip, Bus, Fx };
        Kind kind = Kind::Strip;
        int index = 0;               // the strip, the MixBus, or the FxSlot
        std::string name;
    };
    std::vector<SoloedItem> getSoloed() const;

    // ---- Outputs: where the sound leaves the device ----
    // Monitoring, not mix: a feed never changes the mix, the plan or an export. Feed 0 is the
    // main output and always exists. Two different devices need an Aggregate Device (macOS
    // Audio MIDI Setup); it then appears as one device with every channel.
    void setOutputFeeds (const OutputFeeds&);
    const OutputFeeds& getOutputFeeds() const noexcept { return outputs; }

    // ---- Macros (50 = the plan) ----
    // Under LIVE SAFE a value is clamped to `macroRange()` rather than refused: the pads draw
    // the fence and never ask for what would be clamped, but the policy is enforced here.
    void setMacro (MixMacro m, float value);
    const MixMacroValues& getMacros() const noexcept { return macros; }
    void resetMacros();
    liveSafe::MacroRange macroRange() const noexcept { return liveSafe::macroRange (safety); }

    // ---- Advanced edits (on the kept mix; they survive macro moves) ----
    // A fader move follows the strip's link (StripParameters::linkGroup): every other member
    // moves by the same number of dB, clamped at the ends of its own travel. `withLink = false`
    // (Cmd-drag on the console) moves this one alone.
    void setStripFader (int strip, float db, bool withLink = true);
    void setStripInputGain (int strip, float db);
    void setStripPan (int strip, float pan);            // -1 left .. +1 right (balance on a stereo strip)
    void setStripMute (int strip, bool mute);
    void setStripSolo (int strip, bool solo);
    void setStripSend (int strip, FxSlot slot, float db);

    // ---- EFFECTS ON THIS MICROPHONE ----
    //
    // The pastor's handheld is the same microphone in the sermon and in the last song, and
    // between the two the only thing that has to change is whether it is in the plate. So it is
    // one press, on the strip and in the Inspector: EFFECTS off holds this channel's sends at
    // silence, EFFECTS on lets them through again at exactly the levels they had.
    //
    // It is not `setInputRole`. Nothing is re-routed, no graph is rebuilt, the audio never
    // stops, the chain is untouched and the tuning is kept - which is why LIVE SAFE allows it
    // where it refuses a role change. Turning it on for a channel that has never had a send (a
    // speaking microphone starts dry) seeds the sends from what the profile gives a lead vocal,
    // so "he is singing now" is still one press; after that the levels are the engineer's and
    // the switch only ever gates them.
    void setStripEffects (int strip, bool on);
    // Is anything reaching a return from this channel right now?
    bool stripEffectsOn (int strip) const;
    // Is the switch worth showing? A voice microphone in a session whose vocal returns exist. A
    // kick drum is not asked, and neither is a channel in a session with no returns at all.
    bool stripCanHaveEffects (int strip) const;
    void setBusFader (MixBus bus, float db);
    void setBusMute (MixBus bus, bool mute);
    void setBusSolo (MixBus bus, bool solo);
    // The effects returns as one group. There is nothing to solo a return against, so the
    // group has a fader and a mute and no more: "take the reverb out for the sermon" is one
    // press, and the level TUNE MIX chose for each return is left where it is.
    void setFxReturn (float db);
    void setFxMute (bool mute);
    // The chain itself (the Inspector's stage controls): the whole ChannelParameters at
    // once, the way the engine takes it. A hand edit lives on the kept mix beside the
    // faders, so it survives a macro move and is what gets saved; the next TUNE MIX
    // plans from what it hears and replaces it, exactly as it replaces a fader.
    void setStripChannel (int strip, const ChannelParameters&);
    void setBusChannel (MixBus bus, const ChannelParameters&);
    void clearSolos();

    // ---- Linked faders ----
    // Two or more channels whose faders move together, and which solo together: a pair of
    // overheads, a stereo keyboard on two inputs, the choir's microphones. Mute and pan stay each
    // channel's own. Linking is one undoable mix change that LIVE SAFE lets through.
    // A member that is already in a group brings its group along; a group left with one member
    // dissolves. Linking starts the members level: every member takes the fader of the first
    // strip in the list (the one the link was made from), within the LIVE SAFE step.
    // Returns the group, 0 when there was nothing to link.
    int linkStrips (const std::vector<int>& strips);
    void unlinkStrip (int strip);
    int getStripLink (int strip) const noexcept;             // 0 = not linked
    std::vector<int> linkedWith (int strip) const;           // the other members, in strip order
    std::string linkedNames (int strip) const;               // "OH R, Room" - for a tooltip or a menu
    const MixParameters& getKept() const noexcept { return kept; }          // without macros
    // What is audible, without macros. During a TUNE LIVE MIX verify listen the applied
    // proposal has to stay audible even though the stage says Listening: the second listen is
    // measuring what was applied, and a listen to the old mix would verify nothing.
    const MixParameters& getBase() const noexcept
    {
        const bool previewing = plan && (stage == Stage::Preview || liveVerifying);
        if (! previewing) return kept;
        if (compare == Compare::Before) return plan->before;
        return planSelection ? selectedProposed : plan->proposed;
    }
    const MixParameters& getRunning() const noexcept { return running; }    // what the engine was last given
    void setKept (const MixParameters& p);                                  // session restore
    void restoreKept (const MixParameters& p, int tuneCount);               // session restore with its history
    // The mix, carried onto the session the graph has just been rebuilt for: every channel
    // that survived the change keeps its chain, its gain, its fader, its pan, its keys and its
    // sends, found by which input it *is* rather than where it sits (Mix/MixParameters.h).
    // A host calls this straight after prepare() when the assignments changed - a channel
    // moved on the timeline, an input dropped on ASSIGN - so that reordering the console costs
    // nothing. `previousMix` is what was running under `previousSession`.
    void carryKept (const MixParameters& previousMix, const MixSession& previousSession, int tuneCount);
    bool hasKeptMix() const noexcept { return mixed; }

    // ---- Gain staging: the first move in any mix, in plain words ----
    // What the last listen says about one input's level. Gain staging comes before cleanup,
    // effect, mix and master, so this is the one thing the app says about an input before it
    // says anything else. `known` is false until a plan exists. `consoleMoveDb` is what the
    // preamp on the desk should still do - DLIVE has already done what it can digitally.
    struct InputAdvice
    {
        // `Digital` is the quiet one that matters most in a church: the level works, but only
        // because DLIVE raised (or lowered) it by a lot digitally. The preamp is the right
        // place for that move - a digital raise lifts the preamp's noise with the source.
        enum class Level { Unknown, NotHeard, Faint, Low, Healthy, Hot, Clipping, Bleed, Digital };
        bool known = false;
        Level level = Level::Unknown;
        float capturePeakDb = -120.0f;    // the loudest moment at the device, before DLIVE's gain
        float digitalGainDb = 0.0f;       // the input gain DLIVE set
        float consoleMoveDb = 0.0f;       // what the preamp should still move; 0 = nothing to do
        std::string headline;             // "TURN THE PREAMP UP 6 dB" / "HEALTHY"
        std::string detail;               // the sentence that explains it
        bool needsAttention() const noexcept
        {
            return known && level != Level::Healthy && level != Level::Bleed && level != Level::Unknown;
        }
    };
    InputAdvice getInputAdvice (int strip) const;

    // THE GAIN STAGE, LIVE, BEFORE ANYTHING HAS BEEN TUNED.
    //
    // `getInputAdvice` is the verdict from a finished listen, and it needs a plan. This is the
    // same verdict for the moment the plan does not exist yet: patching the inputs with the
    // band playing, which is the one moment somebody is standing at the desk with a hand on
    // the preamps. It reads one number - the loudest this input has been over the last few
    // seconds - against the same safe range from the same profile that Tune would use for that
    // source, and says the same sentence. A soundcheck and a tune must never disagree about
    // whether an input is hot.
    //
    // No plan, no engine, no strip: a role and a held peak. Message thread.
    InputAdvice liveCaptureAdvice (ChannelRole role, float peakHoldDb) const;

    // ---- The master, metered properly ----
    // Everything anyone needs to answer "is this mix loud enough, and is it safe" from one
    // place, so the mixer, LIVE, the Inspector and the export dialog can never disagree.
    // Read at UI rate; every number comes from the engine's own atomics.
    struct MasterLoudness
    {
        bool known = false;
        float integratedLufs = -120.0f;   // the whole service so far: what a platform normalises against
        float shortTermLufs = -120.0f;    // the last 3 seconds: what to mix by
        float momentaryLufs = -120.0f;    // the last 400 ms
        float truePeakDb = -120.0f;       // inter-sample, of the last block
        float limiterReductionDb = 0.0f;  // how hard the master limiter is working right now
        float targetLufs = -23.0f;        // what this session is aiming at
        float toleranceLu = 1.0f;
        float ceilingDb = -1.0f;          // the limiter's true-peak ceiling
        float headroomDb = 0.0f;          // ceiling - the master's own peak: what is left

        // Where the mix sits against its target, in LU. Positive is louder than asked for.
        float deltaLu() const noexcept { return integratedLufs <= -100.0f ? 0.0f : integratedLufs - targetLufs; }
        bool onTarget() const noexcept { return integratedLufs > -100.0f && std::fabs (deltaLu()) <= toleranceLu; }
        bool tooQuiet() const noexcept { return integratedLufs > -100.0f && deltaLu() < -toleranceLu; }
        bool limiterWorkingHard() const noexcept { return limiterReductionDb > 3.0f; }
    };
    MasterLoudness getMasterLoudness() const;

    // ---- Health: the share of assigned inputs that were heard, not faint and at a healthy level in the last listen (0..100;
    // 0 = nothing known yet). The notes say what is not right, in plain words, so the number is never a mystery.
    int getMixHealthPercent() const;
    std::vector<std::string> getMixHealthNotes() const;

    // ---- THE SESSION'S REVISION: what saving follows ----
    //
    // Every change to anything a SessionState carries bumps this. The host saves when it has
    // moved and then gone quiet, so "was this saved?" is a question about the document rather
    // than about whether thirty-nine call sites all remembered to ask. A bump that is missed
    // delays a save; the next one writes the whole document, so it catches up. A save that was
    // never called did not.
    unsigned long long getRevision() const noexcept { return revision; }
    // Something about the session changed. Safe to call twice, cheap, and the only thing the
    // UI has to remember for a change of its own (a timeline edit) to be written down.
    void touch() noexcept { ++revision; }

    // A MOMENT WORTH NOT LOSING: a tune kept, a scene recalled, a new reference, the
    // assignments rebuilt. It moves the revision like any other change, and it also moves
    // `milestone` - which is how the host knows to write the session now rather than two
    // seconds from now. A service does not get a second chance at the take it was in.
    void mark (const std::string& what);
    unsigned long long getMilestone() const noexcept { return milestone; }
    const std::string& getLastMilestone() const noexcept { return lastMilestone; }

    // ---- MIX HISTORY: the whole mix as it was, hours ago, by name ----
    //
    // Not UNDO. Undo is the last thing you did and it dies with the graph; this is the list an
    // engineer opens on Monday to find the mix the service went out on. A checkpoint is taken
    // at every milestone and on a slow beat while the mix is being worked on, and it is saved
    // with the session. MixHistory.h has the shape and the pruning rule.
    // How long a mix has to be worked on without a milestone before poll() marks one anyway.
    static constexpr long long kCheckpointBeatMs = 5 * 60 * 1000;
    const std::vector<MixCheckpoint>& getCheckpoints() const noexcept { return checkpoints; }
    // Newest first, which is how the list is read.
    std::vector<MixCheckpoint> getCheckpointsNewestFirst() const;
    // Take one now, whatever is happening. `fromTune` ones outlive hand edits when pruning.
    void checkpoint (const std::string& what, bool fromTune = false);
    // Go back to one, by its index in getCheckpoints(). Refused, with a sentence, onto a
    // different set of inputs - the same rule a scene follows. The mix as it is now becomes a
    // checkpoint first, so going back is itself something you can come back from.
    bool restoreCheckpoint (int index);
    void restoreCheckpoints (const std::vector<MixCheckpoint>& list);     // session restore

    std::function<void (const std::string&)> onMessage;   // one-line notices for a toast

    // What happened, for the usage events (app/native/Telemetry, docs/ANALYTICS.md): a fixed
    // event name and a few fixed-vocabulary fields - a scope, an instrument, a count - never a
    // channel name, a sentence, a chat request or a file. Message thread, like every call here.
    struct UsageEvent
    {
        std::string name;
        std::vector<std::pair<std::string, std::string>> words;
        std::vector<std::pair<std::string, double>> numbers;
    };
    std::function<void (const UsageEvent&)> onUsage;

private:
    std::future<MeasuredMix::Result> measuredPlanning;
    std::shared_ptr<std::atomic<bool>> planningCancel;
    MixPlanContext planningContext;
    unsigned long long planningRevision = 0;
    void launchPlanning (const MixPlanContext&, bool masterOnly = false);
    void cancelPlanning() noexcept { if (planningCancel) planningCancel->store (true); }
    void usage (UsageEvent e) const { if (onUsage) onUsage (e); }
    // The scope of the listen or proposal in hand, in the usage events' words, with the
    // instrument or group it was about added to `e`.
    void addTuneScope (UsageEvent& e) const;
    void publish();
    MixParameters compose() const;
    // -1, -1, {} = the whole mix. Exactly one of the three is ever set.
    void startListening (const ListenSettings&, int strip, int bus = -1, const std::vector<int>& strips = {});

    MixSession session;
    MixSession builtSession;            // what `graph` and `kept` below were built for
    RoutingGraph graph;                 // built by rebuild(), from the assignments alone
    bool built = false;                 // rebuild() has run: there is a mix, device or no device
    bool stateStale = true;             // the session has moved ahead of `graph` and `kept`
    unsigned long long revision = 1;    // bumped by touch(): what the host's autosave follows
    unsigned long long milestone = 0;   // bumped by mark(): write now, do not wait
    std::string lastMilestone;          // what the last one was, in the words the menu uses
    MixEngine engine;
    MixCapture capture;
    // The last complete listen, kept so a reference (or a re-plan) can work from what DLIVE
    // already heard instead of asking the band to play again.
    MixCapture::Result lastCapture;
    MixParameters lastCaptureAt;
    // Whether the mix that listen ran through had already been tuned. Stored with the
    // listen, not read from `mixed` when a plan is made: MATCH TO REFERENCE and TRY
    // ANOTHER MIX re-plan a listen from before the last KEEP, and a listen has to plan the
    // same way every time it is planned or nothing downstream is repeatable.
    bool lastCaptureRetune = false;
    bool listened = false;
    ReferenceProfile reference;
    bool prepared = false;
    bool graphStale = false;            // the session was edited after the graph was built
    double sampleRate = 48000.0;
    int blockSize = 64;

    Stage stage = Stage::Setup;
    MixParameters kept;                 // the mix without macros: baselines, then the kept plan + Advanced edits
    MixParameters atCapture;            // what ran while listening
    MixParameters running;
    MixParameters onEngine;             // what the engine was actually handed, in the engine's own strip order
    std::optional<MixPlan> plan;
    Compare compare = Compare::After;
    MixMacroValues macros;
    OutputFeeds outputs;                // where the sound leaves the device (monitoring only)
    LiveSafePolicy safety;              // LIVE SAFE, enforced on every path that changes the mix
    bool bypassed = false;              // hearing the raw inputs; the kept mix is untouched
    int tuneCount = 0;
    int tuningStrip = -1;               // TUNE CHANNEL: the one strip being listened to / previewed
    int tuningBus = -1;                 // TUNE <GROUP>: the one group bus being listened to / previewed
    std::vector<int> tuningStrips;      // TUNE <these channels>: the set, empty when the scope is not that
    std::string lastScope { "the whole mix" };   // what the last listen was about, in the card's own words
    std::string scopeWords() const;     // the current scope, in those words
    std::optional<PlanSelection> planSelection;   // KEEP SOME: what AFTER plays and KEEP applies, while set
    MixParameters selectedProposed;               // the proposal narrowed to the selection (recomputed when either changes)
    void refreshSelection();
    // A listen, a plan and a preview are always about one scope - the whole mix, one channel
    // or one group - and a KEEP SOME selection lives inside that scope. Whatever ends one
    // ends all of them, so there is one place that says so.
    void clearTuningScope() noexcept;
    bool mixed = false;                 // a plan was kept (or a saved mix restored): the mix is more than the baselines
    ListenSettings listen;

    // TUNE LIVE MIX. `liveRun` is on for the whole workflow, across both listens; `liveVerifying`
    // is on only while the second listen runs, and is what keeps the applied mix audible during
    // it (a verify listen has to hear what was applied, not what it replaced).
    // The mix as it was before each change worth undoing, newest last, with what the change
    // was. `future` is what UNDO took away, so REDO can put it back.
    struct MixSnapshot { MixParameters mix; MixMacroValues macros; std::string what; bool mixedThen = false; };
    std::vector<MixSnapshot> history, future;
    static constexpr size_t kMaxHistory = 64;
    void applySnapshot (const MixSnapshot&);
    MixSnapshot snapshotNow (const std::string& what) const;
    // Per channel, oldest first. Cleared with the graph in prepare(); the host carries the
    // saved records back on afterwards (carryStripHistory), as it carries the kept mix.
    std::array<std::vector<StripTuneRecord>, kMaxStrips> stripHistory;
    bool liveKept = false;              // the plan on preview came from TUNE LIVE MIX (or the chat), so KEEP names it so
    bool broadcastDim = false, broadcastMute = false;   // the emergency keys: overlays on what is published, never kept
    // The four fixed service slots first, then the favourites - one list, so saving,
    // restoring and refusing onto a different console are the same code for both.
    // Autopilot. `autopilotMoving` is what tells setBusFader that a move is its own, so an
    // engineer's move on the same group can be told apart from one of its own and hand the
    // group back.
    AutopilotState autopilot;
    AutopilotTarget autopilotTarget;
    AutopilotLimits autopilotLimits;
    bool autopilotMoving = false;
    long long autopilotLastMs = 0;
    int autopilotIntervalMs = 400;
    std::array<float, int (MixBus::Count)> autopilotSinceHistoryDb {};
    // The slow ear: each group's landing level and the groups together, as mean squares
    // averaged over several seconds of audio. `autopilotLearn` is the audio still to hear before
    // a target is taken from them; `autopilotSettle` what is left after the arrangement came back.
    std::array<double, int (MixBus::Count)> autopilotAvgMs {};
    double autopilotMixMs = 0.0;
    double autopilotAvgSeconds = 0.0;        // how much audio the average holds since it was last reset
    double autopilotSinceDecide = 0.0;       // seconds of audio since it last decided anything
    // Whether each group is playing, with a memory: a kick between hits or a singer between
    // lines is not a group that has stopped, and reading it so would call every bar a new
    // arrangement.
    std::array<bool, int (MixBus::Count)> autopilotPlaying {};
    std::array<double, int (MixBus::Count)> autopilotFlipFor {};   // seconds the meter has disagreed with it
    double autopilotLearn = 0.0;             // seconds of audio still to learn from
    double autopilotSettle = 0.0;            // seconds of audio still to settle for
    long long autopilotLastSamples = -1;
    void pollAutopilot();
    bool engineHasThisGraph() const noexcept;     // the graph the engine was prepared with is this one
    MixParameters onEngineGraph (const MixParameters& p) const;   // p, strip by strip, onto the graph the engine plays
    // AN ENGINEER'S MOVE IS THE ENGINEER'S, WHICHEVER SIDE IS PLAYING. While a proposal is on
    // BEFORE / AFTER a fader, a mute, a solo, a send or a pan goes into both sides: a mic muted
    // over a howl on BEFORE is muted in what is heard, and REVERT or TRY ANOTHER never un-mutes
    // it. A change to a channel's processing stays with the proposal it was made on.
    template <typename Edit> void bothSides (Edit&& edit)
    {
        if (! plan || stage != Stage::Preview) return;
        edit (plan->proposed);
        edit (plan->before);
    }
    void autopilotRelearn();                 // a new mix to hold: learn it before moving anything
    void autopilotFlushHistory (const char* why);

    std::vector<MixScene> scenes = std::vector<MixScene> (size_t (kMixScenes));
    MixFingerprint measureNow() const;      // what the mix that is running actually sounds like
    std::vector<MixCheckpoint> checkpoints;
    long long lastCheckpointMs = 0;     // the slow beat: see poll()
    MixParameters atLastCheckpoint;
    std::vector<std::string> inputNamesNow() const;
    void recordStripTune (int strip, const std::string& what, const StripParameters& before, const StripParameters& after);
    std::array<std::vector<StripTuneRecord>, kMaxStrips> carriedStripHistory (const MixSession& previous) const;

    std::vector<ChatTurn> chat;
    bool chatRun = false;               // this live run was asked for from Mix Buddy (AskForChange)
    std::string buddyRequest;           // ... and what it asked for, until KEEP or REVERT decides it

    TuneLiveCoordinator tuneLive;
    LiveTuneSettings liveSettings;
    MixParameters liveBefore;           // the complete pre-Tune snapshot: what REVERT goes back to
    bool liveRun = false;
    bool liveVerifying = false;
    void pollTuneLive();
    void applyLiveProposal();
    void endTuneLive (const std::string& message, bool keepProposal, const char* outcome);   // outcome: the usage word
    Stage restingStage() const noexcept { return mixed ? Stage::Mixed : Stage::Ready; }
};

} // namespace livemix
