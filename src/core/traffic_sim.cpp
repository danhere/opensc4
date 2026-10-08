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
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

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

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("traffic: first %s", name);
}

// A chained hash table keyed by a cell (x << 8 | z, 16 bits): whether the
// key is in it.
bool InCellTable(void* self, std::size_t table, uint8_t x, uint32_t z)
{
    const auto key = static_cast<uint16_t>((static_cast<uint32_t>(x) << 8) | z);
    void** const buckets = At<void**>(self, table);
    const auto count = static_cast<uint32_t>(At<void**>(self, table + 4) - buckets);
    int found = 0;
    for (void* node = buckets[key % count]; node; node = *static_cast<void**>(node))
        found += At<uint16_t>(node, 0x4) == key;
    return found != 0;
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
// x << 8 | z), or the game's empty record (0xAB31D0).
const void* __fastcall GetTrafficEdgeDensity(void* self, void*, uint8_t x, uint8_t z, uint8_t travelType, uint8_t evening)
{
    static bool logged = false;
    FirstCall("GetTrafficEdgeDensity", logged);
    const uint32_t key = (((((evening ? 1u : 0u) << 8) | travelType) << 8 | x) << 8) | z;
    char** const buckets = At<char**>(self, 0x17C);
    const auto count = static_cast<uint32_t>(At<char**>(self, 0x180) - buckets);
    for (char* node = buckets[key % count]; node; node = At<char*>(node, 0))
        if (At<uint32_t>(node, 0x4) == key)
            return node + 0x8;
    return reinterpret_cast<const void*>(addr::cSC4TrafficSimulator_EmptyEdgeDensity);
}

// 16 (0x00720070).
uint8_t __fastcall IsRoadDamaged(void* self, void*, uint8_t x, uint32_t z)
{
    return InCellTable(self, field::kDamagedRoads, x, z) ? 1 : 0;
}

// 17 (0x00711590): a rail crossing at the cell, and a draw of the
// simulator's generator below the accident chance (+0xD04).
uint32_t __fastcall CheckRailAccident(void* self, void*, uint8_t x, uint32_t z)
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
            return Virtual<char*>(self, 0x30, static_cast<uint8_t>(x), static_cast<uint8_t>(z),
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

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x0071FFA0, GetSimulatorType),
    OPENSC4_SLOT(8, 0x006AAC50, FieldGetter<0x30>),
    OPENSC4_SLOT(9, 0x0080BC00, FieldGetter<0x38>),
    OPENSC4_SLOT(10, 0x00698CF0, FieldGetter<0x48>),
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
    OPENSC4_SLOT(32, 0x0071FFF0, GetFerryTerminalCount),
    OPENSC4_SLOT(33, 0x0070EC10, GetWaterRoute),
    OPENSC4_SLOT(34, 0x00720000, GetTrafficStats),
    OPENSC4_SLOT(39, 0x00711B50, GetSubnetworksForLot),
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
        void* const lot = Virtual<void*>(lots, 0x18, static_cast<uint32_t>(static_cast<uint8_t>(node[0x14])),
                                         static_cast<uint32_t>(static_cast<uint8_t>(node[0x15])), 0u);
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

bool InstallMonth(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4TrafficSimulator_OnMonthPassed);
    const addr::Entry* first = addr_check::Find(addr::cSC4TrafficSimulator_ApplyFirstFunding);
    const addr::Entry* second = addr_check::Find(addr::cSC4TrafficSimulator_ApplySecondFunding);
    const addr::Entry* strike = addr_check::Find(addr::cSC4TrafficSimulator_CheckStrike);
    return game::IsBuild641() && entry && first && second && strike &&
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
    using InsertFn = void(__thiscall*)(void* set, const uint32_t* key);
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
                const auto insert = reinterpret_cast<InsertFn>(addr::SC4Uint32Set_Insert);
                for (uint32_t i = 0; i < 9; ++i) {
                    uint32_t key = (((i << 8) | static_cast<uint8_t>(x)) << 8) | static_cast<uint8_t>(z);
                    insert(set, &key);
                    key = ((((i | 0x100) << 8) | static_cast<uint8_t>(x)) << 8) | static_cast<uint8_t>(z);
                    insert(set, &key);
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
    case 0xC2C09554: reinterpret_cast<SelfFn>(addr::cSC4TrafficSimulator_OnPlantDisaster)(self); return 1;
    case 0xC53D10AA:
        if (Virtual<uint32_t>(message, 0x24) == 0xE7E2C2DB && Virtual<uint32_t>(message, 0x38) == 0xC9133286)
            reinterpret_cast<SelfFn>(addr::cSC4TrafficSimulator_OnMessageC53D10AA)(self);
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

// The rows of a sim grid (its slot 24 returns the vector of row vectors).
template <typename T>
T& GridCell(void* rows, int32_t x, int32_t z)
{
    return reinterpret_cast<T*>(static_cast<char***>(rows)[0][3 * x])[z];
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
// z) in the hash table at +0x17C, or null.
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
void ClearDensities(void* self, uint8_t x, uint8_t z)
{
    for (uint32_t type = 0; type < 9; ++type) {
        for (uint32_t evening = 0; evening < 2; ++evening) {
            if (char* const node = DensityNode(self, (((evening << 8 | type) << 8 | x) << 8) | z))
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
        ClearDensities(self, static_cast<uint8_t>(x), static_cast<uint8_t>(z));
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
    reinterpret_cast<FinishFn>(addr::SC4TrafficZones_Finish)(static_cast<char*>(self) + 0x70, static_cast<char*>(self) + 0x64);
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
    "Our traffic sub-step phases 0, 2-8, 10-13, 15, 16 and 19 (docs/traffic.md)",
    false,
    InstallPhases,
});

} // namespace
} // namespace opensc4::traffic_sim
