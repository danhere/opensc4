// Our cSC4ZoneManager::PlaceZone (build 641, slot 11, 0x004DD780; the
// class map is in zonemgr_sim.h): every zoning and dezoning of the game
// goes through it, and the zoning tool's preview calls it without placing.
//
//   PlaceZone(region, type, place, skipFunds, customZoning, skipSize,
//             wholeLots, cost, error, lotSet), ret 0x28
//
//   region       SC4CellRegion: the rectangle (x1, z1, x2, z2), then at
//                +0x10 the cells (vtable, width +0x14, depth +0x18, rows of
//                bits +0x20). Cells that cannot take the zone are cleared
//                in the caller's region.
//   type         0 to 15 (0 dezones; anything above 15 is taken as 0)
//   place        false: only the tests, the cost and the error
//   skipFunds    the cost is not compared with the funds and nothing is
//                charged (the cost given back is then 0)
//   customZoning the caller is the player's tool: cells steeper than the
//                type's slope and lots whose building cannot be removed are
//                left out, lots are demolished as by the player, and the
//                zone developer and the message get the flag
//   skipSize     the rectangle's sides are not held against the type's
//                least and greatest size
//   wholeLots    a lot reaching out of the rectangle keeps its cell
//   cost         int64 out (may be null)
//   error        int32 out (may be null): 0 none, 1 not enough funds, 2
//                landfill on water, 3 too small, 4 too large, 5 landfill on
//                something that cannot be zoned
//   lotSet       a std::set of lots (may be null): every lot that would be
//                demolished is added
//
// The order: the funds (the budget simulator's slot 6); the size tests;
// then cell by cell (x outer, z inner) the tests below, each cell either
// skipped, cleared from the region, recorded (with its old type, and the
// cost of the change added) or ending the walk with an error; the lots on
// the recorded cells priced by the demolition utility (slot 7, not
// demolishing); the lots added to lotSet; for landfill the landfill
// developer's zoning cost; the funds test; then, when there was no error
// and `place` is set: the cost withdrawn (slot 8 of the budget simulator; a
// refusal is error 1), a copy of the region made with only the recorded
// cells, the zone grid and the counts per type written, the lots
// demolished (slot 7, demolishing, flags 0x2000), the zone developer's
// DoParcellization (slot 6) on the copy and the message 0x07699455 sent
// (data 1 and the unknown = the manager, data 2 = the copy, which the
// message frees, data 3 = the type, data 4 = customZoning).
//
// Every virtual call has its argument count from the callee's own `ret N`:
// the budget simulator 0x00AB0908, the building development simulator
// 0x00AB0AF8 and its lot developer 0x00AB1B98, the city 0x00A87620, the
// traffic network 0x00A8FED8, the network occupants (7 vtables; slot 22
// jumps to slot 8 with its one argument), the occupants (57 vtables, slots
// 14 and 32 shared code), the terrain 0x00AB3D18, the lot manager
// 0x00AB37C0, the lot 0x00AB19B0, the lot configuration 0x00A8CEB8, the
// building occupants (slot 3), the occupant manager 0x00A8F7F0, the
// demolition utility 0x00A88004, the landfill developer 0x00AB157C, the
// zone developer 0x00AB3830, the message 0x00A92D40 (slot 33 jumps to a
// `ret 4` helper: one argument) and the message servers.
#include "core/zonemgr_sim.h"

#include "core/game_dezone.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::zonemgr_sim {
namespace {

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using PushBackFn = void(__thiscall*)(void* list, void* const* object);
using InsertFn = void*(__thiscall*)(void* set, void* result, void* const* value);
using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr uint32_t kZoneAirport = 0xB;
constexpr uint32_t kZoneSeaport = 0xC;
constexpr uint32_t kZoneLandfill = 0xE;
constexpr uint32_t kZonePlopped = 0xF;
constexpr uint32_t kNetworkMask = 0x1F4F;
constexpr uint32_t kOccupantType6A11BD2B = 0x6A11BD2B;

enum Error : int32_t {
    kErrorNone = 0,
    kErrorFunds = 1,
    kErrorWater = 2,
    kErrorTooSmall = 3,
    kErrorTooLarge = 4,
    kErrorBlocked = 5,
};

// A recorded cell (6 bytes): its old type, x and z. The game leaves the
// second byte as the stack had it (never read): zero here.
struct Cell {
    uint8_t oldType;
    uint8_t unused;
    int16_t x;
    int16_t z;
};
static_assert(sizeof(Cell) == 6);

struct CellVector {
    Cell* begin;
    Cell* end;
    Cell* capacity;
};

// push_back as the game has it inline, growing through 0x004DD050 (one
// value at the end): twice the size (1 for an empty vector) from the STL
// allocator, the old block freed after the copy.
void PushCell(CellVector& cells, const Cell& cell)
{
    if (cells.end != cells.capacity) {
        *cells.end = cell;
        ++cells.end;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(cells.end - cells.begin);
    const uint32_t capacity = size + (size < 1 ? 1 : size);
    Cell* const grown = capacity ? static_cast<Cell*>(Allocate(capacity * sizeof(Cell))) : nullptr;
    Cell* at = grown;
    for (const Cell* from = cells.begin; from != cells.end; ++from, ++at)
        *at = *from;
    *at = cell;
    if (cells.begin)
        Free(cells.begin);
    cells.begin = grown;
    cells.end = at + 1;
    cells.capacity = grown + capacity;
}

// The region's bit of a cell given relative to its corner.
uint32_t& RegionWord(int32_t* region, uint32_t dx, uint32_t dz)
{
    uint32_t** const rows = reinterpret_cast<uint32_t**>(static_cast<std::uintptr_t>(region[8]));
    return rows[dx][dz >> 5];
}

void ClearRegionBit(int32_t* region, int32_t x, int32_t z)
{
    const uint32_t dz = static_cast<uint32_t>(z - region[1]);
    RegionWord(region, static_cast<uint32_t>(x - region[0]), dz) &= ~(1u << (dz & 31));
}

enum class Outcome { Clear, Record, Fail };

struct Walk {
    char* self;
    int32_t* region;
    uint32_t type;
    bool custom;
    bool wholeLots;
    void* traffic;
    void* lots;
    char** lotList; // the list's head pointer
    int64_t cost;
    int32_t error;
};

// The lot added to the list when it is not in it: the game holds it in a
// reference for the search (0x004DC710) and in another for the push
// (0x007E98C0, which references it for the node).
void AddLot(Walk& walk, void* lot)
{
    char* const head = *walk.lotList;
    AddRefOf(lot);
    char* found = At<char*>(head, 0x0);
    while (found != head && At<void*>(found, 0x8) != lot)
        found = At<char*>(found, 0x0);
    ReleaseOf(lot);
    if (found != head)
        return;
    AddRefOf(lot);
    void* held = lot;
    reinterpret_cast<PushBackFn>(addr::SC4RefList_PushBack2)(walk.lotList, &held);
    ReleaseOf(lot);
}

// One cell of the region that is set (0x004DD924 to 0x004DDD76).
Outcome TestCell(Walk& walk, int32_t x, int32_t z)
{
    char* const self = walk.self;
    const uint32_t type = walk.type;
    // A network occupant (the traffic network's slot 8, mask 0x1F4F) whose
    // occupant has flag 1 (slot 32) keeps its cell unless the cell is
    // dezoned or made landfill; dezoning under rail (IsOfType 3) adds the
    // occupant's demolition cost.
    void* const network = Virtual<void*>(walk.traffic, 0x20, x, z, kNetworkMask, 1u);
    if (network) {
        void* const occupant = Virtual<void*>(network, 0xC0);
        if (Virtual<uint8_t>(occupant, 0x80, 1u) && type != kZoneLandfill) {
            if (type != 0)
                return Outcome::Clear;
            if (Virtual<uint8_t>(network, 0x58, 3u))
                walk.cost += Virtual<int64_t>(network, 0xFC);
        }
    }
    // The terrain's slot 34 (water): only dezoning and seaport ground.
    if (Virtual<uint8_t>(At<void*>(self, field::kTerrain), 0x88, x, z)) {
        if (type == kZoneLandfill) {
            walk.error = kErrorWater;
            return Outcome::Fail;
        }
        if (type != kZoneSeaport && type != 0)
            return Outcome::Clear;
    }
    // The cell's slope (the lot developer's slot 29 on the one-cell
    // rectangle, in ST0) against the type's greatest; an unordered compare
    // keeps the cell.
    if (walk.custom && type != 0 && type != kZonePlopped) {
        void* const developer = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
        int32_t rect[4] = {x, z, x, z};
        const double slope = Virtual<double>(developer, 0x74, static_cast<int32_t*>(rect), 1u);
        if (slope > static_cast<double>(At<float>(self, field::kMaxSlope + type * 4)))
            return Outcome::Clear;
    }
    void* const lot = Virtual<void*>(walk.lots, 0x18, x, z, 0u);
    if (!lot) {
        // No lot: an occupant of type 0x6A11BD2B on the cell must be
        // removable (its slot 14 with 2), whatever the type; landfill
        // cannot go on a network cell.
        void* occupant = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x84, &occupant, x, z, kOccupantType6A11BD2B)) {
            const uint8_t removable = Virtual<uint8_t>(occupant, 0x38, 2u);
            ReleaseOf(occupant);
            if (!removable) {
                walk.error = kErrorBlocked;
                return Outcome::Fail;
            }
        } else if (type == kZoneLandfill && network) {
            walk.error = kErrorBlocked;
            return Outcome::Fail;
        }
        return Outcome::Record;
    }
    const int32_t* const region = walk.region;
    if (walk.wholeLots) {
        int32_t bounds[4] = {};
        Virtual<void>(lot, 0x38, static_cast<int32_t*>(bounds));
        if (bounds[0] < region[0] || bounds[2] > region[2] || bounds[1] < region[1] || bounds[3] > region[3]) {
            if (type == kZoneLandfill) {
                walk.error = kErrorBlocked;
                return Outcome::Fail;
            }
            return Outcome::Clear;
        }
    }
    // Plopped, airport and seaport lots keep their cells.
    const uint32_t lotZone = Virtual<uint32_t>(lot, 0x7C);
    if (lotZone == kZonePlopped || lotZone == kZoneAirport || lotZone == kZoneSeaport) {
        if (type != kZoneLandfill)
            return Outcome::Clear;
        walk.error = kErrorBlocked;
        return Outcome::Fail;
    }
    // A lot whose configuration (the lot's slot 9) fits the new zone (the
    // configuration's slot 15) stays; any other is demolished, and so is
    // every lot when dezoning.
    void* const configuration = Virtual<void*>(lot, 0x24);
    if (!configuration)
        return Outcome::Record;
    if (Virtual<uint8_t>(configuration, 0x3C, type) && type != 0)
        return Outcome::Record;
    void* const building = Virtual<void*>(lot, 0xC0);
    if (walk.custom && building) {
        void* const occupant = Virtual<void*>(building, 0xC);
        if (!Virtual<uint8_t>(occupant, 0x38, 2u))
            return Outcome::Clear;
    }
    AddLot(walk, lot);
    return Outcome::Record;
}

// Slot 11 (0x004DD780). The byte arguments come as the caller's pushed
// dwords: the game tests their low bytes and hands customZoning on to the
// zone developer as pushed.
uint8_t __fastcall PlaceZone(char* self, void*, int32_t* region, uint32_t type, uint32_t place, uint32_t skipFunds,
                             uint32_t customZoning, uint32_t skipSize, uint32_t wholeLots, int64_t* costOut,
                             int32_t* errorOut, void* lotSet)
{
    static bool logged = false;
    FirstCall("PlaceZone", logged);
    // An improvement beyond the original: nothing unless the de-zone tool
    // calls with Shift held (docs/dezone.md).
    game_dezone::KeepRoads(region, type, customZoning, __builtin_return_address(0));
    uint8_t result = 1;
    const int64_t funds = Virtual<int64_t>(At<void*>(self, field::kBudget), 0x18);
    CellVector cells{nullptr, nullptr, nullptr};
    const int32_t width = region[2] - region[0] + 1;
    const int32_t depth = region[3] - region[1] + 1;
    char* lotList = static_cast<char*>(Allocate(0xC));
    At<char*>(lotList, 0x0) = lotList;
    At<char*>(lotList, 0x4) = lotList;
    if (type >= kTypes)
        type = 0;
    Walk walk{};
    walk.self = self;
    walk.region = region;
    walk.type = type;
    walk.custom = static_cast<uint8_t>(customZoning) != 0;
    walk.wholeLots = static_cast<uint8_t>(wholeLots) != 0;
    walk.lotList = &lotList;
    walk.cost = 0;
    walk.error = kErrorNone;

    bool walkCells = true;
    if (!static_cast<uint8_t>(skipSize)) {
        const int32_t least = At<int32_t>(self, field::kMinSize + type * 4);
        if (width < least || depth < least) {
            walk.error = kErrorTooSmall;
            walkCells = false;
        } else {
            const int32_t most = At<int32_t>(self, field::kMaxSize + type * 4);
            if (width > most || depth > most) {
                walk.error = kErrorTooLarge;
                walkCells = false;
            }
        }
        if (!walkCells)
            result = 0;
    }
    if (walkCells) {
        // The lot developer is asked for and not used here.
        Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
        walk.lots = Global<void*>(addr::SC4_LotManager);
        walk.traffic = Virtual<void*>(At<void*>(self, field::kCity), 0xA0);
        char** const columns = Virtual<char**>(At<void*>(self, field::kGrid), 0x60);
        bool failed = false;
        for (int32_t x = region[0]; x <= region[2] && !failed; ++x) {
            for (int32_t z = region[1]; z <= region[3]; ++z) {
                const uint32_t dx = static_cast<uint32_t>(x - region[0]);
                const uint32_t dz = static_cast<uint32_t>(z - region[1]);
                if (dx >= static_cast<uint32_t>(region[5]) || dz >= static_cast<uint32_t>(region[6]))
                    continue;
                if (!(RegionWord(region, dx, dz) & (1u << (dz & 31))))
                    continue;
                uint32_t oldType = static_cast<uint32_t>(
                    static_cast<int32_t>(At<int8_t*>(*columns, static_cast<std::size_t>(x) * 12)[z]));
                if (oldType >= kTypes)
                    oldType = 0;
                const Outcome outcome = TestCell(walk, x, z);
                if (outcome == Outcome::Fail) {
                    result = 0;
                    failed = true;
                    break;
                }
                if (outcome == Outcome::Clear) {
                    ClearRegionBit(region, x, z);
                    continue;
                }
                PushCell(cells,
                         Cell{static_cast<uint8_t>(oldType), 0, static_cast<int16_t>(x), static_cast<int16_t>(z)});
                if (type != oldType)
                    walk.cost += At<int64_t>(self, field::kDezoneCost + oldType * 8) +
                                 At<int64_t>(self, field::kZoneCost + type * 8);
            }
        }
    }

    // The lots in the way: what demolishing them costs.
    const uint32_t custom = walk.custom ? 1u : 0u;
    if (At<char*>(lotList, 0x0) != lotList) {
        int64_t demolition = 0;
        Virtual<void>(Global<void*>(addr::SC4_Service43D00), 0x1C, 0u, &lotList, custom, 1u, 0u, 0u, &demolition, 0u);
        walk.cost += demolition;
    }
    if (lotSet) {
        for (char* at = At<char*>(lotList, 0x0); at != lotList; at = At<char*>(at, 0x0)) {
            struct {
                void* node;
                uint32_t inserted;
            } inserted{};
            reinterpret_cast<InsertFn>(addr::SC4LotPtrSet_InsertUnique)(lotSet, &inserted, &At<void*>(at, 0x8));
        }
    }
    if (type == kZoneLandfill) {
        void* const landfill = Virtual<void*>(At<void*>(self, field::kCity), 0x138);
        walk.cost += Virtual<int64_t>(landfill, 0x24, static_cast<void*>(region));
    }

    bool affordable = true;
    if (static_cast<uint8_t>(skipFunds))
        walk.cost = 0;
    else if (walk.cost > funds)
        affordable = false;
    if (affordable && result && static_cast<uint8_t>(place)) {
        if (!Virtual<uint8_t>(At<void*>(self, field::kBudget), 0x20, walk.cost)) {
            affordable = false;
        } else if (cells.begin != cells.end) {
            // The copy of the region with only the cells that change hands
            // (the game does not test the allocations for the calls after).
            int32_t* const copy = static_cast<int32_t*>(OperatorNew(0x24));
            if (copy) {
                std::memcpy(copy, region, 4 * sizeof(int32_t));
                const uint8_t fill = 0;
                reinterpret_cast<CellsFn>(addr::SC4CellRegion_ConstructCells)(
                    copy + 4, static_cast<uint32_t>(region[2] - region[0] + 1),
                    static_cast<uint32_t>(region[3] - region[1] + 1), &fill);
            }
            char** const columns = Virtual<char**>(At<void*>(self, field::kGrid), 0x60);
            for (const Cell* cell = cells.begin; cell != cells.end; ++cell) {
                const int32_t x = cell->x;
                const int32_t z = cell->z;
                const uint32_t dz = static_cast<uint32_t>(z - copy[1]);
                RegionWord(copy, static_cast<uint32_t>(x - copy[0]), dz) |= 1u << (dz & 31);
                At<int8_t*>(*columns, static_cast<std::size_t>(x) * 12)[z] = static_cast<int8_t>(type);
                --At<int32_t>(self, field::kZoned + cell->oldType * 4);
                ++At<int32_t>(self, field::kZoned + type * 4);
            }
            Virtual<void>(Global<void*>(addr::SC4_Service43D00), 0x1C, 1u, &lotList, custom, 0x2000u, 0u, 0u, 0u, 0u);
            Virtual<void>(Global<void*>(addr::SC4_Service43DB0), 0x18, static_cast<void*>(copy), type, customZoning);
            void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
            if (message) {
                reinterpret_cast<SelfFn>(addr::cGZMessage_cGZMessage)(message);
                At<std::uintptr_t>(message, 0x0) = addr::cSC4ZoneManager_ZonedMessageVtable;
            }
            AddRefOf(message);
            Virtual<void>(message, 0x14, kMessageZoned);
            Virtual<void>(message, 0x30, static_cast<void*>(self));
            Virtual<void>(message, 0x84, static_cast<void*>(self));
            Virtual<void>(message, 0x44, static_cast<void*>(copy));
            Virtual<void>(message, 0x54, type);
            Virtual<void>(message, 0x68, custom);
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x10, message, 0u);
            ReleaseOf(message);
        }
    }
    if (!affordable) {
        result = 0;
        walk.error = kErrorFunds;
    }

    if (costOut)
        *costOut = walk.cost;
    if (errorOut)
        *errorOut = walk.error;
    FreeRefList(lotList);
    if (cells.begin)
        Free(cells.begin);
    return result;
}

const Slot kPlaceSlots[] = {
    {addr::cSC4ZoneManager_vftable, 11, addr::cSC4ZoneManager_PlaceZone, reinterpret_cast<void*>(&PlaceZone)},
};

bool InstallPlace(registry::Mode) { return SwapSlots(kPlaceSlots, 1, "place"); }

const bool g_registered = registry::Register({
    "simulators.zonemgr.place",
    "simulators/zonemgr",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4ZoneManager_PlaceZone,
    "Our zone manager PlaceZone: the tests, cost and placing of every zoning and dezoning "
    "(src/core/zonemgr_place.cpp)",
    false,
    InstallPlace,
});

} // namespace
} // namespace opensc4::zonemgr_sim
