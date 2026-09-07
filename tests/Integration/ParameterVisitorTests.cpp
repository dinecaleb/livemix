#include "TestFramework.h"
#include "DSP/ChannelParameters.h"
#include <set>
#include <map>

using namespace livemix;

TEST_CASE ("Parameter visitor: ids are unique and the round trip is lossless")
{
    ChannelParameters p;
    std::set<std::string> ids;
    forEachDspParameter (p, [&] (const std::string& id, auto&) { CHECK (ids.insert (id).second); });
    CHECK (int (ids.size()) == countDspParameters());
    CHECK (ids.count ("gateThreshold") == 1);
    CHECK (ids.count ("toneEq4Type") == 1);
    CHECK (ids.count ("corrEq1Freq") == 1);

    // Write distinctive values, serialise to a map, then restore into a fresh struct.
    ChannelParameters src;
    src.gateThresholdDb = -33.0f; src.polarityInvert = true; src.hpfSlope = 1;
    src.toneBands[1].type = FilterType::HighShelf; src.toneBands[1].gainDb = -2.5f;
    src.correctiveBands[2].enabled = true; src.correctiveBands[2].freqHz = 777.0f;
    src.bypassAll = true;

    std::map<std::string, float> store;
    forEachDspParameter (src, [&] (const std::string& id, auto& v) { store[id] = float (v); });

    ChannelParameters dst;
    forEachDspParameter (dst, [&] (const std::string& id, auto& v)
    {
        using T = std::remove_reference_t<decltype (v)>;
        if constexpr (std::is_same_v<T, bool>) v = store[id] >= 0.5f;
        else if constexpr (std::is_same_v<T, int>) v = int (store[id]);
        else v = store[id];
    });

    CHECK_NEAR (dst.gateThresholdDb, -33.0f, 1e-6f);
    CHECK (dst.polarityInvert);
    CHECK (dst.hpfSlope == 1);
    CHECK (dst.toneBands[1].type == FilterType::HighShelf);
    CHECK_NEAR (dst.toneBands[1].gainDb, -2.5f, 1e-6f);
    CHECK (dst.correctiveBands[2].enabled);
    CHECK_NEAR (dst.correctiveBands[2].freqHz, 777.0f, 1e-6f);
    CHECK (dst.bypassAll);
}
