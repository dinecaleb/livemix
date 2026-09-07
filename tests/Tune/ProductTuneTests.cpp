// Tune, profiles, products and macros for Dine Vocals, Dine Keys, Dine Master, Dine Guitar and Dine Bass.
#include "TestFramework.h"
#include "Tune/TuneEngine.h"
#include "Tune/SourceStrategy.h"
#include "Profiles/StyleProfile.h"
#include "Profiles/MacroMapping.h"
#include "Core/ProductDefinition.h"
#include "State/ParameterSpecs.h"
#include "State/ParameterIDs.h"
#include <cmath>
#include <set>

using namespace livemix;

namespace
{
    AnalysisResult onTarget (ChannelRole role, StyleProfileId profile = StyleProfileId::ModernGospel)
    {
        const auto t = StyleProfile::targets (role, profile);
        AnalysisResult a;
        a.valid = true;
        a.numChannels = productOf (role) == Product::Drums ? 1 : 2;
        a.peakDb = 0.5f * (t.capturePeakMinDb + t.capturePeakMaxDb);
        a.hitLevelDb = a.peakDb - 2.0f;
        a.crestFactorDb = 0.5f * (t.crestFactorMinDb + t.crestFactorMaxDb);
        a.rmsDb = a.peakDb - a.crestFactorDb;
        a.noiseFloorDb = a.hitLevelDb - 45.0f;
        a.dynamicRangeDb = 45.0f;
        a.silencePercent = 20.0f;
        a.transientCount = 20; a.transientsPerSecond = 2.0f; a.meanTransientRiseDb = 22.0f;
        a.meanDecayMs = 250.0f; a.decayCount = 18;
        a.bandEnergyDb = t.bandTargetDb;
        a.bleedEstimate = 0.05f;
        a.fundamentalHz = t.fundamentalMaxHz > 0.0f ? 0.5f * (t.fundamentalMinHz + t.fundamentalMaxHz) : 0.0f;
        a.fundamentalLevelDb = -6.0f;
        a.stereoCorrelation = 0.6f;
        a.sibilanceDb = t.sibilanceMaxDb - 3.0f;
        a.loudnessLufs = t.loudnessTargetAppropriate ? t.targetLufs : -18.0f;
        a.truePeakDb = a.peakDb;
        return a;
    }

    TuneContext context (ChannelRole role, const AnalysisResult& a, StyleProfileId profile = StyleProfileId::ModernGospel)
    {
        TuneContext c;
        c.analysis = a;
        c.role = role;
        c.profile = profile;
        c.current = StyleProfile::baseline (role, profile);
        return c;
    }

    const Recommendation* find (const TuneResult& r, TuneSection s, const char* needle = nullptr)
    {
        for (const auto& i : r.report.items)
            if (i.section == s && (needle == nullptr || i.what.find (needle) != std::string::npos)) return &i;
        return nullptr;
    }
    float changeValue (const Recommendation& r, const std::string& id, float fallback = -999.0f)
    {
        for (const auto& c : r.changes) if (c.paramId == id) return c.value;
        return fallback;
    }

    const ChannelRole kNewRoles[] = {
        ChannelRole::LeadVocal, ChannelRole::BackingVocal, ChannelRole::Choir, ChannelRole::Speech, ChannelRole::VocalBus,
        ChannelRole::Piano, ChannelRole::ElectricPiano, ChannelRole::Organ, ChannelRole::SynthPad, ChannelRole::SynthLead, ChannelRole::KeysBus,
        ChannelRole::MasterStream, ChannelRole::MasterBroadcast, ChannelRole::MasterRecording, ChannelRole::MasterRoom,
        ChannelRole::AcousticGuitar, ChannelRole::ElectricGuitarClean, ChannelRole::ElectricGuitarDrive, ChannelRole::GuitarBus,
        ChannelRole::BassDI, ChannelRole::BassAmp, ChannelRole::SynthBass, ChannelRole::BassBus
    };
}

TEST_CASE ("Products: every role belongs to exactly one product; product tables cover the fields they use; ids unique")
{
    for (int p = 0; p < int (Product::Count); ++p)
    {
        const auto& def = productDefinition (Product (p));
        CHECK (! def.roles.empty());
        CHECK (def.name[0] != '\0' && def.shortName[0] != '\0' && def.playerPrompt[0] != '\0');
        for (auto r : def.roles) CHECK (productOf (r) == Product (p));
        CHECK (roleFromProductIndex (def, roleIndexInProduct (def, def.defaultRole)) == def.defaultRole);
        for (const auto& m : def.macros) { CHECK (m.id[0] != '\0'); CHECK (m.label[0] != '\0'); CHECK (m.tooltip[0] != '\0'); CHECK (findParameterSpec (m.id) != nullptr); }
        CHECK (! def.stages.empty() && def.stages.front() == ChainStage::Input && def.stages.back() == ChainStage::Output);

        std::set<std::string> ids;
        const auto& specs = channelParameterSpecs (Product (p));
        for (const auto& s : specs)
        {
            CHECK (ids.insert (s.id).second);
            CHECK (s.defaultValue >= s.minValue && s.defaultValue <= s.maxValue);
        }
        for (const auto& m : def.macros) CHECK (ids.count (m.id) == 1);
        CHECK (ids.count (ParamID::role) == 1 && ids.count (ParamID::profile) == 1 && ids.count (ParamID::liveSafe) == 1);
        // Every field in the product table matches the struct default and type.
        ChannelParameters d;
        forEachDspParameter (d, [&] (const std::string& id, auto& v)
        {
            if (! productUsesParameter (Product (p), id)) return;
            using T = std::remove_reference_t<decltype (v)>;
            const ParameterSpec* s = findParameterSpec (id);
            REQUIRE (s != nullptr);
            if constexpr (std::is_same_v<T, bool>) CHECK (s->type == ParameterSpec::Type::Bool);
            else if constexpr (std::is_same_v<T, int>) CHECK (s->type == ParameterSpec::Type::Choice);
            else CHECK (s->type == ParameterSpec::Type::Float);
            CHECK_NEAR (float (v), s->defaultValue, 1e-5f);
        });
        // Fields a product hides stay off in every one of its baselines (so nothing inaudible is stored in sessions).
        for (auto r : def.roles)
            for (int st = 0; st < int (StyleProfileId::Count); ++st)
            {
                const auto b = StyleProfile::baseline (r, StyleProfileId (st));
                if (! productUsesParameter (Product (p), ParamID::deEssOn)) CHECK (! b.deEssEnabled);
                if (! productUsesParameter (Product (p), ParamID::widthOn)) CHECK (! b.widthEnabled);
                if (! productUsesParameter (Product (p), ParamID::limiterOn)) CHECK (! b.limiterEnabled);
                if (! productUsesParameter (Product (p), ParamID::gateOn)) CHECK (! b.gateEnabled);
                if (! productUsesParameter (Product (p), ParamID::transOn)) CHECK (! b.transientEnabled);
            }
    }
    // The released Drums table is unchanged in shape: no de-esser / width / limiter ids.
    CHECK (! productUsesParameter (Product::Drums, ParamID::deEssOn));
    CHECK (! productUsesParameter (Product::Drums, ParamID::limiterOn));
    CHECK (productUsesParameter (Product::Drums, ParamID::gateOn));
    CHECK (productUsesParameter (Product::Vocals, ParamID::deEssOn) && ! productUsesParameter (Product::Vocals, ParamID::transOn));
    CHECK (productUsesParameter (Product::Keys, ParamID::widthOn) && ! productUsesParameter (Product::Keys, ParamID::gateOn));
    CHECK (productUsesParameter (Product::Master, ParamID::limiterOn) && productUsesParameter (Product::Master, ParamID::widthOn));
    CHECK (productUsesParameter (Product::Guitar, ParamID::gateOn) && productUsesParameter (Product::Guitar, ParamID::widthOn));
    CHECK (! productUsesParameter (Product::Guitar, ParamID::deEssOn) && ! productUsesParameter (Product::Guitar, ParamID::transOn) && ! productUsesParameter (Product::Guitar, ParamID::limiterOn));
    CHECK (productUsesParameter (Product::Bass, ParamID::gateOn) && productUsesParameter (Product::Bass, ParamID::satOn) && productUsesParameter (Product::Bass, ParamID::grit));
    CHECK (! productUsesParameter (Product::Bass, ParamID::widthOn) && ! productUsesParameter (Product::Bass, ParamID::deEssOn) && ! productUsesParameter (Product::Bass, ParamID::limiterOn));
    // Every field has a spec somewhere (the union of the tables).
    ChannelParameters d;
    forEachDspParameter (d, [&] (const std::string& id, auto&) { CHECK (findParameterSpec (id) != nullptr); });
    for (int r = 0; r < int (ChannelRole::Count); ++r) CHECK (roleHint (ChannelRole (r))[0] != '\0');
}

TEST_CASE ("Profiles: every new family has targets, a baseline inside the parameter bounds and a strategy")
{
    for (auto role : kNewRoles)
        for (int s = 0; s < int (StyleProfileId::Count); ++s)
        {
            const auto p = StyleProfile::baseline (role, StyleProfileId (s));
            ChannelParameters copy = p;
            forEachDspParameter (copy, [&] (const std::string& id, auto& v)
            {
                const auto* spec = findParameterSpec (id);
                REQUIRE (spec != nullptr);
                CHECK (float (v) >= spec->minValue - 1e-5f && float (v) <= spec->maxValue + 1e-5f);
            });
            const auto t = StyleProfile::targets (role, StyleProfileId (s));
            CHECK (t.intent[0] != '\0');
            CHECK (t.capturePeakMinDb < t.capturePeakMaxDb);
            CHECK (t.hpfMaxHz >= t.hpfMinHz && t.compRatioMax >= t.compRatioMin && t.crestFactorMaxDb > t.crestFactorMinDb);
            CHECK (strategyFor (roleFamily (role)).name()[0] != '\0');
        }
    // Delivery loudness differs by output.
    CHECK (StyleProfile::targets (ChannelRole::MasterBroadcast, StyleProfileId::ModernGospel).targetLufs < StyleProfile::targets (ChannelRole::MasterStream, StyleProfileId::ModernGospel).targetLufs);
    CHECK (! StyleProfile::targets (ChannelRole::MasterRoom, StyleProfileId::ModernGospel).loudnessTargetAppropriate);
    CHECK (StyleProfile::baseline (ChannelRole::MasterStream, StyleProfileId::ModernGospel).limiterEnabled);
    CHECK (StyleProfile::baseline (ChannelRole::LeadVocal, StyleProfileId::ModernGospel).deEssEnabled);
    CHECK (! StyleProfile::baseline (ChannelRole::Choir, StyleProfileId::ModernGospel).gateEnabled);
    CHECK (StyleProfile::baseline (ChannelRole::Piano, StyleProfileId::ModernGospel).widthEnabled);
    CHECK (! StyleProfile::baseline (ChannelRole::GuitarBus, StyleProfileId::ModernGospel).gateEnabled);
    CHECK (StyleProfile::baseline (ChannelRole::ElectricGuitarDrive, StyleProfileId::ModernGospel).lpfEnabled);
    CHECK (! StyleProfile::baseline (ChannelRole::ElectricGuitarClean, StyleProfileId::ModernGospel).satEnabled);
    CHECK (! StyleProfile::targets (ChannelRole::GuitarBus, StyleProfileId::ModernGospel).gateAppropriate);
    CHECK (! StyleProfile::targets (ChannelRole::SynthBass, StyleProfileId::ModernGospel).gateAppropriate);
    CHECK (StyleProfile::baseline (ChannelRole::BassDI, StyleProfileId::ModernGospel).satEnabled);
    CHECK (StyleProfile::baseline (ChannelRole::BassAmp, StyleProfileId::ModernGospel).lpfEnabled);
    CHECK (StyleProfile::targets (ChannelRole::BassDI, StyleProfileId::ModernGospel).fundamentalMinHz <= 41.0f);
    // Drum baselines never carry the new stages.
    for (int r = int (ChannelRole::KickIn); r <= int (ChannelRole::DrumBus); ++r)
    {
        const auto p = StyleProfile::baseline (ChannelRole (r), StyleProfileId::ModernGospel);
        CHECK (! p.deEssEnabled && ! p.widthEnabled && ! p.limiterEnabled);
    }
}

TEST_CASE ("Tune is idempotent for every vocal, keys, master, guitar and bass source")
{
    for (auto role : kNewRoles)
    {
        auto a = onTarget (role);
        a.bleedEstimate = 0.5f; a.noiseFloorDb = a.hitLevelDb - 25.0f;
        a.crestFactorDb = StyleProfile::targets (role, StyleProfileId::ModernGospel).crestFactorMaxDb + 4.0f;
        a.sibilanceDb = -2.0f;            // sharp S sounds
        a.stereoCorrelation = 0.05f;      // too wide
        a.loudnessLufs = -24.0f;          // quiet mix
        auto ctx = context (role, a);
        auto first = TuneEngine::tune (ctx);
        REQUIRE (first.valid);
        CHECK (first.report.inputHealth == "Healthy");
        ctx.current = first.proposed;
        auto second = TuneEngine::tune (ctx);
        REQUIRE (second.valid);
        if (! second.noChangeRequired)
            for (const auto& i : second.report.items)
                if (! i.changes.empty()) testfw::reportFailure (__FILE__, __LINE__, std::string (channelRoleName (role)) + " moved again: " + i.what);
        CHECK (second.parametersChanged == 0);
        CHECK (diffParameters (second.before, second.proposed).empty());
        // Every change is inside the product's own parameter table (nothing hidden gets written).
        for (const auto& i : first.report.items)
            for (const auto& c : i.changes)
                if (! productUsesParameter (productOf (role), c.paramId))
                    testfw::reportFailure (__FILE__, __LINE__, std::string (channelRoleName (role)) + " wrote a hidden parameter: " + c.paramId);
    }
}

TEST_CASE ("Tune (vocals): sharp S sounds add bounded S control; boom raises the high-pass; a smooth voice gets no de-esser")
{
    auto a = onTarget (ChannelRole::LeadVocal);
    a.sibilanceDb = -1.0f; a.sibilancePercent = 20.0f;
    a.bandEnergyDb[size_t (Band::Low)] += 14.0f; // boom well past the (male-voice friendly) tolerance
    auto ctx = context (ChannelRole::LeadVocal, a);
    ctx.current.deEssEnabled = false;
    auto r = TuneEngine::tune (ctx);
    const auto* s = find (r, TuneSection::Tone, "S control");
    REQUIRE (s != nullptr);
    const auto t = StyleProfile::targets (ChannelRole::LeadVocal, StyleProfileId::ModernGospel);
    CHECK (changeValue (*s, ParamID::deEssRange) <= t.deEssMaxRangeDb + 0.01f);
    CHECK (changeValue (*s, ParamID::deEssRange) >= 3.0f);
    CHECK (changeValue (*s, ParamID::deEssOn) >= 0.5f);
    CHECK (r.proposed.deEssEnabled);
    const auto* hpf = find (r, TuneSection::Tone, "High-pass");
    REQUIRE (hpf != nullptr);
    CHECK (changeValue (*hpf, ParamID::hpfFreq) > 90.0f && changeValue (*hpf, ParamID::hpfFreq) <= t.hpfMaxHz);

    auto smooth = onTarget (ChannelRole::LeadVocal);
    smooth.sibilanceDb = -22.0f;
    auto rs = TuneEngine::tune (context (ChannelRole::LeadVocal, smooth));
    CHECK (! rs.proposed.deEssEnabled);

    // Speech never gets a transient shaper or saturation; the expander stays shallow.
    auto sp = onTarget (ChannelRole::Speech);
    sp.bleedEstimate = 0.8f; sp.noiseFloorDb = sp.hitLevelDb - 15.0f;
    auto rp = TuneEngine::tune (context (ChannelRole::Speech, sp));
    CHECK (! rp.proposed.transientEnabled && ! rp.proposed.satEnabled);
    CHECK (rp.proposed.gateRangeDb <= StyleProfile::targets (ChannelRole::Speech, StyleProfileId::ModernGospel).gateMaxRangeDb + 0.01f);

    // A choir is never gated.
    auto ch = onTarget (ChannelRole::Choir);
    ch.bleedEstimate = 0.8f; ch.noiseFloorDb = ch.hitLevelDb - 15.0f;
    CHECK (! TuneEngine::tune (context (ChannelRole::Choir, ch)).proposed.gateEnabled);
}

TEST_CASE ("Tune (keys): a wide, out-of-phase piano is narrowed with the low end centred; a mono source is left alone; mud is cut")
{
    auto a = onTarget (ChannelRole::Piano);
    a.stereoCorrelation = -0.1f;
    a.bandEnergyDb[size_t (Band::LowMid)] += 8.0f;
    auto r = TuneEngine::tune (context (ChannelRole::Piano, a));
    const auto* w = find (r, TuneSection::Mix, "Width");
    REQUIRE (w != nullptr);
    const auto t = StyleProfile::targets (ChannelRole::Piano, StyleProfileId::ModernGospel);
    CHECK (changeValue (*w, ParamID::widthAmount) < t.widthTarget);
    CHECK (changeValue (*w, ParamID::widthAmount) >= t.widthMin - 0.01f);
    CHECK (changeValue (*w, ParamID::widthMonoBelow, t.monoBelowHz) >= 20.0f);
    CHECK (find (r, TuneSection::Tone, "low-mid") != nullptr);
    CHECK (! r.proposed.gateEnabled && ! r.proposed.transientEnabled);

    auto mono = onTarget (ChannelRole::Piano);
    mono.numChannels = 1;
    auto ctx = context (ChannelRole::Piano, mono);
    ctx.current.widthAmount = 1.3f;
    auto rm = TuneEngine::tune (ctx);
    CHECK_NEAR (rm.proposed.widthAmount, 1.0f, 1e-4f);

    // A nearly mono synth pad gets a little width, bounded by the profile.
    auto pad = onTarget (ChannelRole::SynthPad);
    pad.stereoCorrelation = 0.99f;
    auto rp = TuneEngine::tune (context (ChannelRole::SynthPad, pad));
    const auto tp = StyleProfile::targets (ChannelRole::SynthPad, StyleProfileId::ModernGospel);
    CHECK (rp.proposed.widthAmount >= tp.widthTarget - 1e-4f && rp.proposed.widthAmount <= tp.widthMax + 1e-4f);
}

TEST_CASE ("Tune (master): a quiet mix is pushed toward the delivery loudness with the limiter at the ceiling; the room feed only gets safety limiting")
{
    auto a = onTarget (ChannelRole::MasterStream);
    a.loudnessLufs = -22.0f;
    auto ctx = context (ChannelRole::MasterStream, a);
    auto r = TuneEngine::tune (ctx);
    const auto* l = find (r, TuneSection::Mix, "Raised the output");
    REQUIRE (l != nullptr);
    const float trim = changeValue (*l, ParamID::outputTrim);
    CHECK (trim > ctx.current.outputTrimDb);
    CHECK (trim - ctx.current.outputTrimDb <= 12.0f);
    CHECK_NEAR (r.proposed.limiterCeilingDb, -1.0f, 1e-4f);
    CHECK (r.proposed.limiterEnabled);
    CHECK (! r.proposed.gateEnabled && ! r.proposed.transientEnabled);

    auto loud = onTarget (ChannelRole::MasterBroadcast);
    loud.loudnessLufs = -14.0f; // way over the -23 broadcast target
    auto ctxb = context (ChannelRole::MasterBroadcast, loud);
    auto rb = TuneEngine::tune (ctxb);
    CHECK (find (rb, TuneSection::Mix, "Lowered the output") != nullptr);
    CHECK (rb.proposed.outputTrimDb < ctxb.current.outputTrimDb);
    CHECK_NEAR (rb.proposed.limiterCeilingDb, -2.0f, 1e-4f);

    auto room = onTarget (ChannelRole::MasterRoom);
    room.loudnessLufs = -30.0f;
    auto ctxr = context (ChannelRole::MasterRoom, room);
    auto rr = TuneEngine::tune (ctxr);
    CHECK (find (rr, TuneSection::Mix, "Raised the output") == nullptr);
    CHECK (rr.proposed.limiterEnabled);
    CHECK_NEAR (rr.proposed.outputTrimDb, ctxr.current.outputTrimDb, 1e-4f);

    auto onIt = onTarget (ChannelRole::MasterStream);
    auto ro = TuneEngine::tune (context (ChannelRole::MasterStream, onIt));
    CHECK (find (ro, TuneSection::Mix, "Loudness on target") != nullptr);
}

TEST_CASE ("Tune (guitar): a boomy acoustic gets the high-pass and boom cut; fizz on a driven amp is rolled off and stays put; noise between phrases gets a bounded expander; the bus is never gated")
{
    // Acoustic DI with body boom and pickup quack.
    auto a = onTarget (ChannelRole::AcousticGuitar);
    a.numChannels = 1;
    a.bandEnergyDb[size_t (Band::Low)] += 9.0f;
    a.bandEnergyDb[size_t (Band::LowMid)] += 7.0f;
    a.resonances.push_back ({ 3100.0f, 10.0f });
    auto ctx = context (ChannelRole::AcousticGuitar, a);
    auto r = TuneEngine::tune (ctx);
    REQUIRE (r.valid);
    const auto t = StyleProfile::targets (ChannelRole::AcousticGuitar, StyleProfileId::ModernGospel);
    const auto* hpf = find (r, TuneSection::Tone, "High-pass");
    REQUIRE (hpf != nullptr);
    CHECK (changeValue (*hpf, ParamID::hpfFreq) > ctx.current.hpfHz && changeValue (*hpf, ParamID::hpfFreq) <= t.hpfMaxHz);
    CHECK (find (r, TuneSection::Tone, "low-mid") != nullptr);
    CHECK (find (r, TuneSection::Tone, "harshness") != nullptr);
    CHECK (! r.proposed.transientEnabled && ! r.proposed.deEssEnabled && ! r.proposed.limiterEnabled);
    CHECK_NEAR (r.proposed.widthAmount, 1.0f, 1e-4f); // mono: width untouched
    {
        ctx.current = r.proposed;
        CHECK (TuneEngine::tune (ctx).parametersChanged == 0);
    }

    // Driven electric with a fizzy top: the low-pass comes down from the template and re-tuning does not walk it.
    auto e = onTarget (ChannelRole::ElectricGuitarDrive);
    e.bandEnergyDb[size_t (Band::Brilliance)] += 12.0f;
    e.bandEnergyDb[size_t (Band::Air)] += 12.0f;
    auto ctxe = context (ChannelRole::ElectricGuitarDrive, e);
    auto re = TuneEngine::tune (ctxe);
    const auto* lpf = find (re, TuneSection::Tone, "fizz");
    REQUIRE (lpf != nullptr);
    CHECK (changeValue (*lpf, ParamID::lpfFreq) < ctxe.current.lpfHz);
    CHECK (changeValue (*lpf, ParamID::lpfFreq) >= 6000.0f);
    CHECK (re.proposed.lpfEnabled);
    for (int pass = 0; pass < 3; ++pass)
    {
        ctxe.current = re.proposed;
        auto again = TuneEngine::tune (ctxe);
        for (const auto& i : again.report.items)
            if (! i.changes.empty()) testfw::reportFailure (__FILE__, __LINE__, std::string ("Electric Drive walked on re-tune: ") + i.what);
        CHECK (again.parametersChanged == 0);
        re = again;
    }

    // A dull clean electric never gets a fizz roll-off; the template low-pass stays off.
    auto dull = onTarget (ChannelRole::ElectricGuitarClean);
    dull.bandEnergyDb[size_t (Band::Brilliance)] -= 6.0f;
    auto rd = TuneEngine::tune (context (ChannelRole::ElectricGuitarClean, dull));
    CHECK (! rd.proposed.lpfEnabled);
    CHECK (find (rd, TuneSection::Tone, "fizz") == nullptr);

    // Amp hum between phrases: an expander, never deeper than the profile allows.
    auto noisy = onTarget (ChannelRole::ElectricGuitarDrive);
    noisy.bleedEstimate = 0.8f; noisy.noiseFloorDb = noisy.hitLevelDb - 18.0f;
    auto rn = TuneEngine::tune (context (ChannelRole::ElectricGuitarDrive, noisy));
    CHECK (rn.proposed.gateEnabled);
    CHECK (rn.proposed.gateRangeDb <= StyleProfile::targets (ChannelRole::ElectricGuitarDrive, StyleProfileId::ModernGospel).gateMaxRangeDb + 0.01f);
    CHECK (rn.proposed.gateThresholdDb < noisy.hitLevelDb - 12.0f + 0.01f);

    // Continuous strumming (no gaps): no expander, ever.
    auto strummed = onTarget (ChannelRole::AcousticGuitar);
    strummed.silencePercent = 1.0f; strummed.transientsPerSecond = 0.2f; strummed.transientCount = 2;
    strummed.noiseFloorDb = strummed.hitLevelDb - 10.0f; strummed.bleedEstimate = 1.0f;
    auto ctxs = context (ChannelRole::AcousticGuitar, strummed);
    ctxs.current.gateEnabled = true;
    CHECK (! TuneEngine::tune (ctxs).proposed.gateEnabled);

    // The guitar bus is never gated.
    auto bus = onTarget (ChannelRole::GuitarBus);
    bus.bleedEstimate = 0.9f; bus.noiseFloorDb = bus.hitLevelDb - 15.0f;
    auto ctxb = context (ChannelRole::GuitarBus, bus);
    ctxb.current.gateEnabled = true;
    CHECK (! TuneEngine::tune (ctxb).proposed.gateEnabled);

    // A stereo modeler that is out of phase is narrowed with the low end centred.
    auto wide = onTarget (ChannelRole::ElectricGuitarClean);
    wide.stereoCorrelation = -0.1f;
    auto rw = TuneEngine::tune (context (ChannelRole::ElectricGuitarClean, wide));
    const auto* w = find (rw, TuneSection::Mix, "Width");
    REQUIRE (w != nullptr);
    const auto tw = StyleProfile::targets (ChannelRole::ElectricGuitarClean, StyleProfileId::ModernGospel);
    CHECK (changeValue (*w, ParamID::widthAmount) < tw.widthTarget && changeValue (*w, ParamID::widthAmount) >= tw.widthMin - 0.01f);
}

TEST_CASE ("Tune (bass): the high-pass stays under the lowest note; noise between notes gets a bounded expander; a spiky DI gets bounded grit; synth bass and the bus are never gated")
{
    // Low E (41 Hz) with rumble under it: the high-pass rises but stays under 0.8 x the fundamental.
    auto a = onTarget (ChannelRole::BassDI);
    a.numChannels = 1;
    a.fundamentalHz = 41.2f; a.fundamentalLevelDb = -5.0f;
    a.bandEnergyDb[size_t (Band::Sub)] += 9.0f;
    a.bandEnergyDb[size_t (Band::LowMid)] += 7.0f;
    a.resonances.push_back ({ 2600.0f, 11.0f });
    auto ctx = context (ChannelRole::BassDI, a);
    auto r = TuneEngine::tune (ctx);
    REQUIRE (r.valid);
    CHECK (r.proposed.hpfEnabled);
    CHECK (r.proposed.hpfHz <= 41.2f * 0.8f + 0.5f);
    CHECK (r.proposed.hpfHz >= 25.0f);
    CHECK (find (r, TuneSection::Tone, "low-mid") != nullptr);
    CHECK (find (r, TuneSection::Tone, "harshness") != nullptr);
    CHECK (! r.proposed.transientEnabled && ! r.proposed.widthEnabled && ! r.proposed.deEssEnabled && ! r.proposed.limiterEnabled);
    {
        ctx.current = r.proposed;
        CHECK (TuneEngine::tune (ctx).parametersChanged == 0);
    }

    // Spiky DI (crest well above the profile): compression fitted and a little grit, bounded by the profile, idempotent.
    auto spiky = onTarget (ChannelRole::BassDI);
    const auto t = StyleProfile::targets (ChannelRole::BassDI, StyleProfileId::ModernGospel);
    spiky.crestFactorDb = t.crestFactorMaxDb + 5.0f;
    auto ctxs = context (ChannelRole::BassDI, spiky);
    auto rs = TuneEngine::tune (ctxs);
    CHECK (rs.proposed.compEnabled && rs.proposed.compRatio >= t.compRatioMin - 0.01f && rs.proposed.compRatio <= t.compRatioMax + 0.01f);
    const auto* grit = find (rs, TuneSection::Dynamics, "grit");
    REQUIRE (grit != nullptr);
    CHECK (rs.proposed.satEnabled && rs.proposed.satDrive > ctxs.current.satDrive && rs.proposed.satDrive <= t.satMaxDrive + 1e-4f);
    for (int pass = 0; pass < 3; ++pass)
    {
        ctxs.current = rs.proposed;
        auto again = TuneEngine::tune (ctxs);
        for (const auto& i : again.report.items)
            if (! i.changes.empty()) testfw::reportFailure (__FILE__, __LINE__, std::string ("Bass DI walked on re-tune: ") + i.what);
        CHECK (again.parametersChanged == 0);
        rs = again;
    }

    // Hum between notes: an expander, never deeper than the profile allows, with the detector under the fundamental.
    auto noisy = onTarget (ChannelRole::BassDI);
    noisy.fundamentalHz = 55.0f; noisy.fundamentalLevelDb = -5.0f;
    noisy.bleedEstimate = 0.8f; noisy.noiseFloorDb = noisy.hitLevelDb - 32.0f;
    auto rn = TuneEngine::tune (context (ChannelRole::BassDI, noisy));
    CHECK (rn.proposed.gateEnabled);
    CHECK (rn.proposed.gateRangeDb <= t.gateMaxRangeDb + 0.01f);
    CHECK (rn.proposed.gateScHpfHz <= 0.7f * 55.0f + 0.5f);

    // The church DI stem: 1 % silence, notes ringing for a second, the level between notes only 13 dB under them.
    // That floor is the bass itself, so no expander, and a gate left on is bypassed.
    auto rings = onTarget (ChannelRole::BassDI);
    rings.silencePercent = 1.0f; rings.transientsPerSecond = 1.3f; rings.meanDecayMs = 1089.0f;
    rings.noiseFloorDb = rings.hitLevelDb - 13.0f; rings.bleedEstimate = 0.9f;
    auto ctxr = context (ChannelRole::BassDI, rings);
    ctxr.current.gateEnabled = true;
    auto rr = TuneEngine::tune (ctxr);
    CHECK (! rr.proposed.gateEnabled);
    CHECK (find (rr, TuneSection::Bleed, "rings") != nullptr);

    // Synth bass and the bus are never gated, even with a noisy floor and a gate left on.
    for (auto role : { ChannelRole::SynthBass, ChannelRole::BassBus })
    {
        auto b = onTarget (role);
        b.bleedEstimate = 0.9f; b.noiseFloorDb = b.hitLevelDb - 15.0f;
        auto ctxb = context (role, b);
        ctxb.current.gateEnabled = true;
        CHECK (! TuneEngine::tune (ctxb).proposed.gateEnabled);
    }
}

TEST_CASE ("MacroMapping (products): centre reproduces the baseline; extremes stay bounded; directions match the labels")
{
    for (int p = 0; p < int (Product::Count); ++p)
    {
        const Product product = Product (p);
        const auto& def = productDefinition (product);
        const auto affected = MacroMapping::affectedParameterIds (product);
        CHECK (! affected.empty());
        for (const auto& id : affected) CHECK (productUsesParameter (product, id));
        for (auto role : def.roles)
            for (int s = 0; s < int (StyleProfileId::Count); ++s)
            {
                const auto base = StyleProfile::baseline (role, StyleProfileId (s));
                const auto same = MacroMapping::apply (product, base, MacroMapping::defaults (product), roleFamily (role));
                CHECK (diffParameters (base, same).empty());
                for (float v : { 0.0f, 100.0f })
                {
                    MacroValues m;
                    for (size_t i = 0; i < 5; ++i) m.v[i] = def.macros[i].minValue + (def.macros[i].maxValue - def.macros[i].minValue) * (v / 100.0f);
                    const auto out = MacroMapping::apply (product, base, m, roleFamily (role));
                    ChannelParameters copy = out;
                    forEachDspParameter (copy, [&] (const std::string& id, auto& val)
                    {
                        const auto* spec = findParameterSpec (id);
                        REQUIRE (spec != nullptr);
                        CHECK (float (val) >= spec->minValue - 1e-5f && float (val) <= spec->maxValue + 1e-5f);
                    });
                    // Only affected ids change.
                    for (const auto& c : diffParameters (base, out))
                        if (std::find (affected.begin(), affected.end(), c.paramId) == affected.end())
                            testfw::reportFailure (__FILE__, __LINE__, std::string (def.name) + " macro touched " + c.paramId);
                }
            }
    }

    // Vocals: WARMTH CLARITY SMOOTH STEADY CLEAN-UP
    {
        const auto base = StyleProfile::baseline (ChannelRole::LeadVocal, StyleProfileId::ModernGospel);
        auto at = [&] (int knob, float v) { MacroValues m = MacroMapping::defaults (Product::Vocals); m.v[size_t (knob)] = v; return MacroMapping::apply (Product::Vocals, base, m, RoleFamily::LeadVocal); };
        CHECK (at (0, 90.0f).toneBands[0].gainDb > base.toneBands[0].gainDb);
        CHECK (at (1, 90.0f).toneBands[2].gainDb > base.toneBands[2].gainDb);
        CHECK (at (2, 90.0f).deEssRangeDb > base.deEssRangeDb);
        CHECK (! at (2, 0.0f).deEssEnabled);
        CHECK (at (3, 90.0f).compRatio > base.compRatio);
        CHECK (at (4, 90.0f).hpfHz > base.hpfHz && at (4, 90.0f).gateEnabled);
        CHECK (! at (4, 0.0f).gateEnabled);
    }
    // Keys: WARMTH SHINE CLEAN-UP STEADY WIDTH
    {
        const auto base = StyleProfile::baseline (ChannelRole::Piano, StyleProfileId::ModernGospel);
        auto at = [&] (int knob, float v) { MacroValues m = MacroMapping::defaults (Product::Keys); m.v[size_t (knob)] = v; return MacroMapping::apply (Product::Keys, base, m, RoleFamily::Piano); };
        CHECK (at (1, 90.0f).toneBands[3].gainDb > base.toneBands[3].gainDb);
        CHECK (at (2, 90.0f).correctiveBands[0].gainDb < base.correctiveBands[0].gainDb);
        CHECK (at (4, 90.0f).widthAmount > base.widthAmount);
        CHECK (at (4, 10.0f).widthAmount < base.widthAmount);
    }
    // Master: WARMTH CLARITY GLUE LOUD WIDTH
    {
        const auto base = StyleProfile::baseline (ChannelRole::MasterStream, StyleProfileId::ModernGospel);
        auto at = [&] (int knob, float v) { MacroValues m = MacroMapping::defaults (Product::Master); m.v[size_t (knob)] = v; return MacroMapping::apply (Product::Master, base, m, RoleFamily::Master); };
        CHECK (at (2, 90.0f).compThresholdDb < base.compThresholdDb);
        CHECK (at (3, 90.0f).outputTrimDb > base.outputTrimDb);
        CHECK (at (3, 90.0f).limiterEnabled);
        CHECK_NEAR (at (3, 90.0f).limiterCeilingDb, base.limiterCeilingDb, 1e-5f); // LOUD never moves the ceiling
        CHECK (at (4, 90.0f).widthAmount > base.widthAmount);
    }
    // Guitar: WARMTH CLARITY SMOOTH STEADY CLEAN-UP
    {
        const auto base = StyleProfile::baseline (ChannelRole::ElectricGuitarDrive, StyleProfileId::ModernGospel);
        auto at = [&] (int knob, float v) { MacroValues m = MacroMapping::defaults (Product::Guitar); m.v[size_t (knob)] = v; return MacroMapping::apply (Product::Guitar, base, m, RoleFamily::ElectricGuitar); };
        CHECK (at (0, 90.0f).toneBands[0].gainDb > base.toneBands[0].gainDb);
        CHECK (at (1, 90.0f).toneBands[2].gainDb > base.toneBands[2].gainDb);
        CHECK (at (2, 90.0f).correctiveBands[2].gainDb < base.correctiveBands[2].gainDb);
        CHECK (at (2, 10.0f).correctiveBands[2].gainDb > base.correctiveBands[2].gainDb);
        CHECK (at (3, 90.0f).compRatio > base.compRatio);
        CHECK (at (4, 90.0f).hpfHz > base.hpfHz && at (4, 90.0f).gateEnabled && at (4, 90.0f).gateRangeDb > base.gateRangeDb);
        CHECK (! at (4, 0.0f).gateEnabled);
        // The bus never gets a gate from the knob; the acoustic does.
        const auto busBase = StyleProfile::baseline (ChannelRole::GuitarBus, StyleProfileId::ModernGospel);
        MacroValues clean = MacroMapping::defaults (Product::Guitar); clean.v[4] = 90.0f;
        CHECK (! MacroMapping::apply (Product::Guitar, busBase, clean, RoleFamily::GuitarBus).gateEnabled);
        const auto acBase = StyleProfile::baseline (ChannelRole::AcousticGuitar, StyleProfileId::ModernGospel);
        CHECK (MacroMapping::apply (Product::Guitar, acBase, clean, RoleFamily::AcousticGuitar).gateEnabled);
    }
    // Bass: WARMTH CLARITY GRIT STEADY CLEAN-UP
    {
        const auto base = StyleProfile::baseline (ChannelRole::BassDI, StyleProfileId::ModernGospel);
        auto at = [&] (int knob, float v) { MacroValues m = MacroMapping::defaults (Product::Bass); m.v[size_t (knob)] = v; return MacroMapping::apply (Product::Bass, base, m, RoleFamily::ElectricBass); };
        CHECK (at (0, 90.0f).toneBands[0].gainDb > base.toneBands[0].gainDb);
        CHECK (at (1, 90.0f).toneBands[2].gainDb > base.toneBands[2].gainDb);
        CHECK (at (2, 90.0f).satDrive > base.satDrive && at (2, 90.0f).satEnabled);
        CHECK (at (2, 10.0f).satDrive < base.satDrive);
        CHECK (! at (2, 0.0f).satEnabled);
        CHECK (at (3, 90.0f).compRatio > base.compRatio);
        CHECK (at (4, 100.0f).hpfHz > base.hpfHz && at (4, 100.0f).hpfHz <= 42.5f); // never above the low E
        CHECK (at (4, 90.0f).gateEnabled && ! at (4, 0.0f).gateEnabled);
        const auto synthBase = StyleProfile::baseline (ChannelRole::SynthBass, StyleProfileId::ModernGospel);
        MacroValues clean = MacroMapping::defaults (Product::Bass); clean.v[4] = 90.0f;
        CHECK (! MacroMapping::apply (Product::Bass, synthBase, clean, RoleFamily::SynthBass).gateEnabled);
    }
    // Drums through the generic entry point equals the released mapping.
    {
        const auto base = StyleProfile::baseline (ChannelRole::SnareTop, StyleProfileId::ModernWorship);
        Macros m; m.punch = 80.0f; m.character = 0.9f;
        CHECK (diffParameters (MacroMapping::apply (base, m, RoleFamily::Snare), MacroMapping::apply (Product::Drums, base, MacroMapping::fromDrums (m), RoleFamily::Snare)).empty());
    }
}

TEST_CASE ("Tune (from real stems): cuts are relative to the template so re-tuning never walks; continuous singing gets no expander; the vocal high-pass stays under the fundamental")
{
    // Full-drums stem: presence +5 dB and brilliance +3 dB over tolerance kept walking the shelf down 0.5 dB per re-tune.
    for (auto role : { ChannelRole::DrumBus, ChannelRole::MasterStream, ChannelRole::SnareTop, ChannelRole::LeadVocal })
    {
        auto a = onTarget (role);
        a.bandEnergyDb[size_t (Band::Presence)] += 9.0f;
        a.bandEnergyDb[size_t (Band::Brilliance)] += 7.0f;
        a.bandEnergyDb[size_t (Band::Low)] += 8.0f;
        auto ctx = context (role, a);
        auto first = TuneEngine::tune (ctx);
        REQUIRE (first.valid);
        for (int pass = 0; pass < 3; ++pass)
        {
            ctx.current = first.proposed;
            auto again = TuneEngine::tune (ctx);
            if (again.parametersChanged != 0)
                for (const auto& i : again.report.items)
                    if (! i.changes.empty()) testfw::reportFailure (__FILE__, __LINE__, std::string (channelRoleName (role)) + " walked on re-tune: " + i.what);
            CHECK (again.parametersChanged == 0);
            first = again;
        }
    }

    // Lead vocal stem: continuous singing (1 % silence, 0.1 phrases/s, floor 10 dB under the phrases). No expander, ever.
    auto sung = onTarget (ChannelRole::LeadVocal);
    sung.silencePercent = 1.0f; sung.transientsPerSecond = 0.1f; sung.transientCount = 1;
    sung.noiseFloorDb = sung.hitLevelDb - 10.0f; sung.bleedEstimate = 1.0f; sung.meanDecayMs = 2000.0f;
    auto rs = TuneEngine::tune (context (ChannelRole::LeadVocal, sung));
    CHECK (! rs.proposed.gateEnabled);
    CHECK (find (rs, TuneSection::Bleed, "continuous") != nullptr);

    // Male lead (126 Hz fundamental) with a loud low band: the high-pass must stay under the voice.
    auto male = onTarget (ChannelRole::LeadVocal);
    male.fundamentalHz = 126.0f; male.fundamentalLevelDb = -7.0f;
    male.bandEnergyDb[size_t (Band::Low)] += 12.0f;
    male.bandEnergyDb[size_t (Band::Sub)] += 10.0f;
    auto rm = TuneEngine::tune (context (ChannelRole::LeadVocal, male));
    CHECK (rm.proposed.hpfHz <= 126.0f * 0.8f + 0.5f);
    CHECK (rm.proposed.hpfHz >= 70.0f);
    {
        auto ctx = context (ChannelRole::LeadVocal, male);
        ctx.current = rm.proposed;
        CHECK (TuneEngine::tune (ctx).parametersChanged == 0);
    }

    // Master: a capture far below the healthy input range waits for the preamp instead of pushing the plugin gain.
    auto quiet = onTarget (ChannelRole::MasterStream);
    quiet.peakDb = -28.0f; quiet.hitLevelDb = -30.0f; quiet.loudnessLufs = -42.0f;
    auto ctxq = context (ChannelRole::MasterStream, quiet);
    auto rq = TuneEngine::tune (ctxq);
    CHECK (find (rq, TuneSection::Mix, "waits") != nullptr);
    CHECK_NEAR (rq.proposed.outputTrimDb, ctxq.current.outputTrimDb, 1e-4f);
    CHECK (rq.report.inputHealth == "Low");
}
