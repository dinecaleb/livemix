#include "TestFramework.h"
#include "Mix/MixMacros.h"
#include "Mix/MixPlanner.h"
#include "Core/Constants.h"

using namespace livemix;

namespace
{
    MixSession session()
    {
        MixSession s;
        s.inputs = { { "Kick", ChannelRole::KickIn, 0, -1 }, { "Bass", ChannelRole::BassDI, 1, -1 }, { "Keys", ChannelRole::Piano, 2, 3 },
                     { "Lead", ChannelRole::LeadVocal, 4, -1 }, { "Vox", ChannelRole::BackingVocal, 5, -1 } };
        return s;
    }
}

TEST_CASE ("MixMacros: the middle position is the plan exactly, and moves are bounded and deterministic")
{
    const auto s = session();
    const auto g = RoutingGraph::build (s);
    const auto base = startingPoint (s, g);
    MixMacroValues neutral;
    CHECK (neutral.isNeutral());
    CHECK (MixPlanner::countParameterChanges (base, MixMacros::apply (base, neutral, g, s.profile)) == 0);

    MixMacroValues wet; wet.set (MixMacro::Space, 100.0f);
    const auto w = MixMacros::apply (base, wet, g, s.profile);
    const int lead = 3;
    CHECK_NEAR (w.strips[lead].sendDb[size_t (FxSlot::VocalPlate)], base.strips[lead].sendDb[size_t (FxSlot::VocalPlate)] + 12.0f, 1e-4);
    CHECK (w.strips[0].sendDb[size_t (FxSlot::VocalPlate)] <= kSilenceDb);   // the kick never had a plate send and does not get one
    MixMacroValues dry; dry.set (MixMacro::Space, 0.0f);
    const auto d = MixMacros::apply (base, dry, g, s.profile);
    CHECK_NEAR (d.strips[lead].sendDb[size_t (FxSlot::VocalPlate)], base.strips[lead].sendDb[size_t (FxSlot::VocalPlate)] - 12.0f, 1e-4);

    MixMacroValues bright; bright.set (MixMacro::Vocals, 100.0f);
    const auto b = MixMacros::apply (base, bright, g, s.profile);
    const auto& vb = b.buses[size_t (MixBus::Vocals)].channel;
    CHECK (vb.toneBands[3].enabled);
    CHECK (vb.toneBands[3].gainDb > base.buses[size_t (MixBus::Vocals)].channel.toneBands[3].gainDb);
    CHECK (vb.toneBands[3].gainDb <= 6.0f);
    // Nothing outside the vocal bus moved.
    CHECK (diffParameters (base.buses[size_t (MixBus::Drums)].channel, b.buses[size_t (MixBus::Drums)].channel).empty());
    CHECK (diffParameters (base.strips[lead].channel, b.strips[lead].channel).empty());

    MixMacroValues tight; tight.set (MixMacro::Drums, 0.0f);
    const auto t = MixMacros::apply (base, tight, g, s.profile);
    CHECK (t.buses[size_t (MixBus::Drums)].channel.compRatio > base.buses[size_t (MixBus::Drums)].channel.compRatio);
    CHECK (t.buses[size_t (MixBus::Drums)].channel.compAttackMs < base.buses[size_t (MixBus::Drums)].channel.compAttackMs);

    MixMacroValues huge; huge.set (MixMacro::Bass, 100.0f);
    const auto h = MixMacros::apply (base, huge, g, s.profile);
    CHECK (h.buses[size_t (MixBus::Bass)].channel.satEnabled);
    CHECK (h.buses[size_t (MixBus::Bass)].channel.satDrive > base.buses[size_t (MixBus::Bass)].channel.satDrive);

    MixMacroValues polished; polished.set (MixMacro::Energy, 100.0f);
    const auto e = MixMacros::apply (base, polished, g, s.profile);
    CHECK (e.master().channel.compThresholdDb < base.master().channel.compThresholdDb);
    CHECK (e.master().channel.limiterEnabled == base.master().channel.limiterEnabled);

    // Same inputs, same output.
    CHECK (MixPlanner::countParameterChanges (MixMacros::apply (base, polished, g, s.profile), e) == 0);
}
