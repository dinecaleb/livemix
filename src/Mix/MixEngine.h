#pragma once
#include <array>
#include "Core/Realtime.h"
#include <atomic>
#include <memory>
#include <vector>
#include "MixSession.h"
#include "MixParameters.h"
#include "OutputFeeds.h"
#include "RoutingGraph.h"
#include "DSP/ChannelProcessor.h"
#include "FX/FxChain.h"
#include "Core/Smoother.h"
#include "Core/TripleBuffer.h"

namespace livemix
{

// Audio-thread taps used while Tune Mix listens (MixCapture implements this).
// Every call is wait-free; the engine only calls them while isActive().
class MixTap
{
public:
    virtual ~MixTap() = default;
    virtual bool isActive() const noexcept = 0;
    virtual void pushStripInput (int strip, const AudioBlockView& raw) noexcept = 0;         // what the chain receives: after DLIVE's digital gain
    // What the converter delivered, before DLIVE's digital gain: the only place a clip is a clip.
    // A converter clipping under a -6 dB digital trim arrives at -6 dBFS after it and reads
    // healthy; a clean input under a +6 dB trim reads as clipping. Default: nothing counted.
    virtual void countConverterClips (int, const AudioBlockView&) noexcept {}
    virtual void pushStripProcessed (int strip, const AudioBlockView& processed) noexcept = 0; // after the chain, before the fader
    virtual void pushBus (MixBus bus, const AudioBlockView& input) noexcept = 0;               // what the bus chain receives (after summing and the bus fader, before its processing)
    virtual void pushMasterOutput (const AudioBlockView& output) noexcept = 0;                  // what leaves the master (the broadcast)
};

// The whole DLIVE mix as one real-time processor:
//
//   device inputs -> strips (ChannelProcessor each) -> fader/pan -> DRUMS | BASS | MUSIC | VOCALS buses
//                                                    -> post-fader sends -> FX returns (FxChain, wet only)
//   buses -> fader -> MASTER (ChannelProcessor with limiter + loudness meter) -> stereo output
//
// prepare() allocates everything for the session; process() never allocates,
// locks or blocks. Parameters arrive whole through a TripleBuffer and are applied
// only when a new snapshot was published, so a knob drag costs one apply, not one
// per block. Meters live in the processors (atomics) and are read by the UI.
class MixEngine
{
public:
    MixEngine();
    ~MixEngine();

    // Message thread, audio stopped. Builds the graph for the session.
    void prepare (double sampleRate, int maxBlockSize, const MixSession& session);
    void reset() noexcept;   // clear DSP state, keep parameters

    // Message thread: publish a complete snapshot. Wait-free for both sides.
    void setParameters (const MixParameters& p);

    // Message thread: where the sound leaves the device. Monitoring only - a feed never
    // changes the mix, so this has its own publish and is not part of MixParameters.
    void setOutputFeeds (const OutputFeeds& f);
    const OutputFeeds& getAppliedOutputFeeds() const noexcept { return appliedFeeds; }
    const MixParameters& getAppliedParameters() const noexcept { return applied; } // audio-thread view (read for display only)
    // What speech priority is doing right now, dB (<= 0). Any thread.
    float getSpeechDuckDb() const noexcept { return speechDuckDb.load (std::memory_order_relaxed); }
    // SHARE THE MICS: how far strip `i` is stepped back right now (0 = open), for the UI and the tests.
    float getAutoMixGainDb (int i) const noexcept
    {
        return i >= 0 && i < int (strips.size()) ? strips[size_t (i)]->autoGainDb.load (std::memory_order_relaxed) : 0.0f;
    }

    // Audio thread. inputs: device channels; outputs: at least 1 channel (mono sum) or 2 (L/R).
    // Every output channel is written: the feeds decide what lands where, the rest is silence.
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept LIVEMIX_NONBLOCKING;

    // Tune Mix listening. Set on the message thread before starting a capture; the engine checks isActive() per block.
    void setTap (MixTap* newTap) noexcept { tap.store (newTap, std::memory_order_release); }

    int getLatencySamples() const noexcept;

    // Sample replacement: the sounds the drum strips can play, by family and slot. Message
    // thread; the table and every bank in it outlive the engine (the app owns them for its
    // lifetime), so the audio thread only ever reads a pointer that is always valid. A strip
    // picks its bank from the table whenever a snapshot lands (its `replaceSound`).
    void setSampleBanks (const SampleBankTable* table) noexcept { sampleBanks.store (table, std::memory_order_release); }
    const SampleBankTable* getSampleBanks() const noexcept { return sampleBanks.load (std::memory_order_acquire); }
    // Hear a sound before using it: plays the bank's loudest hit once, at `gainDb` peak, into the
    // engineer's listen (the monitor bus) and nowhere else - the broadcast never hears an
    // audition. Message thread; nothing happens when no feed carries the monitor.
    // `onMain` is the one exception, and it is solo in place's: a device with one pair has no
    // private listen, so with solo set to be heard by everyone the sound goes into the main
    // mix, ahead of the master's chain so its ceiling still holds. MixController decides it.
    void auditionSample (const SampleBank* bank, float gainDb, bool onMain = false) noexcept
    {
        auditionGainDb.store (gainDb, std::memory_order_relaxed);
        auditionOnMain.store (onMain, std::memory_order_relaxed);
        auditionRequest.store (bank, std::memory_order_release);
    }
    double getSampleRate() const noexcept { return sr; }
    // How many outputs the device handed the last block had; 0 before the first block.
    int getDeviceOutputs() const noexcept { return deviceOutputs.load (std::memory_order_relaxed); }
    int getNumStrips() const noexcept { return numStrips; }
    const RoutingGraph& getGraph() const noexcept { return graph; }

    // Message thread. A strip's name is a label the UI reads, never something process()
    // looks at, so correcting one costs nothing: the routing, the chains, the kept mix and
    // the plan all stay exactly as they are. Anything that changes the routing needs
    // prepare() and a rebuilt graph instead.
    void setStripName (int strip, const std::string& name)
    {
        if (strip >= 0 && strip < int (graph.strips.size())) graph.strips[size_t (strip)].name = name;
    }

    // The same for what the source is drawn as: a label the UI reads, nothing else.
    void setStripIcon (int strip, const std::string& icon)
    {
        if (strip >= 0 && strip < int (graph.strips.size())) graph.strips[size_t (strip)].icon = icon;
    }

    // Meters (any thread).
    //
    // Strip indices come from the RoutingGraph, and the *session's* graph is a step ahead of
    // the engine's whenever the assignments have changed and no device has re-prepared yet -
    // including the whole time a session is open with nothing plugged in, when the engine has
    // no strips at all. So an index past what is running reads an idle strip: silent meters,
    // default options, nothing allocated. A page that draws a channel DLIVE is not yet
    // playing draws it quiet, which is the truth, instead of reading past the end of a vector.
    // What arrived from the console on a strip's channels, before anything DLIVE does: the
    // loudest sample since the last call (one reader: CHECK INPUTS), and whether any sample has
    // reached full scale since the clip was last cleared. Message thread.
    float consumeConverterPeakDb (int strip) const noexcept;
    bool converterClipped (int strip) const noexcept;
    void clearConverterClips() const noexcept;
    const ChannelProcessor& getStrip (int index) const noexcept
    {
        if (index < 0 || index >= numStrips) return idle;
        return strips[size_t (index)]->processor;
    }
    const ChannelProcessor& getBus (MixBus bus) const noexcept { return buses[size_t (bus)].processor; }
    ChannelProcessor& getBus (MixBus bus) noexcept { return buses[size_t (bus)].processor; }
    const FxChain& getFx (FxSlot slot) const noexcept { return fx[size_t (slot)].chain; }

    // A READ-ONLY TAP ON A GROUP, for the offline bounce.
    //
    // After `process` returns, a group bus's accumulator still holds that block's stereo
    // output, post its own chain and pre its fader; `busFaderGain` is the fader that was
    // applied into the master. Together they are one stem. Nothing on the audio thread reads
    // either of them and `process` does not change because they exist - they are a way of
    // looking at what it already computed, in a render that is nobody's audio callback.
    const float* busOutput (MixBus bus, int channel) const noexcept
    {
        if (channel < 0 || channel > 1) return nullptr;
        return buses[size_t (bus)].ptrs[size_t (channel)];
    }
    float busFaderGain (MixBus bus) const noexcept { return buses[size_t (bus)].gain.getCurrent(); }
    bool isBusUsed (MixBus b) const noexcept { return graph.busUsed[size_t (b)]; }
    // Is anything soloed into the engineer's listen? (Audio-thread view; display only.)
    bool isMonitorSoloActive() const noexcept { return monitorSoloActive; }
    bool isFxUsed (FxSlot s) const noexcept { return graph.fxUsed[size_t (s)]; }

    // Audio-thread cost, for the performance tests and the diagnostics view.
    struct Stats { float lastBlockMicros = 0.0f, peakBlockMicros = 0.0f; int blocks = 0; };
    Stats getStats() const noexcept;
    void resetStats() noexcept { peakMicros.store (0.0f); blockCount.store (0); }

    // The two last-resort guards (see MixEngine.cpp): how many blocks a stage produced that
    // were not numbers, and how many blocks an output had to be held at full scale. Both are
    // zero in a mix that is right; a number here is a fault somebody should be told about.
    // How much audio has gone through, for anything on the message thread that needs time in
    // the audio's own terms (Autopilot's averages): a UI thread that stalls must not make a
    // second of music count as three.
    long long getProcessedSamples() const noexcept { return processedSamples.load (std::memory_order_relaxed); }
    int getNonFiniteBlocks() const noexcept { return nonFinite.load (std::memory_order_relaxed); }
    int getClampedOutputBlocks() const noexcept { return outputClamped.load (std::memory_order_relaxed); }

private:
    struct Strip
    {
        ChannelProcessor processor;
        int inputA = -1, inputB = -1, channels = 1;
        MixBus bus = MixBus::Music;
        Smoother inputGain;                                     // digital preamp, linear
        Smoother gainL, gainR;                                  // fader x pan, linear
        Smoother monitorGain;                                   // 1 while soloed into the monitor bus
        std::array<Smoother, int (FxSlot::Count)> send;         // linear
        std::array<std::vector<float>, kMaxChannels> scratch;
        std::array<float*, kMaxChannels> ptrs {};
        // AT THE CONVERTER, before the digital gain (see MixTap::countConverterClips): the peak
        // since it was last read, and whether it has hit full scale. CHECK INPUTS reads these.
        std::atomic<float> converterPeak { 0.0f };
        std::atomic<bool> converterClipped { false };
        // SHARE THE MICS (MixParameters::AutoMix). The member's voice level (a mean-square
        // envelope of the processed signal), the gain it is at and the one it is heading for,
        // and the signal as it was before that gain - what a pre-fade listen hears.
        bool autoMember = false;
        float autoEnv = 0.0f, autoGain = 1.0f, autoTarget = 1.0f;
        std::array<std::vector<float>, kMaxChannels> preAuto;
        std::atomic<float> autoGainDb { 0.0f };
    };
    struct Bus
    {
        ChannelProcessor processor;
        Smoother gain;
        Smoother monitorGain;                                  // 1 while soloed into the monitor bus
        std::array<std::vector<float>, 2> buffer;               // accumulator, stereo
        std::array<float*, 2> ptrs {};
    };
    struct Fx
    {
        FxChain chain;
        Smoother returnGain;
        Smoother monitorGain;                                  // soloed into the engineer's listen
        std::array<std::vector<float>, 2> buffer;               // send accumulator, stereo
        std::array<float*, 2> ptrs {};
    };
    // The engineer's listen. A stereo accumulator that exists beside the master and never
    // feeds it: whatever is soloed lands here and leaves by a monitor output feed, so the
    // broadcast is untouched by anything an engineer does to find a problem.
    struct Monitor
    {
        Smoother gain;
        std::array<std::vector<float>, 2> buffer;
        std::array<float*, 2> ptrs {};
    };

    void applyParameters (const MixParameters& p) noexcept;
    static void panGains (float pan, bool stereo, float& l, float& r) noexcept;

    double sr = 48000.0;
    int maxBlock = 0;
    int numStrips = 0;
    MixSession session;
    RoutingGraph graph;

    std::vector<std::unique_ptr<Strip>> strips;
    std::vector<int> stripOrder;                                 // processing order: kicks, snares, then everything else
    ChannelProcessor idle;              // never prepared, never processed: what getStrip() reads past the end
    std::array<Bus, int (MixBus::Count)> buses;
    std::array<Fx, int (FxSlot::Count)> fx;
    Monitor monitor;
    // Resolved once per publish, so the block itself only multiplies.
    bool monitorSoloActive = false;         // something is soloed
    bool monitorPfl = false;                // tap before the fader
    bool monitorRouted = false;             // a feed actually carries it: with none, the whole monitor path is skipped
    std::atomic<int> deviceOutputs { 0 };   // written by process(), read by the message thread
    MixBus monitorSource = MixBus::Master;  // what it carries with nothing soloed

    TripleBuffer<MixParameters> mailbox;
    MixParameters applied;                                       // audio thread's copy of the last snapshot
    TripleBuffer<OutputFeeds> feedMailbox;
    OutputFeeds appliedFeeds;                                    // audio thread's copy of the output routing
    // Each feed's level and mute, ramped like every other gain: a feed turned down or muted is
    // a move, never a step (and never a click on the broadcast).
    std::array<Smoother, kMaxOutputFeeds> feedLevel;
    std::vector<float> feedRamp;                                 // this block's per-sample feed gain (sized in prepare)
    bool feedsSettled = false;                                   // the first routing after prepare lands at once
    OutputFeeds previousFeeds;                                   // the routing before this one, to tell a new feed from a moved level
    // The last mix the controller published (message thread), so a prepare - a device coming
    // back, a rebuild - starts from the mix that was running rather than from the profile's
    // baseline with every mute off and the broadcast un-MUTEd. See prepare().
    MixParameters lastPublished;
    bool hasPublished = false;
    bool snapNextApply = false;                                  // audio thread: the first publish after prepare lands at once
    bool haveApplied = false;

    std::atomic<MixTap*> tap { nullptr };
    std::atomic<const SampleBankTable*> sampleBanks { nullptr };

    // SPEECH PRIORITY. The one gain in the engine that moves without being told to. The
    // detector is the speech group's own processed output, read a block behind - the groups
    // are processed in console order and SPEECH comes after the band - which at a 150 ms
    // attack is a millisecond and a half of nothing. `duck` is linear gain, smoothed per
    // sample so a band stepping back never sounds like a gate.
    float speechDuckGain = 1.0f;
    std::vector<float> duckScratch;    // per-sample duck gain for this block (sized in prepare)
    // Everything the duck needs while the audio runs, worked out when the snapshot arrives:
    // process() only multiplies and adds.
    float speechThresholdLin = 0.0f, speechDepthGain = 1.0f;
    float speechAttackCoeff = 0.0f, speechReleaseCoeff = 0.0f, speechOffCoeff = 0.0f;
    float speechHoldSamples = 0.0f;
    float speechHoldLeft = 0.0f;       // samples of hold still owed
    bool speechWasOpen = false;
    // The detector itself: the speech group's voice band (150 Hz - 4 kHz), as a level rather
    // than a peak, with a gap between opening and closing. A peak of the band bleeding into an
    // open lectern microphone opened the old one; a pastor's breath between phrases closed it.
    float speechLow = 0.0f, speechLow2 = 0.0f, speechHigh = 0.0f, speechHigh2 = 0.0f, speechEnv = 0.0f;
    float speechLowCoeff = 0.0f, speechHighCoeff = 0.0f, speechEnvUp = 0.0f, speechEnvDown = 0.0f;
    bool speechVoiced = false;           // the detector's own state, before the hold
    std::atomic<float> speechDuckDb { 0.0f };   // what it is doing, for the UI
    // SHARE THE MICS: resolved once per publish (applyParameters), used per block.
    bool autoOn = false;
    float autoThresholdPow = 1.0e-5f, autoDepthGain = 0.18f;
    float autoAttackCoeff = 0.0f, autoReleaseCoeff = 0.0f, autoEnvUp = 0.0f, autoEnvDown = 0.0f;
    KitTriggerTable kitTriggers;                                 // the drum strips' word to each other (audio thread only)
    Smoother broadcastGain;                                      // DIM (-20 dB) / MUTE on every feed but the listen
    std::vector<float> broadcastRamp;                            // the smoother, per sample, for the block (feeds share it)
    SamplePlayer auditionPlayer;                                 // HEAR IT: one voice into the monitor bus
    std::atomic<const SampleBank*> auditionRequest { nullptr };
    std::atomic<float> auditionGainDb { -12.0f };
    std::atomic<bool> auditionOnMain { false };
    bool auditionPlayingOnMain = false;                          // audio thread: where the sound now playing was sent
    long long samplePosition = 0;                                // running, from prepare()
    std::atomic<float> lastMicros { 0.0f }, peakMicros { 0.0f };
    std::atomic<int> blockCount { 0 };
    std::atomic<long long> processedSamples { 0 };
    std::atomic<int> nonFinite { 0 };        // blocks a stage produced that were not numbers
    std::atomic<int> outputClamped { 0 };    // blocks where an output had to be held at full scale
};

} // namespace livemix
