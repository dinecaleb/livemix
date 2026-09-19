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
    virtual void pushStripInput (int strip, const AudioBlockView& raw) noexcept = 0;         // what the converter delivered
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

    // Audio thread. inputs: device channels; outputs: at least 1 channel (mono sum) or 2 (L/R).
    // Every output channel is written: the feeds decide what lands where, the rest is silence.
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept LIVEMIX_NONBLOCKING;

    // Tune Mix listening. Set on the message thread before starting a capture; the engine checks isActive() per block.
    void setTap (MixTap* newTap) noexcept { tap.store (newTap, std::memory_order_release); }

    int getLatencySamples() const noexcept;
    double getSampleRate() const noexcept { return sr; }
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
    const ChannelProcessor& getStrip (int index) const noexcept { return strips[size_t (index)]->processor; }
    const ChannelProcessor& getBus (MixBus bus) const noexcept { return buses[size_t (bus)].processor; }
    ChannelProcessor& getBus (MixBus bus) noexcept { return buses[size_t (bus)].processor; }
    const FxChain& getFx (FxSlot slot) const noexcept { return fx[size_t (slot)].chain; }
    bool isBusUsed (MixBus b) const noexcept { return graph.busUsed[size_t (b)]; }
    // Is anything soloed into the engineer's listen? (Audio-thread view; display only.)
    bool isMonitorSoloActive() const noexcept { return monitorSoloActive; }
    bool isFxUsed (FxSlot s) const noexcept { return graph.fxUsed[size_t (s)]; }

    // Audio-thread cost, for the performance tests and the diagnostics view.
    struct Stats { float lastBlockMicros = 0.0f, peakBlockMicros = 0.0f; int blocks = 0; };
    Stats getStats() const noexcept;
    void resetStats() noexcept { peakMicros.store (0.0f); blockCount.store (0); }

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
    std::array<Bus, int (MixBus::Count)> buses;
    std::array<Fx, int (FxSlot::Count)> fx;
    Monitor monitor;
    // Resolved once per publish, so the block itself only multiplies.
    bool monitorSoloActive = false;         // something is soloed
    bool monitorPfl = false;                // tap before the fader
    bool monitorRouted = false;             // a feed actually carries it: with none, the whole monitor path is skipped
    MixBus monitorSource = MixBus::Master;  // what it carries with nothing soloed

    TripleBuffer<MixParameters> mailbox;
    MixParameters applied;                                       // audio thread's copy of the last snapshot
    TripleBuffer<OutputFeeds> feedMailbox;
    OutputFeeds appliedFeeds;                                    // audio thread's copy of the output routing
    std::array<float, kMaxOutputFeeds> feedGain { { 1.0f, 1.0f, 1.0f, 1.0f } };   // dB resolved once per publish
    bool haveApplied = false;

    std::atomic<MixTap*> tap { nullptr };
    std::atomic<float> lastMicros { 0.0f }, peakMicros { 0.0f };
    std::atomic<int> blockCount { 0 };
};

} // namespace livemix
