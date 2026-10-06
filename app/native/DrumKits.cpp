#include "DrumKits.h"
#include "MixController.h"
#include "SampleLibrary.h"

namespace livemix
{

const std::vector<DrumKit>& builtInDrumKits()
{
    // The toms are four sizes of one kit, so every kit shares them: rack toms high to low in
    // the order their strips come, the floor tom under them.
    static const std::vector<std::string> racks { "10 inch rack tom", "12 inch rack tom", "13 inch rack tom" };
    static const std::string floor { "16 inch floor tom" };
    static const std::vector<DrumKit> kits {
        { "Church", "Perfect kick", "Church snare",   racks, floor, "Perfect kick and Church snare: the everyday Sunday kit." },
        { "Punchy", "Punch kick",   "Powerful snare", racks, floor, "Punch kick and Powerful snare: for a band that pushes." },
        { "Soft",   "Soft kick",    "Crisp snare",    racks, floor, "Soft kick and Crisp snare: for a quiet set." },
        { "Big",    "Perfect kick", "Exploder snare", racks, floor, "Perfect kick and Exploder snare: for the big songs." },
    };
    return kits;
}

const DrumKit* findDrumKit (const std::string& name)
{
    for (const auto& k : builtInDrumKits())
        if (k.name == name) return &k;
    return nullptr;
}

std::array<SampleChoice, kMaxStrips> drumKitChoices (const DrumKit& kit, const MixController& controller)
{
    std::array<SampleChoice, kMaxStrips> out {};
    const auto& graph = controller.getGraph();
    size_t rack = 0;
    for (int i = 0; i < graph.numStrips() && i < kMaxStrips; ++i)
    {
        const auto role = graph.strips[size_t (i)].role;
        const auto family = roleFamily (role);
        std::string name;
        if (family == RoleFamily::Kick) name = kit.kick;
        else if (family == RoleFamily::Snare) name = kit.snare;
        else if (family == RoleFamily::Tom)
        {
            if (role == ChannelRole::FloorTom || kit.rackToms.empty()) name = kit.floorTom;
            else name = kit.rackToms[std::min (rack++, kit.rackToms.size() - 1)];
        }
        if (name.empty()) continue;
        out[size_t (i)] = { sampleFamilyFolder (family), name, false, {} };
    }
    return out;
}

std::string currentDrumKit (const std::array<SampleChoice, kMaxStrips>& now, const MixController& controller)
{
    // The strips a kit speaks for, and nothing else: a hi-hat's sound is its own business.
    bool anyDrum = false;
    const auto& graph = controller.getGraph();
    for (int i = 0; i < graph.numStrips() && i < kMaxStrips; ++i)
    {
        const auto f = roleFamily (graph.strips[size_t (i)].role);
        if (f == RoleFamily::Kick || f == RoleFamily::Snare || f == RoleFamily::Tom) anyDrum = true;
    }
    if (! anyDrum) return {};

    for (const auto& kit : builtInDrumKits())
    {
        const auto want = drumKitChoices (kit, controller);
        // A built-in is its family and its name (its path inside the bundle is not part of
        // what a kit names); one of the engineer's own sounds is never a kit's.
        bool all = true;
        for (int i = 0; i < kMaxStrips && all; ++i)
        {
            const auto& w = want[size_t (i)];
            const auto& n = now[size_t (i)];
            if (w.set() && (n.user || n.family != w.family || n.name != w.name)) all = false;
        }
        if (all) return kit.name;
    }
    return "Custom";
}

std::string applyDrumKit (const DrumKit& kit, const SampleLibrary& library, MixController& controller)
{
    const auto want = drumKitChoices (kit, controller);
    std::vector<std::pair<int, int>> slots;
    std::vector<std::string> missing;
    const auto& graph = controller.getGraph();
    for (int i = 0; i < kMaxStrips; ++i)
    {
        const auto& c = want[size_t (i)];
        if (! c.set()) continue;
        const auto family = sampleFamilyFromFolder (c.family);
        const int slot = library.slotFor (family, c.name, false, {});
        if (slot < 0) { missing.push_back (c.name); continue; }   // that strip keeps what it had
        slots.emplace_back (i, slot);
    }
    if (slots.empty())
        return graph.numStrips() == 0 ? std::string ("There are no drum channels to choose a kit for.")
                                      : "The " + kit.name + " kit's sounds are not on this Mac, so nothing changed.";

    controller.chooseSampleSounds (slots, "drum kit: " + kit.name);
    std::string said = "Drum kit: " + kit.name + ". Each drum's sound changed; its blend, sensitivity and alignment did not.";
    if (! missing.empty())
    {
        said += " Not on this Mac, so those drums kept their sound:";
        for (size_t k = 0; k < missing.size(); ++k) said += (k == 0 ? " " : ", ") + missing[k];
        said += ".";
    }
    return said;
}

} // namespace livemix
