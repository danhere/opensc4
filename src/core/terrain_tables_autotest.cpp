// The autotest's part for the terrain's startup tables (docs/terrain.md):
// what the three loaders of terrain_tables.cpp and terrain_tables_ini.cpp
// write, read back from memory.
//
// Fingerprint part "terraintables" (order 125):
//   d<byte>          the set-up's "done" byte 0x00B4BC3C
//   x<n>:<hash>      the texture exemplars (0x0074F410): n records in the
//                    map 0x00B4BCDC
//   i<n>:<hash>      the INI resource (0x0074DC30): n IDs in the list
//                    0x00B4BCD8
//   t<n>:<hash>      the tuning exemplar (0x0074B6A0): n random words in the
//                    five tables
// Each hash is over every destination its loader writes, in the order of
// the table below: scalars by their bits, vectors by their length and
// contents, curves by their segment count and segments, the map by its
// records (bucket by bucket, which is the order lookups walk), the list by
// its values in order. No address goes into a hash or the output.
//
// One exception: a texture record's reach (+0x54) and last x (+0x68) of a
// curve without segments are left out, because the game leaves stack memory
// there when the curve's property is missing (terrain_tables.cpp). The
// record's first float (+0) has the same weakness when property 0x09C2AEF0
// is missing, and is hashed all the same: nothing tells the two cases apart.
// The game's own data has every property.
//
// The values are fixed once loaded, so the part is computed once and kept;
// it is computed again only when the "done" byte changes.
//
// OPENSC4_TEST_TERRAIN_TABLES=1: the first time the part is computed, each
// destination's own hash on a line of the log ("terrain tables test: <name>:
// <hash>"), so that a difference between two runs can be located.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

using terrain_sim::AtAddress;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

enum class Kind {
    Dwords,   // `count` dwords at the address
    Bytes,    // `count` bytes at the address
    Vector32, // a vector of dwords (floats by their bits)
    Vector16, // a vector of words
    Curve,    // an SC4Curve (0x10 bytes)
    Records,  // the hash map of texture records
    List,     // the sorted list of texture IDs
};

struct Destination {
    const char* name;
    char loader; // 'x' the texture exemplars, 'i' the INI resource, 't' the tuning exemplar
    Kind kind;
    std::uintptr_t address;
    uint32_t count;
};

constexpr Destination kDestinations[] = {
    // 0x0074F410 / 0x0074F180
    {"TextureIDLimit", 'x', Kind::Dwords, addr::cSTETerrain_TextureIDLimit, 1},
    {"TextureReachMax", 'x', Kind::Dwords, addr::cSTETerrain_TextureReachMax, 5},
    {"TextureRecords", 'x', Kind::Records, addr::cSTETerrain_TextureRecords, 0},
    // 0x0074DC30 / 0x0074D5A0 / 0x0074C800
    {"TextureMap", 'i', Kind::Dwords, addr::cSTETerrain_TextureMap, 32 * 16},
    {"TextureIDs", 'i', Kind::List, addr::cSTETerrain_TextureIDs, 0},
    {"HighCliffTexture", 'i', Kind::Dwords, addr::cSTETerrain_HighCliffTexture, 1},
    {"LowCliffTexture", 'i', Kind::Dwords, addr::cSTETerrain_LowCliffTexture, 1},
    {"BeachTexture", 'i', Kind::Dwords, addr::cSTETerrain_BeachTexture, 1},
    {"IniRaiseLowerAmount", 'i', Kind::Dwords, addr::cSTETerrain_IniRaiseLowerAmount, 1},
    {"IniEnableSimulation", 'i', Kind::Bytes, addr::cSTETerrain_IniEnableSimulation, 1},
    {"SmoothOnlyPasses", 'i', Kind::Dwords, addr::cSTETerrain_SmoothOnlyPasses, 1},
    {"IniWindIterationsPerMonth", 'i', Kind::Dwords, addr::cSTETerrain_IniWindIterationsPerMonth, 1},
    {"IniTalusAngle", 'i', Kind::Dwords, addr::cSTETerrain_IniTalusAngle, 1},
    {"SmoothStraightLimit", 'i', Kind::Dwords, addr::cSTETerrain_SmoothStraightLimit, 1},
    {"SmoothDiagonalLimit", 'i', Kind::Dwords, addr::cSTETerrain_SmoothDiagonalLimit, 1},
    {"SmoothFactor", 'i', Kind::Dwords, addr::cSTETerrain_SmoothFactor, 1},
    {"ErodeRuns", 'i', Kind::Dwords, addr::cSTETerrain_ErodeRuns, 1},
    {"ErodeIterations", 'i', Kind::Dwords, addr::cSTETerrain_ErodeIterations, 1},
    {"IniSurfaceWaterThreshold", 'i', Kind::Dwords, addr::cSTETerrain_IniSurfaceWaterThreshold, 1},
    {"ErodeRain", 'i', Kind::Dwords, addr::cSTETerrain_ErodeRain, 1},
    {"IniWaterSourceCapacity", 'i', Kind::Dwords, addr::cSTETerrain_IniWaterSourceCapacity, 1},
    {"IniWaterSinkCapacity", 'i', Kind::Dwords, addr::cSTETerrain_IniWaterSinkCapacity, 1},
    {"ErodeCapacity", 'i', Kind::Dwords, addr::cSTETerrain_ErodeCapacity, 1},
    {"ErodeRateLow", 'i', Kind::Dwords, addr::cSTETerrain_ErodeRateLow, 1},
    {"ErodeRateHigh", 'i', Kind::Dwords, addr::cSTETerrain_ErodeRateHigh, 1},
    {"ErodeSlopeLow", 'i', Kind::Dwords, addr::cSTETerrain_ErodeSlopeLow, 1},
    {"ErodeSlopeHigh", 'i', Kind::Dwords, addr::cSTETerrain_ErodeSlopeHigh, 1},
    {"ErodeDeposit", 'i', Kind::Dwords, addr::cSTETerrain_ErodeDeposit, 1},
    {"IniSedimentSpreadUnderSea", 'i', Kind::Dwords, addr::cSTETerrain_IniSedimentSpreadUnderSea, 1},
    {"IniDepositRateUnderSea", 'i', Kind::Dwords, addr::cSTETerrain_IniDepositRateUnderSea, 1},
    {"IniSedimentInfluence", 'i', Kind::Dwords, addr::cSTETerrain_IniSedimentInfluence, 1},
    {"IniMaxSedimentAlpha", 'i', Kind::Dwords, addr::cSTETerrain_IniMaxSedimentAlpha, 1},
    {"IniSedimentColor", 'i', Kind::Bytes, addr::cSTETerrain_IniSedimentColor, 3},
    // 0x0074B6A0 / 0x0074B460
    {"NoiseSteps", 't', Kind::Vector32, addr::cSTETerrain_NoiseSteps, 0},
    {"NoiseSides", 't', Kind::Dwords, addr::cSTETerrain_NoiseSides, 5},
    {"NoiseTable0", 't', Kind::Vector16, addr::cSTETerrain_NoiseTables, 0},
    {"NoiseTable1", 't', Kind::Vector16, addr::cSTETerrain_NoiseTables + 0xC, 0},
    {"NoiseTable2", 't', Kind::Vector16, addr::cSTETerrain_NoiseTables + 0x18, 0},
    {"NoiseTable3", 't', Kind::Vector16, addr::cSTETerrain_NoiseTables + 0x24, 0},
    {"NoiseTable4", 't', Kind::Vector16, addr::cSTETerrain_NoiseTables + 0x30, 0},
    {"SizeTypeFloats", 't', Kind::Vector32, addr::cSTETerrain_SizeTypeFloats, 0},
    {"MaxAllowedHeightValue", 't', Kind::Dwords, addr::cSTETerrain_MaxAllowedHeightValue, 1},
    {"MinAllowedHeightValue", 't', Kind::Dwords, addr::cSTETerrain_MinAllowedHeightValue, 1},
    {"DefaultSeaLevel", 't', Kind::Dwords, addr::cSTETerrain_DefaultSeaLevel, 1},
    {"DefaultNewHeight", 't', Kind::Dwords, addr::cSTETerrain_DefaultNewHeight, 1},
    {"Tuning7B22", 't', Kind::Vector32, addr::cSTETerrain_Tuning7B22, 0},
    {"Tuning7B23", 't', Kind::Vector32, addr::cSTETerrain_Tuning7B23, 0},
    {"Tuning7B24", 't', Kind::Vector32, addr::cSTETerrain_Tuning7B24, 0},
    {"Tuning7B25", 't', Kind::Vector32, addr::cSTETerrain_Tuning7B25, 0},
    {"HeightUnit", 't', Kind::Dwords, addr::cSTETerrain_HeightUnit, 1},
    {"Tuning7B30", 't', Kind::Dwords, addr::cSTETerrain_Tuning7B30, 1},
    {"InitScale", 't', Kind::Dwords, addr::cSTETerrain_InitScale, 1},
    {"Tuning7B40", 't', Kind::Dwords, addr::cSTETerrain_Tuning7B40, 1},
    {"Tuning7B41", 't', Kind::Dwords, addr::cSTETerrain_Tuning7B41, 1},
    {"Tuning7B42", 't', Kind::Dwords, addr::cSTETerrain_Tuning7B42, 1},
    {"Tuning7B43", 't', Kind::Vector32, addr::cSTETerrain_Tuning7B43, 0},
    {"FlatMinCount", 't', Kind::Dwords, addr::cSTETerrain_FlatMinCount, 1},
    {"FlatReachFactor", 't', Kind::Dwords, addr::cSTETerrain_FlatReachFactor, 1},
    {"FlatBandHigh", 't', Kind::Dwords, addr::cSTETerrain_FlatBandHigh, 1},
    {"FlatBandLow", 't', Kind::Dwords, addr::cSTETerrain_FlatBandLow, 1},
    {"FlatSpread", 't', Kind::Dwords, addr::cSTETerrain_FlatSpread, 1},
    {"Tuning5640", 't', Kind::Dwords, addr::cSTETerrain_Tuning5640, 1},
    {"Tuning5641", 't', Kind::Dwords, addr::cSTETerrain_Tuning5641, 1},
    {"ShoreMinRun", 't', Kind::Dwords, addr::cSTETerrain_ShoreMinRun, 1},
    {"ShoreMaxRun", 't', Kind::Dwords, addr::cSTETerrain_ShoreMaxRun, 1},
    {"SteepNormalY", 't', Kind::Dwords, addr::cSTETerrain_SteepNormalY, 1},
    {"ShoreMinHeightSum", 't', Kind::Dwords, addr::cSTETerrain_ShoreMinHeightSum, 1},
    {"ShoreCliffLow", 't', Kind::Dwords, addr::cSTETerrain_ShoreCliffLow, 1},
    {"ShoreCliffHigh", 't', Kind::Dwords, addr::cSTETerrain_ShoreCliffHigh, 1},
    {"MinSeaLevel", 't', Kind::Dwords, addr::cSTETerrain_MinSeaLevel, 1},
    {"MaxSeaLevel", 't', Kind::Dwords, addr::cSTETerrain_MaxSeaLevel, 1},
    {"SeaLevelStep", 't', Kind::Dwords, addr::cSTETerrain_SeaLevelStep, 1},
    {"DefaultRaiseLowerAmount", 't', Kind::Dwords, addr::cSTETerrain_DefaultRaiseLowerAmount, 1},
    {"WaterDistanceLimit", 't', Kind::Bytes, addr::cSTETerrain_WaterDistanceLimit, 1},
    {"Tuning83D1", 't', Kind::Dwords, addr::cSTETerrain_Tuning83D1, 1},
    {"Tuning7B80", 't', Kind::Bytes, addr::cSTETerrain_Tuning7B80, 1},
    {"Tuning7B81", 't', Kind::Bytes, addr::cSTETerrain_Tuning7B81, 1},
    {"Tuning7BA0", 't', Kind::Vector32, addr::cSTETerrain_Tuning7BA0, 0},
    {"PendingExpiry", 't', Kind::Dwords, addr::cSTETerrain_PendingExpiry, 1},
    {"PendingRepeat", 't', Kind::Dwords, addr::cSTETerrain_PendingRepeat, 1},
    {"ErodeIterationsPerStep", 't', Kind::Vector32, addr::cSTETerrain_ErodeIterationsPerStep, 0},
    {"ErodeSmoothPasses", 't', Kind::Dwords, addr::cSTETerrain_ErodeSmoothPasses, 1},
    {"EdgeReachCurve", 't', Kind::Curve, addr::cSTETerrain_EdgeReachCurve, 0},
    {"EdgeSmoothPasses", 't', Kind::Dwords, addr::cSTETerrain_EdgeSmoothPasses, 1},
    {"EdgeTolerance", 't', Kind::Dwords, addr::cSTETerrain_EdgeTolerance, 1},
};

// Limits that only stop a walk over memory that is not what it should be.
constexpr uint32_t kMostElements = 1u << 22, kMostNodes = 1u << 16;

// A vector of `size`-byte elements at `address` (begin, end): its length
// and elements into the hash; the length, or 0xFFFFFFFF when the pointers
// make no sense.
uint32_t MixVector(Hash& hash, uint32_t address, uint32_t size)
{
    const uint32_t begin = AtAddress<uint32_t>(address), end = AtAddress<uint32_t>(address + 4u);
    if (end < begin || (!begin && end) || (end - begin) / size > kMostElements) {
        hash.Mix(0xFFFFFFFFu);
        return 0xFFFFFFFFu;
    }
    const uint32_t count = (end - begin) / size;
    hash.Mix(count);
    for (uint32_t i = 0; i < count; ++i)
        hash.Mix(size == 2u ? AtAddress<uint16_t>(begin + 2u * i) : AtAddress<uint32_t>(begin + 4u * i));
    return count;
}

// An SC4Curve: its segment count (+0xC) and its segments (0x14 bytes each,
// five dwords) into the hash; the number of segments its vector holds.
uint32_t MixCurve(Hash& hash, uint32_t address)
{
    hash.Mix(AtAddress<uint32_t>(address + 0xCu));
    const uint32_t segments = MixVector(hash, address, 4u);
    return segments == 0xFFFFFFFFu ? 0u : segments / 5u;
}

// The map of texture records: its element count, its bucket count, and
// bucket by bucket each node's key and record; the number of records
// walked.
uint32_t MixRecords(Hash& hash, uint32_t map)
{
    const uint32_t buckets = AtAddress<uint32_t>(map + 4u), bucketsEnd = AtAddress<uint32_t>(map + 8u);
    hash.Mix(AtAddress<uint32_t>(map + 0x10u));
    if (!buckets || bucketsEnd < buckets || (bucketsEnd - buckets) / 4u > kMostElements) {
        hash.Mix(0xFFFFFFFFu);
        return 0;
    }
    const uint32_t bucketCount = (bucketsEnd - buckets) / 4u;
    hash.Mix(bucketCount);
    uint32_t records = 0;
    for (uint32_t b = 0; b < bucketCount; ++b) {
        for (uint32_t node = AtAddress<uint32_t>(buckets + 4u * b); node && records < kMostNodes; node = AtAddress<uint32_t>(node)) {
            ++records;
            hash.Mix(b);
            hash.Mix(AtAddress<uint32_t>(node + 4u));
            const uint32_t record = node + 8u;
            hash.Mix(AtAddress<uint32_t>(record));
            for (uint32_t i = 0; i < 5; ++i) {
                if (MixCurve(hash, record + 4u + 0x10u * i) != 0) {
                    hash.Mix(AtAddress<uint32_t>(record + 0x54u + 4u * i));
                    hash.Mix(AtAddress<uint32_t>(record + 0x68u + 4u * i));
                }
            }
        }
    }
    return records;
}

// The list of texture IDs (a pointer to its end node; nodes: next,
// previous, value): the values in order; how many.
uint32_t MixList(Hash& hash, uint32_t list)
{
    const uint32_t head = AtAddress<uint32_t>(list);
    if (!head) {
        hash.Mix(0xFFFFFFFFu);
        return 0;
    }
    uint32_t count = 0;
    for (uint32_t node = AtAddress<uint32_t>(head); node && node != head && count < kMostNodes; node = AtAddress<uint32_t>(node)) {
        hash.Mix(AtAddress<uint32_t>(node + 8u));
        ++count;
    }
    hash.Mix(count);
    return count;
}

// One destination into the hash; its element count.
uint32_t MixDestination(Hash& hash, const Destination& d)
{
    const uint32_t address = static_cast<uint32_t>(d.address);
    switch (d.kind) {
    case Kind::Dwords:
        for (uint32_t i = 0; i < d.count; ++i)
            hash.Mix(AtAddress<uint32_t>(address + 4u * i));
        return d.count;
    case Kind::Bytes:
        for (uint32_t i = 0; i < d.count; ++i)
            hash.Mix(AtAddress<uint8_t>(address + i));
        return d.count;
    case Kind::Vector32:
        return MixVector(hash, address, 4u);
    case Kind::Vector16:
        return MixVector(hash, address, 2u);
    case Kind::Curve:
        return MixCurve(hash, address);
    case Kind::Records:
        return MixRecords(hash, address);
    case Kind::List:
        return MixList(hash, address);
    }
    return 0;
}

std::string g_text;
int g_textDone = -1; // the "done" byte the text was computed with; -1: not yet
bool g_listed = false;

void Compute(uint8_t done)
{
    const bool list = !g_listed && EnvInt("OPENSC4_TEST_TERRAIN_TABLES", 0) != 0;
    Hash exemplars, ini, tuning;
    uint32_t records = 0, ids = 0, words = 0;
    for (const Destination& d : kDestinations) {
        Hash own;
        const uint32_t count = MixDestination(own, d);
        Hash& total = d.loader == 'x' ? exemplars : d.loader == 'i' ? ini : tuning;
        total.Mix(own.h);
        if (d.kind == Kind::Records)
            records = count;
        else if (d.kind == Kind::List)
            ids = count;
        else if (d.kind == Kind::Vector16 && count != 0xFFFFFFFFu)
            words += count;
        if (list)
            log::Write("terrain tables test: %s: %08X", d.name, own.h);
    }
    if (list)
        g_listed = true;
    char text[160];
    std::snprintf(text, sizeof text, " | terraintables d%u x%u:%08X i%u:%08X t%u:%08X", static_cast<unsigned>(done), records,
                  exemplars.h, ids, ini.h, words, tuning.h);
    g_text = text;
    g_textDone = done;
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (!game::IsBuild641())
        return;
    const uint8_t done = Global<uint8_t>(addr::cSTETerrain_StaticsDone);
    if (g_textDone != static_cast<int>(done))
        Compute(done);
    line += g_text;
}

const FingerprintPart kFingerprint{"terraintables", 125, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
