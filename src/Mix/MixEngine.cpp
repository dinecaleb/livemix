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

    // NOT A NUMBER NEVER REACHES THE MIX. One NaN out of a stage - a filter handed a value
    // nothing should have let through, a sound that decoded badly - is otherwise latched in
    // that stage's state and summed into the bus, the master, the reverb's feedback and the
    // limiter's delay line, and the broadcast carries it until the next prepare. Multiplying
    // by zero keeps a NaN or an infinity and turns every number into 0, so one comparison at
    // the end answers for the whole block, and a block that fails is silenced and its
    // processor reset: one channel drops out for a block instead of the whole service.
    inline bool allFinite (const AudioBlockView& v) noexcept
    {
        float acc = 0.0f;
        for (int ch = 0; ch < v.numChannels; ++ch)
            for (int i = 0; i < v.numSamples; ++i) acc += v.channels[ch][i] * 0.0f;
        return acc == 0.0f;
    }

    template <typename P>
    inline void guard (const AudioBlockView& v, P& processor, std::atomic<int>& caught) noexcept
    {
        if (allFinite (v)) return;
        for (int ch = 0; ch < v.numChannels; ++ch)
            std::memset (v.channels[ch], 0, sizeof (float) * size_t (v.numSamples));
        processor.reset();
        caught.fetch_add (1, std::memory_order_relaxed);
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
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        auto pole = [fs] (double hz) { return float (1.0 - std::exp (-2.0 * 3.14159265358979 * hz / fs)); };
        auto time = [fs] (double ms) { return float (1.0 - std::exp (-1.0 / (fs * ms * 0.001))); };
        speechLowCoeff = pole (150.0);      // below this is rumble and the kick, not a voice
        speechHighCoeff = pole (4000.0);    // above this is cymbals and sibilance, not a voice
        speechEnvUp = time (15.0);
        speechEnvDown = time (120.0);
        speechLow = speechLow2 = speechHigh = speechHigh2 = speechEnv = 0.0f;
        speechVoiced = false;
    }
    maxBlock = maxBlockSize;
    session = newSession;
    graph = RoutingGraph::build (session);
    numStrips = graph.numStrips();

    kitTriggers = KitTriggerTable {};
    samplePosition = 0;
    broadcastGain.prepare (sr, kGainSmoothMs);
    broadcastGain.snapTo (1.0f);
    broadcastRamp.assign (size_t (maxBlock), 1.0f);
    duckScratch.assign (size_t (maxBlock), 1.0f);
    speechDuckGain = 1.0f;
    speechHoldLeft = 0.0f;
    speechWasOpen = false;
    speechDuckDb.store (0.0f, std::memory_order_relaxed);
    auditionPlayer.prepare (sr);
    auditionRequest.store (nullptr, std::memory_order_relaxed);
    strips.clear();
    strips.reserve (size_t (numStrips));
    // The order strips are processed in (see process()): kicks, then snares, then the rest.
    stripOrder.clear();
    stripOrder.reserve (size_t (numStrips));
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 0; i < numStrips; ++i)
        {
            const auto f = roleFamily (graph.strips[size_t (i)].role);
            const int rank = f == RoleFamily::Kick ? 0 : f == RoleFamily::Snare ? 1 : 2;
            if (rank == pass) stripOrder.push_back (i);
        }
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
        o.sampleReplacement = sampleReplacementAppropriate (roleFamily (route.role));   // kick, snare, toms
        s->processor.configure (o);
        if (o.sampleReplacement) s->processor.setKit (&kitTriggers, roleFamily (route.role));
        s->processor.prepare (sr, maxBlock, s->channels);
        s->inputGain.prepare (sr, kGainSmoothMs);
        s->gainL.prepare (sr, kGainSmoothMs);
        s->gainR.prepare (sr, kGainSmoothMs);
        s->monitorGain.prepare (sr, kSoloSmoothMs);
        for (auto& sm : s->send) sm.prepare (sr, kGainSmoothMs);
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            s->scratch[size_t (ch)].assign (size_t (maxBlock), 0.0f);
            s->preAuto[size_t (ch)].assign (size_t (maxBlock), 0.0f);
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

    // The mix starts on the profile baselines - unless one was running, in which case the
    // things that must never leak across a device restart come with it: MUTE and DIM on the
    // broadcast, and, on the same strips, every mute. A new layout keeps only what is not one
    // input's. And the first mix the controller publishes after this lands at once rather than
    // ramping from the baseline, so nothing that was muted is heard for the 20 ms of a ramp.
    haveApplied = false;
    applied = startingPoint (session, graph);
    if (hasPublished)
    {
        applied.broadcastMute = lastPublished.broadcastMute;
        applied.broadcastDim = lastPublished.broadcastDim;
        applied.monitor = lastPublished.monitor;
        for (int b = 0; b < int (MixBus::Count); ++b) applied.buses[size_t (b)].mute = lastPublished.buses[size_t (b)].mute;
        applied.fxMute = lastPublished.fxMute;
        if (lastPublished.numStrips == applied.numStrips)
            for (int i = 0; i < applied.numStrips; ++i) applied.strips[size_t (i)].mute = lastPublished.strips[size_t (i)].mute;
    }
    snapNextApply = true;
    feedsSettled = false;
    for (int f = 0; f < kMaxOutputFeeds; ++f)
    {
        const auto& fd = appliedFeeds.feeds[size_t (f)];
        feedLevel[size_t (f)].prepare (sr, kGainSmoothMs);
        feedLevel[size_t (f)].snapTo (fd.mute ? 0.0f : dbToGain (clamp (fd.gainDb, -60.0f, 12.0f)));
    }
    feedRamp.assign (size_t (maxBlock), 0.0f);
    applyParameters (applied);
    haveApplied = true;
    mailbox.beginWrite() = applied;
    mailbox.publish();
    resetStats();
}

void MixEngine::reset() noexcept
{
    for (auto& s : strips)
    {
        s->processor.reset();
        s->autoEnv = 0.0f; s->autoGain = 1.0f; s->autoTarget = 1.0f;
        s->autoGainDb.store (0.0f, std::memory_order_relaxed);
    }
    for (auto& b : buses) b.processor.reset();
    for (auto& f : fx) f.chain.reset();
}

void MixEngine::setParameters (const MixParameters& p)
{
    lastPublished = p;
    hasPublished = true;
    mailbox.beginWrite() = p;
    mailbox.publish();
}

void MixEngine::applyParameters (const MixParameters& p) noexcept
{
    // Speech priority: the coefficients, once, here - never in process().
    {
        const auto& sd = p.speechDuck;
        auto coeffFor = [this] (float ms) { return 1.0f - std::exp (-1.0f / std::max (1.0f, float (sr) * 0.001f * std::max (1.0f, ms))); };
        speechThresholdLin = dbToGain (sd.thresholdDb);
        speechDepthGain = dbToGain (-std::fabs (sd.depthDb));
        speechAttackCoeff = coeffFor (sd.attackMs);
        speechReleaseCoeff = coeffFor (sd.releaseMs);
        speechOffCoeff = coeffFor (800.0f);            // switched off mid-service: back at a release, never a step
        speechHoldSamples = sd.holdMs * 0.001f * float (sr);
    }
    // Share the mics: the same - coefficients here, never in process().
    {
        const auto& am = p.autoMix;
        auto coeffFor = [this] (float ms) { return 1.0f - std::exp (-1.0f / std::max (1.0f, float (sr) * 0.001f * std::max (1.0f, ms))); };
        autoOn = am.enabled;
        const float t = dbToGain (am.thresholdDb);
        autoThresholdPow = t * t;
        autoDepthGain = dbToGain (-std::fabs (am.depthDb));
        autoAttackCoeff = coeffFor (am.attackMs);
        autoReleaseCoeff = coeffFor (am.releaseMs);
        autoEnvUp = coeffFor (5.0f);          // the detector: a syllable's onset ...
        autoEnvDown = coeffFor (90.0f);       // ... and long enough not to follow every vowel down
        for (int i = 0; i < numStrips && i < kMaxStrips; ++i)
            strips[size_t (i)]->autoMember = am.member[size_t (i)];
    }
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
    broadcastGain.setTarget (p.broadcastMute ? 0.0f : p.broadcastDim ? dbToGain (-20.0f) : 1.0f);
    if (! haveApplied) broadcastGain.snapToTarget();

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
        if (s.processor.getOptions().sampleReplacement)
        {
            const SampleBankTable* table = sampleBanks.load (std::memory_order_acquire);
            s.processor.setSampleBank (table != nullptr ? table->bank (roleFamily (graph.strips[size_t (i)].role), sp.channel.replaceSound) : nullptr);
        }

        s.inputGain.setTarget (dbToGain (sp.inputGainDb));
        const bool busSolo = p.buses[size_t (s.bus)].solo;
        const bool silenced = sp.mute || (soloAffectsMix && ! sp.solo && ! busSolo);
        const float g = silenced ? 0.0f : dbToGain (sp.faderDb);
        s.monitorGain.setTarget (sp.solo ? 1.0f : 0.0f);
        float pl, pr;
        panGains (sp.pan, s.channels == 2, pl, pr);
        s.gainL.setTarget (g * pl);
        s.gainR.setTarget (g * pr);
        // EFFECTS OFF on a channel holds every send at silence without touching the levels,
        // so the way back is one press. It is a smoothed target like any other, so the
        // reverb this channel was feeding rings out rather than stopping dead.
        // A MUTED GROUP IS SILENT, effects included. The returns go straight to the master, so
        // without this a muted BGV group went on singing through the plate - which is exactly
        // what somebody reaching for the group mute did not want.
        const bool groupMuted = p.buses[size_t (s.bus)].mute;
        for (int f = 0; f < int (FxSlot::Count); ++f)
            s.send[size_t (f)].setTarget (! sp.effectsOff && ! groupMuted && sp.sendDb[size_t (f)] > kSilenceDb && graph.fxUsed[size_t (f)]
                                              ? dbToGain (sp.sendDb[size_t (f)]) : 0.0f);
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
        if (p.bypassProcessing && MixBus (b) == MixBus::Master)
        {
            // BYPASS is the raw inputs, on the air: every stage of the master is out of the way
            // except the ceiling. Twenty unprocessed inputs at unity with nothing holding them
            // under full scale is the one thing a comparison must never put on the broadcast.
            ChannelParameters ceiling = bp.channel;
            ceiling.bypassAll = false;
            ceiling.abLoudnessMatch = false;
            ceiling.inputTrimDb = 0.0f; ceiling.outputTrimDb = 0.0f; ceiling.polarityInvert = false;
            ceiling.hpfEnabled = ceiling.lpfEnabled = ceiling.gateEnabled = ceiling.replaceEnabled = false;
            ceiling.correctiveEqEnabled = ceiling.compEnabled = ceiling.transientEnabled = false;
            ceiling.toneEqEnabled = ceiling.satEnabled = ceiling.deEssEnabled = ceiling.widthEnabled = false;
            if (! bp.channel.limiterEnabled) ceiling.limiterCeilingDb = -1.0f;
            ceiling.limiterEnabled = true;
            bus.processor.setParameters (ceiling);
        }
        else if (p.bypassProcessing)
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

void MixEngine::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept LIVEMIX_NONBLOCKING
{
    ScopedNoDenormals noDenormals;
    const auto start = std::chrono::steady_clock::now();
    deviceOutputs.store (numOutputs, std::memory_order_relaxed);

    if (mailbox.hasNew())
    {
        applied = mailbox.acquire();
        if (snapNextApply) haveApplied = false;          // the first publish after prepare: no ramp from the baseline
        applyParameters (applied);
        haveApplied = true;
        snapNextApply = false;
    }
    if (feedMailbox.hasNew())
    {
        appliedFeeds = feedMailbox.acquire();
        // Resolve the levels once here, so the block itself is only multiplies.
        for (int f = 0; f < kMaxOutputFeeds; ++f)
        {
            const auto& fd = appliedFeeds.feeds[size_t (f)];
            const auto& was = previousFeeds.feeds[size_t (f)];
            feedLevel[size_t (f)].setTarget (fd.mute ? 0.0f : dbToGain (clamp (fd.gainDb, -60.0f, 12.0f)));
            // A feed that is new, or now goes somewhere else or carries something else, starts
            // at its own level: a ramp is for a level that changes, not for a different feed.
            const bool different = f >= previousFeeds.count || was.left != fd.left || was.right != fd.right
                                || was.source != fd.source || was.monitor != fd.monitor;
            if (! feedsSettled || different) feedLevel[size_t (f)].snapToTarget();
        }
        feedsSettled = true;
        previousFeeds = appliedFeeds;
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
        else if (! auditionOnMain.load (std::memory_order_relaxed))
            auditionRequest.store (nullptr, std::memory_order_relaxed);   // nowhere to hear it: the request is dropped, never kept for later

        // ---- Strips ----
        // In kit order, not console order: a tom or hi-hat asks the kick's and snare's triggers
        // whether a hit on its microphone was really theirs, and can only hear an answer from a
        // strip that has already run this block. Summing is the same in any order.
        for (int oi = 0; oi < numStrips; ++oi)
        {
            const int i = stripOrder[size_t (oi)];
            Strip& s = *strips[size_t (i)];
            for (int ch = 0; ch < s.channels; ++ch)
            {
                const int idx = ch == 0 ? s.inputA : s.inputB;
                if (idx >= 0 && idx < numInputs) std::memcpy (s.ptrs[size_t (ch)], in[size_t (idx)], sizeof (float) * size_t (n));
                else std::memset (s.ptrs[size_t (ch)], 0, sizeof (float) * size_t (n));
            }
            // At the converter: before the digital preamp touches it, which is where a clip is a clip.
            {
                float peak = 0.0f;
                for (int ch = 0; ch < s.channels; ++ch)
                    for (int k = 0; k < n; ++k) peak = std::max (peak, std::fabs (s.ptrs[size_t (ch)][k]));
                if (peak > s.converterPeak.load (std::memory_order_relaxed)) s.converterPeak.store (peak, std::memory_order_relaxed);
                if (peak >= 0.9999f) s.converterClipped.store (true, std::memory_order_relaxed);
                if (listening)
                {
                    AudioBlockView raw { s.ptrs.data(), s.channels, n };
                    t->countConverterClips (i, raw);
                }
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
            s.processor.setBlockStart (samplePosition);
            s.processor.process (view);
            guard (view, s.processor, nonFinite);
            if (listening) t->pushStripProcessed (i, view);

            // ---- SHARE THE MICS: this member's voice level, and the gain it is heading for ----
            // The gain itself was decided at the end of the last block from every member's level
            // (a member cannot know its share until all of them have been heard); a block's lag
            // is a millisecond or two, which is nothing to a microphone opening. Applied after the
            // listen tap, so TUNE measures the microphone and not the automixer.
            bool autoScaled = false;
            if (s.autoMember && (autoOn || s.autoGain < 0.9999f))
            {
                float e = 0.0f;
                for (int ch = 0; ch < s.channels; ++ch)
                    for (int k = 0; k < n; ++k) e += s.ptrs[size_t (ch)][k] * s.ptrs[size_t (ch)][k];
                e /= float (n * s.channels);
                const float detector = e > s.autoEnv ? autoEnvUp : autoEnvDown;
                s.autoEnv += (1.0f - std::pow (1.0f - detector, float (n))) * (e - s.autoEnv);

                const float target = autoOn ? s.autoTarget : 1.0f;
                const float c = target > s.autoGain ? autoAttackCoeff : autoReleaseCoeff;
                const float g0 = s.autoGain;
                const float g1 = g0 + (1.0f - std::pow (1.0f - c, float (n))) * (target - g0);
                if (std::fabs (g1 - 1.0f) > 1.0e-5f || std::fabs (g0 - 1.0f) > 1.0e-5f)
                {
                    // What a pre-fade listen hears: the microphone, before the automixer.
                    if (monitorRouted && monitorPfl)
                        for (int ch = 0; ch < s.channels; ++ch)
                            std::memcpy (s.preAuto[size_t (ch)].data(), s.ptrs[size_t (ch)], sizeof (float) * size_t (n));
                    const float step = (g1 - g0) / float (n);
                    for (int ch = 0; ch < s.channels; ++ch)
                    {
                        float g = g0;
                        float* x = s.ptrs[size_t (ch)];
                        for (int k = 0; k < n; ++k) { g += step; x[k] *= g; }
                    }
                    autoScaled = monitorRouted && monitorPfl;
                }
                s.autoGain = g1 > 0.9999f && target >= 1.0f ? 1.0f : g1;
                s.autoGainDb.store (gainToDb (s.autoGain), std::memory_order_relaxed);
            }

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
                    // PFL at unity fader, through the same centre pan law AFL uses for a mono
                    // channel: switching between them must not be a 3 dB jump in the ears.
                    const float pflGain = s.channels == 2 ? 1.0f : 0.70710678f;
                    const float al = monitorPfl ? pflGain : s.gainL.getCurrent();
                    const float ar = monitorPfl ? pflGain : s.gainR.getCurrent();
                    // PFL hears the microphone before the automixer stepped it back.
                    if (autoScaled)
                    {
                        xl = s.preAuto[0].data();
                        xr = s.channels == 2 ? s.preAuto[1].data() : s.preAuto[0].data();
                    }
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

        // ---- SHARE THE MICS: every member's share of the voices, for the next block ----
        // Each open member's gain is its share of the members' summed voice power, so the room
        // always hears one microphone's worth (two people at once: each 3 dB down). Nobody over
        // the threshold: nobody is speaking, and every gain holds - the last speaker stays open.
        if (autoOn)
        {
            float sum = 0.0f, loudest = 0.0f;
            for (int i = 0; i < numStrips; ++i)
            {
                const Strip& s = *strips[size_t (i)];
                if (! s.autoMember || applied.strips[size_t (i)].mute) continue;
                sum += s.autoEnv;
                loudest = std::max (loudest, s.autoEnv);
            }
            if (loudest >= autoThresholdPow && sum > 0.0f)
                for (int i = 0; i < numStrips; ++i)
                {
                    Strip& s = *strips[size_t (i)];
                    if (! s.autoMember) continue;
                    const float share = applied.strips[size_t (i)].mute ? 0.0f : s.autoEnv / sum;
                    s.autoTarget = std::max (autoDepthGain, std::sqrt (share));   // amplitude of a power share
                }
        }

        // ---- Buses -> master ----
        Bus& master = buses[size_t (MixBus::Master)];

        // SPEECH PRIORITY: this block's duck gain, one value per sample so a band stepping
        // back is a move and not a step. Attack while the speech group is open, release when
        // it has been shut for longer than the hold.
        float* duckAt = duckScratch.data();
        if (applied.speechDuck.enabled)
        {
            const float target = speechWasOpen ? speechDepthGain : 1.0f;
            const float coeff = speechWasOpen ? speechAttackCoeff : speechReleaseCoeff;
            for (int k = 0; k < n; ++k)
            {
                speechDuckGain += coeff * (target - speechDuckGain);
                duckAt[size_t (k)] = speechDuckGain;
            }
            speechDuckDb.store (gainToDb (speechDuckGain), std::memory_order_relaxed);
        }
        else if (speechDuckGain < 0.9999f)
        {
            // Switched off mid-service: the band comes back at a release rather than in one
            // step, so turning it off is never a jump.
            for (int k = 0; k < n; ++k)
            {
                speechDuckGain += speechOffCoeff * (1.0f - speechDuckGain);
                duckAt[size_t (k)] = speechDuckGain;
            }
            speechDuckDb.store (gainToDb (speechDuckGain), std::memory_order_relaxed);
        }
        else
        {
            speechDuckGain = 1.0f;
            speechDuckDb.store (0.0f, std::memory_order_relaxed);
        }

        for (int b = 0; b < int (MixBus::Count); ++b)
        {
            if (MixBus (b) == MixBus::Master || ! graph.busUsed[size_t (b)]) continue;
            Bus& bus = buses[size_t (b)];
            AudioBlockView view { bus.ptrs.data(), 2, n };
            if (listening) t->pushBus (MixBus (b), view);
            bus.processor.process (view);
            guard (view, bus.processor, nonFinite);
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
            // SPEECH PRIORITY: the band steps back into the master while somebody is speaking.
            // Into the master only - the listen above has already taken its copy, so what the
            // engineer hears is always what is really there.
            const bool ducked = applied.speechDuck.enabled
                             && (MixBus (b) == MixBus::Drums || MixBus (b) == MixBus::Bass || MixBus (b) == MixBus::Music);
            if (ducked || bus.gain.isSmoothing())
            {
                for (int k = 0; k < n; ++k)
                {
                    const float g = bus.gain.next() * (ducked ? duckAt[size_t (k)] : 1.0f);
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

        // The detector for the *next* block: how loud the speech group is putting out now.
        if (applied.speechDuck.enabled && graph.busUsed[size_t (MixBus::Speech)])
        {
            const Bus& speech = buses[size_t (MixBus::Speech)];
            // Its level in the voice band, as a level: a mean square with a quick rise and a
            // slower fall, so a word opens it and a hit of bleed does not.
            for (int k = 0; k < n; ++k)
            {
                // Two poles each way (12 dB/octave): one was not enough to keep a kick's thump
                // or a cymbal's wash out of a voice detector.
                const float x = 0.5f * (speech.ptrs[0][k] + speech.ptrs[1][k]);
                speechLow += speechLowCoeff * (x - speechLow);
                const float hp1 = x - speechLow;
                speechLow2 += speechLowCoeff * (hp1 - speechLow2);
                const float band = hp1 - speechLow2;
                speechHigh += speechHighCoeff * (band - speechHigh);
                speechHigh2 += speechHighCoeff * (speechHigh - speechHigh2);
                const float e = speechHigh2 * speechHigh2;
                speechEnv += (e > speechEnv ? speechEnvUp : speechEnvDown) * (e - speechEnv);
            }
            speechEnv = flushDenormal (speechEnv);
            const float level = std::sqrt (speechEnv);
            // Open at the threshold, close 6 dB under it: a voice that dips mid-word is still a
            // voice. A SPEECH group that is muted, or pulled right down, is nobody speaking.
            constexpr float kCloseBelow = 0.5f;               // -6 dB
            const bool groupOpen = speech.gain.getCurrent() > 0.01f;   // above -40 dB
            speechVoiced = groupOpen && (speechVoiced ? level > speechThresholdLin * kCloseBelow
                                                      : level > speechThresholdLin);
            const bool open = speechVoiced;
            if (open) speechHoldLeft = speechHoldSamples;
            else      speechHoldLeft = std::max (0.0f, speechHoldLeft - float (n));
            speechWasOpen = open || speechHoldLeft > 0.0f;
        }
        else
        {
            speechWasOpen = false;
            speechHoldLeft = 0.0f;
            speechVoiced = false;
        }

        // ---- FX returns -> master ----
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (! graph.fxUsed[size_t (f)]) continue;
            Fx& slot = fx[size_t (f)];
            AudioBlockView view { slot.ptrs.data(), 2, n };
            slot.chain.process (view);
            guard (view, slot.chain, nonFinite);
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
        // HEAR IT in place: no private listen, and solo is set to be heard by everyone.
        if (auditionOnMain.load (std::memory_order_relaxed))
            if (const SampleBank* want = auditionRequest.exchange (nullptr, std::memory_order_acq_rel))
            {
                auditionPlayer.setBank (want);
                auditionPlayer.trigger (0, 1.0f, dbToGain (clamp (auditionGainDb.load (std::memory_order_relaxed), -60.0f, 0.0f)), 1.0);
                auditionPlayingOnMain = true;
            }
        if (auditionPlayingOnMain)
        {
            if (auditionPlayer.isPlaying()) auditionPlayer.render (masterView, 1.0f);
            else auditionPlayingOnMain = false;
        }
        if (listening) t->pushBus (MixBus::Master, masterView);
        master.processor.process (masterView);
        guard (masterView, master.processor, nonFinite);
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
            // HEAR IT: an audition lands here and nowhere else, before the listen's own level.
            if (const SampleBank* want = auditionRequest.exchange (nullptr, std::memory_order_acq_rel))
            {
                auditionPlayer.setBank (want);
                auditionPlayer.trigger (0, 1.0f, dbToGain (clamp (auditionGainDb.load (std::memory_order_relaxed), -60.0f, 0.0f)), 1.0);
                auditionPlayingOnMain = false;
            }
            if (! auditionPlayingOnMain && auditionPlayer.isPlaying())
            {
                AudioBlockView listen { monitor.ptrs.data(), 2, n };
                auditionPlayer.render (listen, 1.0f);
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

        // DIM / MUTE: one ramp for the block, shared by every feed that is not the listen.
        const bool broadcastFlat = ! broadcastGain.isSmoothing();
        const float broadcastNow = broadcastGain.getCurrent();
        if (! broadcastFlat)
            for (int k = 0; k < n; ++k) broadcastRamp[size_t (k)] = broadcastGain.next();

        const int feeds = clamp (appliedFeeds.count, 1, kMaxOutputFeeds);
        for (int f = 0; f < feeds; ++f)
        {
            const auto& feed = appliedFeeds.feeds[size_t (f)];
            Smoother& level = feedLevel[size_t (f)];
            const bool levelRamp = level.isSmoothing();
            if (! levelRamp && level.getCurrent() == 0.0f) continue;      // muted, and settled there

            // What this feed carries. A group bus is post-processing, so its own fader is
            // applied here the way the master sum applies it.
            const float* srcL = nullptr;
            const float* srcR = nullptr;
            float gain = 1.0f;
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

            // The gain for every sample of this block: the feed's own level (ramped), the
            // group's fader for a group feed, and DIM / MUTE for anything but the listen.
            const bool broadcastRamped = ! feed.monitor && ! broadcastFlat;
            if (! feed.monitor && broadcastFlat) gain *= broadcastNow;
            const bool ramped = levelRamp || broadcastRamped;
            float* g = feedRamp.data();
            if (ramped)
                for (int k = 0; k < n; ++k)
                    g[k] = (levelRamp ? level.next() : level.getCurrent()) * gain * (broadcastRamped ? broadcastRamp[size_t (k)] : 1.0f);
            // A fade to silence ends in silence: an exponential never gets there, so below
            // -80 dB it is let go.
            if (level.getTarget() == 0.0f && level.getCurrent() < 1.0e-4f) level.snapTo (0.0f);
            const float flat = level.getCurrent() * gain;
            if (feed.mono || (hasL != hasR))
            {
                for (int k = 0; k < n; ++k)
                {
                    const float mono = (srcL[k] + srcR[k]) * 0.5f * (ramped ? g[k] : flat);
                    if (hasL) out[size_t (l)][k] += mono;
                    if (hasR && r != l) out[size_t (r)][k] += mono;
                }
            }
            else
            {
                for (int k = 0; k < n; ++k)
                {
                    const float gk = ramped ? g[k] : flat;
                    out[size_t (l)][k] += srcL[k] * gk;
                    out[size_t (r)][k] += srcR[k] * gk;
                }
            }
        }

        // THE LAST THING BEFORE THE DEVICE. Nothing past the master limiter has a ceiling of
        // its own - a feed's gain, two feeds summed onto one pair, a group feed, the listen -
        // and a converter handed more than full scale clips anyway, so the device is never
        // handed more than it can play, and never something that is not a number. The count
        // is how the status foot knows it happened: in a mix that is right, it never does.
        for (int o = 0; o < numOutputs; ++o)
        {
            float* x = out[size_t (o)];
            if (x == nullptr) continue;
            bool over = false;
            for (int k = 0; k < n; ++k)
            {
                const float v = x[k];
                if (! (v >= -1.0f && v <= 1.0f))           // also true for a NaN
                {
                    x[k] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : 0.0f);
                    over = true;
                }
            }
            if (over) outputClamped.fetch_add (1, std::memory_order_relaxed);
        }
    }

    const auto micros = float (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count());
    lastMicros.store (micros, std::memory_order_relaxed);
    if (micros > peakMicros.load (std::memory_order_relaxed)) peakMicros.store (micros, std::memory_order_relaxed);
    blockCount.fetch_add (1, std::memory_order_relaxed);
    processedSamples.fetch_add (numSamples, std::memory_order_relaxed);
    samplePosition += numSamples;
}

float MixEngine::consumeConverterPeakDb (int strip) const noexcept
{
    if (strip < 0 || strip >= int (strips.size()) || strips[size_t (strip)] == nullptr) return kSilenceDb;
    return gainToDb (strips[size_t (strip)]->converterPeak.exchange (0.0f, std::memory_order_relaxed));
}

bool MixEngine::converterClipped (int strip) const noexcept
{
    if (strip < 0 || strip >= int (strips.size()) || strips[size_t (strip)] == nullptr) return false;
    return strips[size_t (strip)]->converterClipped.load (std::memory_order_relaxed);
}

void MixEngine::clearConverterClips() const noexcept
{
    for (auto& s : strips) if (s != nullptr) s->converterClipped.store (false, std::memory_order_relaxed);
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
