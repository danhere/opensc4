// Autotest: the power and water paths no test city runs (docs/power.md,
// docs/water.md, "What the test cities reach"): connections and deals with
// a neighbor, and the helpers those and a new city call, each answer into
// the fingerprint (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// Everything the switch asked, folded together; reported only in runs
// that use the switch, so other runs' fingerprints keep their shape.
uint32_t g_hash = 2166136261u;
bool g_ran = false;

void Add(uint32_t value)
{
    g_hash = (g_hash ^ BytesChecksum(&value, sizeof value)) * 16777619u;
}

// A list of the node allocator at `allocator`, empty.
void* NewList(std::uintptr_t allocator)
{
    void** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(allocator), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    return sentinel;
}

template <typename Fn>
void ForEachNode(void* head, Fn fn)
{
    for (void** node = static_cast<void**>(static_cast<void**>(head)[0]); node != head;
         node = static_cast<void**>(node[0]))
        fn(node);
}

// The neighbors simulator's connections of a type (slot 17), referenced,
// handed to `fn` one by one.
template <typename Fn>
void ForEachConnection(void* neighbors, uint32_t type, Fn fn)
{
    using ClearFn = void(__thiscall*)(void*);
    void* connections = NewList(addr::SC4List_NodeAllocator3);
    Virtual<void>(neighbors, 0x44, type, &connections);
    ForEachNode(connections, [&fn](void** node) { fn(node[2]); });
    reinterpret_cast<ClearFn>(addr::SC4List_ClearReleasing)(&connections);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, connections);
}

// The middle cell past the first edge with a city across (neighbors slot
// 12, established or not), west, north, east, south; false for none.
bool EdgeCellWithCity(cISC4City* city, void* neighbors, int32_t* x, int32_t* z, void** across)
{
    const auto cellsX = Virtual<int32_t>(city, 0x174);
    const auto cellsZ = Virtual<int32_t>(city, 0x178);
    const int32_t cells[4][2] = {{-1, cellsZ / 2}, {cellsX / 2, -1}, {cellsX, cellsZ / 2}, {cellsX / 2, cellsZ}};
    for (const auto& cell : cells) {
        if (void* const other = Virtual<void*>(neighbors, 0x30, cell[0], cell[1])) {
            *x = cell[0];
            *z = cell[1];
            *across = other;
            return true;
        }
    }
    return false;
}

// A power line (4) and a pipe (5) connection at the cell (neighbors slot 6,
// the game's own code for a line dragged to the edge), and a deal to sell
// power (0x4773FFE1) and one to sell water (0x4773FFE3) to the city across
// (0x006D0520, added now). Buying needs a connection across, which only an
// established neighbor has.
void ConnectAndDeal(void* neighbors, int32_t x, int32_t z, void* across)
{
    using MakeDealFn = uint8_t(__thiscall*)(void* self, uint32_t type, int32_t amount, void* city, void* list,
                                            uint8_t now);
    using DestroyFn = void(__thiscall*)(void* list);
    for (const uint32_t type : {4u, 5u})
        Add(Virtual<uint32_t>(neighbors, 0x18, type, x, z, uint8_t{0}));
    const struct {
        uint32_t type;
        int32_t amount;
    } deals[] = {{0x4773FFE1u, 100}, {0x4773FFE3u, 1000}};
    uint8_t made[2] = {};
    for (std::size_t i = 0; i < 2; ++i) {
        void* list = NewList(addr::SC4List_NodeAllocator);
        made[i] = reinterpret_cast<MakeDealFn>(addr::cSC4NeighborsSimulator_MakeDeal)(
            neighbors, deals[i].type, deals[i].amount, across, list, uint8_t{1});
        reinterpret_cast<DestroyFn>(addr::SC4List_Destroy)(&list);
        Add(made[i]);
    }
    log::Write("autotest: utilities: power and pipe connections at %d,%d; deals to sell power %u, water %u", x, z,
               made[0], made[1]);
}

// Power's helpers the network edits and a new city call: the cell of each
// power connection (OccupantCell, 0x006EB280; it asks the object's slot 6,
// which only a connection answers with a cell: a plant's is its property
// holder, without arguments), the occupant search for the city across
// (FindConnectedOccupant, 0x006EDB40, neither switching on nor marking),
// and a new city's bit arrays (CreateBitArrays, 0x006EAED0), made,
// checksummed and freed, the city's own put back.
void AskPower(void* power, void* neighbors, void* across)
{
    using CellFn = void(__stdcall*)(void* object, int32_t* x, int32_t* z);
    using FindFn = uint8_t(__thiscall*)(void* self, void* key, uint8_t switchOn, uint8_t mark);
    using SelfFn = void(__thiscall*)(void* self);
    using FreeFn = void(__cdecl*)(void* p);
    const auto cell = reinterpret_cast<CellFn>(addr::cSC4PowerSimulator_OccupantCell);
    const auto ask = [&cell](void* object) {
        int32_t x = -7, z = -7;
        cell(object, &x, &z);
        Add(static_cast<uint32_t>(x));
        Add(static_cast<uint32_t>(z));
    };
    if (neighbors)
        ForEachConnection(neighbors, 4, ask);
    if (across)
        Add(reinterpret_cast<FindFn>(addr::cSC4PowerSimulator_FindConnectedOccupant)(power, across, 0, 0));

    constexpr std::size_t kArrays[] = {0x2C4, 0x2C8, 0x2D4, 0x2D0};
    void* kept[4];
    for (std::size_t i = 0; i < 4; ++i)
        kept[i] = At<void*>(power, kArrays[i]);
    const int32_t keptWords = At<int32_t>(power, 0x2CC);
    reinterpret_cast<SelfFn>(addr::cSC4PowerSimulator_CreateBitArrays)(power);
    const int32_t words = At<int32_t>(power, 0x2CC);
    Add(static_cast<uint32_t>(words));
    for (std::size_t i = 0; i < 4; ++i) {
        void* const made = At<void*>(power, kArrays[i]);
        Add(made != nullptr);
        if (made && words > 0)
            Add(BytesChecksum(made, static_cast<std::size_t>(words) * 4));
        if (made)
            reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(made);
        At<void*>(power, kArrays[i]) = kept[i];
    }
    At<int32_t>(power, 0x2CC) = keptWords;
}

// Water's helpers: the search for other sources around each pump and the
// city's middle, for both source kinds that search (FindOtherSources,
// 0x006DB2B0, radii +0xAC and +0xB0), the cell of each pipe connection
// (ObjectCell, 0x006DA480; connections only, as for power), and the lot
// search for the city across (Serve, 0x006DCEE0, not switching on).
void AskWater(cISC4City* city, void* water, void* neighbors, void* across)
{
    using SearchFn = int32_t(__thiscall*)(void* self, int32_t x, int32_t z, uint32_t kind, int32_t radius);
    using CellFn = void(__thiscall*)(void* self, void* object, int32_t* x, int32_t* z);
    using ServeFn = uint8_t(__thiscall*)(void* self, void* key, uint8_t switchOn);
    const auto search = reinterpret_cast<SearchFn>(addr::cSC4PlumbingSimulator_FindOtherSources);
    const auto cell = reinterpret_cast<CellFn>(addr::cSC4PlumbingSimulator_ObjectCell);
    const auto searchAt = [water, &search](int32_t x, int32_t z) {
        Add(static_cast<uint32_t>(search(water, x, z, 0, At<int32_t>(water, 0xAC))));
        Add(static_cast<uint32_t>(search(water, x, z, 1, At<int32_t>(water, 0xB0))));
    };
    const auto ask = [water, &cell](void* object) {
        int32_t x = -7, z = -7;
        cell(water, object, &x, &z);
        Add(static_cast<uint32_t>(x));
        Add(static_cast<uint32_t>(z));
    };
    ForEachNode(At<void*>(water, 0xBC),
                [&searchAt](void** node) { searchAt(At<int32_t>(node, 0x8), At<int32_t>(node, 0xC)); });
    searchAt(Virtual<int32_t>(city, 0x174) / 2, Virtual<int32_t>(city, 0x178) / 2);
    if (neighbors)
        ForEachConnection(neighbors, 5, ask);
    if (across)
        Add(reinterpret_cast<ServeFn>(addr::cSC4PlumbingSimulator_Serve)(water, across, 0));
}

// OPENSC4_TEST_UTILITY_PATHS=<n>: on the n-th day of a run (before the
// power and water wake-ups of that day), a power line and a pipe to the
// city across an edge with deals to sell it power and water, then the
// deal news (0xEA418D8F) to both simulators, so their next pass serves the
// deals (FindConnectedOccupant, Serve, ObjectCell); and the helpers above
// asked directly. Run with OPENSC4_TEST_POWER_WAKE and _WATER_WAKE on the
// same day or later.
void Paths(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_UTILITY_PATHS") || !game::IsBuild641())
        return;
    g_ran = true;
    void* const power = day.city->GetPowerSimulator();
    void* const water = day.city->GetPlumbingSimulator();
    void* const neighbors = day.city->GetNeighborsSimulator();
    void* across = nullptr;
    int32_t x = 0, z = 0;
    if (neighbors && EdgeCellWithCity(day.city, neighbors, &x, &z, &across))
        ConnectAndDeal(neighbors, x, z, across);
    else
        log::Write("autotest: utilities: no city across any edge; no connections or deals");
    Add(across != nullptr);
    if (power)
        AskPower(power, neighbors, across);
    if (water)
        AskWater(day.city, water, neighbors, across);
    for (void* const simulator : {power, water})
        if (simulator)
            Deliver(simulator, 0xEA418D8F, nullptr, 0, 0);
    log::Write("autotest: utilities: paths asked, %08X", g_hash);
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (!g_ran)
        return;
    char text[24];
    std::snprintf(text, sizeof text, " | util %08X", g_hash);
    line += text;
}

const DayHook kPaths{"utilities.paths", 5, &Paths};
const FingerprintPart kFingerprint{"utilities", 25, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
