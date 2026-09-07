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

namespace livemix
{

// The message-thread owner of one DINELIVE mix: the session (who is what), the engine,
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
    void abortTuneMix();
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

    // ---- Macros (50 = the plan) ----
    void setMacro (MixMacro m, float value);
    const MixMacroValues& getMacros() const noexcept { return macros; }
    void resetMacros();

    // ---- Advanced edits (on the kept mix; they survive macro moves) ----
    void setStripFader (int strip, float db);
    void setStripInputGain (int strip, float db);
    void setStripMute (int strip, bool mute);
    void setStripSend (int strip, FxSlot slot, float db);
    void setBusFader (MixBus bus, float db);
    const MixParameters& getKept() const noexcept { return kept; }          // without macros
    const MixParameters& getBase() const noexcept { return (plan && stage == Stage::Preview) ? (compare == Compare::Before ? plan->before : plan->proposed) : kept; } // what is audible, without macros
    const MixParameters& getRunning() const noexcept { return running; }    // what the engine was last given
    void setKept (const MixParameters& p);                                  // session restore
    void restoreKept (const MixParameters& p, int tuneCount);               // session restore with its history
    bool hasKeptMix() const noexcept { return mixed; }

    // ---- Health: the share of assigned inputs that were heard, not faint and at a healthy level in the last listen (0..100;
    // 0 = nothing known yet). The notes say what is not right, in plain words, so the number is never a mystery.
    int getMixHealthPercent() const;
    std::vector<std::string> getMixHealthNotes() const;

    std::function<void (const std::string&)> onMessage;   // one-line notices for a toast
    std::function<void()> onMixChanged;                   // the kept mix, the macros or an Advanced edit changed: worth saving

private:
    void publish();
    MixParameters compose() const;

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
    int tuneCount = 0;
    bool mixed = false;                 // a plan was kept (or a saved mix restored): the mix is more than the baselines
    ListenSettings listen;
    Stage restingStage() const noexcept { return mixed ? Stage::Mixed : Stage::Ready; }
};

} // namespace livemix
