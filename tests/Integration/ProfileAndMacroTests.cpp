#include "TestFramework.h"
#include "Profiles/StyleProfile.h"
#include "Profiles/MacroMapping.h"

using namespace livemix;

TEST_CASE ("StyleProfile: every role/style baseline is within parameter bounds")
{
    for (int r = 0; r < int (ChannelRole::Count); ++r)
        for (int s = 0; s < int (StyleProfileId::Count); ++s)
        {
            auto p = StyleProfile::baseline (ChannelRole (r), StyleProfileId (s));
            CHECK (p.compRatio >= 1.0f && p.compRatio <= 20.0f);
            CHECK (p.compAttackMs >= 0.1f && p.compAttackMs <= 200.0f);
            CHECK (p.hpfHz >= 20.0f && p.hpfHz <= 1000.0f);
            CHECK (p.gateRangeDb >= 0.0f && p.gateRangeDb <= 80.0f);
            CHECK (p.satDrive >= 0.0f && p.satDrive <= 1.0f);
            for (auto& b : p.toneBands) CHECK (std::fabs (b.gainDb) <= 12.0f);
            auto t = StyleProfile::targets (ChannelRole (r), StyleProfileId (s));
            CHECK (t.capturePeakMinDb < t.capturePeakMaxDb);
        }
    // Overheads never get a gate by default.
    CHECK (! StyleProfile::baseline (ChannelRole::OverheadLeft, StyleProfileId::ModernWorship).gateEnabled);
    CHECK (StyleProfile::baseline (ChannelRole::KickIn, StyleProfileId::ModernWorship).gateEnabled);
}

TEST_CASE ("MacroMapping: centre position reproduces the baseline; extremes stay bounded")
{
    for (int r = 0; r < int (ChannelRole::Count); ++r)
    {
        const ChannelRole role = ChannelRole (r);
        auto base = StyleProfile::baseline (role, StyleProfileId::ModernWorship);
        Macros centre;
        auto same = MacroMapping::apply (base, centre, roleFamily (role));
        CHECK_NEAR (same.compRatio, base.compRatio, 1e-4f);
        CHECK_NEAR (same.compAttackMs, base.compAttackMs, 1e-4f);
        CHECK_NEAR (same.toneBands[0].gainDb, base.toneBands[0].gainDb, 1e-4f);
        CHECK_NEAR (same.toneBands[2].gainDb, base.toneBands[2].gainDb, 1e-4f);
        CHECK_NEAR (same.gateRangeDb, base.gateRangeDb, 1e-4f);
        CHECK (same.gateEnabled == base.gateEnabled);
        CHECK (same.satEnabled == base.satEnabled);

        for (float v : { 0.0f, 100.0f })
        {
            Macros m; m.punch = v; m.body = v; m.attack = v; m.bleedReduction = v; m.character = v / 100.0f;
            auto p = MacroMapping::apply (base, m, roleFamily (role));
            CHECK (p.compRatio >= 1.0f && p.compRatio <= 20.0f);
            CHECK (p.compAttackMs >= 0.1f && p.compAttackMs <= 200.0f);
            CHECK (p.transientAttack >= -1.0f && p.transientAttack <= 1.0f);
            CHECK (p.gateRangeDb >= 0.0f && p.gateRangeDb <= 80.0f);
            CHECK (p.satDrive <= 0.7f);
            for (auto& b : p.toneBands) CHECK (std::fabs (b.gainDb) <= 12.0f);
        }
    }
}

TEST_CASE ("MacroMapping: directions are musically sensible")
{
    auto base = StyleProfile::baseline (ChannelRole::SnareTop, StyleProfileId::ModernWorship);
    Macros more; more.punch = 90.0f;
    auto p = MacroMapping::apply (base, more, RoleFamily::Snare);
    CHECK (p.compRatio > base.compRatio);
    CHECK (p.transientAttack > base.transientAttack);
    CHECK (p.toneBands[2].gainDb > base.toneBands[2].gainDb);

    Macros body; body.body = 90.0f;
    CHECK (MacroMapping::apply (base, body, RoleFamily::Snare).toneBands[0].gainDb > base.toneBands[0].gainDb);

    Macros bleed; bleed.bleedReduction = 100.0f;
    auto g = MacroMapping::apply (base, bleed, RoleFamily::Snare);
    CHECK (g.gateEnabled);
    CHECK (g.gateRangeDb > base.gateRangeDb);

    Macros noBleed; noBleed.bleedReduction = 0.0f;
    CHECK (! MacroMapping::apply (base, noBleed, RoleFamily::Snare).gateEnabled);

    auto oh = StyleProfile::baseline (ChannelRole::Overhead, StyleProfileId::ModernWorship);
    auto ohBleed = MacroMapping::apply (oh, bleed, RoleFamily::Overhead);
    CHECK (! ohBleed.gateEnabled);
    CHECK (ohBleed.hpfHz > oh.hpfHz);

    Macros aggressive; aggressive.character = 1.0f;
    auto a = MacroMapping::apply (base, aggressive, RoleFamily::Snare);
    CHECK (a.satEnabled);
    CHECK (a.compRatio > base.compRatio);

    CHECK (! MacroMapping::affectedParameterIds().empty());
}
