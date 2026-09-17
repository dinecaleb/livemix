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
    // Solo is a switch an engineer flicks while the service is running. 8 ms is short enough
    // to feel instant and long enough that a headphone amp never hears a step.
    constexpr float kSoloSmoothMs = 8.0f;
    constexpr int kMaxDeviceInputs = 128;
    constexpr int kMaxDeviceOutputs = kMaxOutputs;

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
        s->monitorGain.prepare (sr, kSoloSmoothMs);
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
        bus.monitorGain.prepare (sr, kSoloSmoothMs);
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
        slot.monitorGain.prepare (sr, kSoloSmoothMs);
        for (int ch = 0; ch < 2; ++ch)
        {
            slot.buffer[size_t (ch)].assign (size_t (maxBlock), 0.0f);
            slot.ptrs[size_t (ch)] = slot.buffer[size_t (ch)].data();
        }
    }

    monitor.gain.prepare (sr, kGainSmoothMs);
    for (int ch = 0; ch < 2; ++ch)
    {
        monitor.buffer[size_t (ch)].assign (size_t (maxBlock), 0.0f);
        monitor.ptrs[size_t (ch)] = monitor.buffer[size_t (ch)].data();
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

    // Solo. By default it feeds the engineer's monitor bus and the main mix never hears
    // about it (MonitorBus.h): a vocal can be picked apart in headphones while the broadcast
    // carries on. SoloMode::InPlace is the old destructive behaviour - mute-unless-soloed on
    // the main mix - and is only ever on because somebody chose it.
    bool anySolo = false;
    for (int i = 0; i < n; ++i)
        if (p.strips[size_t (i)].solo) { anySolo = true; break; }
    if (! anySolo)
        for (int b = 0; b < int (MixBus::Master); ++b)
            if (graph.busUsed[size_t (b)] && p.buses[size_t (b)].solo) { anySolo = true; break; }
    if (! anySolo)
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (graph.fxUsed[size_t (f)] && p.fx[size_t (f)].solo) { anySolo = true; break; }

    monitorSoloActive = anySolo;
    monitorPfl = p.monitor.point == SoloPoint::PFL;
    monitorSource = p.monitor.source;
    // Solo only silences the main mix when the engineer asked for solo-in-place. This one
    // line is what keeps the broadcast safe, so it is written once and read everywhere below.
    const bool soloAffectsMix = anySolo && p.monitor.mode == SoloMode::InPlace;

    std::array<bool, int (MixBus::Count)> busHasSoloedStrip {};
    for (int i = 0; i < n; ++i)
        if (p.strips[size_t (i)].solo)
            busHasSoloedStrip[size_t (strips[size_t (i)]->bus)] = true;

    monitor.gain.setTarget (p.monitor.mute ? 0.0f : dbToGain (clamp (p.monitor.effectiveGainDb(), -60.0f, 12.0f)));
    if (! haveApplied) monitor.gain.snapToTarget();

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
        const bool busSolo = p.buses[size_t (s.bus)].solo;
        const bool silenced = sp.mute || (soloAffectsMix && ! sp.solo && ! busSolo);
        const float g = silenced ? 0.0f : dbToGain (sp.faderDb);
        s.monitorGain.setTarget (sp.solo ? 1.0f : 0.0f);
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
            s.monitorGain.snapToTarget();
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

        bool silenced = bp.mute;
        if (MixBus (b) != MixBus::Master && soloAffectsMix)
            silenced = silenced || (! bp.solo && ! busHasSoloedStrip[size_t (b)]);
        bus.gain.setTarget (silenced ? 0.0f : dbToGain (bp.faderDb));
        bus.monitorGain.setTarget (MixBus (b) != MixBus::Master && bp.solo && graph.busUsed[size_t (b)] ? 1.0f : 0.0f);
        if (! haveApplied) { bus.gain.snapToTarget(); bus.monitorGain.snapToTarget(); }
    }
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        auto& slot = fx[size_t (f)];
        const auto& fp = p.fx[size_t (f)];
        FxParameters params = fp.fx;
        params.mix = 1.0f;
        params.bypassAll = false;
        slot.chain.setParameters (params);
        slot.chain.setTempo (double (p.tempoBpm));   // a synced delay is only in time if the tempo is
        // The FX group's fader and mute ride every used return together, at the one place the
        // return's gain is already decided, so BYPASS and an unused slot still win.
        slot.returnGain.setTarget (fp.enabled && ! p.fxMute && ! p.bypassProcessing && graph.fxUsed[size_t (f)]
                                       ? dbToGain (fp.returnDb + p.fxReturnDb) : 0.0f);
        slot.monitorGain.setTarget (fp.solo && graph.fxUsed[size_t (f)] ? 1.0f : 0.0f);
        if (! haveApplied) { slot.returnGain.snapToTarget(); slot.monitorGain.snapToTarget(); }
    }
}

void MixEngine::setOutputFeeds (const OutputFeeds& f)
{
    auto& slot = feedMailbox.beginWrite();
    slot = f;
    slot.count = clamp (slot.count, 1, kMaxOutputFeeds);
    feedMailbox.publish();
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
    if (feedMailbox.hasNew())
    {
        appliedFeeds = feedMailbox.acquire();
        // Resolve the levels once here, so the block itself is only multiplies.
        for (int f = 0; f < kMaxOutputFeeds; ++f)
            feedGain[size_t (f)] = dbToGain (clamp (appliedFeeds.feeds[size_t (f)].gainDb, -60.0f, 12.0f));
        // With no feed carrying the engineer's listen there is nothing to build, and the whole
        // monitor path costs a session that never uses it exactly nothing.
        monitorRouted = false;
        for (int f = 0, fn = clamp (appliedFeeds.count, 1, kMaxOutputFeeds); f < fn; ++f)
            if (appliedFeeds.feeds[size_t (f)].monitor) { monitorRouted = true; break; }
    }

    if (numInputs > kMaxDeviceInputs) numInputs = kMaxDeviceInputs;
    if (numOutputs > kMaxDeviceOutputs) numOutputs = kMaxDeviceOutputs;
    std::array<const float*, kMaxDeviceInputs> in {};
    std::array<float*, kMaxDeviceOutputs> out {};

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        for (int i = 0; i < numInputs; ++i) in[size_t (i)] = inputs[i] + offset;
        // A device can hand us a null pointer for a channel it is not really driving.
        for (int o = 0; o < numOutputs; ++o) out[size_t (o)] = outputs[o] != nullptr ? outputs[o] + offset : nullptr;

        MixTap* t = tap.load (std::memory_order_acquire);
        const bool listening = t != nullptr && t->isActive();

        for (int b = 0; b < int (MixBus::Count); ++b)
            if (graph.busUsed[size_t (b)])
                for (int ch = 0; ch < 2; ++ch) std::memset (buses[size_t (b)].ptrs[size_t (ch)], 0, sizeof (float) * size_t (n));
        for (int f = 0; f < int (FxSlot::Count); ++f)
            if (graph.fxUsed[size_t (f)])
                for (int ch = 0; ch < 2; ++ch) std::memset (fx[size_t (f)].ptrs[size_t (ch)], 0, sizeof (float) * size_t (n));
        if (monitorRouted)
            for (int ch = 0; ch < 2; ++ch) std::memset (monitor.ptrs[size_t (ch)], 0, sizeof (float) * size_t (n));

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

            // ---- the engineer's listen ----
            // PFL taps the channel before its fader, so a muted channel is still audible in
            // headphones: that is the point of a pre-fade listen. AFL takes it where it sits in
            // the mix. Either way nothing here is added to a bus, so the broadcast cannot hear it.
            if (monitorRouted)
            {
                const bool ramping = s.monitorGain.isSmoothing();
                const float mg = s.monitorGain.getCurrent();
                if (ramping || mg != 0.0f)
                {
                    const float al = monitorPfl ? 1.0f : s.gainL.getCurrent();
                    const float ar = monitorPfl ? 1.0f : s.gainR.getCurrent();
                    float* ml = monitor.ptrs[0];
                    float* mr = monitor.ptrs[1];
                    if (ramping)
                    {
                        for (int k = 0; k < n; ++k)
                        {
                            const float m = s.monitorGain.next();
                            ml[k] += xl[k] * al * m;
                            mr[k] += xr[k] * ar * m;
                        }
                    }
                    else
                    {
                        if (al != 0.0f) addScaled (ml, xl, al * mg, n);
                        if (ar != 0.0f) addScaled (mr, xr, ar * mg, n);
                    }
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
            // A soloed group goes to the engineer's listen and nowhere else. AFL takes it at
            // its fader (where it sits in the mix), PFL at unity (what the group sounds like).
            if (monitorRouted)
            {
                const bool ramping = bus.monitorGain.isSmoothing();
                const float mg = bus.monitorGain.getCurrent();
                if (ramping || mg != 0.0f)
                {
                    const float a = monitorPfl ? 1.0f : bus.gain.getCurrent();
                    if (ramping)
                    {
                        for (int k = 0; k < n; ++k)
                        {
                            const float m = bus.monitorGain.next() * a;
                            monitor.ptrs[0][k] += bus.ptrs[0][k] * m;
                            monitor.ptrs[1][k] += bus.ptrs[1][k] * m;
                        }
                    }
                    else if (a != 0.0f)
                    {
                        addScaled (monitor.ptrs[0], bus.ptrs[0], a * mg, n);
                        addScaled (monitor.ptrs[1], bus.ptrs[1], a * mg, n);
                    }
                }
            }
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
            if (monitorRouted)
            {
                const bool ramping = slot.monitorGain.isSmoothing();
                const float mg = slot.monitorGain.getCurrent();
                if (ramping || mg != 0.0f)
                {
                    const float a = monitorPfl ? 1.0f : slot.returnGain.getCurrent();
                    if (ramping)
                    {
                        for (int k = 0; k < n; ++k)
                        {
                            const float m = slot.monitorGain.next() * a;
                            monitor.ptrs[0][k] += slot.ptrs[0][k] * m;
                            monitor.ptrs[1][k] += slot.ptrs[1][k] * m;
                        }
                    }
                    else if (a != 0.0f)
                    {
                        addScaled (monitor.ptrs[0], slot.ptrs[0], a * mg, n);
                        addScaled (monitor.ptrs[1], slot.ptrs[1], a * mg, n);
                    }
                }
            }
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

        // ---- the monitor bus ----
        // With nothing soloed the engineer's listen carries whatever it is set to follow -
        // normally the finished mix - so the headphones are useful before anybody presses S.
        // Then its own level, dim and mute, which exist nowhere near the master.
        if (monitorRouted)
        {
            if (! monitorSoloActive)
            {
                const float* fl = nullptr;
                const float* fr = nullptr;
                if (monitorSource == MixBus::Master) { fl = master.ptrs[0]; fr = master.ptrs[1]; }
                else if (monitorSource >= MixBus::Drums && monitorSource < MixBus::Master
                         && graph.busUsed[size_t (monitorSource)])
                {
                    const Bus& b = buses[size_t (monitorSource)];
                    fl = b.ptrs[0]; fr = b.ptrs[1];
                }
                if (fl != nullptr)
                {
                    std::memcpy (monitor.ptrs[0], fl, sizeof (float) * size_t (n));
                    std::memcpy (monitor.ptrs[1], fr, sizeof (float) * size_t (n));
                }
            }
            if (monitor.gain.isSmoothing())
            {
                for (int k = 0; k < n; ++k)
                {
                    const float g = monitor.gain.next();
                    monitor.ptrs[0][k] *= g;
                    monitor.ptrs[1][k] *= g;
                }
            }
            else
            {
                const float g = monitor.gain.getCurrent();
                if (g != 1.0f) for (int k = 0; k < n; ++k) { monitor.ptrs[0][k] *= g; monitor.ptrs[1][k] *= g; }
            }
        }

        // ---- Outputs: every feed lands on its own pair of device channels ----
        for (int o = 0; o < numOutputs; ++o)
            if (out[size_t (o)] != nullptr) std::memset (out[size_t (o)], 0, sizeof (float) * size_t (n));

        const int feeds = clamp (appliedFeeds.count, 1, kMaxOutputFeeds);
        for (int f = 0; f < feeds; ++f)
        {
            const auto& feed = appliedFeeds.feeds[size_t (f)];
            if (feed.mute) continue;

            // What this feed carries. A group bus is post-processing, so its own fader is
            // applied here the way the master sum applies it.
            const float* srcL = nullptr;
            const float* srcR = nullptr;
            float gain = feedGain[size_t (f)];
            if (feed.monitor)
            {
                // The engineer's listen. Never the master, never a bus: whatever is soloed.
                srcL = monitor.ptrs[0];
                srcR = monitor.ptrs[1];
            }
            else if (feed.source == MixBus::Master)
            {
                srcL = master.ptrs[0];
                srcR = master.ptrs[1];
            }
            else if (feed.source >= MixBus::Drums && feed.source < MixBus::Master
                     && graph.busUsed[size_t (feed.source)])
            {
                const Bus& bus = buses[size_t (feed.source)];
                srcL = bus.ptrs[0];
                srcR = bus.ptrs[1];
                gain *= bus.gain.getCurrent();
            }
            if (srcL == nullptr) continue;

            const int l = feed.left, r = feed.right;
            const bool hasL = l >= 0 && l < numOutputs && out[size_t (l)] != nullptr;
            const bool hasR = r >= 0 && r < numOutputs && out[size_t (r)] != nullptr;
            if (! hasL && ! hasR) continue;

            if (feed.mono || (hasL != hasR))
            {
                const float half = gain * 0.5f;
                for (int k = 0; k < n; ++k)
                {
                    const float mono = (srcL[k] + srcR[k]) * half;
                    if (hasL) out[size_t (l)][k] += mono;
                    if (hasR && r != l) out[size_t (r)][k] += mono;
                }
            }
            else
            {
                for (int k = 0; k < n; ++k)
                {
                    out[size_t (l)][k] += srcL[k] * gain;
                    out[size_t (r)][k] += srcR[k] * gain;
                }
            }
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
