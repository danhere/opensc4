// Our cSC4TrafficSimulator (build 641), first part: the simple slots of its
// interface, swapped into the original object's vtable (docs/traffic.md).
// Fields, two hash tables keyed by cell (damaged roads, rail crossings),
// the per-building trip properties and the network edge lookup.
//
// The traffic code also holds patch sites of the Network Addon Mod (DLL
// and RUL-driven pathfinding; docs/patch-census.md): the functions those
// sites are in stay the game's, so that NAM keeps working whenever our
// replacements are off.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/occmgr_sim.h"
#include "core/registry.h"
#include "core/traffic_cells.h"
#include "core/virtual_clock.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <set>

namespace opensc4::traffic_sim {
namespace {

// Fields of cSC4TrafficSimulator used here (offsets from the object's start).
namespace field {
constexpr std::size_t kEdges = 0x7C;            // vector<vector<edge*>>: [x][z], a chain by +0x14
constexpr std::size_t kRandom = 0x6FC;          // the simulator's generator
constexpr std::size_t kMonthlyIncome = 0x788;   // float
constexpr std::size_t kDamagedRoads = 0x7D0;    // hash table: buckets begin, end
constexpr std::size_t kRailCrossings = 0x804;   // hash table: buckets begin, end
constexpr std::size_t kPhase = 0x814;          // the step's phase
constexpr std::size_t kInsertionPoint = 0x904;
constexpr std::size_t kTripScale = 0xC54;       // float
constexpr std::size_t kFreightScaling = 0xCF4;  // float
constexpr std::size_t kTripScaleForDisplay = 0xCFC; // float
constexpr std::size_t kRailAccidentChance = 0xD04;  // float
constexpr std::size_t kFerryTerminals = 0x128;  // uint32 by type
constexpr std::size_t kTrafficStats = 0x1E4;
} // namespace field

constexpr uint32_t kTripCapacityProperty = 0x8A0B4100;

// Hooks to leave out, for bisecting: OPENSC4_TRAFFIC_SKIP lists function
// addresses (hex, e.g. 00719910,00714740).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_TRAFFIC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("traffic: first %s", name);
}

// The rows of a sim grid (its slot 24 returns the vector of row vectors).
template <typename T>
T& GridCell(void* rows, int32_t x, int32_t z)
{
    return reinterpret_cast<T*>(static_cast<char***>(rows)[0][3 * x])[z];
}

// A big city's uint16 cell sets (damaged roads +0x7B8 and +0x7CC, rail
// crossings +0x7EC and +0x800): a 16-bit key cannot hold its cells, so with
// wide cells ours keep CellKeys here, ordered, and the game's sets stay
// empty. Emptied by Shutdown and before Read.
std::set<uint32_t>& WideSet(std::size_t offset)
{
    static std::set<uint32_t> sets[4];
    return sets[offset == 0x7B8 ? 0 : offset == 0x7CC ? 1 : offset == 0x7EC ? 2 : 3];
}

void ClearWideSets()
{
    for (std::size_t offset : {0x7B8u, 0x7CCu, 0x7ECu, 0x800u})
        WideSet(offset).clear();
    WideLinks().clear();
}

// A chained hash table keyed by a cell (x << 8 | z, 16 bits): whether the
// key is in it. `table` is the set's buckets (the set + 4).
bool InCellTable(void* self, std::size_t table, uint32_t x, uint32_t z)
{
    if (g_wideCells)
        return WideSet(table - 4).count(CellKey(static_cast<int32_t>(x), static_cast<int32_t>(z))) != 0;
    const auto key = static_cast<uint16_t>(((x & 0xFF) << 8) | z);
    void** const buckets = At<void**>(self, table);
    const auto count = static_cast<uint32_t>(At<void**>(self, table + 4) - buckets);
    int found = 0;
    for (void* node = buckets[key % count]; node; node = *static_cast<void**>(node))
        found += At<uint16_t>(node, 0x4) == key;
    return found != 0;
}

// 0x0060D170 (and the same search inline): the node of a 16-bit key in a
// chained hash table (buckets begin/end at +4/+8; node: next, key at +4).
char* FindCellNode(void* table, uint16_t key)
{
    char** const buckets = At<char**>(table, 4);
    const auto count = static_cast<uint32_t>(At<char**>(table, 8) - buckets);
    for (char* node = buckets[key % count]; node; node = At<char*>(node, 0)) {
        if (At<uint16_t>(node, 4) == key)
            return node;
    }
    return nullptr;
}

// 0x0070F9C0: the entry of a connection object's list (+0x10, circular,
// entries at node + 8) whose first word is the key.
char* FindConnection(char* object, uint16_t key)
{
    char* const head = At<char*>(object, 0x10);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        if (At<uint16_t>(node, 8) == key)
            return node + 8;
    }
    return nullptr;
}

// 0x0070F9F0: the entry of a connection object's list (+0x14) with a cell
// pair matching (a, b) at either end.
char* FindConnectionPair(char* object, int16_t a, int16_t b)
{
    char* const head = At<char*>(object, 0x14);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        char* const entry = node + 8;
        if ((At<int16_t>(entry, 0) == a && At<int16_t>(entry, 2) == b) ||
            (At<int16_t>(entry, 4) == a && At<int16_t>(entry, 6) == b))
            return entry;
    }
    return nullptr;
}

// The network edge of a type at a cell (the chain at [x][z], by +0x14), or
// null.
char* FindEdge(void* self, uint32_t type, int32_t x, int32_t z)
{
    char*** const rows = At<char***>(self, field::kEdges); // rows are vectors: begin, end, capacity
    for (char* edge = rows[3 * x][z]; edge; edge = At<char*>(edge, 0x14))
        if (static_cast<uint8_t>(edge[6]) == type)
            return edge;
    return nullptr;
}

// Whether the keys are wide (traffic_cells.h): a city past 256 cells a
// side, with big cities on.
void SetWideCells(int32_t width, int32_t height)
{
    g_wideCells = (width > 256 || height > 256) && occmgr_sim::BigCities();
}

// The lot multimap's value (+0x1A0, in the node from +0x10): a bucket and
// the lot's cell as bytes, then two bytes of padding. A big city's cell
// takes the four bytes as a CellKey.
struct LotValue {
    uint32_t key;
    uint8_t cell[4];
};

LotValue MakeLotValue(uint32_t bucket, int32_t x, int32_t z)
{
    LotValue value{bucket, {static_cast<uint8_t>(x), static_cast<uint8_t>(z), 0, 0}};
    if (g_wideCells) {
        const uint32_t cell = CellKey(x, z);
        std::memcpy(value.cell, &cell, 4);
    }
    return value;
}

uint32_t LotValueX(const char* node)
{
    return g_wideCells ? static_cast<uint32_t>(KeyX(At<uint32_t>(const_cast<char*>(node), 0x14)))
                       : static_cast<uint8_t>(node[0x14]);
}

uint32_t LotValueZ(const char* node)
{
    return g_wideCells ? static_cast<uint32_t>(KeyZ(At<uint32_t>(const_cast<char*>(node), 0x14)))
                       : static_cast<uint8_t>(node[0x15]);
}

// --- The getters -------------------------------------------------------------

// 5 (0x0071FFA0).
uint32_t __fastcall GetSimulatorType(void*, void*)
{
    return 0x88027F0D;
}

template <std::size_t Offset>
uint32_t __fastcall FieldGetter(void* self, void*)
{
    return At<uint32_t>(self, Offset);
}

template <std::size_t Offset>
float __fastcall FloatGetter(void* self, void*)
{
    return At<float>(self, Offset);
}

// 12 (0x00711460): the edge density record of a cell, travel type and
// commute (hash table at +0x17C keyed by evening << 24 | type << 16 |
// x << 8 | z; TypedCellKey), or the game's empty record (0xAB31D0). The
// game's takes the cell as bytes; a big city's needs the whole value.
const void* __fastcall GetTrafficEdgeDensity(void* self, void*, uint32_t x, uint32_t z, uint8_t travelType,
                                             uint8_t evening)
{
    static bool logged = false;
    FirstCall("GetTrafficEdgeDensity", logged);
    if (!g_wideCells) {
        x &= 0xFF;
        z &= 0xFF;
    }
    const uint32_t key = TypedCellKey(evening ? 1u : 0u, travelType, static_cast<int32_t>(x), static_cast<int32_t>(z));
    char** const buckets = At<char**>(self, 0x17C);
    const auto count = static_cast<uint32_t>(At<char**>(self, 0x180) - buckets);
    for (char* node = buckets[key % count]; node; node = At<char*>(node, 0))
        if (At<uint32_t>(node, 0x4) == key)
            return node + 0x8;
    return reinterpret_cast<const void*>(addr::cSC4TrafficSimulator_EmptyEdgeDensity);
}

// 16 (0x00720070).
uint8_t __fastcall IsRoadDamaged(void* self, void*, uint32_t x, uint32_t z)
{
    return InCellTable(self, field::kDamagedRoads, x, z) ? 1 : 0;
}

// 17 (0x00711590): a rail crossing at the cell, and a draw of the
// simulator's generator below the accident chance (+0xD04).
uint32_t __fastcall CheckRailAccident(void* self, void*, uint32_t x, uint32_t z)
{
    using RandomFn = double(__thiscall*)(void* random); // ST0
    if (!InCellTable(self, field::kRailCrossings, x, z))
        return 0;
    const double draw = reinterpret_cast<RandomFn>(addr::SC4Random_Unit)(At<void*>(self, field::kRandom));
    return draw < At<float>(self, field::kRailAccidentChance) ? 1 : 0;
}

// The building's trip property (0x8A0B4100): an array of uint16 (variant
// type 0x8004) or uint32, read at an index.
uint32_t TripProperty(void* holder, uint32_t index16, uint32_t index32)
{
    void* const property = Virtual<void*>(holder, 0x24, kTripCapacityProperty);
    if (!property)
        return 0;
    void* const variant = Virtual<void*>(property, 0x14);
    if (Virtual<uint32_t>(variant, 0xC) == 0x8004)
        return Virtual<const uint16_t*>(variant, 0x18C)[index16];
    return Virtual<const uint32_t*>(variant, 0x19C)[index32];
}

// 19 (0x0070DF70, through 0x0070DE70): sets one of the building's 12 trip
// values, adding the property (uint16 array of zeros) if missing and
// widening the array to uint32 for a value of 0xFFFF or more. Returns the
// array's address, as the game leaves it in EAX.
uint32_t __fastcall SetMaxTripCapacity(void*, void*, void* holder, uint32_t kind, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetMaxTripCapacity", logged);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using ConstructFn = void*(__thiscall*)(void* memory, uint32_t);
    void* property = Virtual<void*>(holder, 0x24, kTripCapacityProperty);
    if (!property) {
        void* const memory = reinterpret_cast<AllocateFn>(addr::SC4Property_Allocate)(0x1C);
        property = memory ? reinterpret_cast<ConstructFn>(addr::SC4Property_Construct)(memory, 0) : nullptr;
        Virtual<uint32_t>(property, 0x4);
        void* const variant = Virtual<void*>(property, 0x14);
        uint16_t zeros[12] = {};
        Virtual<uint32_t>(variant, 0x188, zeros, 12u);
        Virtual<uint32_t>(property, 0x10, kTripCapacityProperty);
        Virtual<uint32_t>(holder, 0x3C, property, 0u);
        Virtual<uint32_t>(property, 0x8);
    }
    void* const variant = Virtual<void*>(property, 0x14);
    if (Virtual<uint32_t>(variant, 0xC) == 0x8004) {
        uint16_t* const narrow = Virtual<uint16_t*>(variant, 0x18C);
        if (value < 0xFFFF) {
            narrow[kind] = static_cast<uint16_t>(value);
            return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(narrow));
        }
        uint32_t wide[12];
        for (int i = 0; i < 12; ++i)
            wide[i] = narrow[i];
        Virtual<uint32_t>(variant, 0x198, wide, 12u);
    }
    uint32_t* const values = Virtual<uint32_t*>(variant, 0x19C);
    values[kind] = value;
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(values));
}

// 20 (0x0070DF90): the most trips of a kind the building can make.
uint32_t __fastcall GetMaxTripCapacity(void*, void*, void* holder, uint32_t kind)
{
    return TripProperty(holder, kind, kind);
}

// 21 (0x0070E050): the trips of a kind that reached it (the array's second
// half: 8 entries on).
uint32_t __fastcall GetTrafficArrived(void*, void*, void* holder, uint32_t kind)
{
    return TripProperty(holder, kind + 8, kind + 8);
}

// 22 (0x00720010).
uint32_t __fastcall GetDesiredLotInsertionPoint(void* self, void*)
{
    return At<uint32_t>(self, field::kPhase) == 1 ? At<uint32_t>(self, field::kInsertionPoint) : 0;
}

// 23 (0x00710320): 1 / the edge's first value, or 0 without an edge (ST0).
double __fastcall GetCapacity(void* self, void*, uint32_t type, int32_t x, int32_t z)
{
    const char* const edge = FindEdge(self, type, x, z);
    if (!edge)
        return 0.0f;
    return 1.0f / static_cast<double>(At<float>(const_cast<char*>(edge), 0));
}

// 24 (0x007103B0): 1.0f for a travel type not in use or without an edge of
// its network at the cell; else the travel time curve at the cell's
// traffic (both commutes of the types marked at +0xC34, slot 12's +0x20)
// x the edge's first float, unrounded in ST0.
double __fastcall GetTravelTimeRatio(void* self, void*, int32_t x, int32_t z, uint32_t travelType)
{
    using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0
    static bool logged = false;
    FirstCall("GetTravelTimeRatio", logged);
    const float one = 1.0f;
    if (!reinterpret_cast<const uint8_t*>(addr::SC4Traffic_TravelTypeEnabled)[travelType])
        return one;
    uint32_t traffic = 0;
    for (uint32_t type = 0; type < 9; ++type) {
        if (!At<uint8_t>(self, 0xC34 + type))
            continue;
        auto density = [self, x, z, type](uint8_t evening) {
            return Virtual<char*>(self, 0x30, static_cast<uint32_t>(x), static_cast<uint32_t>(z),
                                  static_cast<uint8_t>(type), evening); // slot 12
        };
        traffic += At<uint16_t>(density(0), 0x20);
        traffic += At<uint16_t>(density(1), 0x20);
    }
    const uint32_t network = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkOfTravelType)[travelType];
    char* const edge = FindEdge(self, network, x, z);
    if (!edge)
        return one;
    const auto load = static_cast<float>(static_cast<double>(traffic) * At<float>(edge, 0));
    return reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(reinterpret_cast<void*>(addr::SC4Traffic_TravelTimeCurve), load);
}

// 25 (0x00710370).
uint32_t __fastcall GetConnectionCount(void* self, void*, uint32_t type, int32_t x, int32_t z)
{
    const char* const edge = FindEdge(self, type, x, z);
    return edge ? static_cast<uint8_t>(edge[4]) : 0;
}

// 26 (0x00718BB0): three percentages of a wealth (bytes at +0xC5C + 3 x
// wealth, x 0.01f) into a vector resized to 3. Returns the last byte, as
// the game leaves it in EAX.
uint32_t __fastcall GetTravelStrategyPercentages(void* self, void*, uint32_t wealth, float** percentages)
{
    using ResizeFn = void(__thiscall*)(float** vector, uint32_t count, const uint32_t* value);
    static bool logged = false;
    FirstCall("GetTravelStrategyPercentages", logged);
    const uint32_t zero = 0;
    reinterpret_cast<ResizeFn>(addr::SC4FloatVector_Resize)(percentages, 3, &zero);
    const uint8_t* const bytes = &At<uint8_t>(self, 0xC5C + 3 * wealth);
    for (int i = 0; i < 3; ++i)
        (*percentages)[i] = static_cast<float>(static_cast<double>(bytes[i]) * 0.01f);
    return bytes[2];
}

// 32 (0x0071FFF0).
uint32_t __fastcall GetFerryTerminalCount(void* self, void*, uint32_t type)
{
    return At<uint32_t>(self, field::kFerryTerminals + 4 * type);
}

// 33 (0x0070EC10): the water route finder at +0x108, given a pointer to
// the stack slot that held the vector (as the game passes it) and a flag 0.
uint32_t __fastcall GetWaterRoute(void* self, void*, int32_t x1, int32_t z1, int32_t x2, int32_t z2, void* route)
{
    static bool logged = false;
    FirstCall("GetWaterRoute", logged);
    using FindFn = uint32_t(__thiscall*)(void* finder, int32_t, int32_t, int32_t, int32_t, void** slot, void* route,
                                         uint32_t flag);
    void* slot = route;
    return reinterpret_cast<FindFn>(addr::SC4WaterRouteFinder_Find)(At<void*>(self, 0x108), x1, z1, x2, z2, &slot, route, 0);
}

// 34 (0x00720000).
void* __fastcall GetTrafficStats(void* self, void*)
{
    return static_cast<char*>(self) + field::kTrafficStats;
}

// 39 (0x00711B50): the vector emptied, then the lot's property 0x2A4F9C74
// (lot slot 3 gives its property holder).
uint32_t __fastcall GetSubnetworksForLot(void*, void*, void* lot, void** subnetworks)
{
    static bool logged = false;
    FirstCall("GetSubnetworksForLot", logged);
    using ReadFn = uint32_t(__cdecl*)(void* holder, uint32_t property, void** vector);
    subnetworks[1] = subnetworks[0];
    void* const holder = Virtual<void*>(lot, 0xC);
    return reinterpret_cast<ReadFn>(addr::SC4_GetPropertyUint32Vector)(holder, 0x2A4F9C74, subnetworks);
}

// 0 (0x0070D7E0): the simulator itself for its own ID and 1, the message
// target at +4 (0x452294AA), the serializable at +8 (0xE4FDA3D4), each
// with a reference added.
uint8_t __fastcall QueryInterface(void* self, void*, uint32_t id, void** out)
{
    char* found;
    if (id == 0x68027F37 || id == 1)
        found = static_cast<char*>(self);
    else if (id == 0x452294AA)
        found = static_cast<char*>(self) + 4;
    else if (id == 0xE4FDA3D4)
        found = static_cast<char*>(self) + 8;
    else
        return 0;
    *out = found;
    Virtual<uint32_t>(self, 0x4); // slot 1: AddRef
    return 1;
}

// 2 (0x0070E2F0): the last reference shuts the simulator down (slot 4) and
// deletes it (slot 43); the count stays at 1 then.
uint32_t __fastcall Release(void* self, void*)
{
    int32_t& references = At<int32_t>(self, 0x10);
    if (references > 1)
        return static_cast<uint32_t>(--references);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0xAC, 1u);
    return 0;
}

// 11 (0x0070E870): the background traffic at a cell: three weighted sums
// of int16 grid values (0x00B43D68 slot 32 by ID, slot 15, slot 7 (x, z)):
// residential (0x1010-0x1030) x +0xCE8, commercial (0x3110-0x3130,
// 0x3320, 0x3330) x +0xCEC, industrial (0x4100-0x4400) x +0xCF0; ST0.
double __fastcall GetBackgroundTraffic(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("GetBackgroundTraffic", logged);
    void* const service = Global<void*>(addr::SC4_Service43D68);
    auto sum = [service, x, z](std::initializer_list<uint32_t> ids) {
        int32_t total = 0;
        for (const uint32_t id : ids) {
            void* const grid = Virtual<void*>(Virtual<void*>(service, 0x80, id), 0x3C);
            total += static_cast<int16_t>(Virtual<uint16_t>(grid, 0x1C, x, z));
        }
        return total;
    };
    const int32_t residential = sum({0x1010, 0x1020, 0x1030});
    const int32_t commercial = sum({0x3110, 0x3120, 0x3130, 0x3320, 0x3330});
    const int32_t industrial = sum({0x4100, 0x4200, 0x4300, 0x4400});
    return static_cast<double>(residential) * At<float>(self, 0xCE8) +
           static_cast<double>(commercial) * At<float>(self, 0xCEC) +
           static_cast<double>(industrial) * At<float>(self, 0xCF0);
}

// The 28-byte subnetwork records at +0x70: a std::map<uint32, uint32> of
// occupant counts by type (head at +0), then by kind the counts of
// destinations at +0xC.
constexpr std::size_t kSubnetworkRecord = 28;

uint32_t SubnetworkCount(void* self)
{
    return static_cast<uint32_t>((At<char*>(self, 0x74) - At<char*>(self, 0x70)) / static_cast<int32_t>(kSubnetworkRecord));
}

// std::map<uint32, uint32>::find: the node of a key, or the head.
char* FindCount(char* head, uint32_t key)
{
    char* best = head;
    for (char* node = At<char*>(head, 4); node;) {
        if (At<uint32_t>(node, 0x10) >= key) {
            best = node;
            node = At<char*>(node, 8);
        } else {
            node = At<char*>(node, 0xC);
        }
    }
    return best != head && !(key < At<uint32_t>(best, 0x10)) ? best : head;
}

// The lot's subnetworks (property 0x2A4F9C74) and the largest value over
// those in range, or 0 without the property.
template <typename ValueFn>
uint32_t MaxOverLotSubnetworks(void* self, void* lot, ValueFn value)
{
    using ReadFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t** vector);
    using FreeFn = void(__cdecl*)(void* memory);
    uint32_t* subnetworks[3] = {};
    void* const holder = Virtual<void*>(lot, 0xC);
    uint32_t best = 0;
    if (reinterpret_cast<ReadFn>(addr::SC4_GetPropertyUint32Vector)(holder, 0x2A4F9C74, subnetworks)) {
        const auto count = static_cast<int32_t>(subnetworks[1] - subnetworks[0]);
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t index = subnetworks[0][i];
            if (index >= SubnetworkCount(self))
                continue;
            const uint32_t v = value(At<char*>(self, 0x70) + kSubnetworkRecord * index);
            if (v > best)
                best = v;
        }
    }
    if (subnetworks[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(subnetworks[0]);
    return best;
}

bool InCity(void* self, int32_t x, int32_t z)
{
    return x >= At<int32_t>(self, 0x18C) && z >= At<int32_t>(self, 0x190) && x <= At<int32_t>(self, 0x194) &&
           z <= At<int32_t>(self, 0x198);
}

uint16_t CellKey(int32_t x, int32_t z)
{
    return static_cast<uint16_t>(((x & 0xFF) << 8) | z);
}

// The entry of a connection object's list at +0x14 whose cell pair at
// `offset` (0 or 4) is (x, z).
char* FindConnectionEnd(char* object, std::size_t offset, int32_t x, int32_t z)
{
    char* const head = At<char*>(object, 0x14);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        if (At<int16_t>(node, 8 + offset) == x && At<int16_t>(node, 10 + offset) == z)
            return node + 8;
    }
    return nullptr;
}

// Links a node at the end of a circular list (head: next, prev).
void LinkAtEnd(char* head, char* node)
{
    char* const last = At<char*>(head, 4);
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
}

// 28 (0x00714460): the transit switch occupants (the first value of each
// node of the table at +0x70C, in bucket order), each with a reference
// added, in a list emptied first. Returns what the game leaves in EAX: the
// bucket count (when the table is empty, the address of a local: nonzero).
uint32_t __fastcall GetTransitSwitches(void* self, void*, char** list)
{
    using ClearFn = void(__thiscall*)(char** list);
    static bool logged = false;
    FirstCall("GetTransitSwitches", logged);
    reinterpret_cast<ClearFn>(addr::SC4List_Clear2)(list);
    char* const table = static_cast<char*>(self) + 0x70C;
    char** const buckets = At<char**>(table, 4);
    const auto count = static_cast<uint32_t>(At<char**>(table, 8) - buckets);
    char* node = nullptr;
    for (uint32_t i = 0; i < count && !node; ++i)
        node = buckets[i];
    if (!node)
        return 1;
    while (node) {
        void* const occupant = At<void*>(node, 8);
        Virtual<uint32_t>(occupant, 0x4); // AddRef
        char* const link = Virtual<char*>(ListNodeAllocator(), 0xC, 12u);
        At<void*>(link, 8) = occupant;
        LinkAtEnd(*list, link);
        char* next = At<char*>(node, 0);
        for (uint32_t i = At<uint32_t>(node, 4) % count + 1; !next && i < count; ++i)
            next = buckets[i];
        node = next;
    }
    return count;
}

// 30 (0x00718C40): every ferry route: the route bytes of both sub-lists
// (+0x18, +0x1C; vector at +0x14 of each node) of each entry of +0x104,
// copied into a list of byte vectors emptied first. Returns the entry
// list's head, as the game leaves it in EAX.
uint32_t __fastcall GetAllFerryRoutes(void* self, void*, char** routes)
{
    using AllocateFn = char*(__cdecl*)(std::size_t size);
    using CopyFn = void(__thiscall*)(void* to, const void* from);
    using FreeFn = void(__cdecl*)(void* memory);
    static bool logged = false;
    FirstCall("GetAllFerryRoutes", logged);
    char* const head = *routes;
    for (char* node = At<char*>(head, 0); node != head;) {
        char* const next = At<char*>(node, 0);
        if (At<void*>(node, 8))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(node, 8));
        reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
        node = next;
    }
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    char* const entries = At<char*>(self, 0x104);
    for (char* entry = At<char*>(entries, 0); entry != entries; entry = At<char*>(entry, 0)) {
        for (const std::size_t list : {std::size_t{0x18}, std::size_t{0x1C}}) {
            char* const sub = At<char*>(entry, list);
            for (char* item = At<char*>(sub, 0); item != sub; item = At<char*>(item, 0)) {
                char* const node = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x14);
                reinterpret_cast<CopyFn>(addr::SC4ByteVector_CopyConstruct)(node + 8, item + 0x14);
                LinkAtEnd(*routes, node);
            }
        }
    }
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(entries));
}

// 29 (0x00713B70): the ferry route record between two cells: with the
// first in the city, its connection (+0xDC) and the entry to the second
// (the second's own connection, +0xF0, when it has one; else the entry
// whose far end, or near end for a second outside the city, is the cell),
// +0xC into the entry; with the first outside, the second's connection
// (+0xF0, keyed by the second cell) and the entry starting at the first,
// +0x18 into it. Null when there is none.
char* __fastcall GetFerryRouteBetweenTiles(void* self, void*, int32_t x1, int32_t z1, int32_t x2, int32_t z2)
{
    static bool logged = false;
    FirstCall("GetFerryRouteBetweenTiles", logged);
    char* const base = static_cast<char*>(self);
    if (!InCity(self, x1, z1)) {
        char* const node = FindCellNode(base + 0xF0, CellKey(x2, z2));
        if (!node)
            return nullptr;
        char* const entry = FindConnectionEnd(At<char*>(node, 8) + 8, 0, x1, z1);
        return entry ? entry + 0x18 : nullptr;
    }
    char* const first = FindCellNode(base + 0xDC, CellKey(x1, z1));
    if (!first)
        return nullptr;
    char* const object = At<char*>(first, 8) + 8;
    char* entry;
    if (!InCity(self, x2, z2)) {
        entry = FindConnectionEnd(object, 0, x2, z2);
    } else if (char* const second = FindCellNode(base + 0xF0, CellKey(x2, z2))) {
        char* const target = At<char*>(second, 8) + 8;
        entry = FindConnection(object, static_cast<uint16_t>(At<uint8_t>(target, 0) << 8 | At<uint8_t>(target, 1)));
    } else {
        entry = FindConnectionEnd(object, 4, x2, z2);
    }
    return entry ? entry + 0xC : nullptr;
}

// The budget's cost of a transit switch occupant (its property holder,
// slot 6, and property 0xCA550301 to the budget simulator's slot 51; slot
// 28 for the line item): the
// switch's count x the line item's first float, truncated, into +0x10 of
// the query data.
void SwitchCost(void* occupant, uint32_t* data)
{
    struct Query {
        uint32_t id;
        uint32_t unknown[3];
        int64_t count;
        uint32_t more[1];
        uint32_t cleared;
        uint32_t tail[4];
    } query{};
    static_assert(offsetof(Query, count) == 0x10 && offsetof(Query, cleared) == 0x1C);
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    void* const holder = Virtual<void*>(occupant, 0x18); // slot 6: its property holder
    if (!Virtual<uint8_t>(budget, 0xCC, holder, 0xCA550301u, &query))
        return;
    void* const item = Virtual<void*>(budget, 0x70, query.id);
    if (!item)
        return;
    const float* const cost = Virtual<const float*>(item, 0x28);
    data[4] = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(query.count) * *cost));
}

// 35 (0x0071CB80): what the transit switch query shows for a cell: on an
// edge (+0x704 slot 7) its cost record (+0x70C: values at +0xC and +0x14,
// truncated, and +0x18; the defaults when +0x7A4 is 1), else the switch of
// the lot there (the first entry of +0x104 inside the lot: +0x20 and 1.0).
// False only without a lot or switch.
uint8_t __fastcall GetTransitSwitchQueryData(void* self, void*, int32_t x, int32_t z, uint32_t* data)
{
    using CostFn = char*(__thiscall*)(void* table, const uint32_t* key);
    static bool logged = false;
    FirstCall("GetTransitSwitchQueryData", logged);
    uint32_t edge = Virtual<uint32_t>(At<void*>(self, 0x704), 0x1C, x, z);
    if (edge) {
        char* const record = reinterpret_cast<CostFn>(addr::SC4CostTable_At)(static_cast<char*>(self) + 0x70C, &edge);
        data[1] = static_cast<uint32_t>(static_cast<int32_t>(At<float>(record, 0xC)));
        if (At<uint32_t>(self, 0x7A4) == 1) {
            data[0] = 0;
            data[3] = 0;
            data[2] = Global<uint32_t>(addr::cSC4TrafficSimulator_RecordDefault);
        } else {
            data[0] = static_cast<uint32_t>(static_cast<int32_t>(At<float>(record, 0xC)));
            data[3] = static_cast<uint32_t>(static_cast<int32_t>(At<float>(record, 0x14)));
            data[2] = At<uint32_t>(record, 0x18);
        }
        data[4] = 0;
        SwitchCost(At<void*>(record, 0), data);
        return 1;
    }
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u);
    if (!lot)
        return 0;
    int32_t rect[4];
    Virtual<void>(lot, 0x38, rect);
    char* const head = At<char*>(self, 0x104);
    char* node = At<char*>(head, 0);
    for (; node != head; node = At<char*>(node, 0)) {
        const int32_t cx = At<uint8_t>(node, 0x24);
        const int32_t cz = At<uint8_t>(node, 0x25);
        if (cx >= rect[0] && cz >= rect[1] && cx <= rect[2] && cz <= rect[3])
            break;
    }
    if (node == head)
        return 0;
    if (At<uint32_t>(self, 0x7A4) == 1) {
        data[0] = 0;
        data[1] = 0;
        data[3] = 0;
        data[2] = Global<uint32_t>(addr::cSC4TrafficSimulator_RecordDefault);
    } else {
        data[0] = At<uint16_t>(node, 0x20);
        data[1] = At<uint16_t>(node, 0x20);
        data[3] = At<uint16_t>(node, 0x20);
        data[2] = Global<uint32_t>(addr::SC4_FloatOne);
    }
    data[4] = 0;
    if (void* const occupant = Virtual<void*>(lot, 0xC0)) // slot 48
        SwitchCost(Virtual<void*>(occupant, 0xC), data);
    return 1;
}

// 36 (0x00710540): whether two lots share a subnetwork (property
// 0x2A4F9C74 of each).
uint8_t __fastcall AreLotsConnected(void*, void*, void* first, void* second)
{
    using ReadFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t** vector);
    using FreeFn = void(__cdecl*)(void* memory);
    static bool logged = false;
    FirstCall("AreLotsConnected", logged);
    uint32_t* a[3] = {};
    uint32_t* b[3] = {};
    bool shared = false;
    if (reinterpret_cast<ReadFn>(addr::SC4_GetPropertyUint32Vector)(Virtual<void*>(first, 0xC), 0x2A4F9C74, a) &&
        reinterpret_cast<ReadFn>(addr::SC4_GetPropertyUint32Vector)(Virtual<void*>(second, 0xC), 0x2A4F9C74, b)) {
        const auto countA = a[1] - a[0];
        const auto countB = b[1] - b[0];
        for (std::ptrdiff_t i = 0; i < countA && !shared; ++i) {
            for (std::ptrdiff_t j = 0; j < countB; ++j) {
                if (a[0][i] == b[0][j]) {
                    shared = true;
                    break;
                }
            }
        }
    }
    if (b[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(b[0]);
    if (a[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(a[0]);
    return shared ? 1 : 0;
}

// 37 (0x00711A70): the most occupants of a type in any of the lot's
// subnetworks.
uint32_t __fastcall GetConnectedOccupantCount(void* self, void*, void* lot, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetConnectedOccupantCount", logged);
    return MaxOverLotSubnetworks(self, lot, [type](char* record) {
        char* const head = At<char*>(record, 0);
        char* const node = FindCount(head, type);
        return node != head ? At<uint32_t>(node, 0x14) : 0u;
    });
}

// 38 (0x007106C0): the most destinations of a kind (0-3) in any of the
// lot's subnetworks; 0 for a kind above 3.
uint32_t __fastcall GetConnectedDestinationCount(void* self, void*, void* lot, int32_t kind)
{
    static bool logged = false;
    FirstCall("GetConnectedDestinationCount", logged);
    if (kind > 3)
        return 0;
    return MaxOverLotSubnetworks(self, lot, [kind](char* record) { return At<uint32_t>(record, 0xC + 4 * kind); });
}

// The subnetwork IDs of the road edges (type 1; four words at +0xC) on the
// cells of a rectangle inside the city that pass a filter, gathered in a
// hash set (the game's) and copied into the vector in bucket order.
// Returns what the game leaves in EAX (the bucket array's free).
template <typename Filter>
uint32_t SubnetworksIn(void* self, const int32_t* rect, Filter inside, uint32_t** subnetworks)
{
    struct HashSet {
        uint32_t first;
        void** begin;
        void** end;
        uint32_t unused;
        uint32_t count;
    };
    using ConstructFn = void(__thiscall*)(HashSet* set, uint32_t buckets);
    using ResizeFn = void(__thiscall*)(HashSet* set, uint32_t count);
    using SetInsertFn = void(__thiscall*)(HashSet* set, void* result, const uint32_t* value);
    using ClearFn = void(__thiscall*)(HashSet* set);
    using InsertFn = void(__thiscall*)(uint32_t** vector, uint32_t* where, uint32_t count, const uint32_t* value);
    using FreeFn = uint32_t(__cdecl*)(void* memory);
    HashSet set{};
    reinterpret_cast<ConstructFn>(addr::SC4Uint32HashSet_Construct)(&set, 100);
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            if (!inside(x, z) || !InCity(self, x, z))
                continue;
            char* node = GridCell<char*>(static_cast<char*>(self) + field::kEdges, x, z);
            while (node && At<uint8_t>(node, 6) != 1)
                node = At<char*>(node, 0x14);
            if (!node)
                continue;
            for (int side = 0; side < 4; ++side) {
                const uint16_t id = At<uint16_t>(node, 0xC + 2 * side);
                if (!id)
                    continue;
                const uint32_t value = id;
                uint32_t result[3]; // pair<iterator {node, table}, bool>: 9 bytes
                reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(&set, set.count + 1);
                reinterpret_cast<SetInsertFn>(addr::SC4Uint32HashSet_Insert)(&set, result, &value);
            }
        }
    }
    subnetworks[1] = subnetworks[0];
    const auto buckets = static_cast<uint32_t>(set.end - set.begin);
    char* node = nullptr;
    for (uint32_t i = 0; i < buckets && !node; ++i)
        node = static_cast<char*>(set.begin[i]);
    while (node) {
        const uint32_t value = At<uint32_t>(node, 4);
        reinterpret_cast<InsertFn>(addr::SC4Uint32Vector_InsertN)(subnetworks, subnetworks[1], 1, &value);
        char* next = At<char*>(node, 0);
        for (uint32_t i = value % buckets + 1; !next && i < buckets; ++i)
            next = static_cast<char*>(set.begin[i]);
        node = next;
    }
    reinterpret_cast<ClearFn>(addr::SC4Uint32HashSet_Clear)(&set);
    return set.begin ? reinterpret_cast<FreeFn>(addr::SC4_Free)(set.begin) : 0;
}

// 40 (0x00718D20): the subnetworks in a rectangle.
uint32_t __fastcall GetSubnetworksInRectangle(void* self, void*, const int32_t* rect, uint32_t** subnetworks)
{
    static bool logged = false;
    FirstCall("GetSubnetworksInRectangle", logged);
    return SubnetworksIn(self, rect, [](int32_t, int32_t) { return true; }, subnetworks);
}

// 41 (0x00718F90): the subnetworks in a cell region: its rectangle at +0,
// width +0x14, height +0x18 and rows of bits at +0x20.
uint32_t __fastcall GetSubnetworksInRegion(void* self, void*, const int32_t* region, uint32_t** subnetworks)
{
    static bool logged = false;
    FirstCall("GetSubnetworksInRegion", logged);
    auto inside = [region](int32_t x, int32_t z) {
        const auto dx = static_cast<uint32_t>(x - region[0]);
        const auto dz = static_cast<uint32_t>(z - region[1]);
        if (dx >= static_cast<uint32_t>(region[5]) || dz >= static_cast<uint32_t>(region[6]))
            return false;
        const uint32_t* const row = reinterpret_cast<uint32_t* const*>(static_cast<std::uintptr_t>(region[8]))[dx];
        return (row[dz >> 5] & (1u << (dz & 31))) != 0;
    };
    return SubnetworksIn(self, region, inside, subnetworks);
}

// 42 (0x007143B0): the occupants of a type in each subnetwork (index 0
// left at 0). Returns what the game leaves in EAX: the vector's start when
// the last subnetwork has the type, else 0 (the assign's result with fewer
// than two subnetworks).
uint32_t __fastcall GetOccupantCountForAllSubnetworks(void* self, void*, uint32_t type, uint32_t** counts)
{
    using AssignFn = uint32_t(__thiscall*)(uint32_t** vector, uint32_t count, const uint32_t* value);
    static bool logged = false;
    FirstCall("GetOccupantCountForAllSubnetworks", logged);
    const auto count = static_cast<int32_t>(SubnetworkCount(self));
    const uint32_t zero = 0;
    uint32_t result = reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(counts, static_cast<uint32_t>(count), &zero);
    for (int32_t i = 1; i < count; ++i) {
        char* const head = At<char*>(At<char*>(self, 0x70) + kSubnetworkRecord * i, 0);
        char* const node = FindCount(head, type);
        result = 0;
        if (node != head) {
            counts[0][i] = At<uint32_t>(node, 0x14);
            result = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(counts[0]));
        }
    }
    return result;
}

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(0, 0x0070D7E0, QueryInterface),
    OPENSC4_SLOT(2, 0x0070E2F0, Release),
    OPENSC4_SLOT(5, 0x0071FFA0, GetSimulatorType),
    OPENSC4_SLOT(8, 0x006AAC50, FieldGetter<0x30>),
    OPENSC4_SLOT(9, 0x0080BC00, FieldGetter<0x38>),
    OPENSC4_SLOT(10, 0x00698CF0, FieldGetter<0x48>),
    OPENSC4_SLOT(11, 0x0070E870, GetBackgroundTraffic),
    OPENSC4_SLOT(12, 0x00711460, GetTrafficEdgeDensity),
    OPENSC4_SLOT(13, 0x00820250, FieldGetter<0x44>),
    OPENSC4_SLOT(14, 0x0071FFB0, FloatGetter<field::kTripScale>),
    OPENSC4_SLOT(15, 0x0071FFC0, FloatGetter<field::kTripScaleForDisplay>),
    OPENSC4_SLOT(16, 0x00720070, IsRoadDamaged),
    OPENSC4_SLOT(17, 0x00711590, CheckRailAccident),
    OPENSC4_SLOT(18, 0x0071FFD0, FloatGetter<field::kMonthlyIncome>),
    OPENSC4_SLOT(19, 0x0070DF70, SetMaxTripCapacity),
    OPENSC4_SLOT(20, 0x0070DF90, GetMaxTripCapacity),
    OPENSC4_SLOT(21, 0x0070E050, GetTrafficArrived),
    OPENSC4_SLOT(22, 0x00720010, GetDesiredLotInsertionPoint),
    OPENSC4_SLOT(23, 0x00710320, GetCapacity),
    OPENSC4_SLOT(24, 0x007103B0, GetTravelTimeRatio),
    OPENSC4_SLOT(25, 0x00710370, GetConnectionCount),
    OPENSC4_SLOT(26, 0x00718BB0, GetTravelStrategyPercentages),
    OPENSC4_SLOT(27, 0x0071FFE0, FloatGetter<field::kFreightScaling>),
    OPENSC4_SLOT(28, 0x00714460, GetTransitSwitches),
    OPENSC4_SLOT(29, 0x00713B70, GetFerryRouteBetweenTiles),
    OPENSC4_SLOT(30, 0x00718C40, GetAllFerryRoutes),
    OPENSC4_SLOT(32, 0x0071FFF0, GetFerryTerminalCount),
    OPENSC4_SLOT(33, 0x0070EC10, GetWaterRoute),
    OPENSC4_SLOT(34, 0x00720000, GetTrafficStats),
    OPENSC4_SLOT(35, 0x0071CB80, GetTransitSwitchQueryData),
    OPENSC4_SLOT(36, 0x00710540, AreLotsConnected),
    OPENSC4_SLOT(37, 0x00711A70, GetConnectedOccupantCount),
    OPENSC4_SLOT(38, 0x007106C0, GetConnectedDestinationCount),
    OPENSC4_SLOT(39, 0x00711B50, GetSubnetworksForLot),
    OPENSC4_SLOT(40, 0x00718D20, GetSubnetworksInRectangle),
    OPENSC4_SLOT(41, 0x00718F90, GetSubnetworksInRegion),
    OPENSC4_SLOT(42, 0x007143B0, GetOccupantCountForAllSubnetworks),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 44;

// Swaps every slot or none: each must still hold the game's function.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4TrafficSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("traffic interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("traffic interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("traffic interface: %u of 43 cISC4TrafficSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])));
    return true;
}

const bool g_registered = registry::Register({
    "simulators.traffic.interface",
    "simulators/traffic",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_vftable,
    "Our cISC4TrafficSimulator getters on the original object (docs/traffic.md)",
    false,
    Install,
});


// --- The step (0x007215C0) ------------------------------------------------------

namespace step {
constexpr std::size_t kLastDay = 0x1AC;           // day of the month at the last step
constexpr std::size_t kLots = 0x1A0;              // multimap: bucket -> lot cell (node +0x14 x, +0x15 z)
constexpr std::size_t kDepartment1 = 0x724;       // budget departments (funding: slot 10)
constexpr std::size_t kDepartment2 = 0x728;
constexpr std::size_t kIncome2 = 0x780;           // float, to line item 2 of department 2
constexpr std::size_t kIncome1 = 0x784;           // float, to line item 5 of department 1
constexpr std::size_t kBucket = 0x928;            // the next bucket of +0x1A0
constexpr std::size_t kDaysPerCycle = 0xC48;      // the buckets go round in this many days
constexpr std::size_t kBuckets = 0xC4C;
constexpr std::size_t kDone = 0x914;
constexpr std::size_t kPart = 0x918;
constexpr std::size_t kTotal = 0x91C;
constexpr std::size_t kParts = 0x92C;
constexpr std::size_t kCount16 = 0x93C;           // what the phases 16, 17 and 18 work through
constexpr std::size_t kCount17 = 0x940;
constexpr std::size_t kCount18 = 0x944;
constexpr uint32_t kLotNoise = 0x4A611773;        // lot property set to a random 0-99
} // namespace step

// The time budget (0x0068BF50: rdtsc based, made deterministic by
// determinism.fixed_ticks).
struct Deadline {
    uint32_t end[2];
};

bool Expired(Deadline& deadline)
{
    return reinterpret_cast<uint8_t(__thiscall*)(Deadline*)>(addr::SC4Deadline_Expired)(&deadline) != 0;
}

// The multimap's nodes: +4 parent, +8 left, +0xC right, +0x10 key.
char* Link(char* node, std::size_t offset) { return At<char*>(node, offset); }

// In order, as the game's iterator does (the header's right is the root's
// parent side).
char* NextNode(char* node)
{
    char* next = Link(node, 0xC);
    if (next) {
        while (Link(next, 0x8))
            next = Link(next, 0x8);
        return next;
    }
    char* result = node;
    char* parent = Link(node, 0x4);
    while (result == Link(parent, 0xC)) {
        result = parent;
        parent = Link(parent, 0x4);
    }
    return Link(result, 0xC) != parent ? parent : result;
}

// Monthly: last month's incomes into two budget line items; their sum is
// slot 18's.
void BookIncome(void* self)
{
    auto book = [self](std::size_t department, uint32_t index, std::size_t income) {
        void* const owner = At<void*>(self, department);
        void* const item = Virtual<void*>(owner, 0x50, index);
        Virtual<void>(item, 0x3C, static_cast<int64_t>(At<float>(self, income)));
    };
    book(step::kDepartment2, 2, step::kIncome2);
    book(step::kDepartment1, 5, step::kIncome1);
    At<float>(self, field::kMonthlyIncome) =
        static_cast<float>(static_cast<double>(At<float>(self, step::kIncome1)) + At<float>(self, step::kIncome2));
}

// Every few days: each lot of the next bucket gets a new random 0-99 in its
// property 0x4A611773.
void StirBucket(void* self)
{
    using EqualRangeFn = void(__thiscall*)(void* map, char** range, const uint32_t* key);
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
    using SetFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    char* range[2] = {};
    reinterpret_cast<EqualRangeFn>(addr::SC4Multimap_EqualRange)(static_cast<char*>(self) + step::kLots, range,
                                                                &At<uint32_t>(self, step::kBucket));
    for (char* node = range[0]; node != range[1]; node = NextNode(node)) {
        void* const lot = Virtual<void*>(lots, 0x18, LotValueX(node), LotValueZ(node), 0u);
        if (!lot)
            continue;
        const auto noise = static_cast<uint8_t>(reinterpret_cast<BelowFn>(addr::SC4Random_Below)(At<void*>(self, field::kRandom), 100));
        void* const holder = Virtual<void*>(lot, 0xC); // lot slot 3
        reinterpret_cast<SetFn>(addr::SC4_SetPropertyUint8)(holder, step::kLotNoise, noise);
    }
    uint32_t& bucket = At<uint32_t>(self, step::kBucket);
    if (++bucket == At<uint32_t>(self, step::kBuckets))
        bucket = 0;
}

// The phases 16-18 work through a count in parts of at least a twentieth
// (16) or an eighteenth (17, 18).
void StartParts(void* self, std::size_t count, uint32_t divisor)
{
    const uint32_t total = At<uint32_t>(self, count);
    At<uint32_t>(self, step::kTotal) = total;
    At<uint32_t>(self, step::kParts) = total / divisor < 1 ? 1 : total / divisor;
    At<uint32_t>(self, step::kPart) = 0;
    At<uint32_t>(self, step::kDone) = 0;
}

uint8_t __fastcall Simulate(void* self, void*)
{
    using StartFn = void(__thiscall*)(Deadline* deadline, uint32_t budget);
    using SubStepFn = uint8_t(__thiscall*)(void* self, Deadline* deadline);
    static bool logged = false;
    FirstCall("Simulate", logged);
    Deadline deadline{};
    reinterpret_cast<StartFn>(addr::SC4Deadline_Start)(&deadline, 4);
    void* const service = Global<void*>(addr::SC4_SimulatorService);
    uint32_t day = 0;
    uint32_t* const none = nullptr;
    Virtual<void>(service, 0x18, none, none, &day, none, none); // GetSimDate(year, month, day, ...)
    if (At<uint32_t>(self, step::kLastDay) != day) {
        At<uint32_t>(self, step::kLastDay) = day;
        if (day == 1)
            BookIncome(self);
        const uint32_t number = Virtual<uint32_t>(service, 0x20); // GetSimDateNumber
        const uint32_t every = At<uint32_t>(self, step::kDaysPerCycle) / At<uint32_t>(self, step::kBuckets);
        if (number % every == 0)
            StirBucket(self);
    }
    while (!Expired(deadline)) {
        if (!reinterpret_cast<SubStepFn>(addr::cSC4TrafficSimulator_SubStep)(self, &deadline)) {
            const int32_t phase = At<int32_t>(self, field::kPhase);
            if (phase < 16 || phase > 18)
                return 0;
            const uint32_t part = At<uint32_t>(self, step::kPart);
            const uint32_t parts = At<uint32_t>(self, step::kParts);
            if (part >= parts)
                return 0;
            if (At<uint32_t>(self, step::kDone) < At<uint32_t>(self, step::kTotal) * (part + 1) / parts)
                return 0;
            At<uint32_t>(self, step::kPart) = part + 1;
            return 1;
        }
        const int32_t phase = At<int32_t>(self, field::kPhase);
        if (phase == 16) {
            StartParts(self, step::kCount16, 20);
            for (std::size_t offset : {0x940u, 0x944u, 0x948u, 0x954u, 0x958u, 0x95Cu, 0x960u, 0x964u, 0x968u, 0x96Cu})
                At<uint32_t>(self, offset) = 0;
        } else if (phase == 17) {
            StartParts(self, step::kCount17, 18);
            At<uint32_t>(self, 0x94C) = 0;
            return 1;
        } else if (phase == 18) {
            StartParts(self, step::kCount18, 18);
            At<uint32_t>(self, step::kCount16) = 0;
            return 1;
        }
        if (phase == 2 || phase == 11)
            return 1;
    }
    return 0;
}

using SimulateFn = uint8_t(__fastcall*)(void*, void*);
SimulateFn g_originalSimulate = nullptr;

bool InstallStep(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_Simulate);
    return game::IsBuild641() && entry && hook::Install(*entry, &Simulate, &g_originalSimulate);
}

const bool g_registeredStep = registry::Register({
    "simulators.traffic.step",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_Simulate,
    "Our traffic step: monthly income, lot buckets, the sub-step under a time budget (docs/traffic.md)",
    false,
    InstallStep,
});


// --- The sub-step (0x00720F00) --------------------------------------------------

// The phase functions, by phase (null: none; the phase is done at once).
const std::uintptr_t kPhases[20] = {
    addr::cSC4TrafficSimulator_Phase0,  0,
    addr::cSC4TrafficSimulator_Phase2,  addr::cSC4TrafficSimulator_Phase3,
    addr::cSC4TrafficSimulator_Phase4,  addr::cSC4TrafficSimulator_Phase5,
    addr::cSC4TrafficSimulator_Phase6,  addr::cSC4TrafficSimulator_Phase7,
    addr::cSC4TrafficSimulator_Phase8,  0,
    addr::cSC4TrafficSimulator_Phase10, addr::cSC4TrafficSimulator_Phase11,
    0,                                  addr::cSC4TrafficSimulator_Phase13,
    0,                                  addr::cSC4TrafficSimulator_Phase15,
    addr::cSC4TrafficSimulator_Phase16, addr::cSC4TrafficSimulator_Phase17,
    addr::cSC4TrafficSimulator_Phase18, addr::cSC4TrafficSimulator_Phase19,
};

void RestartCells(void* self)
{
    using RestartFn = void(__thiscall*)(void* iterator, uint32_t, uint32_t);
    reinterpret_cast<RestartFn>(addr::SC4CellIterator_Restart)(At<void*>(self, 0x818), 0, 0);
}

// Phase 2 starts: the per-type totals and the 12 records cleared.
void StartPhase2(void* self)
{
    RestartCells(self);
    for (std::size_t i = 0; i < 13; ++i) {
        for (std::size_t offset = 0x440 + 0x14 * i; offset < 0x454 + 0x14 * i; offset += 4)
            At<uint32_t>(self, offset) = 0;
        At<uint32_t>(self, 0x67C + 4 * i) = 0xBF800000; // -1.0f
        At<uint32_t>(self, 0x6B4 + 4 * i) = 0;
    }
    for (std::size_t offset = 0x544; offset <= 0x554; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x6B0) = 0xBF800000;
    At<uint32_t>(self, 0x6E8) = 0;
    for (std::size_t i = 0; i < 12; ++i) {
        const std::size_t record = 0x578 + 0x14 * i;
        At<uint32_t>(self, record + 0x4) = Global<uint32_t>(addr::cSC4TrafficSimulator_RecordDefault);
        At<uint32_t>(self, record + 0x10) = 0x3F8147AE; // 1.01f
        At<uint32_t>(self, record + 0x8) = 0;
        At<uint32_t>(self, record + 0xC) = 0;
        At<uint16_t>(self, record) = 0;
    }
    for (std::size_t offset = 0x558; offset <= 0x56C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint16_t>(self, 0x570) = 0;
    At<uint16_t>(self, 0x572) = 0;
    Virtual<uint32_t>(At<void*>(self, 0x3C), 0x6C, 0u);
}

// Phase 13 starts: the zones (+0x64) emptied and made again, one per
// +0x60, and the cell iterator at +0x818 reset.
void StartPhase13(void* self)
{
    using ThisFn = void(__thiscall*)(void* object);
    using ResizeFn = void(__thiscall*)(void* zones, uint32_t count, const void* element);
    void* const zones = static_cast<char*>(self) + 0x64;
    reinterpret_cast<ThisFn>(addr::SC4TrafficZones_Clear)(zones);
    uint32_t element[7] = {}; // +8 is not set by the constructor (stack contents in the game)
    reinterpret_cast<ThisFn>(addr::SC4TrafficZone_Construct)(element);
    reinterpret_cast<ResizeFn>(addr::SC4TrafficZones_Resize)(zones, At<uint16_t>(self, 0x60), element);
    char* const iterator = At<char*>(self, 0x818); // +0 state, +4 x, +8 z, +0x10 the rectangle
    At<uint32_t>(iterator, 0x4) = 0;
    At<uint32_t>(iterator, 0x8) = 0;
    iterator[0] = 0;
    if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
        At<int32_t>(iterator, 0x1C) < 0)
        iterator[0] = 2;
    reinterpret_cast<ThisFn>(addr::SC4TrafficZone_Destroy)(element);
}

// Phase 19 starts: this month's counts become last month's (the list at
// +0x104 and each node's two lists), and the cells of the occupants of
// type 9 are collected at +0x114.
void StartPhase19(void* self)
{
    using InsertFn = void(__thiscall*)(void* vector, uint32_t* at, const uint32_t* value, void* allocator,
                                       uint32_t count, uint8_t atEnd);
    using ListFn = void(__thiscall*)(void** list);
    char* const list = At<char*>(self, 0x104);
    At<uint32_t>(self, 0x43C) = 0;
    for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
        const uint16_t count = At<uint16_t>(node, 0x22);
        At<uint16_t>(node, 0x20) = count;
        At<uint16_t>(node, 0x22) = 0;
        At<uint32_t>(self, 0x43C) += count;
        char* const first = At<char*>(node, 0x18);
        At<uint8_t>(node, 0x26) = 1;
        for (char* link = At<char*>(first, 0); link != first; link = At<char*>(link, 0)) {
            At<uint16_t>(link, 0x20) = At<uint16_t>(link, 0x22);
            At<uint16_t>(link, 0x24) = At<uint16_t>(link, 0x26);
            At<uint16_t>(link, 0x22) = 0;
            At<uint16_t>(link, 0x26) = 0;
        }
        char* const second = At<char*>(node, 0x1C);
        for (char* link = At<char*>(second, 0); link != second; link = At<char*>(link, 0)) {
            At<uint16_t>(link, 0x2C) = At<uint16_t>(link, 0x2E);
            At<uint16_t>(link, 0x30) = At<uint16_t>(link, 0x32);
            At<uint16_t>(link, 0x2E) = 0;
            At<uint16_t>(link, 0x32) = 0;
        }
    }
    char* const cells = static_cast<char*>(self) + 0x114; // vector of (x, z): begin, end, capacity
    At<char*>(cells, 4) = At<char*>(cells, 0);
    void* occupants = Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 12u);
    static_cast<void**>(occupants)[0] = occupants;
    static_cast<void**>(occupants)[1] = occupants;
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x44, 9u, &occupants);
    for (char* node = At<char*>(occupants, 0); node != occupants; node = At<char*>(node, 0)) {
        void* const occupant = At<void*>(node, 8);
        uint32_t cell[2] = {};
        Virtual<void>(occupant, 0x18, &cell[0], &cell[1]);
        auto* const end = At<uint32_t*>(cells, 4);
        if (end == At<uint32_t*>(cells, 8)) {
            uint8_t allocator = 0;
            reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN)(cells, end, cell, &allocator, 1, 1);
        } else {
            end[0] = cell[0];
            end[1] = cell[1];
            At<uint32_t*>(cells, 4) = end + 2;
        }
    }
    At<char*>(self, 0x10C) = At<char*>(list, 0);
    At<uint32_t>(self, 0x124) = 0;
    reinterpret_cast<ListFn>(addr::SC4List_Destroy3)(&occupants);
}

uint8_t __fastcall SubStep(void* self, void*, void* deadline)
{
    using StopwatchFn = void(__thiscall*)(void* stopwatch, uint32_t mode);
    using StartFn = void(__thiscall*)(void* stopwatch);
    using RectFn = void(__thiscall*)(void* self, int32_t, int32_t, int32_t, int32_t);
    using PhaseFn = uint8_t(__thiscall*)(void* self, void* deadline);
    using ThisFn = void(__thiscall*)(void* object);
    using AssignFn = void(__thiscall*)(void* vector, uint32_t count, const uint32_t* value);
    using FillFn = void(__cdecl*)(void* to, uint32_t value, uint32_t count);
    static bool logged = false;
    FirstCall("SubStep", logged);
    uint32_t stopwatch[6] = {}; // started and never read, as the game's; it reads the clock
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Construct)(stopwatch, 4);
    reinterpret_cast<StartFn>(addr::SC4Stopwatch_Start)(stopwatch);
    if (At<uint8_t>(self, 0x81C)) {
        reinterpret_cast<RectFn>(addr::cSC4TrafficSimulator_UpdatePendingRect)(
            self, At<int32_t>(self, 0x820), At<int32_t>(self, 0x824), At<int32_t>(self, 0x828), At<int32_t>(self, 0x82C));
        At<uint8_t>(self, 0x81C) = 0;
    }
    const auto phase = At<uint32_t>(self, field::kPhase);
    static uint32_t seen = 0;
    if (phase < 32 && !(seen & (1u << phase))) {
        seen |= 1u << phase;
        log::Write("traffic: first phase %u", phase);
    }
    uint8_t done = 1;
    if (phase == 12)
        reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_Phase12)(self);
    else if (phase < 20 && kPhases[phase])
        done = reinterpret_cast<PhaseFn>(kPhases[phase])(self, deadline);
    if (!done)
        return 0;
    // The phase function may have changed the phase: the next comes from the current one.
    uint32_t next = reinterpret_cast<const uint32_t*>(addr::cSC4TrafficSimulator_NextPhase)[At<uint32_t>(self, field::kPhase)];
    if (next == 12 && !At<uint8_t>(self, 0x62))
        next = 13;
    At<uint32_t>(self, field::kPhase) = next;
    switch (next) {
    case 1:
    case 15: {
        void** const walk = At<void**>(self, 0x904);
        if (void** const list = static_cast<void**>(walk[1]))
            walk[0] = *static_cast<void**>(list[0]);
        At<uint16_t>(self, 0x90C) = 0;
        At<uint16_t>(self, 0x90E) = 0;
        At<uint8_t>(self, 0x838) = 1;
        break;
    }
    case 2: StartPhase2(self); break;
    case 4: {
        RestartCells(self);
        void* const source = Virtual<void*>(At<void*>(self, 0x3C), 0x60);
        void* const target = Virtual<void*>(At<void*>(self, 0x38), 0x60);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4Vector12_Assign)(target, source);
        break;
    }
    case 10:
        reinterpret_cast<FillFn>(addr::SC4_FillUint32)(static_cast<char*>(self) + 0x1B0, 0, 4);
        RestartCells(self);
        break;
    case 5:
    case 6: RestartCells(self); break;
    case 13: StartPhase13(self); break;
    case 14: {
        char* const iterator = At<char*>(self, 0x910); // back at its rectangle's corner
        At<uint32_t>(iterator, 0x4) = At<uint32_t>(iterator, 0x10);
        At<uint32_t>(iterator, 0x8) = At<uint32_t>(iterator, 0x14);
        iterator[0] = 0;
        break;
    }
    case 16:
    case 17:
    case 18:
        reinterpret_cast<ThisFn>(addr::SC4LotWalk_Restart)(At<void*>(self, 0x908));
        At<uint16_t>(self, 0x90C) = 0;
        At<uint16_t>(self, 0x90E) = 0;
        At<uint8_t>(self, 0x838) = 1;
        At<uint32_t>(self, 0x884) = 0;
        if (next == 18) {
            const uint32_t zero = 0;
            reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_Assign)(static_cast<char*>(self) + 0x430, 9, &zero);
        }
        break;
    case 19: StartPhase19(self); break;
    default: break;
    }
    return 1;
}

using SubStepFn = uint8_t(__fastcall*)(void*, void*, void*);
SubStepFn g_originalSubStep = nullptr;

bool InstallSubStep(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_SubStep);
    return game::IsBuild641() && entry && hook::Install(*entry, &SubStep, &g_originalSubStep);
}

const bool g_registeredSubStep = registry::Register({
    "simulators.traffic.substep",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_SubStep,
    "Our traffic sub-step: the phase functions in turn and the start of each phase (docs/traffic.md)",
    false,
    InstallSubStep,
});


// --- The month (0x00718AC0) ---------------------------------------------------

namespace month {
constexpr std::size_t kFunding1 = 0x40C;       // last month's funding of the departments
constexpr std::size_t kFunding2 = 0x410;
constexpr std::size_t kTrafficShare = 0x420;   // float, published x 25
constexpr std::size_t kByType = 0x424;         // uint32[9]
constexpr std::size_t kTotal = 0x43C;
} // namespace month

// 0x00717D30 (kind 1, fields at +0x7AC) and 0x00717E00 (kind 2, +0x7E0):
// a department's funding moves a level (+8) by the curve at +0xD20, clamped
// to [0, 1]; that share of a count (+0) is the active count (+4), and a
// change goes through 0x00715FB0 with the lists at +0xC and +0x20.
void ApplyFunding(void* self, const float* funding, std::size_t base, uint32_t kind)
{
    using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0
    using ChangeFn = void(__thiscall*)(void* self, void* a, void* b, int32_t change, uint32_t kind);
    const auto x = static_cast<float>(static_cast<double>(*funding) * 100.0f);
    float& level = At<float>(self, base + 0x8);
    level = static_cast<float>(
        reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(static_cast<char*>(self) + 0xD20, x) + level);
    if (level < 0.0f)
        level = 0.0f;
    if (1.0f < level)
        level = 1.0f;
    const auto target =
        static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(At<uint32_t>(self, base)) * level));
    int32_t& active = At<int32_t>(self, base + 0x4);
    if (target == active)
        return;
    const int32_t change = target - active;
    if (kind == 2)
        active += change;
    reinterpret_cast<ChangeFn>(addr::cSC4TrafficSimulator_ChangeActive)(
        self, static_cast<char*>(self) + base + 0xC, static_cast<char*>(self) + base + 0x20, change, kind);
    if (kind == 1) {
        active += change;
        At<uint16_t>(self, 0x318) = static_cast<uint16_t>(active);
    }
}

void __fastcall ApplyFirstFunding(void* self, void*, const float* funding)
{
    static bool logged = false;
    FirstCall("ApplyFirstFunding", logged);
    ApplyFunding(self, funding, 0x7AC, 1);
}

void __fastcall ApplySecondFunding(void* self, void*, const float* funding)
{
    static bool logged = false;
    FirstCall("ApplySecondFunding", logged);
    ApplyFunding(self, funding, 0x7E0, 2);
}

// Posts a message of a type carrying the simulator (slots 12 and 33),
// through the message server at +0x18.
void PostSelfMessage(void* self, uint32_t type)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Virtual<uint32_t>(At<void*>(self, 0x18), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// 0x0070DBA0: the strike state (+0x7A4) from the funding of department 2.
// 0: under +0xB94 (with traffic at +0x778) a strike starts (1), else 2.
// 1: it lasts while underfunded, up to +0xB9C months, then 2. 2: under
// +0xB98 a draw below (+0xB98 - funding) x +0xBA0 makes it 0 again.
void __fastcall CheckStrike(void* self, void*)
{
    static bool logged = false;
    FirstCall("CheckStrike", logged);
    const float funding = *Virtual<const float*>(At<void*>(self, step::kDepartment2), 0x28);
    int32_t& state = At<int32_t>(self, 0x7A4);
    uint32_t& months = At<uint32_t>(self, 0x7A8);
    const bool traffic = At<int64_t>(self, 0x778) != 0;
    uint32_t type = 0;
    switch (state) {
    case 0:
        if (funding < At<float>(self, 0xB94) && traffic) {
            state = 1;
            type = 0x69247DC4;
            months = 0;
            At<uint8_t>(self, 0x414) = 1;
        } else {
            type = 0x69247DC3;
            state = 2;
        }
        break;
    case 1:
        if (funding < At<float>(self, 0xB94) && months < At<uint32_t>(self, 0xB9C) && traffic) {
            ++months;
            return;
        }
        type = 0x69247DC5;
        At<uint8_t>(self, 0x414) = 0;
        state = 2;
        break;
    case 2: {
        if (!(funding < At<float>(self, 0xB98)) || At<int64_t>(self, 0x778) <= 0)
            return;
        const auto chance =
            static_cast<float>((static_cast<double>(At<float>(self, 0xB98)) - funding) * At<float>(self, 0xBA0));
        At<float>(self, 0x418) = chance;
        using RandomFn = double(__thiscall*)(void* random); // ST0
        const double draw = reinterpret_cast<RandomFn>(addr::SC4Random_Unit)(At<void*>(self, field::kRandom));
        if (!(draw < chance))
            return;
        state = 0;
        type = 0x69247DC6;
        break;
    }
    default: return;
    }
    PostSelfMessage(self, type);
}

uint32_t MagicRound(double value);

// The cells a view bound covers, rounded the game's way: the lower bound
// is round(v / 16 - 0.5), one more when v / 16 is exactly that plus one;
// the upper round(v / 16 + 0.5), one less when v / 16 is exactly that less one.
int32_t ViewLower(float bound)
{
    const double cells = static_cast<double>(bound) * 0.0625f;
    const auto rounded = static_cast<int32_t>(MagicRound(cells - 0.5f));
    return cells == static_cast<double>(rounded + 1) ? rounded + 1 : rounded;
}

int32_t ViewUpper(float bound)
{
    const double cells = static_cast<double>(bound) * 0.0625f;
    const auto rounded = static_cast<int32_t>(MagicRound(0.5f + cells));
    return cells == static_cast<double>(rounded - 1) ? rounded - 1 : rounded;
}

// 0x007184E0: when the stopwatch at +0x148 passes +0x160 (then +0xD54 ms
// on), the cells of +0x13C in a shuffled order: the first in view whose
// chance (+8) beats a draw and that has an occupant gets the event (slot 48
// with +0xD50 x 1000, then message 0x0C05E4C3 with the cell).
void __fastcall MonthlyEvent(void* self, void*)
{
    using ElapsedFn = uint32_t(__thiscall*)(void* stopwatch);
    using RangeFn = double(__thiscall*)(void* random, double low, double high); // ST0
    static bool logged = false;
    FirstCall("MonthlyEvent", logged);
    char* const base = static_cast<char*>(self);
    const uint32_t elapsed = reinterpret_cast<ElapsedFn>(addr::GZStopwatch_ElapsedMilliseconds)(base + 0x148);
    if (elapsed <= At<uint32_t>(self, 0x160))
        return;
    At<uint32_t>(self, 0x160) = static_cast<uint32_t>(static_cast<int64_t>(At<float>(self, 0xD54))) + elapsed;
    const float* const view = reinterpret_cast<const float*>(Global<char*>(addr::SC4_CameraView) + 0x3EC);
    const int32_t x0 = ViewLower(view[0]);
    const int32_t x1 = ViewUpper(view[2]);
    const int32_t z0 = ViewLower(view[1]);
    const int32_t z1 = ViewUpper(view[3]);
    struct {
        uint8_t flags[3];
        uint8_t padding;
        uint32_t order[3]; // a vector
        uint32_t count;
        uint32_t zero[2];
        uint32_t one;
    } shuffled{{1, 1, 1}, 0, {}, static_cast<uint32_t>((At<char*>(self, 0x140) - At<char*>(self, 0x13C)) / 12), {}, 1};
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4ShuffledIndex_Init)(&shuffled, 1);
    uint32_t index = 0;
    using NextFn = uint8_t(__thiscall*)(void* iterator, uint32_t* index);
    while (reinterpret_cast<NextFn>(addr::SC4ShuffledIndex_Next)(&shuffled, &index)) {
        const double draw = reinterpret_cast<RangeFn>(addr::cRZRandom_Range)(At<void*>(self, field::kRandom), 0.0, 1.0);
        char* const cell = At<char*>(self, 0x13C) + 12 * index;
        if (!(draw < At<float>(cell, 0x8)))
            continue;
        const int32_t x = At<int32_t>(cell, 0);
        const int32_t z = At<int32_t>(cell, 4);
        if (x < x0 || z < z0 || x > x1 || z > z1)
            continue;
        void* const target = Virtual<void*>(Global<void*>(addr::SC4_City), 0xAC);
        void** const occupant = Virtual<void**>(At<void*>(self, 0x1C), 0x38, x, z);
        if (!occupant || !*occupant)
            continue;
        const auto strength = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(At<float>(self, 0xD50)) * 1000.0f));
        void* const holder = Virtual<void*>(*occupant, 0xC0); // slot 48: its property holder
        reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t, uint32_t)>(addr::SC4_ApplyTrafficEvent)(target, holder, 2u, strength);
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        if (!messages)
            break;
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
        void* const message = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        if (message)
            Virtual<uint32_t>(message, 0x4);
        Virtual<uint32_t>(message, 0x14, 0x0C05E4C3u);
        Virtual<uint32_t>(message, 0x2C, At<uint32_t>(cell, 0));
        Virtual<uint32_t>(message, 0x40, At<uint32_t>(cell, 4));
        Virtual<uint32_t>(messages, 0x10, message, 0u);
        Virtual<uint32_t>(message, 0x8);
        break;
    }
    if (shuffled.order[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(reinterpret_cast<void*>(shuffled.order[0]));
}

void __fastcall OnMonthPassed(void* self, void*)
{
    using FundingFn = void(__thiscall*)(void* self, const float* funding);
    using ThisFn = void(__thiscall*)(void* self);
    static bool logged = false;
    FirstCall("OnMonthPassed", logged);
    auto funding = [self](std::size_t department) {
        return Virtual<const float*>(At<void*>(self, department), 0x28);
    };
    reinterpret_cast<FundingFn>(addr::cSC4TrafficSimulator_ApplyFirstFunding)(self, funding(step::kDepartment1));
    reinterpret_cast<FundingFn>(addr::cSC4TrafficSimulator_ApplySecondFunding)(self, funding(step::kDepartment2));
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_CheckStrike)(self);
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_MonthlyEvent)(self);
    At<uint32_t>(self, month::kFunding1) = *reinterpret_cast<const uint32_t*>(funding(step::kDepartment1));
    At<uint32_t>(self, month::kFunding2) = *reinterpret_cast<const uint32_t*>(funding(step::kDepartment2));
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    // x87 products, truncated through __ftol2 (int64), the low half published.
    const auto stats = static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(At<float>(self, field::kTrafficStats)) * 100.0f));
    Virtual<uint32_t>(figures, 0x30, 0xEA5BA01Du, stats);
    const auto share = static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(At<float>(self, month::kTrafficShare)) * 25.0f));
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, 0x8A5D4116u, share);
    for (uint32_t i = 0; i < 9; ++i)
        Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, 0xABB8FAACu + i, At<uint32_t*>(self, month::kByType)[i]);
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, 0xEC0BDBF8u, At<uint32_t>(self, month::kTotal));
}

using MonthFn = void(__fastcall*)(void*, void*);
using FundingFn = void(__fastcall*)(void*, void*, const float*);
MonthFn g_originalMonth = nullptr;
FundingFn g_originalFirstFunding = nullptr;
FundingFn g_originalSecondFunding = nullptr;
MonthFn g_originalStrike = nullptr;
MonthFn g_originalEvent = nullptr;

bool InstallMonth(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_OnMonthPassed);
    const addr::Entry* first = addr_check::Find(addr::cSC4TrafficSimulator_ApplyFirstFunding);
    const addr::Entry* second = addr_check::Find(addr::cSC4TrafficSimulator_ApplySecondFunding);
    const addr::Entry* strike = addr_check::Find(addr::cSC4TrafficSimulator_CheckStrike);
    const addr::Entry* event = addr_check::Find(addr::cSC4TrafficSimulator_MonthlyEvent);
    return game::IsBuild641() && entry && first && second && strike && event &&
           hook::Install(*event, &MonthlyEvent, &g_originalEvent) &&
           hook::Install(*entry, &OnMonthPassed, &g_originalMonth) &&
           hook::Install(*first, &ApplyFirstFunding, &g_originalFirstFunding) &&
           hook::Install(*second, &ApplySecondFunding, &g_originalSecondFunding) &&
           hook::Install(*strike, &CheckStrike, &g_originalStrike);
}

const bool g_registeredMonth = registry::Register({
    "simulators.traffic.month",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_OnMonthPassed,
    "Our traffic month: funding effects, strike check, monthly event and last month's figures (docs/traffic.md)",
    false,
    InstallMonth,
});


// --- DoMessage (+0x4 slot 3, 0x007241B0) ----------------------------------------

// A building's insertion or removal (0x99EF1142 / 0x99EF1143): its kinds
// (slot 22) choose the handlers; one of the network kinds also updates the
// cells around it. Afterwards +0x920 is set (unless the message is not
// tagged 0xC772BF98).
void OnOccupant(void* self, void* message, uint32_t type)
{
    using OccupantFn = void(__thiscall*)(void* self, void* object, uint8_t inserted);
    using KindFn = void(__thiscall*)(void* self, void* occupant, uint8_t inserted, uint32_t kind);
    using EraseFn = uint32_t(__thiscall*)(void* set, const uint32_t* key);
    using CellsFn = void(__thiscall*)(void* self, int32_t x1, int32_t z1, int32_t x2, int32_t z2);
    if (Virtual<uint32_t>(message, 0x4C) != 0xC772BF98)
        return;
    void* const occupant = Virtual<void*>(message, 0x28);
    void* building = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &building)) {
        static bool loggedBuilding = false;
        FirstCall(type == 0x99EF1142 ? "building inserted" : "building removed", loggedBuilding);
        const uint8_t inserted = type == 0x99EF1142 ? 1 : 0;
        auto is = [building](uint32_t kind) { return Virtual<uint8_t>(building, 0x58, kind) != 0; };
        const auto onKind = reinterpret_cast<KindFn>(addr::cSC4TrafficSimulator_OnBuildingOfKind);
        if (is(6))
            onKind(self, occupant, inserted, 6);
        if (is(0))
            onKind(self, occupant, inserted, 0);
        for (uint32_t kind : {10u, 3u, 2u, 12u}) {
            if (is(kind)) {
                onKind(self, occupant, inserted, kind);
                break;
            }
        }
        if (is(1))
            reinterpret_cast<OccupantFn>(addr::cSC4TrafficSimulator_OnKind1Building)(self, building, inserted);
        if (is(7) && !Virtual<uint8_t>(building, 0x20, 0x80000000u))
            reinterpret_cast<OccupantFn>(addr::cSC4TrafficSimulator_OnKind7Building)(self, occupant, inserted);
        bool network = false;
        const auto* const kinds = reinterpret_cast<const uint8_t*>(addr::cSC4TrafficSimulator_NetworkKinds);
        for (uint32_t kind = 0; kind < 13; ++kind)
            if (kinds[kind] && is(kind))
                network = true;
        if (network) {
            static bool loggedNetwork = false;
            FirstCall("network building", loggedNetwork);
            void* object = nullptr;
            if (Virtual<uint8_t>(building, 0x0, 0x8A4BD52Bu, &object)) {
                reinterpret_cast<OccupantFn>(addr::cSC4TrafficSimulator_OnNetworkBuilding)(self, object, inserted);
                Virtual<uint32_t>(object, 0x8);
            }
            float position[3] = {};
            Virtual<uint32_t>(occupant, 0x20, position);
            int32_t x = 0;
            int32_t z = 0;
            void* const converter = At<void*>(self, 0x14);
            Virtual<void>(converter, 0x17C, position[0], position[2], &x, &z);
            if (type == 0x99EF1143) {
                void* const set = static_cast<char*>(self) + 0x178;
                const auto erase = reinterpret_cast<EraseFn>(addr::SC4Uint32HashSet_Erase);
                for (uint32_t i = 0; i < 9; ++i) {
                    uint32_t key = TypedCellKey(0, i, x, z);
                    erase(set, &key);
                    key = TypedCellKey(1, i, x, z);
                    erase(set, &key);
                }
            }
            reinterpret_cast<CellsFn>(addr::cSC4TrafficSimulator_OnCellsChanged)(self, x, z, x, z);
        }
        Virtual<uint32_t>(building, 0x8);
    }
    At<uint8_t>(self, 0x920) = 1;
}

uint8_t __fastcall DoMessage(void* interface4, void*, void* message)
{
    using SelfFn = void(__thiscall*)(void* self);
    using ThreeFn = void(__thiscall*)(void* self, uint32_t a, uint32_t b, uint32_t c);
    using ObjectFn = void(__thiscall*)(void* self, void* object);
    using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(interface4) - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10); // GetType
    auto three = [self, message](std::uintptr_t handler) {
        const uint32_t a = Virtual<uint32_t>(message, 0x24);
        const uint32_t b = Virtual<uint32_t>(message, 0x38);
        const uint32_t c = Virtual<uint32_t>(message, 0x4C);
        reinterpret_cast<ThreeFn>(handler)(self, a, b, c);
    };
    switch (type) {
    case 0x26D31EC1: reinterpret_cast<SelfFn>(addr::cSC4TrafficSimulator_OnCityInited)(self); return 1;
    case 0x08DAC3E7:
        if (Virtual<uint32_t>(message, 0x38) == 0xC921CEEB) {
            const float* const funding = Virtual<const float*>(At<void*>(self, step::kDepartment2), 0x28);
            const auto x = static_cast<float>(static_cast<double>(*funding) * 100.0f);
            At<float>(self, 0x720) = static_cast<float>(
                reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(static_cast<char*>(self) + 0xD10, x));
        }
        return 1;
    case 0x66956810: At<uint8_t>(self, 0x920) = 1; return 1;
    case 0x66956814: return reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4TrafficSimulator_Simulate)(self);
    case 0x66956816: reinterpret_cast<SelfFn>(addr::cSC4TrafficSimulator_OnMonthPassed)(self); return 1;
    case 0x8A0D15DA: three(addr::cSC4TrafficSimulator_OnMessage8A0D15DA); return 1;
    case 0x99EF1142:
    case 0x99EF1143: OnOccupant(self, message, type); return 1;
    case 0xC2C09554: reinterpret_cast<SelfFn>(addr::cSC4TrafficSimulator_LoadTravelStrategies)(self); return 1;
    case 0xC53D10AA:
        if (Virtual<uint32_t>(message, 0x24) == 0xE7E2C2DB && Virtual<uint32_t>(message, 0x38) == 0xC9133286)
            reinterpret_cast<SelfFn>(addr::cSC4TrafficSimulator_LoadTuning)(self);
        return 1;
    case 0xE8BDA2E7: {
        void* object = nullptr;
        void* const data = Virtual<void*>(message, 0x80);
        if (!Virtual<uint8_t>(data, 0x0, 0xE818FCEDu, &object))
            return 1;
        const uint32_t removedKind = Virtual<uint32_t>(message, 0x38);
        if (Virtual<uint32_t>(message, 0x24) == 3)
            reinterpret_cast<ObjectFn>(addr::cSC4TrafficSimulator_OnNetworkObjectAdded)(self, object);
        else if (removedKind == 3)
            reinterpret_cast<ObjectFn>(addr::cSC4TrafficSimulator_OnNetworkObjectRemoved)(self, object);
        Virtual<uint32_t>(object, 0x8);
        return 1;
    }
    case 0xEA0D163E: three(addr::cSC4TrafficSimulator_OnMessageEA0D163E); return 1;
    default: return 1;
    }
}

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4TrafficSimulator_vftable_4);
    if (vtable[3] != addr::cSC4TrafficSimulator_DoMessage) {
        log::Write("traffic messages: +0x4 slot 3 holds %08X, expected %08X; not installing",
                   static_cast<unsigned>(vtable[3]), static_cast<unsigned>(addr::cSC4TrafficSimulator_DoMessage));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(&vtable[3], sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    vtable[3] = reinterpret_cast<std::uintptr_t>(&DoMessage);
    VirtualProtect(&vtable[3], sizeof(*vtable), old, &old);
    return true;
}

const bool g_registeredMessages = registry::Register({
    "simulators.traffic.messages",
    "simulators/traffic",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_vftable_4,
    "Our traffic DoMessage: each message to its handler (docs/traffic.md)",
    false,
    InstallMessages,
});


// --- The phases (the sub-step's phase functions) ---------------------------------

namespace phase {
constexpr std::size_t kIncome2 = 0x790;        // double: this month's, to +0x780 in phase 8
constexpr std::size_t kIncome1 = 0x798;        // double: to +0x784
constexpr std::size_t kStartDay = 0x7A0;
constexpr std::size_t kCycle = 0x950;          // 0-3
constexpr std::size_t kTrips = 0x6EC;          // float: over +0x6F0 trips
constexpr std::size_t kTripCount = 0x6F0;
constexpr std::size_t kTripTime = 0x6F4;       // float: over +0x6F8 trips
constexpr std::size_t kTimedTrips = 0x6F8;
constexpr std::size_t kCommute = 0x41C;        // uint8
constexpr std::size_t kCosts = 0x70C;          // hash table: node +0xC float
constexpr std::size_t kCostScale = 0x97C;      // float
} // namespace phase

// Phase 0 (0x00717C90): the month's counts cleared.
uint8_t __fastcall Phase0(void* self, void*, void*)
{
    using ThisFn = void(__thiscall*)(void* self);
    static bool logged = false;
    FirstCall("Phase0", logged);
    const uint32_t day = Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x20); // GetSimDateNumber
    At<double>(self, phase::kIncome2) = 0.0;
    At<uint32_t>(self, phase::kStartDay) = day;
    At<double>(self, phase::kIncome1) = 0.0;
    for (std::size_t offset = 0x1C0; offset <= 0x1E0; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, phase::kCycle) = (At<uint32_t>(self, phase::kCycle) + 1) & 3;
    for (std::size_t offset : {phase::kTripCount, phase::kTrips, phase::kTimedTrips, phase::kTripTime})
        At<uint32_t>(self, offset) = 0;
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_StartTripCounts)(self);
    return 1;
}

// Phase 3 (0x00713750): every cost in the hash table at +0x70C scaled by
// +0x97C.
uint8_t __fastcall Phase3(void* self, void*, void*)
{
    static bool logged = false;
    FirstCall("Phase3", logged);
    char* const table = static_cast<char*>(self) + phase::kCosts;
    char** const buckets = At<char**>(table, 4);
    const auto count = static_cast<uint32_t>(At<char**>(table, 8) - buckets);
    for (uint32_t bucket = 0; bucket < count; ++bucket) {
        for (char* node = buckets[bucket]; node; node = At<char*>(node, 0)) {
            float& cost = At<float>(node, 0xC);
            cost = static_cast<float>(static_cast<double>(At<float>(self, phase::kCostScale)) * cost);
        }
    }
    return 1;
}

// A time-sliced loop's deadline (as water's): the parent's end, checked
// every 0x400 steps of a step size kept in a global by 0x0068B960.
struct SlicedDeadline {
    uint32_t end[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
};

// A float rounded to an integer the game's way: added to 1.5 x 2^23 in
// x87 precision, stored as a float, and the bits read back.
uint32_t MagicRound(double value)
{
    const auto rounded = static_cast<float>(value + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof(bits));
    return bits + 0xB4C00000;
}

// Starts a sliced deadline from the parent's end and a step kept in a
// global (set to 0x400 once, flag bit 0 at global + 8).
void StartSliced(SlicedDeadline& deadline, const void* parent, std::uintptr_t stepGlobal)
{
    uint32_t& flags = Global<uint32_t>(stepGlobal + 8);
    if (!(flags & 1)) {
        flags |= 1;
        Global<uint32_t>(stepGlobal) = 0x400;
        Global<uint32_t>(stepGlobal + 4) = 0;
    }
    deadline = {};
    deadline.end[0] = static_cast<const uint32_t*>(parent)[0];
    deadline.end[1] = static_cast<const uint32_t*>(parent)[1];
    deadline.step = Global<uint32_t>(stepGlobal);
    deadline.stepGlobal = &Global<uint32_t>(stepGlobal);
}

// One step of a sliced deadline: true when it is spent (then ended).
bool SlicedExpired(SlicedDeadline& deadline)
{
    using ExpiredFn = uint8_t(__thiscall*)(SlicedDeadline* deadline);
    using EndFn = void(__thiscall*)(SlicedDeadline* deadline);
    deadline.count += deadline.step;
    if ((deadline.count & 0x3FF) || !reinterpret_cast<ExpiredFn>(addr::SC4Deadline_Expired)(&deadline))
        return false;
    deadline.timedOut = 1;
    reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
    return true;
}

void EndSliced(SlicedDeadline& deadline)
{
    reinterpret_cast<void(__thiscall*)(SlicedDeadline*)>(addr::SC4Deadline_End)(&deadline);
}

// The cell iterator at +0x818: the next cell of its rectangle (state +0:
// 0 new, 1 walking, 2 done; x +4, z +8, step +0xC, first x +0x10, last x
// +0x18, last z +0x1C). False when done.
bool NextCell(char* iterator)
{
    if (iterator[0] == 2)
        return false;
    if (iterator[0] == 0) {
        iterator[0] = 1;
        return true;
    }
    At<int32_t>(iterator, 0x4) += At<int32_t>(iterator, 0xC);
    if (At<int32_t>(iterator, 0x4) <= At<int32_t>(iterator, 0x18))
        return true;
    At<int32_t>(iterator, 0x4) = At<int32_t>(iterator, 0x10);
    At<int32_t>(iterator, 0x8) += At<int32_t>(iterator, 0xC);
    if (At<int32_t>(iterator, 0x8) <= At<int32_t>(iterator, 0x1C))
        return true;
    iterator[0] = 2;
    return false;
}

// Phase 4 (0x0070F460): for each cell, from the float grid at +0x34 the
// byte grid at +0x30 (x 0.1, rounded, at most 255) and from the travel
// time curve at the grid at +0x174 the byte grid at +0x48 (1 up to 1.0,
// then (t - 1) x +0xC58 rounded, 1 to 255; both 0 where +0x34 is 0).
// Posts 0x69247DC7 when done.
uint8_t __fastcall Phase4(void* self, void*, void* parent)
{
    using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0
    static bool logged = false;
    FirstCall("Phase4", logged);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase4Step);
    void* const bytesA = Virtual<void*>(At<void*>(self, 0x30), 0x60);
    void* const bytesB = Virtual<void*>(At<void*>(self, 0x48), 0x60);
    void* const floats = Virtual<void*>(At<void*>(self, 0x34), 0x60);
    void* const times = Virtual<void*>(At<void*>(self, 0x174), 0x60);
    for (;;) {
        if (SlicedExpired(deadline))
            return 0;
        char* const iterator = At<char*>(self, 0x818);
        if (!NextCell(iterator)) {
            PostSelfMessage(self, 0x69247DC7);
            EndSliced(deadline);
            return 1;
        }
        const int32_t x = At<int32_t>(iterator, 0x4);
        const int32_t z = At<int32_t>(iterator, 0x8);
        const float value = GridCell<float>(floats, x, z);
        if (value == 0.0f) {
            GridCell<uint8_t>(bytesA, x, z) = 0;
            GridCell<uint8_t>(bytesB, x, z) = 0;
            continue;
        }
        const uint32_t a = MagicRound(static_cast<double>(value) * 0.1f);
        GridCell<uint8_t>(bytesA, x, z) = static_cast<uint8_t>(a > 0xFF ? 0xFF : a);
        const double time = reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(
            reinterpret_cast<void*>(addr::SC4Traffic_TravelTimeCurve), GridCell<float>(times, x, z));
        if (!(time > 1.0f)) {
            GridCell<uint8_t>(bytesB, x, z) = 1;
            continue;
        }
        const uint32_t b = MagicRound((time - 1.0f) * At<float>(self, 0xC58));
        GridCell<uint8_t>(bytesB, x, z) = static_cast<uint8_t>(b > 0xFF ? 0xFF : (b == 0 ? 1 : b));
    }
}

// Phase 10 (0x0070F6E0): for each cell, bits 0-2 of the bordered byte
// grid at +0x4C (rows x + 1, z + 1) cleared, then a bit per trip kind 0-3
// the lot there can make (slot 20 on the lot's holder): for a lot found
// with flag 1 also counted at +0x1B0, else one found with flag 0.
uint8_t __fastcall Phase10(void* self, void*, void* parent)
{
    static bool logged = false;
    FirstCall("Phase10", logged);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase10Step);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    for (;;) {
        if (SlicedExpired(deadline))
            return 0;
        char* const iterator = At<char*>(self, 0x818);
        if (!NextCell(iterator)) {
            EndSliced(deadline);
            return 1;
        }
        const int32_t x = At<int32_t>(iterator, 0x4);
        const int32_t z = At<int32_t>(iterator, 0x8);
        uint8_t* const bits = At<uint8_t**>(self, 0x4C)[3 * x + 3] + z + 1;
        *bits &= 0xF8;
        void* lot = Virtual<void*>(lots, 0x18, x, z, 1u); // GetLot
        const bool counted = lot != nullptr;
        if (!counted)
            lot = Virtual<void*>(lots, 0x18, x, z, 0u);
        if (!lot)
            continue;
        void* const holder = Virtual<void*>(lot, 0xC);
        for (uint32_t kind = 0; kind < 4; ++kind) {
            if (!Virtual<uint32_t>(self, 0x50, holder, kind)) // slot 20
                continue;
            *bits |= static_cast<uint8_t>(1u << kind);
            if (counted)
                ++At<uint32_t>(self, 0x1B0 + 4 * kind);
        }
    }
}

// Phase 5 (0x0070E4F0): for each cell's lot (flag 1), its trip values
// 4-7 move to 8-11 (through 0x0070DE70, as slot 19) and 4-7 become 0; the
// trips of kinds 0-2 it can make (slot 20) add to +0x1C0.
uint8_t __fastcall Phase5(void* self, void*, void* parent)
{
    static bool logged = false;
    FirstCall("Phase5", logged);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase5Step);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    for (;;) {
        if (SlicedExpired(deadline))
            return 0;
        char* const iterator = At<char*>(self, 0x818);
        if (!NextCell(iterator)) {
            EndSliced(deadline);
            return 1;
        }
        void* const lot = Virtual<void*>(lots, 0x18, At<int32_t>(iterator, 0x4), At<int32_t>(iterator, 0x8), 1u);
        if (!lot)
            continue;
        void* const holder = Virtual<void*>(lot, 0xC);
        if (!holder)
            continue;
        for (uint32_t kind = 0; kind < 4; ++kind) {
            const uint32_t value = TripProperty(holder, 4 + kind, 4 + kind);
            SetMaxTripCapacity(nullptr, nullptr, holder, 8 + kind, value);
            SetMaxTripCapacity(nullptr, nullptr, holder, 4 + kind, 0);
        }
        for (uint32_t kind = 0; kind < 3; ++kind)
            At<uint32_t>(self, 0x1C0 + 4 * kind) += Virtual<uint32_t>(self, 0x50, holder, kind); // slot 20
    }
}

// The edge density node of a key (evening << 24 | type << 16 | x << 8 |
// z; TypedCellKey) in the hash table at +0x17C, or null.
char* DensityNode(void* self, uint32_t key)
{
    char** const buckets = At<char**>(self, 0x17C);
    const auto count = static_cast<uint32_t>(At<char**>(self, 0x180) - buckets);
    for (char* node = buckets[key % count]; node; node = At<char*>(node, 0))
        if (At<uint32_t>(node, 0x4) == key)
            return node;
    return nullptr;
}

// The 32 bytes of counters at +0x2A of a cell's density nodes, for all 9
// travel types and both commutes, cleared.
void ClearDensities(void* self, int32_t x, int32_t z)
{
    for (uint32_t type = 0; type < 9; ++type) {
        for (uint32_t evening = 0; evening < 2; ++evening) {
            if (char* const node = DensityNode(self, TypedCellKey(evening, type, x, z)))
                std::memset(node + 0x2A, 0, 0x20);
        }
    }
}

// Phase 6 (0x007110B0): the densities of each cell with an edge or a
// value in the grid at +0x704 cleared (the deadline counts only those
// cells), then of each entry's cell (+0xA, +0xB) of the list at +0x104.
uint8_t __fastcall Phase6(void* self, void*, void* parent)
{
    static bool logged = false;
    FirstCall("Phase6", logged);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase6Step);
    void* const grid = Virtual<void*>(At<void*>(self, 0x704), 0x60);
    for (;;) {
        char* const iterator = At<char*>(self, 0x818);
        if (!NextCell(iterator))
            break;
        const int32_t x = At<int32_t>(iterator, 0x4);
        const int32_t z = At<int32_t>(iterator, 0x8);
        if (!At<char***>(self, field::kEdges)[3 * x][z] && GridCell<uint32_t>(grid, x, z) == 0)
            continue;
        ClearDensities(self, x, z);
        if (SlicedExpired(deadline))
            return 0;
    }
    char* const list = At<char*>(self, 0x104);
    for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0))
        ClearDensities(self, static_cast<uint8_t>(node[0xA]), static_cast<uint8_t>(node[0xB]));
    EndSliced(deadline);
    return 1;
}

// Phase 2 (0x0071A0C0): the month's per-cell statistics, under a sliced
// deadline. For each cell: the congestion (+0x34 grid) and load (+0x174
// grid) restart at 0; every 4th cell in both directions gets the block
// value (0x00714AA0) x +0xD0C in the byte grid at +0x3C. For each network
// type 0-5 with an edge at the cell: the congestion is the highest type
// value (not types 2 and 3), the load the highest value x the edge's
// capacity; busy edges of types 1 and 2 add an effect (0x00716950); the
// per-type sums, counts and maxima (+0x440 + 0x14 x type, or +0x544 for
// edges with bit 0 of +7) grow; the type value decays by +0x97C (type 1
// adds its background traffic, slot 11). A cell in the grid at +0x704
// rates its cost table entry, kept in the 12 records at +0x578. When the
// walk is done the sums become averages and are copied to last month's
// (0x25C lower).
uint8_t __fastcall Phase2(void* self, void*, void* parent)
{
    using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0
    using BlockFn = uint16_t(__thiscall*)(void* self, int32_t x, int32_t z);
    using EffectFn = void(__thiscall*)(void* self, int32_t x, int32_t z, float value);
    using AtFn = char*(__thiscall*)(void* table, const uint32_t* key);
    using ThisFn = void(__thiscall*)(void* self);
    using RecordsFn = void(__thiscall*)(void* records, void* vector);
    static bool logged = false;
    FirstCall("Phase2", logged);
    const auto evaluate = [self](std::size_t curve, float x) {
        return reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(static_cast<char*>(self) + curve, x);
    };
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase2Step);
    Virtual<void*>(At<void*>(self, 0x30), 0x60);
    void* const congestion = Virtual<void*>(At<void*>(self, 0x34), 0x60);
    void* const blocks = Virtual<void*>(At<void*>(self, 0x3C), 0x60);
    void* const load = Virtual<void*>(At<void*>(self, 0x174), 0x60);
    void* byType[6];
    for (std::size_t type = 0; type < 6; ++type)
        byType[type] = Virtual<void*>(At<void**>(self, 0x164)[type], 0x60);
    void* const costCells = Virtual<void*>(At<void*>(self, 0x704), 0x60);
    Virtual<uint32_t>(At<void*>(self, 0x3C), 0x30);
    for (;;) {
        if (SlicedExpired(deadline))
            return 0;
        char* const iterator = At<char*>(self, 0x818);
        if (!NextCell(iterator))
            break;
        const int32_t x = At<int32_t>(iterator, 0x4);
        const int32_t z = At<int32_t>(iterator, 0x8);
        float& top = GridCell<float>(congestion, x, z);
        float& busiest = GridCell<float>(load, x, z);
        top = 0.0f;
        busiest = 0.0f;
        if (!(x & 3) && !(z & 3)) {
            const uint16_t value = reinterpret_cast<BlockFn>(addr::cSC4TrafficSimulator_BlockTraffic)(self, x >> 2, z >> 2);
            const auto rounded = static_cast<int32_t>(MagicRound(static_cast<double>(value) * At<float>(self, 0xD0C)));
            GridCell<uint8_t>(blocks, x >> 2, z >> 2) = static_cast<uint8_t>(rounded > 0xFF ? 0xFF : rounded);
        }
        bool changed = false;
        for (uint32_t type = 0; type < 6; ++type) {
            char* const edge = FindEdge(self, type, x, z);
            if (!edge)
                continue;
            float& cell = GridCell<float>(byType[type], x, z);
            if (type != 3 && type != 2 && cell > top)
                top = cell;
            const double product = static_cast<double>(cell) * At<float>(edge, 0);
            const auto value = static_cast<float>(product);
            if (product > busiest)
                busiest = value;
            if ((type == 1 || type == 2) && static_cast<uint8_t>(edge[4]) > 3) {
                const double first = evaluate(0xD30, value);
                const auto firstValue = static_cast<float>(first);
                if (first > 0.0f) {
                    const auto inverse = static_cast<float>(1.0f / static_cast<double>(At<float>(edge, 0)));
                    const double effect = evaluate(0xD40, inverse) * firstValue;
                    if (effect > 0.0f)
                        reinterpret_cast<EffectFn>(addr::cSC4TrafficSimulator_AddCongestionEffect)(
                            self, x, z, static_cast<float>(effect));
                }
            }
            const uint32_t kind = static_cast<uint8_t>(edge[5]);
            if (!(edge[7] & 1)) {
                char* const stats = static_cast<char*>(self) + 0x440 + 0x14 * kind;
                ++At<uint32_t>(self, 0x6B4 + 4 * kind);
                At<float>(stats, 0) = static_cast<float>(static_cast<double>(value) + At<float>(stats, 0));
                float& highest = At<float>(self, 0x67C + 4 * kind);
                if (highest < value) {
                    highest = value;
                    At<int32_t>(stats, 0xC) = z;
                    At<int32_t>(stats, 0x8) = x;
                    At<float>(stats, 0x10) = value;
                }
                if (value > 1.0f)
                    ++At<uint32_t>(stats, 0x4);
                ++At<uint16_t>(self, 0x558 + 2 * kind);
            } else {
                At<float>(self, 0x544) = static_cast<float>(static_cast<double>(value) + At<float>(self, 0x544));
                ++At<uint32_t>(self, 0x6E8);
                if (At<float>(self, 0x6B0) < value) {
                    At<float>(self, 0x6B0) = value;
                    At<int32_t>(self, 0x54C) = x;
                    At<int32_t>(self, 0x550) = z;
                    At<float>(self, 0x554) = value;
                }
                if (value > 1.0f)
                    ++At<uint32_t>(self, 0x548);
                ++At<uint16_t>(self, 0x572);
            }
            cell = static_cast<float>(static_cast<double>(At<float>(self, 0x97C)) * cell);
            if (type == 1) {
                const double background = Virtual<double>(self, 0x2C, x, z); // slot 11, in ST0
                cell = static_cast<float>(background + cell);
            }
            changed = true;
        }
        uint32_t key = GridCell<uint32_t>(costCells, x, z);
        if (key) {
            char* const node = reinterpret_cast<AtFn>(addr::SC4CostTable_At)(static_cast<char*>(self) + 0x70C, &key);
            At<uint32_t>(node, 0x14) = At<uint32_t>(node, 0x4);
            const double rate = static_cast<double>(At<float>(node, 0x4)) / At<float>(node, 0xC);
            const auto rateValue = static_cast<float>(rate);
            if (rate > busiest)
                busiest = rateValue;
            const float capped = 1.0f < rateValue ? 1.0f : rateValue;
            At<float>(node, 0x18) = capped;
            for (uint32_t record = 0; record < 12; ++record) {
                if (!(static_cast<uint8_t>(node[0x10]) & static_cast<uint8_t>(1u << record)))
                    continue;
                char* const entry = static_cast<char*>(self) + 0x578 + 0x14 * record;
                At<float>(entry, 0x4) = static_cast<float>(static_cast<double>(capped) + At<float>(entry, 0x4));
                ++At<uint16_t>(entry, 0);
                if (capped < At<float>(entry, 0x10)) {
                    At<float>(entry, 0x10) = capped;
                    At<int32_t>(entry, 0x8) = x;
                    At<int32_t>(entry, 0xC) = z;
                }
            }
        } else if (!changed) {
            continue;
        }
        if (top < 1.0f)
            top = 1.0f;
    }
    // Averages, then this month's figures copied to last month's.
    for (std::size_t kind = 0; kind < 13; ++kind) {
        char* const stats = static_cast<char*>(self) + 0x440 + 0x14 * kind;
        if (const uint32_t count = At<uint32_t>(self, 0x6B4 + 4 * kind))
            At<float>(stats, 0) = static_cast<float>(At<float>(stats, 0) / static_cast<double>(count));
        std::memcpy(stats - 0x25C, stats, 0x14);
    }
    if (const uint32_t count = At<uint32_t>(self, 0x6E8))
        At<float>(self, 0x544) = static_cast<float>(At<float>(self, 0x544) / static_cast<double>(count));
    std::memcpy(static_cast<char*>(self) + 0x2E8, static_cast<char*>(self) + 0x544, 0x14);
    for (std::size_t record = 0; record < 12; ++record) {
        char* const entry = static_cast<char*>(self) + 0x578 + 0x14 * record;
        if (const uint16_t count = At<uint16_t>(entry, 0))
            At<float>(entry, 0x4) = static_cast<float>(At<float>(entry, 0x4) / static_cast<double>(count));
        std::memcpy(entry - 0x25C, entry, 0x14);
    }
    for (std::size_t kind = 0; kind < 13; ++kind) {
        const uint16_t count = At<uint16_t>(self, 0x558 + 2 * kind);
        At<uint16_t>(self, 0x558 + 2 * kind - 0x25C) = count;
        At<uint32_t>(self, 0x740 + 4 * kind) = count;
    }
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_FinishStats1)(self);
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_FinishStats2)(self);
    char* const vector = static_cast<char*>(self) + 0x130;
    reinterpret_cast<RecordsFn>(addr::SC4TrafficRecords_Update)(static_cast<char*>(self) + 0x13C, vector);
    At<char*>(vector, 4) = At<char*>(vector, 0);
    EndSliced(deadline);
    return 1;
}

// Phase 7 (0x007137D0): in each node of the hash table at +0x178, the 16
// counters at +0x2A move to +0x8 and their sum (at most 0xFFFF) goes to
// +0x28.
uint8_t __fastcall Phase7(void* self, void*, void*)
{
    static bool logged = false;
    FirstCall("Phase7", logged);
    char* const table = static_cast<char*>(self) + 0x178;
    char** const buckets = At<char**>(table, 4);
    const auto count = static_cast<uint32_t>(At<char**>(table, 8) - buckets);
    for (uint32_t bucket = 0; bucket < count; ++bucket) {
        for (char* node = buckets[bucket]; node; node = At<char*>(node, 0)) {
            uint32_t sum = 0;
            for (std::size_t i = 0; i < 16; ++i) {
                const uint16_t value = At<uint16_t>(node, 0x2A + 2 * i);
                At<uint16_t>(node, 0x8 + 2 * i) = value;
                sum += value;
            }
            At<uint16_t>(node, 0x28) = static_cast<uint16_t>(sum > 0xFFFF ? 0xFFFF : sum);
        }
    }
    return 1;
}

// Phase 8 (0x0070E7D0): the month's incomes as floats, the average trip
// (+0x420) and the commute byte (+0x41C: average trip time / trip scale x
// 255, truncated through __ftol2).
uint8_t __fastcall Phase8(void* self, void*, void*)
{
    static bool logged = false;
    FirstCall("Phase8", logged);
    Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x20); // read and unused, as the game's
    At<float>(self, step::kIncome2) = static_cast<float>(At<double>(self, phase::kIncome2));
    At<float>(self, step::kIncome1) = static_cast<float>(At<double>(self, phase::kIncome1));
    At<float>(self, month::kTrafficShare) =
        static_cast<float>(At<float>(self, phase::kTrips) / static_cast<double>(At<uint32_t>(self, phase::kTripCount)));
    const uint32_t timed = At<uint32_t>(self, phase::kTimedTrips);
    double commute = 0.0;
    if (timed)
        commute = At<float>(self, phase::kTripTime) / static_cast<double>(timed) / At<float>(self, field::kTripScale) * 255.0f;
    At<uint8_t>(self, phase::kCommute) = static_cast<uint8_t>(static_cast<int64_t>(commute));
    return 1;
}

// Phase 11 (0x00714BE0): for every lot (month-end service slot 16), its
// three uint32-array properties 0xAA9EA81A-C copied to the IDs 3 on (when
// present) and set to nine zeros, and its trip values 4-7 moved to 8-11.
uint8_t __fastcall Phase11(void*, void*, void*)
{
    using InsertFn = void(__thiscall*)(uint32_t** vector, uint32_t* where, uint32_t count, const uint32_t* value);
    using ReadFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t** vector);
    using WriteFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t** vector);
    using ListFn = void(__thiscall*)(void** list);
    using FreeFn = void(__cdecl*)(void* memory);
    static bool logged = false;
    FirstCall("Phase11", logged);
    const uint32_t zero = 0;
    uint32_t* values[3] = {};   // vector: begin, end, capacity
    uint32_t* zeros[3] = {};
    const auto insert = reinterpret_cast<InsertFn>(addr::SC4Uint32Vector_InsertN);
    insert(values, values[0], 9, &zero);
    const auto size = static_cast<uint32_t>(zeros[1] - zeros[0]);
    if (size > 9)
        zeros[1] = zeros[0] + 9;
    else
        insert(zeros, zeros[1], 9 - size, &zero);
    void* lots = Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 12u);
    static_cast<void**>(lots)[0] = lots;
    static_cast<void**>(lots)[1] = lots;
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x40, &lots);
    for (char* node = At<char*>(lots, 0); node != lots; node = At<char*>(node, 0)) {
        void* const holder = Virtual<void*>(At<void*>(node, 8), 0xC);
        for (uint32_t property = 0xAA9EA81A; property < 0xAA9EA81D; ++property) {
            if (reinterpret_cast<ReadFn>(addr::SC4_GetPropertyUint32Vector)(holder, property, values))
                reinterpret_cast<WriteFn>(addr::SC4_SetPropertyUint32Vector)(holder, property + 3, values);
            reinterpret_cast<WriteFn>(addr::SC4_SetPropertyUint32Vector)(holder, property, zeros);
        }
        for (uint32_t kind = 0; kind < 4; ++kind) {
            const uint32_t value = TripProperty(holder, 4 + kind, 4 + kind);
            SetMaxTripCapacity(nullptr, nullptr, holder, 8 + kind, value);
            SetMaxTripCapacity(nullptr, nullptr, holder, 4 + kind, 0);
        }
    }
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing)(&lots);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, lots);
    if (zeros[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(zeros[0]);
    if (values[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(values[0]);
    return 1;
}

// Phase 12 (0x007183B0, no time budget): 0x0070FCE0, then for each type-1
// edge a call of 0x00717EC0 per direction whose word (+0xC + 2 x
// direction) is 0: directions 0 and 1 walking the cells forward, 2 and 3
// backward. The city size (+0x194, +0x198) is read again as the game does.
void __fastcall Phase12(void* self, void*)
{
    using ThisFn = void(__thiscall*)(void* self);
    using SpreadFn = void(__thiscall*)(void* self, int32_t x, int32_t z, uint32_t type, uint32_t direction);
    static bool logged = false;
    FirstCall("Phase12", logged);
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_ClearRoadEdges)(self);
    const auto spread = reinterpret_cast<SpreadFn>(addr::cSC4TrafficSimulator_SpreadFromEdge);
    auto visit = [self, spread](int32_t x, int32_t z, uint32_t first) {
        char* const edge = FindEdge(self, 1, x, z);
        if (!edge)
            return;
        if (At<uint16_t>(edge, 0xC + 2 * first) == 0)
            spread(self, x, z, 1, first);
        if (At<uint16_t>(edge, 0xE + 2 * first) == 0)
            spread(self, x, z, 1, first + 1);
    };
    for (int32_t x = 0; x < At<int32_t>(self, 0x194); ++x)
        for (int32_t z = 0; z < At<int32_t>(self, 0x198); ++z)
            visit(x, z, 0);
    for (int32_t x = At<int32_t>(self, 0x194) - 1; x > -1; --x)
        for (int32_t z = At<int32_t>(self, 0x198) - 1; z > -1; --z)
            visit(x, z, 2);
}

// Phase 13 (0x0071DC50): the walk of 0x00716180 over the cell iterator at
// +0x818; when done, the zones are finished (0x0071CD90).
uint8_t __fastcall Phase13(void* self, void*, void* deadline)
{
    using WalkFn = uint8_t(__thiscall*)(void* self, void* deadline, void* iterator);
    using FinishFn = void(__thiscall*)(void* target, void* zones);
    static bool logged = false;
    FirstCall("Phase13", logged);
    if (!reinterpret_cast<WalkFn>(addr::cSC4TrafficSimulator_WalkCells13)(self, deadline, At<void*>(self, 0x818)))
        return 0;
    reinterpret_cast<FinishFn>(addr::SC4TrafficZones_Assign)(static_cast<char*>(self) + 0x70, static_cast<char*>(self) + 0x64);
    return 1;
}

// Phase 15 (0x0070FEF0): each occupant of the walk at +0x904 loses flag
// 0x80 and gives its cell to +0x90C, +0x90E; one of type 8 or 9 (slot 31)
// that is not marked (slot 28) paints its rectangle (slot 14) in the byte
// grid at +0x44 with the value at a linked occupant's cell (slot 59), or
// else at the nearest marked occupant of its type found walking out in
// the four directions (0 when none).
uint8_t __fastcall Phase15(void* self, void*, void* parent)
{
    static const int32_t kStepX[4] = {-1, 0, 1, 0}; // 0x00AB325C
    static const int32_t kStepZ[4] = {0, -1, 0, 1}; // 0x00AB326C
    static bool logged = false;
    FirstCall("Phase15", logged);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase15Step);
    void* const grid = Virtual<void*>(At<void*>(self, 0x44), 0x60);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    for (;;) {
        if (SlicedExpired(deadline))
            return 0;
        void** const walk = At<void**>(self, 0x904);
        void* const* const head = static_cast<void* const*>(walk[1]);
        if (!head || walk[0] == *head) {
            EndSliced(deadline);
            return 1;
        }
        void** const node = static_cast<void**>(walk[0]);
        walk[0] = node[0];
        void* const occupant = node[2];
        Virtual<void>(occupant, 0x34, Virtual<uint32_t>(occupant, 0x30) & 0xFFFFFF7Fu);
        int32_t cellX = 0;
        int32_t cellZ = 0;
        Virtual<void>(occupant, 0x54, &cellX, &cellZ);
        At<uint16_t>(self, 0x90E) = static_cast<uint16_t>(cellZ);
        At<uint16_t>(self, 0x90C) = static_cast<uint16_t>(cellX);
        const uint32_t type = Virtual<uint32_t>(occupant, 0x7C);
        if ((type != 9 && type != 8) || Virtual<uint8_t>(occupant, 0x70))
            continue;
        uint8_t value = 0;
        if (void* const linked = Virtual<void*>(occupant, 0xEC)) {
            int32_t x = 0;
            int32_t z = 0;
            Virtual<void>(linked, 0x54, &x, &z);
            value = GridCell<uint8_t>(grid, x, z);
        } else {
            int32_t originX = 0;
            int32_t originZ = 0;
            Virtual<void>(occupant, 0x54, &originX, &originZ);
            uint8_t open = 0xF;
            uint8_t direction = 3;
            uint8_t distance = 0;
            while (open) {
                do {
                    if (++direction == 4) {
                        direction = 0;
                        ++distance;
                    }
                } while (!(open & (1u << direction)));
                const int32_t x = originX + kStepX[direction] * distance;
                const int32_t z = originZ + kStepZ[direction] * distance;
                void* other = nullptr;
                if (x >= At<int32_t>(self, 0x18C) && z >= At<int32_t>(self, 0x190) && x <= At<int32_t>(self, 0x194) &&
                    z <= At<int32_t>(self, 0x198))
                    other = Virtual<void*>(lots, 0x18, x, z, 0u);
                if (!other || Virtual<uint32_t>(other, 0x7C) != type) {
                    open = static_cast<uint8_t>(open & ~(1u << direction));
                    continue;
                }
                if (other == occupant || !Virtual<uint8_t>(other, 0x70))
                    continue;
                value = GridCell<uint8_t>(grid, x, z);
                break;
            }
        }
        int32_t rect[4] = {};
        Virtual<void>(occupant, 0x38, rect);
        for (int32_t x = rect[0]; x <= rect[2]; ++x)
            for (int32_t z = rect[1]; z <= rect[3]; ++z)
                GridCell<uint8_t>(grid, x, z) = value;
    }
}

// Phase 16 (0x0071E1B0): each lot of the walk at +0x908 (its cell to
// +0x83C, +0x840 and the words +0x90C, +0x90E) through 0x0071D920, until
// the deadline.
uint8_t __fastcall Phase16(void* self, void*, void* parent)
{
    using NextFn = uint8_t(__thiscall*)(void* walk, void** lot, uint32_t* x, uint32_t* z);
    using VisitFn = void(__thiscall*)(void* self, void* lot);
    using EndFn = void(__thiscall*)(SlicedDeadline* deadline);
    using ExpiredFn = uint8_t(__thiscall*)(SlicedDeadline* deadline);
    static bool logged = false;
    FirstCall("Phase16", logged);
    uint32_t& flags = Global<uint32_t>(addr::cSC4TrafficSimulator_Phase16Step + 8);
    if (!(flags & 1)) {
        flags |= 1;
        Global<uint32_t>(addr::cSC4TrafficSimulator_Phase16Step) = 0x400;
        Global<uint32_t>(addr::cSC4TrafficSimulator_Phase16Step + 4) = 0;
    }
    SlicedDeadline deadline{};
    deadline.end[0] = static_cast<uint32_t*>(parent)[0];
    deadline.end[1] = static_cast<uint32_t*>(parent)[1];
    deadline.step = Global<uint32_t>(addr::cSC4TrafficSimulator_Phase16Step);
    deadline.stepGlobal = &Global<uint32_t>(addr::cSC4TrafficSimulator_Phase16Step);
    void* lot = nullptr;   // the game's [esp+0x14]
    void* current = nullptr; // and its ESI
    for (;;) {
        deadline.count += deadline.step;
        if (!(deadline.count & 0x3FF) && reinterpret_cast<ExpiredFn>(addr::SC4Deadline_Expired)(&deadline)) {
            deadline.timedOut = 1;
            if (current)
                Virtual<uint32_t>(current, 0x8);
            reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
            return 0;
        }
        if (current) {
            lot = nullptr;
            Virtual<uint32_t>(current, 0x8);
        }
        uint32_t x = 0;
        uint32_t z = 0;
        if (!reinterpret_cast<NextFn>(addr::SC4LotWalk_Next)(At<void*>(self, 0x908), &lot, &x, &z)) {
            if (lot)
                Virtual<uint32_t>(lot, 0x8);
            reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
            return 1;
        }
        const int32_t cellX = static_cast<int16_t>(x);
        const int32_t cellZ = static_cast<int16_t>(z);
        current = lot;
        At<int32_t>(self, 0x83C) = cellX;
        At<uint16_t>(self, 0x90E) = static_cast<uint16_t>(cellZ);
        At<uint16_t>(self, 0x90C) = static_cast<uint16_t>(cellX);
        At<int32_t>(self, 0x840) = cellZ;
        Virtual<uint32_t>(current, 0x10);
        reinterpret_cast<VisitFn>(addr::cSC4TrafficSimulator_VisitLot16)(self, current);
    }
}

// Phase 17 (0x0071D020): trip routing. For each lot of the walk at +0x908
// (held at +0x84C) each of its trips waiting to be routed (+0x1F = 1) is
// checked (0x007136B0) and set up (0x0070EC40) once, then routed
// (0x0071C6C0: 1 found, the path finder's cost kept; 2 out of time, the
// phase stops; else 5); then trips to the same place are merged.
uint8_t __fastcall Phase17(void* self, void*, void* parent)
{
    using NextFn = uint8_t(__thiscall*)(void* walk, void** lot, uint32_t* x, uint32_t* z);
    using TripFn = uint8_t(__thiscall*)(void* self, char* trip);
    using SetUpFn = void(__thiscall*)(void* self, void* cell, char* trip);
    using RouteFn = int32_t(__thiscall*)(void* self, void* cell, char* trip);
    using RemainingFn = uint32_t(__thiscall*)(SlicedDeadline* deadline);
    using ExpiredFn = uint8_t(__thiscall*)(SlicedDeadline* deadline);
    using FreeFn = void(__cdecl*)(void* memory);
    static bool logged = false;
    FirstCall("Phase17", logged);
    char* const base = static_cast<char*>(self);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase17Step);
    const auto expired = [&deadline]() {
        deadline.count += deadline.step;
        return !(deadline.count & 0x3FF) && reinterpret_cast<ExpiredFn>(addr::SC4Deadline_Expired)(&deadline);
    };
    const auto release = [](void* object) {
        if (object)
            Virtual<uint32_t>(object, 0x8);
    };
    void* held = nullptr; // the lot from the walk ([esp+0x14])
    for (;;) {
        if (expired() || deadline.timedOut) {
            release(held);
            EndSliced(deadline);
            return 0;
        }
        if (At<uint8_t>(self, 0x838)) {
            if (void* const old = held) {
                held = nullptr;
                release(old);
            }
            uint32_t x = 0, z = 0;
            if (!reinterpret_cast<NextFn>(addr::SC4LotWalk_Next)(At<void*>(self, 0x908), &held, &x, &z)) {
                release(held);
                EndSliced(deadline);
                return 1;
            }
            const int32_t cellX = static_cast<int16_t>(x);
            const int32_t cellZ = static_cast<int16_t>(z);
            At<uint16_t>(self, 0x90C) = static_cast<uint16_t>(cellX);
            At<int32_t>(self, 0x83C) = cellX;
            At<int32_t>(self, 0x840) = cellZ;
            At<uint16_t>(self, 0x90E) = static_cast<uint16_t>(cellZ);
            void* const old = At<void*>(self, 0x84C);
            if (held != old) {
                if (held)
                    Virtual<uint32_t>(held, 0x4);
                At<void*>(self, 0x84C) = held;
                release(old);
            }
            At<uint8_t>(self, 0x838) = 0;
            At<uint8_t>(self, 0x881) = 0;
        }
        if (At<uint8_t>(self, 0x888)) {
            Virtual<uint32_t>(At<void*>(self, 0x830), 0x58);
            Virtual<uint32_t>(At<void*>(self, 0x834), 0x58);
            At<uint32_t>(self, 0x884) = 0;
            At<uint8_t>(self, 0x888) = 0;
        }
        void* const lot = At<void*>(self, 0x84C);
        if (lot)
            Virtual<uint32_t>(lot, 0x4);
        char* trip = At<char*>(*Virtual<char**>(lot, 0xC), 0); // slot 3: the lot's trips
        char* const sentinel = *Virtual<char**>(lot, 0xC);
        bool merge = true;
        if (trip != sentinel) {
            for (; trip != sentinel; trip = At<char*>(trip, 0)) {
                if (expired()) {
                    deadline.timedOut = 1;
                    merge = false;
                    break;
                }
                char* const value = trip + 8;
                if (value[0x1F] != 1)
                    continue;
                if (!At<uint32_t>(self, 0x884)) {
                    if (!reinterpret_cast<TripFn>(addr::cSC4TrafficSimulator_CanRouteTrip)(self, value)) {
                        value[0x1F] = 0;
                        ++At<uint32_t>(self, 0x944);
                        ++At<uint32_t>(self, 0x958);
                    } else {
                        reinterpret_cast<SetUpFn>(addr::cSC4TrafficSimulator_SetUpTrip)(self, base + 0x83C, value);
                    }
                }
                if (value[0x1F] != 1)
                    continue;
                At<uint32_t>(self, 0x88C) = reinterpret_cast<RemainingFn>(addr::SC4Deadline_Remaining)(&deadline);
                const int32_t result = reinterpret_cast<RouteFn>(addr::cSC4TrafficSimulator_RouteTrip)(self, base + 0x83C, value);
                if (result == 2) {
                    release(lot);
                    release(held);
                    EndSliced(deadline);
                    return 0;
                }
                ++At<uint32_t>(self, 0x914);
                if (result == 1) {
                    value[0x1F] = 3;
                    At<float>(value, 0x24) = static_cast<float>(Virtual<double>(At<void*>(self, 0x830), 0x64)); // ST0
                    ++At<uint32_t>(self, 0x94C);
                } else {
                    value[0x1F] = 5;
                }
            }
        }
        if (merge) {
            // Trips not to be routed (+0x1F = 0) to the same place (+0x24,
            // +0x25) are merged into the first, their counts (+0x20) added.
            for (char* first = At<char*>(*Virtual<char**>(lot, 0xC), 0); first != sentinel; first = At<char*>(first, 0)) {
                if (first[0x27])
                    continue;
                for (char* other = At<char*>(first, 0); other != sentinel;) {
                    if (first[0x27] == 3 || other[0x24] != first[0x24] || other[0x25] != first[0x25]) {
                        other = At<char*>(other, 0);
                        continue;
                    }
                    At<uint32_t>(first, 0x20) += At<uint32_t>(other, 0x20);
                    Virtual<void*>(lot, 0xC);
                    char* const next = At<char*>(other, 0);
                    char* const previous = At<char*>(other, 4);
                    At<char*>(previous, 0) = next;
                    At<char*>(next, 4) = previous;
                    if (void* const bytes = At<void*>(other, 0x14))
                        reinterpret_cast<FreeFn>(addr::SC4_Free)(bytes);
                    if (void* const bytes = At<void*>(other, 0x8))
                        reinterpret_cast<FreeFn>(addr::SC4_Free)(bytes);
                    reinterpret_cast<FreeFn>(addr::SC4_Free)(other);
                    other = next;
                }
            }
            At<uint8_t>(self, 0x838) = 1;
        }
        release(lot);
    }
}

// Phase 18 (0x0071D440): as phase 17 for the trips in state 0: each is set
// up, given the time left, routed, and split when the route carries fewer
// than its count (the rest a new trip in state 0, routed next). When the
// walk ends the two by-type vectors at +0x424 and +0x430 swap.
uint8_t __fastcall Phase18(void* self, void*, void* parent)
{
    using NextFn = uint8_t(__thiscall*)(void* walk, void** lot, uint32_t* x, uint32_t* z);
    using SetUpFn = void(__thiscall*)(void* self, void* cell, char* trip);
    using RouteFn = int32_t(__thiscall*)(void* self, void* cell, char* trip);
    using RemainingFn = uint32_t(__thiscall*)(SlicedDeadline* deadline);
    using ExpiredFn = uint8_t(__thiscall*)(SlicedDeadline* deadline);
    using CopyFn = void(__thiscall*)(void* to, const void* from);
    static bool logged = false;
    FirstCall("Phase18", logged);
    char* const base = static_cast<char*>(self);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase18Step);
    const auto expired = [&deadline]() {
        deadline.count += deadline.step;
        return !(deadline.count & 0x3FF) && reinterpret_cast<ExpiredFn>(addr::SC4Deadline_Expired)(&deadline);
    };
    const auto release = [](void* object) {
        if (object)
            Virtual<uint32_t>(object, 0x8);
    };
    void* held = nullptr;
    for (;;) {
        if (expired() || deadline.timedOut) {
            deadline.timedOut = 1;
            release(held);
            EndSliced(deadline);
            return 0;
        }
        if (At<uint8_t>(self, 0x838)) {
            if (void* const old = held) {
                held = nullptr;
                release(old);
            }
            uint32_t x = 0, z = 0;
            if (!reinterpret_cast<NextFn>(addr::SC4LotWalk_Next)(At<void*>(self, 0x908), &held, &x, &z)) {
                for (std::size_t offset = 0; offset < 0xC; offset += 4)
                    std::swap(At<uint32_t>(self, 0x424 + offset), At<uint32_t>(self, 0x430 + offset));
                release(held);
                EndSliced(deadline);
                return 1;
            }
            const int32_t cellX = static_cast<int16_t>(x);
            const int32_t cellZ = static_cast<int16_t>(z);
            At<uint16_t>(self, 0x90C) = static_cast<uint16_t>(cellX);
            At<int32_t>(self, 0x83C) = cellX;
            At<int32_t>(self, 0x840) = cellZ;
            At<uint16_t>(self, 0x90E) = static_cast<uint16_t>(cellZ);
            void* const old = At<void*>(self, 0x84C);
            if (held != old) {
                if (held)
                    Virtual<uint32_t>(held, 0x4);
                At<void*>(self, 0x84C) = held;
                release(old);
            }
            At<uint8_t>(self, 0x838) = 0;
            At<uint8_t>(self, 0x881) = 0;
        }
        if (At<uint8_t>(self, 0x888)) {
            Virtual<uint32_t>(At<void*>(self, 0x830), 0x58);
            Virtual<uint32_t>(At<void*>(self, 0x834), 0x58);
            At<uint32_t>(self, 0x884) = 0;
            At<uint8_t>(self, 0x888) = 0;
        }
        void* const lot = At<void*>(self, 0x84C);
        if (lot)
            Virtual<uint32_t>(lot, 0x4);
        char* trip = At<char*>(*Virtual<char**>(lot, 0xC), 0);
        char* const sentinel = *Virtual<char**>(lot, 0xC);
        bool done = true;
        for (; trip != sentinel; trip = At<char*>(trip, 0)) {
            if (expired()) {
                deadline.timedOut = 1;
                done = false;
                break;
            }
            char* const value = trip + 8;
            if (value[0x1F])
                continue;
            if (!At<uint32_t>(self, 0x884))
                reinterpret_cast<SetUpFn>(addr::cSC4TrafficSimulator_SetUpTrip)(self, base + 0x83C, value);
            At<uint32_t>(self, 0x88C) = reinterpret_cast<RemainingFn>(addr::SC4Deadline_Remaining)(&deadline);
            Virtual<uint32_t>(At<void*>(self, 0x830), 0x54, At<uint32_t>(self, 0x88C));
            Virtual<uint32_t>(At<void*>(self, 0x834), 0x54, At<uint32_t>(self, 0x88C));
            const int32_t result = reinterpret_cast<RouteFn>(addr::cSC4TrafficSimulator_RouteTrip)(self, base + 0x83C, value);
            if (result == 2) {
                release(lot);
                release(held);
                EndSliced(deadline);
                return 0;
            }
            ++At<uint32_t>(self, 0x914);
            if (result != 1) {
                value[0x1F] = 5;
                At<uint8_t>(self, 0x881) = 0;
                continue;
            }
            value[0x1F] = 3;
            At<float>(value, 0x24) = static_cast<float>(Virtual<double>(At<void*>(self, 0x830), 0x64)); // ST0
            const uint32_t capacity = Virtual<uint32_t>(At<void*>(self, 0x830), 0x60);
            if (capacity >= At<uint32_t>(value, 0x18)) {
                At<uint8_t>(self, 0x881) = 0;
                continue;
            }
            // The rest of the trip: a copy before the next trip, in state 0.
            char* const next = At<char*>(trip, 0);
            Virtual<void*>(lot, 0xC);
            char* const split = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(0x34));
            reinterpret_cast<CopyFn>(addr::SC4ByteVector_CopyConstruct)(split + 0x8, value);
            reinterpret_cast<CopyFn>(addr::SC4ByteVector_CopyConstruct)(split + 0x14, value + 0xC);
            At<uint32_t>(split, 0x20) = At<uint32_t>(value, 0x18);
            std::memcpy(split + 0x24, value + 0x1C, 4);
            At<uint16_t>(split, 0x28) = At<uint16_t>(value, 0x20);
            At<uint16_t>(split, 0x2A) = At<uint16_t>(value, 0x22);
            At<uint32_t>(split, 0x2C) = At<uint32_t>(value, 0x24);
            At<uint32_t>(split, 0x30) = At<uint32_t>(value, 0x28);
            At<char*>(split, 4) = At<char*>(next, 4);
            At<char*>(split, 0) = next;
            At<char*>(At<char*>(next, 4), 0) = split;
            At<char*>(next, 4) = split;
            split[0x27] = 0;
            const uint32_t rest = At<uint32_t>(value, 0x18) - capacity;
            At<uint32_t>(split, 0x20) = rest;
            At<uint32_t>(value, 0x18) -= rest;
            ++At<uint32_t>(self, 0x944);
            ++At<uint32_t>(self, 0x91C);
            ++At<uint32_t>(self, 0x96C);
            At<uint8_t>(self, 0x881) = 1;
        }
        if (done) {
            At<uint8_t>(self, 0x838) = 1;
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::cSC4TrafficSimulator_FinishLotTrips)(self, At<void*>(self, 0x84C));
        }
        release(lot);
    }
}

// Phase 19 (0x0071DC90), in states at +0x124 under a sliced deadline:
// 0 takes the next entry of the list at +0x104 (+0x10C; at the end posts
// 0x2C06BA82 and is done), 1 pairs it with each other entry of the same
// +8 (0x0071BE50), 2 routes it to each cell collected at +0x114
// (0x0071C0C0). The deadline is checked after a pairing or a cell.
uint8_t __fastcall Phase19(void* self, void*, void* parent)
{
    using LinkFn = void(__thiscall*)(void* self, char* entry, char* other);
    using RouteFn = void(__thiscall*)(void* self, char* entry, uint32_t x, uint32_t z);
    using EndFn = void(__thiscall*)(SlicedDeadline* deadline);
    using ExpiredFn = uint8_t(__thiscall*)(SlicedDeadline* deadline);
    static bool logged = false;
    FirstCall("Phase19", logged);
    uint32_t& flags = Global<uint32_t>(addr::cSC4TrafficSimulator_Phase19Step + 8);
    if (!(flags & 1)) {
        flags |= 1;
        Global<uint32_t>(addr::cSC4TrafficSimulator_Phase19Step) = 0x400;
        Global<uint32_t>(addr::cSC4TrafficSimulator_Phase19Step + 4) = 0;
    }
    SlicedDeadline deadline{};
    deadline.end[0] = static_cast<uint32_t*>(parent)[0];
    deadline.end[1] = static_cast<uint32_t*>(parent)[1];
    deadline.step = Global<uint32_t>(addr::cSC4TrafficSimulator_Phase19Step);
    deadline.stepGlobal = &Global<uint32_t>(addr::cSC4TrafficSimulator_Phase19Step);
    int32_t& state = At<int32_t>(self, 0x124);
    char*& entry = At<char*>(self, 0x10C);
    char*& other = At<char*>(self, 0x110);
    uint32_t*& cell = At<uint32_t*>(self, 0x120);
    for (;;) {
        if (state == 0) {
            char* const list = At<char*>(self, 0x104);
            if (entry == list) {
                PostSelfMessage(self, 0x2C06BA82);
                reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
                return 1;
            }
            other = At<char*>(list, 0);
            state = 1;
        }
        if (state == 1) {
            char* const current = entry + 8;
            if (other == At<char*>(self, 0x104)) {
                current[0x1E] = 0;
                state = 2;
                cell = At<uint32_t*>(self, 0x114);
                continue;
            }
            char* const candidate = other + 8;
            bool linked = false;
            if (candidate != current && At<uint32_t>(candidate, 0x8) == At<uint32_t>(current, 0x8) && candidate[0x1E]) {
                reinterpret_cast<LinkFn>(addr::cSC4TrafficSimulator_LinkRoutes19)(self, current, candidate);
                linked = true;
            }
            other = At<char*>(other, 0);
            if (!linked)
                continue;
        } else if (state == 2) {
            if (cell == At<uint32_t*>(self, 0x118)) {
                entry = At<char*>(entry, 0);
                state = 0;
            } else {
                reinterpret_cast<RouteFn>(addr::cSC4TrafficSimulator_RouteToCell19)(self, entry + 8, cell[0], cell[1]);
                cell += 2;
            }
        } else {
            continue; // the game's loop spins on any other state
        }
        deadline.count += deadline.step;
        if (!(deadline.count & 0x3FF) && reinterpret_cast<ExpiredFn>(addr::SC4Deadline_Expired)(&deadline)) {
            deadline.timedOut = 1;
            reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
            return 0;
        }
    }
}

using PhaseFn = uint8_t(__fastcall*)(void*, void*, void*);
PhaseFn g_originalPhase0 = nullptr;
PhaseFn g_originalPhase2 = nullptr;
PhaseFn g_originalPhase3 = nullptr;
PhaseFn g_originalPhase4 = nullptr;
PhaseFn g_originalPhase5 = nullptr;
PhaseFn g_originalPhase6 = nullptr;
PhaseFn g_originalPhase7 = nullptr;
PhaseFn g_originalPhase8 = nullptr;
PhaseFn g_originalPhase10 = nullptr;
PhaseFn g_originalPhase11 = nullptr;
PhaseFn g_originalPhase13 = nullptr;
PhaseFn g_originalPhase15 = nullptr;
PhaseFn g_originalPhase16 = nullptr;
PhaseFn g_originalPhase17 = nullptr;
PhaseFn g_originalPhase18 = nullptr;
PhaseFn g_originalPhase19 = nullptr;
using Phase12Fn = void(__fastcall*)(void*, void*);
Phase12Fn g_originalPhase12 = nullptr;

bool InstallPhases(registry::Mode)
{
    const struct {
        std::uintptr_t address;
        PhaseFn ours;
        PhaseFn* original;
    } phases[] = {
        {addr::cSC4TrafficSimulator_Phase0, &Phase0, &g_originalPhase0},
        {addr::cSC4TrafficSimulator_Phase2, &Phase2, &g_originalPhase2},
        {addr::cSC4TrafficSimulator_Phase3, &Phase3, &g_originalPhase3},
        {addr::cSC4TrafficSimulator_Phase4, &Phase4, &g_originalPhase4},
        {addr::cSC4TrafficSimulator_Phase5, &Phase5, &g_originalPhase5},
        {addr::cSC4TrafficSimulator_Phase6, &Phase6, &g_originalPhase6},
        {addr::cSC4TrafficSimulator_Phase7, &Phase7, &g_originalPhase7},
        {addr::cSC4TrafficSimulator_Phase8, &Phase8, &g_originalPhase8},
        {addr::cSC4TrafficSimulator_Phase10, &Phase10, &g_originalPhase10},
        {addr::cSC4TrafficSimulator_Phase11, &Phase11, &g_originalPhase11},
        {addr::cSC4TrafficSimulator_Phase13, &Phase13, &g_originalPhase13},
        {addr::cSC4TrafficSimulator_Phase15, &Phase15, &g_originalPhase15},
        {addr::cSC4TrafficSimulator_Phase16, &Phase16, &g_originalPhase16},
        {addr::cSC4TrafficSimulator_Phase17, &Phase17, &g_originalPhase17},
        {addr::cSC4TrafficSimulator_Phase18, &Phase18, &g_originalPhase18},
        {addr::cSC4TrafficSimulator_Phase19, &Phase19, &g_originalPhase19},
    };
    if (!game::IsBuild641())
        return false;
    const addr::Entry* twelve = addr_check::Find(addr::cSC4TrafficSimulator_Phase12);
    if (!twelve || !hook::Install(*twelve, &Phase12, &g_originalPhase12))
        return false;
    for (const auto& entry : phases) {
        const addr::Entry* found = addr_check::Find(entry.address);
        if (!found || !hook::Install(*found, entry.ours, entry.original))
            return false;
    }
    return true;
}

const bool g_registeredPhases = registry::Register({
    "simulators.traffic.phases",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_Phase0,
    "Our traffic sub-step phase functions, all 16 (docs/traffic.md)",
    false,
    InstallPhases,
});


// --- Write (+0x8 slot 3, 0x00712400) ----------------------------------------------

// The game's chained hash tables: an object with buckets at +4 (begin) and
// +8 (end); nodes chain by +0. Visits every node in bucket order, as the
// game's iterators do.
template <typename Fn>
void ForEachHashNode(char* table, Fn fn)
{
    char** const buckets = At<char**>(table, 4);
    const auto count = static_cast<uint32_t>(At<char**>(table, 8) - buckets);
    for (uint32_t bucket = 0; bucket < count; ++bucket)
        for (char* node = buckets[bucket]; node; node = At<char*>(node, 0))
            fn(node);
}

// A list with a sentinel (nodes chain by +0): its entries' count.
uint32_t ListCount(char* list)
{
    uint32_t count = 0;
    for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0))
        ++count;
    return count;
}

// The save record, version 41; the layout is traffic_simulator in
// tools/re/sc4save.py.
uint8_t __fastcall Write(void* serializable, void*, void* stream)
{
    using ObjectFn = void(__cdecl*)(void* stream, const void* object);
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = static_cast<char*>(serializable) - 0x8;
    const auto ref = [stream](const void* object) {
        reinterpret_cast<ObjectFn>(addr::SC4Stream_WriteReference)(stream, object);
    };
    const auto u8 = [stream](uint8_t v) { Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(v)); };
    const auto s16 = [stream](uint16_t v) { Virtual<uint8_t>(stream, 0x18, static_cast<uint32_t>(v)); };
    const auto u16 = [stream](uint16_t v) { Virtual<uint8_t>(stream, 0x1C, static_cast<uint32_t>(v)); };
    const auto s32 = [stream](uint32_t v) { Virtual<uint8_t>(stream, 0x20, v); };
    const auto u32 = [stream](uint32_t v) { Virtual<uint8_t>(stream, 0x24, v); };
    const auto f32 = [stream](uint32_t bits) { Virtual<uint8_t>(stream, 0x30, bits); }; // float bits, pushed as is
    const auto f64 = [stream](double v) { Virtual<uint8_t>(stream, 0x34, v); };
    const auto field = [self](std::size_t offset) { return At<uint32_t>(self, offset); };
    const auto bytes = [stream](const void* vector) {
        reinterpret_cast<ObjectFn>(addr::SC4Stream_WriteByteVector)(stream, vector);
    };
    u16(41);
    for (uint32_t count : {13u, 1u, 13u, 9u})
        s32(count);
    for (std::size_t offset : {0x30u, 0x34u, 0x38u, 0x3Cu, 0x44u, 0x48u})
        ref(At<void*>(self, offset));
    // The bordered byte grid at +0x4C: a vector of byte rows.
    char** const rows = At<char**>(self, 0x4C);
    const auto rowCount = static_cast<uint32_t>((At<char**>(self, 0x50) - rows) / 3);
    const auto columns = static_cast<uint32_t>(rows[1] - rows[0]);
    u32(rowCount);
    u32(columns);
    for (uint32_t row = 0; row < rowCount; ++row)
        for (uint32_t column = 0; column < columns; ++column)
            u8(static_cast<uint8_t>(rows[3 * row][column]));
    void** const grids = At<void**>(self, 0x164);
    const auto gridCount = static_cast<uint32_t>(At<void**>(self, 0x168) - grids);
    u32(gridCount);
    for (uint32_t i = 0; i < gridCount; ++i)
        ref(grids[i]);
    ref(At<void*>(self, 0x170));
    ref(At<void*>(self, 0x174));
    u32(field(0x188));
    ForEachHashNode(self + 0x178, [&](char* node) {
        u32(At<uint32_t>(node, 0x4));
        for (std::size_t i = 0; i < 16; ++i)
            u16(At<uint16_t>(node, 0x8 + 2 * i));
        u16(At<uint16_t>(node, 0x28));
        for (std::size_t i = 0; i < 16; ++i)
            u16(At<uint16_t>(node, 0x2A + 2 * i));
    });
    for (std::size_t i = 0; i < 3; ++i) {
        u32(field(0x1C0 + 4 * i));
        u32(field(0x1CC + 4 * i));
        u32(field(0x1D8 + 4 * i));
    }
    ref(At<void*>(self, 0x700));
    ref(At<void*>(self, 0x704));
    u32(field(0x708));
    u32(field(0x71C));
    ForEachHashNode(self + 0x70C, [&](char* node) {
        u32(At<uint32_t>(node, 0x4));
        ref(At<void*>(node, 0x8));
        f32(At<uint32_t>(node, 0xC));
        f32(At<uint32_t>(node, 0x10));
        f32(At<uint32_t>(node, 0x14));
        u8(At<uint8_t>(node, 0x19));
        u8(At<uint8_t>(node, 0x1A));
        u8(At<uint8_t>(node, 0x1B));
        f32(At<uint32_t>(node, 0x1C));
        f32(At<uint32_t>(node, 0x20));
        char* const parts = At<char*>(node, 0x24);
        const auto partCount = static_cast<uint32_t>((At<char*>(node, 0x28) - parts) / 10);
        u32(partCount);
        for (uint32_t i = 0; i < partCount; ++i) {
            char* const part = parts + 10 * i;
            for (std::size_t j = 0; j < 5; ++j)
                u8(At<uint8_t>(part, j));
            u16(At<uint16_t>(part, 0x6));
            u16(At<uint16_t>(part, 0x8));
        }
        f32(At<uint32_t>(node, 0x34));
        u8(At<uint8_t>(node, 0x38));
    });
    ref(At<void*>(self, 0x724));
    ref(At<void*>(self, 0x728));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, 0x778));
    for (std::size_t offset : {0x780u, 0x784u, 0x788u})
        f32(field(offset));
    f64(At<double>(self, 0x790));
    f64(At<double>(self, 0x798));
    u32(field(0x7A0));
    u8(At<uint8_t>(self, 0x7A4));
    s32(field(0x7A8));
    u32(field(0x7AC));
    u32(field(0x7B0));
    f32(field(0x7B4));
    // A big city's: the count and the CellKeys of WideSet, as uint32.
    const auto keys = [&](std::size_t set) {
        if (g_wideCells) {
            u32(static_cast<uint32_t>(WideSet(set).size()));
            for (const uint32_t cell : WideSet(set))
                u32(cell);
            return;
        }
        u32(field(set + 0x10));
        ForEachHashNode(self + set, [&](char* node) { u16(At<uint16_t>(node, 0x4)); });
    };
    keys(0x7B8);
    keys(0x7CC);
    u32(field(0x7E0));
    u32(field(0x7E4));
    f32(field(0x7E8));
    keys(0x7EC);
    keys(0x800);
    u8(At<uint8_t>(self, 0x814));
    reinterpret_cast<ObjectFn>(addr::SC4Stream_WriteCellIterator)(stream, At<void*>(self, 0x818));
    u8(At<uint8_t>(self, 0x81C));
    for (std::size_t offset : {0x820u, 0x824u, 0x828u, 0x82Cu})
        s32(field(offset));
    u8(At<uint8_t>(self, 0x920));
    u32(field(0x924));
    u32(field(0x928));
    s16(At<uint16_t>(self, 0x90C));
    s16(At<uint16_t>(self, 0x90E));
    reinterpret_cast<ObjectFn>(addr::SC4Stream_WriteFlaggedIterator)(stream, At<void*>(self, 0x910));
    u8(At<uint8_t>(self, 0x880));
    u8(At<uint8_t>(self, 0x869));
    f32(field(0x864));
    s32(field(0x83C));
    s32(field(0x840));
    u32(field(0x87C));
    for (std::size_t i = 0; i < 4; ++i)
        u32(field(0x854 + 4 * i));
    u8(At<uint8_t>(self, 0x868));
    u32(field(0x850));
    for (std::size_t offset : {0x86Cu, 0x870u, 0x874u, 0x878u})
        s32(field(offset));
    for (std::size_t i = 0; i < 27; ++i)
        u32(field(0x894 + 4 * i));
    u8(At<uint8_t>(self, 0x900));
    u8(At<uint8_t>(self, 0x901));
    for (std::size_t offset : {0x914u, 0x918u, 0x91Cu})
        s32(field(offset));
    for (std::size_t offset : {0x92Cu, 0x930u, 0x934u, 0x938u})
        u32(field(offset));
    for (std::size_t offset : {0x93Cu, 0x940u, 0x944u})
        s32(field(offset));
    for (std::size_t i = 0; i < 10; ++i)
        u32(field(0x948 + 4 * i));
    // Last month's statistics (+0x1E4), then this month's (+0x440).
    for (std::size_t block : {0x1E4u, 0x440u}) {
        const auto record = [&](std::size_t at) {
            f32(field(at));
            u32(field(at + 0x4));
            s32(field(at + 0x8));
            s32(field(at + 0xC));
            f32(field(at + 0x10));
        };
        for (std::size_t kind = 0; kind < 13; ++kind)
            record(block + 0x14 * kind);
        record(block + 0x104);
        for (std::size_t kind = 0; kind < 13; ++kind)
            u16(At<uint16_t>(self, block + 0x118 + 2 * kind));
        u16(At<uint16_t>(self, block + 0x132));
        u16(At<uint16_t>(self, block + 0x134));
        s32(12);
        for (std::size_t i = 0; i < 12; ++i) {
            const std::size_t at = block + 0x138 + 0x14 * i;
            u16(At<uint16_t>(self, at));
            f32(field(at + 0x4));
            s32(field(at + 0x8));
            s32(field(at + 0xC));
            f32(field(at + 0x10));
        }
        f32(field(block + 0x228));
        f32(field(block + 0x22C));
        u8(At<uint8_t>(self, block + 0x230));
        f32(field(block + 0x234));
        u8(At<uint8_t>(self, block + 0x238));
    }
    // A big city's links: a uint32 key and the linked cell as two uint16.
    if (g_wideCells) {
        u32(static_cast<uint32_t>(WideLinks().size()));
        for (auto& [cell, link] : WideLinks()) {
            u32(cell);
            u16(static_cast<uint16_t>(link.x));
            u16(static_cast<uint16_t>(link.z));
            u16(At<uint16_t>(link.node, 0xA));
            u16(At<uint16_t>(link.node, 0x30));
            for (std::size_t i = 0; i < 9; ++i)
                f32(At<uint32_t>(link.node, 0xC + 4 * i));
        }
    } else {
        u32(field(0xD8));
        ForEachHashNode(self + 0xC8, [&](char* node) {
            u16(At<uint16_t>(node, 0x4));
            u8(At<uint8_t>(node, 0x8));
            u8(At<uint8_t>(node, 0x9));
            u16(At<uint16_t>(node, 0xA));
            u16(At<uint16_t>(node, 0x30));
            for (std::size_t i = 0; i < 9; ++i)
                f32(At<uint32_t>(node, 0xC + 4 * i));
        });
    }
    f32(field(0x420));
    f32(field(0x6EC));
    u32(field(0x6F0));
    f32(field(0x6F4));
    u32(field(0x6F8));
    for (std::size_t i = 0; i < 13; ++i)
        u32(field(0x740 + 4 * i));
    u32(4);
    for (std::size_t i = 0; i < 4; ++i)
        u32(field(0x1B0 + 4 * i));
    // The map at +0xB8 (in order): its key and the object's own Write.
    u32(field(0xBC));
    char* const header = At<char*>(self, 0xB8);
    for (char* node = At<char*>(header, 0x8); node != header; node = NextNode(node)) {
        u16(At<uint16_t>(node, 0x10));
        void* const object = At<void*>(node, 0x14);
        Virtual<uint8_t>(object, 0x5C, stream);
    }
    for (std::size_t i = 0; i < 9; ++i) {
        u32(At<uint32_t*>(self, 0x424)[i]);
        u32(At<uint32_t*>(self, 0x430)[i]);
    }
    u32(field(0x43C));
    char* const routes = At<char*>(self, 0x104);
    u32(ListCount(routes));
    for (char* route = At<char*>(routes, 0); route != routes; route = At<char*>(route, 0)) {
        for (std::size_t offset : {0x8u, 0x9u, 0xAu, 0xBu, 0xCu, 0xDu, 0x10u})
            u8(At<uint8_t>(route, offset));
        u16(At<uint16_t>(route, 0x14));
        u16(At<uint16_t>(route, 0x20));
        u16(At<uint16_t>(route, 0x22));
        u8(At<uint8_t>(route, 0x24));
        u8(At<uint8_t>(route, 0x25));
        char* const first = At<char*>(route, 0x18);
        u32(ListCount(first));
        for (char* entry = At<char*>(first, 0); entry != first; entry = At<char*>(entry, 0)) {
            f32(At<uint32_t>(entry, 0x10));
            s16(At<uint16_t>(entry, 0xA));
            s16(At<uint16_t>(entry, 0xC));
            u16(At<uint16_t>(entry, 0x8));
            bytes(entry + 0x14);
            for (std::size_t offset : {0x20u, 0x24u, 0x22u, 0x26u})
                u16(At<uint16_t>(entry, offset));
        }
        char* const second = At<char*>(route, 0x1C);
        u32(ListCount(second));
        for (char* entry = At<char*>(second, 0); entry != second; entry = At<char*>(entry, 0)) {
            f32(At<uint32_t>(entry, 0x10));
            for (std::size_t offset : {0x8u, 0xAu, 0xCu, 0xEu})
                s16(At<uint16_t>(entry, offset));
            bytes(entry + 0x14);
            bytes(entry + 0x20);
            for (std::size_t offset : {0x2Cu, 0x30u, 0x2Eu, 0x32u})
                u16(At<uint16_t>(entry, offset));
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

using WriteFn = uint8_t(__fastcall*)(void*, void*, void*);
WriteFn g_originalWrite = nullptr;

bool InstallSave(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_Write);
    return game::IsBuild641() && entry && hook::Install(*entry, &Write, &g_originalWrite);
}

const bool g_registeredSave = registry::Register({
    "simulators.traffic.save",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_Write,
    "Our traffic Write: the save record, version 41 (docs/traffic.md)",
    false,
    InstallSave,
});


// --- Read (+0x8 slot 4, 0x00721990) -----------------------------------------------

// Reads the save record, versions 19 to 41, as the game does: older versions
// default the counts read at the start and map their statistics to today's
// kinds. Offsets here are from the cIGZSerializable part (object +0x8), as
// in the game's code.
uint8_t __fastcall Read(void* serializable, void*, void* stream)
{
    using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t flags);
    using ObjectFn = void(__cdecl*)(void* stream, void* object);
    using NewFn = void*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* memory);
    using ThisFn = void(__thiscall*)(void* object);
    using AtFn = char*(__thiscall*)(void* table, const void* key);
    static bool logged = false;
    FirstCall("Read", logged);
    char* const s = static_cast<char*>(serializable);
    const auto in = [stream](std::size_t slot, void* to) { Virtual<uint8_t>(stream, slot, to); };
    const auto u8 = [&in](void* to) { in(0x14, to); };
    const auto s16 = [&in](void* to) { in(0x18, to); };
    const auto u16 = [&in](void* to) { in(0x1C, to); };
    const auto s32 = [&in](void* to) { in(0x20, to); };
    const auto u32 = [&in](void* to) { in(0x24, to); };
    const auto f32 = [&in](void* to) { in(0x30, to); };
    const auto boolean = [stream](void* to) { reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadBool)(stream, to); };
    const auto ref = [stream](uint32_t iid, void* to) {
        reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference)(stream, iid, to, 0);
    };
    const auto stlNew = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
    const auto stlFree = reinterpret_cast<FreeFn>(addr::SC4_Free);
    const auto at = [s](std::size_t offset) { return s + offset; };
    // A float scaled by 0.01 (old versions kept percentages).
    const auto percent = [&f32](void* to) {
        float value = 0.0f;
        f32(&value);
        *static_cast<float*>(to) = static_cast<float>(static_cast<double>(value) * 0.01f);
    };
    // The saved keys are wide for a big city, and Read may come before Init.
    if (void* const city = Global<void*>(addr::SC4_City))
        SetWideCells(Virtual<int32_t>(city, 0x174), Virtual<int32_t>(city, 0x178));
    ClearWideSets();
    uint16_t version = 0;
    u16(&version);
    if (version < 0x13)
        return 0;
    int32_t kinds = 8, extra = 1, oldKinds = 4, types = 7;
    if (version >= 0x18)
        s32(&kinds);
    if (version >= 0x25)
        s32(&extra);
    if (version >= 0x1C)
        s32(&oldKinds);
    if (version >= 0x1C)
        s32(&types);
    for (std::size_t offset : {0x28u, 0x2Cu, 0x30u, 0x34u, 0x3Cu, 0x40u})
        ref(0xC9BD939A, at(offset));
    // The bordered byte grid.
    uint32_t rows = 0, columns = 0;
    u32(&rows);
    u32(&columns);
    char* emptyRow[3] = {};
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, void*)>(addr::SC4RowVector_Resize)(at(0x44), rows, emptyRow);
    for (uint32_t row = 0; row < rows; ++row) {
        char** const vector = At<char**>(s, 0x44) + 3 * row;
        const auto size = static_cast<uint32_t>(vector[1] - vector[0]);
        uint8_t zero = 0;
        if (columns < size)
            vector[1] = vector[0] + columns;
        else
            reinterpret_cast<void(__thiscall*)(void*, char*, uint32_t, const uint8_t*)>(addr::SC4ByteVector_InsertN)(
                vector, vector[1], columns - size, &zero);
        for (uint32_t column = 0; column < columns; ++column)
            u8(At<char**>(s, 0x44)[3 * row] + column);
    }
    // The grids at +0x15C.
    uint32_t count = 0;
    u32(&count);
    {
        void** const vector = reinterpret_cast<void**>(at(0x15C));
        const auto size = static_cast<uint32_t>(static_cast<void**>(vector[1]) - static_cast<void**>(vector[0]));
        uint32_t zero = 0;
        if (count < size)
            vector[1] = static_cast<void**>(vector[0]) + count;
        else
            reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t, const uint32_t*)>(addr::SC4PtrVector_FillInsert)(
                vector, vector[1], count - size, &zero);
        for (uint32_t i = 0; i < count; ++i)
            ref(0xC9BD939A, static_cast<void**>(vector[0]) + i);
    }
    ref(0xC9BD939A, at(0x168));
    ref(0xC9BD939A, at(0x16C));
    // The edge density table (+0x170 here).
    u32(&count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0;
        uint16_t value[0x21] = {};
        u32(&key);
        if (version < 0x14) {
            const auto rounded = [&f32]() {
                float number = 0.0f;
                f32(&number);
                const uint32_t bits = MagicRound(number);
                return static_cast<uint16_t>(bits > 0xFFFF ? 0xFFFF : bits);
            };
            for (std::size_t j = 0; j < 16; ++j)
                value[j] = rounded();
            value[16] = rounded();
            for (std::size_t j = 17; j < 33; ++j)
                value[j] = rounded();
            float unused = 0.0f;
            f32(&unused);
        } else {
            for (std::size_t j = 0; j < 33; ++j)
                u16(&value[j]);
        }
        char* const node = reinterpret_cast<AtFn>(addr::SC4DensityTable_At)(at(0x170), &key);
        std::memcpy(node, value, 0x42);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        u32(at(0x1B8 + 4 * i));
        u32(at(0x1C4 + 4 * i));
        u32(at(0x1D0 + 4 * i));
    }
    ref(0xC9BD939A, at(0x6F8));
    ref(0xC9BD939A, at(0x6FC));
    u32(at(0x700));
    // The cost table (+0x704 here).
    u32(&count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0;
        u32(&key);
        char value[0x34] = {}; // +0x1C a vector of 10-byte parts
        ref(0x26BB767E, value);
        f32(value + 0x4);
        f32(value + 0x8);
        f32(value + 0xC);
        if (version < 0x1D) {
            uint32_t skipped = 0;
            u32(&skipped);
            for (uint32_t j = 0; j < skipped; ++j) {
                uint8_t byte = 0;
                u8(&byte);
            }
            At<uint8_t>(s, 0x195) = 1;
        }
        boolean(value + 0x11);
        u8(value + 0x12);
        u8(value + 0x13);
        f32(value + 0x14);
        if (version >= 0x17)
            f32(value + 0x18);
        else
            percent(value + 0x18);
        At<uint16_t>(value, 0x28) = 0;
        value[0x10] = 0;
        if (version > 0x1C) {
            uint32_t parts = 0;
            u32(&parts);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4PartVector_Reserve)(value + 0x1C, parts);
            for (uint32_t j = 0; j < parts; ++j) {
                char part[10] = {};
                boolean(part);
                for (std::size_t k = 1; k < 5; ++k)
                    boolean(part + k);
                u16(part + 6);
                u16(part + 8);
                // push_back, growing to size + max(size, 1) as the game's inline code does
                char** const vector = reinterpret_cast<char**>(value + 0x1C);
                if (vector[1] != vector[2]) {
                    std::memcpy(vector[1], part, 10);
                    vector[1] += 10;
                } else {
                    const auto size = static_cast<uint32_t>((vector[1] - vector[0]) / 10);
                    const uint32_t capacity = size + (size < 1 ? 1 : size);
                    char* const grown = capacity ? static_cast<char*>(stlNew(10 * capacity)) : nullptr;
                    if (size)
                        std::memcpy(grown, vector[0], 10 * size);
                    std::memcpy(grown + 10 * size, part, 10);
                    if (vector[0])
                        stlFree(vector[0]);
                    vector[0] = grown;
                    vector[1] = grown + 10 * (size + 1);
                    vector[2] = grown + 10 * capacity;
                }
                const uint16_t kind = At<uint16_t>(part, 8);
                if (part[0])
                    At<uint16_t>(value, 0x28) |= kind;
                switch (kind) {
                case 2: value[0x10] |= 0x01; break;
                case 3: value[0x10] |= 0x02; break;
                case 5: value[0x10] |= 0x20; break;
                case 6: value[0x10] |= 0x04; break;
                case 7: value[0x10] |= 0x08; break;
                case 8: value[0x10] |= 0x10; break;
                default: break;
                }
                const uint16_t other = At<uint16_t>(part, 6);
                if ((kind == 7 && other == 6) || (other == 7 && kind == 6))
                    value[0x10] |= 0x40;
            }
        }
        if (version > 0x20) {
            f32(value + 0x2C);
            boolean(value + 0x30);
        }
        char* const node = reinterpret_cast<AtFn>(addr::SC4CostTable_At)(at(0x704), &key);
        std::memcpy(node, value, 0x1C);
        reinterpret_cast<void(__thiscall*)(void*, const void*)>(addr::SC4PartVector_Assign)(node + 0x1C, value + 0x1C);
        At<uint16_t>(node, 0x28) = At<uint16_t>(value, 0x28);
        At<uint32_t>(node, 0x2C) = At<uint32_t>(value, 0x2C);
        node[0x30] = value[0x30];
        if (char* const parts = At<char*>(value, 0x1C))
            stlFree(parts);
    }
    ref(0x223E2B67, at(0x71C));
    ref(0x223E2B67, at(0x720));
    in(0x28, at(0x770));
    f32(at(0x778));
    if (version > 0x28)
        f32(at(0x77C));
    f32(at(0x780));
    if (version > 0x15) {
        in(0x34, at(0x788));
    } else {
        float value = 0.0f;
        f32(&value);
        At<double>(s, 0x788) = value;
    }
    if (version > 0x28)
        in(0x34, at(0x790));
    u32(at(0x798));
    uint8_t byte = 0;
    u8(&byte);
    At<uint32_t>(s, 0x79C) = byte;
    s32(at(0x7A0));
    u32(at(0x7A4));
    u32(at(0x7A8));
    f32(at(0x7AC));
    const auto keys = [&](std::size_t set) {
        uint32_t n = 0;
        u32(&n);
        if (g_wideCells) { // as Write: uint32 CellKeys (`set` is from +8)
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t cell = 0;
                u32(&cell);
                WideSet(set + 8).insert(cell);
            }
            return;
        }
        for (uint32_t i = 0; i < n; ++i) {
            uint16_t key = 0;
            u16(&key);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4Uint16Set_Reserve)(at(set), At<uint32_t>(s, set + 0x10) + 1);
            uint32_t result[3] = {};
            reinterpret_cast<void(__thiscall*)(void*, void*, const uint16_t*)>(addr::SC4Uint16Set_Insert)(at(set), result, &key);
        }
    };
    keys(0x7B0);
    keys(0x7C4);
    if (version >= 0x1A) {
        u32(at(0x7D8));
        u32(at(0x7DC));
        f32(at(0x7E0));
        keys(0x7E4);
        keys(0x7F8);
    }
    u8(&byte);
    At<uint32_t>(s, 0x80C) = byte;
    const auto newIterator = [](bool flagged) {
        auto* const iterator = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x20));
        if (!iterator)
            return iterator;
        iterator[0] = 0;
        if (flagged)
            iterator[1] = 0;
        At<int32_t>(iterator, 0x4) = 0;
        At<int32_t>(iterator, 0x8) = 0;
        At<int32_t>(iterator, 0xC) = 1;
        At<uint32_t>(iterator, 0x10) = 0x80000000;
        At<uint32_t>(iterator, 0x14) = 0x80000000;
        At<uint32_t>(iterator, 0x18) = 0x7FFFFFFF;
        At<uint32_t>(iterator, 0x1C) = 0x7FFFFFFF;
        if (!flagged && (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 ||
                         At<int32_t>(iterator, 0x18) < 0 || At<int32_t>(iterator, 0x1C) < 0))
            iterator[0] = 2;
        return iterator;
    };
    char* const cells = newIterator(false);
    At<char*>(s, 0x810) = cells;
    reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadCellIterator)(stream, cells);
    boolean(at(0x814));
    for (std::size_t offset : {0x818u, 0x81Cu, 0x820u, 0x824u})
        s32(at(offset));
    boolean(at(0x918));
    u32(at(0x91C));
    u32(at(0x920));
    if (version < 0x19) {
        uint8_t x = 0, z = 0;
        u8(&x);
        u8(&z);
        At<uint16_t>(s, 0x904) = x;
        At<uint16_t>(s, 0x906) = z;
    } else {
        s16(at(0x904));
        s16(at(0x906));
    }
    char* const walk = newIterator(true);
    At<char*>(s, 0x908) = walk;
    reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadFlaggedIterator)(stream, walk);
    boolean(at(0x878));
    boolean(at(0x861));
    f32(at(0x85C));
    s32(at(0x834));
    s32(at(0x838));
    u32(at(0x874));
    for (std::size_t i = 0; i < 4; ++i)
        u32(at(0x84C + 4 * i));
    u8(at(0x860));
    u32(at(0x848));
    for (std::size_t i = 0; i < 4; ++i)
        s32(at(0x864 + 4 * i));
    for (std::size_t group = 0; group < 3; ++group) {
        for (int32_t i = 0; i < types; ++i)
            u32(at(0x88C + 0x24 * group + 4 * i));
        for (int32_t i = types; i < 9; ++i)
            At<uint32_t>(s, 0x88C + 0x24 * group + 4 * i) = 0;
    }
    u8(at(0x8F8));
    u8(at(0x8F9));
    s32(at(0x90C));
    s32(at(0x910));
    if (version > 0x18)
        s32(at(0x914));
    for (std::size_t offset : {0x924u, 0x928u, 0x92Cu, 0x930u})
        u32(at(offset));
    if (version > 0x18) {
        s32(at(0x934));
        s32(at(0x938));
        s32(at(0x93C));
        for (std::size_t offset : {0x940u, 0x944u, 0x948u, 0x94Cu})
            u32(at(offset));
    }
    if (version > 0x27) {
        for (std::size_t i = 0; i < 6; ++i)
            u32(at(0x950 + 4 * i));
    } else {
        for (std::size_t i = 0; i < 6; ++i)
            At<uint32_t>(s, 0x950 + 4 * i) = 0;
    }
    // Last month's and this month's statistics.
    static const uint32_t kOldKinds[6] = {0, 0, 1, 7, 8, 9};
    for (std::size_t block : {0x1DCu, 0x438u}) {
        const int32_t n = version >= 0x23 ? kinds : oldKinds;
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t kind = version >= 0x23 ? static_cast<uint32_t>(i) : kOldKinds[i];
            char* const record = at(block + 0x14 * kind);
            f32(record);
            u32(record + 0x4);
            s32(record + 0x8);
            s32(record + 0xC);
            f32(record + 0x10);
        }
        if (version >= 0x25) {
            for (int32_t i = 0; i < extra; ++i) {
                char* const record = at(block + 0x104 + 0x14 * i);
                f32(record);
                u32(record + 0x4);
                s32(record + 0x8);
                s32(record + 0xC);
                f32(record + 0x10);
            }
        }
        for (int32_t i = 0; i < kinds; ++i)
            u16(at(block + 0x118 + 2 * i));
        if (version >= 0x25)
            for (int32_t i = 0; i < extra; ++i)
                u16(at(block + 0x132 + 2 * i));
        u16(at(block + 0x134));
        int32_t records = 3;
        if (version >= 0x22)
            s32(&records);
        for (int32_t i = 0; i < records; ++i) {
            char* const record = at(block + 0x13C + 0x14 * i);
            u16(record - 0x4);
            if (version >= 0x17)
                f32(record);
            else
                percent(record);
            s32(record + 0x4);
            s32(record + 0x8);
            if (version >= 0x17)
                f32(record + 0xC);
            else
                percent(record + 0xC);
        }
        f32(at(block + 0x228));
        f32(at(block + 0x22C));
        boolean(at(block + 0x230));
        f32(at(block + 0x234));
        if (version > 0x14)
            u8(at(block + 0x238));
        else
            At<uint8_t>(s, block + 0x238) = 0x80;
    }
    // The link table (+0xC0 here).
    u32(&count);
    for (uint32_t i = 0; g_wideCells && i < count; ++i) { // as Write writes a big city's
        uint32_t cell = 0;
        uint16_t x = 0, z = 0;
        u32(&cell);
        u16(&x);
        u16(&z);
        WideLink& link = WideLinks()[cell];
        std::memset(link.node, 0, sizeof link.node);
        link.x = x;
        link.z = z;
        u16(link.node + 0xA);
        u16(link.node + 0x30);
        for (int32_t j = 0; j < types; ++j)
            f32(link.node + 0xC + 4 * j);
    }
    for (uint32_t i = 0; !g_wideCells && i < count; ++i) {
        uint16_t key = 0;
        uint32_t value[11] = {};
        u16(&key);
        u8(reinterpret_cast<char*>(value));
        u8(reinterpret_cast<char*>(value) + 1);
        u16(reinterpret_cast<char*>(value) + 2);
        u16(reinterpret_cast<char*>(value) + 0x28);
        for (int32_t j = 0; j < types; ++j)
            f32(&value[1 + j]);
        for (int32_t j = types; j < 9; ++j)
            value[1 + j] = 0;
        char* const node = reinterpret_cast<AtFn>(addr::SC4LinkTable_At)(at(0xC0), &key);
        std::memcpy(node, value, sizeof(value));
    }
    f32(at(0x418));
    f32(at(0x6E4));
    u32(at(0x6E8));
    if (version > 0x14) {
        f32(at(0x6EC));
        u32(at(0x6F0));
    } else {
        At<uint32_t>(s, 0x6EC) = 0;
        At<uint32_t>(s, 0x6F0) = 0;
    }
    int32_t read = 0;
    for (; read < kinds; ++read)
        u32(at(0x738 + 4 * read));
    for (; read < 13; ++read)
        At<uint32_t>(s, 0x738 + 4 * read) = 0;
    count = 0;
    u32(&count);
    uint32_t done = 0;
    for (; done < count; ++done)
        u32(at(0x1A8 + 4 * done));
    if (count == 0 || done < 4)
        for (; done < 4; ++done)
            At<uint32_t>(s, 0x1A8 + 4 * done) = 0xFFFFFFFF;
    // The map at +0xB0 here: objects that read themselves.
    u32(&count);
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t key = 0;
        u16(&key);
        void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x14);
        void* const object = memory ? (reinterpret_cast<ThisFn>(addr::SC4TrafficObject_Construct)(memory), memory) : nullptr;
        Virtual<uint32_t>(object, 0x4);
        Virtual<uint8_t>(object, 0x60, stream);
        *reinterpret_cast<void**>(reinterpret_cast<AtFn>(addr::SC4ObjectMap_At)(at(0xB0), &key)) = object;
    }
    if (version > 0x1A) {
        for (int32_t i = 0; i < types; ++i) {
            u32(At<uint32_t*>(s, 0x41C) + i);
            u32(At<uint32_t*>(s, 0x428) + i);
        }
        for (int32_t i = types; i < 9; ++i) {
            At<uint32_t*>(s, 0x41C)[i] = 0;
            At<uint32_t*>(s, 0x428)[i] = 0;
        }
    }
    if (version > 0x26)
        u32(at(0x434));
    if (version > 0x1D) {
        uint32_t routes = 0;
        u32(&routes);
        for (uint32_t r = 0; r < routes; ++r) {
            if (version < 0x20) {
                uint16_t unused = 0;
                u16(&unused);
            }
            // A new route at the end of the list (+0xFC here), copied from an
            // empty one (the game's prototype has two empty lists).
            char prototype[0x20] = {};
            char* const first = static_cast<char*>(stlNew(0x28));
            At<char*>(first, 0) = first;
            At<char*>(first, 4) = first;
            char* const second = static_cast<char*>(stlNew(0x34));
            At<char*>(second, 0) = second;
            At<char*>(second, 4) = second;
            At<char*>(prototype, 0x10) = first;
            At<char*>(prototype, 0x14) = second;
            char* const list = At<char*>(s, 0xFC);
            char* const node = static_cast<char*>(stlNew(0x28));
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4Route_CopyConstruct)(node + 8, prototype);
            At<char*>(node, 0) = list;
            At<char*>(node, 4) = At<char*>(list, 4);
            At<char*>(At<char*>(list, 4), 0) = node;
            At<char*>(list, 4) = node;
            reinterpret_cast<ThisFn>(addr::SC4RouteList_Clear)(&At<char*>(prototype, 0x14));
            stlFree(second);
            stlFree(first);
            char* const route = At<char*>(At<char*>(s, 0xFC), 4) + 8;
            for (std::size_t k = 0; k < 6; ++k)
                u8(route + k);
            u8(&byte);
            At<uint32_t>(route, 0x8) = byte;
            u16(route + 0xC);
            u16(route + 0x18);
            u16(route + 0x1A);
            if (version > 0x25) {
                u8(route + 0x1C);
                u8(route + 0x1D);
            } else {
                route[0x1C] = route[0];
                route[0x1D] = route[1];
            }
            const auto append = [&stlNew](char* sentinel, std::size_t size, const char* value, std::size_t vectors) {
                char* const entry = static_cast<char*>(stlNew(size));
                std::memcpy(entry + 8, value, size - 8);
                for (std::size_t v = 0; v < vectors; ++v)
                    reinterpret_cast<void(__thiscall*)(void*, const void*)>(addr::SC4ByteVector_CopyConstruct)(
                        entry + 8 + 0xC + 0xC * v, value + 0xC + 0xC * v);
                At<char*>(entry, 0) = sentinel;
                At<char*>(entry, 4) = At<char*>(sentinel, 4);
                At<char*>(At<char*>(sentinel, 4), 0) = entry;
                At<char*>(sentinel, 4) = entry;
            };
            uint32_t entries = 0;
            u32(&entries);
            for (uint32_t e = 0; e < entries; ++e) {
                char value[0x20] = {};
                f32(value + 0x8);
                s16(value + 0x2);
                s16(value + 0x4);
                u16(value);
                reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadByteVector)(stream, value + 0xC);
                if (version > 0x23) {
                    u16(value + 0x18);
                    u16(value + 0x1C);
                    u16(value + 0x1A);
                    u16(value + 0x1E);
                }
                append(At<char*>(route, 0x10), 0x28, value, 1);
                if (char* const bytes = At<char*>(value, 0xC))
                    stlFree(bytes);
            }
            u32(&entries);
            for (uint32_t e = 0; e < entries; ++e) {
                char value[0x2C] = {};
                f32(value + 0x8);
                for (std::size_t k = 0; k < 4; ++k)
                    s16(value + 2 * k);
                reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadByteVector)(stream, value + 0xC);
                if (version > 0x1E)
                    reinterpret_cast<ObjectFn>(addr::SC4Stream_ReadByteVector)(stream, value + 0x18);
                if (version > 0x23) {
                    u16(value + 0x24);
                    u16(value + 0x28);
                    u16(value + 0x26);
                    u16(value + 0x2A);
                }
                append(At<char*>(route, 0x14), 0x34, value, 2);
                if (char* const bytes = At<char*>(value, 0x18))
                    stlFree(bytes);
                if (char* const bytes = At<char*>(value, 0xC))
                    stlFree(bytes);
            }
            char* const routeNode = route - 8;
            uint32_t key = static_cast<uint32_t>(static_cast<uint8_t>(route[0])) << 8 | static_cast<uint8_t>(route[1]);
            *reinterpret_cast<char**>(reinterpret_cast<AtFn>(addr::SC4RouteIndex_At)(at(0xD4), &key)) = routeNode;
            key = static_cast<uint32_t>(static_cast<uint8_t>(route[2])) << 8 | static_cast<uint8_t>(route[3]);
            *reinterpret_cast<char**>(reinterpret_cast<AtFn>(addr::SC4RouteIndex_At)(at(0xE8), &key)) = routeNode;
            const auto ferry = At<int32_t>(route, 0x8);
            if (ferry >= 0 && ferry < 2)
                ++At<uint32_t>(s, 0x120 + 4 * ferry);
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

using ReadFn = uint8_t(__fastcall*)(void*, void*, void*);
ReadFn g_originalRead = nullptr;

bool InstallLoad(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_Read);
    return game::IsBuild641() && entry && hook::Install(*entry, &Read, &g_originalRead);
}

const bool g_registeredLoad = registry::Register({
    "simulators.traffic.load",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_Read,
    "Our traffic Read: the save record, versions 19 to 41 (docs/traffic.md)",
    false,
    InstallLoad,
});


// --- Shutdown (slot 4, 0x00719290) -------------------------------------------------

uint8_t __fastcall Shutdown(void* object, void*)
{
    using ThisFn = void(__thiscall*)(void* object);
    using RangeFn = void(__thiscall*)(void* vector, void* begin, void* end);
    using BlockFn = void(__thiscall*)(void* pool, void* block);
    using DeleteFn = void(__cdecl*)(void* memory);
    static bool logged = false;
    FirstCall("Shutdown", logged);
    char* const self = static_cast<char*>(object);
    ClearWideSets();
    if (!At<uint8_t>(self, 0xC))
        return 1;
    At<uint8_t>(self, 0xC) = 0;
    const auto release = [self](std::size_t offset) {
        if (void* const held = At<void*>(self, offset)) {
            Virtual<uint32_t>(held, 0x8);
            At<void*>(self, offset) = nullptr;
        }
    };
    // A grid: its slot 4 (shut down), then released.
    const auto grid = [self](std::size_t offset) {
        if (void* const held = At<void*>(self, offset)) {
            Virtual<uint32_t>(held, 0x10);
            Virtual<uint32_t>(At<void*>(self, offset), 0x8);
            At<void*>(self, offset) = nullptr;
        }
    };
    const auto remove = reinterpret_cast<DeleteFn>(addr::SC4_operator_delete);
    release(0x1C);
    release(0x14);
    for (std::size_t offset : {0x30u, 0x34u, 0x38u, 0x3Cu, 0x40u, 0x44u})
        grid(offset);
    void**& gridsBegin = At<void**>(self, 0x164);
    if (gridsBegin != At<void**>(self, 0x168)) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<void**>(self, 0x168) - gridsBegin); ++i) {
            Virtual<uint32_t>(gridsBegin[i], 0x10);
            Virtual<uint32_t>(gridsBegin[i], 0x8);
            gridsBegin[i] = nullptr;
        }
        At<void**>(self, 0x168) = gridsBegin; // resize(0)
    }
    grid(0x170);
    grid(0x48);
    grid(0x174);
    reinterpret_cast<RangeFn>(addr::SC4RowVector_Destroy)(self + 0x4C, At<void*>(self, 0x4C), At<void*>(self, 0x50));
    grid(0x700);
    grid(0x704);
    // The edge chains: each node back to its 2 KB pool block.
    char** const rows = At<char**>(self, field::kEdges);
    if (rows != At<char**>(self, 0x80)) {
        const auto rowCount = static_cast<int32_t>((At<char**>(self, 0x80) - rows) / 3);
        for (int32_t row = 0; row < rowCount; ++row) {
            char** const cells = reinterpret_cast<char**>(At<char**>(self, field::kEdges)[3 * row]);
            const auto cellCount =
                static_cast<int32_t>(reinterpret_cast<char**>(At<char**>(self, field::kEdges)[3 * row + 1]) - cells);
            for (int32_t cell = 0; cell < cellCount; ++cell) {
                char* node = reinterpret_cast<char**>(At<char**>(self, field::kEdges)[3 * row])[cell];
                while (node) {
                    char* const next = At<char*>(node, 0x14);
                    char* const block = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(node) & 0xFFFFF800u);
                    At<char*>(node, 0) = At<char*>(block, 0x8);
                    At<char*>(block, 0x8) = node;
                    const uint32_t left = --At<uint32_t>(block, 0xC);
                    void* const pool = reinterpret_cast<void*>(addr::SC4_EdgePool);
                    if (left == 0)
                        reinterpret_cast<BlockFn>(addr::SC4Pool_ReleaseBlock)(pool, block);
                    else if (left == 0x4000003F)
                        reinterpret_cast<BlockFn>(addr::SC4Pool_ReopenBlock)(pool, block);
                    node = next;
                }
            }
        }
    }
    reinterpret_cast<RangeFn>(addr::SC4EdgeRows_Erase)(self + field::kEdges, At<void*>(self, field::kEdges), At<void*>(self, 0x80));
    remove(At<void*>(self, 0x58));
    At<void*>(self, 0x58) = nullptr;
    if (void* const held = At<void*>(self, 0x6FC)) {
        remove(held);
        At<void*>(self, 0x6FC) = nullptr;
    }
    release(0x724);
    release(0x728);
    ForEachHashNode(self + 0x70C, [](char* node) { Virtual<uint32_t>(At<void*>(node, 0x8), 0x8); });
    reinterpret_cast<ThisFn>(addr::SC4CostTable_Clear)(self + 0x70C);
    if (void* const held = At<void*>(self, 0x818)) {
        remove(held);
        At<void*>(self, 0x818) = nullptr;
    }
    release(0x830);
    release(0x834);
    // The occupant walk: its nodes in the list it walks are removed first.
    if (char* const walk = At<char*>(self, 0x904)) {
        if (char* const head = At<char*>(walk, 0x4)) {
            char* const sentinel = At<char*>(head, 0x4);
            for (char* node = At<char*>(sentinel, 0); node != sentinel;) {
                char* const next = At<char*>(node, 0);
                if (At<char*>(node, 0x8) == walk) {
                    char* const previous = At<char*>(node, 0x4);
                    At<char*>(previous, 0) = next;
                    At<char*>(next, 0x4) = previous;
                    reinterpret_cast<DeleteFn>(addr::SC4_Free)(node);
                }
                node = next;
            }
        }
        remove(walk);
        At<void*>(self, 0x904) = nullptr;
    }
    if (void* const held = At<void*>(self, 0x910)) {
        remove(held);
        At<void*>(self, 0x910) = nullptr;
    }
    if (char* const walk = At<char*>(self, 0x908)) {
        reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4List_ClearReleasing)(reinterpret_cast<void**>(walk + 4));
        Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, At<void*>(walk, 4));
        remove(walk);
        At<void*>(self, 0x908) = nullptr;
    }
    if (At<void*>(self, 0x18)) {
        for (uint32_t type : {0xC2C09554u, 0x08DAC3E7u, 0x99EF1142u, 0x99EF1143u, 0x26D31EC1u, 0xE8BDA2E7u, 0xC53D10AAu,
                              0x8A0D15DAu, 0xEA0D163Eu})
            Virtual<uint8_t>(At<void*>(self, 0x18), 0x18, self + 4, type);
        Virtual<uint32_t>(At<void*>(self, 0x18), 0x8);
        At<void*>(self, 0x18) = nullptr;
    }
    if (void* const simulator = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint8_t>(simulator, 0x50, self + 4);
    // The map at +0xB8: its objects released, then its nodes freed.
    char* const header = At<char*>(self, 0xB8);
    for (char* node = At<char*>(header, 0x8); node != header; node = NextNode(node))
        Virtual<uint32_t>(At<void*>(node, 0x14), 0x8);
    if (At<uint32_t>(self, 0xBC)) {
        for (char* node = At<char*>(At<char*>(self, 0xB8), 0x4); node;) {
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4ObjectMap_EraseSubtree)(self + 0xB8, At<void*>(node, 0xC));
            char* const left = At<char*>(node, 0x8);
            reinterpret_cast<DeleteFn>(addr::SC4_Free)(node);
            node = left;
        }
        char* const root = At<char*>(self, 0xB8);
        At<char*>(root, 0x8) = root;
        At<char*>(root, 0x4) = nullptr;
        At<char*>(root, 0xC) = root;
        At<uint32_t>(self, 0xBC) = 0;
    }
    if (void* const held = At<void*>(self, 0xC4)) {
        At<void*>(self, 0xC4) = nullptr;
        Virtual<uint32_t>(held, 0x8);
    }
    if (void* const finder = At<void*>(self, 0x108)) {
        reinterpret_cast<ThisFn>(addr::SC4WaterRouteFinder_Shutdown)(finder);
        if (void* const held = At<void*>(self, 0x108)) {
            At<void*>(self, 0x108) = nullptr;
            Virtual<uint32_t>(held, 0x8);
        }
    }
    return 1;
}

using ShutdownFn = uint8_t(__fastcall*)(void*, void*);
ShutdownFn g_originalShutdown = nullptr;

bool InstallShutdown(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_Shutdown);
    return game::IsBuild641() && entry && hook::Install(*entry, &Shutdown, &g_originalShutdown);
}

const bool g_registeredShutdown = registry::Register({
    "simulators.traffic.shutdown",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_Shutdown,
    "Our traffic Shutdown (docs/traffic.md)",
    false,
    InstallShutdown,
});


// --- Init (slot 3, 0x0071E7E0) ----------------------------------------------------

// A sim grid as Init constructs it (operator new; the other fields are left
// as it returns them).
void* NewTrafficGrid(std::uintptr_t vtable, uint32_t vtable4, uint32_t iid)
{
    auto* const grid = static_cast<uint32_t*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x44));
    if (!grid)
        return nullptr;
    grid[1] = 0x00A80784;
    grid[0] = static_cast<uint32_t>(vtable);
    grid[1] = vtable4;
    reinterpret_cast<uint8_t*>(grid)[8] = 0;
    grid[3] = 0;
    grid[4] = iid;
    grid[6] = 0;
    grid[14] = 0;
    grid[15] = 0;
    grid[16] = 0;
    return grid;
}

// The game's cIGZString over an STL-style buffer.
struct TrafficName {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

uint8_t __fastcall Init(void* object, void*)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using ThisFn = void(__thiscall*)(void* object);
    static bool logged = false;
    FirstCall("Init", logged);
    char* const self = static_cast<char*>(object);
    if (At<uint8_t>(self, 0xC))
        return 1;
    At<uint8_t>(self, 0xC) = 1;
    if (!Global<uint8_t>(addr::cSC4TrafficSimulator_InitOnce))
        Global<uint8_t>(addr::cSC4TrafficSimulator_InitOnce) = 1;
    const auto operatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_LoadTuning)(self);
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    TrafficName name{};
    reinterpret_cast<void(__thiscall*)(void*, const char*)>(addr::SC4String_FromText)(
        &name, reinterpret_cast<const char*>(addr::cSC4TrafficSimulator_Name));
    void* const target = self + 4;
    for (uint32_t kind : {0u, 6u, 4u})
        Virtual<uint32_t>(simulators, 0x4C, target, kind, &name, 2u);
    if (!At<void*>(self, 0x18)) {
        At<void*>(self, 0x18) = Global<void*>(addr::SC4_MessageServer);
        Virtual<uint32_t>(At<void*>(self, 0x18), 0x4);
        for (uint32_t type : {0x99EF1142u, 0x99EF1143u, 0x26D31EC1u, 0xE8BDA2E7u, 0xC53D10AAu, 0x8A0D15DAu, 0xEA0D163Eu,
                              0x08DAC3E7u, 0xC2C09554u})
            Virtual<uint8_t>(At<void*>(self, 0x18), 0x14, target, type);
    }
    if (!At<void*>(self, 0x14)) {
        At<void*>(self, 0x14) = Global<void*>(addr::SC4_City);
        Virtual<uint32_t>(At<void*>(self, 0x14), 0x4);
    }
    const int32_t width = Virtual<int32_t>(At<void*>(self, 0x14), 0x174);
    At<int32_t>(self, 0x20) = width;
    const int32_t height = Virtual<int32_t>(At<void*>(self, 0x14), 0x178);
    At<int32_t>(self, 0x24) = height;
    At<int32_t>(self, 0x28) = width - 1;
    At<int32_t>(self, 0x2C) = height - 1;
    At<int32_t>(self, 0x198) = height - 1;
    At<int32_t>(self, 0x194) = width - 1;
    At<int32_t>(self, 0x18C) = 0;
    At<int32_t>(self, 0x190) = 0;
    {
        const uint32_t zero[2] = {};
        const auto assign = reinterpret_cast<void(__thiscall*)(void*, uint32_t, const void*)>(addr::SC4Vector8_Assign);
        assign(self + 0x88, At<uint32_t>(self, 0x24), zero);
        assign(self + 0x94, At<uint32_t>(self, 0x20), zero);
        assign(self + 0xA0, At<uint32_t>(self, 0x24), zero);
        assign(self + 0xAC, At<uint32_t>(self, 0x20), zero);
    }
    if (!At<void*>(self, 0x1C)) {
        At<void*>(self, 0x1C) = Virtual<void*>(At<void*>(self, 0x14), 0xA0);
        Virtual<uint32_t>(At<void*>(self, 0x1C), 0x4);
    }
    const auto grid = [self](std::size_t offset, std::uintptr_t vtable, uint32_t vtable4, uint32_t iid, uint32_t scale,
                             uint32_t id) {
        if (At<void*>(self, offset))
            return;
        At<void*>(self, offset) = NewTrafficGrid(vtable, vtable4, iid);
        Virtual<uint32_t>(At<void*>(self, offset), 0x4);
        Virtual<uint32_t>(At<void*>(self, offset), 0xC);
        Virtual<uint32_t>(At<void*>(self, offset), 0x24, scale);
        Virtual<uint32_t>(At<void*>(self, offset), 0x18, id);
    };
    constexpr std::uintptr_t kBool8 = addr::SC4SimGridBool8_Vtable;
    constexpr std::uintptr_t kFloat32 = addr::SC4SimGridFloat32_Vtable;
    grid(0x30, kBool8, 0x00A878A4, 0x49B9E602, At<uint32_t>(self, 0x970), 0x89D5C3A2);
    grid(0x34, kFloat32, 0x00A87C04, 0x49B9E60A, At<uint32_t>(self, 0x970), 0x69D5C3AC);
    grid(0x38, kBool8, 0x00A878A4, 0x49B9E602, 4, 0xCABD2925);
    grid(0x3C, kBool8, 0x00A878A4, 0x49B9E602, 4, 0xCABD2926);
    grid(0x40, kBool8, 0x00A878A4, 0x49B9E602, 1, 0x69D5C3B5);
    grid(0x44, kBool8, 0x00A878A4, 0x49B9E602, 1, 0x69D5C3BF);
    {
        void** const vector = reinterpret_cast<void**>(self + 0x164);
        auto size = static_cast<int32_t>(static_cast<void**>(vector[1]) - static_cast<void**>(vector[0]));
        if (static_cast<uint32_t>(size) < 6) {
            uint32_t zero = 0;
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, const uint32_t*)>(addr::SC4PtrVector_Fill)(vector, 6, &zero);
            for (; size < 6; ++size) {
                void** const grids = static_cast<void**>(vector[0]);
                grids[size] = NewTrafficGrid(kFloat32, 0x00A87C04, 0x49B9E60A);
                Virtual<uint32_t>(static_cast<void**>(vector[0])[size], 0x4);
                Virtual<uint32_t>(static_cast<void**>(vector[0])[size], 0xC);
                Virtual<uint32_t>(static_cast<void**>(vector[0])[size], 0x24, 1u);
                Virtual<uint32_t>(static_cast<void**>(vector[0])[size], 0x18, 0x69D5C3E0u + static_cast<uint32_t>(size));
            }
        }
    }
    grid(0x170, kFloat32, 0x00A87C04, 0x49B9E60A, 1, 0x69D5C3FA);
    grid(0x48, kBool8, 0x00A878A4, 0x49B9E602, 1, 0x69D5C402);
    grid(0x174, kFloat32, 0x00A87C04, 0x49B9E60A, 1, 0x69D5C40E);
    // The bordered byte grid: (width + 2) rows of (height + 2).
    const uint32_t columns = At<uint32_t>(self, 0x24) + 2;
    const uint32_t rows = At<uint32_t>(self, 0x20) + 2;
    if (static_cast<uint32_t>((At<char**>(self, 0x50) - At<char**>(self, 0x4C)) / 3) != rows) {
        char* emptyRow[3] = {};
        reinterpret_cast<void(__thiscall*)(void*, uint32_t, void*)>(addr::SC4RowVector_Resize)(self + 0x4C, rows, emptyRow);
        const uint8_t zero = 0;
        for (uint32_t row = 0; row < rows; ++row)
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, const uint8_t*)>(addr::SC4ByteVector_Resize)(
                At<char**>(self, 0x4C) + 3 * row, columns, &zero);
    }
    grid(0x700, kBool8, 0x00A878A4, 0x49B9E602, 1, 0x69D5C424);
    grid(0x704, addr::SC4SimGridUint32_Vtable, 0x00A87DB4, 0x49B9E606, 1, 0x69D5C42D);
    // The edge rows: width rows of height chains, all empty.
    {
        const uint32_t edgeColumns = columns - 2;
        const uint32_t edgeRows = rows - 2;
        using RangeFn = void(__thiscall*)(void* vector, void* begin, void* end);
        const auto erase = reinterpret_cast<RangeFn>(addr::SC4EdgeRows_Erase);
        erase(self + field::kEdges, At<void*>(self, field::kEdges), At<void*>(self, 0x80));
        char* emptyRow[3] = {};
        const auto size = static_cast<uint32_t>((At<char**>(self, 0x80) - At<char**>(self, field::kEdges)) / 3);
        if (edgeRows < size)
            erase(self + field::kEdges, At<char**>(self, field::kEdges) + 3 * edgeRows, At<void*>(self, 0x80));
        else
            reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t, void*)>(addr::SC4EdgeRows_FillInsert)(
                self + field::kEdges, At<void*>(self, 0x80), edgeRows - size, emptyRow);
        for (uint32_t row = 0; row < edgeRows; ++row) {
            char** const vector = At<char**>(self, field::kEdges) + 3 * row;
            const auto cells = static_cast<uint32_t>((vector[1] - vector[0]) / 4);
            uint32_t zero = 0;
            if (edgeColumns < cells)
                vector[1] = vector[0] + 4 * edgeColumns;
            else
                reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t, const uint32_t*)>(addr::SC4PtrVector_FillInsert)(
                    vector, vector[1], edgeColumns - cells, &zero);
        }
    }
    // 65,536 records of 46 bytes: nine words 0, nine words 0xFFFF, the rest
    // 0 (the game's template leaves eight of those bytes as its stack held them).
    // With big cities, enough for the city when it is past 256 cells a side
    // (traffic_cells.h).
    {
        int32_t rows = 256;
        g_recordStride = 256;
        if (occmgr_sim::BigCities()) {
            if (width > rows)
                rows = width;
            if (height > g_recordStride)
                g_recordStride = height;
        }
        SetWideCells(width, height);
        const std::size_t bytes = 0x2E * static_cast<std::size_t>(rows) * static_cast<std::size_t>(g_recordStride);
        char* const records = static_cast<char*>(operatorNew(bytes));
        At<char*>(self, 0x58) = records;
        char record[0x2E] = {};
        for (std::size_t i = 0; i < 9; ++i)
            At<uint16_t>(record, 0x12 + 2 * i) = 0xFFFF;
        for (std::size_t offset = 0; offset < bytes; offset += 0x2E)
            std::memcpy(records + offset, record, 0x2E);
    }
    if (!At<void*>(self, 0x6FC)) {
        void* const memory = operatorNew(0x9C8);
        void* generator = nullptr;
        if (memory) {
            uint32_t now = 0;
            reinterpret_cast<uint32_t(__cdecl*)(uint32_t*)>(addr::CRT_time32)(&now);
            const auto* const local = reinterpret_cast<const int32_t*>(
                reinterpret_cast<void*(__cdecl*)(const uint32_t*)>(addr::CRT_localtime)(&now));
            uint32_t date[4] = {};
            reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t, int32_t)>(addr::SC4Date_Construct)(
                date, local[4] + 1, local[3], local[5] + 1900);
            uint32_t moment[4] = {};
            const auto* const seed = reinterpret_cast<const uint32_t*>(
                reinterpret_cast<void*(__thiscall*)(void*, const void*, int32_t, int32_t, int32_t)>(
                    addr::SC4DateTime_Construct)(moment, date, local[2], local[1], local[0]));
            generator = reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::SC4Random_ConstructSeeded)(memory, *seed);
        }
        At<void*>(self, 0x6FC) = generator;
    }
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    const auto label = [](void* item, uint32_t text) { Virtual<uint32_t>(item, 0x1C, 0x6A3FF01Cu, text); };
    if (!At<void*>(self, 0x724)) {
        At<void*>(self, 0x724) = Virtual<void*>(budget, 0x64, 0xC921CEEAu, 0xAA369059u);
        void* const department = At<void*>(self, 0x724);
        Virtual<uint32_t>(department, 0x4);
        Virtual<uint32_t>(At<void*>(self, 0x724), 0x14, 0x6A3FF01Cu, 0x4A493B73u);
        Virtual<uint32_t>(At<void*>(self, 0x724), 0x34, self + 0xB88);
        label(Virtual<void*>(At<void*>(self, 0x724), 0x44, 1u, 0u), 0xEA8446BE);
        label(Virtual<void*>(At<void*>(self, 0x724), 0x44, 2u, 0u), 0x2A8446DA);
        label(Virtual<void*>(At<void*>(self, 0x724), 0x44, 3u, 0u), 0x2A8446DF);
    }
    if (!At<void*>(self, 0x728)) {
        At<void*>(self, 0x728) = Virtual<void*>(budget, 0x64, 0xC921CEEBu, 0xAA369059u);
        Virtual<uint32_t>(At<void*>(self, 0x728), 0x4);
        Virtual<uint32_t>(At<void*>(self, 0x728), 0x14, 0x6A3FF01Cu, 0xEA493B83u);
        Virtual<uint32_t>(At<void*>(self, 0x728), 0x34, self + 0xB8C);
        Virtual<void*>(At<void*>(self, 0x728), 0x44, 1u, 0u);
        label(Virtual<void*>(At<void*>(self, 0x728), 0x50, 1u), 0xEA5F96E7);
        Virtual<void*>(At<void*>(self, 0x728), 0x44, 2u, 0u);
        void* const item = Virtual<void*>(At<void*>(self, 0x728), 0x50, 2u);
        Virtual<uint32_t>(item, 0x14, 1u);
        label(item, 0x8A5F978C);
        label(Virtual<void*>(At<void*>(self, 0x728), 0x44, 3u, 0u), 0x6A8B0CFB);
    }
    const auto ensure = [self, &label](std::size_t department, uint32_t index, uint32_t text) {
        if (Virtual<void*>(At<void*>(self, department), 0x50, index))
            return static_cast<void*>(nullptr);
        void* const item = Virtual<void*>(At<void*>(self, department), 0x44, index, 0u);
        label(item, text);
        return item;
    };
    ensure(0x728, 4, 0x2AB7EA93);
    ensure(0x728, 5, 0x8BCE33E0);
    ensure(0x724, 4, 0xCBCE33D5);
    ensure(0x728, 6, 0x8BCE33E1);
    if (void* const item = ensure(0x724, 5, 0x0C3BF3C4))
        Virtual<uint32_t>(item, 0x14, 1u);
    ensure(0x724, 6, 0xEA5F96E7);
    const auto newIterator = [&operatorNew](bool flagged) {
        auto* const iterator = static_cast<char*>(operatorNew(0x20));
        if (!iterator)
            return iterator;
        if (flagged) {
            iterator[0] = 0;
            iterator[1] = 0;
            At<int32_t>(iterator, 0x4) = 0;
            At<int32_t>(iterator, 0x8) = 0;
            At<int32_t>(iterator, 0xC) = 1;
            At<uint32_t>(iterator, 0x10) = 0x80000000;
            At<uint32_t>(iterator, 0x14) = 0x80000000;
            At<uint32_t>(iterator, 0x18) = 0x7FFFFFFF;
            At<uint32_t>(iterator, 0x1C) = 0x7FFFFFFF;
            return iterator;
        }
        At<int32_t>(iterator, 0xC) = 1;
        At<uint32_t>(iterator, 0x10) = 0x80000000;
        At<uint32_t>(iterator, 0x14) = 0x80000000;
        At<uint32_t>(iterator, 0x18) = 0x7FFFFFFF;
        At<uint32_t>(iterator, 0x1C) = 0x7FFFFFFF;
        At<int32_t>(iterator, 0x4) = 0;
        At<int32_t>(iterator, 0x8) = 0;
        iterator[0] = 0;
        if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
            At<int32_t>(iterator, 0x1C) < 0)
            iterator[0] = 2;
        return iterator;
    };
    const auto bound = [](char* iterator, int32_t x0, int32_t z0, int32_t x1, int32_t z1) {
        At<int32_t>(iterator, 0x10) = x0;
        At<int32_t>(iterator, 0x14) = z0;
        At<int32_t>(iterator, 0x18) = x1;
        At<int32_t>(iterator, 0x1C) = z1;
        if (At<int32_t>(iterator, 0x18) < At<int32_t>(iterator, 0x10))
            At<int32_t>(iterator, 0x18) = At<int32_t>(iterator, 0x10);
        if (At<int32_t>(iterator, 0x1C) < At<int32_t>(iterator, 0x14))
            At<int32_t>(iterator, 0x1C) = At<int32_t>(iterator, 0x14);
    };
    if (!At<void*>(self, 0x818)) {
        char* const iterator = newIterator(false);
        const int32_t x1 = At<int32_t>(self, 0x28);
        const int32_t z1 = At<int32_t>(self, 0x2C);
        At<char*>(self, 0x818) = iterator;
        bound(iterator, 0, 0, x1, z1);
        At<uint32_t>(self, 0x814) = 0;
        At<uint8_t>(self, 0x81C) = 0;
    }
    if (!At<void*>(self, 0x830)) {
        Virtual<uint8_t>(self, 0x18, &At<void*>(self, 0x830)); // slot 6: a path finder
        At<uint8_t>(self, 0x881) = 0;
    }
    if (!At<void*>(self, 0x834))
        Virtual<uint8_t>(self, 0x18, &At<void*>(self, 0x834));
    At<void*>(self, 0x844) = At<void*>(self, 0x830);
    At<void*>(self, 0x848) = At<void*>(self, 0x834);
    if (!At<void*>(self, 0x904))
        At<void*>(self, 0x904) = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x3C);
    if (!At<void*>(self, 0x910)) {
        char* const iterator = newIterator(true);
        const int32_t x0 = At<int32_t>(self, 0x18C) - 1;
        const int32_t x1 = At<int32_t>(self, 0x194) + 1;
        const int32_t z0 = At<int32_t>(self, 0x190) - 1;
        const int32_t z1 = At<int32_t>(self, 0x198) + 1;
        At<char*>(self, 0x910) = iterator;
        bound(iterator, x0, z0, x1, z1);
    }
    if (!At<void*>(self, 0x908)) {
        auto* const walk = static_cast<void**>(operatorNew(0x10));
        if (walk) {
            walk[0] = nullptr;
            walk[1] = nullptr;
            auto** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 12u));
            sentinel[0] = sentinel;
            sentinel[1] = sentinel;
            walk[1] = sentinel;
        }
        At<void**>(self, 0x908) = walk;
        if (!walk[0])
            walk[0] = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x3C);
        Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x40, &walk[1]);
        void** const head = static_cast<void**>(walk[0]);
        if (void** const list = static_cast<void**>(head[1]))
            head[0] = *static_cast<void**>(list[0]);
        walk[2] = *static_cast<void**>(walk[1]);
        walk[3] = nullptr;
    }
    At<uint8_t>(self, 0x888) = 1;
    for (std::size_t i = 0; i < 3; ++i) {
        At<uint32_t>(self, 0x1C0 + 4 * i) = 0;
        At<uint32_t>(self, 0x1CC + 4 * i) = 0;
        At<uint32_t>(self, 0x1D8 + 4 * i) = 0;
    }
    const auto replace = [self](std::size_t offset, void* fresh) {
        void* const old = At<void*>(self, offset);
        if (fresh == old)
            return;
        if (fresh)
            Virtual<uint32_t>(fresh, 0x4);
        At<void*>(self, offset) = fresh;
        if (old)
            Virtual<uint32_t>(old, 0x8);
    };
    {
        void* const memory = operatorNew(0x14);
        replace(0xC4, memory ? (reinterpret_cast<ThisFn>(addr::SC4TrafficObject_Construct)(memory), memory) : nullptr);
    }
    Virtual<uint32_t>(self, 0x4C, At<void*>(self, 0xC4), 3u, 0xFFFFFFFFu); // slot 19
    {
        void* const memory = operatorNew(0x305C);
        replace(0x108, memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4WaterRouteFinder_Construct)(memory) : nullptr);
    }
    reinterpret_cast<ThisFn>(addr::SC4WaterRouteFinder_Init)(At<void*>(self, 0x108));
    At<uint32_t>(self, 0x160) = 0;
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4Stopwatch_ConstructMode)(self + 0x148, 5);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Start)(self + 0x148);
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(name.begin);
    return 1;
}

using InitFn = uint8_t(__fastcall*)(void*, void*);
InitFn g_originalInit = nullptr;

bool InstallInit(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_Init);
    return game::IsBuild641() && entry && hook::Install(*entry, &Init, &g_originalInit);
}

const bool g_registeredInit = registry::Register({
    "simulators.traffic.init",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_Init,
    "Our traffic Init (docs/traffic.md)",
    false,
    InstallInit,
});


// --- CityInited (0x00720430) -------------------------------------------------------

// A deadline that never passes, as the game makes one from an rdtsc read
// (one of the virtual clock's trapped sites): the read less one, plus
// 0x7FFFFFFF in the high half.
Deadline FarDeadline(const void* stack)
{
    const uint64_t now = virtual_clock::ReadTsc(stack);
    const auto low = static_cast<uint32_t>(now);
    const auto high = static_cast<uint32_t>(now >> 32);
    return Deadline{{low - 1, high + 0x7FFFFFFF + (low != 0 ? 1u : 0u)}};
}

// A cell iterator as the game builds one on the stack: step 1 over the
// rectangle, state 2 when it is empty.
struct CellRect {
    uint8_t state;
    uint8_t flag;
    uint8_t padding[2];
    int32_t x, z, step, x0, z0, x1, z1;
};

CellRect MakeCellRect(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    CellRect rect{};
    rect.step = 1;
    rect.x0 = x0;
    rect.z0 = z0;
    rect.x1 = x1 < x0 ? x0 : x1;
    rect.z1 = z1 < z0 ? z0 : z1;
    rect.x = x0;
    rect.z = z0;
    if (x0 > 0 || z0 > 0 || rect.x1 < 0 || rect.z1 < 0)
        rect.state = 2;
    return rect;
}

// The cell iterator at +0x818 back at its start.
void RestartIterator818(void* self)
{
    char* const iterator = At<char*>(self, 0x818);
    At<int32_t>(iterator, 0x4) = 0;
    At<int32_t>(iterator, 0x8) = 0;
    iterator[0] = 0;
    if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
        At<int32_t>(iterator, 0x1C) < 0)
        iterator[0] = 2;
}

// The occupant walk at +0x904 back at its first occupant.
void RestartWalk904(void* self)
{
    void** const walk = At<void**>(self, 0x904);
    if (void** const list = static_cast<void**>(walk[1]))
        walk[0] = *static_cast<void**>(list[0]);
}

void __fastcall CityInited(void* object, void*)
{
    using ThisFn = void(__thiscall*)(void* self);
    using ValueFn = void(__thiscall*)(void* self, void* value);
    using PhaseFn = uint8_t(__thiscall*)(void* self, void* deadline);
    static bool logged = false;
    FirstCall("CityInited", logged);
    char* const self = static_cast<char*>(object);
    ForEachHashNode(self + 0x70C, [self](char* node) {
        reinterpret_cast<ValueFn>(addr::cSC4TrafficSimulator_RefreshCost)(self, node + 8);
    });
    ForEachHashNode(self + 0x70C, [](char* node) {
        if (!((static_cast<uint8_t>(node[0x18]) >> 2) & 1)) {
            node[0x19] = 0;
        } else if (!node[0x19]) {
            node[0x19] = 1;
            uint32_t cell[2] = {};
            Virtual<uint32_t>(At<void*>(node, 0x8), 0x2C, cell);
            node[0x1A] = static_cast<char>(cell[0]);
            node[0x1B] = static_cast<char>(cell[1]);
        }
    });
    reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t, int32_t, int32_t)>(addr::cSC4TrafficSimulator_OnCellsChanged)(
        self, 0, 0, At<int32_t>(self, 0x28), At<int32_t>(self, 0x2C));
    // Every lot announced again (0x007153C0, as message 0x8A0D15DA).
    void* lots = Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 12u);
    static_cast<void**>(lots)[0] = lots;
    static_cast<void**>(lots)[1] = lots;
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x40, &lots);
    for (char* node = At<char*>(lots, 0); node != lots; node = At<char*>(node, 0)) {
        void* const lot = At<void*>(node, 8);
        int32_t x = 0, z = 0;
        Virtual<uint32_t>(lot, 0x18, &x, &z);
        const uint32_t id = Virtual<uint32_t>(lot, 0x14);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t, int32_t, int32_t)>(addr::cSC4TrafficSimulator_OnMessage8A0D15DA)(
            self, id, x, z);
    }
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_RebuildNetworks)(self);
    // The bordered byte grid: cells without traffic cleared.
    {
        CellRect rect{};
        rect.step = 1;
        rect.x0 = -1;
        rect.z0 = -1;
        rect.x1 = At<int32_t>(self, 0x20) < -1 ? -1 : At<int32_t>(self, 0x20);
        rect.z1 = At<int32_t>(self, 0x24) < -1 ? -1 : At<int32_t>(self, 0x24);
        rect.x = -1;
        rect.z = -1;
        using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
        int32_t x = 0, z = 0;
        while (reinterpret_cast<NextFn>(addr::SC4RectIterator_Next)(&rect, &x, &z)) {
            if (!reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t, int32_t)>(addr::cSC4TrafficSimulator_CellHasTraffic)(self, x, z))
                At<uint8_t**>(self, 0x4C)[3 * x + 3][z + 1] = 0;
        }
    }
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_ClearRoadEdges)(self);
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_Phase12)(self);
    // The zones, made again and walked to completion (phase 13's work).
    using ResizeFn = void(__thiscall*)(void* zones, uint32_t count, const void* element);
    reinterpret_cast<ThisFn>(addr::SC4TrafficZones_Clear)(self + 0x64);
    uint32_t element[7] = {};
    reinterpret_cast<ThisFn>(addr::SC4TrafficZone_Construct)(element);
    reinterpret_cast<ResizeFn>(addr::SC4TrafficZones_Resize)(self + 0x64, At<uint16_t>(self, 0x60), element);
    {
        CellRect rect = MakeCellRect(0, 0, At<int32_t>(self, 0x28), At<int32_t>(self, 0x2C));
        using WalkFn = uint8_t(__thiscall*)(void* self, void* deadline, void* iterator);
        for (;;) {
            Deadline deadline = FarDeadline(&rect);
            if (reinterpret_cast<WalkFn>(addr::cSC4TrafficSimulator_WalkCells13)(self, &deadline, &rect))
                break;
        }
    }
    reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4TrafficZones_Assign)(self + 0x70, self + 0x64);
    if (At<uint32_t>(self, field::kPhase) == 12 || At<uint32_t>(self, field::kPhase) == 13)
        At<uint32_t>(self, field::kPhase) = 8;
    {
        using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0
        const float* const funding = Virtual<const float*>(At<void*>(self, step::kDepartment2), 0x28);
        const auto x = static_cast<float>(static_cast<double>(*funding) * 100.0f);
        At<float>(self, 0x720) =
            static_cast<float>(reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(self + 0xD10, x));
    }
    // Once: the buildings of each kind counted at +0x740.
    if (At<uint8_t>(self, 0x19C)) {
        void* buildings = Virtual<void*>(ListNodeAllocator(), 0xC, 12u);
        static_cast<void**>(buildings)[0] = buildings;
        static_cast<void**>(buildings)[1] = buildings;
        const int32_t low[2] = {0, 0};
        const int32_t high[2] = {At<int32_t>(self, 0x28), At<int32_t>(self, 0x2C)};
        Virtual<uint32_t>(Global<void*>(addr::SC4_NetworkService), 0xA4, &buildings, low, high, 0xC772BF98u, 0xFFFFFFFFu);
        for (char* node = At<char*>(buildings, 0); node != buildings; node = At<char*>(node, 0)) {
            void* const occupant = At<void*>(node, 8);
            void* building = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &building)) {
                for (uint32_t kind = 0; kind < 13; ++kind)
                    if (Virtual<uint8_t>(building, 0x58, kind))
                        ++At<uint32_t>(self, 0x740 + 4 * kind);
                Virtual<uint32_t>(building, 0x8);
            }
            Virtual<uint32_t>(occupant, 0x8);
        }
        At<uint8_t>(self, 0x19C) = 0;
        reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4List_Clear2)(&buildings);
        Virtual<uint32_t>(ListNodeAllocator(), 0x10, buildings);
    }
    // An interrupted phase finished or restarted.
    switch (At<uint32_t>(self, field::kPhase)) {
    case 1:
    case 14: {
        reinterpret_cast<void(__cdecl*)(void*, uint32_t, uint32_t)>(addr::SC4_FillUint32)(self + 0x1B0, 0, 4);
        RestartIterator818(self);
        for (;;) {
            Deadline deadline = FarDeadline(&deadline);
            if (reinterpret_cast<PhaseFn>(addr::cSC4TrafficSimulator_Phase5)(self, &deadline))
                break;
        }
        RestartIterator818(self);
        for (;;) {
            Deadline deadline = FarDeadline(&deadline);
            if (reinterpret_cast<PhaseFn>(addr::cSC4TrafficSimulator_Phase10)(self, &deadline))
                break;
        }
        At<uint32_t>(self, field::kPhase) = 0;
        break;
    }
    case 15: {
        RestartWalk904(self);
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18,
                                         static_cast<int32_t>(At<int16_t>(self, 0x90C)),
                                         static_cast<int32_t>(At<int16_t>(self, 0x90E)), 0u);
        if (!lot)
            break;
        void* found = nullptr;
        for (;;) {
            void** const walk = At<void**>(self, 0x904);
            void* const* const head = static_cast<void* const*>(walk[1]);
            if (!head || walk[0] == *head)
                break;
            void** const node = static_cast<void**>(walk[0]);
            walk[0] = node[0];
            found = node[2];
            if (found == lot)
                break;
        }
        if (found != lot)
            RestartWalk904(self);
        break;
    }
    case 16:
    case 17:
    case 18: {
        At<uint32_t>(self, 0x884) = 0;
        using NextFn = uint8_t(__thiscall*)(void* walk, void** lot, uint32_t* x, uint32_t* z);
        reinterpret_cast<ThisFn>(addr::SC4LotWalk_Restart)(At<void*>(self, 0x908));
        void*& current = At<void*>(self, 0x84C);
        for (;;) {
            if (void* const held = current) {
                current = nullptr;
                Virtual<uint32_t>(held, 0x8);
            }
            uint32_t x = 0, z = 0;
            if (!reinterpret_cast<NextFn>(addr::SC4LotWalk_Next)(At<void*>(self, 0x908), &current, &x, &z)) {
                reinterpret_cast<ThisFn>(addr::SC4LotWalk_Restart)(At<void*>(self, 0x908));
                if (void* const held = current) {
                    current = nullptr;
                    Virtual<uint32_t>(held, 0x8);
                }
                break;
            }
            if (static_cast<uint16_t>(x) == At<uint16_t>(self, 0x90C) && static_cast<uint16_t>(z) == At<uint16_t>(self, 0x90E))
                break;
        }
        break;
    }
    case 19: {
        char* const list = At<char*>(self, 0x104);
        for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0))
            node[0x26] = 1;
        At<char*>(self, 0x10C) = At<char*>(At<char*>(self, 0x104), 0);
        At<uint32_t>(self, 0x124) = 0;
        break;
    }
    default: break;
    }
    // The lot buckets (+0x1A0) made again from each lot's property 0x691C863C.
    if (At<uint32_t>(self, 0x1A4)) {
        char* const header = At<char*>(self, 0x1A0);
        for (char* node = At<char*>(header, 0x4); node;) {
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4LotMultimap_EraseSubtree)(self + 0x1A0, At<void*>(node, 0xC));
            char* const left = At<char*>(node, 0x8);
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
            node = left;
        }
        At<char*>(At<char*>(self, 0x1A0), 0x8) = At<char*>(self, 0x1A0);
        At<char*>(At<char*>(self, 0x1A0), 0x4) = nullptr;
        At<char*>(At<char*>(self, 0x1A0), 0xC) = At<char*>(self, 0x1A0);
        At<uint32_t>(self, 0x1A4) = 0;
    }
    {
        auto** const walk = static_cast<void**>(Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x3C));
        using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
        using InsertFn = void(__thiscall*)(void* map, void* result, void* x, void* parent, const void* value, uint32_t flag);
        while (walk[1] && walk[0] != *static_cast<void**>(walk[1])) {
            void** const node = static_cast<void**>(walk[0]);
            walk[0] = node[0];
            void* const lot = node[2];
            uint32_t bucket = 0;
            if (!reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32)(Virtual<void*>(lot, 0xC), 0x691C863C, &bucket))
                continue;
            int32_t x = 0, z = 0;
            Virtual<uint32_t>(lot, 0x54, &x, &z);
            const LotValue value = MakeLotValue(bucket, x, z);
            char* parent = At<char*>(self, 0x1A0);
            for (char* at = At<char*>(parent, 0x4); at;) {
                parent = at;
                at = value.key < At<uint32_t>(at, 0x10) ? At<char*>(at, 0x8) : At<char*>(at, 0xC);
            }
            uint32_t result[2] = {};
            reinterpret_cast<InsertFn>(addr::SC4LotMultimap_Insert)(self + 0x1A0, result, nullptr, parent, &value, 0);
        }
        if (walk[1]) {
            char* const sentinel = At<char*>(static_cast<char*>(walk[1]), 0x4);
            for (char* node = At<char*>(sentinel, 0); node != sentinel;) {
                char* const next = At<char*>(node, 0);
                if (At<void*>(node, 0x8) == walk) {
                    char* const previous = At<char*>(node, 0x4);
                    At<char*>(previous, 0) = next;
                    At<char*>(next, 0x4) = previous;
                    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
                }
                node = next;
            }
        }
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(walk);
    }
    At<uint32_t>(self, 0x92C) = Virtual<uint32_t>(Global<void*>(addr::SC4_LotManager), 0x40) / 18;
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_FinishStats2)(self);
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_FinishStats1)(self);
    reinterpret_cast<ThisFn>(addr::SC4TrafficZone_Destroy)(element);
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4List_ClearReleasing)(&lots);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, lots);
}

using CityInitedFn = void(__fastcall*)(void*, void*);
CityInitedFn g_originalCityInited = nullptr;

bool InstallCityInited(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_OnCityInited);
    return game::IsBuild641() && entry && hook::Install(*entry, &CityInited, &g_originalCityInited);
}

const bool g_registeredCityInited = registry::Register({
    "simulators.traffic.cityinited",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_OnCityInited,
    "Our traffic CityInited handler (docs/traffic.md)",
    false,
    InstallCityInited,
});


// --- Trip routing (phases 17 and 18) ------------------------------------------------

struct Rect16 {
    int32_t values[4];
};

// One cell of a found route, as the route iterator (0x006D6510) gives it.
struct RouteStep {
    int16_t x;
    int16_t z;
    uint8_t type;      // travel type
    uint8_t toType;    // the travel type the route goes on with
    uint8_t unknown;
    uint8_t from;      // side the route enters by (0 -x, 1 -z, 2 +x, 3 +z)
    uint8_t to;        // side it leaves by
    uint8_t joined;    // tested on the following cell: a connection object
};
static_assert(sizeof(RouteStep) == 10);

// 0x0070D780: the side of (x, z) its neighbour (nx, nz) is on, -1 when it
// is not a neighbour (16-bit differences).
int32_t NeighborSide(int32_t x, int32_t z, int32_t nx, int32_t nz)
{
    const auto dx = static_cast<int16_t>(nx - x);
    const auto dz = static_cast<int16_t>(nz - z);
    if (dx == 0)
        return dz == -1 ? 1 : dz == 1 ? 3 : -1;
    if (dz == 0)
        return dx == -1 ? 0 : dx == 1 ? 2 : -1;
    return -1;
}

// Adds to a saturating 16-bit counter.
void AddCapped(uint16_t& counter, uint32_t amount)
{
    const uint32_t sum = counter + amount;
    counter = static_cast<uint16_t>(sum > 0xFFFF ? 0xFFFF : sum);
}

// Adds a route's trips to a connection entry by kind: 0 +0x26, 1 +0x2A,
// 2 both.
void AddToConnection(char* entry, uint32_t kind, uint32_t amount)
{
    if (kind == 0) {
        At<uint16_t>(entry, 0x26) += static_cast<uint16_t>(amount);
    } else if (kind == 1) {
        At<uint16_t>(entry, 0x2A) += static_cast<uint16_t>(amount);
    } else if (kind == 2) {
        At<uint16_t>(entry, 0x26) += static_cast<uint16_t>(amount);
        At<uint16_t>(entry, 0x2A) += static_cast<uint16_t>(amount);
    }
}

// 0x00719910: records a found route: walks its cells, adding the trip's
// count (value, rounded) to the cell densities (+0x178, by type, cell, side
// in and side out; kind 2 also the way back: bit 24 of the key, sides
// swapped), to the per-network maps (+0x164) and edge loads (+0x70C), and to the
// connections at the city edge; sums the travel time into +0x790 and the
// cost into +0x798. Kind 0 and 2 routes start in the rectangle (the
// origin lot), kind 1 at (x, z); kind 1 (a return trip) does not call the
// end handler (0x00714740). An invalid side in mid-route stops without
// the sums.
void* g_originalRecordRoute = nullptr;

void __fastcall RecordRouteImpl(void* self, void*, char* trip, uint32_t value, Rect16 area, uint32_t x, uint32_t z,
                                uint32_t kind, uint32_t flags)
{
    using InitFn = void(__thiscall*)(void* iterator, char* trip);
    using NextFn = uint8_t(__thiscall*)(void* iterator, RouteStep* step);
    using EndFn = void(__thiscall*)(void* self, uint32_t value, uint32_t x, uint32_t z, uint32_t type, uint32_t flags);
    using DensityFn = char*(__thiscall*)(void* table, const uint32_t* key);
    using CostFn = char*(__thiscall*)(void* table, const uint32_t* key);

    uint32_t iterator[6] = {};
    reinterpret_cast<InitFn>(addr::SC4RouteIterator_Init)(iterator, trip);
    void* const edges = Virtual<void*>(At<void*>(self, 0x704), 0x60); // slot 24: the edge-id grid

    const auto count = static_cast<float>(static_cast<double>(value));
    float weighted = count;
    void* const service = Global<void*>(addr::SC4_Service43D90);
    if (Virtual<uint8_t>(service, 0x20, 0xA2BF1DDCu))
        weighted = static_cast<float>(std::ceil(static_cast<double>(count) * static_cast<double>(0.8f)));

    float time = 0.0f;
    float cost = 0.0f;
    RouteStep step;
    if (!reinterpret_cast<NextFn>(addr::SC4RouteIterator_Next)(iterator, &step))
        return;

    int32_t rect[4];
    if (kind != 1) {
        std::memcpy(rect, area.values, sizeof rect);
    } else {
        const auto cx = static_cast<int32_t>(static_cast<int16_t>(x));
        const auto cz = static_cast<int32_t>(static_cast<int16_t>(z));
        rect[0] = cx;
        rect[1] = cz;
        rect[2] = cx;
        rect[3] = cz;
        if (cx >= At<int32_t>(self, 0x18C) && cz >= At<int32_t>(self, 0x190) && cx <= At<int32_t>(self, 0x194) &&
            cz <= At<int32_t>(self, 0x198)) {
            if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cx, cz, 0u))
                Virtual<void>(lot, 0x38, rect); // the lot's cell rectangle
        }
    }

    // The first cell: when it lies along the rectangle's edge on neither or
    // both axes, a connection there takes the trips; its side in is the one
    // facing the rectangle.
    {
        int32_t a = step.x;
        int32_t b = step.z;
        const bool alongX = a == rect[0] - 1 || a == rect[2] + 1;
        const bool alongZ = b == rect[1] - 1 || b == rect[3] + 1;
        if (alongX == alongZ) {
            const auto key = static_cast<uint16_t>(((a & 0xFF) << 8) | static_cast<uint16_t>(b));
            if (char* const node = FindCellNode(static_cast<char*>(self) + 0xF0, key)) {
                char* const object = At<char*>(node, 8) + 8;
                if (char* const entry = FindConnectionPair(object, static_cast<int16_t>(rect[0]), static_cast<int16_t>(rect[1]))) {
                    const float v = step.type == 1 ? weighted : count;
                    time = static_cast<float>(static_cast<double>(v) * At<float>(self, 0xD08));
                    AddToConnection(entry, kind, MagicRound(v));
                }
            }
        }
        if (a < rect[0])
            ++a;
        else if (a > rect[2])
            --a;
        else if (b < rect[1])
            ++b;
        else
            --b;
        step.from = static_cast<uint8_t>(NeighborSide(step.x, step.z, a, b));
    }

    // The iterator leaves the record alone when the route is over, so on
    // the last cell `next` still holds that cell (the original reads it
    // there; for a one-cell route it is whatever was on its stack).
    RouteStep next = step;
    uint32_t lastEdge = 0;
    for (;;) {
        const uint8_t type = step.type;
        const int32_t cx = step.x;
        const int32_t cz = step.z;
        const float v = type == 1 ? weighted : count;
        const uint32_t amount = MagicRound(v);
        const bool inCity = cx >= At<int32_t>(self, 0x18C) && cz >= At<int32_t>(self, 0x190) &&
                            cx <= At<int32_t>(self, 0x194) && cz <= At<int32_t>(self, 0x198);
        const bool last = !reinterpret_cast<NextFn>(addr::SC4RouteIterator_Next)(iterator, &next);
        if (inCity) {
            // The cell's network of this type gets the trips (kind 2 twice).
            const uint32_t network = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkOfTravelType)[type];
            for (char* node = GridCell<char*>(static_cast<char*>(self) + field::kEdges, cx, cz); node;
                 node = At<char*>(node, 0x14)) {
                if (At<uint8_t>(node, 6) != network)
                    continue;
                if (At<uint8_t>(self, 0xC34 + type)) {
                    void* const map = Virtual<void*>(At<void**>(self, 0x164)[network], 0x60);
                    float& cell = GridCell<float>(map, cx, cz);
                    const double sum = static_cast<double>(v) + cell;
                    cell = static_cast<float>(sum);
                    if (kind == 2)
                        cell = static_cast<float>(sum + v);
                }
                break;
            }
            const uint32_t edge = GridCell<uint32_t>(edges, cx, cz);
            if (edge != lastEdge) {
                if (edge) {
                    uint32_t key = edge;
                    char* const load = reinterpret_cast<CostFn>(addr::SC4CostTable_At)(static_cast<char*>(self) + 0x70C, &key);
                    if (!(type == 6 && next.type == 6)) {
                        At<float>(load, 4) = static_cast<float>(static_cast<double>(v) + At<float>(load, 4));
                        cost = static_cast<float>(static_cast<double>(v) * At<float>(load, 0x2C) + cost);
                    }
                }
                lastEdge = edge;
            }

            const uint32_t from = step.from;
            uint32_t to = step.to;
            if (last) {
                if (kind != 1) {
                    to = static_cast<uint32_t>(NeighborSide(cx, cz, static_cast<int16_t>(x), static_cast<int16_t>(z)));
                } else if (cx < area.values[0]) {
                    to = 2;
                } else if (cx > area.values[2]) {
                    to = 0;
                } else {
                    to = cz < area.values[1] ? 3 : 1;
                }
            }
            const bool sides = from != 0xFFFFFFFF && to != 0xFFFFFFFF && from != 0xFF && to != 0xFF;
            if (!last && !sides)
                return;
            if (sides) {
                const uint32_t base = TypedCellKey(0, static_cast<uint32_t>(type), cx, cz);
                uint32_t key = base | (kind == 1 ? 1u << 24 : 0u);
                char* density = reinterpret_cast<DensityFn>(addr::SC4DensityTable_At)(static_cast<char*>(self) + 0x178, &key);
                AddCapped(At<uint16_t>(density, 0x22 + 2 * (from * 4 + to)), amount);
                if (kind == 2) { // the way back: sides swapped
                    key = base | 1u << 24;
                    density = reinterpret_cast<DensityFn>(addr::SC4DensityTable_At)(static_cast<char*>(self) + 0x178, &key);
                    AddCapped(At<uint16_t>(density, 0x22 + 2 * (to * 4 + from)), amount);
                }
            }

            // The next cell is a connection's: the trips go to it.
            if (!last && next.joined) {
                const auto cellKey = static_cast<uint16_t>(((cx & 0xFF) << 8) | cz);
                if (char* const node = FindCellNode(static_cast<char*>(self) + 0xDC, cellKey)) {
                    char* const object = At<char*>(node, 8);
                    At<uint16_t>(object, 0x22) += static_cast<uint16_t>(amount);
                    time = static_cast<float>(static_cast<double>(v) * At<float>(self, 0xD08) + time);
                    if (kind == 2)
                        At<uint16_t>(object, 0x22) += static_cast<uint16_t>(amount);
                    char* const own = object + 8;
                    const auto nextKey = static_cast<uint16_t>(((next.x & 0xFF) << 8) | static_cast<uint16_t>(next.z));
                    if (char* const other = FindCellNode(static_cast<char*>(self) + 0xF0, nextKey)) {
                        char* const target = At<char*>(other, 8) + 8;
                        const auto targetKey = static_cast<uint16_t>(At<uint8_t>(target, 0) << 8 | At<uint8_t>(target, 1));
                        char* const entry = FindConnection(own, targetKey);
                        if (entry && kind == 1) {
                            At<uint16_t>(entry, 0x1E) += static_cast<uint16_t>(amount);
                        } else {
                            if (entry)
                                At<uint16_t>(entry, 0x1A) += static_cast<uint16_t>(amount);
                            if (kind == 2) {
                                if (char* const back = FindConnection(target, cellKey))
                                    At<uint16_t>(back, 0x1E) += static_cast<uint16_t>(amount);
                            }
                        }
                    } else if (char* const entry = FindConnectionPair(own, next.x, next.z)) {
                        AddToConnection(entry, kind, amount);
                    }
                }
            }
        }
        time = static_cast<float>(static_cast<double>(v) * At<float>(self, 0xBA4 + 4 * type) + time);
        if (last)
            break;
        step = next;
    }
    if (kind != 1)
        reinterpret_cast<EndFn>(addr::cSC4TrafficSimulator_RouteEnded)(self, value, x, z, step.type, flags);
    At<double>(self, 0x790) += time;
    At<double>(self, 0x798) += cost;
}

// 0x00719910 with the path finder's rectangle at +0x18 (passed by value).
void RecordRoute(void* self, char* trip, void* finder, uint32_t kind)
{
    Rect16 rect;
    std::memcpy(&rect, static_cast<char*>(finder) + 0x18, sizeof rect);
    const uint32_t value = Virtual<uint32_t>(finder, 0x60); // slot 24
    using RecordFn = void(__thiscall*)(void* self, char* trip, uint32_t value, Rect16 rect, uint32_t x, uint32_t z,
                                       uint32_t kind, uint32_t flags);
    reinterpret_cast<RecordFn>(addr::cSC4TrafficSimulator_RecordRoute)(
        self, trip, value, rect, At<uint16_t>(trip, 0x20), At<uint16_t>(trip, 0x22), kind, static_cast<uint8_t>(trip[0x1C]));
}

// 0x0071C6C0: runs the first path finder (+0x830) for a trip; when its
// route ends off the network (slot 30 false), sets up the second (+0x834)
// from where the first stopped to the trip's end and runs it. +0x48 of the
// lot cell record keeps which search is under way (2: the second). 2 = out
// of time, 0 = no route, else found; found routes are recorded (unless
// bit 1 of +0x1E).
int32_t __fastcall RouteTrip(void* self, void*, char* cell, char* trip)
{
    using ContainsFn = uint8_t(__thiscall*)(void* rect, int32_t x, int32_t z);
    using CellFn = const uint16_t*(__thiscall*)(void* self, int32_t x, int32_t z);
    using NudgeFn = void(__thiscall*)(void* self, int32_t* x, int32_t* z, uint32_t destination);
    static bool logged = false;
    FirstCall("RouteTrip", logged);
    char* const base = static_cast<char*>(self);
    const bool silent = (static_cast<uint8_t>(trip[0x1E]) >> 1) & 1;
    char* const first = At<char*>(self, 0x830);
    char* const second = At<char*>(self, 0x834);
    if (At<int32_t>(cell, 0x48) != 2) {
        const int32_t result = Virtual<int32_t>(first, 0x5C); // slot 23: the search
        if (result == 2) {
            At<int32_t>(cell, 0x48) = 1;
            return 2;
        }
        if (result == 0) {
            At<int32_t>(cell, 0x48) = 0;
            return 0;
        }
        uint32_t x = 0, z = 0;
        Virtual<uint32_t>(first, 0x6C, &x, &z);
        At<uint16_t>(trip, 0x20) = static_cast<uint16_t>(x);
        At<uint16_t>(trip, 0x22) = static_cast<uint16_t>(z);
        if (!cell[0x4D]) {
            Virtual<uint32_t>(first, 0x80, trip, trip + 0x28);
            if (!silent)
                RecordRoute(self, trip, first, 0);
            At<int32_t>(cell, 0x48) = 0;
            return result;
        }
        if (Virtual<uint8_t>(first, 0x78)) {
            trip[0x1E] |= 1;
            Virtual<uint32_t>(first, 0x80, trip, trip + 0x28);
            if (!silent)
                RecordRoute(self, trip, first, 2);
            At<int32_t>(cell, 0x48) = 0;
            return result;
        }
        trip[0x1E] &= ~1;
        const auto contains = reinterpret_cast<ContainsFn>(addr::SC4Rect_Contains);
        const auto networkCell = reinterpret_cast<CellFn>(addr::cSC4TrafficSimulator_NetworkCellAt);
        const auto nudge = reinterpret_cast<NudgeFn>(addr::cSC4TrafficSimulator_NudgeToNetwork);
        // The second search starts where the first stopped (+0x58, +0x5C)...
        if (contains(base + 0x18C, At<int32_t>(first, 0x58), At<int32_t>(first, 0x5C))) {
            if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, At<int32_t>(first, 0x58),
                                                 At<int32_t>(first, 0x5C), 0u))
                Virtual<uint32_t>(lot, 0x38, second + 0x18);
        } else {
            const uint32_t type = static_cast<uint8_t>(At<char*>(first, 0x10)[0x13]);
            int32_t sx = At<int32_t>(first, 0x58);
            int32_t sz = At<int32_t>(first, 0x5C);
            if (type == 1 || type == 2) {
                if (*networkCell(self, sx, sz) & 0x1108)
                    nudge(self, &sx, &sz, 0);
            }
            At<int32_t>(second, 0x18) = sx;
            At<int32_t>(second, 0x1C) = sz;
            At<int32_t>(second, 0x20) = sx;
            At<int32_t>(second, 0x24) = sz;
        }
        // ...and ends at the first's destination (+0x18).
        if (contains(base + 0x18C, At<int32_t>(first, 0x18), At<int32_t>(first, 0x1C))) {
            std::memcpy(second + 0x28, first + 0x18, 0x10);
        } else {
            int32_t dx = At<int32_t>(first, 0x18);
            int32_t dz = At<int32_t>(first, 0x1C);
            const uint16_t* const flags = networkCell(self, dx, dz);
            if (flags && (*flags & 0x1108)) {
                nudge(self, &dx, &dz, 1);
                At<int32_t>(second, 0x28) = dx;
                At<int32_t>(second, 0x2C) = dz;
                At<int32_t>(second, 0x30) = dx;
                At<int32_t>(second, 0x34) = dz;
            } else {
                std::memcpy(second + 0x28, first + 0x18, 0x10);
            }
        }
        Virtual<uint32_t>(second, 0x1C);
        Virtual<uint32_t>(second, 0x14, 1u);
        At<uint32_t>(second, 0x68) = At<uint32_t>(first, 0x6C);
        At<uint32_t>(second, 0x80) = 3;
        At<uint16_t>(second, 0x64) = At<uint16_t>(first, 0x64);
        At<uint16_t>(second, 0x66) = static_cast<uint16_t>(1u << static_cast<uint8_t>(At<char*>(first, 0x10)[0x13]));
        At<uint32_t>(second, 0x90) = At<uint32_t>(first, 0x90);
        const uint16_t avoid = At<uint16_t>(first, 0xEE);
        if (avoid != 0xFFFE) {
            At<uint16_t>(second, 0xEE) = avoid;
            At<uint16_t>(second, 0xF0) = At<uint16_t>(first, 0xF0);
            second[0xF2] = first[0xF2];
            Virtual<uint32_t>(second, 0x14, 8u);
        }
        At<int32_t>(cell, 0x48) = 2;
    }
    const int32_t result = Virtual<int32_t>(second, 0x5C);
    if (result == 2)
        return 2;
    At<int32_t>(cell, 0x48) = 0;
    if (result == 0)
        return 0;
    Virtual<uint32_t>(first, 0x80, trip, trip + 0x28);
    uint32_t more = 0;
    Virtual<uint32_t>(second, 0x80, trip + 0xC, &more);
    At<uint32_t>(trip, 0x28) |= more;
    if (!silent) {
        RecordRoute(self, trip, first, 0);
        RecordRoute(self, trip + 0xC, first, 1);
    }
    return result;
}

// 0x0070EC40: the path finder at +0x8 of the lot cell record set up for a
// trip: its target (slot 18), a start cell or the trip's own lot rectangle
// (+0x1F = 1), or the trip's travel types; the time left; the lot's
// rectangle; the trip scale; and by trip kind (+0x1C) its cost tables.
void __fastcall SetUpTrip(void* self, void*, char* cell, char* trip)
{
    static bool logged = false;
    FirstCall("SetUpTrip", logged);
    char* const base = static_cast<char*>(self);
    void* const finder = At<void*>(cell, 0x8);
    Virtual<uint32_t>(finder, 0x48, At<uint32_t>(trip, 0x18));
    if (trip[0x1F] == 1) {
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x24, 3u);
        const int32_t x = At<int16_t>(trip, 0x20);
        const int32_t z = At<int16_t>(trip, 0x22);
        Rect16 rect{};
        void* lot = nullptr;
        if (x >= At<int32_t>(self, 0x18C) && z >= At<int32_t>(self, 0x190) && x <= At<int32_t>(self, 0x194) &&
            z <= At<int32_t>(self, 0x198))
            lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u);
        if (lot)
            Virtual<uint32_t>(lot, 0x38, &rect);
        else
            rect = Rect16{{x, z, x, z}};
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x50, rect);
    } else {
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x24, 0u);
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x30);
        if ((trip[0x1E] & 2) && !trip[0x1C])
            Virtual<uint32_t>(At<void*>(cell, 0x8), 0x28, 7u);
        else
            Virtual<uint32_t>(At<void*>(cell, 0x8), 0x28, 1u << static_cast<uint8_t>(trip[0x1C]));
    }
    Virtual<uint32_t>(At<void*>(cell, 0x8), 0x54, At<uint32_t>(cell, 0x50));
    Virtual<uint32_t>(At<void*>(cell, 0xC), 0x54, At<uint32_t>(cell, 0x50));
    Rect16 lotRect{};
    const Rect16 area = *Virtual<const Rect16*>(At<void*>(cell, 0x10), 0x14, &lotRect);
    Virtual<uint32_t>(At<void*>(cell, 0x8), 0x4C, area);
    Virtual<uint32_t>(At<void*>(cell, 0x8), 0x1C);
    Virtual<uint32_t>(At<void*>(cell, 0x8), 0x3C);
    Virtual<uint32_t>(At<void*>(cell, 0x8), 0x40, static_cast<void*>(nullptr));
    Virtual<uint32_t>(At<void*>(cell, 0x8), 0x44, At<uint32_t>(self, field::kTripScale));
    const uint8_t kind = static_cast<uint8_t>(trip[0x1C]);
    if (kind <= 2) {
        At<uint32_t>(cell, 0x54) = 0;
        cell[0x4D] = 1;
        if (cell[0x45])
            Virtual<uint32_t>(At<void*>(cell, 0x8), 0x14, 4u);
        if (trip[0x1E] & 4) {
            Virtual<uint32_t>(At<void*>(cell, 0x8), 0x34, 0x1CFu, 0u);
            Virtual<uint32_t>(At<void*>(cell, 0x8), 0x34, 1u << static_cast<uint8_t>(trip[0x1D]), 1u);
            return;
        }
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x34, 0x1CFu, 0u);
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x34, 3u, 1u);
        switch (static_cast<uint8_t>(trip[0x1D])) {
        case 0: Virtual<uint32_t>(At<void*>(cell, 0x8), 0x40, static_cast<void*>(base + 0xC9C)); break;
        case 1: Virtual<uint32_t>(At<void*>(cell, 0x8), 0x40, static_cast<void*>(base + 0xCC0)); break;
        case 2: Virtual<uint32_t>(At<void*>(cell, 0x8), 0x40, static_cast<void*>(base + 0xC78)); break;
        default: break;
        }
        return;
    }
    if (kind == 3) {
        At<uint32_t>(cell, 0x54) = 1;
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x14, 1u);
        Virtual<uint32_t>(At<void*>(cell, 0x8), 0x34, 0x30u, 1u);
        cell[0x4D] = 0;
    }
}

// 0x007136B0: whether the trip's origin can still send it: the building's
// most trips of the kind (slot 20) less those sent (0x0070DFF0) is at
// least the trip's count (+0x18). Outside the city the neighbour
// connection's holder (0x007114E0).
uint8_t __fastcall CanRouteTrip(void* self, void*, char* trip)
{
    using HolderFn = void*(__thiscall*)(void* self, int32_t kind, int32_t x, int32_t z);
    using SentFn = uint32_t(__thiscall*)(void* self, void* holder, uint32_t kind);
    static bool logged = false;
    FirstCall("CanRouteTrip", logged);
    const int32_t x = At<int16_t>(trip, 0x20);
    const int32_t z = At<int16_t>(trip, 0x22);
    void* holder = nullptr;
    if (x >= At<int32_t>(self, 0x18C) && z >= At<int32_t>(self, 0x190) && x <= At<int32_t>(self, 0x194) &&
        z <= At<int32_t>(self, 0x198)) {
        if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u))
            holder = Virtual<void*>(lot, 0xC);
    } else {
        holder = reinterpret_cast<HolderFn>(addr::cSC4TrafficSimulator_NeighborHolder)(self, -1, x, z);
    }
    uint32_t left = 0;
    if (holder) {
        const uint32_t kind = static_cast<uint8_t>(trip[0x1C]);
        const uint32_t most = Virtual<uint32_t>(self, 0x50, holder, kind); // slot 20
        left = most - reinterpret_cast<SentFn>(addr::cSC4TrafficSimulator_GetTripsSent)(self, holder, kind);
    }
    return left < At<uint32_t>(trip, 0x18) ? 0 : 1;
}

// 0x0070F0A0: a lot's trips after routing. Routed trips (3) add their
// count x cost (+0x2C) to a sum and, unless bit 1 of +0x26, to the
// statistics by kind (+0x1CC, +0x1D8, +0x6EC or +0x6F4) and by the type bits
// of +0x30 (the vector at +0x430); unroutable ones (5) add count x the
// trip scale. The average then goes into the lot's cells of the byte grid
// at +0x44 (x 255 / trip scale, 1 to 255; 0 when some trip failed), or for a
// lot outside the city onto the first neighbour connection's property
// 0xCA947077.
void __fastcall FinishLotTrips(void* self, void*, void* lot)
{
    using SetFloatFn = void(__cdecl*)(void* holder, uint32_t property, float value);
    static bool logged = false;
    FirstCall("FinishLotTrips", logged);
    char* trip = At<char*>(*Virtual<char**>(lot, 0xC), 0);
    char* const sentinel = *Virtual<char**>(lot, 0xC);
    if (trip == sentinel)
        return;
    double sum = 0.0f;
    int32_t trips = 0;
    bool failed = false;
    float lastCost = 0.0f; // the game's [esp+0x30]
    const auto unsigned32 = [](uint32_t value) { return static_cast<double>(value); };
    for (; trip != sentinel; trip = At<char*>(trip, 0)) {
        ++At<uint32_t>(self, 0x93C);
        const uint32_t count = At<uint32_t>(trip, 0x20);
        if (trip[0x27] == 3) {
            trip[0x26] |= 8;
            const double cost = unsigned32(count) * At<float>(trip, 0x2C);
            lastCost = static_cast<float>(cost);
            trips += static_cast<int32_t>(count);
            sum += cost;
            if (trip[0x26] & 2)
                continue;
            const uint8_t kind = static_cast<uint8_t>(trip[0x24]);
            if (kind < 3) {
                At<uint32_t>(self, 0x1CC + 4 * kind) += count;
                At<uint32_t>(self, 0x1D8 + 4 * kind) += At<uint32_t>(trip, 0x20);
                At<float>(self, 0x6EC) = static_cast<float>(
                    unsigned32(At<uint32_t>(trip, 0x20)) * At<float>(trip, 0x2C) + At<float>(self, 0x6EC));
                At<uint32_t>(self, 0x6F0) += At<uint32_t>(trip, 0x20);
            } else if (kind == 3) {
                At<float>(self, 0x6F4) = static_cast<float>(static_cast<double>(lastCost) + At<float>(self, 0x6F4));
                At<uint32_t>(self, 0x6F8) += At<uint32_t>(trip, 0x20);
            }
            const uint32_t types = At<uint32_t>(trip, 0x30);
            for (uint32_t bit = 0; bit < 9; ++bit)
                if (types & (1u << bit))
                    At<uint32_t*>(self, 0x430)[bit] += At<uint32_t>(trip, 0x20);
        } else if (trip[0x27] == 5) {
            trip[0x26] &= ~8;
            failed = true;
            const double cost = unsigned32(count) * At<float>(self, field::kTripScale);
            trips += static_cast<int32_t>(count);
            sum += cost;
            const uint8_t kind = static_cast<uint8_t>(trip[0x24]);
            if (kind < 3 && !(trip[0x26] & 2))
                At<uint32_t>(self, 0x1CC + 4 * kind) += count;
            if (static_cast<uint8_t>(trip[0x24]) == 3) {
                At<float>(self, 0x6F4) = static_cast<float>(
                    unsigned32(At<uint32_t>(trip, 0x20)) * At<float>(self, field::kTripScale) + At<float>(self, 0x6F4));
                At<uint32_t>(self, 0x6F8) += At<uint32_t>(trip, 0x20);
            }
        } else {
            trip[0x26] &= ~8;
        }
    }
    if (!trips)
        return;
    const auto average = static_cast<float>(sum / static_cast<double>(trips));
    int32_t rect[4] = {};
    Virtual<uint32_t>(lot, 0x14, rect);
    const int32_t x0 = rect[0], z0 = rect[1];
    if (x0 >= At<int32_t>(self, 0x18C) && z0 >= At<int32_t>(self, 0x190) && x0 <= At<int32_t>(self, 0x194) &&
        z0 <= At<int32_t>(self, 0x198)) {
        uint8_t value = 0;
        if (!failed) {
            const uint32_t scaled =
                MagicRound(static_cast<double>(average) / At<float>(self, field::kTripScale) * 255.0f);
            value = static_cast<uint8_t>(scaled > 0xFF ? 0xFF : (scaled == 0 ? 1 : scaled));
        }
        void* const grid = Virtual<void*>(At<void*>(self, 0x44), 0x60);
        for (int32_t x = rect[0]; x <= rect[2]; ++x)
            for (int32_t z = rect[1]; z <= rect[3]; ++z)
                GridCell<uint8_t>(grid, x, z) = value;
        return;
    }
    for (uint32_t kind = 0; kind < 13; ++kind) {
        void* const occupant = Virtual<void*>(Global<void*>(addr::SC4_MonthEndService), 0x2C, kind, x0, z0);
        if (!occupant)
            continue;
        void* const holder = Virtual<void*>(occupant, 0xC);
        reinterpret_cast<SetFloatFn>(addr::SC4_SetPropertyFloat)(holder, 0xCA947077, average);
        return;
    }
}

// 0x0070F970: the record of a cell just outside the city (8 bytes: word
// flags, word map key, ...): west +0x88 and east +0xA0 by z, north +0x94
// and south +0xAC by x.
uint16_t* __fastcall NetworkCellAt(void* self, void*, int32_t x, int32_t z)
{
    if (x < 0)
        return reinterpret_cast<uint16_t*>(At<char*>(self, 0x88) + 8 * z);
    if (x > At<int32_t>(self, 0x194))
        return reinterpret_cast<uint16_t*>(At<char*>(self, 0xA0) + 8 * z);
    if (z < 0)
        return reinterpret_cast<uint16_t*>(At<char*>(self, 0x94) + 8 * x);
    return reinterpret_cast<uint16_t*>(At<char*>(self, 0xAC) + 8 * x);
}

// 0x0070DD30: a border cell moved one step along the border, the way
// depending on the game's slot 38 / slot 25 setting and on whether it is
// the destination.
void __fastcall NudgeToNetwork(void* self, void*, int32_t* x, int32_t* z, uint8_t destination)
{
    bool forward = true;
    if (void* const setting = Virtual<void*>(reinterpret_cast<void*(__cdecl*)()>(addr::SC4_CurrentApp)(), 0x98, 0u))
        forward = Virtual<uint8_t>(setting, 0x64) == 1;
    if (destination)
        forward = !forward;
    const int32_t lastX = At<int32_t>(self, 0x28);
    const int32_t lastZ = At<int32_t>(self, 0x2C);
    if (*x < 0) {
        *z += forward ? 1 : -1;
    } else if (*x > lastX) {
        *z += forward ? -1 : 1;
    } else if (*z < 0) {
        *x += forward ? -1 : 1;
    } else if (*z > lastZ) {
        *x += forward ? 1 : -1;
    }
}

// 0x007114E0: for a border cell whose flags match the network of a travel
// type (or any, -1), or bit 9: the object of its map key (+0xB8; a stale
// key is cleared), else the default at +0xC4. Null when nothing matches.
void* __fastcall NeighborHolder(void* self, void*, int32_t kind, int32_t x, int32_t z)
{
    using FindFn = char*(__thiscall*)(void* map, const uint16_t* key);
    uint32_t mask = 0xFFFFFFFF;
    if (kind != -1)
        mask = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkMaskOfType)
            [reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkOfTravelType)[kind]];
    uint16_t* const cell = NetworkCellAt(self, nullptr, x, z);
    if (!(mask & cell[0]) && !(cell[0] & 0x200))
        return nullptr;
    if (cell[1]) {
        char* const found = reinterpret_cast<FindFn>(addr::SC4ObjectMap_Find)(static_cast<char*>(self) + 0xB8, &cell[1]);
        if (found != At<char*>(self, 0xB8))
            return At<void*>(found, 0x14);
        cell[1] = 0;
    }
    return At<void*>(self, 0xC4);
}

// 0x0070DFF0: the building's trips of a kind already sent (trip property
// index 4 + kind).
uint32_t __fastcall GetTripsSent(void*, void*, void* holder, uint32_t kind)
{
    return TripProperty(holder, kind + 4, kind + 4);
}

// 0x0070E2B0: what is left of a deadline in milliseconds, at least 1 (a
// virtual clock read site).
int32_t __fastcall DeadlineRemaining(Deadline* deadline, void*)
{
    const uint64_t now = virtual_clock::ReadTsc(deadline);
    const uint64_t end = static_cast<uint64_t>(deadline->end[1]) << 32 | deadline->end[0];
    const auto left = static_cast<int64_t>(end - now);
    const auto ms = static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(left) * Global<float>(addr::SC4Deadline_MsPerTick)));
    return ms > 1 ? ms : 1;
}

// 0x007104A0: outside the city, the first network object (networks 0-12,
// through 0x00B43D8C slot 11) of the travel type's network at an edge
// cell; null inside the city or without one.
void* __fastcall EdgeNetworkObject(void* self, void*, uint32_t type, int32_t x, int32_t z)
{
    if (x >= At<int32_t>(self, 0x18C) && z >= At<int32_t>(self, 0x190) && x <= At<int32_t>(self, 0x194) &&
        z <= At<int32_t>(self, 0x198))
        return nullptr;
    const uint32_t mask = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkMaskOfType)
        [reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkOfTravelType)[type]];
    const uint16_t* const cell = NetworkCellAt(self, nullptr, x, z);
    if (!(mask & cell[0]))
        return nullptr;
    for (uint32_t network = 0; network < 13; ++network) {
        if (!(mask & (1u << network) & cell[0]))
            continue;
        if (void* const object = Virtual<void*>(Global<void*>(addr::SC4_MonthEndService), 0x2C, network, x, z))
            return object;
    }
    return nullptr;
}

// 0x00714740: a kind 0/2 route reached (x, z) with a count of trips: adds
// them to the destination's trips sent (trip value 4 + kind; a neighbour
// connection's too, which also counts them by travel type in property
// 0xAA9EA81A + kind); when that reaches the destination's capacity, clears
// the kind's bit over its cells (+0x4C) and lowers the number of open
// destinations of the kind (+0x1B0).
void __fastcall RouteEnded(void* self, void*, uint32_t value, int32_t x16, int32_t z16, uint32_t type, uint32_t kind)
{
    using InsertFn = void(__thiscall*)(uint32_t** vector, uint32_t* where, uint32_t count, const uint32_t* value);
    using ReadFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t** vector);
    using WriteFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t** vector);
    using FreeFn = void(__cdecl*)(void* memory);
    const auto x = static_cast<int32_t>(static_cast<int16_t>(x16));
    const auto z = static_cast<int32_t>(static_cast<int16_t>(z16));
    void* neighbor = nullptr;
    void* holder = nullptr;
    int32_t rect[4];
    if (x >= At<int32_t>(self, 0x18C) && z >= At<int32_t>(self, 0x190) && x <= At<int32_t>(self, 0x194) &&
        z <= At<int32_t>(self, 0x198)) {
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u);
        if (!lot)
            return;
        holder = Virtual<void*>(lot, 0xC);
        Virtual<void>(lot, 0x38, rect);
    } else {
        const uint8_t travel = static_cast<uint8_t>(type);
        holder = NeighborHolder(self, nullptr, travel, x, z);
        if (void* const object = EdgeNetworkObject(self, nullptr, travel, x, static_cast<int16_t>(z16))) {
            neighbor = Virtual<void*>(object, 0xC);
            if (static_cast<uint8_t>(kind) < 3) {
                uint32_t* counts[3] = {};   // vector: begin, end, capacity
                const uint32_t property = 0xAA9EA81A + static_cast<uint8_t>(kind);
                if (!reinterpret_cast<ReadFn>(addr::SC4_GetPropertyUint32Vector)(neighbor, property, counts)) {
                    const uint32_t zero = 0;
                    const auto size = static_cast<uint32_t>(counts[1] - counts[0]);
                    if (size > 9)
                        counts[1] = counts[0] + 9;
                    else
                        reinterpret_cast<InsertFn>(addr::SC4Uint32Vector_InsertN)(counts, counts[1], 9 - size, &zero);
                }
                counts[0][travel] += value;
                reinterpret_cast<WriteFn>(addr::SC4_SetPropertyUint32Vector)(neighbor, property, counts);
                if (counts[0])
                    reinterpret_cast<FreeFn>(addr::SC4_Free)(counts[0]);
            }
        }
        rect[0] = x;
        rect[1] = z;
        rect[2] = x;
        rect[3] = z;
    }
    if (!holder)
        return;
    const uint8_t which = static_cast<uint8_t>(kind);
    const uint32_t capacity = Virtual<uint32_t>(self, 0x50, holder, static_cast<uint32_t>(which)); // slot 20
    const uint32_t sent = GetTripsSent(self, nullptr, holder, which) + value;
    SetMaxTripCapacity(nullptr, nullptr, holder, which + 4u, sent);
    if (neighbor)
        SetMaxTripCapacity(nullptr, nullptr, neighbor, which + 4u, sent);
    if (sent < capacity)
        return;
    const auto keep = static_cast<uint8_t>(~(1u << (which & 31)));
    for (int32_t cx = rect[0]; cx <= rect[2]; ++cx) {
        for (int32_t cz = rect[1]; cz <= rect[3]; ++cz)
            At<uint8_t**>(self, 0x4C)[3 * cx + 3][cz + 1] &= keep;
    }
    --At<int32_t>(self, 0x1B0 + 4 * which);
}

using NetworkCellFn = uint16_t*(__fastcall*)(void*, void*, int32_t, int32_t);
NetworkCellFn g_originalNetworkCell = nullptr;
using NudgeFn = void(__fastcall*)(void*, void*, int32_t*, int32_t*, uint8_t);
NudgeFn g_originalNudge = nullptr;
using NeighborFn = void*(__fastcall*)(void*, void*, int32_t, int32_t, int32_t);
NeighborFn g_originalNeighbor = nullptr;
using SentFn = uint32_t(__fastcall*)(void*, void*, void*, uint32_t);
SentFn g_originalSent = nullptr;
using RemainingFn = int32_t(__fastcall*)(Deadline*, void*);
RemainingFn g_originalRemaining = nullptr;
void* g_originalEdgeObject = nullptr;
void* g_originalRouteEnded = nullptr;

using CanRouteFn = uint8_t(__fastcall*)(void*, void*, char*);
CanRouteFn g_originalCanRoute = nullptr;
using FinishLotFn = void(__fastcall*)(void*, void*, void*);
FinishLotFn g_originalFinishLot = nullptr;

using SetUpTripFn = void(__fastcall*)(void*, void*, char*, char*);
SetUpTripFn g_originalSetUpTrip = nullptr;

using RouteTripFn = int32_t(__fastcall*)(void*, void*, char*, char*);
RouteTripFn g_originalRouteTrip = nullptr;

bool InstallRoutingHelpers()
{
    const struct {
        std::uintptr_t address;
        void* ours;
        void** original;
    } hooks[] = {
        {addr::cSC4TrafficSimulator_NetworkCellAt, reinterpret_cast<void*>(&NetworkCellAt), reinterpret_cast<void**>(&g_originalNetworkCell)},
        {addr::cSC4TrafficSimulator_NudgeToNetwork, reinterpret_cast<void*>(&NudgeToNetwork), reinterpret_cast<void**>(&g_originalNudge)},
        {addr::cSC4TrafficSimulator_NeighborHolder, reinterpret_cast<void*>(&NeighborHolder), reinterpret_cast<void**>(&g_originalNeighbor)},
        {addr::cSC4TrafficSimulator_GetTripsSent, reinterpret_cast<void*>(&GetTripsSent), reinterpret_cast<void**>(&g_originalSent)},
        {addr::SC4Deadline_Remaining, reinterpret_cast<void*>(&DeadlineRemaining), reinterpret_cast<void**>(&g_originalRemaining)},
        {addr::cSC4TrafficSimulator_EdgeNetworkObject, reinterpret_cast<void*>(&EdgeNetworkObject), &g_originalEdgeObject},
        {addr::cSC4TrafficSimulator_RouteEnded, reinterpret_cast<void*>(&RouteEnded), &g_originalRouteEnded},
        {addr::cSC4TrafficSimulator_RecordRoute, reinterpret_cast<void*>(&RecordRouteImpl), reinterpret_cast<void**>(&g_originalRecordRoute)},
    };
    for (const auto& entry : hooks) {
        if (Skipped(entry.address))
            continue;
        const addr::Entry* found = addr_check::Find(entry.address);
        if (!found || !hook::Install(*found, entry.ours, entry.original))
            return false;
    }
    return true;
}

bool InstallRouting(registry::Mode)
{
    const addr::Entry* route = addr_check::Find(addr::cSC4TrafficSimulator_RouteTrip);
    const addr::Entry* setUp = addr_check::Find(addr::cSC4TrafficSimulator_SetUpTrip);
    const addr::Entry* canRoute = addr_check::Find(addr::cSC4TrafficSimulator_CanRouteTrip);
    const addr::Entry* finish = addr_check::Find(addr::cSC4TrafficSimulator_FinishLotTrips);
    return game::IsBuild641() && route && setUp && canRoute && finish &&
           hook::Install(*route, &RouteTrip, &g_originalRouteTrip) &&
           hook::Install(*setUp, &SetUpTrip, &g_originalSetUpTrip) &&
           hook::Install(*canRoute, &CanRouteTrip, &g_originalCanRoute) &&
           hook::Install(*finish, &FinishLotTrips, &g_originalFinishLot) && InstallRoutingHelpers();
}

const bool g_registeredRouting = registry::Register({
    "simulators.traffic.routing",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_RouteTrip,
    "Our traffic trip routing helpers (docs/traffic.md)",
    false,
    InstallRouting,
});


// --- Helpers ------------------------------------------------------------------

// 0x00716950: appends a congestion effect (three values) to the vector at
// +0x130, through the game's growing insert when it is full.
void __fastcall AddCongestionEffect(void* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    using InsertFn = void(__thiscall*)(void* vector, uint32_t* where, const uint32_t* value, const uint32_t* unused,
                                       uint32_t count, uint32_t one);
    char* const vector = static_cast<char*>(self) + 0x130;
    uint32_t* const last = At<uint32_t*>(vector, 4);
    if (last != At<uint32_t*>(vector, 8)) {
        last[0] = a;
        last[1] = b;
        last[2] = c;
        At<uint32_t*>(vector, 4) = last + 3;
        return;
    }
    const uint32_t value[3] = {a, b, c};
    reinterpret_cast<InsertFn>(addr::SC4Uint32x3Vector_Insert)(vector, last, value, &a, 1, 1);
}

// 0x0070DDD0: for a cell outside exactly one side of the city (+0x28,
// +0x2C), the side and the nearest edge cell; -1 inside or at a corner.
int32_t __fastcall EdgeSide(void* self, void*, int32_t x, int32_t z, int32_t* edgeX, int32_t* edgeZ)
{
    const bool outX = x < 0 || x > At<int32_t>(self, 0x28);
    const bool outZ = z < 0 || z > At<int32_t>(self, 0x2C);
    if (outX == outZ)
        return -1;
    *edgeX = x;
    *edgeZ = z;
    if (outX) {
        if (x < 0) {
            *edgeX = 0;
            return 0;
        }
        *edgeX = At<int32_t>(self, 0x28);
        return 2;
    }
    if (z < 0) {
        *edgeZ = 0;
        return 1;
    }
    *edgeZ = At<int32_t>(self, 0x2C);
    return 3;
}

// 0x0070FA40: whether a cell outside the city carries traffic: a bit of
// its side in the edge cell's 46-byte record (+0x58; nine types x four
// commutes), or bit 9 of its border record.
uint8_t __fastcall CellHasTraffic(void* self, void*, int32_t x, int32_t z)
{
    int32_t edgeX = 0, edgeZ = 0;
    const int32_t side = EdgeSide(self, nullptr, x, z, &edgeX, &edgeZ);
    if (side == -1)
        return 0;
    const auto* const record = reinterpret_cast<const uint16_t*>(CellRecord(self, edgeX, edgeZ));
    for (int type = 0; type < 9; ++type) {
        for (int commute = 0; commute < 4; ++commute) {
            if (record[type] & (1u << ((side + 4 * commute) & 31)))
                return 1;
        }
    }
    const uint16_t* const cell = NetworkCellAt(self, nullptr, x, z);
    return cell && (cell[0] & 0x200) ? 1 : 0;
}

// 0x0070FCE0: clears the two counters (+0xC, +0x10) of every edge in the
// city and sets +0x60 to 1.
void __fastcall ClearRoadEdges(void* self, void*)
{
    At<uint16_t>(self, 0x60) = 1;
    for (int32_t x = 0; x <= At<int32_t>(self, 0x194); ++x) {
        for (int32_t z = 0; z <= At<int32_t>(self, 0x198); ++z) {
            for (char* node = GridCell<char*>(static_cast<char*>(self) + field::kEdges, x, z); node;
                 node = At<char*>(node, 0x14)) {
                At<uint32_t>(node, 0xC) = 0;
                At<uint32_t>(node, 0x10) = 0;
            }
        }
    }
}

// 0x007206BD calls 0x007183B0 right after ClearRoadEdges without setting
// ECX again: the game's whole-program build knew ClearRoadEdges keeps it.
// The thunk keeps ECX and EDX.
__declspec(naked) void ClearRoadEdgesThunk()
{
    __asm push ecx
    __asm push edx
    __asm call ClearRoadEdges
    __asm pop edx
    __asm pop ecx
    __asm ret
}

// Budget line items: department slot 20 (index) gives the item, whose
// slot 12 takes the month's amount (int64).
void* LineItem(void* department, uint32_t index)
{
    return Virtual<void*>(department, 0x50, index);
}

void SetAmount(void* item, uint64_t amount)
{
    Virtual<void>(item, 0x30, static_cast<uint32_t>(amount), static_cast<uint32_t>(amount >> 32));
}

// A count x a cost, rounded the game's way.
uint32_t Cost(void* self, std::size_t count, std::size_t cost)
{
    return MagicRound(static_cast<double>(At<uint32_t>(self, count)) * At<float>(self, cost));
}

// 0x0070D8A0: department 1's costs (+0x724): item 2 = +0x740 x +0xB54 and
// +0x768 x +0xB7C, item 1 = +0x74C x +0xB60, item 3 = +0x748 x +0xB5C and
// +0x770 x +0xB84, item 4 = +0x758 x +0xB6C.
void __fastcall FinishStats1(void* self, void*)
{
    void* const department = At<void*>(self, 0x724);
    const uint32_t a = Cost(self, 0x740, 0xB54);
    const uint32_t b = Cost(self, 0x768, 0xB7C);
    SetAmount(LineItem(department, 2), static_cast<uint64_t>(b) + a);
    const uint32_t c = Cost(self, 0x74C, 0xB60);
    SetAmount(LineItem(department, 1), c);
    const uint32_t d = Cost(self, 0x748, 0xB5C);
    const uint32_t e = Cost(self, 0x770, 0xB84);
    SetAmount(LineItem(department, 3), static_cast<uint64_t>(e) + d);
    const uint32_t f = Cost(self, 0x758, 0xB6C);
    SetAmount(LineItem(department, 4), f);
}

// 0x0070DA40: department 2's costs (+0x728): item 3 = +0x744 x +0xB58
// (added with IDs 0x6A3FF01C, 0x6A8B0CFB when missing), items 4-6 =
// +0x75C x +0xB70, +0x760 x +0xB74, +0x764 x +0xB78.
void __fastcall FinishStats2(void* self, void*)
{
    void* const department = At<void*>(self, 0x728);
    const uint32_t a = Cost(self, 0x744, 0xB58);
    void* item = LineItem(department, 3);
    if (!item) {
        item = Virtual<void*>(department, 0x44, 3u, 0u);
        Virtual<uint32_t>(item, 0x1C, 0x6A3FF01Cu, 0x6A8B0CFBu);
    }
    SetAmount(item, a);
    void* const item4 = LineItem(department, 4);
    SetAmount(item4, Cost(self, 0x75C, 0xB70));
    void* const item5 = LineItem(department, 5);
    SetAmount(item5, Cost(self, 0x760, 0xB74));
    void* const item6 = LineItem(department, 6);
    SetAmount(item6, Cost(self, 0x764, 0xB78));
}

// 0x0070EA70: a kind 7 building added (count +0x75C up) or removed (down,
// not below 0); then department 2's costs.
void __fastcall OnKind7Building(void* self, void*, void*, uint8_t added)
{
    uint32_t& count = At<uint32_t>(self, 0x75C);
    if (added)
        ++count;
    else if (count)
        --count;
    FinishStats2(self, nullptr);
}

// 0x00714940: the busiest network at a cell for a commute: the counts
// (+0x28) of the cell's density records (+0x178, key commute << 24 | type
// << 16 | x << 8 | z) of the travel types on networks 1, 4 and 5, summed
// by network into six uint16 counters, the largest returned.
uint16_t __fastcall CellTrafficPeak(void* self, void*, int32_t x, int32_t z, uint32_t commute)
{
    using InsertFn = void(__thiscall*)(uint16_t** vector, uint16_t* where, uint32_t count, const uint16_t* value);
    using FreeFn = void(__cdecl*)(void* memory);
    uint16_t* sums[3] = {};
    const uint16_t zero = 0;
    reinterpret_cast<InsertFn>(addr::SC4Uint16Vector_InsertN)(sums, nullptr, 6, &zero);
    const auto* const networks = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkOfTravelType);
    char** const buckets = At<char**>(self, 0x17C);
    const auto count = static_cast<uint32_t>(At<char**>(self, 0x180) - buckets);
    for (uint32_t type = 0; type < 9; ++type) {
        const uint32_t network = networks[type];
        if (network != 1 && !(network > 3 && network <= 5))
            continue;
        const uint32_t key = TypedCellKey(static_cast<uint8_t>(commute), type, x, z);
        for (char* node = buckets[key % count]; node; node = At<char*>(node, 0)) {
            if (At<uint32_t>(node, 4) == key) {
                sums[0][network] = static_cast<uint16_t>(sums[0][network] + At<uint16_t>(node, 0x28));
                break;
            }
        }
    }
    uint16_t peak = 0;
    for (int i = 0; i < 6; ++i) {
        if (sums[0][i] > peak)
            peak = sums[0][i];
    }
    reinterpret_cast<FreeFn>(addr::SC4_Free)(sums[0]);
    return peak;
}

// 0x00714AA0: the traffic around a 4x4 block (bx, bz): the largest cell
// peak (0x00714940) of each commute over the block's cells and, in the
// block's own rows, two cells either side (rows two either side too, up
// to +0x24), the two summed.
uint32_t __fastcall BlockTraffic(void* self, void*, int32_t bx, int32_t bz)
{
    const int32_t x0 = 4 * bx;
    const int32_t z0 = 4 * bz;
    const int32_t zEnd = At<int32_t>(self, 0x24) < z0 + 6 ? At<int32_t>(self, 0x24) : z0 + 6;
    uint16_t morning = 0;
    uint16_t evening = 0;
    for (int32_t z = z0 - 2 < 0 ? 0 : z0 - 2; z < zEnd; ++z) {
        int32_t xStart = x0;
        int32_t xEnd = x0 + 4;
        if (z >= z0 && z < z0 + 4) {
            xStart = x0 - 2 < 0 ? 0 : x0 - 2;
            xEnd = x0 + 6;
        }
        for (int32_t x = xStart; x < xEnd; ++x) {
            const uint16_t first = CellTrafficPeak(self, nullptr, x, z, 0);
            const uint16_t second = CellTrafficPeak(self, nullptr, x, z, 1);
            if (first >= morning)
                morning = first;
            if (second >= evening)
                evening = second;
        }
    }
    return static_cast<uint32_t>(evening) + morning;
}

// 0x00713AB0 (message 0xEA0D163E): a network bit cleared from a border
// cell; when none is left, the cell's map key is cleared, the networks
// rebuilt (0x007138B0) and the cell's trip-kind bits (+0x4C) dropped from
// the open destination counts (+0x1B0-0x1BC).
void __fastcall ClearBorderNetwork(void* self, void*, uint32_t bit, int32_t x, int32_t z)
{
    using ThisFn = void(__thiscall*)(void* self);
    uint16_t* const cell = NetworkCellAt(self, nullptr, x, z);
    if (!cell)
        return;
    cell[0] &= static_cast<uint16_t>(~(1u << (bit & 31)));
    if (cell[0])
        return;
    cell[1] = 0;
    reinterpret_cast<ThisFn>(addr::cSC4TrafficSimulator_RebuildNetworks)(self);
    uint8_t& kinds = At<uint8_t**>(self, 0x4C)[3 * x + 3][z + 1];
    for (uint32_t kind = 0; kind < 4; ++kind) {
        if (kinds & (1u << kind))
            --At<int32_t>(self, 0x1B0 + 4 * kind);
    }
    kinds = 0;
}

// Posts message `type` for a cell (message slots 11 and 16: x, z; slot 21:
// 0) through the message server at +0x18.
void PostCellMessage(void* self, uint32_t type, uint32_t x, uint32_t z)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    Virtual<uint32_t>(message, 0x2C, x);
    Virtual<uint32_t>(message, 0x40, z);
    Virtual<uint32_t>(message, 0x54, 0u);
    Virtual<uint32_t>(At<void*>(self, 0x18), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// A building's cell entering or leaving a pair of uint16 cell sets: when
// added, into the first (and +count up); when removed, out of the first,
// or else out of the second (then +second down, +0x318 down and message
// 0x69247DC8 for the cell); +count down either way.
void UpdateBuildingCells(void* self, bool added, uint32_t x, uint32_t z, std::size_t first, std::size_t second,
                         std::size_t count, std::size_t secondCount)
{
    using ReserveFn = void(__thiscall*)(void* set, uint32_t count);
    using InsertFn = void(__thiscall*)(void* set, void* result, const uint16_t* key);
    using EraseFn = void(__thiscall*)(void* set, const void* const* iterator);
    char* const base = static_cast<char*>(self);
    if (g_wideCells) {
        const uint32_t cell = CellKey(static_cast<int32_t>(x), static_cast<int32_t>(z));
        if (added) {
            WideSet(first).insert(cell);
            ++At<uint32_t>(self, count);
            return;
        }
        if (!WideSet(first).erase(cell) && WideSet(second).erase(cell)) {
            --At<uint32_t>(self, secondCount);
            --At<uint16_t>(self, 0x318);
            PostCellMessage(self, 0x69247DC8, x, z);
        }
        --At<uint32_t>(self, count);
        return;
    }
    const auto key = static_cast<uint16_t>(((x & 0xFF) << 8) | z);
    if (added) {
        uint32_t result[3];
        reinterpret_cast<ReserveFn>(addr::SC4Uint16Set_Reserve)(base + first, At<uint32_t>(base + first, 0x10) + 1);
        reinterpret_cast<InsertFn>(addr::SC4Uint16Set_Insert)(base + first, result, &key);
        ++At<uint32_t>(self, count);
        return;
    }
    if (char* const node = FindCellNode(base + first, key)) {
        const void* const iterator[2] = {node, base + first};
        reinterpret_cast<EraseFn>(addr::SC4Uint16Set_Erase)(base + first, iterator);
    } else if (char* const other = FindCellNode(base + second, key)) {
        const void* const iterator[2] = {other, base + second};
        reinterpret_cast<EraseFn>(addr::SC4Uint16Set_Erase)(base + second, iterator);
        --At<uint32_t>(self, secondCount);
        --At<uint16_t>(self, 0x318);
        PostCellMessage(self, 0x69247DC8, x, z);
    }
    --At<uint32_t>(self, count);
}

// 0x007155D0: a building of a counted kind (+0x740 by kind) added or
// removed; department 1's costs; then, except kinds 2 and 12, its cell
// (position through +0x14 slot 95) in the sets +0x7B8 (count +0x7AC) and
// +0x7CC (count +0x7B0).
void __fastcall OnBuildingOfKind(void* self, void*, void* occupant, uint8_t added, uint32_t kind)
{
    float position[3];
    Virtual<uint32_t>(occupant, 0x20, position);
    int32_t x = 0, z = 0;
    Virtual<uint32_t>(At<void*>(self, 0x14), 0x17C, position[0], position[2], &x, &z);
    uint32_t& counted = At<uint32_t>(self, 0x740 + 4 * kind);
    if (added)
        ++counted;
    else if (counted)
        --counted;
    FinishStats1(self, nullptr);
    if (kind == 2 || kind == 12)
        return;
    UpdateBuildingCells(self, added != 0, static_cast<uint32_t>(x), static_cast<uint32_t>(z), 0x7B8, 0x7CC, 0x7AC, 0x7B0);
}

// 0x00715790: a kind 1 building added or removed (count +0x744); unless its
// slot 9 says 9, its cell (slot 59) in the sets +0x7EC (count +0x7E0) and
// +0x800 (count +0x7E4); then department 2's costs.
void __fastcall OnKind1Building(void* self, void*, void* occupant, uint8_t added)
{
    Virtual<void*>(occupant, 0xC0);
    uint32_t& counted = At<uint32_t>(self, 0x744);
    if (added)
        ++counted;
    else if (counted)
        --counted;
    if (!Virtual<uint8_t>(occupant, 0x24, 9u)) {
        int32_t x = 0, z = 0;
        Virtual<uint32_t>(occupant, 0xEC, &x, &z);
        UpdateBuildingCells(self, added != 0, static_cast<uint32_t>(x), static_cast<uint32_t>(z), 0x7EC, 0x800, 0x7E0, 0x7E4);
    }
    FinishStats2(self, nullptr);
}

// The next node of a uint16 hash set (node: next, key at +4), in bucket
// order; null at the end.
char* NextHashNode(void* set, char* node)
{
    if (char* const next = At<char*>(node, 0))
        return next;
    char** const buckets = At<char**>(set, 4);
    const auto count = static_cast<uint32_t>(At<char**>(set, 8) - buckets);
    for (uint32_t i = At<uint16_t>(node, 4) % count + 1; i < count; ++i) {
        if (buckets[i])
            return buckets[i];
    }
    return nullptr;
}

// 0x00715FB0: moves up to |count| random cells of a uint16 cell set to the
// other (count > 0: first to second, the edges of the network type on
// those cells made active with +0xB90; else back, made inactive), each
// announced with message 0x69247DC8 (x, z, active).
void __fastcall ChangeActive(void* self, void*, void* first, void* second, int32_t count, uint32_t network)
{
    using ReserveFn = void(__thiscall*)(void* set, uint32_t count);
    using InsertFn = void(__thiscall*)(void* set, void* result, const uint16_t* key);
    using EraseFn = void(__thiscall*)(void* set, const void* const* iterator);
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t limit);
    const bool activate = count > 0;
    void* from = first;
    void* to = second;
    auto moves = static_cast<uint32_t>(count);
    if (!activate) {
        from = second;
        to = first;
        moves = static_cast<uint32_t>(-count);
    }
    // A big city's cells are in WideSet instead of the game's sets.
    std::set<uint32_t>* const wideFrom =
        g_wideCells ? &WideSet(static_cast<std::size_t>(static_cast<char*>(from) - static_cast<char*>(self))) : nullptr;
    std::set<uint32_t>* const wideTo =
        g_wideCells ? &WideSet(static_cast<std::size_t>(static_cast<char*>(to) - static_cast<char*>(self))) : nullptr;
    const auto size = [&]() { return wideFrom ? static_cast<uint32_t>(wideFrom->size()) : At<uint32_t>(from, 0x10); };
    if (moves > size())
        moves = size();
    while (moves) {
        const uint32_t pick = reinterpret_cast<BelowFn>(addr::SC4Random_Below)(At<void*>(self, field::kRandom), size());
        char* node = nullptr;
        uint16_t key = 0;
        uint32_t x = 0, z = 0;
        if (wideFrom) {
            auto it = wideFrom->begin();
            std::advance(it, pick);
            x = static_cast<uint32_t>(KeyX(*it));
            z = static_cast<uint32_t>(KeyZ(*it));
        } else {
            char** const buckets = At<char**>(from, 4);
            const auto bucketCount = static_cast<uint32_t>(At<char**>(from, 8) - buckets);
            for (uint32_t i = 0; i < bucketCount && !node; ++i)
                node = buckets[i];
            for (uint32_t i = 0; i < pick; ++i)
                node = NextHashNode(from, node);
            key = At<uint16_t>(node, 4);
            x = key >> 8;
            z = key & 0xFF;
        }
        char* edge = GridCell<char*>(static_cast<char*>(self) + field::kEdges, static_cast<int32_t>(x), static_cast<int32_t>(z));
        while (edge && At<uint8_t>(edge, 6) != network)
            edge = At<char*>(edge, 0x14);
        if (edge) {
            At<uint32_t>(edge, 8) = activate ? At<uint32_t>(self, 0xB90) : 0;
            using NewFn = void*(__cdecl*)(std::size_t size);
            using MessageFn = void*(__thiscall*)(void* memory);
            void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
            void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
            Virtual<uint32_t>(message, 0x4);
            Virtual<uint32_t>(message, 0x14, 0x69247DC8u);
            Virtual<uint32_t>(message, 0x2C, x);
            Virtual<uint32_t>(message, 0x40, z);
            Virtual<uint32_t>(message, 0x54, activate ? 1u : 0u);
            Virtual<uint32_t>(At<void*>(self, 0x18), 0x10, message, 0u);
            Virtual<uint32_t>(message, 0x8);
        }
        if (wideFrom) {
            const uint32_t cell = CellKey(static_cast<int32_t>(x), static_cast<int32_t>(z));
            wideFrom->erase(cell);
            wideTo->insert(cell);
            --moves;
            continue;
        }
        const void* const iterator[2] = {node, from};
        reinterpret_cast<EraseFn>(addr::SC4Uint16Set_Erase)(from, iterator);
        uint32_t result[3];
        reinterpret_cast<ReserveFn>(addr::SC4Uint16Set_Reserve)(to, At<uint32_t>(to, 0x10) + 1);
        reinterpret_cast<InsertFn>(addr::SC4Uint16Set_Insert)(to, result, &key);
        --moves;
    }
}

// The in-order successor in a red-black tree (head: parent = root).
char* TreeNext(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (At<char*>(right, 8))
            right = At<char*>(right, 8);
        return right;
    }
    char* parent = At<char*>(node, 4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 4);
    }
    return At<char*>(node, 0xC) != parent ? parent : node;
}

// 0x007138B0: drops the objects of the map at +0xB8 (count +0xBC) that no
// border cell (the four border vectors from +0x88, entries 0 to +0x194
// less one) refers to any more, releasing each.
void __fastcall RebuildNetworks(void* self, void*)
{
    using AllocateFn = char*(__cdecl*)(std::size_t size);
    using InsertFn = void(__thiscall*)(void* set, void* result, const uint16_t* key);
    using RebalanceFn = char*(__cdecl*)(char* node, char** root, char** leftmost, char** rightmost);
    using EraseSubtreeFn = void(__thiscall*)(void* set, char* node);
    using FreeFn = void(__cdecl*)(void* memory);
    struct {
        char* head;
        uint32_t size;
    } keys{};
    char* const head = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x14);
    keys.head = head;
    keys.size = 0;
    head[0] = 0;
    At<char*>(head, 4) = nullptr;
    At<char*>(head, 8) = head;
    At<char*>(head, 0xC) = head;
    for (std::size_t border = 0x88; border <= 0xAC; border += 0xC) {
        for (int32_t i = 0; i < At<int32_t>(self, 0x194); ++i) {
            const uint16_t* const key = reinterpret_cast<const uint16_t*>(At<char*>(self, border) + 8 * i + 2);
            if (*key) {
                uint32_t result[2];
                reinterpret_cast<InsertFn>(addr::SC4Uint16TreeSet_Insert)(&keys, result, key);
            }
        }
    }
    char* const map = At<char*>(self, 0xB8);
    for (char* node = At<char*>(map, 8); node != At<char*>(self, 0xB8);) {
        const uint16_t key = At<uint16_t>(node, 0x10);
        char* best = head;
        for (char* at = At<char*>(head, 4); at;) {
            if (At<uint16_t>(at, 0x10) < key) {
                at = At<char*>(at, 0xC);
            } else {
                best = at;
                at = At<char*>(at, 8);
            }
        }
        if (best != head && !(key < At<uint16_t>(best, 0x10))) {
            node = TreeNext(node);
            continue;
        }
        Virtual<uint32_t>(At<void*>(node, 0x14), 0x8); // Release
        char* const next = TreeNext(node);
        char* const map2 = At<char*>(self, 0xB8);
        if (char* const freed = reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(
                node, reinterpret_cast<char**>(map2 + 4), reinterpret_cast<char**>(map2 + 8),
                reinterpret_cast<char**>(map2 + 0xC)))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(freed);
        --At<uint32_t>(self, 0xBC);
        node = next;
    }
    if (keys.size) {
        for (char* node = At<char*>(keys.head, 4); node;) {
            reinterpret_cast<EraseSubtreeFn>(addr::SC4Uint16TreeSet_EraseSubtree)(&keys, At<char*>(node, 0xC));
            char* const left = At<char*>(node, 8);
            reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
            node = left;
        }
        At<char*>(keys.head, 8) = keys.head;
        At<char*>(keys.head, 4) = nullptr;
        At<char*>(keys.head, 0xC) = keys.head;
        keys.size = 0;
    }
    if (keys.head)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(keys.head);
}

// 0x0071C0C0 (phase 19): the ferry route from an entry of +0x104 (its cell
// at +4/+5) to a cell just outside the city: the water route finder
// (+0x108) from the entry to the nearest edge cell; found, the entry's
// route record for the cell (list at +0x14, created when missing, its
// counters cleared) gets the cells, cost x 0.01 and both byte vectors; not
// found, an existing record is erased.
void __fastcall RouteToCell19(void* self, void*, char* entry, int32_t x, int32_t z)
{
    using FindFn = uint8_t(__thiscall*)(void* finder, uint32_t, uint32_t, int32_t, int32_t, int32_t* cost, void* route,
                                        void* second);
    using PushFn = void(__thiscall*)(void* list, const void* record);
    using EraseFn = void(__thiscall*)(void* list, void* result, char* where);
    using AssignFn = void(__thiscall*)(void* to, const void* from);
    using FreeFn = void(__cdecl*)(void* memory);
    int32_t edgeX = 0, edgeZ = 0;
    if (EdgeSide(self, nullptr, x, z, &edgeX, &edgeZ) == -1)
        return;
    char** const list = reinterpret_cast<char**>(entry + 0x14);
    char* const head = *list;
    char* node = At<char*>(head, 0);
    while (node != head && !(At<int16_t>(node, 8) == x && At<int16_t>(node, 0xA) == z))
        node = At<char*>(node, 0);
    int32_t cost = 0;
    uint8_t* route[3] = {};
    uint8_t* second[3] = {};
    const bool found = reinterpret_cast<FindFn>(addr::SC4WaterRouteFinder_Find)(
        At<void*>(self, 0x108), At<uint8_t>(entry, 4), At<uint8_t>(entry, 5), edgeX, edgeZ, &cost, route, second);
    if (found) {
        char* record;
        if (node == head) {
            uint32_t blank[11] = {};
            reinterpret_cast<PushFn>(addr::SC4FerryRouteList_PushBack)(list, blank);
            record = At<char*>(*list, 4) + 8;
            At<uint16_t>(record, 0x28) = 0;
            At<uint16_t>(record, 0x24) = 0;
            At<uint16_t>(record, 0x2A) = 0;
            At<uint16_t>(record, 0x26) = 0;
        } else {
            record = node + 8;
        }
        At<uint16_t>(record, 2) = static_cast<uint16_t>(z);
        At<uint16_t>(record, 0) = static_cast<uint16_t>(x);
        At<float>(record, 8) = static_cast<float>(static_cast<double>(cost) * Global<float>(addr::SC4Traffic_FerryCostScale));
        At<uint16_t>(record, 4) = static_cast<uint16_t>(edgeX);
        At<uint16_t>(record, 6) = static_cast<uint16_t>(edgeZ);
        reinterpret_cast<AssignFn>(addr::SC4ByteVector_Assign)(record + 0xC, route);
        reinterpret_cast<AssignFn>(addr::SC4ByteVector_Assign)(record + 0x18, second);
    } else if (node != head) {
        char* result = nullptr;
        reinterpret_cast<EraseFn>(addr::SC4FerryRouteList_Erase)(list, &result, node);
    }
    if (second[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(second[0]);
    if (route[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(route[0]);
}

// 0x007153C0 (message 0x8A0D15DA): a network's object at a cell just
// outside the city (0x00B43D8C slot 11): the network's bit in the border
// record, its trip scale raised to the neighbour's property 0xCA947077
// (the simulator's own at +0xC54 by default); and the record's map key
// (+2): the object at +0xB8 whose properties 0x4A23576B/C match the
// connection's pair (slot 9, slot 43, slot 5), or a new one under the
// lowest free key; 0 without a connection.
void __fastcall OnNetworkAtEdge(void* self, void*, uint32_t network, int32_t x, int32_t z)
{
    using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
    using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
    using SetFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
    using NewFn = void*(__cdecl*)(std::size_t size);
    using ThisFn = void*(__thiscall*)(void* self);
    using AtFn = void*(__thiscall*)(void* map, const uint16_t* key);
    void* const service = Global<void*>(addr::SC4_MonthEndService);
    void* const object = Virtual<void*>(service, 0x2C, network, x, z);
    if (!object)
        return;
    uint16_t* const cell = NetworkCellAt(self, nullptr, x, z);
    if (!cell)
        return;
    cell[0] |= static_cast<uint16_t>(1u << (network & 31));
    float scale = At<float>(self, 0xC54);
    if (void* const neighbor = Virtual<void*>(service, 0x34, object))
        reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(Virtual<void*>(neighbor, 0xC), 0xCA947077, &scale);
    float& cellScale = *reinterpret_cast<float*>(cell + 2);
    if (cellScale < scale)
        cellScale = scale;
    void* const connection = Virtual<void*>(object, 0x24);
    if (!connection || !Virtual<uint8_t>(connection, 0xAC)) {
        cell[1] = 0;
        return;
    }
    uint32_t a = 0, b = 0;
    Virtual<uint32_t>(connection, 0x14, &a, &b);
    char* const head = At<char*>(self, 0xB8);
    for (char* node = At<char*>(head, 8); node != head; node = TreeNext(node)) {
        void* const holder = At<void*>(node, 0x14);
        uint32_t first = 0, second = 0;
        if (reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32)(holder, 0x4A23576B, &first) &&
            reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32)(holder, 0x4A23576C, &second) && first == a &&
            second == b) {
            cell[1] = At<uint16_t>(node, 0x10);
            return;
        }
    }
    void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x14);
    void* const holder = memory ? reinterpret_cast<ThisFn>(addr::SC4TrafficObject_Construct)(memory) : nullptr;
    Virtual<uint32_t>(holder, 0x4);
    reinterpret_cast<SetFn>(addr::SC4_SetPropertySint32)(holder, 0x4A23576B, a);
    reinterpret_cast<SetFn>(addr::SC4_SetPropertySint32)(holder, 0x4A23576C, b);
    uint16_t key = 1;
    for (;; ++key) {
        char* const mapHead = At<char*>(self, 0xB8);
        char* best = mapHead;
        for (char* at = At<char*>(mapHead, 4); at;) {
            if (At<uint16_t>(at, 0x10) < key) {
                at = At<char*>(at, 0xC);
            } else {
                best = at;
                at = At<char*>(at, 8);
            }
        }
        if (best == mapHead || key < At<uint16_t>(best, 0x10))
            break;
    }
    cell[1] = key;
    *static_cast<void**>(reinterpret_cast<AtFn>(addr::SC4ObjectMap_At)(static_cast<char*>(self) + 0xB8, &key)) = holder;
}

// 0x00711810 (message 0xC2C09554): the travel strategy percentages at
// +0xC5C (four groups of three bytes) from their exemplar, then the deltas
// of every active ordinance (0x00B43D90 slots 14, 15, 20; property
// 0x8A612FEE, nine values) added to the last three groups, each clamped to
// 0-100.
void __fastcall LoadTravelStrategies(void* self, void*)
{
    using GetBytesFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t** vector);
    using GetValuesFn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t** vector);
    using NewFn = void*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* memory);
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xC9133286};
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        for (uint32_t group = 0; group < 4; ++group) {
            uint8_t* bytes[3] = {};
            if (reinterpret_cast<GetBytesFn>(addr::SC4_GetPropertyUint8Vector)(holder, 0x4953E8A3 + group, bytes)) {
                At<uint8_t>(self, 0xC5C + 3 * group) = bytes[0][0];
                At<uint8_t>(self, 0xC5D + 3 * group) = bytes[0][1];
                At<uint8_t>(self, 0xC5E + 3 * group) = bytes[0][2];
            }
            if (bytes[0])
                reinterpret_cast<FreeFn>(addr::SC4_Free)(bytes[0]);
        }
        Virtual<uint32_t>(exemplar, 0x8);
    }
    void* const ordinances = Global<void*>(addr::SC4_Service43D90);
    if (!ordinances || !Virtual<uint32_t>(ordinances, 0x38))
        return;
    auto* const ids = static_cast<uint32_t*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(4 * Virtual<uint32_t>(ordinances, 0x38)));
    uint32_t count = 0;
    Virtual<uint32_t>(ordinances, 0x3C, ids, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* const ordinance = Virtual<void*>(ordinances, 0x50, ids[i]);
        int32_t* deltas[3] = {};
        if (!ordinance || !Virtual<void*>(ordinance, 0x40))
            continue;
        if (reinterpret_cast<GetValuesFn>(addr::SC4_GetPropertySint32Vector)(Virtual<void*>(ordinance, 0x40), 0x8A612FEE, deltas)) {
            for (uint32_t k = 0; k < 9; ++k) {
                int32_t value = At<uint8_t>(self, 0xC5F + k) + deltas[0][k];
                if (value < 0)
                    value = 0;
                if (value > 100)
                    value = 100;
                At<uint8_t>(self, 0xC5F + k) = static_cast<uint8_t>(value);
            }
        }
        if (deltas[0])
            reinterpret_cast<FreeFn>(addr::SC4_Free)(deltas[0]);
    }
    reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(ids);
}

// The link record (0x20 bytes, in a list node at +8) of a ferry entry's
// link list for a key, created at the end with its counters cleared when
// missing.
char* FerryLink(char* entry, char* found)
{
    using AllocateFn = char*(__cdecl*)(std::size_t size);
    using CopyFn = void(__thiscall*)(void* to, const void* from);
    char* const head = At<char*>(entry, 0x10);
    if (found != head)
        return found + 8;
    uint32_t blank[8] = {};
    char* const node = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x28);
    reinterpret_cast<CopyFn>(addr::SC4FerryLink_CopyConstruct)(node + 8, blank);
    LinkAtEnd(head, node);
    char* const record = At<char*>(At<char*>(entry, 0x10), 4) + 8;
    At<uint16_t>(record, 0x1C) = 0;
    At<uint16_t>(record, 0x18) = 0;
    At<uint16_t>(record, 0x1E) = 0;
    At<uint16_t>(record, 0x1A) = 0;
    return record;
}

// Unlinks and frees a ferry link node.
void DropFerryLink(char* node)
{
    using FreeFn = void(__cdecl*)(void* memory);
    char* const next = At<char*>(node, 0);
    char* const prev = At<char*>(node, 4);
    At<char*>(prev, 0) = next;
    At<char*>(next, 4) = prev;
    if (At<void*>(node, 0x14))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(node, 0x14));
    reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
}

// 0x0071BE50 (phase 19): links two ferry entries (their cells at +4/+5,
// keys from +0/+1) both ways: with a water route between them, each one's
// link to the other (+0x10, created when missing) gets the cost x 0.01, the
// other's +2/+3 and its route bytes; without one, both links are dropped.
void __fastcall LinkRoutes19(void* self, void*, char* a, char* b)
{
    using FindFn = uint8_t(__thiscall*)(void* finder, uint32_t, uint32_t, uint32_t, uint32_t, int32_t* cost, void* route,
                                        void* back);
    using AssignFn = void(__thiscall*)(void* to, const void* from);
    using FreeFn = void(__cdecl*)(void* memory);
    const auto keyB = static_cast<uint16_t>(At<uint8_t>(b, 0) << 8 | At<uint8_t>(b, 1));
    const auto keyA = static_cast<uint16_t>(At<uint8_t>(a, 0) << 8 | At<uint8_t>(a, 1));
    auto find = [](char* entry, uint16_t key) {
        char* const head = At<char*>(entry, 0x10);
        char* node = At<char*>(head, 0);
        while (node != head && At<uint16_t>(node, 8) != key)
            node = At<char*>(node, 0);
        return node;
    };
    char* const toB = find(a, keyB);
    char* const toA = find(b, keyA);
    int32_t cost = 0;
    uint8_t* forward[3] = {};
    uint8_t* backward[3] = {};
    if (reinterpret_cast<FindFn>(addr::SC4WaterRouteFinder_Find)(At<void*>(self, 0x108), At<uint8_t>(a, 4), At<uint8_t>(a, 5),
                                                                At<uint8_t>(b, 4), At<uint8_t>(b, 5), &cost, forward, backward)) {
        const auto scaled = static_cast<float>(static_cast<double>(cost) * Global<float>(addr::SC4Traffic_FerryCostScale));
        char* record = FerryLink(a, toB);
        At<uint16_t>(record, 0) = keyB;
        At<float>(record, 8) = scaled;
        At<uint16_t>(record, 2) = At<uint8_t>(b, 2);
        At<uint16_t>(record, 4) = At<uint8_t>(b, 3);
        reinterpret_cast<AssignFn>(addr::SC4ByteVector_Assign)(record + 0xC, forward);
        record = FerryLink(b, toA);
        At<uint16_t>(record, 0) = keyA;
        At<float>(record, 8) = scaled;
        At<uint16_t>(record, 2) = At<uint8_t>(a, 2);
        At<uint16_t>(record, 4) = At<uint8_t>(a, 3);
        reinterpret_cast<AssignFn>(addr::SC4ByteVector_Assign)(record + 0xC, backward);
    } else {
        if (toB != At<char*>(a, 0x10))
            DropFerryLink(toB);
        if (toA != At<char*>(b, 0x10))
            DropFerryLink(toA);
    }
    if (backward[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(backward[0]);
    if (forward[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(forward[0]);
}

// 0x007140E0: a building linked to another (subway/el entrance and the
// like) added or removed. Removed: its cell's entry of the link table
// (+0xC8) is erased. Added, with a linked building (+0x1D0) of a type it
// has (slot 22: 2, 6, 0, 1, 10, 12 or 9 in that order): the entry gets the
// other's cell, flags 0x1FE (the game tests its type flags with `or`), the
// type and nine costs: the distance between the two x the type's cost per
// travel type (0x00B4B110) / 16.
void __fastcall OnNetworkBuilding(void* self, void*, char* occupant, uint8_t added)
{
    using EraseFn = void(__thiscall*)(void* table, const uint16_t* key);
    using AtFn = void*(__thiscall*)(void* table, const uint16_t* key);
    float here[3];
    Virtual<uint32_t>(occupant + 4, 0x20, here);
    int32_t x = 0, z = 0;
    Virtual<uint32_t>(At<void*>(self, 0x14), 0x17C, here[0], here[2], &x, &z);
    const auto key = static_cast<uint16_t>((static_cast<uint32_t>(x & 0xFF) << 8) | static_cast<uint32_t>(z));
    char* const table = static_cast<char*>(self) + 0xC8;
    if (!added) {
        if (g_wideCells)
            WideLinks().erase(CellKey(x, z));
        else
            reinterpret_cast<EraseFn>(addr::SC4LinkTable_Erase)(table, &key);
        return;
    }
    void* const linked = At<void*>(occupant, 0x1D0);
    if (!linked)
        return;
    Virtual<uint32_t>(linked, 0x4);
    float there[3];
    Virtual<uint32_t>(Virtual<void*>(linked, 0xC0), 0x20, there);
    int32_t otherX = 0, otherZ = 0;
    Virtual<uint32_t>(At<void*>(self, 0x14), 0x17C, there[0], there[2], &otherX, &otherZ);
    Virtual<uint32_t>(linked, 0x8);
    uint32_t type = 0xFFFFFFFF;
    for (const uint32_t candidate : {2u, 6u, 0u, 1u, 10u, 12u, 9u}) {
        if (Virtual<uint8_t>(occupant, 0x58, candidate)) {
            type = candidate;
            break;
        }
    }
    if (type == 0xFFFFFFFF)
        return;
    struct {
        uint8_t x;
        uint8_t z;
        uint16_t flags;
        float costs[9];
        uint16_t type;
        uint16_t unused;
    } link{};
    static_assert(sizeof(link) == 0x2C);
    link.x = static_cast<uint8_t>(otherX);
    link.z = static_cast<uint8_t>(otherZ);
    link.type = static_cast<uint16_t>(type);
    link.flags = 0x1FE;
    const double dx = static_cast<double>(here[0]) - there[0];
    const double dy = static_cast<double>(here[1]) - there[1];
    const double dz = static_cast<double>(here[2]) - there[2];
    const double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
    const auto* const tables = reinterpret_cast<const float*>(addr::SC4Traffic_LinkCostTables);
    for (uint32_t k = 0; k < 9; ++k)
        link.costs[k] = static_cast<float>(distance * tables[13 * k + type] * 0.0625f);
    if (g_wideCells) {
        WideLink& wide = WideLinks()[CellKey(x, z)];
        std::memset(wide.node, 0, sizeof wide.node);
        std::memcpy(wide.node + 8, &link, sizeof link);
        wide.x = otherX;
        wide.z = otherZ;
        return;
    }
    std::memcpy(reinterpret_cast<AtFn>(addr::SC4LinkTable_At)(table, &key), &link, sizeof link);
}

// push_back of a 10-byte part, as the game inlines it (growing to size +
// max(size, 1)).
void PushPart(char* vector, const uint8_t* part)
{
    using AllocateFn = char*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* memory);
    char*& begin = At<char*>(vector, 0);
    char*& end = At<char*>(vector, 4);
    char*& capacity = At<char*>(vector, 8);
    if (end != capacity) {
        if (end)
            std::memcpy(end, part, 10);
        end += 10;
        return;
    }
    const auto size = static_cast<uint32_t>((end - begin) / 10);
    const uint32_t grown = size + (size < 1 ? 1 : size);
    char* const fresh = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(10 * grown);
    char* out = fresh;
    for (char* at = begin; at != end; at += 10, out += 10) {
        if (out)
            std::memcpy(out, at, 10);
    }
    if (out)
        std::memcpy(out, part, 10);
    if (begin)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(begin);
    begin = fresh;
    end = out + 10;
    capacity = fresh + 10 * grown;
}

// 0x00715CB0: a transit record of the cost table (+0x70C) refreshed from
// its building (record +0): +0x28 cleared; the building's property
// 0xE90E25A1 (groups of four bytes) appended to the parts at +0x1C as
// 10-byte parts (switch flag 0x82, the four side bits 7-4 turned by the
// lot's orientation (0x00AB3280), two words), +0x28 getting bit (fourth
// byte) of each non-switch part; then +0x10 from the parts' types.
void __fastcall RefreshCost(void* self, void*, char* record)
{
    using GetBytesFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t** vector);
    using ReserveFn = void(__thiscall*)(void* vector, uint32_t count);
    using FreeFn = void(__cdecl*)(void* memory);
    (void)self;
    void* const object = At<void*>(record, 0);
    uint8_t* bytes[3] = {};
    reinterpret_cast<GetBytesFn>(addr::SC4_GetPropertyUint8Vector)(Virtual<void*>(object, 0x18), 0xE90E25A1, bytes);
    At<uint16_t>(record, 0x28) = 0;
    int32_t cell[4] = {}; // slot 11 fills more than the cell (the game leaves 16 bytes)
    Virtual<uint32_t>(object, 0x2C, cell);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cell[0], cell[1], 0u);
    if (!lot) {
        if (bytes[0])
            reinterpret_cast<FreeFn>(addr::SC4_Free)(bytes[0]);
        return;
    }
    const auto count = static_cast<uint32_t>(static_cast<uint32_t>(bytes[1] - bytes[0]) >> 2);
    char* const parts = record + 0x1C;
    reinterpret_cast<ReserveFn>(addr::SC4PartVector_Reserve)(parts, count);
    const uint32_t orientation = Virtual<uint32_t>(lot, 0x5C);
    const auto* const turn = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_SideTurns) + 4 * orientation;
    for (uint32_t i = 0; i < static_cast<uint32_t>(bytes[1] - bytes[0]); i += 4) {
        uint8_t part[10] = {};
        part[0] = bytes[0][i] == 0x82 ? 1 : 0;
        const uint8_t sides = bytes[0][i + 1];
        for (uint32_t k = 0; k < 4; ++k)
            part[1 + turn[k]] = (sides & (1u << (7 - k))) ? 1 : 0;
        const uint16_t first = bytes[0][i + 2];
        const uint16_t second = bytes[0][i + 3];
        std::memcpy(part + 6, &first, 2);
        std::memcpy(part + 8, &second, 2);
        if (!part[0])
            At<uint16_t>(record, 0x28) |= static_cast<uint16_t>(1u << (second & 0xFF & 31));
        PushPart(parts, part);
    }
    At<uint8_t>(record, 0x10) = 0;
    const char* part = At<char*>(parts, 0);
    for (uint32_t i = 0; i < count; ++i, part += 10) {
        const uint16_t type = At<uint16_t>(const_cast<char*>(part), 8);
        switch (static_cast<uint8_t>(type)) {
        case 2: At<uint8_t>(record, 0x10) |= 0x01; break;
        case 3: At<uint8_t>(record, 0x10) |= 0x02; break;
        case 6: At<uint8_t>(record, 0x10) |= 0x04; break;
        case 7: At<uint8_t>(record, 0x10) |= 0x08; break;
        case 8: At<uint8_t>(record, 0x10) |= 0x10; break;
        default: break;
        }
        const uint16_t other = At<uint16_t>(const_cast<char*>(part), 6);
        if ((type == 7 && other == 6) || (other == 7 && type == 6))
            At<uint8_t>(record, 0x10) |= 0x40;
    }
    if (bytes[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(bytes[0]);
}

// 0x00714E60 (phase 0): the neighbour connections' trip capacities and the
// trip-kind bits of the city's edge cells. The region's populations
// (0x00B43D74 slot 6 by 0x2010/0x2020/0x2030, slot 9, truncated) are
// shared out over the neighbour cities (0x00B43D8C slot 21, their slot 17
// by the same IDs): each one's connection object (the +0xB8 entry whose
// properties 0x4A23576B/C match its slot 5 pair) gets kind k = its share
// (slot 19) and trips sent 4 + k = 0, kind 3 no limit; the default holder
// (+0xC4) trips sent 7 = 0. Then each border cell with traffic (0x0070FA40)
// sets, for each kind its holder takes (slot 20), its bit in +0x4C and
// counts the destination at +0x1B0.
void __fastcall StartTripCounts(void* self, void*)
{
    using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
    using ClearFn = void(__thiscall*)(char** list);
    using HasTrafficFn = uint8_t(__thiscall*)(void* self, int32_t x, int32_t z);
    char* const list = Virtual<char*>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, 12u);
    At<char*>(list, 0) = list;
    At<char*>(list, 4) = list;
    char* head = list;
    void* const regions = Global<void*>(addr::SC4_Service43D74);
    uint32_t population[3];
    for (uint32_t i = 0; i < 3; ++i) {
        void* const group = Virtual<void*>(regions, 0x18, 0x2010u + 0x10 * i, 0x10000u);
        population[i] = static_cast<uint32_t>(static_cast<int64_t>(Virtual<double>(group, 0x24)));
    }
    uint32_t totals[3] = {};
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x54, &head, 0u);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const city = At<void*>(node, 8);
        for (uint32_t i = 0; i < 3; ++i)
            totals[i] += Virtual<uint32_t>(city, 0x44, 0x2010u + 0x10 * i);
    }
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        void* const city = At<void*>(node, 8);
        uint32_t a = 0, b = 0;
        Virtual<uint32_t>(city, 0x14, &a, &b);
        void* holder = nullptr;
        char* const map = At<char*>(self, 0xB8);
        for (char* entry = At<char*>(map, 8); entry != map; entry = TreeNext(entry)) {
            void* const candidate = At<void*>(entry, 0x14);
            uint32_t first = 0, second = 0;
            if (reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32)(candidate, 0x4A23576B, &first) &&
                reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32)(candidate, 0x4A23576C, &second) && first == a &&
                second == b) {
                holder = candidate;
                break;
            }
        }
        if (!holder)
            continue;
        for (uint32_t i = 0; i < 3; ++i) {
            const auto mine = static_cast<int32_t>(Virtual<uint32_t>(city, 0x44, 0x2010u + 0x10 * i));
            const double share = static_cast<double>(mine) / static_cast<double>(totals[i]) * static_cast<double>(population[i]);
            Virtual<uint32_t>(self, 0x4C, holder, i, static_cast<uint32_t>(static_cast<int64_t>(share))); // slot 19
            SetMaxTripCapacity(nullptr, nullptr, holder, 4 + i, 0);
        }
        Virtual<uint32_t>(self, 0x4C, holder, 3u, 0xFFFFFFFFu);
        SetMaxTripCapacity(nullptr, nullptr, holder, 7, 0);
    }
    SetMaxTripCapacity(nullptr, nullptr, At<void*>(self, 0xC4), 7, 0);
    for (uint32_t side = 0; side < 4; ++side) {
        for (int32_t i = 0; i < At<int32_t>(self, 0x194); ++i) {
            uint16_t* const cell = reinterpret_cast<uint16_t*>(At<char*>(self, 0x88 + 0xC * side) + 8 * i);
            if (!cell[0])
                continue;
            int32_t x = 0, z = 0;
            switch (side) {
            case 0: x = -1; z = i; break;
            case 1: z = -1; x = i; break;
            case 2: x = At<int32_t>(self, 0x194) + 1; z = i; break;
            default: z = At<int32_t>(self, 0x198) + 1; x = i; break;
            }
            if (!reinterpret_cast<HasTrafficFn>(addr::cSC4TrafficSimulator_CellHasTraffic)(self, x, z))
                continue;
            void* holder = At<void*>(self, 0xC4);
            if (cell[1]) {
                char* const mapHead = At<char*>(self, 0xB8);
                char* best = mapHead;
                for (char* at = At<char*>(mapHead, 4); at;) {
                    if (At<uint16_t>(at, 0x10) < cell[1]) {
                        at = At<char*>(at, 0xC);
                    } else {
                        best = at;
                        at = At<char*>(at, 8);
                    }
                }
                if (best != mapHead && !(cell[1] < At<uint16_t>(best, 0x10))) {
                    holder = At<void*>(best, 0x14);
                } else {
                    cell[1] = 0;
                }
            }
            for (uint32_t kind = 0; kind < 4; ++kind) {
                if (!Virtual<uint32_t>(self, 0x50, holder, kind)) // slot 20
                    continue;
                At<uint8_t**>(self, 0x4C)[3 * x + 3][z + 1] |= static_cast<uint8_t>(1u << kind);
                ++At<uint32_t>(self, 0x1B0 + 4 * kind);
            }
        }
    }
    reinterpret_cast<ClearFn>(addr::SC4List_ClearReleasing4)(&head);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, head);
}

// 0x00716180 (phase 13 and CityInited): the subnetwork census, under a
// sliced deadline (step at 0x00B4B33C). For each lot of the cell walk (the
// iterator given; lot manager slot 6 with 1): the subnetwork IDs of the
// road edges (four words at +0xC) on the cells around and under the lot
// that share a row or column with it (property 0x2A4F9C74 of the lot,
// removed when there are none); each subnetwork's record (+0x64, 28
// bytes) gets the lot's occupant counts by type (lot slot 54: maps of
// type to count) and the lot's trip capacities (slot 20, kinds 0-3, at
// +0xC). When the walk is done, each neighbour connection (0x00B43D8C
// slot 16; its slot 6 cell, moved onto the city's edge) adds its holder's
// capacities to the subnetworks of the road edge there. False when out of
// time.
uint8_t __fastcall WalkCells13(void* self, void*, const void* parent, char* walk)
{
    using RectNextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
    using SetFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t** vector);
    using MapAtFn = uint32_t*(__thiscall*)(void* map, const uint32_t* key);
    using AllocateFn = uint32_t*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* memory);
    using ExpiredFn = uint8_t(__thiscall*)(SlicedDeadline* deadline);
    using ClearFn = void(__thiscall*)(char** list);
    SlicedDeadline deadline;
    StartSliced(deadline, parent, addr::cSC4TrafficSimulator_Phase13Step);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    uint32_t* ids[3] = {};
    ids[0] = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x100);
    ids[1] = ids[0];
    ids[2] = ids[0] + 0x40;
    auto record = [self](uint32_t id) { return At<char*>(self, 0x64) + 0x1C * id; };
    for (;;) {
        deadline.count += deadline.step;
        if (!(deadline.count & 0x3FF) && reinterpret_cast<ExpiredFn>(addr::SC4Deadline_Expired)(&deadline)) {
            if (ids[0])
                reinterpret_cast<FreeFn>(addr::SC4_Free)(ids[0]);
            EndSliced(deadline);
            return 0;
        }
        if (!NextCell(walk))
            break;
        void* const lot = Virtual<void*>(lots, 0x18, At<int32_t>(walk, 4), At<int32_t>(walk, 8), 1u);
        if (!lot)
            continue;
        int32_t rect[4];
        Virtual<uint32_t>(lot, 0x38, rect);
        const int32_t xa = rect[0] - 1 < 0 ? 0 : rect[0] - 1;
        const int32_t za = rect[1] - 1 < 0 ? 0 : rect[1] - 1;
        int32_t xb = rect[2] + 1 > At<int32_t>(self, 0x194) ? At<int32_t>(self, 0x194) : rect[2] + 1;
        int32_t zb = rect[3] + 1 > At<int32_t>(self, 0x198) ? At<int32_t>(self, 0x198) : rect[3] + 1;
        if (xb < xa)
            xb = xa;
        if (zb < za)
            zb = za;
        CellRect around{};
        around.x = xa;
        around.z = za;
        around.step = 1;
        around.x0 = xa;
        around.z0 = za;
        around.x1 = xb;
        around.z1 = zb;
        int32_t count = 0;
        ids[1] = ids[0];
        int32_t cx = 0, cz = 0;
        while (reinterpret_cast<RectNextFn>(addr::SC4RectIterator_Next)(&around, &cx, &cz)) {
            const bool row = cx >= rect[0] && cx <= rect[2];
            const bool column = cz >= rect[1] && cz <= rect[3];
            if (!row && !column)
                continue;
            char* edge = GridCell<char*>(static_cast<char*>(self) + field::kEdges, cx, cz);
            while (edge && At<uint8_t>(edge, 6) != 1)
                edge = At<char*>(edge, 0x14);
            if (!edge)
                continue;
            for (uint32_t side = 0; side < 4; ++side) {
                const uint32_t id = At<uint16_t>(edge, 0xC + 2 * side);
                if (!id)
                    continue;
                bool known = false;
                for (int32_t i = 0; i < count && !known; ++i)
                    known = ids[0][i] == id;
                if (known)
                    continue;
                if (ids[1] != ids[2]) {
                    if (ids[1])
                        *ids[1] = id;
                    ++ids[1];
                } else {
                    const auto size = static_cast<uint32_t>(ids[1] - ids[0]);
                    const uint32_t grown = size + (size < 1 ? 1 : size);
                    uint32_t* const fresh = grown ? reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(4 * grown) : nullptr;
                    if (size)
                        std::memmove(fresh, ids[0], 4 * size);
                    fresh[size] = id;
                    if (ids[0])
                        reinterpret_cast<FreeFn>(addr::SC4_Free)(ids[0]);
                    ids[0] = fresh;
                    ids[1] = fresh + size + 1;
                    ids[2] = fresh + grown;
                }
                ++count;
            }
        }
        void* const holder = Virtual<void*>(lot, 0xC);
        if (!count) {
            Virtual<uint32_t>(holder, 0x44, 0x2A4F9C74u, 0u);
            continue;
        }
        reinterpret_cast<SetFn>(addr::SC4_SetPropertyUint32Vector)(holder, 0x2A4F9C74, ids);
        char** const maps = Virtual<char**>(lot, 0xD8); // slot 54: per occupant group, a map of type to count
        for (char* group = maps[0]; group != maps[1]; group += 0xC) {
            char* const head = At<char*>(group, 0);
            for (char* node = At<char*>(head, 8); node != head; node = TreeNext(node)) {
                const uint32_t type = At<uint32_t>(node, 0x10);
                const uint16_t number = At<uint16_t>(node, 0x14);
                for (int32_t i = 0; i < count; ++i)
                    *reinterpret_cast<MapAtFn>(addr::SC4Uint32Map_At)(record(ids[0][i]), &type) += number;
            }
        }
        void* const lotHolder = Virtual<void*>(lot, 0xC);
        for (int32_t i = 0; i < count; ++i) {
            char* const r = record(ids[0][i]);
            for (uint32_t kind = 0; kind < 4; ++kind)
                At<uint32_t>(r, 0xC + 4 * kind) += Virtual<uint32_t>(self, 0x50, lotHolder, kind); // slot 20
        }
    }
    char* const list = Virtual<char*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 12u);
    At<char*>(list, 0) = list;
    At<char*>(list, 4) = list;
    char* head = list;
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x40, &head);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        int32_t x = 0, z = 0;
        Virtual<uint32_t>(At<void*>(node, 8), 0x18, &x, &z);
        int32_t ex = x, ez = z;
        if (x == -1)
            ex = 0;
        else if (x == At<int32_t>(self, 0x20))
            ex = At<int32_t>(self, 0x28);
        else if (z == -1)
            ez = 0;
        else if (z == At<int32_t>(self, 0x24))
            ez = At<int32_t>(self, 0x2C);
        char* edge = GridCell<char*>(static_cast<char*>(self) + field::kEdges, ex, ez);
        while (edge && At<uint8_t>(edge, 6) != 1)
            edge = At<char*>(edge, 0x14);
        if (!edge)
            continue;
        void* const neighbor = NeighborHolder(self, nullptr, 1, x, z);
        if (!neighbor)
            continue;
        uint32_t capacities[4];
        for (uint32_t kind = 0; kind < 4; ++kind)
            capacities[kind] = Virtual<uint32_t>(self, 0x50, neighbor, kind);
        for (uint32_t side = 0; side < 4; ++side) {
            const uint16_t id = At<uint16_t>(edge, 0xC + 2 * side);
            if (!id)
                continue;
            char* const r = record(id);
            for (uint32_t kind = 0; kind < 4; ++kind)
                At<uint32_t>(r, 0xC + 4 * kind) += capacities[kind];
        }
    }
    reinterpret_cast<ClearFn>(addr::SC4List_ClearReleasing)(&head);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, head);
    if (ids[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(ids[0]);
    EndSliced(deadline);
    return 1;
}

// 0x0071E2F0 (message 0xE8BDA2E7, slot 9 = 3): a transit lot going away.
// Its entry of the lot multimap (+0x1A0, bucket = lot property 0x691C863C,
// matched by cell); its cost record (+0x70C, by the edge ID at its corner)
// released and erased, its cells' edge IDs and +0x700 bytes cleared and
// the cells around refreshed (0x0071A860); its budget items (0xCA550301:
// with its cost taken off +0x778; 0x0C3BF549) dropped; outside phase 10,
// its trip-kind bits dropped from the open destinations (+0x1B0); its
// ferry entry (+0x104, cell inside the lot) torn down: the terminal count
// (+0x128 by type), the links to it from the other entries, the walk
// positions of phase 19 (+0x10C, +0x110), its keys in +0xDC and +0xF0,
// both route lists. Last, its cells' trip-kind bits (+0x4C) cleared.
void __fastcall OnTransitLotRemoved(void* self, void*, void* lot)
{
    using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
    using EraseFn = void(__thiscall*)(void* map, char* where);
    using CostEraseFn = void(__thiscall*)(void* table, const void* const* iterator);
    using CellsFn = void(__thiscall*)(void* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1);
    using IndexFn = char**(__thiscall*)(void* table, const void* key);
    using KeyEraseFn = void(__thiscall*)(void* table, const uint16_t* key);
    using ClearFn = void(__thiscall*)(void* list);
    using FreeFn = void(__cdecl*)(void* memory);
    char* const base = static_cast<char*>(self);
    if (!lot)
        return;
    uint32_t bucket = 0;
    if (reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32)(Virtual<void*>(lot, 0xC), 0x691C863C, &bucket)) {
        int32_t x = 0, z = 0;
        Virtual<uint32_t>(lot, 0x54, &x, &z);
        char* const head = At<char*>(self, 0x1A0);
        char* node = head;
        for (char* at = At<char*>(head, 4); at;) {
            if (At<uint32_t>(at, 0x10) < bucket) {
                at = At<char*>(at, 0xC);
            } else {
                node = at;
                at = At<char*>(at, 8);
            }
        }
        for (; node != At<char*>(self, 0x1A0) && At<uint32_t>(node, 0x10) == bucket; node = TreeNext(node)) {
            if (LotValueX(node) == static_cast<uint32_t>(x) && LotValueZ(node) == static_cast<uint32_t>(z)) {
                reinterpret_cast<EraseFn>(addr::SC4LotMultimap_Erase)(base + 0x1A0, node);
                break;
            }
        }
    }
    int32_t rect[4];
    Virtual<uint32_t>(lot, 0x38, rect);
    void* const ids = Virtual<void*>(At<void*>(self, 0x704), 0x60);
    const uint32_t id = GridCell<uint32_t>(ids, rect[0], rect[1]);
    char** const buckets = At<char**>(self, 0x710);
    const auto count = static_cast<uint32_t>(At<char**>(self, 0x714) - buckets);
    char* entry = buckets[id % count];
    while (entry && At<uint32_t>(entry, 4) != id)
        entry = At<char*>(entry, 0);
    if (entry) {
        void* const bytes = Virtual<void*>(At<void*>(self, 0x700), 0x60);
        Virtual<uint32_t>(At<void*>(entry, 8), 0x8); // Release
        const void* const iterator[2] = {entry, base + 0x70C};
        reinterpret_cast<CostEraseFn>(addr::SC4CostTable_Erase)(base + 0x70C, iterator);
        for (int32_t cx = rect[0]; cx <= rect[2]; ++cx) {
            for (int32_t cz = rect[1]; cz <= rect[3]; ++cz) {
                GridCell<uint32_t>(ids, cx, cz) = 0;
                GridCell<uint8_t>(bytes, cx, cz) = 0;
            }
        }
        reinterpret_cast<CellsFn>(addr::cSC4TrafficSimulator_OnCellsChanged)(self, rect[0] - 1, rect[1] - 1, rect[2] + 1, rect[3] + 1);
    }
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    struct {
        uint32_t id;
        uint32_t unknown[3];
        int64_t cost;
        uint32_t item;
        uint32_t cleared;
        uint32_t tail[4];
    } query;
    query.cleared = 0;
    if (Virtual<uint8_t>(budget, 0xCC, Virtual<void*>(lot, 0xC), 0xCA550301u, &query)) {
        Virtual<uint32_t>(budget, 0xE0, Virtual<void*>(lot, 0xC), query.item);
        At<int64_t>(self, 0x778) -= query.cost;
    }
    if (Virtual<uint8_t>(budget, 0xCC, Virtual<void*>(lot, 0xC), 0x0C3BF549u, &query))
        Virtual<uint32_t>(budget, 0xE0, Virtual<void*>(lot, 0xC), query.item);
    if (At<uint32_t>(self, field::kPhase) != 10) {
        const uint8_t kinds = At<uint8_t**>(self, 0x4C)[3 * rect[0] + 3][rect[1] + 1];
        for (uint32_t kind = 0; kind < 4; ++kind) {
            if (kinds & (1u << kind))
                --At<int32_t>(self, 0x1B0 + 4 * kind);
        }
    }
    int32_t cellX = 0, cellZ = 0;
    Virtual<uint32_t>(lot, 0x54, &cellX, &cellZ);
    char* const ferries = At<char*>(self, 0x104);
    char* ferry = At<char*>(ferries, 0);
    while (ferry != At<char*>(self, 0x104)) {
        const int32_t fx = At<uint8_t>(ferry, 0x24);
        const int32_t fz = At<uint8_t>(ferry, 0x25);
        if (fx >= rect[0] && fz >= rect[1] && fx <= rect[2] && fz <= rect[3])
            break;
        ferry = At<char*>(ferry, 0);
    }
    if (ferry != At<char*>(self, 0x104)) {
        int32_t& terminals = At<int32_t>(self, 0x128 + 4 * At<uint32_t>(ferry, 0x10));
        if (--terminals < 0)
            terminals = 0;
        const auto key = static_cast<uint16_t>(At<uint8_t>(ferry, 8) << 8 | At<uint8_t>(ferry, 9));
        char* const links = At<char*>(ferry, 0x18);
        for (char* link = At<char*>(links, 0); link != links; link = At<char*>(link, 0)) {
            char* const other = *reinterpret_cast<IndexFn>(addr::SC4RouteIndex_At)(base + 0xDC, link + 8);
            char* const theirs = At<char*>(other, 0x18);
            for (char* back = At<char*>(theirs, 0); back != theirs;) {
                if (At<uint16_t>(back, 8) != key) {
                    back = At<char*>(back, 0);
                    continue;
                }
                char* const next = At<char*>(back, 0);
                char* const prev = At<char*>(back, 4);
                At<char*>(prev, 0) = next;
                At<char*>(next, 4) = prev;
                if (At<void*>(back, 0x14))
                    reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(back, 0x14));
                reinterpret_cast<FreeFn>(addr::SC4_Free)(back);
                back = next;
            }
        }
        if (At<uint32_t>(self, field::kPhase) == 19) {
            if (At<char*>(self, 0x10C) == ferry)
                At<char*>(self, 0x10C) = At<char*>(ferry, 0);
            if (At<char*>(self, 0x110) == ferry)
                At<char*>(self, 0x110) = At<char*>(ferry, 0);
        }
        reinterpret_cast<KeyEraseFn>(addr::SC4LinkTable_Erase)(base + 0xDC, &key);
        const auto cellKey = static_cast<uint16_t>(At<uint8_t>(ferry, 0xA) << 8 | At<uint8_t>(ferry, 0xB));
        reinterpret_cast<KeyEraseFn>(addr::SC4LinkTable_Erase)(base + 0xF0, &cellKey);
        At<char*>(At<char*>(ferry, 4), 0) = At<char*>(ferry, 0);
        At<char*>(At<char*>(ferry, 0), 4) = At<char*>(ferry, 4);
        reinterpret_cast<ClearFn>(addr::SC4RouteList_Clear)(ferry + 0x1C);
        if (At<void*>(ferry, 0x1C))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(ferry, 0x1C));
        char* const own = At<char*>(ferry, 0x18);
        for (char* link = At<char*>(own, 0); link != At<char*>(ferry, 0x18);) {
            char* const next = At<char*>(link, 0);
            if (At<void*>(link, 0x14))
                reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(link, 0x14));
            reinterpret_cast<FreeFn>(addr::SC4_Free)(link);
            link = next;
        }
        At<char*>(At<char*>(ferry, 0x18), 0) = At<char*>(ferry, 0x18);
        At<char*>(At<char*>(ferry, 0x18), 4) = At<char*>(ferry, 0x18);
        if (At<void*>(ferry, 0x18))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(ferry, 0x18));
        reinterpret_cast<FreeFn>(addr::SC4_Free)(ferry);
    }
    for (int32_t cx = rect[0]; cx <= rect[2]; ++cx) {
        for (int32_t cz = rect[1]; cz <= rect[3]; ++cz)
            At<uint8_t**>(self, 0x4C)[3 * cx + 3][cz + 1] = 0;
    }
}

// 0x00723620: a ferry terminal (property 0x0BD86FD3: type 0-2, at most 16
// of a type) set up from the three parts on its lot (0xA823821E occupants
// of classes 0x2C520000, 0x2C530000 and 0x2C540000, the last beside water
// outside the lot): a ferry entry (+0x104) with their cells, its type and
// kind word, its lot cell, indexed by both cells (+0xDC, +0xF0), linked to
// every other terminal of its type (0x0071BE50) and routed to each
// neighbour connection by water (0x0071C0C0).
void __fastcall OnFerryTerminal(void* self, void*, void* occupant)
{
    using GetU8Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
    using ThisFn = void(__thiscall*)(void* object);
    using ContainsFn = uint8_t(__thiscall*)(const void* rect, int32_t x, int32_t z);
    using PushFn = void(__thiscall*)(void* list, const void* entry);
    using IndexFn = char**(__thiscall*)(void* table, const void* key);
    using LinkFn = void(__thiscall*)(void* self, char* a, char* b);
    using RouteFn = void(__thiscall*)(void* self, char* entry, int32_t x, int32_t z);
    using FreeFn = void(__cdecl*)(void* memory);
    char* const base = static_cast<char*>(self);
    void* const lot = Virtual<void*>(occupant, 0xC0);
    if (!lot)
        return;
    void* const holder = Virtual<void*>(lot, 0xC);
    int32_t rect[4];
    Virtual<uint32_t>(occupant, 0x38, rect);
    uint8_t type = 0;
    if (!reinterpret_cast<GetU8Fn>(addr::SC4_GetPropertyUint8)(Virtual<void*>(holder, 0x18), 0x0BD86FD3, &type) || type > 2 ||
        At<int32_t>(self, 0x128 + 4 * type) >= 16)
        return;
    alignas(4) char entry[0x20] = {};
    reinterpret_cast<ThisFn>(addr::SC4FerryEntry_Construct)(entry);
    At<uint16_t>(entry, 0xC) = reinterpret_cast<const uint16_t*>(addr::SC4Traffic_FerryKindOfType)[type];
    At<uint32_t>(entry, 8) = type;
    int32_t cellX = 0, cellZ = 0;
    Virtual<uint32_t>(occupant, 0x54, &cellX, &cellZ);
    entry[0x1D] = static_cast<char>(cellZ);
    entry[0x1C] = static_cast<char>(cellX);
    bool found[3] = {};
    void* list = nullptr;
    char* const head = Virtual<char*>(ListNodeAllocator(), 0xC, 12u);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    list = head;
    int32_t from[2] = {rect[0], rect[1]};
    int32_t to[2] = {rect[2], rect[3]};
    Virtual<uint32_t>(Global<void*>(addr::SC4_NetworkService), 0xA4, &list, from, to, 0xA823821Eu, 0xFFFFFFFFu);
    const auto* const stepX = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDX);
    const auto* const stepZ = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDZ);
    for (char* node = At<char*>(static_cast<char*>(list), 0); node != static_cast<char*>(list); node = At<char*>(node, 0)) {
        void* const part = At<void*>(node, 8);
        void* face = nullptr;
        if (Virtual<uint8_t>(part, 0x0, 0xE9793A65u, &face)) {
            const uint32_t kind = Virtual<uint32_t>(face, 0x1C);
            if (kind == 0x2C520000 || kind == 0x2C530000 || kind == 0x2C540000) {
                float position[3];
                Virtual<uint32_t>(part, 0x20, position);
                int32_t cx = 0, cz = 0;
                Virtual<uint32_t>(At<void*>(self, 0x14), 0x17C, position[0], position[2], &cx, &cz);
                if (kind == 0x2C520000) {
                    entry[0] = static_cast<char>(cx);
                    entry[1] = static_cast<char>(cz);
                    found[0] = true;
                } else if (kind == 0x2C530000) {
                    entry[2] = static_cast<char>(cx);
                    entry[3] = static_cast<char>(cz);
                    found[1] = true;
                } else {
                    for (uint32_t side = 0; side < 4; ++side) {
                        const int32_t nx = cx + stepX[side];
                        const int32_t nz = cz + stepZ[side];
                        if (reinterpret_cast<ContainsFn>(addr::SC4Rect_Contains)(rect, nx, nz))
                            continue;
                        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Terrain), 0x88, nx, nz)) {
                            entry[4] = static_cast<char>(nx);
                            entry[5] = static_cast<char>(nz);
                            found[2] = true;
                            break;
                        }
                    }
                }
            }
        }
        Virtual<uint32_t>(part, 0x8);
        if (face)
            Virtual<uint32_t>(face, 0x8);
    }
    if (found[0] && found[1] && found[2]) {
        reinterpret_cast<PushFn>(addr::SC4FerryList_PushBack)(base + 0x104, entry);
        ++At<int32_t>(self, 0x128 + 4 * At<uint32_t>(entry, 8));
        char* const added = At<char*>(At<char*>(self, 0x104), 4);
        char* const mine = added + 8;
        uint32_t key = static_cast<uint32_t>(At<uint8_t>(added, 8) << 8 | At<uint8_t>(added, 9));
        *reinterpret_cast<IndexFn>(addr::SC4RouteIndex_At)(base + 0xDC, &key) = added;
        key = static_cast<uint32_t>(At<uint8_t>(mine, 2) << 8 | At<uint8_t>(mine, 3));
        *reinterpret_cast<IndexFn>(addr::SC4RouteIndex_At)(base + 0xF0, &key) = added;
        char* const ferries = At<char*>(self, 0x104);
        for (char* other = At<char*>(ferries, 0); other != ferries; other = At<char*>(other, 0)) {
            if (other + 8 != mine && At<uint32_t>(other, 0x10) == At<uint32_t>(mine, 8))
                reinterpret_cast<LinkFn>(addr::cSC4TrafficSimulator_LinkRoutes19)(self, mine, other + 8);
        }
        char* const neighbors = Virtual<char*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 12u);
        At<char*>(neighbors, 0) = neighbors;
        At<char*>(neighbors, 4) = neighbors;
        char* cities = neighbors;
        Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x44, 9u, &cities);
        for (char* node = At<char*>(cities, 0); node != cities; node = At<char*>(node, 0)) {
            int32_t x = 0, z = 0;
            Virtual<uint32_t>(At<void*>(node, 8), 0x18, &x, &z);
            reinterpret_cast<RouteFn>(addr::cSC4TrafficSimulator_RouteToCell19)(self, mine, x, z);
        }
        reinterpret_cast<ThisFn>(addr::SC4List_Destroy3)(&cities);
    }
    reinterpret_cast<ThisFn>(addr::SC4List_Destroy2)(&list);
    reinterpret_cast<ThisFn>(addr::SC4RouteList_Clear)(entry + 0x14);
    if (At<void*>(entry, 0x14))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(entry, 0x14));
    reinterpret_cast<ThisFn>(addr::SC4FerryLinkList_Destroy)(entry + 0x10);
    if (At<void*>(entry, 0x10))
        reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(entry, 0x10));
}

// 0x00723AF0 (message 0xE8BDA2E7, slot 14 = 3): a transit building
// arriving. With its parts property (0xE90E25A1, groups of four bytes) it
// gets a cost record (+0x70C) under the next edge ID (+0x708): costs from
// properties 0xE90E25A2/3, 0x0BFC0A4C, 0x4BFC47B0, the parts (0x00715CB0),
// for a switch (flag 4) the cell of the first 0x8A13918A occupant on it
// (else its own), its cells stamped with the ID and the part types; with
// property 0x0BD86FD3 a ferry terminal (0x00723620). The cells are
// refreshed (0x0071A860) when either happened. Its lot joins the lot
// multimap (+0x1A0) under the next bucket (+0x924, wrapping at +0xC4C;
// property 0x691C863C) with a noise draw (0x4A611773); its budget items
// move to the lot (0xCA550301 adding its cost to +0x778, or 0x0C3BF549);
// outside phase 10 its trip-kind bits are set and counted.
// The record's cell bytes (+0x12, +0x13) are zero here for a building that
// is not a switch; the game leaves stack bytes there (and saves them).
void __fastcall OnTransitLotAdded(void* self, void*, void* occupant)
{
    using GetBytesFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t** vector);
    using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
    using GetBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
    using SetU32Fn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
    using SetU8Fn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
    using RecordFn = void(__thiscall*)(void* self, char* record);
    using ListFn = void(__thiscall*)(void* list);
    using CopyFn = void(__thiscall*)(void* to, const void* from);
    using ReserveFn = void(__thiscall*)(void* table, uint32_t count);
    using InsertFn = void(__thiscall*)(void* table, void* result, const void* pair);
    using MultimapInsertFn = void(__thiscall*)(void* map, void* result, char* child, char* parent, const void* value, uint32_t);
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t limit);
    using ThisObjectFn = void(__thiscall*)(void* self, void* object);
    using CellsFn = void(__thiscall*)(void* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1);
    using FreeFn = void(__cdecl*)(void* memory);
    char* const base = static_cast<char*>(self);
    if (!occupant)
        return;
    void* const lot = Virtual<void*>(occupant, 0xC0);
    if (!lot)
        return;
    void* const holder = Virtual<void*>(lot, 0xC);
    int32_t rect[4];
    Virtual<uint32_t>(occupant, 0x38, rect);
    void* const ground = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, rect[0], rect[1], 0u);
    uint8_t* bytes[3] = {};
    bool changed = false;
    if (reinterpret_cast<GetBytesFn>(addr::SC4_GetPropertyUint8Vector)(Virtual<void*>(holder, 0x18), 0xE90E25A1, bytes)) {
        void* const ids = Virtual<void*>(At<void*>(self, 0x704), 0x60);
        void* const types = Virtual<void*>(At<void*>(self, 0x700), 0x60);
        if ((bytes[1] - bytes[0]) & 3) {
            reinterpret_cast<FreeFn>(addr::SC4_Free)(bytes[0]);
            return;
        }
        alignas(4) char record[0x34] = {};
        At<void*>(record, 0) = holder;
        Virtual<uint32_t>(holder, 0x4);
        At<uint32_t>(record, 0x18) = Global<uint32_t>(addr::cSC4TrafficSimulator_RecordDefault);
        if (!reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(Virtual<void*>(holder, 0x18), 0xE90E25A2, &At<float>(record, 8)))
            At<float>(record, 8) = 0.0f;
        if (!reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(Virtual<void*>(holder, 0x18), 0xE90E25A3, &At<float>(record, 0xC)))
            At<float>(record, 0xC) = 0.0f;
        if (!reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(Virtual<void*>(holder, 0x18), 0x0BFC0A4C, &At<float>(record, 0x2C)))
            At<float>(record, 0x2C) = 0.0f;
        if (!reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool)(Virtual<void*>(holder, 0x18), 0x4BFC47B0, &At<uint8_t>(record, 0x30)))
            At<uint8_t>(record, 0x30) = 0;
        reinterpret_cast<RecordFn>(addr::cSC4TrafficSimulator_RefreshCost)(self, record);
        if (At<uint8_t>(record, 0x10) & 4) {
            void* list = nullptr;
            reinterpret_cast<ListFn>(addr::SC4List_Construct3)(&list);
            int32_t from[2] = {rect[0], rect[1]};
            int32_t to[2] = {rect[2], rect[3]};
            void* const network = Global<void*>(addr::SC4_NetworkService);
            Virtual<uint32_t>(network, 0xA4, &list, from, to, 0x8A13918Au, 0xFFFFFFFFu);
            char* const head = static_cast<char*>(list);
            if (At<char*>(head, 0) != head) {
                float position[3];
                Virtual<uint32_t>(At<void*>(At<char*>(head, 0), 8), 0x20, position);
                int32_t cx = 0, cz = 0;
                Virtual<uint32_t>(At<void*>(self, 0x14), 0x17C, position[0], position[2], &cx, &cz);
                record[0x12] = static_cast<char>(cx);
                record[0x13] = static_cast<char>(cz);
                record[0x11] = 1;
                Virtual<uint32_t>(network, 0xC0, &list);
            } else {
                int32_t cell[4] = {};
                Virtual<uint32_t>(occupant, 0x2C, cell);
                record[0x11] = 1;
                record[0x12] = static_cast<char>(cell[0]);
                record[0x13] = static_cast<char>(cell[1]);
            }
            reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&list);
        }
        uint8_t partTypes = 0;
        const char* const parts = At<char*>(record, 0x1C);
        const auto partCount = static_cast<uint32_t>((At<char*>(record, 0x20) - parts) / 10);
        for (uint32_t i = 0; i < partCount; ++i) {
            const char* const part = parts + 10 * i;
            const uint16_t type = At<uint16_t>(const_cast<char*>(part), 6);
            if (!part[0] && type < 8)
                partTypes |= static_cast<uint8_t>(1u << type);
        }
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            for (int32_t z = rect[1]; z <= rect[3]; ++z) {
                GridCell<uint32_t>(ids, x, z) = At<uint32_t>(self, 0x708);
                GridCell<uint8_t>(types, x, z) = partTypes;
            }
        }
        alignas(4) char pair[0x38];
        At<uint32_t>(pair, 0) = At<uint32_t>(self, 0x708);
        reinterpret_cast<CopyFn>(addr::SC4CostRecord_CopyConstruct)(pair + 4, record);
        reinterpret_cast<ReserveFn>(addr::SC4Uint32HashSet_Resize)(base + 0x70C, At<uint32_t>(self, 0x71C) + 1);
        uint32_t result[3];
        reinterpret_cast<InsertFn>(addr::SC4CostTable_Insert)(base + 0x70C, result, pair);
        if (At<void*>(pair, 4 + 0x1C))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(pair, 4 + 0x1C));
        ++At<uint32_t>(self, 0x708);
        changed = true;
        if (At<void*>(record, 0x1C))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(record, 0x1C));
    }
    if (Virtual<uint8_t>(Virtual<void*>(holder, 0x18), 0xC, 0x0BD86FD3u)) {
        reinterpret_cast<ThisObjectFn>(addr::cSC4TrafficSimulator_OnFerryTerminal)(self, occupant);
        reinterpret_cast<CellsFn>(addr::cSC4TrafficSimulator_OnCellsChanged)(self, rect[0], rect[1], rect[2], rect[3]);
    } else if (changed) {
        reinterpret_cast<CellsFn>(addr::cSC4TrafficSimulator_OnCellsChanged)(self, rect[0], rect[1], rect[2], rect[3]);
    }
    if (ground) {
        const uint32_t bucket = At<uint32_t>(self, 0x924);
        At<uint32_t>(self, 0x924) = bucket + 1;
        if (bucket + 1 == At<uint32_t>(self, 0xC4C))
            At<uint32_t>(self, 0x924) = 0;
        reinterpret_cast<SetU32Fn>(addr::SC4_SetPropertyUint32)(Virtual<void*>(ground, 0xC), 0x691C863C, bucket);
        int32_t x = 0, z = 0;
        Virtual<uint32_t>(ground, 0x54, &x, &z);
        const LotValue value = MakeLotValue(bucket, x, z);
        char* parent = At<char*>(self, 0x1A0);
        char* at = At<char*>(parent, 4);
        while (at) {
            parent = at;
            at = value.key < At<uint32_t>(at, 0x10) ? At<char*>(at, 8) : At<char*>(at, 0xC);
        }
        uint32_t inserted[2] = {};
        reinterpret_cast<MultimapInsertFn>(addr::SC4LotMultimap_Insert)(base + 0x1A0, inserted, nullptr, parent, &value, 0);
        const auto noise = static_cast<uint8_t>(reinterpret_cast<BelowFn>(addr::SC4Random_Below)(At<void*>(self, field::kRandom), 100));
        reinterpret_cast<SetU8Fn>(addr::SC4_SetPropertyUint8)(Virtual<void*>(ground, 0xC), 0x4A611773, noise);
    }
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    struct {
        uint32_t id;
        uint32_t unknown[3];
        int64_t cost;
        uint32_t item;
        uint32_t cleared;
        uint32_t tail[4];
    } query;
    query.cleared = 0;
    if (Virtual<uint8_t>(budget, 0xCC, Virtual<void*>(holder, 0x18), 0xCA550301u, &query)) {
        if (ground) {
            Virtual<uint32_t>(budget, 0xE8, Virtual<void*>(holder, 0x18), Virtual<void*>(ground, 0xC));
            Virtual<uint32_t>(budget, 0xDC, Virtual<void*>(ground, 0xC), query.item);
            At<int64_t>(self, 0x778) += query.cost;
        }
    } else if (Virtual<uint8_t>(budget, 0xCC, Virtual<void*>(holder, 0x18), 0x0C3BF549u, &query) && ground) {
        Virtual<uint32_t>(budget, 0xE8, Virtual<void*>(holder, 0x18), Virtual<void*>(ground, 0xC));
        Virtual<uint32_t>(budget, 0xDC, Virtual<void*>(ground, 0xC), query.item);
    }
    if (At<uint32_t>(self, field::kPhase) != 10) {
        uint8_t kinds = 0;
        for (uint32_t kind = 0; kind < 4; ++kind) {
            if (Virtual<uint32_t>(self, 0x50, Virtual<void*>(occupant, 0xC), kind)) {
                kinds |= static_cast<uint8_t>(1u << kind);
                ++At<uint32_t>(self, 0x1B0 + 4 * kind);
            }
        }
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            for (int32_t z = rect[1]; z <= rect[3]; ++z)
                At<uint8_t**>(self, 0x4C)[3 * x + 3][z + 1] = kinds;
        }
    }
    if (bytes[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(bytes[0]);
}

// 0x0070FC70: a cell's edge of a network unlinked and freed.
void __fastcall RemoveEdge(void* self, void*, uint32_t network, int32_t x, int32_t z)
{
    using FreeFn = void*(__thiscall*)(void* edge, uint32_t flags);
    char*& first = GridCell<char*>(static_cast<char*>(self) + field::kEdges, x, z);
    char* node = first;
    if (!node)
        return;
    if (At<uint8_t>(node, 6) == network) {
        first = At<char*>(node, 0x14);
        reinterpret_cast<FreeFn>(addr::SC4TrafficEdge_Free)(node, 1);
        return;
    }
    for (char* next = At<char*>(node, 0x14); next; node = next, next = At<char*>(node, 0x14)) {
        if (At<uint8_t>(next, 6) == network) {
            At<char*>(node, 0x14) = At<char*>(next, 0x14);
            reinterpret_cast<FreeFn>(addr::SC4TrafficEdge_Free)(next, 1);
            return;
        }
    }
}

// A new edge node from the edge pool (as the game inlines it), its fields
// cleared but for the network.
char* NewEdge(uint32_t network)
{
    using GrowFn = char*(__thiscall*)(void* pool);
    char* edge = At<char*>(Global<char*>(addr::SC4EdgePool + 4), 8);
    if (!edge)
        edge = reinterpret_cast<GrowFn>(addr::SC4EdgePool_Grow)(reinterpret_cast<void*>(addr::SC4EdgePool));
    char* const page = Global<char*>(addr::SC4EdgePool + 4);
    At<char*>(page, 8) = At<char*>(edge, 0);
    ++At<uint32_t>(page, 0xC);
    edge[4] = 0;
    edge[7] = 0;
    At<uint32_t>(edge, 8) = 0;
    At<char*>(edge, 0x14) = nullptr;
    edge[6] = static_cast<char>(network);
    At<uint32_t>(edge, 0xC) = 0;
    At<uint32_t>(edge, 0x10) = 0;
    return edge;
}

// 0x0070FB70: a cell's edge of a network; a new one at the chain's end when
// missing (the old last edge's flag byte +7 cleared).
char* __fastcall GetOrCreateEdge(void* self, void*, uint32_t network, int32_t x, int32_t z)
{
    char*& first = GridCell<char*>(static_cast<char*>(self) + field::kEdges, x, z);
    char* node = first;
    if (!node) {
        char* const edge = NewEdge(network);
        GridCell<char*>(static_cast<char*>(self) + field::kEdges, x, z) = edge;
        return edge;
    }
    for (;;) {
        if (At<uint8_t>(node, 6) == network)
            return node;
        if (!At<char*>(node, 0x14))
            break;
        node = At<char*>(node, 0x14);
    }
    char* const edge = NewEdge(network);
    node[7] = 0;
    At<char*>(node, 0x14) = edge;
    return edge;
}

// 0x0070EAB0: a way through a cell for a travel type; for road travel also
// the neighbour's mark on the side facing it.
void __fastcall SetWay(void* self, void*, int32_t x, int32_t z, uint32_t from, uint32_t to, uint32_t type)
{
    At<uint16_t>(CellRecord(self, x, z), 2 * type) |= static_cast<uint16_t>(1u << ((to + 4 * from) & 31));
    if (reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkOfTravelType)[type] != 1)
        return;
    const int32_t nx = x + reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDX)[to];
    const int32_t nz = z + reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDZ)[to];
    if (!InCity(self, nx, nz))
        return;
    At<uint16_t>(CellRecord(self, nx, nz), 0x24 + 2 * ((to + 2) & 3)) |=
        reinterpret_cast<const uint16_t*>(addr::SC4Traffic_RoadSideMask)[from];
}

// 0x0070EB60: the pending rectangle (+0x820, flag +0x81C) grown to take in
// another.
void __fastcall MarkPending(void* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    if (!At<uint8_t>(self, 0x81C)) {
        At<int32_t>(self, 0x820) = x0;
        At<int32_t>(self, 0x824) = z0;
        At<int32_t>(self, 0x828) = x1;
        At<int32_t>(self, 0x82C) = z1;
        At<uint8_t>(self, 0x81C) = 1;
        return;
    }
    if (x0 < At<int32_t>(self, 0x820))
        At<int32_t>(self, 0x820) = x0;
    if (z0 < At<int32_t>(self, 0x824))
        At<int32_t>(self, 0x824) = z0;
    if (At<int32_t>(self, 0x828) < x1)
        At<int32_t>(self, 0x828) = x1;
    if (At<int32_t>(self, 0x82C) < z1)
        At<int32_t>(self, 0x82C) = z1;
}

// 0x006D6000: the side of (x, z) that (x2, z2) is on.
uint8_t __cdecl SideOf(int32_t x, int32_t z, int32_t x2, int32_t z2)
{
    if (x2 < x)
        return 0;
    if (x2 > x)
        return 2;
    return z2 >= z ? 3 : 1;
}

// 0x00715940 (OnCellsChanged, road ways): the side word (+0x24 + 2 x
// from) nibble of `to` of a cell set to the sides by which the road piece
// of the neighbour on side `from` continues: the end points of the
// piece's paths (slot 14 for side `from`, slot 3) leaving by `to`, matched
// against the neighbour's piece (network occupant of 0xC772BF98 with
// slot 9 0x1F4F, its slot 49) arriving from the opposite side (slot 7
// within 4.0).
void __fastcall OnRoadPiece(void* self, void*, int32_t x, int32_t z, void* piece, uint32_t from, uint32_t to)
{
    using ReserveFn = void(__thiscall*)(void* vector, uint32_t count);
    using InsertFn = void(__thiscall*)(void* vector, char* where, const void* value, const void* unused, uint32_t count,
                                       uint32_t one);
    using ClearFn = void(__thiscall*)(void* list);
    using FreeFn = void(__cdecl*)(void* memory);
    uint32_t* paths[3] = {};
    const uint32_t pathCount = Virtual<uint32_t>(piece, 0x38, 1u, from, paths);
    char* points[3] = {};
    reinterpret_cast<ReserveFn>(addr::SC4Float3Vector_Reserve)(points, pathCount);
    for (uint32_t i = 0; i < pathCount; ++i) {
        if (static_cast<uint8_t>(paths[0][i]) != to)
            continue;
        void* const path = Virtual<void*>(piece, 0xC, paths[0][i], 0u);
        if (!path)
            continue;
        const char* const point = *static_cast<char**>(path);
        if (points[1] != points[2]) {
            if (points[1])
                std::memcpy(points[1], point, 12);
            points[1] += 12;
        } else {
            const uint32_t unused = from;
            reinterpret_cast<InsertFn>(addr::SC4Float3Vector_Insert)(points, points[1], point, &unused, 1, 1);
        }
    }
    if (points[0] != points[1]) {
        const auto pointCount = static_cast<int32_t>((points[1] - points[0]) / 12);
        int32_t neighbor[2] = {x + reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDX)[from],
                               z + reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDZ)[from]};
        char* list = Virtual<char*>(ListNodeAllocator(), 0xC, 12u);
        At<char*>(list, 0) = list;
        At<char*>(list, 4) = list;
        Virtual<uint32_t>(Global<void*>(addr::SC4_NetworkService), 0xA4, &list, neighbor, neighbor, 0xC772BF98u, 0xFFFFFFFFu);
        for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
            void* const occupant = At<void*>(node, 8);
            void* network = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &network)) {
                if (Virtual<uint8_t>(network, 0x24, 0x1F4Fu)) {
                    if (void* const other = Virtual<void*>(network, 0xC4)) {
                        const uint32_t back = (from + 2) & 3;
                        uint32_t sides = 0;
                        for (int32_t i = 0; i < pointCount; ++i) {
                            uint32_t* ends[3] = {};
                            Virtual<uint32_t>(other, 0x1C, ends, 1u, points[0] + 12 * i, 0xFFu, back, 4.0f);
                            for (uint32_t* end = ends[0]; end != ends[1]; ++end)
                                sides |= 1u << (reinterpret_cast<const uint8_t*>(end)[1] & 31);
                            if (ends[0])
                                reinterpret_cast<FreeFn>(addr::SC4_Free)(ends[0]);
                        }
                        uint16_t& word = At<uint16_t>(CellRecord(self, x, z), 0x24 + 2 * from);
                        const uint32_t shift = (to * 4) & 31;
                        word = static_cast<uint16_t>((word & ~(0xFu << shift)) | (sides << ((to << 2) & 0xFF & 31)));
                    }
                }
                Virtual<uint32_t>(network, 0x8);
            }
            Virtual<uint32_t>(occupant, 0x8);
        }
        reinterpret_cast<ClearFn>(addr::SC4List_Clear2)(&list);
        Virtual<uint32_t>(ListNodeAllocator(), 0x10, list);
    }
    if (points[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(points[0]);
    if (paths[0])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(paths[0]);
}

// 0x0071A860: the network records of a rectangle (grown by 2, inside the
// city's last cells +0x28/+0x2C) rebuilt: pending (0x0070EB60); every
// edge of the six networks removed and the cells' 46-byte records reset;
// then for each network occupant (0xC772BF98 objects with interface
// 0xA821EF94, a type marked in 0x00AB3164, not flag 0x200000) its ways
// (slot 53 by network type 0x00AB3214 and sides) set for the travel types
// of the network, dead ends (piece slot 4) as a way back, road pieces
// handed to 0x00715940, and an edge per network it serves (way count,
// damage cost +0xB90 for roads, flag 0x40000, the kind of building with
// its cost +0xC00, 1.0 by default) with density records for both commutes.
// Then the transit buildings (cost records by edge ID, rectangle grown by
// 1) join their parts to the neighbouring cells, the link table marks
// cells (flag 2) and the ferry entries their cells (flag 4) and ways.
// Last, +0x62 and +0x888 set, +0x881 cleared.
void __fastcall OnCellsChanged(void* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    using RemoveFn = void(__thiscall*)(void* self, uint32_t network, int32_t x, int32_t z);
    using EdgeFn = char*(__thiscall*)(void* self, uint32_t network, int32_t x, int32_t z);
    using PieceFn = void(__thiscall*)(void* self, int32_t x, int32_t z, void* piece, uint32_t from, uint32_t to);
    using DensityFn = char*(__thiscall*)(void* table, const uint32_t* key);
    using CostFn = char*(__thiscall*)(void* table, const uint32_t* key);
    using AllocateFn = char*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* memory);
    using ClearFn = void(__thiscall*)(void* list);
    char* const base = static_cast<char*>(self);
    const auto* const networkOf = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkOfTravelType);
    const auto* const stepX = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDX);
    const auto* const stepZ = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDZ);
    int32_t xs = x0 - 2 < 0 ? 0 : x0 - 2;
    int32_t zs = z0 - 2 < 0 ? 0 : z0 - 2;
    int32_t xe = At<int32_t>(self, 0x28) < x1 + 2 ? At<int32_t>(self, 0x28) : x1 + 2;
    int32_t ze = At<int32_t>(self, 0x2C) < z1 + 2 ? At<int32_t>(self, 0x2C) : z1 + 2;
    MarkPending(self, nullptr, xs, zs, xe, ze);
    At<uint8_t>(self, 0x881) = 0;
    for (int32_t x = xs; x <= xe; ++x) {
        for (int32_t z = zs; z <= ze; ++z) {
            for (uint32_t network = 0; network < 6; ++network)
                reinterpret_cast<RemoveFn>(addr::cSC4TrafficSimulator_RemoveEdge)(self, network, x, z);
            char* const cell = CellRecord(self, x, z);
            for (uint32_t t = 0; t < 9; ++t) {
                At<uint16_t>(cell, 2 * t) = 0;
                At<uint16_t>(cell, 0x12 + 2 * t) = 0xFFFF;
            }
            At<uint32_t>(cell, 0x24) = 0xFFFFFFFF;
            At<uint32_t>(cell, 0x28) = 0xFFFFFFFF;
            cell[0x2C] = 0;
        }
    }

    char* list = Virtual<char*>(ListNodeAllocator(), 0xC, 12u);
    At<char*>(list, 0) = list;
    At<char*>(list, 4) = list;
    char* const unused = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0xC);
    At<char*>(unused, 0) = unused;
    At<char*>(unused, 4) = unused;
    int32_t from[2] = {xs, zs};
    int32_t to[2] = {xe, ze};
    Virtual<uint32_t>(Global<void*>(addr::SC4_NetworkService), 0xA4, &list, from, to, 0xC772BF98u, 0xFFFFFFFFu);
    for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
        void* const occupant = At<void*>(node, 8);
        void* network = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &network)) {
            bool counted = false;
            for (uint32_t kind = 0; kind < 13; ++kind) {
                if (reinterpret_cast<const uint8_t*>(addr::cSC4TrafficSimulator_NetworkKinds)[kind] &&
                    Virtual<uint8_t>(network, 0x58, kind))
                    counted = true;
            }
            if (Virtual<uint8_t>(network, 0x20, 0x200000u) || !counted) {
                Virtual<uint32_t>(network, 0x8);
                Virtual<uint32_t>(occupant, 0x8);
                continue;
            }
            float position[3];
            Virtual<uint32_t>(occupant, 0x20, position);
            int32_t cx = 0, cz = 0;
            Virtual<uint32_t>(At<void*>(self, 0x14), 0x17C, position[0], position[2], &cx, &cz);
            char* const cell = CellRecord(self, cx, cz);
            void* piece = nullptr;
            for (uint32_t index = 0; index < 6; ++index) {
                const uint32_t networkType = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_NetworkTypeOfIndex)[index];
                uint32_t ways = 0;
                for (uint32_t a = 0; a < 4; ++a) {
                    uint32_t here = 0;
                    for (uint32_t b = 0; b < 4; ++b) {
                        if (!Virtual<uint8_t>(network, 0xD4, networkType, a, b))
                            continue;
                        ++ways;
                        ++here;
                        for (uint32_t t = 0; t < 9; ++t) {
                            if (networkOf[t] != index)
                                continue;
                            At<uint16_t>(cell, 2 * t) |= static_cast<uint16_t>(1u << ((b + 4 * a) & 31));
                            if (t == 1)
                                reinterpret_cast<PieceFn>(addr::cSC4TrafficSimulator_OnRoadPiece)(self, cx, cz, Virtual<void*>(network, 0xC4),
                                                                                              a, b);
                        }
                    }
                    if (here)
                        continue;
                    if (!piece) {
                        piece = Virtual<void*>(network, 0xC4);
                        if (!piece)
                            continue;
                    }
                    if (!Virtual<void*>(piece, 0x10, networkType, a, 0xFFu, 0u, 0u))
                        continue;
                    ++ways;
                    for (uint32_t t = 0; t < 9; ++t) {
                        if (networkOf[t] == index)
                            SetWay(self, nullptr, cx, cz, a, a, t);
                    }
                }
                if (!static_cast<uint16_t>(ways))
                    continue;
                char* const edge = reinterpret_cast<EdgeFn>(addr::cSC4TrafficSimulator_GetOrCreateEdge)(self, index, cx, cz);
                edge[4] = static_cast<char>(ways);
                if (index == 1 && Virtual<uint8_t>(self, 0x40, cx, cz)) // slot 16: IsRoadDamaged
                    At<uint32_t>(edge, 8) = At<uint32_t>(self, 0xB90);
                else
                    At<uint32_t>(edge, 8) = 0;
                if (Virtual<uint8_t>(network, 0x20, 0x40000u))
                    edge[7] |= 1;
                edge[5] = static_cast<char>(0xFF);
                for (uint32_t kind = 0; kind < 13; ++kind) {
                    if (!(reinterpret_cast<const uint32_t*>(addr::SC4Traffic_LinkFlagsOfType)[kind] & (1u << index)) ||
                        !Virtual<uint8_t>(network, 0x58, kind))
                        continue;
                    if (kind == 3 && static_cast<uint8_t>(edge[5]) != 0xFF)
                        continue;
                    if (kind == 0 && edge[5] == 2)
                        continue;
                    edge[5] = static_cast<char>(kind);
                    At<uint32_t>(edge, 0) = At<uint32_t>(self, 0xC00 + 4 * kind);
                }
                if (static_cast<uint8_t>(edge[5]) == 0xFF) {
                    edge[5] = 0;
                    At<float>(edge, 0) = 1.0f;
                }
                for (uint32_t t = 0; t < 9; ++t) {
                    if (networkOf[t] != index)
                        continue;
                    for (uint32_t evening = 0; evening < 2; ++evening) {
                        uint32_t key = TypedCellKey(evening, t, cx, cz);
                        char** const buckets = At<char**>(self, 0x17C);
                        const auto count = static_cast<uint32_t>(At<char**>(self, 0x180) - buckets);
                        char* found = buckets[key % count];
                        while (found && At<uint32_t>(found, 4) != key)
                            found = At<char*>(found, 0);
                        if (!found)
                            std::memcpy(reinterpret_cast<DensityFn>(addr::SC4DensityTable_At)(base + 0x178, &key),
                                        reinterpret_cast<const void*>(addr::cSC4TrafficSimulator_EmptyEdgeDensity), 0x42);
                    }
                }
            }
            Virtual<uint32_t>(network, 0x8);
        }
        Virtual<uint32_t>(occupant, 0x8);
    }

    xs = xs - 1 < 0 ? 0 : xs - 1;
    zs = zs - 1 < 0 ? 0 : zs - 1;
    xe = At<int32_t>(self, 0x28) < xe + 1 ? At<int32_t>(self, 0x28) : xe + 1;
    ze = At<int32_t>(self, 0x2C) < ze + 1 ? At<int32_t>(self, 0x2C) : ze + 1;
    void* const ids = Virtual<void*>(At<void*>(self, 0x704), 0x60);
    for (int32_t x = xs; x <= xe; ++x) {
        for (int32_t z = zs; z <= ze; ++z) {
            uint32_t id = GridCell<uint32_t>(ids, x, z);
            if (!id)
                continue;
            char* const record = reinterpret_cast<CostFn>(addr::SC4CostTable_At)(base + 0x70C, &id);
            char* const parts = At<char*>(record, 0x1C);
            const auto partCount = static_cast<int32_t>((At<char*>(record, 0x20) - parts) / 10);
            char* const cell = CellRecord(self, x, z);
            for (uint32_t t = 0; t < 9; ++t) {
                if (t == 6)
                    continue;
                At<uint16_t>(cell, 2 * t) = 0;
                At<uint16_t>(cell, 0x12 + 2 * t) = 0xFFFF;
            }
            for (uint32_t d = 0; d < 4; ++d) {
                const int32_t nx = x + stepX[d];
                const int32_t nz = z + stepZ[d];
                if (!InCity(self, nx, nz))
                    continue;
                if (GridCell<uint32_t>(ids, nx, nz) == id) {
                    for (int32_t i = 0; i < partCount; ++i) {
                        const char* const part = parts + 10 * i;
                        if (part[0] || !part[1 + d])
                            continue;
                        for (uint32_t k = 0; k < 4; ++k) {
                            if (part[1 + k])
                                SetWay(self, nullptr, x, z, k, d, At<uint16_t>(const_cast<char*>(part), 8));
                        }
                    }
                    continue;
                }
                const uint32_t side = SideOf(nx, nz, x, z);
                char* const neighbour = CellRecord(self, nx, nz);
                for (int32_t i = 0; i < partCount; ++i) {
                    const char* const part = parts + 10 * i;
                    if (!part[1 + d])
                        continue;
                    const uint32_t fromType = At<uint16_t>(const_cast<char*>(part), 6);
                    const uint32_t toType = At<uint16_t>(const_cast<char*>(part), 8);
                    if (part[0]) {
                        char* edge = GridCell<char*>(base + field::kEdges, nx, nz);
                        while (edge && At<uint8_t>(edge, 6) != networkOf[toType])
                            edge = At<char*>(edge, 0x14);
                        if (edge) {
                            if (fromType == toType) {
                                for (uint32_t k = 0; k < 4; ++k)
                                    SetWay(self, nullptr, x, z, k, d, fromType);
                            } else {
                                const uint32_t shift = (d << 2) & 0xFFFF;
                                uint16_t& nibbles = At<uint16_t>(cell, 0x12 + 2 * fromType);
                                nibbles = static_cast<uint16_t>((nibbles & ~(0xFu << shift)) | (toType << shift));
                            }
                        }
                        for (uint32_t k = 0; k < 4; ++k) {
                            if (k == side)
                                continue;
                            const uint16_t ways = At<uint16_t>(neighbour, 2 * toType);
                            for (uint32_t j = 0; j < 4; ++j) {
                                if (ways & (1u << ((k + 4 * j) & 31))) {
                                    SetWay(self, nullptr, nx, nz, side, k, toType);
                                    break;
                                }
                            }
                        }
                        continue;
                    }
                    char* edge = GridCell<char*>(base + field::kEdges, nx, nz);
                    while (edge && At<uint8_t>(edge, 6) != networkOf[fromType])
                        edge = At<char*>(edge, 0x14);
                    if (!edge)
                        continue;
                    if (fromType != toType) {
                        const uint32_t shift = (side << 2) & 0xFFFF;
                        uint16_t& nibbles = At<uint16_t>(neighbour, 0x12 + 2 * fromType);
                        nibbles = static_cast<uint16_t>((nibbles & ~(0xFu << shift)) | (toType << shift));
                    } else {
                        for (uint32_t j = 0; j < 4; ++j) {
                            if (At<uint16_t>(neighbour, 2 * fromType) & (0xFu << (4 * j)))
                                SetWay(self, nullptr, nx, nz, j, side, fromType);
                        }
                    }
                    if (fromType == 1) {
                        for (uint32_t k = 0; k < 4; ++k) {
                            if (k == side)
                                continue;
                            const uint16_t ways = At<uint16_t>(neighbour, 2);
                            for (uint32_t j = 0; j < 4; ++j) {
                                if (ways & (1u << ((k + 4 * j) & 31))) {
                                    SetWay(self, nullptr, nx, nz, side, k, 1);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (int32_t x = xs; x <= xe; ++x) {
        for (int32_t z = zs; z <= ze; ++z) {
            if (FindLink(self, x, z))
                CellRecord(self, x, z)[0x2C] |= 2;
        }
    }

    char* const ferries = At<char*>(self, 0x104);
    for (char* ferry = At<char*>(ferries, 0); ferry != ferries; ferry = At<char*>(ferry, 0)) {
        const uint16_t travel = At<uint16_t>(ferry, 0x14);
        const int32_t ax = At<uint8_t>(ferry, 8);
        const int32_t az = At<uint8_t>(ferry, 9);
        if (ax >= xs && ax <= xe && az >= zs && az <= ze) {
            CellRecord(self, ax, az)[0x2C] |= 4;
            for (uint32_t network = 0; network <= 1; ++network) {
                char* edge = GridCell<char*>(base + field::kEdges, ax, az);
                while (edge && At<uint8_t>(edge, 6) != network)
                    edge = At<char*>(edge, 0x14);
                if (edge)
                    continue;
                edge = reinterpret_cast<EdgeFn>(addr::cSC4TrafficSimulator_GetOrCreateEdge)(self, network, ax, az);
                edge[4] = 1;
                edge[5] = 0;
                At<uint32_t>(edge, 0) = At<uint32_t>(self, 0xC00);
                At<uint32_t>(edge, 8) = 0;
            }
            for (uint32_t d = 0; d < 4; ++d) {
                const int32_t nx = ax + stepX[d];
                const int32_t nz = az + stepZ[d];
                if (!InCity(self, nx, nz))
                    continue;
                char* const neighbour = CellRecord(self, nx, nz);
                const uint32_t side = SideOf(nx, nz, ax, az);
                for (uint32_t t = 0; t < 9; ++t) {
                    uint16_t& ways = At<uint16_t>(neighbour, 2 * t);
                    if (travel & (1u << t)) {
                        for (uint32_t j = 0; j < 4; ++j) {
                            if (j == side)
                                continue;
                            if (ways & (0xFu << (4 * j)))
                                SetWay(self, nullptr, nx, nz, j, side, t);
                        }
                    } else {
                        for (uint32_t m = 0; m < 4; ++m)
                            ways = static_cast<uint16_t>(ways & ~(1u << ((side + 4 * m) & 31)));
                    }
                }
            }
        }
        const int32_t bx = At<uint8_t>(ferry, 0xA);
        const int32_t bz = At<uint8_t>(ferry, 0xB);
        if (bx < xs || bx > xe || bz < zs || bz > ze)
            continue;
        for (uint32_t d = 0; d < 4; ++d) {
            const int32_t nx = bx + stepX[d];
            const int32_t nz = bz + stepZ[d];
            if (!InCity(self, nx, nz))
                continue;
            char* const neighbour = CellRecord(self, nx, nz);
            const uint32_t side = SideOf(nx, nz, bx, bz);
            for (uint32_t t = 0; t < 9; ++t) {
                uint16_t& ways = At<uint16_t>(neighbour, 2 * t);
                if (travel & (1u << t)) {
                    bool any = false;
                    for (uint32_t j = 0; j < 4; ++j) {
                        if (j == side)
                            continue;
                        for (uint32_t m = 0; m < 4; ++m) {
                            if (ways & (1u << ((4 * j + m) & 31))) {
                                SetWay(self, nullptr, nx, nz, side, m, t);
                                any = true;
                            }
                        }
                    }
                    if (any) {
                        for (uint32_t m = 0; m < 4; ++m)
                            SetWay(self, nullptr, bx, bz, m, d, t);
                    }
                } else {
                    for (uint32_t m = 0; m < 4; ++m)
                        ways = static_cast<uint16_t>(ways & ~(1u << ((side + 4 * m) & 31)));
                }
            }
        }
    }

    At<uint8_t>(self, 0x62) = 1;
    At<uint8_t>(self, 0x881) = 0;
    At<uint8_t>(self, 0x888) = 1;
    for (char* node = At<char*>(unused, 0); node != unused;) {
        char* const next = At<char*>(node, 0);
        reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
        node = next;
    }
    At<char*>(unused, 0) = unused;
    At<char*>(unused, 4) = unused;
    reinterpret_cast<FreeFn>(addr::SC4_Free)(unused);
    reinterpret_cast<ClearFn>(addr::SC4List_Clear2)(&list);
    Virtual<uint32_t>(ListNodeAllocator(), 0x10, list);
}

// std::deque<uint32> (the game's, 0x80-byte blocks) as the traffic code
// inlines it: push_back with its own block buying, pop_back, and the
// destructor. Fields: first {cur, first, last, node}, last {...}, map, map
// size.
void DequePushBack(uint32_t* deque, uint32_t item)
{
    using ReserveFn = void(__thiscall*)(void* deque, uint32_t count, uint32_t atFront);
    using AllocateFn = uint32_t*(__cdecl*)(std::size_t size);
    auto* cur = reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(deque[4]));
    if (deque[4] != deque[6] - 4) {
        if (cur)
            *cur = item;
        deque[4] += 4;
        return;
    }
    if (deque[9] - ((deque[7] - deque[8]) >> 2) < 2) {
        reinterpret_cast<ReserveFn>(addr::SC4CellDeque_ReserveMapAtBack)(deque, 1, 0);
        cur = reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(deque[4]));
    }
    auto* const node = reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(deque[7] + 4));
    uint32_t* const block = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x80);
    *node = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(block));
    if (cur)
        *cur = item;
    deque[7] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(node));
    deque[5] = *node;
    deque[4] = *node;
    deque[6] = *node + 0x80;
}

uint32_t DequePopBack(uint32_t* deque)
{
    using FreeFn = void(__cdecl*)(void* memory);
    auto word = [](uint32_t address) { return *reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(address)); };
    if (deque[4] != deque[5]) {
        deque[4] -= 4;
        return word(deque[4]);
    }
    const uint32_t item = word(word(deque[7] - 4) + 0x80 - 4);
    if (deque[5])
        reinterpret_cast<FreeFn>(addr::SC4_Free)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(deque[5])));
    deque[7] -= 4;
    deque[5] = word(deque[7]);
    deque[6] = deque[5] + 0x80;
    deque[4] = deque[6] - 4;
    return item;
}

void DequeFree(uint32_t* deque)
{
    using FreeFn = void(__cdecl*)(void* memory);
    if (!deque[8])
        return;
    for (uint32_t node = deque[3]; node < deque[7] + 4; node += 4) {
        if (const uint32_t block = *reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(node)))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(block)));
    }
    reinterpret_cast<FreeFn>(addr::SC4_Free)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(deque[8])));
}

// 0x0071B9A0 (the sub-step, for the rectangle at +0x820 when +0x81C is
// set): each road edge (type 1, not +5 = 2) within +0xC68 cells of the
// rectangle gets its capacity (+0) from its kind (+0xC00 by +5), scaled by
// +0xC6C[depth] when a blocking cell is within +0xC68 steps: a depth-first
// walk (std::deque of {way, x, z}) along the open ways (word 1 of the
// cells' 46-byte records), bounded by the grid (+0x20 both ways), where a
// cell with an edge of kind above 2, or without a road edge but with an
// edge ID whose cost record has +0x30 set, is blocking; cells without a
// road edge and flag 2 of +0x2C end a branch. Edges of kind above 2 get
// factor 0.
void __fastcall UpdatePendingRect(void* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    using ConstructFn = void(__thiscall*)(void* deque, uint32_t count);
    using CostFn = char*(__thiscall*)(void* table, const uint32_t* key);
    const auto* const stepX = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDX);
    const auto* const stepZ = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDZ);
    const int32_t radius = At<uint8_t>(self, 0xC68);
    const int32_t xs = x0 - radius < 0 ? 0 : x0 - radius;
    const int32_t zs = z0 - radius < 0 ? 0 : z0 - radius;
    const int32_t xe = At<int32_t>(self, 0x28) < x1 + radius ? At<int32_t>(self, 0x28) : x1 + radius;
    const int32_t ze = At<int32_t>(self, 0x2C) < z1 + radius ? At<int32_t>(self, 0x2C) : z1 + radius;
    void* const ids = Virtual<void*>(At<void*>(self, 0x704), 0x60);
    uint32_t deque[10] = {};
    reinterpret_cast<ConstructFn>(addr::SC4CellDeque_Construct)(deque, 0);
    auto roadEdge = [self](int32_t x, int32_t z) {
        char* node = GridCell<char*>(static_cast<char*>(self) + field::kEdges, x, z);
        while (node && At<uint8_t>(node, 6) != 1)
            node = At<char*>(node, 0x14);
        return node;
    };
    for (int32_t x = xs; x <= xe; ++x) {
        for (int32_t z = zs; z <= ze; ++z) {
            char* const edge = roadEdge(x, z);
            if (!edge || At<uint8_t>(edge, 5) == 2)
                continue;
            int32_t limit = 0;
            if (At<uint8_t>(edge, 4) <= 2) {
                limit = radius + 1;
                int32_t depth = 0;
                uint32_t way = 0;
                int32_t cx = x, cz = z;
                for (;;) {
                    bool stop = false;
                    if (way == 0) {
                        if (char* const here = roadEdge(cx, cz)) {
                            if (At<uint8_t>(here, 4) > 2)
                                limit = depth;
                        } else {
                            uint32_t id = GridCell<uint32_t>(ids, cx, cz);
                            if (id && At<uint8_t>(reinterpret_cast<CostFn>(addr::SC4CostTable_At)(static_cast<char*>(self) + 0x70C, &id), 0x30))
                                limit = depth;
                            stop = true;
                        }
                    }
                    bool go = false;
                    const char* const cell = CellRecord(self, cx, cz);
                    if (!stop && depth < limit && !(cell[0x2C] & 2)) {
                        while (way < 4) {
                            bool open = false;
                            for (uint32_t k = 0; k < 4 && !open; ++k)
                                open = (At<uint16_t>(const_cast<char*>(cell), 2) & (1u << ((way + 4 * k) & 31))) != 0;
                            if (open) {
                                const int32_t nx = cx + stepX[way];
                                const int32_t nz = cz + stepZ[way];
                                const int32_t size = At<int32_t>(self, 0x20);
                                if (nx > -1 && nx < size && nz > -1 && nz < size) {
                                    go = true;
                                    break;
                                }
                            }
                            ++way;
                        }
                    }
                    if (go) {
                        DequePushBack(deque, (way << (2 * CellBits())) | CellKey(cx, cz));
                        cx += stepX[way];
                        cz += stepZ[way];
                        way = 0;
                        ++depth;
                        continue;
                    }
                    if (deque[4] == deque[0])
                        break;
                    const uint32_t item = DequePopBack(deque);
                    way = ((item >> (2 * CellBits())) & 0xFF) + 1;
                    cz = KeyZ(item);
                    cx = KeyX(item);
                    --depth;
                }
            }
            const float capacity = At<float>(self, 0xC00 + 4 * At<uint8_t>(edge, 5));
            At<float>(edge, 0) = capacity;
            if (limit < radius)
                At<float>(edge, 0) = static_cast<float>(static_cast<double>(capacity) * At<float*>(self, 0xC6C)[limit]);
        }
    }
    DequeFree(deque);
}

// 0x00717EC0 (phase 12): marks the edges of a network reachable from one
// side of an edge (x, z): a depth-first walk (a std::deque<uint32> of
// cells {x, z, side, next direction}, used as the game does) stamps each
// edge side it leaves with +0x60 and follows each open way out (the
// network's word of the cell's 46-byte record, bit way + 4 x side): to the
// neighbouring edge of the network (across a span of one edge ID, +0x704
// slot 24, when the neighbour has none), along the city edge (same cell,
// the way taken as the side), or, after the four ways and with flag 2 of
// +0x2C, through the link table (+0xC8). Stops at sides already stamped.
// Bumps +0x60 when anything was marked.
void __fastcall SpreadFromEdge(void* self, void*, int32_t startX, int32_t startZ, uint32_t network, uint32_t startSide)
{
    using ConstructFn = void(__thiscall*)(void* deque, uint32_t count);
    using PushFn = void(__thiscall*)(void* deque, const uint32_t* item);
    using DestroyFn = void(__thiscall*)(void* deque);
    using EdgeFn = char*(__thiscall*)(void* self, uint32_t network, int32_t x, int32_t z);
    using FreeFn = void(__cdecl*)(void* memory);
    uint32_t deque[10] = {}; // first {cur, first, last, node}, last {...}, map, size
    reinterpret_cast<ConstructFn>(addr::SC4CellDeque_Construct)(deque, 0);
    const auto destroy = [&deque]() { reinterpret_cast<DestroyFn>(addr::SC4Uint32Deque_Destroy)(deque); };
    const auto edgeOf = reinterpret_cast<EdgeFn>(addr::cSC4TrafficSimulator_EdgeOfNetwork);
    const uint32_t word = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_RecordWordOfNetwork)[network];
    const auto* const stepX = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDX);
    const auto* const stepZ = reinterpret_cast<const int32_t*>(addr::SC4Traffic_SideDZ);
    const auto* const opposite = reinterpret_cast<const uint32_t*>(addr::SC4Traffic_OppositeSide);
    auto findEdge = [self, network](int32_t x, int32_t z) {
        char* node = GridCell<char*>(static_cast<char*>(self) + field::kEdges, x, z);
        while (node && At<uint8_t>(node, 6) != network)
            node = At<char*>(node, 0x14);
        return node;
    };
    auto record = [self](int32_t x, int32_t z) { return CellRecord(self, x, z); };
    auto ways = [word](const char* cell) { return At<uint16_t>(const_cast<char*>(cell), 2 * word); };
    auto stamp = [](char* edge, uint32_t side) -> uint16_t& { return At<uint16_t>(edge, 0xC + 2 * side); };

    char* node = findEdge(startX, startZ);
    if (!node || stamp(node, startSide) != 0 || At<uint8_t>(node, 4) > 2 || (record(startX, startZ)[0x2C] & 2)) {
        destroy();
        return;
    }
    void* const ids = Virtual<void*>(At<void*>(self, 0x704), 0x60); // slot 24: the edge-ID grid
    bool changed = false;
    // The game's cursor is four bytes {x, z, side, way}, pushed as one word;
    // a big city's packs x and z in 10 bits each.
    struct {
        int32_t x, z;
        uint8_t side, way;
    } cur = {startX, startZ, static_cast<uint8_t>(startSide), 0};
    auto pack = [&cur]() -> uint32_t {
        const uint32_t bits = CellBits();
        const uint32_t mask = (1u << bits) - 1;
        return (static_cast<uint32_t>(cur.x) & mask) | (static_cast<uint32_t>(cur.z) & mask) << bits |
               static_cast<uint32_t>(cur.side) << (2 * bits) | static_cast<uint32_t>(cur.way) << 24;
    };
    auto unpack = [&cur](uint32_t item) {
        const uint32_t bits = CellBits();
        const uint32_t mask = (1u << bits) - 1;
        cur.x = static_cast<int32_t>(item & mask);
        cur.z = static_cast<int32_t>((item >> bits) & mask);
        cur.side = static_cast<uint8_t>((item >> (2 * bits)) & 0xF);
        cur.way = static_cast<uint8_t>(item >> 24);
    };
    if (!g_wideCells) {
        cur.x &= 0xFF;
        cur.z &= 0xFF;
    }
    for (;;) {
        const int32_t x = cur.x;
        const int32_t z = cur.z;
        const char* const cell = record(x, z);
        bool found = false;
        char* next = nullptr;
        int32_t nextX = 0, nextZ = 0;
        uint32_t nextSide = 0;
        while (cur.way < 4 && !found) {
            const uint32_t way = cur.way;
            const uint32_t side = cur.side;
            if (ways(cell) & (1u << ((way + 4 * side) & 31))) {
                stamp(node, side) = At<uint16_t>(self, 0x60);
                int32_t tx = x + stepX[way];
                int32_t tz = z + stepZ[way];
                changed = true;
                if (!InCity(self, tx, tz)) {
                    if ((ways(cell) & (1u << ((side + 4 * way) & 31))) && stamp(node, way) == 0) {
                        found = true;
                        next = node;
                        nextX = x;
                        nextZ = z;
                        nextSide = way;
                    }
                } else {
                    const uint32_t back = opposite[way];
                    char* other = edgeOf(self, network, tx, tz);
                    if (!other) {
                        const uint32_t id = GridCell<uint32_t>(ids, tx, tz);
                        if (id) {
                            for (;;) {
                                tz += stepZ[way];
                                tx += stepX[way];
                                if (!InCity(self, tx, tz))
                                    break;
                                if (GridCell<uint32_t>(ids, tx, tz) == id)
                                    continue;
                                other = edgeOf(self, network, tx, tz);
                                break;
                            }
                        }
                    }
                    if (other && stamp(other, back) == 0) {
                        found = true;
                        next = other;
                        nextX = tx;
                        nextZ = tz;
                        nextSide = back;
                    }
                }
            }
            ++cur.way;
        }
        if (!found && cur.way < 5 && (cell[0x2C] & 2)) {
            cur.way = 5;
            char* const link = FindLink(self, x, z);
            if (link && (At<uint16_t>(link, 0xA) & (1u << (word & 31)))) {
                const uint32_t side = cur.side;
                stamp(node, side) = At<uint16_t>(self, 0x60);
                const int32_t lx = LinkedX(link);
                const int32_t lz = LinkedZ(link);
                char* const other = edgeOf(self, network, lx, lz);
                if (other && stamp(other, side) == 0) {
                    found = true;
                    next = other;
                    nextX = lx;
                    nextZ = lz;
                    nextSide = side;
                }
            }
        }
        if (found) {
            const uint32_t item = pack();
            if (deque[4] != deque[6] - 4) {
                if (deque[4])
                    *reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(deque[4])) = item;
                deque[4] += 4;
            } else {
                reinterpret_cast<PushFn>(addr::SC4Uint32Deque_PushBackSlow)(deque, &item);
            }
            cur = {nextX, nextZ, static_cast<uint8_t>(nextSide), 0};
            if (!g_wideCells) {
                cur.x &= 0xFF;
                cur.z &= 0xFF;
            }
            node = next;
            continue;
        }
        if (deque[4] == deque[0]) {
            if (changed)
                ++At<uint16_t>(self, 0x60);
            destroy();
            return;
        }
        uint32_t item;
        if (deque[4] == deque[5]) {
            const uint32_t previous = *reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(deque[7] - 4));
            item = *reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(previous + 0x80 - 4));
            if (deque[5])
                reinterpret_cast<FreeFn>(addr::SC4_Free)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(deque[5])));
            deque[7] -= 4;
            const uint32_t block = *reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(deque[7]));
            deque[6] = block + 0x80;
            deque[5] = block;
            deque[4] = block + 0x80 - 4;
        } else {
            deque[4] -= 4;
            item = *reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(deque[4]));
        }
        unpack(item);
        node = findEdge(cur.x, cur.z);
        if (!node) {
            destroy();
            return;
        }
    }
}

// 0x0070E320: whether a recorded route is still open: walking its cells
// (the route iterator), each city cell's 46-byte record (+0x58) must allow
// it. The first: any commute's bit for its exit side, or, when the type
// changes there, the type's change nibble for that side (+0x12 by type) is
// the new type. The others: the bit for side in and side out, else flag 2
// of +0x2C, or flag 4 when there is no strike (+0x7A4 = 1); a type change
// as for the first, and never during a strike. Cells leaving by side 0xFF
// are not checked. False for an empty route.
uint8_t __fastcall RouteStillOpen(void* self, void*, char* route)
{
    using InitFn = void(__thiscall*)(void* iterator, char* trip);
    using NextFn = uint8_t(__thiscall*)(void* iterator, RouteStep* step);
    const bool strike = At<uint32_t>(self, 0x7A4) == 1;
    uint32_t iterator[6] = {};
    reinterpret_cast<InitFn>(addr::SC4RouteIterator_Init)(iterator, route);
    RouteStep step;
    if (!reinterpret_cast<NextFn>(addr::SC4RouteIterator_Next)(iterator, &step))
        return 0;
    auto record = [self](int32_t x, int32_t z) {
        return reinterpret_cast<const uint8_t*>(CellRecord(self, x, z));
    };
    auto changeNibble = [](const uint8_t* cell, uint8_t type, uint8_t side) {
        uint16_t nibbles;
        std::memcpy(&nibbles, cell + 0x12 + 2 * type, 2);
        return static_cast<uint8_t>((nibbles >> ((side << 2) & 0xFF)) & 0xF);
    };
    if (InCity(self, step.x, step.z) && step.to != 0xFF) {
        const uint8_t* const cell = record(step.x, step.z);
        bool open = false;
        for (uint32_t commute = 0; commute < 4 && !open; ++commute) {
            if (step.type == step.toType) {
                uint16_t bits;
                std::memcpy(&bits, cell + 2 * step.type, 2);
                open = (bits & (1u << ((step.to + 4 * commute) & 31))) != 0;
            } else {
                open = changeNibble(cell, step.type, step.to) == step.toType;
            }
        }
        if (!open)
            return 0;
    }
    while (reinterpret_cast<NextFn>(addr::SC4RouteIterator_Next)(iterator, &step)) {
        if (!InCity(self, step.x, step.z) || step.to == 0xFF)
            continue;
        const uint8_t* const cell = record(step.x, step.z);
        if (step.type == step.toType) {
            uint16_t bits;
            std::memcpy(&bits, cell + 2 * step.type, 2);
            if (bits & (1u << ((step.to + 4 * step.from) & 0xFF & 31)))
                continue;
            const uint8_t flags = cell[0x2C];
            if (flags & 2)
                continue;
            if (!(flags & 4) || strike)
                return 0;
        } else {
            if (strike || changeNibble(cell, step.type, step.to) != step.toType)
                return 0;
        }
    }
    return 1;
}

// 0x0071D920 (phase 16): one lot's trips (lot slot 3: a list; trip at
// node + 8: route vectors +0 and +0xC, count +0x18, kind +0x1C, +0x1D,
// flags +0x1E, state +0x1F, cell +0x20/+0x22). Counts the visit (+0x954)
// and the trips waiting (+0x964); every fourth visit (by +0x950) merges
// routed trips with the same +0x1C/+0x1D into one (+0x95C counts the
// merged); routed trips the origin can no longer send go back to waiting
// (+0x958), the others, outside a merge visit and while their route is
// still open (0x0070E320), have it recorded again (by kind: 3 as kind 0
// with flags 3; bit 0 of +0x1E as kind 2; else the two halves as kinds 0
// and 1, the second only while also open, +0x960 otherwise; bit 1 of
// +0x1E skips the recording) and stay routed (+0x948); unroutable ones
// (5) go back to waiting (+0x968). Then the states are counted (+0x940
// for 1, +0x944 for 0).
void __fastcall VisitLot16(void* self, void*, void* lot)
{
    using CanRouteFn = uint8_t(__thiscall*)(void* self, char* trip);
    using OpenFn = uint8_t(__thiscall*)(void* self, char* route);
    using RecordFn = void(__thiscall*)(void* self, char* trip, uint32_t value, Rect16 rect, uint32_t x, uint32_t z,
                                       uint32_t kind, uint32_t flags);
    using FreeFn = void(__cdecl*)(void* memory);
    char* const base = static_cast<char*>(self);
    char* const head = *Virtual<char**>(lot, 0xC);
    const uint32_t visit = ++At<uint32_t>(self, 0x954);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        if (!node[0x27])
            ++At<uint32_t>(self, 0x964);
    }
    const bool merge = (visit & 3) == At<uint32_t>(self, 0x950);
    if (merge) {
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
            if (node[0x27] != 3)
                continue;
            for (char* other = At<char*>(node, 0); other != head;) {
                if (other[0x24] != node[0x24] || other[0x25] != node[0x25]) {
                    other = At<char*>(other, 0);
                    continue;
                }
                At<uint32_t>(node, 0x20) += At<uint32_t>(other, 0x20);
                node[0x27] = 0;
                Virtual<void*>(lot, 0xC);
                char* const next = At<char*>(other, 0);
                char* const prev = At<char*>(other, 4);
                At<char*>(prev, 0) = next;
                At<char*>(next, 4) = prev;
                if (At<void*>(other, 0x14))
                    reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(other, 0x14));
                if (At<void*>(other, 8))
                    reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(other, 8));
                reinterpret_cast<FreeFn>(addr::SC4_Free)(other);
                other = next;
            }
            if (!node[0x27])
                ++At<uint32_t>(self, 0x95C);
        }
    }
    auto rect = [lot]() { // slot 5 fills a buffer and returns where the rectangle is
        Rect16 buffer;
        Rect16 r;
        std::memcpy(&r, Virtual<const void*>(lot, 0x14, &buffer), sizeof r);
        return r;
    };
    const auto record = reinterpret_cast<RecordFn>(addr::cSC4TrafficSimulator_RecordRoute);
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        char* const trip = node + 8;
        ++At<uint32_t>(self, 0x914);
        uint8_t& state = reinterpret_cast<uint8_t&>(trip[0x1F]);
        if (state == 3) {
            if (!reinterpret_cast<CanRouteFn>(addr::cSC4TrafficSimulator_CanRouteTrip)(self, trip)) {
                state = 0;
                ++At<uint32_t>(self, 0x958);
            } else {
                state = trip[0x1C] != 3 ? 1 : 0;
                if (!merge && reinterpret_cast<OpenFn>(addr::cSC4TrafficSimulator_RouteStillOpen)(self, trip)) {
                    const uint8_t flags = static_cast<uint8_t>(trip[0x1E]);
                    const uint8_t kind = static_cast<uint8_t>(trip[0x1C]);
                    const bool skip = (flags >> 1) & 1;
                    const uint32_t value = At<uint32_t>(trip, 0x18);
                    const uint32_t x = At<uint16_t>(trip, 0x20);
                    const uint32_t z = At<uint16_t>(trip, 0x22);
                    bool routed = true;
                    if (kind == 3) {
                        if (!skip)
                            record(base, trip, value, rect(), x, z, 0, 3);
                    } else if (flags & 1) {
                        if (!skip)
                            record(base, trip, value, rect(), x, z, 2, kind);
                    } else if (!reinterpret_cast<OpenFn>(addr::cSC4TrafficSimulator_RouteStillOpen)(self, trip + 0xC)) {
                        ++At<uint32_t>(self, 0x960);
                        routed = false;
                    } else if (!skip) {
                        record(base, trip, value, rect(), x, z, 0, kind);
                        record(base, trip + 0xC, value, rect(), x, z, 1, kind);
                    }
                    if (routed) {
                        state = 3;
                        ++At<uint32_t>(self, 0x948);
                    }
                }
            }
        } else if (state == 5) {
            state = 0;
            ++At<uint32_t>(self, 0x968);
        }
        if (state == 1)
            ++At<uint32_t>(self, 0x940);
        else if (state == 0)
            ++At<uint32_t>(self, 0x944);
    }
}

void* g_originalAddCongestion = nullptr;
void* g_originalEdgeSide = nullptr;
void* g_originalCellHasTraffic = nullptr;
void* g_originalClearRoadEdges = nullptr;
void* g_originalFinishStats1 = nullptr;
void* g_originalFinishStats2 = nullptr;
void* g_originalKind7 = nullptr;
void* g_originalPeak = nullptr;
void* g_originalBlock = nullptr;
void* g_originalClearBorder = nullptr;
void* g_originalBuildingOfKind = nullptr;
void* g_originalKind1 = nullptr;
void* g_originalChangeActive = nullptr;
void* g_originalRebuild = nullptr;
void* g_originalRouteToCell = nullptr;
void* g_originalNetworkAtEdge = nullptr;
void* g_originalStrategies = nullptr;
void* g_originalLinkRoutes = nullptr;
void* g_originalNetworkBuilding = nullptr;
void* g_originalRefreshCost = nullptr;
void* g_originalVisitLot = nullptr;
void* g_originalStillOpen = nullptr;
void* g_originalSpread = nullptr;
void* g_originalPendingRect = nullptr;
void* g_originalTripCounts = nullptr;
void* g_originalWalk13 = nullptr;
void* g_originalLotRemoved = nullptr;
void* g_originalLotAdded = nullptr;
void* g_originalFerryTerminal = nullptr;
void* g_originalSetWay = nullptr;
void* g_originalMarkPending = nullptr;
void* g_originalSideOf = nullptr;
void* g_originalCellsChanged = nullptr;
void* g_originalRoadPiece = nullptr;
void* g_originalRemoveEdge = nullptr;
void* g_originalGetEdge = nullptr;

bool InstallHelpers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const struct {
        std::uintptr_t address;
        void* ours;
        void** original;
    } hooks[] = {
        {addr::cSC4TrafficSimulator_AddCongestionEffect, reinterpret_cast<void*>(&AddCongestionEffect), &g_originalAddCongestion},
        {addr::cSC4TrafficSimulator_EdgeSide, reinterpret_cast<void*>(&EdgeSide), &g_originalEdgeSide},
        {addr::cSC4TrafficSimulator_CellHasTraffic, reinterpret_cast<void*>(&CellHasTraffic), &g_originalCellHasTraffic},
        {addr::cSC4TrafficSimulator_ClearRoadEdges, reinterpret_cast<void*>(&ClearRoadEdgesThunk), &g_originalClearRoadEdges},
        {addr::cSC4TrafficSimulator_FinishStats1, reinterpret_cast<void*>(&FinishStats1), &g_originalFinishStats1},
        {addr::cSC4TrafficSimulator_FinishStats2, reinterpret_cast<void*>(&FinishStats2), &g_originalFinishStats2},
        {addr::cSC4TrafficSimulator_OnKind7Building, reinterpret_cast<void*>(&OnKind7Building), &g_originalKind7},
        {addr::cSC4TrafficSimulator_CellTrafficPeak, reinterpret_cast<void*>(&CellTrafficPeak), &g_originalPeak},
        {addr::cSC4TrafficSimulator_BlockTraffic, reinterpret_cast<void*>(&BlockTraffic), &g_originalBlock},
        {addr::cSC4TrafficSimulator_OnMessageEA0D163E, reinterpret_cast<void*>(&ClearBorderNetwork), &g_originalClearBorder},
        {addr::cSC4TrafficSimulator_OnBuildingOfKind, reinterpret_cast<void*>(&OnBuildingOfKind), &g_originalBuildingOfKind},
        {addr::cSC4TrafficSimulator_OnKind1Building, reinterpret_cast<void*>(&OnKind1Building), &g_originalKind1},
        {addr::cSC4TrafficSimulator_ChangeActive, reinterpret_cast<void*>(&ChangeActive), &g_originalChangeActive},
        {addr::cSC4TrafficSimulator_RebuildNetworks, reinterpret_cast<void*>(&RebuildNetworks), &g_originalRebuild},
        {addr::cSC4TrafficSimulator_RouteToCell19, reinterpret_cast<void*>(&RouteToCell19), &g_originalRouteToCell},
        {addr::cSC4TrafficSimulator_OnMessage8A0D15DA, reinterpret_cast<void*>(&OnNetworkAtEdge), &g_originalNetworkAtEdge},
        {addr::cSC4TrafficSimulator_LoadTravelStrategies, reinterpret_cast<void*>(&LoadTravelStrategies), &g_originalStrategies},
        {addr::cSC4TrafficSimulator_LinkRoutes19, reinterpret_cast<void*>(&LinkRoutes19), &g_originalLinkRoutes},
        {addr::cSC4TrafficSimulator_OnNetworkBuilding, reinterpret_cast<void*>(&OnNetworkBuilding), &g_originalNetworkBuilding},
        {addr::cSC4TrafficSimulator_RefreshCost, reinterpret_cast<void*>(&RefreshCost), &g_originalRefreshCost},
        {addr::cSC4TrafficSimulator_VisitLot16, reinterpret_cast<void*>(&VisitLot16), &g_originalVisitLot},
        {addr::cSC4TrafficSimulator_RouteStillOpen, reinterpret_cast<void*>(&RouteStillOpen), &g_originalStillOpen},
        {addr::cSC4TrafficSimulator_SpreadFromEdge, reinterpret_cast<void*>(&SpreadFromEdge), &g_originalSpread},
        {addr::cSC4TrafficSimulator_UpdatePendingRect, reinterpret_cast<void*>(&UpdatePendingRect), &g_originalPendingRect},
        {addr::cSC4TrafficSimulator_StartTripCounts, reinterpret_cast<void*>(&StartTripCounts), &g_originalTripCounts},
        {addr::cSC4TrafficSimulator_WalkCells13, reinterpret_cast<void*>(&WalkCells13), &g_originalWalk13},
        {addr::cSC4TrafficSimulator_OnNetworkObjectAdded, reinterpret_cast<void*>(&OnTransitLotRemoved), &g_originalLotRemoved},
        {addr::cSC4TrafficSimulator_OnNetworkObjectRemoved, reinterpret_cast<void*>(&OnTransitLotAdded), &g_originalLotAdded},
        {addr::cSC4TrafficSimulator_OnFerryTerminal, reinterpret_cast<void*>(&OnFerryTerminal), &g_originalFerryTerminal},
        {addr::cSC4TrafficSimulator_SetWay, reinterpret_cast<void*>(&SetWay), &g_originalSetWay},
        {addr::cSC4TrafficSimulator_MarkPending, reinterpret_cast<void*>(&MarkPending), &g_originalMarkPending},
        {addr::SC4Traffic_SideOf, reinterpret_cast<void*>(&SideOf), &g_originalSideOf},
        {addr::cSC4TrafficSimulator_OnCellsChanged, reinterpret_cast<void*>(&OnCellsChanged), &g_originalCellsChanged},
        {addr::cSC4TrafficSimulator_OnRoadPiece, reinterpret_cast<void*>(&OnRoadPiece), &g_originalRoadPiece},
        {addr::cSC4TrafficSimulator_RemoveEdge, reinterpret_cast<void*>(&RemoveEdge), &g_originalRemoveEdge},
        {addr::cSC4TrafficSimulator_GetOrCreateEdge, reinterpret_cast<void*>(&GetOrCreateEdge), &g_originalGetEdge},
    };
    for (const auto& entry : hooks) {
        if (Skipped(entry.address))
            continue;
        const addr::Entry* found = addr_check::Find(entry.address);
        if (!found || !hook::Install(*found, entry.ours, entry.original))
            return false;
    }
    return true;
}

const bool g_registeredHelpers = registry::Register({
    "simulators.traffic.helpers",
    "simulators/traffic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficSimulator_AddCongestionEffect,
    "Our traffic simulator's helper functions (docs/traffic.md)",
    false,
    InstallHelpers,
});

} // namespace
} // namespace opensc4::traffic_sim
