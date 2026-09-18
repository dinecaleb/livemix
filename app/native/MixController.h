#pragma once
#include <atomic>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "Mix/MixEngine.h"
#include "Mix/MixCapture.h"
#include "Mix/MixPlanner.h"
#include "Mix/MixMacros.h"
#include "Mix/OutputFeeds.h"
#include "Mix/LiveSafe.h"
#include "Mix/MonitorBus.h"
#include "MixAI/TuneLiveCoordinator.h"

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
    void setSessionName (const std::string& name) { session.name = name; }
    // Correcting what an input is called. A name is a label, not routing, so this does not
    // rebuild the graph: the kept mix, the plan, the listen and the timeline's clips all
    // survive it, and TRACKS, MIXER and the Inspector are renamed together because the
    // graph's copy is set here too. Changing which source an input *is* goes through
    // setSession + prepare(), because that changes the bus and the baseline chain.
    void setInputName (int strip, const std::string& name);
    // What the source is drawn as. Empty goes back to the role's own icon. Like a rename
    // this is a label: no rebuild, and nothing about the mix or the plan changes.
    void setInputIcon (int strip, const std::string& icon);
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

    // ---- Engine lifecycle (AudioHost calls these with the device stopped) ----
    void prepare (double sampleRate, int maxBlockSize);   // builds the graph for the session, clears any plan
    // Does the engine have a graph it can run? This is what the audio callback asks before it
    // does anything, so it must mean exactly that - and in particular it must not go false
    // just because the *document* changed, or editing the assignments would silence the room.
    bool isPrepared() const noexcept { return prepared; }
    // The document has been changed and the graph has not caught up yet. The mix keeps
    // playing the graph it has; a host calls prepare() when the user is ready for the change.
    bool needsReconfigure() const noexcept { return graphStale; }
    // The session the running graph was built for. setSession() replaces the document before
    // the device has been stopped and the graph rebuilt, so this - not getSession() - is what
    // the mix that is currently loaded belongs to, and what carrying it across a rebuild has
    // to be read against.
    const MixSession& getPreparedSession() const noexcept { return preparedSession; }
    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    const MixEngine& getEngine() const noexcept { return engine; }
    const RoutingGraph& getGraph() const noexcept { return engine.getGraph(); }

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
    std::string getTuningName() const;                               // that channel's name, empty for a mix

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

    // ---- AI MIX CHAT ----
    // The chat is not a second mixing engine. A request in plain words goes through exactly
    // the same pipeline as TUNE LIVE MIX - intent, resolve, validate, an ordinary MixPlan -
    // so BEFORE / AFTER, KEEP, REVERT, the Inspector and the session record all work on it
    // unchanged, and nothing a sentence asks for can reach a parameter by a path the reasoning
    // layer could not. What the chat adds is the conversation and the history.
    struct ChatTurn
    {
        bool fromEngineer = true;
        std::string text;
        std::vector<std::string> detail;    // what DLIVE decided, a line each
        bool failed = false;
        bool applied = false;
    };
    // Ask for something. Returns false when the mix is busy or there is nothing to work from,
    // with the reason on onMessage. The answer arrives through poll(), like every other run.
    bool sendChatRequest (const std::string& text);
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
    // Is a monitor output actually routed? Solo with nowhere to go is an S key that does
    // nothing audible, so the app says so instead of letting it happen quietly.
    bool hasMonitorOutput() const noexcept { return hasMonitorFeed (outputs); }
    bool anySolo() const noexcept;
    int numSoloed() const noexcept;

    // ---- Outputs: where the sound leaves the device ----
    // Monitoring, not mix: a feed never changes the mix, the plan or an export. Feed 0 is the
    // main output and always exists. Two different devices need an Aggregate Device (macOS
    // Audio MIDI Setup); it then appears as one device with every channel.
    void setOutputFeeds (const OutputFeeds&);
    const OutputFeeds& getOutputFeeds() const noexcept { return outputs; }

    // ---- Macros (50 = the plan) ----
    void setMacro (MixMacro m, float value);
    const MixMacroValues& getMacros() const noexcept { return macros; }
    void resetMacros();

    // ---- Advanced edits (on the kept mix; they survive macro moves) ----
    void setStripFader (int strip, float db);
    void setStripInputGain (int strip, float db);
    void setStripPan (int strip, float pan);            // -1 left .. +1 right (balance on a stereo strip)
    void setStripMute (int strip, bool mute);
    void setStripSolo (int strip, bool solo);
    void setStripSend (int strip, FxSlot slot, float db);
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
    const MixParameters& getKept() const noexcept { return kept; }          // without macros
    // What is audible, without macros. During a TUNE LIVE MIX verify listen the applied
    // proposal has to stay audible even though the stage says Listening: the second listen is
    // measuring what was applied, and a listen to the old mix would verify nothing.
    const MixParameters& getBase() const noexcept
    {
        const bool previewing = plan && (stage == Stage::Preview || liveVerifying);
        return previewing ? (compare == Compare::Before ? plan->before : plan->proposed) : kept;
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

    std::function<void (const std::string&)> onMessage;   // one-line notices for a toast
    std::function<void()> onMixChanged;                   // the kept mix, the macros or an Advanced edit changed: worth saving

private:
    void publish();
    MixParameters compose() const;
    void startListening (const ListenSettings&, int strip);   // -1 = the whole mix

    MixSession session;
    MixSession preparedSession;         // what the running graph was built for
    MixEngine engine;
    MixCapture capture;
    // The last complete listen, kept so a reference (or a re-plan) can work from what DLIVE
    // already heard instead of asking the band to play again.
    MixCapture::Result lastCapture;
    MixParameters lastCaptureAt;
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
    std::optional<MixPlan> plan;
    Compare compare = Compare::After;
    MixMacroValues macros;
    OutputFeeds outputs;                // where the sound leaves the device (monitoring only)
    LiveSafePolicy safety;              // LIVE SAFE, enforced on every path that changes the mix
    bool bypassed = false;              // hearing the raw inputs; the kept mix is untouched
    int tuneCount = 0;
    int tuningStrip = -1;               // TUNE CHANNEL: the one strip being listened to / previewed
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

    std::vector<ChatTurn> chat;
    bool chatRun = false;               // this live run came from the chat, not from TUNE LIVE MIX

    TuneLiveCoordinator tuneLive;
    LiveTuneSettings liveSettings;
    MixParameters liveBefore;           // the complete pre-Tune snapshot: what REVERT goes back to
    bool liveRun = false;
    bool liveVerifying = false;
    void pollTuneLive();
    void applyLiveProposal();
    void endTuneLive (const std::string& message, bool keepProposal);
    Stage restingStage() const noexcept { return mixed ? Stage::Mixed : Stage::Ready; }
};

} // namespace livemix
