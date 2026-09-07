#include "MixEngine.h"
#include "Core/DbUtils.h"
#include "Core/Denormals.h"
#include <chrono>
#include <cmath>
#include <cstring>

namespace livemix
{

namespace
{
    constexpr float kGainSmoothMs = 20.0f;
    constexpr int kMaxDeviceInputs = 128;
    constexpr int kMaxDeviceOutputs = 8;

    inline void addScaled (float* dest, const float* src, float g, int n) noexcept
    {
        for (int i = 0; i < n; ++i) dest[i] += src[i] * g;
    }
}

MixEngine::MixEngine() = default;
MixEngine::~MixEngine() = default;

void MixEngine::panGains (float pan, bool stereo, float& l, float& r) noexcept
{
    const float p = clamp (pan, -1.0f, 1.0f);
    const float angle = (p + 1.0f) * 0.25f * float (M_PI);   // 0 .. pi/2
    l = std::cos (angle);
    r = std::sin (angle);
    if (stereo)
    {
        // Balance: centre is unity on both sides, the far side is turned down.
        l = std::min (1.0f, l * float (M_SQRT2));
        r = std::min (1.0f, r * float (M_SQRT2));
    }
}

void MixEngine::prepare (double sampleRate, int maxBlockSize, const MixSession& newSession)
{
    sr = sampleRate;
    maxBlock = maxBlockSize;
    session = newSession;
    graph = RoutingGraph::build (session);
    numStrips = graph.numStrips();

    strips.clear();
    strips.reserve (size_t (numStrips));
    for (int i = 0; i < numStrips; ++i)
    {
        const auto& route = graph.strips[size_t (i)];
        auto s = std::make_unique<Strip>();
        s->inputA = route.inputA;
        s->inputB = route.inputB;
        s->channels = route.numChannels();
        s->bus = route.bus;
        ChannelProcessor::Options o;
        o.widthMeter = s->channels == 2;
        s->processor.configure (o);
        s->processor.prepare (sr, maxBlock, s->channels);
        s->inputGain.prepare (sr, kGainSmoothMs);
        s->gainL.prepare (sr, kGainSmoothMs);
        s->gainR.prepare (sr, kGainSmoothMs);
        for (auto& sm : s->send) sm.prepare (sr, kGainSmoothMs);
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            s->scratch[size_t (ch)].assign (size_t (maxBlock), 0.0f);
            s->ptrs[size_t (ch)] = s->scratch[size_t (ch)].data();
        }
        strips.push_back (std::move (s));
    }

    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        auto& bus = buses[size_t (b)];
        ChannelProcessor::Options o;
        o.widthMeter = true;
        if (MixBus (b) == MixBus::Master) { o.limiter = true; o.loudnessMeter = true; }
        bus.processor.configure (o);
        bus.processor.prepare (sr, maxBlock, 2);
        bus.gain.prepare (sr, kGainSmoothMs);
        for (int ch = 0; ch < 2; ++ch)
        {
            bus.buffer[size_t (ch)].assign (size_t (maxBlock), 0.0f);
            bus.ptrs[size_t (ch)] = bus.buffer[size_t (ch)].data();
        }
    }

    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        auto& slot = fx[size_t (f)];
        slot.chain.prepare (sr, maxBlock, 2);
        slot.returnGain.prepare (sr, kGainSmoothMs);
        for (int ch = 0; ch < 2; ++ch)
        {
            slot.buffer[size_t (ch)].assign (size_t (maxBlock), 0.0f);
            slot.ptrs[size_t (ch)] = slot.buffer[size_t (ch)].data();
        }
    }

    // The mix starts on the profile baselines; the audio thread picks this up on its first block.
    haveApplied = false;
    applied = startingPoint (session, graph);
    applyParameters (applied);
    haveApplied = true;
    mailbox.beginWrite() = applied;
    mailbox.publish();
    resetStats();
}

void MixEngine::reset() noexcept
{
    for (auto& s : strips) s->processor.reset();
    for (auto& b : buses) b.processor.reset();
    for (auto& f : fx) f.chain.reset();
}

void MixEngine::setParameters (const MixParameters& p)
{
    mailbox.beginWrite() = p;
    mailbox.publish();
}

void MixEngine::applyParameters (const MixParameters& p) noexcept
{
    const int n = p.numStrips < numStrips ? p.numStrips : numStrips;
    for (int i = 0; i < n; ++i)
    {
        auto& s = *strips[size_t (i)];
        const auto& sp = p.strips[size_t (i)];
        if (p.bypassProcessing)
        {
            ChannelParameters raw = sp.channel;
            raw.bypassAll = true;
            raw.abLoudnessMatch = false;
            s.processor.setParameters (raw);
        }
        else
            s.processor.setParameters (sp.channel);

        s.inputGain.setTarget (dbToGain (sp.inputGainDb));
        const float g = sp.mute ? 0.0f : dbToGain (sp.faderDb);
        float pl, pr;
        panGains (sp.pan, s.channels == 2, pl, pr);
        s.gainL.setTarget (g * pl);
        s.gainR.setTarget (g * pr);
        for (int f = 0; f < int (FxSlot::Count); ++f)
            s.send[size_t (f)].setTarget (sp.sendDb[size_t (f)] > kSilenceDb && graph.fxUsed[size_t (f)] ? dbToGain (sp.sendDb[size_t (f)]) : 0.0f);
        if (! haveApplied)
        {
            s.inputGain.snapToTarget();
            s.gainL.snapToTarget(); s.gainR.snapToTarget();
            for (auto& sm : s.send) sm.snapToTarget();
        }
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        auto& bus = buses[size_t (b)];
        const auto& bp = p.buses[size_t (b)];
        if (p.bypassProcessing)
        {
            ChannelParameters raw = bp.channel;
            raw.bypassAll = true;
            raw.abLoudnessMatch = false;
            bus.processor.setParameters (raw);
        }
        else
            bus.processor.setParameters (bp.channel);
        bus.gain.setTarget (bp.mute ? 0.0f : dbToGain (bp.faderDb));
        if (! haveApplied) bus.gain.snapToTarget();
    }
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        auto& slot = fx[size_t (f)];
        const auto& fp = p.fx[size_t (f)];
        FxParameters params = fp.fx;
        params.mix = 1.0f;
        params.bypassAll = false;
        slot.chain.setParameters (params);
        slot.returnGain.setTarget (fp.enabled && ! p.bypassProcessing && graph.fxUsed[size_t (f)] ? dbToGain (fp.returnDb) : 0.0f);
        if (! haveApplied) slot.returnGain.snapToTarget();
    }
}

void MixEngine::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept
{
    ScopedNoDenormals noDenormals;
    const auto start = std::chrono::steady_clock::now();

    if (mailbox.hasNew())
    {
        applied = mailbox.acquire();
        applyParameters (applied);
    }

    if (numInputs > kMaxDeviceInputs) numInputs = kMaxDeviceInputs;
    if (numOutputs > kMaxDeviceOutputs) numOutputs = kMaxDeviceOutputs;
    std::array<const float*, kMaxDeviceInputs> in {};
    std::array<float*, kMaxDeviceOutputs> out {};

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        for (int i = 0; i < numInputs; ++i) in[size_t (i)] = inputs[i] + offset;
        for (int o = 0; o < numOutputs; ++o) out[size_t (o)] = outputs[o] + offset;

        MixTap* t = tap.load (std::memory_order_acquire);
        const bool listening = t != nullptr && t->isActive();

        for (int b = 0; b < int (MixBus::Count); ++b)
            if (graph.busUsed[size_t (b)])
                for (int ch = 0; ch < 2; ++ch) std::memset (buses[size_t (b)].ptrs[size_t (ch)], 0, sizeof (float) * size_t (n));
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (graph.fxUsed[size_t (f)])
                for (int ch = 0; ch < 2; ++ch) std::memset (fx[size_t (f)].ptrs[size_t (ch)], 0, sizeof (float) * size_t (n));

        // ---- Strips ----
        for (int i = 0; i < numStrips; ++i)
        {
            Strip& s = *strips[size_t (i)];
            for (int ch = 0; ch < s.channels; ++ch)
            {
                const int idx = ch == 0 ? s.inputA : s.inputB;
                if (idx >= 0 && idx < numInputs) std::memcpy (s.ptrs[size_t (ch)], in[size_t (idx)], sizeof (float) * size_t (n));
                else std::memset (s.ptrs[size_t (ch)], 0, sizeof (float) * size_t (n));
            }
            // Digital preamp: before the listen tap, so Tune measures what the chain will receive.
            if (s.inputGain.isSmoothing())
            {
                for (int k = 0; k < n; ++k)
                {
                    const float g = s.inputGain.next();
                    for (int ch = 0; ch < s.channels; ++ch) s.ptrs[size_t (ch)][k] *= g;
                }
            }
            else
            {
                const float g = s.inputGain.getCurrent();
                if (g != 1.0f)
                    for (int ch = 0; ch < s.channels; ++ch)
                        for (int k = 0; k < n; ++k) s.ptrs[size_t (ch)][k] *= g;
            }
            AudioBlockView view { s.ptrs.data(), s.channels, n };
            if (listening) t->pushStripInput (i, view);
            s.processor.process (view);
            if (listening) t->pushStripProcessed (i, view);

            Bus& bus = buses[size_t (s.bus)];
            const float* xl = s.ptrs[0];
            const float* xr = s.channels == 2 ? s.ptrs[1] : s.ptrs[0];
            float* bl = bus.ptrs[0];
            float* br = bus.ptrs[1];

            bool smoothing = s.gainL.isSmoothing() || s.gainR.isSmoothing();
            for (int f = 0; f < int (FxSlot::Count) && ! smoothing; ++f)
                if (graph.fxUsed[size_t (f)] && s.send[size_t (f)].isSmoothing()) smoothing = true;

            if (smoothing)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float l = xl[k] * s.gainL.next();
                    const float r = xr[k] * s.gainR.next();
                    bl[k] += l;
                    br[k] += r;
                    for (int f = 0; f < int (FxSlot::Count); ++f)
                    {
                        if (! graph.fxUsed[size_t (f)]) continue;
                        const float sg = s.send[size_t (f)].next();
                        fx[size_t (f)].ptrs[0][k] += l * sg;
                        fx[size_t (f)].ptrs[1][k] += r * sg;
                    }
                }
            }
            else
            {
                const float gl = s.gainL.getCurrent(), gr = s.gainR.getCurrent();
                if (gl != 0.0f) addScaled (bl, xl, gl, n);
                if (gr != 0.0f) addScaled (br, xr, gr, n);
                for (int f = 0; f < int (FxSlot::Count); ++f)
                {
                    if (! graph.fxUsed[size_t (f)]) continue;
                    const float sg = s.send[size_t (f)].getCurrent();
                    if (sg == 0.0f) continue;
                    if (gl != 0.0f) addScaled (fx[size_t (f)].ptrs[0], xl, gl * sg, n);
                    if (gr != 0.0f) addScaled (fx[size_t (f)].ptrs[1], xr, gr * sg, n);
                }
            }
        }

        // ---- Buses -> master ----
        Bus& master = buses[size_t (MixBus::Master)];
        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            if (MixBus (b) == MixBus::Master || ! graph.busUsed[size_t (b)]) continue;
            Bus& bus = buses[size_t (b)];
            AudioBlockView view { bus.ptrs.data(), 2, n };
            if (listening) t->pushBus (MixBus (b), view);
            bus.processor.process (view);
            if (bus.gain.isSmoothing())
            {
                for (int k = 0; k < n; ++k)
                {
                    const float g = bus.gain.next();
                    master.ptrs[0][k] += bus.ptrs[0][k] * g;
                    master.ptrs[1][k] += bus.ptrs[1][k] * g;
                }
            }
            else
            {
                const float g = bus.gain.getCurrent();
                if (g != 0.0f) { addScaled (master.ptrs[0], bus.ptrs[0], g, n); addScaled (master.ptrs[1], bus.ptrs[1], g, n); }
            }
        }

        // ---- FX returns -> master ----
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (! graph.fxUsed[size_t (f)]) continue;
            Fx& slot = fx[size_t (f)];
            AudioBlockView view { slot.ptrs.data(), 2, n };
            slot.chain.process (view);
            if (slot.returnGain.isSmoothing())
            {
                for (int k = 0; k < n; ++k)
                {
                    const float g = slot.returnGain.next();
                    master.ptrs[0][k] += slot.ptrs[0][k] * g;
                    master.ptrs[1][k] += slot.ptrs[1][k] * g;
                }
            }
            else
            {
                const float g = slot.returnGain.getCurrent();
                if (g != 0.0f) { addScaled (master.ptrs[0], slot.ptrs[0], g, n); addScaled (master.ptrs[1], slot.ptrs[1], g, n); }
            }
        }

        // ---- Master: fader before the chain so the limiter always holds the ceiling ----
        if (master.gain.isSmoothing())
        {
            for (int k = 0; k < n; ++k)
            {
                const float g = master.gain.next();
                master.ptrs[0][k] *= g;
                master.ptrs[1][k] *= g;
            }
        }
        else
        {
            const float g = master.gain.getCurrent();
            if (g != 1.0f) for (int k = 0; k < n; ++k) { master.ptrs[0][k] *= g; master.ptrs[1][k] *= g; }
        }
        AudioBlockView masterView { master.ptrs.data(), 2, n };
        if (listening) t->pushBus (MixBus::Master, masterView);
        master.processor.process (masterView);
        if (listening) t->pushMasterOutput (masterView);

        if (numOutputs >= 2)
        {
            std::memcpy (out[0], master.ptrs[0], sizeof (float) * size_t (n));
            std::memcpy (out[1], master.ptrs[1], sizeof (float) * size_t (n));
            for (int o = 2; o < numOutputs; ++o) std::memset (out[size_t (o)], 0, sizeof (float) * size_t (n));
        }
        else if (numOutputs == 1)
        {
            for (int k = 0; k < n; ++k) out[0][k] = 0.5f * (master.ptrs[0][k] + master.ptrs[1][k]);
        }
    }

    const auto micros = float (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count());
    lastMicros.store (micros, std::memory_order_relaxed);
    if (micros > peakMicros.load (std::memory_order_relaxed)) peakMicros.store (micros, std::memory_order_relaxed);
    blockCount.fetch_add (1, std::memory_order_relaxed);
}

int MixEngine::getLatencySamples() const noexcept
{
    return buses[size_t (MixBus::Master)].processor.getLatencySamples();
}

MixEngine::Stats MixEngine::getStats() const noexcept
{
    Stats s;
    s.lastBlockMicros = lastMicros.load (std::memory_order_relaxed);
    s.peakBlockMicros = peakMicros.load (std::memory_order_relaxed);
    s.blocks = blockCount.load (std::memory_order_relaxed);
    return s;
}

} // namespace livemix
