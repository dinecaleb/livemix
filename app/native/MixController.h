#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "Mix/MixEngine.h"
#include "Mix/MixCapture.h"
#include "Mix/MixPlanner.h"
#include "Mix/MixMacros.h"
#include "Mix/OutputFeeds.h"
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

    // ---- Engine lifecycle (AudioHost calls these with the device stopped) ----
    void prepare (double sampleRate, int maxBlockSize);   // builds the graph for the session, clears any plan
    bool isPrepared() const noexcept { return prepared; }
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
    };
    void startTuneLiveMix (const LiveTuneSettings& s);
    void startTuneLiveMix() { startTuneLiveMix (LiveTuneSettings {}); }
    // Off by default is the offline engineer, which needs no network and no configuration.
    // Passing nullptr goes back to it.
    void setReasoningProvider (std::shared_ptr<MixReasoningProvider>);
    const TuneLiveCoordinator& getTuneLive() const noexcept { return tuneLive; }
    bool isTuningLive() const noexcept { return liveRun; }
    // What the sheet reads while a run is going: the state machine's own words.
    std::string getTuneLiveStatus() const { return tuneLive.getStatusText(); }

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

    // ---- BYPASS: hear the inputs with nothing DLIVE does ----
    // Every chain is bypassed, faders and input gains go back to their starting point and
    // the returns go silent, so what comes out is the console feed itself. Nothing about
    // the kept mix changes: switch it off and the mix is exactly as it was. Mutes and solos
    // are carried across so you can still audition one source while comparing.
    void setBypass (bool on);
    bool isBypassed() const noexcept { return bypassed; }

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
    MixEngine engine;
    MixCapture capture;
    bool prepared = false;
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
    bool bypassed = false;              // hearing the raw inputs; the kept mix is untouched
    int tuneCount = 0;
    int tuningStrip = -1;               // TUNE CHANNEL: the one strip being listened to / previewed
    bool mixed = false;                 // a plan was kept (or a saved mix restored): the mix is more than the baselines
    ListenSettings listen;

    // TUNE LIVE MIX. `liveRun` is on for the whole workflow, across both listens; `liveVerifying`
    // is on only while the second listen runs, and is what keeps the applied mix audible during
    // it (a verify listen has to hear what was applied, not what it replaced).
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
