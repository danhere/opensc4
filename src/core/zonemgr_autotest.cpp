// The zone manager's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/zonemgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

using zonemgr_sim::GridCell;
using zonemgr_sim::kTypes;
using zonemgr_sim::Tree;
using zonemgr_sim::TreeNext;
namespace field = zonemgr_sim::field;
namespace slot = zonemgr_sim::slot;
namespace node = zonemgr_sim::node;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixWords(char* object, std::size_t offset, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
            Mix(At<uint32_t>(object, offset + i * 4));
    }
};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
using SelfFn = void(__thiscall*)(void* self);

// SC4CellRegion as the zone manager takes it: the rectangle and its cells
// (vtable, width, depth, a word, the rows of bits).
struct CellRegion {
    int32_t x0, z0, x1, z1;
    uint32_t cells[5];
};

// The count and (type, reason, value) of every entry of one of the two
// maps, in order.
void MixMap(Hash& hash, const Tree* tree)
{
    hash.Mix(tree->count);
    char* const head = tree->head;
    uint32_t walked = 0;
    for (char* at = At<char*>(head, node::kLeft); at != head && walked < 4096; at = TreeNext(at), ++walked) {
        hash.Mix(At<uint8_t>(at, node::kType));
        hash.Mix(At<uint8_t>(at, node::kReason));
        hash.Mix(At<uint32_t>(at, node::kValue));
    }
}

// The nodes of a std::set below `at` freed (color, parent, left, right).
void FreeSetNodes(char* at)
{
    while (at) {
        FreeSetNodes(At<char*>(at, 0xC));
        char* const left = At<char*>(at, 0x8);
        reinterpret_cast<FreeFn>(addr::SC4_Free)(at);
        at = left;
    }
}

// PlaceZone (slot 11) without placing: the answer, the cost, the error,
// the cells left in the region and, when asked for, the number of lots it
// would demolish (an empty std::set of our own for the tenth argument).
// Nothing is zoned and nothing charged: this is what the zoning tool's
// preview calls while the player drags.
void MixCheck(Hash& hash, char* manager, int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t type, uint32_t flags)
{
    CellRegion region{x0, z0, x1, z1, {}};
    const uint8_t fill = 1;
    const uint32_t width = static_cast<uint32_t>(x1 - x0 + 1), depth = static_cast<uint32_t>(z1 - z0 + 1);
    reinterpret_cast<CellsFn>(addr::SC4CellRegion_ConstructCells)(region.cells, width, depth, &fill);
    Tree lots{nullptr, 0};
    const bool withSet = (flags & 8) != 0;
    if (withSet) {
        lots.head = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_stl_allocate)(0x18));
        At<uint8_t>(lots.head, 0x0) = 0;
        At<char*>(lots.head, 0x4) = nullptr;
        At<char*>(lots.head, 0x8) = lots.head;
        At<char*>(lots.head, 0xC) = lots.head;
    }
    int64_t cost = 0;
    int32_t error = 0;
    const uint8_t possible =
        Virtual<uint8_t>(manager, slot::kPlaceZone, static_cast<void*>(&region), type, 0u, (flags >> 2) & 1u,
                         flags & 1u, (flags >> 4) & 1u, (flags >> 1) & 1u, &cost, &error,
                         withSet ? static_cast<void*>(&lots) : nullptr);
    hash.Mix(possible);
    hash.Mix(static_cast<uint32_t>(cost));
    hash.Mix(static_cast<uint32_t>(static_cast<uint64_t>(cost) >> 32));
    hash.Mix(static_cast<uint32_t>(error));
    uint32_t** const rows = reinterpret_cast<uint32_t**>(static_cast<std::uintptr_t>(region.cells[4]));
    for (uint32_t x = 0; x < width; ++x)
        for (uint32_t z = 0; z < depth; ++z)
            hash.Mix((rows[x][z >> 5] >> (z & 31)) & 1u);
    if (withSet) {
        hash.Mix(lots.count);
        FreeSetNodes(At<char*>(lots.head, 0x4));
        reinterpret_cast<FreeFn>(addr::SC4_Free)(lots.head);
    }
    region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells)(region.cells);
}

// OPENSC4_TEST_ZONEMGR: a checksum of the manager's slots that no
// unattended run would call.
//   1: the pure ones: the zone type on a lattice of cells (slot 12), the
//      counts (6, 7, 8, 10), whether slot 5 gives the grid, IsRCIZone,
//      textures, drag colours, both alphas and the sizes for every type,
//      and PlaceZone without placing (above) on 8 x 8 rectangles every 29
//      cells: once as a type by turns and once dezoning, with the flags
//      (custom zoning, whole lots, no funds test, a lot set) by turns, plus
//      a rectangle too small and one too large.
//   2: also GetDevelopmentFailureCount (slot 9) for every type and reason
//      and one past each: this adds 304 zero entries to the saved map, as
//      the game's own callers would.
//   3: also SetZoneDisplayAlpha (slot 18) to another value and back, which
//      has the lot developer redraw every empty lot's texture twice.
uint32_t Queries(char* manager, int32_t width, int32_t depth, int level)
{
    Hash hash;
    for (int32_t z = 0; z < depth; z += 3)
        for (int32_t x = 0; x < width; x += 5)
            hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(manager, slot::kGetZoneType, x, z)));
    for (uint32_t type = 0; type < kTypes; ++type) {
        hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(manager, slot::kGetZoneCount, type)));
        hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(manager, slot::kGetUndeveloped, type)));
        hash.Mix(Virtual<uint32_t>(manager, slot::kGetDragColor, type));
        hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(manager, slot::kGetMinSize, type)));
        hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(manager, slot::kGetMaxSize, type)));
    }
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(manager, slot::kGetUndevelopedTotal)));
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(manager, slot::kGetAbandoned)));
    hash.Mix(Virtual<void*>(manager, slot::kGetZoneGrid) == At<void*>(manager, field::kGrid) ? 1u : 0u);
    for (uint32_t type = 0; type < kTypes + 2; ++type) {
        hash.Mix(Virtual<uint8_t>(manager, slot::kIsRCIZone, type));
        uint32_t key[3] = {};
        hash.Mix(Virtual<uint8_t>(manager, slot::kGetTexture, type, static_cast<uint32_t*>(key)));
        for (const uint32_t part : key)
            hash.Mix(part);
    }
    hash.Mix(Virtual<uint8_t>(manager, slot::kGetAlpha));
    hash.Mix(Virtual<uint8_t>(manager, slot::kGetDefaultAlpha));

    static const uint32_t kTurn[8] = {1, 5, 9, 7, 14, 12, 15, 3};
    uint32_t index = 0;
    for (int32_t z = 2; z + 8 <= depth; z += 29)
        for (int32_t x = 2; x + 8 <= width; x += 29, ++index) {
            MixCheck(hash, manager, x, z, x + 7, z + 7, kTurn[index % 8], index & 15);
            MixCheck(hash, manager, x, z, x + 7, z + 7, 0, (index + 5) & 15);
        }
    if (width >= 48 && depth >= 48) {
        MixCheck(hash, manager, 4, 4, 4, 4, 7, 0);    // too small for agriculture
        MixCheck(hash, manager, 4, 4, 43, 43, 1, 0);  // too large
        MixCheck(hash, manager, 4, 4, 43, 43, 1, 16); // the same without the size test
    }

    if (level >= 2) {
        for (int32_t reason = 0; reason <= zonemgr_sim::kReasons; ++reason)
            for (int32_t type = 0; type <= static_cast<int32_t>(kTypes); ++type)
                hash.Mix(Virtual<uint32_t>(manager, slot::kGetFailureCount, type, reason));
        hash.Mix(At<uint32_t>(manager, field::kFailures + 4));
    }
    if (level >= 3) {
        const uint32_t alpha = At<uint8_t>(manager, field::kAlpha);
        Virtual<void>(manager, slot::kSetAlpha, alpha ^ 0x55u);
        hash.Mix(Virtual<uint8_t>(manager, slot::kGetAlpha));
        Virtual<void>(manager, slot::kSetAlpha, alpha);
        hash.Mix(Virtual<uint8_t>(manager, slot::kGetAlpha));
    }
    return hash.h;
}

// The zone manager (build 641; src/core/zonemgr_sim.h), found through the
// city's slot 36 (the getter of +0x134; the same object as the global
// 0x00B43D14): its reference count and which of its services and grid it
// holds; the cells zoned per type (all sixteen: they sum to the city's
// cells) and a checksum of the two other count arrays with the four
// counters after them; a checksum of the whole zone grid, read as slot 12
// does, with the number of zoned cells in it; the display alpha and its
// default; the two failure maps (count and a checksum of their entries in
// order); and a checksum of the tuning (sizes, slopes, costs, textures,
// colours). No addresses.
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | zonemgr none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x90);
    if (!manager || !At<uint8_t>(manager, field::kInitialized) || !At<void*>(manager, field::kGrid) ||
        !At<void*>(manager, field::kCity)) {
        line += " | zonemgr none";
        return;
    }
    const int32_t width = Virtual<int32_t>(At<void*>(manager, field::kCity), 0x174);
    const int32_t depth = Virtual<int32_t>(At<void*>(manager, field::kCity), 0x178);
    Hash grid;
    uint32_t zonedCells = 0;
    void* const zoneGrid = At<void*>(manager, field::kGrid);
    for (int32_t x = 0; x < width; ++x)
        for (int32_t z = 0; z < depth; ++z) {
            const int8_t type = GridCell(zoneGrid, x, z);
            grid.h = (grid.h ^ static_cast<uint8_t>(type)) * 16777619u;
            zonedCells += type != 0;
        }
    Hash counts;
    counts.MixWords(manager, field::kUndeveloped, kTypes);
    counts.MixWords(manager, field::kCountsCC, kTypes);
    counts.MixWords(manager, field::kUndevelopedTotal, 4);
    Hash failures, other, tuning;
    MixMap(failures, reinterpret_cast<const Tree*>(manager + field::kFailures));
    MixMap(other, reinterpret_cast<const Tree*>(manager + field::kMap370));
    tuning.MixWords(manager, field::kMinSize, (field::kAlpha - field::kMinSize) / 4);
    uint32_t services = 0;
    const std::size_t held[] = {field::kMessages, field::kCity,   field::kOccupants, field::kTerrain,
                                field::kPollution, field::kBudget, field::kGrid};
    for (std::size_t i = 0; i < sizeof held / sizeof held[0]; ++i)
        services |= At<void*>(manager, held[i]) ? 1u << i : 0u;
    std::string zoned;
    for (uint32_t type = 0; type < kTypes; ++type) {
        char one[16];
        std::snprintf(one, sizeof one, "%s%d", type ? "/" : "", At<int32_t>(manager, field::kZoned + type * 4));
        zoned += one;
    }
    char text[400];
    std::snprintf(text, sizeof text, " | zonemgr %u,%X,%u,%s,%08X,%08X/%u,%u/%u,%u/%08X,%u/%08X,%08X",
                  At<uint32_t>(manager, field::kRefCount), services, At<uint8_t>(manager, field::kLoaded),
                  zoned.c_str(), counts.h, grid.h, zonedCells, At<uint8_t>(manager, field::kAlpha),
                  At<uint8_t>(manager, field::kDefaultAlpha), At<uint32_t>(manager, field::kFailures + 4), failures.h,
                  At<uint32_t>(manager, field::kMap370 + 4), other.h, tuning.h);
    line += text;
    const int level = EnvInt("OPENSC4_TEST_ZONEMGR", 0);
    if (level >= 1) {
        char hash[16];
        std::snprintf(hash, sizeof hash, ",q%08X", Queries(manager, width, depth, level));
        line += hash;
    }
}

const FingerprintPart kFingerprint{"zonemgr", 103, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
