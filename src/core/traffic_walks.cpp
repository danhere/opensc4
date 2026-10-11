// Our cSC4TrafficSimulator, the walks (docs/traffic.md): the lot walk at
// +0x908 (0x007107A0, 0x007107F0), the route iterator over a trip's
// packed route bytes (0x006D64B0, 0x006D6510), the trip record reader
// (0x0070E160) and GetFerryRoutesInUse (slot 31, 0x0071C250).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/traffic_cells.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::traffic_sim {
namespace {

// --- The lot walk -----------------------------------------------------------------

// Two lists walked in turn: the lot manager's (behind an iterator object at
// +0, whose +0 is the current node and +4 the list) and the month-end
// service's (list at +4, current node at +8); +0xC is the phase.
struct LotWalk {
    char** lots;
    char* services;
    char* next;
    uint32_t phase;
};

// 0x007107A0
void __fastcall RestartLotWalk(LotWalk* walk, void*)
{
    if (!walk->lots)
        walk->lots = Virtual<char**>(Global<void*>(addr::SC4_LotManager), 0x3C);
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x40, &walk->services);
    char** const lots = walk->lots;
    if (char* const list = lots[1])
        lots[0] = At<char*>(At<char*>(list, 0), 0);
    walk->next = At<char*>(walk->services, 0);
    walk->phase = 0;
}

// 0x007107F0: the next object of the walk, AddRef'd, with its cell; false
// at the end.
uint8_t __fastcall NextOfLotWalk(LotWalk* walk, void*, void** out, int16_t* x, int16_t* z)
{
    int32_t a = 0, b = 0;
    if (walk->phase == 0) {
        char** const lots = walk->lots;
        char* const list = lots[1];
        if (list && lots[0] != At<char*>(list, 0)) {
            char* const node = lots[0];
            lots[0] = At<char*>(node, 0);
            void* const lot = At<void*>(node, 8);
            Virtual<void>(lot, 0x54, &a, &b);
            *x = static_cast<int16_t>(a);
            *z = static_cast<int16_t>(b);
            *out = Virtual<void*>(lot, 0x10);
            Virtual<uint32_t>(lot, 0x4); // AddRef
            return 1;
        }
        walk->phase = 1;
    }
    if (walk->next == walk->services)
        return 0;
    void* const object = At<void*>(walk->next, 8);
    Virtual<void>(object, 0x18, &b, &a);
    *x = static_cast<int16_t>(b);
    *z = static_cast<int16_t>(a);
    *out = Virtual<void*>(object, 0x10);
    Virtual<uint32_t>(object, 0x4); // AddRef
    walk->next = At<char*>(walk->next, 0);
    return 1;
}

// --- The route iterator -----------------------------------------------------------

// One cell of a route: where, its travel type and the next one's, and the
// direction left by (0xFF none).
struct RouteStep {
    int16_t x;
    int16_t z;
    uint8_t type;
    uint8_t nextType;
    uint8_t stepped;
    uint8_t side;
    uint8_t direction;
    uint8_t jump;
};
static_assert(sizeof(RouteStep) == 10);

struct RouteIterator {
    uint8_t** route; // vector<uint8>: begin, end
    uint8_t* at;
    uint8_t run;     // steps left in the run
    uint8_t slot;    // which 2-bit step of the byte
    uint8_t bits;    // the run's current byte, shifted
    uint8_t done;
    RouteStep current;
};
static_assert(offsetof(RouteIterator, current) == 0xC); // the game's is 0x16 bytes

// 0x006D64B0
void __fastcall InitRouteIterator(RouteIterator* it, void*, uint8_t** route)
{
    it->route = route;
    it->at = route[0];
    it->run = 0;
    if (route[0] == route[1]) {
        it->done = 1;
        return;
    }
    it->current.type = *it->at++ & 0x3F;
    int32_t x = 0, z = 0;
    it->at = const_cast<uint8_t*>(ReadRouteCell(it->at, x, z));
    it->current.x = static_cast<int16_t>(x);
    it->current.z = static_cast<int16_t>(z);
    it->current.stepped = 0;
    it->current.side = 0xFF;
    it->done = 0;
}

// 0x006D6510: the current cell out and the next one decoded; false at the
// end. A jump's direction is set only for a straight step: the game takes
// any z step of -1 or more for +1.
uint8_t __fastcall NextOfRoute(RouteIterator* it, void*, RouteStep* out)
{
    if (it->done)
        return 0;
    *out = it->current;
    if (it->run == 0) {
        if (it->at == it->route[1]) {
            it->done = 1;
            out->nextType = out->type;
            out->direction = 0xFF;
            return 1;
        }
        const uint8_t head = *it->at++;
        it->current.type = head & 0x3F;
        out->nextType = head & 0x3F;
        if (head & 0x40) {
            const uint8_t count = *it->at++;
            it->run = count;
            it->slot = 3;
            it->current.jump = 0;
            if (count == 0) {
                it->done = 1;
                out->nextType = out->type;
                out->direction = 0xFF;
                return 1;
            }
        } else {
            int32_t x = 0, z = 0;
            it->at = const_cast<uint8_t*>(ReadRouteCell(it->at, x, z));
            it->current.x = static_cast<int16_t>(x);
            it->current.z = static_cast<int16_t>(z);
            it->current.jump = head >> 7;
            it->current.stepped = 0;
            int32_t dx = it->current.x - out->x;
            int32_t dz = it->current.z - out->z;
            bool clampZ = true;
            if (dx != 0 && dz != 0) {
                if ((dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz))
                    dz = 0;
                else {
                    dx = 0;
                    clampZ = false;
                }
            }
            bool decideX = false;
            if (clampZ) {
                if (dx > 1) {
                    dx = 1;
                    decideX = true;
                } else if (dx < -1) {
                    dx = -1;
                    decideX = true;
                }
            }
            if (!decideX) {
                dz = dz < -1 ? -1 : 1;
                decideX = dx != 0;
                if (!decideX)
                    out->direction = dz < 0 ? 1 : 3;
            }
            if (decideX) {
                if (dz != 0) {
                    out->direction = 0xFF;
                    out->side = 0xFF;
                } else {
                    out->direction = dx < 0 ? 0 : 2;
                }
            }
            if (out->direction != 0xFF)
                it->current.side = static_cast<uint8_t>((out->direction - 2) & 3);
            return 1;
        }
    }
    it->slot = (it->slot + 1) & 3;
    if (it->slot == 0)
        it->bits = *it->at++;
    const uint8_t direction = it->bits & 3;
    out->direction = direction;
    it->current.side = static_cast<uint8_t>((direction - 2) & 3);
    it->bits >>= 2;
    --it->run;
    it->current.x = static_cast<int16_t>(it->current.x + Global<int32_t>(addr::TrafficPathfinder_SideDX + 4 * direction));
    it->current.z = static_cast<int16_t>(it->current.z + Global<int32_t>(addr::TrafficPathfinder_SideDZ + 4 * direction));
    it->current.stepped = 1;
    out->nextType = it->current.type;
    return 1;
}

// --- The trip record reader --------------------------------------------------------

// 0x0070E160: (stream) a trip record: version, two byte vectors (+0,
// +0xC), a uint32 (+0x18), four bytes, two uint16, a float (+0x24), and
// from version 2 a uint32 (+0x28, else 0). 1 when the stream has no error.
uint32_t __fastcall ReadTripRecord(void* self, void*, void* stream)
{
    using ReadVectorFn = void(__cdecl*)(void* stream, void* vector);
    const auto readVector = reinterpret_cast<ReadVectorFn>(addr::SC4Stream_ReadByteVector);
    uint32_t version = 0;
    Virtual<void>(stream, 0x24, &version);
    readVector(stream, self);
    readVector(stream, &At<char>(self, 0xC));
    Virtual<void>(stream, 0x24, &At<char>(self, 0x18));
    for (std::size_t at = 0x1C; at <= 0x1F; ++at)
        Virtual<void>(stream, 0x14, &At<char>(self, at));
    Virtual<void>(stream, 0x18, &At<char>(self, 0x20));
    Virtual<void>(stream, 0x18, &At<char>(self, 0x22));
    Virtual<void>(stream, 0x30, &At<char>(self, 0x24));
    if (version > 1)
        Virtual<void>(stream, 0x24, &At<char>(self, 0x28));
    else
        At<uint32_t>(self, 0x28) = 0;
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1; // slot 18: the stream's error
}

// --- Ferry routes in use ------------------------------------------------------------

// A ferry route in use, as the game's list holds it (node +8, 0x34 bytes).
struct FerryRouteInUse {
    void* from;
    void* to;
    uint32_t id;
    uint16_t trips[4];
    uint8_t outside;
    uint8_t padding;
    int16_t x;
    int16_t z;
    uint8_t padding2[2];
    char route[12];
    char back[12];
};
static_assert(sizeof(FerryRouteInUse) == 0x34);

// An object held by a ferry route entry, AddRef'd, the old one released.
void Hold(void*& slot, void* fresh)
{
    void* const old = slot;
    if (fresh == old)
        return;
    if (fresh)
        Virtual<uint32_t>(fresh, 0x4);
    slot = fresh;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// A new entry at the end of the list, copied from an empty one as the game
// does.
FerryRouteInUse* AppendRoute(char* list)
{
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using CopyFn = void(__thiscall*)(void* entry, const void* from);
    uint8_t blank[sizeof(FerryRouteInUse)] = {};
    char* const node = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x3C));
    if (node + 8)
        reinterpret_cast<CopyFn>(addr::SC4FerryRouteInUse_CopyConstruct)(node + 8, blank);
    char* const sentinel = At<char*>(list, 0);
    char* const last = At<char*>(sentinel, 4);
    At<char*>(node, 0) = sentinel;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(sentinel, 4) = node;
    return reinterpret_cast<FerryRouteInUse*>(At<char*>(At<char*>(list, 0), 4) + 8);
}

// 0x0071C250 (slot 31)
void __fastcall GetFerryRoutesInUse(void* self, void*, char* out)
{
    using ClearFn = void(__thiscall*)(void* list);
    using InsertFn = void(__thiscall*)(void* set, void* result, const uint16_t* key);
    using EraseFn = void(__thiscall*)(void* set, void* node);
    using AssignFn = void(__thiscall*)(void* vector, const void* from);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* p);
    const auto assign = reinterpret_cast<AssignFn>(addr::SC4ByteVector_Assign);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    reinterpret_cast<ClearFn>(addr::SC4FerryRouteList_Clear)(out);

    // The cells done: a set of uint16 (header node 0x14 bytes, size).
    struct {
        char* header;
        uint32_t size;
    } seen{};
    seen.header = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x14));
    seen.header[0] = 0;
    At<char*>(seen.header, 4) = nullptr;
    At<char*>(seen.header, 8) = seen.header;
    At<char*>(seen.header, 0xC) = seen.header;

    char* const connections = At<char*>(self, 0x104);
    for (char* item = At<char*>(connections, 0); item != connections; item = At<char*>(item, 0)) {
        const auto key = static_cast<uint16_t>(static_cast<uint8_t>(item[8]) << 8 | static_cast<uint8_t>(item[9]));
        uint32_t result[2];
        reinterpret_cast<InsertFn>(addr::SC4Uint16TreeSet_Insert)(&seen, result, &key);

        // Its ferry ends, to terminals not yet listed.
        char* const ends = At<char*>(item, 0x18);
        for (char* end = At<char*>(ends, 0); end != ends; end = At<char*>(end, 0)) {
            if (At<uint16_t>(end, 0x20) == 0 && At<uint16_t>(end, 0x24) == 0)
                continue;
            const uint16_t cell = At<uint16_t>(end, 8);
            char* bound = seen.header;
            for (char* at = At<char*>(seen.header, 4); at;) {
                if (At<uint16_t>(at, 0x10) < cell) {
                    at = At<char*>(at, 0xC);
                } else {
                    bound = at;
                    at = At<char*>(at, 8);
                }
            }
            if (bound != seen.header && !(cell < At<uint16_t>(bound, 0x10)))
                continue;
            char** const buckets = At<char**>(self, 0xE0);
            const auto count = static_cast<uint32_t>(At<char**>(self, 0xE4) - buckets);
            char* entry = buckets[cell % count];
            while (entry && At<uint16_t>(entry, 4) != cell)
                entry = At<char*>(entry, 0);
            if (!entry)
                continue;
            char* const terminal = At<char*>(entry, 8) + 8;
            FerryRouteInUse* const route = AppendRoute(out);
            route->id = At<uint32_t>(item, 0x10);
            route->trips[0] = At<uint16_t>(end, 0x20);
            route->trips[2] = At<uint16_t>(end, 0x24);
            assign(route->route, end + 0x14);
            route->outside = 0;
            route->x = -2;
            route->z = -2;
            Hold(route->from, Virtual<void*>(lots, 0x18, static_cast<uint32_t>(static_cast<uint8_t>(item[0x24])),
                                             static_cast<uint32_t>(static_cast<uint8_t>(item[0x25])), 0u));
            Hold(route->to, Virtual<void*>(lots, 0x18, static_cast<uint32_t>(static_cast<uint8_t>(terminal[0x1C])),
                                           static_cast<uint32_t>(static_cast<uint8_t>(terminal[0x1D])), 0u));
            char* const backs = At<char*>(terminal, 0x10);
            char* back = At<char*>(backs, 0);
            while (back != backs && At<uint16_t>(back, 8) != key)
                back = At<char*>(back, 0);
            if (back != backs) {
                route->trips[1] = At<uint16_t>(back, 0x20);
                route->trips[3] = At<uint16_t>(back, 0x24);
                assign(route->back, back + 0x14);
            } else {
                route->trips[1] = 0;
                route->trips[3] = 0;
            }
        }

        // Its routes out of the city.
        char* const outs = At<char*>(item, 0x1C);
        for (char* way = At<char*>(outs, 0); way != outs; way = At<char*>(way, 0)) {
            if (At<uint16_t>(way, 0x2C) == 0 && At<uint16_t>(way, 0x30) == 0)
                continue;
            FerryRouteInUse* const route = AppendRoute(out);
            route->trips[0] = route->trips[1] = At<uint16_t>(way, 0x2C);
            route->trips[2] = route->trips[3] = At<uint16_t>(way, 0x30);
            assign(route->route, way + 0x14);
            assign(route->back, way + 0x20);
            route->outside = 1;
            route->id = At<uint32_t>(item, 0x10);
            Hold(route->from, Virtual<void*>(lots, 0x18, static_cast<uint32_t>(static_cast<uint8_t>(item[0x24])),
                                             static_cast<uint32_t>(static_cast<uint8_t>(item[0x25])), 0u));
            if (void* const to = route->to) {
                route->to = nullptr;
                Virtual<uint32_t>(to, 0x8);
            }
            route->x = At<int16_t>(way, 8);
            route->z = At<int16_t>(way, 0xA);
        }
    }

    if (seen.size) {
        for (char* at = At<char*>(seen.header, 4); at;) {
            reinterpret_cast<EraseFn>(addr::SC4Uint16TreeSet_EraseSubtree)(&seen, At<char*>(at, 0xC));
            char* const left = At<char*>(at, 8);
            reinterpret_cast<FreeFn>(addr::SC4_Free)(at);
            at = left;
        }
        At<char*>(seen.header, 8) = seen.header;
        At<char*>(seen.header, 4) = nullptr;
        At<char*>(seen.header, 0xC) = seen.header;
        seen.size = 0;
    }
    if (seen.header)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(seen.header);
}

// Hooks to leave out, for bisecting: OPENSC4_TRAFFIC_SKIP lists function
// addresses (hex, e.g. 006D6510).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_TRAFFIC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[6] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4LotWalk_Restart, reinterpret_cast<void*>(&RestartLotWalk)},
        {addr::SC4LotWalk_Next, reinterpret_cast<void*>(&NextOfLotWalk)},
        {addr::SC4RouteIterator_Init, reinterpret_cast<void*>(&InitRouteIterator)},
        {addr::SC4RouteIterator_Next, reinterpret_cast<void*>(&NextOfRoute)},
        {addr::SC4TripRecord_Read, reinterpret_cast<void*>(&ReadTripRecord)},
        {addr::cSC4TrafficSimulator_GetFerryRoutesInUse, reinterpret_cast<void*>(&GetFerryRoutesInUse)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) == sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        if (Skipped(hooks[i].address))
            continue;
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "simulators.traffic.walks",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RouteIterator_Next,
    "Our traffic walks: the lot walk, the route iterator, the trip record reader, GetFerryRoutesInUse (docs/traffic.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::traffic_sim
