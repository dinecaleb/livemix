#include "RoutingGraph.h"
#include "Profiles/MixProfileData.h"
#include "Profiles/StyleProfile.h"
#include "FX/FxProfiles.h"
#include "Core/DbUtils.h"
#include <cstdio>
#include <algorithm>
#include <map>
#include <vector>

namespace livemix
{

RoutingGraph RoutingGraph::build (const MixSession& session)
{
    RoutingGraph g;
    g.busUsed[size_t (MixBus::Master)] = true;
    for (int s = 0; s < int (FxSlot::Count); ++s)
        g.fxType[size_t (s)] = MixProfile::fxTypeForSlot (FxSlot (s));

    // Spread is a family's business, not a role's: three toms walk left to right across the kit whether
    // they are called rack or floor, and two overheads are a pair. Counting per role instead put the
    // second floor tom at the far wall, because the spread was added to a home position that was already
    // off centre. The order is the family's own (a rack tom sits left of a floor tom), then the patch
    // order, so the same session always builds the same image.
    std::map<RoleFamily, std::vector<int>> familyMembers;
    for (int i = 0; i < int (session.inputs.size()); ++i)
    {
        const auto& in = session.inputs[size_t (i)];
        if (! in.enabled || in.inputA < 0 || in.isStereo()) continue;
        if (MixProfile::spreadForRole (in.role) > 0.0f) familyMembers[roleFamily (in.role)].push_back (i);
    }
    std::map<int, float> panForInput;
    for (auto& f : familyMembers)
    {
        auto& members = f.second;
        std::stable_sort (members.begin(), members.end(), [&session] (int a, int b)
                          { return int (session.inputs[size_t (a)].role) < int (session.inputs[size_t (b)].role); });
        const int count = int (members.size());
        if (count < 2) continue;   // one of a kind keeps its role's home position
        const float spread = MixProfile::spreadForRole (session.inputs[size_t (members[0])].role);
        for (int k = 0; k < count; ++k)
        {
            const float t = float (k) / float (count - 1);           // 0..1 across the family
            panForInput[members[size_t (k)]] = clamp (spread * (2.0f * t - 1.0f), -1.0f, 1.0f);
        }
    }

    for (int i = 0; i < int (session.inputs.size()) && int (g.strips.size()) < kMaxStrips; ++i)
    {
        const auto& in = session.inputs[size_t (i)];
        if (! in.enabled || in.inputA < 0) continue;
        if (roleFamily (in.role) == RoleFamily::Master) continue; // the master is never an input

        StripRoute r;
        r.input = i;
        r.name = in.name.empty() ? channelRoleName (in.role) : in.name;
        r.icon = in.icon;
        r.role = in.role;
        r.inputA = in.inputA;
        r.inputB = in.inputB;
        r.bus = mixBusForRole (in.role);
        g.busUsed[size_t (r.bus)] = true;

        // Pan: the role's home position, or the place the family's spread gave it.
        const auto placed = panForInput.find (i);
        r.pan = in.isStereo() ? 0.0f
                             : (placed != panForInput.end() ? placed->second : MixProfile::defaultPan (in.role));

        for (int s = 0; s < int (FxSlot::Count); ++s)
        {
            r.sendDb[size_t (s)] = MixProfile::defaultSendDb (session.profile, roleFamily (in.role), FxSlot (s));
            if (r.sendDb[size_t (s)] > kSilenceDb) g.fxUsed[size_t (s)] = true;
        }
        g.strips.push_back (r);
    }
    return g;
}

int RoutingGraph::stripsOnBus (MixBus b) const noexcept
{
    int n = 0;
    for (const auto& s : strips) if (s.bus == b) ++n;
    return n;
}

std::string RoutingGraph::describe() const
{
    std::string out;
    char line[256];
    for (const auto& s : strips)
    {
        std::snprintf (line, sizeof (line), "%-14s %-16s %s -> %s", s.name.c_str(), channelRoleName (s.role),
                       s.numChannels() == 2 ? "stereo" : "mono  ", mixBusName (s.bus));
        out += line;
        if (s.pan != 0.0f) { std::snprintf (line, sizeof (line), "  pan %+.2f", double (s.pan)); out += line; }
        bool first = true;
        for (int f = 0; f < int (FxSlot::Count); ++f)
        {
            if (s.sendDb[size_t (f)] <= kSilenceDb) continue;
            std::snprintf (line, sizeof (line), "%s%s %.0f dB", first ? "  sends: " : ", ", fxSlotName (FxSlot (f)), double (s.sendDb[size_t (f)]));
            out += line;
            first = false;
        }
        out += '\n';
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
        if (busUsed[size_t (b)] && MixBus (b) != MixBus::Master)
        {
            std::snprintf (line, sizeof (line), "%-14s bus (%d strips) -> MASTER\n", mixBusName (MixBus (b)), stripsOnBus (MixBus (b)));
            out += line;
        }
    for (int f = 0; f < int (FxSlot::Count); ++f)
        if (fxUsed[size_t (f)])
        {
            std::snprintf (line, sizeof (line), "%-14s return (%s) -> MASTER\n", fxSlotName (FxSlot (f)), fxTypeName (fxType[size_t (f)]));
            out += line;
        }
    return out;
}

MixParameters startingPoint (const MixSession& session, const RoutingGraph& graph)
{
    MixParameters p;
    p.numStrips = graph.numStrips();
    for (int i = 0; i < p.numStrips; ++i)
    {
        const auto& r = graph.strips[size_t (i)];
        auto& s = p.strips[size_t (i)];
        s.channel = StyleProfile::baseline (r.role, session.profile);
        s.faderDb = 0.0f;
        s.pan = r.pan;
        s.mute = false;
        s.solo = false;
        s.sendDb = r.sendDb;
    }
    for (int b = 0; b < int (MixBus::Count); ++b)
    {
        auto& bus = p.buses[size_t (b)];
        bus.channel = StyleProfile::baseline (busRole (MixBus (b), session.purpose), session.profile);
        bus.faderDb = MixProfile::defaultBusFaderDb (session.profile, MixBus (b));
        bus.mute = false;
        bus.solo = false;
    }
    for (int f = 0; f < int (FxSlot::Count); ++f)
    {
        auto& fx = p.fx[size_t (f)];
        fx.fx = FxProfiles::baseline (session.profile, graph.fxType[size_t (f)]);
        fx.fx.mix = 1.0f;          // a return is wet only
        fx.fx.bypassAll = false;
        fx.returnDb = MixProfile::defaultReturnDb (session.profile, FxSlot (f));
        fx.enabled = graph.fxUsed[size_t (f)];
    }
    return p;
}

} // namespace livemix
