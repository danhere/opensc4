// The network tool's placing in the autotest (part t6;
// src/core/nettool_place_1.cpp has the analysis, nettool_place.h the
// layouts).
//
// The daily part ` | ntpl i<intersections> t<tunnels> a<anchors> <hash>`
// (always, once the network manager is initialised; ` | ntpl none` before)
// reads the network manager's own 13 tools (GetNetworkTool(type, 0): no
// reference added) and hashes what this part leaves on them that no other
// part hashes, values only, read in place:
//   - per tool its type and its failure reason +0x20C (the nettool part
//     counts only the tools with one);
//   - the intersections (+0xCC, 0x28 bytes) in order: the rule ID, x, z,
//     the first step, the words +0x10 and +0x14 (never +0x18: the
//     original's stack leftover, see nettool_place_1.cpp; the key vector
//     +0x1C is the ntsv part's);
//   - the tunnel records (+0xE4, 16 bytes) in order: the two heights' bits
//     and the two steps;
//   - the anchor points (+0x118) in order (slot 7 and the segment placer
//     leave them; the nettool part has their count).
// The solved cells, the override records +0x8 and the intersections' keys
// are the ntsv part's, the edge records the ntoc part's, the occupants the
// city's. Valid in every state the game leaves a tool in (the vectors may
// be empty).
//
// OPENSC4_TEST_NETTOOL_PLACE=1 (with OPENSC4_TEST_NETTOOL=<day>) adds, on
// that day, read-only calls at the game's addresses (so the game's or
// ours, whichever is installed), on the test's own stack words and the
// RUL0 store:
//   edges  64 words from a fixed generator (each side byte's low three bits
//          the kinds 0 to 7), each with the next as the other word:
//          MergeSideKinds 0x00620F00, SetSideByte 0x00620FC0 for the four
//          sides (and whether EAX is the word's address), SetSideFromDirection
//          0x00620FF0 for the four sides and the eight directions,
//          ClearSideHighBits 0x00621440; every result hashed;
//   fits   FitsInCity 0x00608CD0 for every rule of the RUL0 store with a cell grid at seven
//          x by seven z points (inside, on and past the city's edges, and
//          negative ones), the answers summed order-free.
// Nothing of it touches the city or allocates: the words are the test's,
// the store is only read. One log line `autotest: ntpl: ...` gives the
// counts and the two hashes; the daily part then ends with ` drive <hash>`.
//
// The placing itself changes the city (occupants, the terrain, the
// neighbour records; even the preview path makes the RUL0 intersections'
// occupants and clears their cells through PlaceRuleCellOccupant), so it is
// never driven here: the crew test (OPENSC4_TEST_CREW) is its driver, with
// the game's drag (PlaceAllSegments(0, 0, 1, type), then (1, 0, 0, type))
// on land without networks.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netmgr_sim.h"
#include "core/nettool_place.h"
#include "core/nettool_sim.h"

#include <cISC4City.h>
#include <cISC4NetworkManager.h>

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

using nettool_sim::CallThis;
using nettool_sim::Vector;
namespace field = nettool_sim::field;
namespace table = nettool_sim::rul::table;
namespace isect = nettool_sim::place::isect;

constexpr std::size_t kTunnelSize = 0x10;
constexpr uint32_t kWords = 64;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

struct Sum {
    uint32_t count = 0, sum = 0, xored = 0;
    void Add(uint32_t h)
    {
        ++count;
        sum += h;
        xored ^= h;
    }
    void MixInto(Hash& hash) const
    {
        hash.Mix(count);
        hash.Mix(sum);
        hash.Mix(xored);
    }
};

cISC4NetworkManager* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    cISC4NetworkManager* const manager = city->GetNetworkManager();
    if (!manager || !At<uint8_t>(manager, netmgr_sim::field::kInitialized))
        return nullptr;
    return manager;
}

// The elements of a vector of `size`-byte elements, each element's words
// up to `words` bytes mixed in order; the count.
uint32_t MixVector(Hash& hash, char* self, std::size_t offset, std::size_t size, std::size_t words)
{
    const Vector& v = At<Vector>(self, offset);
    const uint32_t count = static_cast<uint32_t>(static_cast<std::size_t>(v.end - v.begin) / size);
    hash.Mix(count);
    for (uint32_t i = 0; i < count; ++i)
        for (std::size_t word = 0; word < words; word += 4)
            hash.Mix(At<uint32_t>(v.begin + i * size, word));
    return count;
}

// ---- The drive -------------------------------------------------------------------

bool g_done = false;
uint32_t g_sum = 0;

uint32_t DriveEdgeWords()
{
    Hash hash;
    uint32_t seed = 0x2545F491u;
    uint32_t words[kWords + 1];
    for (uint32_t i = 0; i <= kWords; ++i) {
        seed = seed * 1664525u + 1013904223u;
        words[i] = seed;
    }
    for (uint32_t i = 0; i < kWords; ++i) {
        uint32_t merged = words[i];
        CallThis<void, const uint32_t*>(addr::cSC4NetworkTool_MergeSideKinds, &merged, &words[i + 1]);
        hash.Mix(merged);
        uint32_t zero = 0, kept = words[i];
        CallThis<void, const uint32_t*>(addr::cSC4NetworkTool_MergeSideKinds, &kept, &zero);
        hash.Mix(kept);
        for (uint32_t side = 0; side < 4; ++side) {
            uint32_t word = words[i];
            uint32_t* const answer = CallThis<uint32_t*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideByte, &word,
                                                                             side, words[i + 1] >> (side * 3));
            hash.Mix(word);
            hash.Mix(answer == &word ? 1u : 0u);
            for (uint32_t direction = 0; direction < 8; ++direction) {
                uint32_t set = words[i];
                CallThis<void, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideFromDirection, &set, side,
                                                   direction | (words[i + 1] & 0xFFFFFF00u));
                hash.Mix(set);
            }
        }
        uint32_t cleared = words[i];
        CallThis<void>(addr::cSC4NetworkTool_ClearSideHighBits, &cleared);
        hash.Mix(cleared);
    }
    return hash.h;
}

Sum DriveFits(uint32_t sizeX, uint32_t sizeZ)
{
    Sum total;
    const int32_t xs[7] = {-3, -1, 0, 1, static_cast<int32_t>(sizeX / 2), static_cast<int32_t>(sizeX) - 1,
                           static_cast<int32_t>(sizeX)};
    const int32_t zs[7] = {-3, -1, 0, 1, static_cast<int32_t>(sizeZ / 2), static_cast<int32_t>(sizeZ) - 1,
                           static_cast<int32_t>(sizeZ)};
    char* const store = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store);
    const Vector& buckets = At<Vector>(store, table::kBuckets);
    for (char* const* at = reinterpret_cast<char* const*>(buckets.begin);
         at != reinterpret_cast<char* const*>(buckets.end); ++at)
        for (char* node = *at; node; node = At<char*>(node, table::kNext)) {
            char* const rule = node + nettool_sim::rul::store::kRule;
            // Only rules with a cell grid: a rule without one keeps the
            // original's stack leftovers in its bounds (+0x18 to +0x24), ours
            // zeros (part r1); the game reads them only behind a grid test.
            const Vector& grid = At<Vector>(rule, nettool_sim::rul::rul0::kCellGrid);
            if (grid.begin == grid.end)
                continue;
            Hash one;
            one.Mix(At<uint32_t>(node, table::kKey));
            for (const int32_t x : xs)
                for (const int32_t z : zs)
                    one.Mix(static_cast<uint8_t>(CallThis<uint32_t, int32_t, int32_t, uint32_t, uint32_t>(
                        addr::SC4Rul0Rule_FitsInCity, rule, x, z, sizeX, sizeZ)));
            total.Add(one.h);
        }
    return total;
}

void Drive(const Day& day)
{
    if (g_done || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL") ||
        EnvInt("OPENSC4_TEST_NETTOOL_PLACE", 0) != 1)
        return;
    g_done = true;
    if (!Manager(day.city)) {
        log::Write("autotest: ntpl: no network manager; nothing driven");
        return;
    }
    const uint32_t edges = DriveEdgeWords();
    const Sum fits = DriveFits(day.city->CellCountX(), day.city->CellCountZ());
    Hash hash;
    hash.Mix(edges);
    fits.MixInto(hash);
    g_sum = hash.h;
    log::Write("autotest: ntpl: drive: edge words %08X; %u rules fitted, %08X %08X; hash %08X", edges, fits.count,
               fits.sum, fits.xored, g_sum);
}

void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4NetworkManager* const manager = Manager(city);
    if (!manager) {
        line += " | ntpl none";
        return;
    }
    Hash hash;
    uint32_t intersections = 0, tunnels = 0, anchors = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(type), false));
        if (!self) {
            hash.Mix(0xFFFFFFFFu);
            continue;
        }
        hash.Mix(At<uint32_t>(self, field::kType));
        hash.Mix(At<uint32_t>(self, field::kFailure));
        intersections += MixVector(hash, self, field::kIntersections, isect::kSize, isect::kWord18);
        tunnels += MixVector(hash, self, field::kVectorE4, kTunnelSize, kTunnelSize);
        anchors += MixVector(hash, self, field::kAnchors, nettool_sim::kPointSize, nettool_sim::kPointSize);
    }
    char text[96];
    std::snprintf(text, sizeof text, " | ntpl i%u t%u a%u %08X", intersections, tunnels, anchors, hash.h);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " drive %08X", g_sum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"ntpl", 205, &Fingerprint};
const DayHook kDrive{"ntpl.drive", 205, &Drive};

} // namespace
} // namespace opensc4::autotest
