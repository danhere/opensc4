// Our cSC4PlumbingSimulator (build 641), first part: its interface,
// swapped into the original object's vtable (docs/water.md). Most getters
// are one field; the rest go through the supply grid (+0x34), the list of
// water producers (+0xBC) or a building's properties, as the game's do.
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
#include <cstring>
#include <utility>

namespace opensc4::water_sim {
namespace {

// Fields of cSC4PlumbingSimulator used here (offsets from the object's start).
namespace field {
constexpr std::size_t kSupplyGrid = 0x34;        // cISC4SimGrid<uint8_t>*: bit 0 watered, bits 0-1 both = watered buildings
constexpr std::size_t kDepartment = 0xA4;
constexpr std::size_t kProducers = 0xBC;         // list sentinel; node +0x34 the building
constexpr std::size_t kCapacity = 0x110;         // slots 14-17, 23, 13
constexpr std::size_t kConsumed = 0x118;
constexpr std::size_t kImported = 0x120;
constexpr std::size_t kExported = 0x128;
constexpr std::size_t kDemand = 0x130;
constexpr std::size_t kPipes = 0x174;            // slots 18-20
constexpr std::size_t kDistressedPipes = 0x178;
constexpr std::size_t kBurstPipes = 0x17C;
constexpr std::size_t kBuildings = 0x188;        // 21, 22
constexpr std::size_t kWateredBuildings = 0x18C;
constexpr std::size_t kPollutedSources = 0x198;
} // namespace field

// A producer node's fields (from the node's start; +0x0/+0x4 are the links).
namespace producer {
constexpr std::size_t kCapacity = 0x24;
constexpr std::size_t kProduction = 0x28;
constexpr std::size_t kValue2C = 0x2C;
constexpr std::size_t kFlag30 = 0x30;            // uint8
constexpr std::size_t kBuilding = 0x34;
} // namespace producer

constexpr uint32_t kPurposeWater = 0xCA58C9D5;

using GetPropertyUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetPropertyFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using LoadCurveFromPropertyFn = uint8_t(__thiscall*)(void* curve, void* property);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using FreeFn = void(__cdecl*)(void* p);
const auto GetPropertyUint32 = reinterpret_cast<GetPropertyUint32Fn>(addr::SC4_GetPropertyUint32);
using GetPropertySint32WaterFn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
const auto GetPropertySint32Water = reinterpret_cast<GetPropertySint32WaterFn>(addr::SC4_GetPropertySint32);
const auto GetPropertyFloat = reinterpret_cast<GetPropertyFloatFn>(addr::SC4_GetPropertyFloat);
const auto LoadCurveFromProperty = reinterpret_cast<LoadCurveFromPropertyFn>(addr::SC4Curve_LoadFromProperty);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void* Grid(void* self) { return At<void*>(self, field::kSupplyGrid); }

void* CityHelper(void* self) { return At<void*>(self, 0x18); } // cell counts (slots 93, 94), the view (84)

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("water: first %s", name);
}

// A grid's cells by row (grid slot 24 returns its vector of row vectors;
// each row is begin, end, capacity, so row x starts at [3 * x]).
uint8_t* const* Rows(void* grid)
{
    return *Virtual<uint8_t* const* const*>(grid, 0x60);
}

// The producer node for a building, or null.
char* FindProducer(void* self, void* building)
{
    void* const sentinel = At<void*>(self, field::kProducers);
    for (void* node = *static_cast<void**>(sentinel); node != sentinel; node = *static_cast<void**>(node))
        if (At<void*>(node, producer::kBuilding) == building)
            return static_cast<char*>(node);
    return nullptr;
}

// cISC4BudgetSimulator::BudgetItem, as in the power simulator.
struct BudgetItem {
    uint32_t departmentId;
    uint32_t lineItem;
    uint32_t purpose;
    uint32_t padding;
    int64_t cost;
    uint32_t index;
    float localFundingPercentage;
};
static_assert(sizeof(BudgetItem) == 0x20);

struct Curve {
    void* points = nullptr;
    void* end = nullptr;
    void* capacity = nullptr;
    int32_t count = 0;
};

// --- The getters -------------------------------------------------------------

// 5 (0x006DA440): the supply grid's value (grid slot 22).
uint32_t __fastcall GetSupplyValue(void* self, void*, int32_t x, int32_t z)
{
    return Virtual<uint32_t>(Grid(self), 0x58, x, z);
}

// 6 (0x006DA450).
void __fastcall GetSupplyValueMapPtr(void* self, void*, void*& grid)
{
    grid = Grid(self);
}

// 7 (0x00A6CAB0, a getter shared with other classes).
void* __fastcall GetWateredGrid(void* self, void*)
{
    return Grid(self);
}

// 8 (0x006DAD50): both low bits of the cell (grid slot 24, the rows).
uint32_t __fastcall AreWateredBuildingsInCell(void* self, void*, int32_t x, int32_t z)
{
    uint8_t* const* const rows = Rows(Grid(self));
    return (rows[3 * x][z] & 3) == 3 ? 1 : 0;
}

// 9 (0x006DA6A0): the building's cell (its position, grid slot 19), then slot 8.
uint32_t __fastcall IsBuildingWatered(void* self, void*, void* building)
{
    float position[3];
    const uint32_t start = Global<uint32_t>(addr::SC4_VectorDefault);
    std::memcpy(&position[0], &start, 4);
    std::memcpy(&position[1], &start, 4);
    std::memcpy(&position[2], &start, 4);
    Virtual<uint32_t>(building, 0x20, position);
    int32_t cellX, cellZ;
    Virtual<uint32_t>(Grid(self), 0x4C, position[0], position[2], &cellX, &cellZ);
    return Virtual<uint32_t>(self, 0x20, cellX, cellZ);
}

// 10 (0x006DAD80).
uint32_t __fastcall GetWaterProduction(void* self, void*, void* building)
{
    char* const node = FindProducer(self, building);
    return node ? At<uint32_t>(node, producer::kProduction) : 0;
}

// 11 (0x006DA460): bit 0 of the grid's cell (grid slot 7).
uint8_t __fastcall IsCellWatered(void* self, void*, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(Grid(self), 0x1C, x, z) & 1;
}

// 12 (0x006DC710).
uint32_t __fastcall GetNumWaterSources(void* self, void*)
{
    void* const sentinel = At<void*>(self, field::kProducers);
    uint32_t count = 0;
    for (void* node = *static_cast<void**>(sentinel); node != sentinel; node = *static_cast<void**>(node))
        ++count;
    return count;
}

// 13-21, 23: one field each.
template <std::size_t Offset>
uint32_t __fastcall FieldGetter(void* self, void*)
{
    return At<uint32_t>(self, Offset);
}

// 22 (0x006DC6C0).
uint32_t __fastcall GetNumUnwateredBuildings(void* self, void*)
{
    return At<uint32_t>(self, field::kBuildings) - At<uint32_t>(self, field::kWateredBuildings);
}

// 24 (0x006DADB0): the producer's figures, each where asked; what it uses
// from its property 0x08D9763D.
uint8_t __fastcall GetWaterProducerInfo(void* self, void*, void* building, uint32_t* production, uint32_t* capacity,
                                        uint32_t* value2C, uint8_t* flag, uint32_t* used)
{
    char* const node = FindProducer(self, building);
    if (!node)
        return 0;
    if (production)
        *production = At<uint32_t>(node, producer::kProduction);
    if (capacity)
        *capacity = At<uint32_t>(node, producer::kCapacity);
    if (value2C)
        *value2C = At<uint32_t>(node, producer::kValue2C);
    if (flag)
        *flag = At<uint8_t>(node, producer::kFlag30);
    if (used)
        GetPropertyUint32(Virtual<void*>(building, 0x18), 0x08D9763D, used);
    return 1;
}

// 25 (0x006DBFC0): the condition curve (property 0x8A1E07EE) at the
// building's age (0xC910B4CF).
uint8_t __fastcall GetWaterProducerConditionPercent(void*, void*, void* building, float* condition)
{
    float age;
    if (!GetPropertyFloat(Virtual<void*>(building, 0x18), 0xC910B4CF, &age))
        return 0;
    void* const property = Virtual<void*>(Virtual<void*>(building, 0x18), 0x24, 0x8A1E07EEu);
    if (!property)
        return 0;
    Curve curve;
    LoadCurveFromProperty(&curve, property);
    *condition = static_cast<float>(EvaluateCurve(&curve, age));
    if (curve.points)
        Free(curve.points);
    return 1;
}

// 26 (0x006DA640): the building's water budget item's cost.
uint8_t __fastcall GetOccupantMonthlyUpkeepCost(void*, void*, void* building, int64_t* cost)
{
    void* const holder = Virtual<void*>(building, 0x18);
    BudgetItem item;
    item.localFundingPercentage = 0.0f;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_BudgetSimulator), 0xCC, holder, kPurposeWater, &item))
        return 0;
    *cost = item.cost;
    return 1;
}

// 27 (0x006DAB60): the producer with the lowest local funding (below 2.0,
// with a line item in the department) and the one furthest through its
// life, as the power simulator's FindPowerPlants.
void __fastcall FindWaterBuildings(void* self, void*, void** lowestFunded, uint32_t* funding, void** oldest,
                                   int32_t* agePercent)
{
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    void* const sentinel = At<void*>(self, field::kProducers);
    void* funded = nullptr;
    void* aged = nullptr;
    float highestAge = 0.0f;
    float lowestFunding = 2.0f;
    for (void* node = *static_cast<void**>(sentinel); node != sentinel; node = *static_cast<void**>(node)) {
        void* const building = At<void*>(node, producer::kBuilding);
        void* const holder = Virtual<void*>(building, 0x18);
        BudgetItem item;
        item.localFundingPercentage = 0.0f;
        if (Virtual<uint8_t>(budget, 0xCC, holder, kPurposeWater, &item) &&
            Virtual<uint8_t>(At<void*>(self, field::kDepartment), 0x70, holder, kPurposeWater) &&
            item.localFundingPercentage < lowestFunding) {
            if (building != funded) {
                Virtual<uint32_t>(building, 0x4);
                if (funded)
                    Virtual<uint32_t>(funded, 0x8);
                funded = building;
            }
            lowestFunding = item.localFundingPercentage;
        }
        float age = 0.0f;
        float end2 = 99.0f;
        float end4 = 99.0f;
        GetPropertyFloat(holder, 0xC910B4CF, &age);
        GetPropertyFloat(holder, 0xEA1CF221, &end2);
        GetPropertyFloat(holder, 0xEA1CF222, &end4);
        const float divisor = end4 < end2 ? end2 : end4;
        const float ratio = static_cast<float>(static_cast<double>(age) / divisor);
        if (highestAge < ratio) {
            if (building != aged) {
                Virtual<uint32_t>(building, 0x4);
                if (aged)
                    Virtual<uint32_t>(aged, 0x8);
                aged = building;
            }
            highestAge = ratio;
        }
    }
    if (aged && oldest) {
        *oldest = aged;
        Virtual<uint32_t>(aged, 0x4);
        *agePercent = Ftol(static_cast<double>(highestAge) * 100.0f);
    }
    if (funded && lowestFunded) {
        *lowestFunded = funded;
        Virtual<uint32_t>(funded, 0x4);
        std::memcpy(funding, &lowestFunding, sizeof *funding);
    }
    if (aged)
        Virtual<uint32_t>(aged, 0x8);
    if (funded)
        Virtual<uint32_t>(funded, 0x8);
}

// 28 (0x006DC7D0): each producer's building, referenced, onto the list.
void __fastcall GetWaterProducers(void* self, void*, void* list)
{
    void* const sentinel = At<void*>(self, field::kProducers);
    for (void* node = *static_cast<void**>(sentinel); node != sentinel; node = *static_cast<void**>(node)) {
        void* const building = At<void*>(node, producer::kBuilding);
        Virtual<uint32_t>(building, 0x4); // AddRef
        void** const end = *static_cast<void***>(list);
        auto** const added = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
        added[2] = building;
        void** const before = static_cast<void**>(end[1]);
        added[0] = end;
        added[1] = before;
        before[0] = added;
        end[1] = added;
    }
}

// --- The simulation step (0x006DD380) -----------------------------------------
//
// One state per call (+0xC0) while the network is awake (+0x14): 0 starts a
// month over, 1 updates the producers (0x006DC050), 2 copies the pipe bits
// into the grid being built, 3 floods the pipe network from the next
// consumer short of water or producer not reached yet, 4 hands out what it
// found, 5 tells pipes and lots, 6 and 7 are passes over the city cut short
// when the time budget (the message's amount) runs out, 8 books the costs
// and publishes the month, 9 waits for a change (+0xC4).

namespace step {
constexpr std::size_t kState = 0xC0;
constexpr std::size_t kDirty = 0xC4;          // bool
constexpr std::size_t kCostsChanged = 0xC5;   // bool
constexpr std::size_t kCellGrid = 0x30;       // the grid being built; +0x34 is the published one
constexpr std::size_t kFloodQueue = 0x38;     // deque of (x, z) bytes
constexpr std::size_t kSpreadQueue = 0x60;
constexpr std::size_t kCells = 0x88;          // bytes, stride +0x8C, height +0x90
constexpr std::size_t kStride = 0x8C;
constexpr std::size_t kHeight = 0x90;
constexpr std::size_t kVisited = 0x94;        // bit array, width +0x98
constexpr std::size_t kVisitedWidth = 0x98;
constexpr std::size_t kMonths = 0x9C;
constexpr std::size_t kSpreadRounds = 0xA8;
constexpr std::size_t kSupply = 0xC8;         // int64
constexpr std::size_t kSpread = 0xD0;         // int64
constexpr std::size_t kShortfall = 0xD8;      // int64
constexpr std::size_t kRoundLeft = 0xE0;
constexpr std::size_t kRound = 0xE4;
constexpr std::size_t kSpreadRate = 0xE8;     // float
constexpr std::size_t kProducer = 0x140;      // the producer node payload being flooded from
constexpr std::size_t kCellIterator = 0x144;
constexpr std::size_t kPipes = 0x170;         // list
} // namespace step

// The game's deque of (x, z) byte pairs: 64 to a block of 0x80 bytes.
struct CellDeque {
    uint16_t* cur;
    uint16_t* first;
    uint16_t* last;
    uint16_t** node;
    uint16_t* finishCur;
    uint16_t* finishFirst;
    uint16_t* finishLast;
    uint16_t** finishNode;
};

int32_t DequeSize(const CellDeque& deque)
{
    return static_cast<int32_t>(((reinterpret_cast<intptr_t>(deque.finishNode) - reinterpret_cast<intptr_t>(deque.node)) >> 2) << 6) +
           static_cast<int32_t>((reinterpret_cast<intptr_t>(deque.finishCur) - reinterpret_cast<intptr_t>(deque.finishFirst)) >> 1) +
           static_cast<int32_t>((reinterpret_cast<intptr_t>(deque.last) - reinterpret_cast<intptr_t>(deque.cur)) >> 1) - 0x40;
}

using DequeFn = void(__thiscall*)(CellDeque* deque, const uint16_t* value);
using DequeClearFn = void(__thiscall*)(CellDeque* deque);

// push_back as the game inlines it: in place unless the block is full.
void PushInline(CellDeque& deque, uint16_t value)
{
    if (deque.finishCur == deque.finishLast - 1) {
        reinterpret_cast<DequeFn>(addr::SC4CellDeque_PushBackSlow)(&deque, &value);
        return;
    }
    if (deque.finishCur)
        *deque.finishCur = value;
    ++deque.finishCur;
}

void PushCall(CellDeque& deque, uint16_t value)
{
    reinterpret_cast<DequeFn>(addr::SC4CellDeque_PushBack)(&deque, &value);
}

uint16_t PopFront(CellDeque& deque)
{
    const uint16_t value = *deque.cur;
    if (deque.cur != deque.last - 1) {
        ++deque.cur;
        return value;
    }
    if (deque.first)
        Free(deque.first);
    ++deque.node;
    deque.first = *deque.node;
    deque.last = deque.first + 0x40;
    deque.cur = deque.first;
    return value;
}

uint16_t Cell(uint32_t x, uint32_t z)
{
    return static_cast<uint16_t>((x & 0xFF) | ((z & 0xFF) << 8));
}

using BitFn = uint32_t(__cdecl*)(const uint32_t* bits, uint32_t width, int32_t x, int32_t z);
using SetBitFn = void(__cdecl*)(uint32_t* bits, uint32_t width, int32_t x, int32_t z);

// The visited bit of a cell, set if it was not: true when it was new.
bool Visit(void* self, int32_t x, int32_t z)
{
    uint32_t* const bits = At<uint32_t*>(self, step::kVisited);
    const uint32_t width = At<uint32_t>(self, step::kVisitedWidth);
    if (static_cast<uint8_t>(reinterpret_cast<BitFn>(addr::BitGrid_Get)(bits, width, x, z)))
        return false;
    reinterpret_cast<SetBitFn>(addr::BitGrid_Set)(At<uint32_t*>(self, step::kVisited),
                                                   At<uint32_t>(self, step::kVisitedWidth), x, z);
    return true;
}

uint8_t CellFlags(void* self, uint32_t x, uint32_t z)
{
    return At<uint8_t*>(self, step::kCells)[At<uint32_t>(self, step::kStride) * z + x];
}

CellDeque& Queue(void* self, std::size_t offset)
{
    return At<CellDeque>(self, offset);
}

// An empty list whose sentinel comes from the allocator cached at `global`.
void* NewList(std::uintptr_t global)
{
    auto** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(global), 0xC, 12u));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    return sentinel;
}

template <typename Fn>
void ForEachNode(void* list, Fn fn)
{
    for (void** node = static_cast<void**>(*static_cast<void**>(list)); node != list;
         node = static_cast<void**>(node[0]))
        fn(node);
}

using ListFn = void(__thiscall*)(void** list);
const auto ListClear = reinterpret_cast<ListFn>(addr::SC4List_Clear);
const auto ListClearReleasing = reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing);
const auto ListDestroy = reinterpret_cast<ListFn>(addr::SC4List_Destroy);

void FreeList(void*& list, std::uintptr_t allocator)
{
    Virtual<uint32_t>(NodeAllocatorAt(allocator), 0x10, list);
}

// The cell iterator at +0x144 (rectangle walk) back at its start.
void ResetIterator(void* self)
{
    auto* const iterator = At<char*>(self, step::kCellIterator);
    At<uint32_t>(iterator, 0x4) = 0;
    At<uint32_t>(iterator, 0x8) = 0;
    iterator[0] = 0;
    if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
        At<int32_t>(iterator, 0x1C) < 0)
        iterator[0] = 2;
}

using NextCellFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
bool NextCell(void* self, int32_t* x, int32_t* z)
{
    return reinterpret_cast<NextCellFn>(addr::SC4CellIterator_Next)(At<void*>(self, step::kCellIterator), x, z) != 0;
}

// The time budget of a pass (0x0068BF50 and the like; rdtsc based, made
// deterministic by determinism.fixed_ticks), checked every 0x400 steps.
struct Deadline {
    uint32_t start[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
    uint32_t spare[2];
};

bool CheckDeadline(Deadline& deadline)
{
    using CheckFn = uint8_t(__thiscall*)(Deadline* deadline);
    deadline.count += deadline.step;
    if (deadline.count & 0x3FF)
        return false;
    return reinterpret_cast<CheckFn>(addr::SC4Deadline_Expired)(&deadline) != 0;
}

void EndDeadline(Deadline& deadline)
{
    using EndFn = void(__thiscall*)(Deadline* deadline);
    reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
}

void StartMonth(void* self)
{
    ++At<int32_t>(self, step::kMonths);
    At<float>(self, step::kSpreadRate) =
        static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Service43D90), 0x64, 0xA8F4EB0Cu, 0u));
    void* const occupants = At<void*>(self, 0x28);
    void* consumers = NewList(addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(occupants, 0x78, 0x4773FFE2u, &consumers);
    ForEachNode(consumers, [](void** node) { Virtual<uint32_t>(node[2], 0x54, 0u); });
    void* others = NewList(addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(occupants, 0x78, 0x4773FFE3u, &others);
    ForEachNode(others, [](void** node) { Virtual<uint32_t>(node[2], 0x54, 0u); });
    void* lots = NewList(addr::SC4List_NodeAllocator3);
    Virtual<uint32_t>(occupants, 0x44, 5u, &lots);
    ForEachNode(lots, [](void** node) { Virtual<uint32_t>(node[2], 0x30, 0u); });
    ListClearReleasing(&lots);
    FreeList(lots, addr::SC4List_NodeAllocator3);
    ListClear(&others);
    FreeList(others, addr::SC4List_NodeAllocator);
    ListClear(&consumers);
    FreeList(consumers, addr::SC4List_NodeAllocator);
    for (std::size_t offset = 0xF0; offset <= 0x10C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x190) = 0;
    At<uint32_t>(self, 0x194) = 0;
    At<int32_t>(self, step::kState) = 1;
}

// State 2: bit 3 of each cell's grid value from bit 4 of its flags.
void CopyPipeBits(void* self)
{
    using RestartFn = void(__thiscall*)(void* iterator, uint32_t a, uint32_t b);
    uint8_t* const* const rows = Rows(At<void*>(self, step::kCellGrid));
    reinterpret_cast<RestartFn>(addr::SC4CellIterator_Restart)(At<void*>(self, step::kCellIterator), 0, 0);
    int32_t x, z;
    while (NextCell(self, &x, &z))
        rows[3 * x][z] = static_cast<uint8_t>((CellFlags(self, x, z) >> 1) & 8);
    ForEachNode(At<void*>(self, field::kProducers), [](void** node) {
        At<uint8_t>(node, 0x20) = 0;
        At<uint8_t>(node, 0x21) = 0;
    });
    At<void*>(self, step::kProducer) = nullptr;
    At<int32_t>(self, step::kState) = 3;
}

using CellOfFn = void(__thiscall*)(void* self, void* object, int32_t* x, int32_t* z);

// State 3: the next source to flood from (a consumer of type 0x4773FFE2
// still short, at its first unserved lot, or a producer not reached), then
// the flood over pipe cells (flag 0x10) along the connections in flags 0-3.
void Flood(void* self)
{
    At<int64_t>(self, step::kShortfall) = 0;
    if (!At<void*>(self, step::kProducer)) {
        void* const occupants = At<void*>(self, 0x28);
        void* consumers = NewList(addr::SC4List_NodeAllocator);
        Virtual<uint32_t>(occupants, 0x78, 0x4773FFE2u, &consumers);
        void* consumer = nullptr;
        void* lot = nullptr;
        for (void** node = static_cast<void**>(*static_cast<void**>(consumers)); node != consumers;
             node = static_cast<void**>(node[0])) {
            void* const candidate = node[2];
            void* const key = Virtual<void*>(candidate, 0x1C);
            const int32_t served = Virtual<int32_t>(candidate, 0x50);
            if (served >= Virtual<int32_t>(candidate, 0x28))
                continue;
            void* const found = Virtual<void*>(occupants, 0x3C, 5u, key);
            if (!found)
                continue;
            Virtual<uint32_t>(found, 0x30, 1u);
            void* const site = Virtual<void*>(At<void*>(self, 0x28), 0x34, found);
            if (site && Virtual<uint8_t>(site, 0x34)) {
                consumer = candidate;
                lot = found;
                break;
            }
        }
        int32_t x = 0, z = 0;
        int32_t amount = 0;
        if (consumer) {
            reinterpret_cast<CellOfFn>(addr::cSC4PlumbingSimulator_ObjectCell)(self, lot, &x, &z);
            const int32_t served = Virtual<int32_t>(consumer, 0x50);
            amount = Virtual<int32_t>(consumer, 0x28) - served;
            At<int64_t>(self, step::kShortfall) = amount;
        } else {
            char* producer = nullptr;
            ForEachNode(At<void*>(self, field::kProducers), [&producer](void** node) {
                if (!producer && !At<uint8_t>(node, 0x20))
                    producer = reinterpret_cast<char*>(node) + 8;
            });
            if (!producer) {
                ResetIterator(self);
                At<int32_t>(self, step::kState) = 5;
                ListDestroy(&consumers);
                return;
            }
            At<char*>(self, step::kProducer) = producer;
            producer[0x18] = 1;
            x = At<int32_t>(producer, 0x0);
            z = At<int32_t>(producer, 0x4);
            amount = At<int32_t>(producer, 0x20);
        }
        reinterpret_cast<DequeClearFn>(addr::SC4CellDeque_Clear)(&Queue(self, step::kFloodQueue));
        reinterpret_cast<DequeClearFn>(addr::SC4CellDeque_Clear)(&Queue(self, step::kSpreadQueue));
        using ClearBitsFn = void(__cdecl*)(uint32_t* bits, uint32_t width);
        reinterpret_cast<ClearBitsFn>(addr::BitGrid_Clear)(At<uint32_t*>(self, step::kVisited),
                                                          At<uint32_t>(self, step::kVisitedWidth));
        const uint16_t seed = Cell(x, z);
        PushInline(Queue(self, step::kFloodQueue), seed);
        PushInline(Queue(self, step::kSpreadQueue), seed);
        reinterpret_cast<SetBitFn>(addr::BitGrid_Set)(At<uint32_t*>(self, step::kVisited),
                                                       At<uint32_t>(self, step::kVisitedWidth), seed & 0xFF, seed >> 8);
        At<int64_t>(self, step::kSupply) = amount;
        At<int64_t>(self, step::kSpread) = 0;
        ListDestroy(&consumers);
    }
    CellDeque& flood = Queue(self, step::kFloodQueue);
    while (flood.finishCur != flood.cur) {
        const uint16_t cell = PopFront(flood);
        const uint32_t x = cell & 0xFF;
        const uint32_t z = cell >> 8;
        const uint8_t flags = CellFlags(self, x, z);
        if (flags & 0x20) {
            // The first producer at the cell, if not reached yet.
            char* producer = nullptr;
            ForEachNode(At<void*>(self, field::kProducers), [&producer, x, z](void** node) {
                if (!producer && At<uint32_t>(node, 0x8) == x && At<uint32_t>(node, 0xC) == z)
                    producer = reinterpret_cast<char*>(node) + 8;
            });
            if (producer && !producer[0x18]) {
                if (const uint32_t output = At<uint32_t>(producer, 0x20))
                    At<int64_t>(self, step::kSupply) += output;
                producer[0x18] = 1;
            }
        }
        const auto reach = [self](uint32_t nx, uint32_t nz) {
            if (!(CellFlags(self, nx, nz) & 0x10) || !Visit(self, nx, nz))
                return;
            PushCall(Queue(self, step::kFloodQueue), Cell(nx, nz));
            PushCall(Queue(self, step::kSpreadQueue), Cell(nx, nz));
        };
        if ((flags & 1) && x > 0)
            reach(x - 1, z);
        if ((flags & 4) && x < At<uint32_t>(self, step::kStride) - 1)
            reach(x + 1, z);
        if ((flags & 2) && z > 0)
            reach(x, z - 1);
        if ((flags & 8) && z < At<uint32_t>(self, step::kHeight) - 1)
            reach(x, z + 1);
    }
    At<int32_t>(self, step::kState) = 4;
    At<int32_t>(self, step::kRoundLeft) = DequeSize(Queue(self, step::kSpreadQueue));
    At<uint32_t>(self, step::kRound) = 0;
}

using ServeFn = uint8_t(__thiscall*)(void* self, void* key, uint32_t flag);
using CellValueFn = int32_t(__thiscall*)(void* self, int32_t x, int32_t z);
using SetPropertyUint32Fn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);

// State 4: the flood's supply goes to consumers of type 0x4773FFE3, then
// spreads over the cells around the network (orthogonal neighbours, and
// diagonal ones in the first half of the rounds), each cell with flag 0x40
// taking its share, until the supply runs out or the rounds end; what is
// left serves the consumer the flood started from. Producers' used
// capacity follows.
void Distribute(void* self)
{
    const auto serve = reinterpret_cast<ServeFn>(addr::cSC4PlumbingSimulator_Serve);
    const int64_t available = At<int64_t>(self, step::kSupply) - At<int64_t>(self, step::kShortfall);
    At<int64_t>(self, 0xF0) += available;
    int32_t served = 0;
    void* const occupants = At<void*>(self, 0x28);
    void* others = NewList(addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(occupants, 0x78, 0x4773FFE3u, &others);
    ForEachNode(others, [self, serve, &served](void** node) {
        void* const consumer = node[2];
        void* const key = Virtual<void*>(consumer, 0x1C);
        const int32_t have = Virtual<int32_t>(consumer, 0x50);
        const int64_t wanted = Virtual<int32_t>(consumer, 0x28) - have;
        const int64_t supply = At<int64_t>(self, step::kSupply);
        const int64_t amount = wanted < supply ? wanted : supply;
        if (amount <= 0)
            return;
        if (!serve(self, key, 0)) {
            Virtual<uint32_t>(consumer, 0x94, 8u, 1u);
            return;
        }
        At<int64_t>(self, 0x108) += amount;
        At<int64_t>(self, step::kSupply) -= amount;
        Virtual<uint32_t>(consumer, 0x54, Virtual<int32_t>(consumer, 0x50) + static_cast<int32_t>(amount));
        served += static_cast<int32_t>(amount);
    });
    uint8_t* const* const rows = Rows(At<void*>(self, step::kCellGrid));
    CellDeque& spread = Queue(self, step::kSpreadQueue);
    if ((At<int64_t>(self, step::kSupply) > 0 || served > 0) && spread.finishCur != spread.cur) {
        const auto cellValue = reinterpret_cast<CellValueFn>(addr::cSC4PlumbingSimulator_CellValue);
        do {
            if (At<int32_t>(self, step::kRoundLeft) <= 0) {
                At<int32_t>(self, step::kRoundLeft) = DequeSize(spread);
                if (++At<uint32_t>(self, step::kRound) >= At<uint32_t>(self, step::kSpreadRounds))
                    break;
            }
            --At<int32_t>(self, step::kRoundLeft);
            const uint16_t cell = PopFront(spread);
            const uint32_t x = cell & 0xFF;
            const uint32_t z = cell >> 8;
            if (CellFlags(self, x, z) & 0x40) {
                const int32_t value = cellValue(self, static_cast<int32_t>(x), static_cast<int32_t>(z));
                int64_t& spent = At<int64_t>(self, step::kSpread);
                spent += Ftol(static_cast<double>(value) * At<float>(self, step::kSpreadRate));
                if (spent > At<int64_t>(self, step::kSupply)) {
                    spent = At<int64_t>(self, step::kSupply);
                    break;
                }
            }
            rows[3 * x][z] |= 1;
            const uint32_t lastX = At<uint32_t>(self, step::kStride) - 1;
            const uint32_t lastZ = At<uint32_t>(self, step::kHeight) - 1;
            const auto reachInline = [self, &spread](uint32_t nx, uint32_t nz) {
                if (Visit(self, static_cast<int32_t>(nx), static_cast<int32_t>(nz)))
                    PushInline(spread, Cell(nx, nz));
            };
            if (x > 0)
                reachInline(x - 1, z);
            if (x < lastX)
                reachInline(x + 1, z);
            if (z > 0)
                reachInline(x, z - 1);
            if (z < lastZ)
                reachInline(x, z + 1);
            if (At<uint32_t>(self, step::kRound) < (At<uint32_t>(self, step::kSpreadRounds) >> 1)) {
                const auto reachCall = [self, &spread](uint32_t nx, uint32_t nz) {
                    if (Visit(self, static_cast<int32_t>(nx), static_cast<int32_t>(nz)))
                        PushCall(spread, Cell(nx, nz));
                };
                if (x > 0 && z > 0)
                    reachCall(x - 1, z - 1);
                if (x < At<uint32_t>(self, step::kStride) - 1 && z > 0)
                    reachCall(x + 1, z - 1);
                if (x > 0 && z < At<uint32_t>(self, step::kHeight) - 1)
                    reachCall(x - 1, z + 1);
                if (x < At<uint32_t>(self, step::kStride) - 1 && z < At<uint32_t>(self, step::kHeight) - 1)
                    reachCall(x + 1, z + 1);
            }
        } while (spread.finishCur != spread.cur);
    }
    At<int64_t>(self, 0xF8) += At<int64_t>(self, step::kSpread);
    At<int64_t>(self, step::kSpread) += served;
    if (At<int64_t>(self, step::kShortfall) > 0) {
        void* consumers = NewList(addr::SC4List_NodeAllocator);
        Virtual<uint32_t>(occupants, 0x78, 0x4773FFE2u, &consumers);
        ForEachNode(consumers, [self, serve](void** node) {
            void* const consumer = node[2];
            void* const key = Virtual<void*>(consumer, 0x1C);
            const int32_t have = Virtual<int32_t>(consumer, 0x50);
            const int64_t wanted = Virtual<int32_t>(consumer, 0x28) - have;
            const int64_t left = At<int64_t>(self, step::kSpread);
            const int64_t amount = left < wanted ? left : wanted;
            if (amount <= 0 || !serve(self, key, 0))
                return;
            At<int64_t>(self, 0x100) += amount;
            At<int64_t>(self, step::kSpread) -= amount;
            Virtual<uint32_t>(consumer, 0x54, Virtual<int32_t>(consumer, 0x50) + static_cast<int32_t>(amount));
        });
        ListDestroy(&consumers);
    }
    float share = 0.0f;
    if (available > 0)
        share = static_cast<float>(static_cast<double>(At<int64_t>(self, step::kSpread)) / static_cast<double>(available));
    ForEachNode(At<void*>(self, field::kProducers), [share](void** node) {
        if (!At<uint8_t>(node, 0x20) || At<uint8_t>(node, 0x21))
            return;
        const double output = static_cast<double>(At<uint32_t>(node, 0x28));
        const auto used = static_cast<uint32_t>(Ftol(output * share));
        reinterpret_cast<SetPropertyUint32Fn>(addr::SC4_SetPropertyUint32)(Virtual<void*>(At<void*>(node, 0x34), 0x18),
                                                                         0x08D9763D, used);
        At<uint8_t>(node, 0x21) = 1;
    });
    reinterpret_cast<DequeClearFn>(addr::SC4CellDeque_Clear)(&spread);
    At<void*>(self, step::kProducer) = nullptr;
    At<int32_t>(self, step::kState) = 3;
    ListClear(&others);
    FreeList(others, addr::SC4List_NodeAllocator);
}

// State 5: each pipe (list +0x170, its slot 59 cell) and each lot of type 5
// told whether its cell is watered in the grid being built.
void NotifyPipesAndLots(void* self)
{
    using PipeFn = void(__thiscall*)(void* pipe, uint32_t watered);
    using LotListFn = void*(__thiscall*)(void** list);
    At<uint32_t>(self, 0x138) = 0;
    At<uint32_t>(self, 0x13C) = 0;
    ForEachNode(At<void*>(self, step::kPipes), [self](void** node) {
        void* const pipe = node[2];
        int32_t x, z;
        Virtual<uint32_t>(pipe, 0xEC, &x, &z);
        const uint8_t watered = Virtual<uint8_t>(At<void*>(self, step::kCellGrid), 0x1C, x, z) & 1;
        reinterpret_cast<PipeFn>(addr::SC4Pipe_SetWatered)(pipe, watered);
        ++At<uint32_t>(self, 0x138);
    });
    void* lots = nullptr;
    reinterpret_cast<LotListFn>(addr::SC4List_Construct2)(&lots);
    Virtual<uint32_t>(At<void*>(self, 0x28), 0x44, 5u, &lots);
    ForEachNode(lots, [self](void** node) {
        void* const lot = node[2];
        int32_t x, z;
        reinterpret_cast<CellOfFn>(addr::cSC4PlumbingSimulator_ObjectCell)(self, lot, &x, &z);
        const uint32_t watered = Virtual<uint32_t>(At<void*>(self, step::kCellGrid), 0x1C, x, z) & 0xFFFFFF01u;
        Virtual<uint32_t>(lot, 0x38, watered);
    });
    At<int32_t>(self, step::kState) = 6;
    reinterpret_cast<ListFn>(addr::SC4List_Destroy3)(&lots);
}

// State 6: each pipe or producer cell (flags 0x60) tells the buildings on
// it (lot manager slot 41) whether it is watered; counts the producer
// cells and the watered ones. Cut short when the time budget runs out;
// done, the grids swap.
uint8_t PassBuildings(void* self, uint32_t budget)
{
    using StartFn = void(__thiscall*)(Deadline* deadline, uint32_t budget);
    using NotifyFn = void(__thiscall*)(void* self, void* grid, void* building, uint32_t flags);
    uint32_t& once = Global<uint32_t>(addr::PlumbingSimulator_PassOnce);
    if (!(once & 1)) {
        once |= 1;
        Global<uint32_t>(addr::PlumbingSimulator_PassStep6) = 0x400;
        Global<uint32_t>(addr::PlumbingSimulator_PassStep6 + 4) = 0;
    }
    Deadline deadline{};
    reinterpret_cast<StartFn>(addr::SC4Deadline_Start)(&deadline, budget);
    deadline.count = 0;
    deadline.step = Global<uint32_t>(addr::PlumbingSimulator_PassStep6);
    deadline.timedOut = 0;
    deadline.stepGlobal = &Global<uint32_t>(addr::PlumbingSimulator_PassStep6);
    uint8_t* const* const rows = Rows(At<void*>(self, step::kCellGrid));
    int32_t x, z;
    while (NextCell(self, &x, &z)) {
        const uint8_t flags = CellFlags(self, x, z);
        if (flags & 0x60) {
            uint8_t state = static_cast<uint8_t>((rows[3 * x][z] & 1) | 2);
            if (flags & 0x40) {
                ++At<uint32_t>(self, 0x190);
                if (state & 1)
                    ++At<uint32_t>(self, 0x194);
            } else {
                state |= 4;
            }
            void* buildings = NewList(addr::SC4List_NodeAllocator2);
            int32_t cell[2] = {x, z};
            Virtual<uint32_t>(At<void*>(self, 0x20), 0xA4, &buildings, cell, cell, 0x278128A0u, 0xFFFFFFFFu);
            ForEachNode(buildings, [self, state](void** node) {
                void* const building = node[2];
                reinterpret_cast<NotifyFn>(addr::cSC4PlumbingSimulator_NotifyBuilding)(
                    self, At<void*>(self, step::kCellGrid), building, state);
                Virtual<uint32_t>(building, 0x8);
            });
            reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&buildings);
            reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&buildings);
            FreeList(buildings, addr::SC4List_NodeAllocator2);
        }
        if (CheckDeadline(deadline)) {
            deadline.timedOut = 1;
            EndDeadline(deadline);
            return 0;
        }
    }
    ResetIterator(self);
    std::swap(At<void*>(self, step::kCellGrid), At<void*>(self, field::kSupplyGrid));
    At<int32_t>(self, step::kState) = 7;
    EndDeadline(deadline);
    return 1;
}

// State 7: the rectangle where the new grid differs from the old one goes
// to the city's view (city slot 84, its slot 69). Cut short like state 6.
uint8_t PassChanges(void* self, uint32_t budget)
{
    using StartWithFn = void(__thiscall*)(Deadline* deadline, uint32_t budget, uint32_t* step);
    uint32_t& once = Global<uint32_t>(addr::PlumbingSimulator_PassOnce);
    if (!(once & 2)) {
        once |= 2;
        Global<uint32_t>(addr::PlumbingSimulator_PassStep7) = 0x400;
        Global<uint32_t>(addr::PlumbingSimulator_PassStep7 + 4) = 0;
    }
    Deadline deadline{};
    reinterpret_cast<StartWithFn>(addr::SC4Deadline_StartWith)(&deadline, budget,
                                                               &Global<uint32_t>(addr::PlumbingSimulator_PassStep7));
    int32_t low[2] = {Virtual<int32_t>(CityHelper(self), 0x174), 0};
    low[1] = Virtual<int32_t>(CityHelper(self), 0x178);
    int32_t high[2] = {0, 0};
    uint8_t* const* const built = Rows(At<void*>(self, step::kCellGrid));
    uint8_t* const* const shown = Rows(At<void*>(self, field::kSupplyGrid));
    bool done = true;
    int32_t x, z;
    while (NextCell(self, &x, &z)) {
        if (built[3 * x][z] != shown[3 * x][z]) {
            low[0] = low[0] < x ? low[0] : x;
            low[1] = low[1] < z ? low[1] : z;
            high[0] = x < high[0] ? high[0] : x;
            high[1] = z < high[1] ? high[1] : z;
        }
        if (CheckDeadline(deadline)) {
            deadline.timedOut = 1;
            done = false;
            break;
        }
        if (deadline.timedOut) {
            done = false;
            break;
        }
    }
    if (low[0] <= high[0])
        Virtual<uint32_t>(Virtual<void*>(CityHelper(self), 0x150), 0x114, 0u, 0u, low, high, 0u);
    if (done)
        At<int32_t>(self, step::kState) = 8;
    EndDeadline(deadline);
    return done ? 1 : 0;
}

// State 8: each producer's usage (used × rate 0xEA1E3C53) totalled, its
// cost (0x2A58D350 × the curve 0x0911E2E7 at its age) into its budget
// line; the month's figures published, message 0x6946DC34 and, for
// consumers of type 0x4773FFE3 still short, their slot 37; then state 9.
void BookMonth(void* self)
{
    using GetPropertySint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
    using GetPropertySint64Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int64_t* value);
    using ProducerFn = void(__thiscall*)(void* self, void* producer);
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    using PostMessageFn = void(__cdecl*)(uint32_t message, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
    const auto getSint32 = reinterpret_cast<GetPropertySint32Fn>(addr::SC4_GetPropertySint32);
    const auto getSint64 = reinterpret_cast<GetPropertySint64Fn>(addr::SC4_GetPropertySint64);
    int64_t total = 0;
    ForEachNode(At<void*>(self, field::kProducers), [&total, getSint32, getSint64](void** node) {
        int64_t cost = 0;
        float rate = 0.0f;
        int32_t used = 0;
        float age = 0.0f;
        void* const holder = Virtual<void*>(At<void*>(node, 0x34), 0x18);
        GetPropertyFloat(holder, 0xEA1E3C53, &rate);
        getSint32(holder, 0x08D9763D, &used);
        GetPropertyFloat(holder, 0xC910B4CF, &age);
        getSint64(holder, 0x2A58D350, &cost);
        const auto usage = static_cast<int64_t>(static_cast<double>(used) * rate);
        if (void* const property = Virtual<void*>(holder, 0x24, 0x0911E2E7u)) {
            Curve curve;
            LoadCurveFromProperty(&curve, property);
            cost = static_cast<int64_t>(static_cast<double>(cost) * EvaluateCurve(&curve, age));
            void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
            BudgetItem item;
            item.localFundingPercentage = 0.0f;
            if (Virtual<uint8_t>(budget, 0xCC, holder, kPurposeWater, &item) && cost != item.cost) {
                if (void* const line = Virtual<void*>(budget, 0xE4, &item)) {
                    Virtual<uint32_t>(line, 0x34, cost - item.cost);
                    Virtual<uint32_t>(budget, 0xD4, holder, item.index, cost);
                }
            }
            if (curve.points)
                Free(curve.points);
        }
        total += usage;
    });
    At<uint32_t>(self, 0x188) = At<uint32_t>(self, 0x190);
    At<uint32_t>(self, 0x18C) = At<uint32_t>(self, 0x194);
    At<int64_t>(self, 0x148) = total;
    if (At<uint8_t>(self, step::kCostsChanged)) {
        At<uint8_t>(self, step::kCostsChanged) = 0;
        ForEachNode(At<void*>(self, field::kProducers), [self](void** node) {
            reinterpret_cast<ProducerFn>(addr::cSC4PlumbingSimulator_UpdateProducer)(self,
                                                                                   reinterpret_cast<char*>(node) + 8);
        });
    }
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (message)
        Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, 0x6946DC34u);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Virtual<uint32_t>(At<void*>(self, 0x1C), 0x10, message, 0u);
    for (const std::size_t offset : {0x110, 0x118, 0x120, 0x128})
        At<int64_t>(self, offset) = At<int64_t>(self, offset - 0x20);
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    const auto figure = [figures](uint32_t id, uint32_t value) { Virtual<uint32_t>(figures, 0x30, id, value); };
    figure(0x0A14EBB4, At<uint32_t>(self, 0x110));
    figure(0xCA14EBD0, At<uint32_t>(self, 0x118));
    figure(0xCAB2F27B, At<uint32_t>(self, 0x120));
    figure(0x8AB2F280, At<uint32_t>(self, 0x128));
    figure(0xCA14EBFE, At<uint32_t>(self, 0x138));
    figure(0xCA14EC19, At<uint32_t>(self, 0x13C));
    figure(0xCA78A955, At<uint32_t>(self, 0x18C));
    figure(0x8A78AB06, At<uint32_t>(self, 0x188) - At<uint32_t>(self, 0x18C));
    void* others = NewList(addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(At<void*>(self, 0x28), 0x78, 0x4773FFE3u, &others);
    ForEachNode(others, [](void** node) {
        void* const consumer = node[2];
        const int32_t have = Virtual<int32_t>(consumer, 0x50);
        if (have < Virtual<int32_t>(consumer, 0x28))
            Virtual<uint32_t>(consumer, 0x94, 0x20u, 1u);
    });
    ListClear(&others);
    FreeList(others, addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x7C);
    At<int32_t>(self, step::kState) = 9;
    reinterpret_cast<PostMessageFn>(addr::SC4_PostMessage)(0xEA418D8F, 8, 0, 0, 0, 0);
    Virtual<uint32_t>(message, 0x8);
}

uint8_t __fastcall Simulate(void* self, void*, uint32_t budget)
{
    static bool logged = false;
    FirstCall("Simulate", logged);
    if (!At<uint8_t>(self, 0x14))
        return 1;
    int32_t& state = At<int32_t>(self, step::kState);
    if (state == 9 && At<uint8_t>(self, step::kDirty)) {
        At<uint8_t>(self, step::kDirty) = 0;
        state = 0;
    }
    switch (state) {
    case 0: StartMonth(self); return 1;
    case 1:
        reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4PlumbingSimulator_UpdateProducers)(self);
        state = 2;
        return 1;
    case 2: CopyPipeBits(self); return 1;
    case 3: Flood(self); return 1;
    case 4: Distribute(self); return 1;
    case 5: NotifyPipesAndLots(self); return PassBuildings(self, budget);
    case 6: return PassBuildings(self, budget);
    case 7: return PassChanges(self, budget);
    case 8: BookMonth(self); return 1;
    default: return 1;
    }
}

using StepFn = uint8_t(__fastcall*)(void*, void*, uint32_t);
StepFn g_originalSimulate = nullptr;

bool InstallStep(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PlumbingSimulator_Simulate);
    return game::IsBuild641() && entry && hook::Install(*entry, &Simulate, &g_originalSimulate);
}

const bool g_registeredStep = registry::Register({
    "simulators.water.step",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_Simulate,
    "Our water simulation step: the month's flood and distribution in ten states (docs/water.md)",
    false,
    InstallStep,
});

// --- Helpers of the step --------------------------------------------------------

// 0x006DA480: an object's cell (its slot 6), clamped to the city.
void __fastcall ObjectCell(void* self, void*, void* object, int32_t* x, int32_t* z)
{
    static bool logged = false;
    FirstCall("ObjectCell", logged);
    if (!object)
        return;
    const int32_t lastX = Virtual<int32_t>(CityHelper(self), 0x174) - 1;
    const int32_t lastZ = Virtual<int32_t>(CityHelper(self), 0x178) - 1;
    Virtual<uint32_t>(object, 0x18, x, z);
    if (*x < 0)
        *x = 0;
    if (*z < 0)
        *z = 0;
    if (*x > lastX)
        *x = lastX;
    if (*z > lastZ)
        *z = lastZ;
}

// 0x006DCDA0: the water the buildings on a cell take (property 0xC8ED2D84,
// summed over the lot manager's buildings of type 0x278128A0 there).
int32_t __fastcall CellValue(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("CellValue", logged);
    int32_t total = 0;
    int32_t cell[2] = {x, z};
    void* buildings = NewList(addr::SC4List_NodeAllocator2);
    Virtual<uint32_t>(At<void*>(self, 0x20), 0xA4, &buildings, cell, cell, 0x278128A0u, 0xFFFFFFFFu);
    ForEachNode(buildings, [&total](void** node) {
        void* const building = node[2];
        uint32_t value;
        if (GetPropertyUint32(Virtual<void*>(building, 0x18), 0xC8ED2D84, &value))
            total += static_cast<int32_t>(value);
        Virtual<uint32_t>(building, 0x8);
    });
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&buildings);
    FreeList(buildings, addr::SC4List_NodeAllocator2);
    return total;
}

// 0x006DCEE0: the first lot of type 5 for the key (occupant service slot
// 18) whose cell the flood reached (+0x94); switched on (slot 12) if asked.
uint8_t __fastcall Serve(void* self, void*, void* key, uint8_t switchOn)
{
    static bool logged = false;
    FirstCall("Serve", logged);
    Virtual<int32_t>(CityHelper(self), 0x174);
    Virtual<int32_t>(CityHelper(self), 0x178);
    void* list = NewList(addr::SC4List_NodeAllocator3);
    Virtual<uint32_t>(At<void*>(self, 0x28), 0x48, 5u, key, &list);
    int32_t x = 0;
    auto z = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(key)); // the game keeps z in the key's slot
    bool found = false;
    void** node = static_cast<void**>(*static_cast<void**>(list));
    for (; node != list; node = static_cast<void**>(node[0])) {
        if (void* const lot = node[2]) {
            const int32_t lastX = Virtual<int32_t>(CityHelper(self), 0x174) - 1;
            const int32_t lastZ = Virtual<int32_t>(CityHelper(self), 0x178) - 1;
            Virtual<uint32_t>(lot, 0x18, &x, &z);
            if (x < 0)
                x = 0;
            if (z < 0)
                z = 0;
            if (x > lastX)
                x = lastX;
            if (z > lastZ)
                z = lastZ;
        }
        if (static_cast<uint8_t>(reinterpret_cast<BitFn>(addr::BitGrid_Get)(
                At<uint32_t*>(self, step::kVisited), At<uint32_t>(self, step::kVisitedWidth), x, z))) {
            found = true;
            break;
        }
    }
    if (found && switchOn)
        Virtual<uint32_t>(node[2], 0x30, 1u);
    ListClearReleasing(&list);
    FreeList(list, addr::SC4List_NodeAllocator3);
    return found ? 1 : 0;
}


// 0x006DC050 (state 1): each producer's output: its capacity, cut to a
// share (+0xB4 %) when its source kind 0 finds other sources around
// (0x006DB2B0), or to 0 when the search fails; times the funding curve
// (+0x1A0 at the department's funding × 100, × 0.01), or the default
// (0x00B49B44) when its pollution is over +0xB8; times its condition
// curve (0x8A1E07EE at its age). A producer whose lot is off gets 0.
void __fastcall UpdateProducers(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateProducers", logged);
    using SearchFn = int32_t(__thiscall*)(void* self, int32_t x, int32_t z, uint32_t kind, uint32_t radius);
    const auto search = reinterpret_cast<SearchFn>(addr::cSC4PlumbingSimulator_FindOtherSources);
    At<uint32_t>(self, 0xA0) = 0;
    uint32_t count = 0;
    const float* const funding = Virtual<float*>(At<void*>(self, field::kDepartment), 0x28);
    const auto x100 = static_cast<float>(static_cast<double>(*funding) * 100.0f);
    const double byFunding = EvaluateCurve(static_cast<char*>(self) + 0x1A0, x100);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    const auto scale = static_cast<float>(byFunding * 0.01f);
    At<uint32_t>(self, field::kPollutedSources) = 0;
    ForEachNode(At<void*>(self, field::kProducers), [self, &count, lots, scale, search](void** node) {
        const uint32_t capacity = At<uint32_t>(node, producer::kCapacity);
        int32_t output = static_cast<int32_t>(capacity);
        void* const building = At<void*>(node, producer::kBuilding);
        void* const holder = Virtual<void*>(building, 0x18);
        float factor = scale;
        uint32_t pollution = 0;
        Virtual<uint32_t>(At<void*>(self, 0x24), 0x20, At<int32_t>(node, 0x8), At<int32_t>(node, 0xC), &pollution);
        At<uint32_t>(self, 0xA0) += pollution;
        ++count;
        void* const lot = Virtual<void*>(lots, 0x38, building);
        if (lot && Virtual<uint8_t>(lot, 0x104) && !Virtual<uint8_t>(lot, 0x108)) {
            At<uint32_t>(node, producer::kProduction) = 0;
            At<uint32_t>(node, producer::kValue2C) = Global<uint32_t>(addr::PlumbingSimulator_DefaultPercentage);
            return;
        }
        uint8_t kind = 0;
        if (void* const property = Virtual<void*>(holder, 0x24, 0x48F23A7Eu))
            kind = Virtual<uint8_t>(Virtual<void*>(property, 0x14), 0x100);
        if (kind == 0) {
            const int32_t found = search(self, At<int32_t>(node, 0x8), At<int32_t>(node, 0xC), 0, At<uint32_t>(self, 0xAC));
            if (found == -1)
                output = 0;
            else if (found > 1)
                output = static_cast<int32_t>(At<uint32_t>(self, 0xB4) * capacity / 100);
        } else if (kind == 1) {
            if (search(self, At<int32_t>(node, 0x8), At<int32_t>(node, 0xC), 1, At<uint32_t>(self, 0xB0)) == -1)
                output = 0;
        }
        At<uint8_t>(node, producer::kFlag30) = 0;
        if (pollution > At<uint32_t>(self, 0xB8) && static_cast<int32_t>(capacity) > 0) {
            ++At<uint32_t>(self, field::kPollutedSources);
            At<uint8_t>(node, producer::kFlag30) = 1;
            std::memcpy(&factor, &Global<uint32_t>(addr::PlumbingSimulator_DefaultPercentage), 4);
        }
        if (void* const property = Virtual<void*>(holder, 0x24, 0x8A1E07EEu)) {
            Curve curve;
            LoadCurveFromProperty(&curve, property);
            float age = 0.0f;
            GetPropertyFloat(holder, 0xC910B4CF, &age);
            factor = static_cast<float>(EvaluateCurve(&curve, age) * factor);
            if (curve.points)
                Free(curve.points);
        }
        At<int32_t>(node, producer::kProduction) = Ftol(static_cast<double>(output) * factor);
        std::memcpy(&At<uint32_t>(node, producer::kValue2C), &factor, 4);
    });
    if (count > 0)
        At<uint32_t>(self, 0xA0) /= count;
}

using UpdateProducersFn = void(__fastcall*)(void*, void*);
UpdateProducersFn g_originalUpdateProducers = nullptr;

// 0x006DAE30 (state 6): a building's cells in a grid get the flags (ORed):
// its lot's rectangle (lot manager slot 6 at the cell of its bounds' low
// corner, lot slot 14), or without a lot the cells of its bounds' corners.
void __fastcall NotifyBuilding(void*, void*, void* grid, void* building, uint8_t flags)
{
    static bool logged = false;
    FirstCall("NotifyBuilding", logged);
    float low[3], high[3];
    const uint32_t start = Global<uint32_t>(addr::SC4_VectorDefault);
    for (float* v : {low, high})
        for (int i = 0; i < 3; ++i)
            std::memcpy(&v[i], &start, 4);
    uint8_t* const* const rows = Rows(grid);
    Virtual<uint32_t>(building, 0x28, low, high);
    int32_t x0, z0, x1, z1;
    Virtual<uint32_t>(grid, 0x4C, low[0], low[2], &x0, &z0);
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x0, z0, 0u)) {
        int32_t rect[4];
        Virtual<uint32_t>(lot, 0x38, rect);
        x0 = rect[0];
        z0 = rect[1];
        x1 = rect[2] + 1;
        z1 = rect[3] + 1;
    } else {
        Virtual<uint32_t>(grid, 0x4C, high[0], high[2], &x1, &z1);
    }
    for (int32_t x = x0; x < x1; ++x)
        for (int32_t z = z0; z < z1; ++z)
            rows[3 * x][z] |= flags;
}

using NotifyBuildingFn = void(__fastcall*)(void*, void*, void*, void*, uint8_t);
NotifyBuildingFn g_originalNotifyBuilding = nullptr;

// 0x006DA700 (state 8, after a month's end): a producer's wear, as the
// power simulator's AgePlant: its age (0xC910B4CF) grows by the curves at
// +0x150 (funding) and +0x160 (use: used / output) times its rate
// (0xEA1CF220); past 0xEA1CF222 its lot's condition becomes 4, past
// 0xEA1CF221 2.
void __fastcall UpdateProducer(void* self, void*, char* producer)
{
    static bool logged = false;
    FirstCall("UpdateProducer", logged);
    using SetFloatFn = void(__cdecl*)(void* holder, uint32_t property, float value);
    using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
    const int32_t output = At<int32_t>(producer, 0x20);
    float age = 0.0f, rate = 0.0f, use = 0.0f, end2 = 99.0f, end4 = 99.0f;
    int32_t used = 0;
    const float funding = *Virtual<float*>(At<void*>(self, field::kDepartment), 0x28);
    void* const building = At<void*>(producer, 0x2C);
    void* const holder = Virtual<void*>(building, 0x18);
    GetPropertyFloat(holder, 0xC910B4CF, &age);
    GetPropertyFloat(holder, 0xEA1CF220, &rate);
    reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32)(holder, 0x08D9763D, &used);
    GetPropertyFloat(holder, 0xEA1CF221, &end2);
    GetPropertyFloat(holder, 0xEA1CF222, &end4);
    if (output > 0 && used > 0)
        use = static_cast<float>(static_cast<double>(used) / output);
    const auto byFunding = static_cast<float>(EvaluateCurve(static_cast<char*>(self) + 0x150, funding));
    age = static_cast<float>(EvaluateCurve(static_cast<char*>(self) + 0x160, use) * byFunding * rate + age);
    reinterpret_cast<SetFloatFn>(addr::SC4_SetPropertyFloat)(holder, 0xC910B4CF, age);
    if (age < end4 && age < end2)
        return;
    float position[3];
    Virtual<uint32_t>(building, 0x20, position);
    int32_t x, z;
    Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &x, &z);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u);
    if (!lot)
        return;
    if (!(age < end4))
        Virtual<uint32_t>(lot, 0x90, 4u);
    else if (!(age < end2))
        Virtual<uint32_t>(lot, 0x90, 2u);
}

using UpdateProducerFn = void(__fastcall*)(void*, void*, char*);
UpdateProducerFn g_originalUpdateProducer = nullptr;

// 0x006DB2B0: water near a producer, for its source kind (0 and 1; kind 2
// needs none): a spiral walk (0x0043FE10, 0x00440850) out from its cell
// over the city's terrain map (city slot 84) within the radius, until a
// cell the map's slot 34 accepts. Returns the walk's count at that cell
// (+0x2C), or -1.
int32_t __fastcall FindOtherSources(void* self, void*, int32_t x, int32_t z, uint32_t kind, int32_t radius)
{
    static bool logged = false;
    FirstCall("FindOtherSources", logged);
    using StartFn = void(__thiscall*)(void* walk, int32_t x, int32_t z);
    using NextFn = uint8_t(__thiscall*)(void* walk, int32_t* x, int32_t* z);
    if (kind == 2)
        return 0;
    void* const map = Virtual<void*>(CityHelper(self), 0x150);
    if (!map)
        return -1;
    const int32_t width = Virtual<int32_t>(map, 0x1C);
    const int32_t height = Virtual<int32_t>(map, 0x20);
    int32_t x0 = x - radius, z0 = z - radius, x1 = x + radius, z1 = z + radius;
    if (x0 < 0)
        x0 = 0;
    if (z0 < 0)
        z0 = 0;
    if (x1 >= width)
        x1 = width - 1;
    if (z1 >= height)
        z1 = height - 1;
    uint32_t walk[12] = {};
    walk[4] = 0x80000000;
    walk[5] = 0x80000000;
    walk[6] = 0x7FFFFFFF;
    walk[7] = 0x7FFFFFFF;
    reinterpret_cast<uint8_t*>(walk)[0x20] = 1;
    walk[11] = 0;
    reinterpret_cast<StartFn>(addr::SC4SpiralWalk_Start)(walk, 0, 0);
    walk[4] = static_cast<uint32_t>(x0);
    walk[5] = static_cast<uint32_t>(z0);
    walk[6] = static_cast<uint32_t>(x1 < x0 ? x0 : x1);
    walk[7] = static_cast<uint32_t>(z1 < z0 ? z0 : z1);
    reinterpret_cast<StartFn>(addr::SC4SpiralWalk_Start)(walk, x, z);
    int32_t cx, cz;
    while (reinterpret_cast<NextFn>(addr::SC4SpiralWalk_Next)(walk, &cx, &cz)) {
        if (Virtual<uint8_t>(map, 0x88, cx, cz))
            return static_cast<int32_t>(walk[11]);
    }
    return -1;
}

using FindOtherSourcesFn = int32_t(__fastcall*)(void*, void*, int32_t, int32_t, uint32_t, int32_t);
FindOtherSourcesFn g_originalFindOtherSources = nullptr;
using ObjectCellFn = void(__fastcall*)(void*, void*, void*, int32_t*, int32_t*);
using CellValueFn2 = int32_t(__fastcall*)(void*, void*, int32_t, int32_t);
using ServeFn2 = uint8_t(__fastcall*)(void*, void*, void*, uint8_t);
ObjectCellFn g_originalObjectCell = nullptr;
CellValueFn2 g_originalCellValue = nullptr;
ServeFn2 g_originalServe = nullptr;

bool InstallHelpers(registry::Mode)
{
    const addr::Entry* objectCell = addr_check::Find(addr::cSC4PlumbingSimulator_ObjectCell);
    const addr::Entry* cellValue = addr_check::Find(addr::cSC4PlumbingSimulator_CellValue);
    const addr::Entry* serve = addr_check::Find(addr::cSC4PlumbingSimulator_Serve);
    const addr::Entry* producers = addr_check::Find(addr::cSC4PlumbingSimulator_UpdateProducers);
    const addr::Entry* notify = addr_check::Find(addr::cSC4PlumbingSimulator_NotifyBuilding);
    const addr::Entry* producer = addr_check::Find(addr::cSC4PlumbingSimulator_UpdateProducer);
    const addr::Entry* sources = addr_check::Find(addr::cSC4PlumbingSimulator_FindOtherSources);
    return game::IsBuild641() && objectCell && cellValue && serve && producers && notify && producer && sources &&
           hook::Install(*sources, &FindOtherSources, &g_originalFindOtherSources) &&
           hook::Install(*producer, &UpdateProducer, &g_originalUpdateProducer) &&
           hook::Install(*notify, &NotifyBuilding, &g_originalNotifyBuilding) &&
           hook::Install(*producers, &UpdateProducers, &g_originalUpdateProducers) &&
           hook::Install(*objectCell, &ObjectCell, &g_originalObjectCell) &&
           hook::Install(*cellValue, &CellValue, &g_originalCellValue) &&
           hook::Install(*serve, &Serve, &g_originalServe);
}

const bool g_registeredHelpers = registry::Register({
    "simulators.water.helpers",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_ObjectCell,
    "Our water step helpers: an object's cell, a cell's demand, serving a consumer (docs/water.md)",
    false,
    InstallHelpers,
});

// --- The month and buildings -----------------------------------------------------

// 0x006DF540 (message 0x66956816): the step restarts at the next change,
// costs are refreshed, and the funding (department slot 10) is applied
// (0x006DD110).
void __fastcall OnMonthPassed(void* self, void*)
{
    static bool logged = false;
    FirstCall("OnMonthPassed", logged);
    using ApplyFn = void(__thiscall*)(void* self, void* funding);
    At<uint8_t>(self, step::kDirty) = 1;
    At<uint8_t>(self, step::kCostsChanged) = 1;
    void* const funding = Virtual<void*>(At<void*>(self, field::kDepartment), 0x28);
    reinterpret_cast<ApplyFn>(addr::cSC4PlumbingSimulator_ApplyFunding)(self, funding);
}

// 0x006DC870 / 0x006DC8F0 (messages 0x99EF1142 / 0x99EF1143): buildings to
// 0x006DBBC0, network pieces (0xC772BF98, interface 0x49C05B9F, unless
// their flag 0x80000000) to 0x006DB9A0, with 1 when added, 0 when removed.
void OnOccupant(void* self, void* occupant, uint32_t added)
{
    using BuildingFn = void(__thiscall*)(void* self, void* occupant, uint32_t added);
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == 0x278128A0) {
        reinterpret_cast<BuildingFn>(addr::cSC4PlumbingSimulator_OnBuilding)(self, occupant, added);
        return;
    }
    if (type != 0xC772BF98)
        return;
    void* piece = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, 0x49C05B9Fu, &piece) && !Virtual<uint8_t>(piece, 0x20, 0x80000000u))
        reinterpret_cast<BuildingFn>(addr::cSC4PlumbingSimulator_OnPipe)(self, piece, added);
    if (piece)
        Virtual<uint32_t>(piece, 0x8);
}

void __fastcall OnOccupantInserted(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("OnOccupantInserted", logged);
    OnOccupant(self, occupant, 1);
}

void __fastcall OnOccupantRemoved(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("OnOccupantRemoved", logged);
    OnOccupant(self, occupant, 0);
}


// 0x006DD110 (from OnMonthPassed): pipe wear. The share of distressed
// pipes (+0x180) moves by the curve at +0x1B0 (funding × 100) and is
// clamped to 0..1 (0 without pipes); the distressed count (+0x178) is
// brought to that share of the pipes (+0x174) by flagging (0x10000) or
// clearing pipes of a shuffled copy of the list (+0x170); then a random
// number of distressed pipes, up to twice the distressed count times
// +0x184, burst (0x20000; the city's view object 0x4A1332E8 told).
void __fastcall ApplyFunding(void* self, void*, const float* funding)
{
    static bool logged = false;
    FirstCall("ApplyFunding", logged);
    using RangeFn = uint32_t(__thiscall*)(void* random, uint32_t low, uint32_t high);
    using FillFn = void(__thiscall*)(void* vector, uint32_t count, void* const* value);
    using ShuffleFn = void(__cdecl*)(void** begin, void** end);
    const auto x100 = static_cast<float>(static_cast<double>(*funding) * 100.0f);
    const double moved = static_cast<double>(At<float>(self, 0x180)) - EvaluateCurve(static_cast<char*>(self) + 0x1B0, x100);
    At<float>(self, 0x180) = static_cast<float>(moved);
    if (moved > 1.0)
        At<float>(self, 0x180) = 1.0f;
    else if (moved < 0.0 || At<uint32_t>(self, 0x174) == 0)
        At<uint32_t>(self, 0x180) = 0;
    const auto distressed = static_cast<uint32_t>(
        Ftol(static_cast<double>(At<uint32_t>(self, 0x174)) * At<float>(self, 0x180)));
    const auto burstLimit = static_cast<uint32_t>(
        Ftol(static_cast<double>(static_cast<int32_t>(distressed)) * At<float>(self, 0x184) * 2.0));
    uint32_t bursts = reinterpret_cast<RangeFn>(addr::SC4Random_Range)(At<void*>(self, 0x2C), 0, burstLimit);
    uint32_t& current = At<uint32_t>(self, 0x178);
    if (distressed == current && bursts == 0)
        return;
    struct {
        void** begin;
        void** end;
        void** capacity;
    } pipes = {};
    void* const none = nullptr;
    reinterpret_cast<FillFn>(addr::SC4PtrVector_Fill)(&pipes, At<uint32_t>(self, 0x174), &none);
    void** item = pipes.begin;
    ForEachNode(At<void*>(self, step::kPipes), [&item](void** node) { *item++ = node[2]; });
    reinterpret_cast<ShuffleFn>(addr::SC4_RandomShuffle)(pipes.begin, pipes.end);
    if (distressed > current) {
        for (void** pipe = pipes.begin; pipe != pipes.end && distressed > current; ++pipe) {
            if (Virtual<uint32_t>(*pipe, 0x30) & 0x10000)
                continue;
            Virtual<uint32_t>(*pipe, 0x28, 0x10000u);
            ++current;
        }
    } else if (distressed < current) {
        for (void** pipe = pipes.begin; pipe != pipes.end && distressed < current; ++pipe) {
            if (!(Virtual<uint32_t>(*pipe, 0x30) & 0x10000))
                continue;
            Virtual<uint32_t>(*pipe, 0x2C, 0x10000u);
            --current;
        }
    }
    for (void** pipe = pipes.end; bursts > 0 && pipe != pipes.begin;) {
        void* const piece = *--pipe;
        if (!(Virtual<uint32_t>(piece, 0x30) & 0x10000) || (Virtual<uint32_t>(piece, 0x30) & 0x20000))
            continue;
        if (void* const view = Virtual<void*>(Virtual<void*>(CityHelper(self), 0x120), 0x1C, 0x4A1332E8u)) {
            int32_t x, z;
            Virtual<uint32_t>(piece, 0xEC, &x, &z);
            Virtual<uint32_t>(view, 0x18, x, z);
        }
        Virtual<uint32_t>(piece, 0x28, 0x20000u);
        ++At<uint32_t>(self, 0x17C);
        --bursts;
    }
    if (pipes.begin)
        Free(pipes.begin);
}

using ApplyFundingFn = void(__fastcall*)(void*, void*, const float*);
ApplyFundingFn g_originalApplyFunding = nullptr;

// 0x006DBBC0: a building added (1) or removed (0). Its demand (0xC8ED2D84)
// marks its cell (flag 0x40) and the total (+0x130); its water budget line
// (as the power simulator's plants) counts it and its cost; producers
// (property 0x88ED3303) join or leave the list (+0xBC), their lot's cells
// marked (flag 0x20).
void __fastcall OnBuilding(void* self, void*, void* building, uint8_t added)
{
    static bool logged = false;
    FirstCall("OnBuilding", logged);
    using SetSint64Fn = void(__cdecl*)(void* holder, uint32_t property, int64_t value);
    using SetFloatFn = void(__cdecl*)(void* holder, uint32_t property, float value);
    using PushFn = void(__thiscall*)(void* list, const void* payload);
    float position[3];
    const uint32_t start = Global<uint32_t>(addr::SC4_VectorDefault);
    for (float& component : position)
        std::memcpy(&component, &start, 4);
    Virtual<uint32_t>(building, 0x20, position);
    int32_t cellX, cellZ;
    Virtual<uint32_t>(At<void*>(self, step::kCellGrid), 0x4C, position[0], position[2], &cellX, &cellZ);
    void* const holder = Virtual<void*>(building, 0x18);
    uint8_t& cell = At<uint8_t*>(self, step::kCells)[At<uint32_t>(self, step::kStride) * cellZ + cellX];
    int32_t demand = 0;
    if (GetPropertySint32Water(holder, 0xC8ED2D84, &demand)) {
        if (added) {
            cell |= 0x40;
            At<int64_t>(self, 0x130) += demand;
        } else {
            cell &= 0xBF;
            At<int64_t>(self, 0x130) -= demand;
        }
    }
    void* source = nullptr;
    if (!Virtual<uint8_t>(building, 0x0, 0x87DDA3A9u, &source))
        return;
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    BudgetItem item;
    item.localFundingPercentage = 0.0f;
    if (Virtual<uint8_t>(budget, 0xCC, holder, kPurposeWater, &item)) {
        void* const department = Virtual<void*>(budget, 0x70, item.departmentId);
        bool haveLine = item.lineItem != 0;
        if (!haveLine) {
            item.lineItem = Virtual<uint32_t>(source, 0x10);
            Virtual<uint32_t>(budget, 0xD0, holder, item.index, item.lineItem);
            haveLine = item.lineItem != 0;
        }
        if (haveLine) {
            void* line = Virtual<void*>(department, 0x50, item.lineItem);
            if (!line) {
                line = Virtual<void*>(department, 0x48, item.lineItem, 0u);
                Virtual<uint32_t>(line, 0x48, 2u, 1u);
            }
            Virtual<uint32_t>(line, 0x34, added ? item.cost : -item.cost);
            Virtual<uint32_t>(line, 0x24, Virtual<int64_t>(line, 0x20) + (added ? 1 : -1));
            reinterpret_cast<SetSint64Fn>(addr::SC4_SetPropertySint64)(holder, 0x2A58D350, item.cost);
        }
    }
    if (void* const property = Virtual<void*>(holder, 0x24, 0x88ED3303u)) {
        if (added) {
            struct {
                int32_t x, z;
                int32_t rect[4];
                uint8_t reached, served, unused[2];
                uint32_t capacity;
                uint32_t production;
                uint32_t value2C;
                uint8_t flag30, unused2[3];
                void* building;
            } entry = {};
            entry.x = cellX;
            entry.z = cellZ;
            entry.value2C = 0;
            entry.building = building;
            Virtual<uint32_t>(building, 0x4);
            void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, 0u);
            if (!lot)
                return; // as the game: the references stay
            int32_t rect[4];
            Virtual<uint32_t>(lot, 0x38, rect);
            std::memcpy(entry.rect, rect, sizeof rect);
            entry.capacity = Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x120);
            entry.production = 0;
            entry.flag30 = 0;
            entry.value2C = Global<uint32_t>(addr::PlumbingSimulator_DefaultPercentage);
            static_assert(sizeof entry == 0x30);
            reinterpret_cast<PushFn>(addr::SC4ProducerList_PushBack)(static_cast<char*>(self) + field::kProducers, &entry);
            for (int32_t x = rect[0]; x <= rect[2]; ++x)
                for (int32_t z = rect[1]; z <= rect[3]; ++z)
                    At<uint8_t*>(self, step::kCells)[At<uint32_t>(self, step::kStride) * z + x] |= 0x20;
            reinterpret_cast<SetFloatFn>(addr::SC4_SetPropertyFloat)(holder, 0xC910B4CF, 0.0f);
        } else {
            void** const producers = At<void**>(self, field::kProducers);
            void** node = static_cast<void**>(producers[0]);
            while (node != producers && At<void*>(node, 0x34) != building)
                node = static_cast<void**>(node[0]);
            if (node != producers) {
                for (int32_t x = At<int32_t>(node, 0x10); x <= At<int32_t>(node, 0x18); ++x)
                    for (int32_t z = At<int32_t>(node, 0x14); z <= At<int32_t>(node, 0x1C); ++z)
                        At<uint8_t*>(self, step::kCells)[At<uint32_t>(self, step::kStride) * z + x] &= 0xDF;
                char* const current = At<char*>(self, step::kProducer);
                if (current && At<void*>(current, 0x2C) == building)
                    At<void*>(self, step::kProducer) = nullptr;
                auto** const next = static_cast<void**>(node[0]);
                auto** const previous = static_cast<void**>(node[1]);
                previous[0] = next;
                next[1] = previous;
                Free(node);
                Virtual<uint32_t>(building, 0x8);
            }
        }
        At<uint8_t>(self, step::kDirty) = 1;
    }
    Virtual<uint32_t>(source, 0x8);
}

// 0x006DB9A0: a network piece added (1) or removed (0): its cells marked as
// pipes (flag 0x10) with their connections (piece slot 47, directions 0-3
// to flags 1, 2, 4, 8), or cleared; the piece joins or leaves the list
// (+0x170, count +0x174); the department's line 2 gets the pipes' cost
// (count × +0x1C0, rounded).
void __fastcall OnPipe(void* self, void*, void* piece, uint8_t added)
{
    static bool logged = false;
    FirstCall("OnPipe", logged);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    void* const part = static_cast<char*>(piece) + 4;
    int32_t rect[4];
    Virtual<uint32_t>(part, 0x2C, rect);
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            uint8_t& cell = At<uint8_t*>(self, step::kCells)[At<uint32_t>(self, step::kStride) * z + x];
            if (!added) {
                cell &= 0xE0;
                continue;
            }
            cell |= 0x10;
            if (Virtual<uint8_t>(piece, 0xBC, 2u, 4u))
                cell |= 4;
            if (Virtual<uint8_t>(piece, 0xBC, 1u, 4u))
                cell |= 2;
            if (Virtual<uint8_t>(piece, 0xBC, 0u, 4u))
                cell |= 1;
            if (Virtual<uint8_t>(piece, 0xBC, 3u, 4u))
                cell |= 8;
        }
    }
    void** const pipes = At<void**>(self, step::kPipes);
    void** node = static_cast<void**>(pipes[0]);
    while (node != pipes && node[2] != piece)
        node = static_cast<void**>(node[0]);
    if (added) {
        if (node == pipes) {
            Virtual<uint32_t>(piece, 0x4);
            auto** const link = static_cast<void**>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0xC));
            link[2] = piece;
            void** const end = At<void**>(self, step::kPipes);
            link[0] = end;
            link[1] = end[1];
            static_cast<void**>(end[1])[0] = link;
            end[1] = link;
            ++At<uint32_t>(self, 0x174);
        }
    } else if (node != pipes) {
        if (Virtual<uint32_t>(piece, 0x30) & 0x10000)
            --At<uint32_t>(self, 0x178);
        if (Virtual<uint32_t>(piece, 0x30) & 0x20000)
            --At<uint32_t>(self, 0x17C);
        auto** const next = static_cast<void**>(node[0]);
        auto** const previous = static_cast<void**>(node[1]);
        previous[0] = next;
        next[1] = previous;
        Free(node);
        Virtual<uint32_t>(piece, 0x8);
        --At<uint32_t>(self, 0x174);
    }
    Virtual<uint32_t>(part, 0x18);
    // The magic-number rounding the game uses (adding 1.5 × 2^23).
    const auto cost = static_cast<float>(
        static_cast<double>(At<uint32_t>(self, 0x174)) * At<float>(self, 0x1C0) + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &cost, 4);
    void* const line = Virtual<void*>(At<void*>(self, field::kDepartment), 0x50, 2u);
    Virtual<uint32_t>(line, 0x30, static_cast<int64_t>(static_cast<int32_t>(bits + 0xB4C00000u)));
    At<uint8_t>(self, step::kDirty) = 1;
}

using BuildingFn = void(__fastcall*)(void*, void*, void*, uint8_t);
BuildingFn g_originalOnBuilding = nullptr;
BuildingFn g_originalOnPipe = nullptr;
using MonthFn = void(__fastcall*)(void*, void*);
MonthFn g_originalMonth = nullptr;
using OccupantFn = void(__fastcall*)(void*, void*, void*);
OccupantFn g_originalInserted = nullptr;
OccupantFn g_originalRemoved = nullptr;

bool InstallMonth(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PlumbingSimulator_OnMonthPassed);
    const addr::Entry* funding = addr_check::Find(addr::cSC4PlumbingSimulator_ApplyFunding);
    return game::IsBuild641() && entry && funding && hook::Install(*entry, &OnMonthPassed, &g_originalMonth) &&
           hook::Install(*funding, &ApplyFunding, &g_originalApplyFunding);
}

bool InstallBuildings(registry::Mode)
{
    const addr::Entry* inserted = addr_check::Find(addr::cSC4PlumbingSimulator_OnOccupantInserted);
    const addr::Entry* removed = addr_check::Find(addr::cSC4PlumbingSimulator_OnOccupantRemoved);
    const addr::Entry* building = addr_check::Find(addr::cSC4PlumbingSimulator_OnBuilding);
    const addr::Entry* pipe = addr_check::Find(addr::cSC4PlumbingSimulator_OnPipe);
    return game::IsBuild641() && inserted && removed && building && pipe &&
           hook::Install(*building, &OnBuilding, &g_originalOnBuilding) &&
           hook::Install(*pipe, &OnPipe, &g_originalOnPipe) &&
           hook::Install(*inserted, &OnOccupantInserted, &g_originalInserted) &&
           hook::Install(*removed, &OnOccupantRemoved, &g_originalRemoved);
}

const bool g_registeredMonth = registry::Register({
    "simulators.water.month",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_OnMonthPassed,
    "Our water MonthPassed handler (docs/water.md)",
    false,
    InstallMonth,
});

const bool g_registeredBuildings = registry::Register({
    "simulators.water.buildings",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_OnOccupantInserted,
    "Our water handlers for occupants added and removed (docs/water.md)",
    false,
    InstallBuildings,
});


// --- The tuning (0x006DC9E0, message 0xCA1B151C) ---------------------------------

struct PointVector {
    float* begin;
    float* end;
    float* capacity;
};

void PushPoint(PointVector& points, float x, float y)
{
    using InsertFn = void(__thiscall*)(PointVector* points, float* where, const float* value, void* allocator,
                                       uint32_t count, uint32_t atEnd);
    const float value[2] = {x, y};
    uint8_t allocator = 0;
    if (points.end == points.capacity) {
        reinterpret_cast<InsertFn>(addr::SC4PointVector_InsertN)(&points, points.end, value, &allocator, 1, 1);
        return;
    }
    if (points.end) {
        points.end[0] = value[0];
        points.end[1] = value[1];
    }
    points.end += 2;
}

// Default curves, then the simulator tuning exemplar's values and curves; a
// curve the exemplar leaves empty gets a flat 100.
void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    using SetPointsFn = uint8_t(__thiscall*)(void* curve, const PointVector* points);
    using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
    using GetPercentageFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
    const auto setPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
    const auto loadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
    char* const base = static_cast<char*>(self);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xC911E35B};
    PointVector points{};
    PushPoint(points, 0.0f, 1.0f);
    PushPoint(points, 1.0f, 1.0f);
    setPoints(base + 0x150, &points); // aging by funding
    setPoints(base + 0x160, &points); // aging by use
    points.end = points.begin;
    PushPoint(points, 0.0f, -0.02f);
    PushPoint(points, 120.0f, 0.01f);
    setPoints(base + 0x1B0, &points); // the distressed share step (ApplyFunding)
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        GetPropertyUint32(holder, 0x68F5436A, &At<uint32_t>(self, step::kSpreadRounds));
        GetPropertyUint32(holder, 0x68F5436B, &At<uint32_t>(self, 0xAC));
        GetPropertyUint32(holder, 0x68F5436C, &At<uint32_t>(self, 0xB0));
        GetPropertyUint32(holder, 0x68F5436D, &At<uint32_t>(self, 0xB4));
        GetPropertyUint32(holder, 0xAA161F81, &At<uint32_t>(self, 0xB8));
        GetPropertyFloat(holder, 0x8A235C56, &At<float>(self, 0x184));
        reinterpret_cast<GetPercentageFn>(addr::SC4_GetPropertyPercentage)(holder, 0xEA4025A9, base + 0x19C);
        GetPropertyFloat(holder, 0x8A84778F, &At<float>(self, 0x1C0));
        PointVector flat{};
        PushPoint(flat, 100.0f, 100.0f);
        const auto empty = [base](std::size_t curve) {
            return (At<char*>(base, curve + 4) - At<char*>(base, curve)) / 20 == 0;
        };
        for (const auto& [curve, property] : {std::pair<std::size_t, uint32_t>{0x1A0, 0x6A410BF2},
                                              {0x150, 0x0911E2E8},
                                              {0x160, 0x0911E2E9}}) {
            loadCurve(base + curve, key, property);
            if (empty(curve))
                setPoints(base + curve, &flat);
        }
        loadCurve(base + 0x1B0, key, 0xCA7138C4);
        Virtual<uint32_t>(exemplar, 0x8);
        if (flat.begin)
            Free(flat.begin);
    }
    if (points.begin)
        Free(points.begin);
}

using LoadTuningFn = void(__fastcall*)(void*, void*);
LoadTuningFn g_originalTuning = nullptr;

bool InstallTuning(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PlumbingSimulator_LoadTuning);
    return game::IsBuild641() && entry && hook::Install(*entry, &LoadTuning, &g_originalTuning);
}

const bool g_registeredTuning = registry::Register({
    "simulators.water.tuning",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_LoadTuning,
    "Our water tuning: default curves, then the simulator exemplar's values and curves (docs/water.md)",
    false,
    InstallTuning,
});


// --- The interface at +0x4: cIGZMessageTarget2 (vtable 0x00AB2214) ----------------

// 3 (0x006DF570) DoMessage: each message to its handler by address (the
// game's or ours as configured); the step's result is passed on.
uint8_t __fastcall DoMessage(void* interface4, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(interface4) - 0x4;
    using StepFn2 = uint8_t(__thiscall*)(void* self, uint32_t budget);
    using SelfFn = void(__thiscall*)(void* self);
    using OccupantFn2 = void(__thiscall*)(void* self, void* occupant);
    const auto step = reinterpret_cast<StepFn2>(addr::cSC4PlumbingSimulator_Simulate);
    const uint32_t type = Virtual<uint32_t>(message, 0x10); // GetType
    switch (type) {
    case 0x66956814: return step(self, 5);
    case 0x6695681A: return step(self, Virtual<uint32_t>(message, 0x4C));
    case 0x66956816: reinterpret_cast<SelfFn>(addr::cSC4PlumbingSimulator_OnMonthPassed)(self); return 1;
    case 0x6AC284F3: At<uint8_t>(self, 0x14) = 1; return 1;
    case 0xCA1B151C: reinterpret_cast<SelfFn>(addr::cSC4PlumbingSimulator_LoadTuning)(self); return 1;
    case 0x99EF1142:
    case 0x99EF1143: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, 0x029244DBu))
            return 1;
        reinterpret_cast<OccupantFn2>(type == 0x99EF1142 ? addr::cSC4PlumbingSimulator_OnOccupantInserted
                                                         : addr::cSC4PlumbingSimulator_OnOccupantRemoved)(self, occupant);
        return 1;
    }
    case 0xEA1F0DCD: {
        const uint32_t x = Virtual<uint32_t>(message, 0x24);
        const uint32_t z = Virtual<uint32_t>(message, 0x38);
        At<uint8_t*>(self, step::kCells)[At<uint32_t>(self, step::kStride) * z + x] &= 0xE0;
        At<uint8_t>(self, step::kDirty) = 1;
        [[fallthrough]];
    }
    case 0xEA418D8F: {
        if (Virtual<uint32_t>(message, 0x24) != 0)
            return 1;
        void* consumer = nullptr;
        if (void* const data = Virtual<void*>(message, 0x80)) {
            if (Virtual<uint8_t>(data, 0x0, 0x0773FEF1u, &consumer)) {
                if (Virtual<uint32_t>(consumer, 0x20) == 0x4773FFE3 || Virtual<uint32_t>(consumer, 0x20) == 0x4773FFE2) {
                    At<int32_t>(self, step::kState) = 9;
                    At<uint8_t>(self, step::kDirty) = 1;
                }
            }
        }
        if (consumer)
            Virtual<uint32_t>(consumer, 0x8);
        return 1;
    }
    default: return 1;
    }
}

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4PlumbingSimulator_vftable_4);
    if (vtable[3] != 0x006DF570) {
        log::Write("water messages: +0x4 slot 3 holds %08X, expected 006DF570; not installing",
                   static_cast<unsigned>(vtable[3]));
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
    "simulators.water.messages",
    "simulators/water",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_vftable_4,
    "Our water DoMessage: each message to its handler (docs/water.md)",
    false,
    InstallMessages,
});


// --- Init (slot 3, 0x006DF0D0) and Shutdown (slot 4, 0x006DA900) -------------------

void Reference(void* self, std::size_t offset, void* object)
{
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4); // AddRef
}

// A cSC4SimGridUint8 as Init constructs it.
void* NewGrid(uint32_t id)
{
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    auto* const grid = static_cast<uint32_t*>(reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(0x44));
    if (grid) {
        grid[1] = 0x00A80784;
        grid[0] = static_cast<uint32_t>(addr::SC4SimGridUint8_Vtable);
        grid[1] = 0x00A878A4;
        reinterpret_cast<uint8_t*>(grid)[8] = 0;
        grid[3] = 0;
        grid[4] = 0x49B9E602;
        grid[6] = 0;
        grid[14] = 0;
        grid[15] = 0;
        grid[16] = 0;
    }
    Virtual<uint32_t>(grid, 0x4);
    Virtual<uint32_t>(grid, 0xC);
    Virtual<uint32_t>(grid, 0x24, 1u);
    Virtual<uint32_t>(grid, 0x18, id);
    Virtual<uint32_t>(grid, 0x6C, 0u);
    return grid;
}

constexpr uint32_t kWaterMessages[] = {0x99EF1142, 0x99EF1143, 0xCA1B151C, 0xEA1F0DCD, 0xEA418D8F, 0x6AC284F3};

uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using RandomFn = void*(__thiscall*)(void* generator, int32_t seed);
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    using IteratorFn = void*(__thiscall*)(void* iterator);
    using ClearBitsFn = void(__cdecl*)(uint32_t* bits, uint32_t width);
    const auto operatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
    char* const base = static_cast<char*>(self);
    Global<uint8_t>(addr::PlumbingSimulator_Started) = 1;
    if (!At<void*>(self, 0x18))
        Reference(self, 0x18, Global<void*>(addr::SC4_City));
    if (!At<void*>(self, 0x2C)) {
        void* const memory = operatorNew(0x18);
        At<void*>(self, 0x2C) = memory ? reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(memory, -1) : nullptr;
    }
    if (!At<void*>(self, 0x20))
        Reference(self, 0x20, Virtual<void*>(CityHelper(self), 0x88));
    if (!At<void*>(self, 0x24))
        Reference(self, 0x24, Virtual<void*>(CityHelper(self), 0x108));
    if (!At<void*>(self, 0x28))
        Reference(self, 0x28, Virtual<void*>(CityHelper(self), 0xF8));
    void* const target = base + 0x4;
    if (!At<uint8_t>(self, 0xC)) {
        At<uint8_t>(self, 0xC) = 1;
        reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4PlumbingSimulator_LoadTuning)(self);
        void* const simulators = Global<void*>(addr::SC4_SimulatorService);
        struct {
            const void* vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t zero;
        } name;
        reinterpret_cast<StringFn>(addr::SC4String_FromText)(&name, reinterpret_cast<const char*>(addr::PlumbingSimulator_Name));
        Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 2u);
        Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
        Virtual<uint32_t>(simulators, 0x4C, target, 10u, &name, 2u);
        if (!At<void*>(self, 0x1C)) {
            Reference(self, 0x1C, Global<void*>(addr::SC4_MessageServer));
            for (const uint32_t message : kWaterMessages)
                Virtual<uint8_t>(At<void*>(self, 0x1C), 0x14, target, message);
        }
        if (!At<void*>(self, field::kDepartment)) {
            void* const created =
                Virtual<void*>(Global<void*>(addr::SC4_BudgetSimulator), 0x64, 0xAA4014B4u, 0x4A357EAFu);
            void* const old = At<void*>(self, field::kDepartment);
            if (created != old) {
                if (created)
                    Virtual<uint32_t>(created, 0x4);
                At<void*>(self, field::kDepartment) = created;
                if (old)
                    Virtual<uint32_t>(old, 0x8);
            }
            void* const department = At<void*>(self, field::kDepartment);
            Virtual<uint32_t>(department, 0x14, 0x6A3FF01Cu, 0x2A4937D3u);
            Virtual<uint32_t>(At<void*>(self, field::kDepartment), 0x34, base + 0x19C);
            Virtual<uint32_t>(Virtual<void*>(At<void*>(self, field::kDepartment), 0x44, 2u, 0u), 0x1C, 0x6A3FF01Cu,
                              0x8A75F61Eu);
        } else {
            Virtual<uint32_t>(At<void*>(self, field::kDepartment), 0x4C, 1u);
        }
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Free(name.begin);
    }
    if (At<uint8_t>(self, 0xD))
        return 1;
    At<uint8_t>(self, 0xD) = 1;
    if (!At<void*>(self, step::kCellGrid))
        At<void*>(self, step::kCellGrid) = NewGrid(0x49D5BB8C);
    if (!At<void*>(self, field::kSupplyGrid))
        At<void*>(self, field::kSupplyGrid) = NewGrid(0x49D5BBA1);
    const uint32_t cellsX = Virtual<uint32_t>(CityHelper(self), 0x174);
    At<uint32_t>(self, step::kStride) = cellsX;
    const uint32_t cellsZ = Virtual<uint32_t>(CityHelper(self), 0x178);
    At<uint32_t>(self, step::kHeight) = cellsZ;
    At<uint32_t>(self, step::kVisitedWidth) =
        static_cast<int32_t>(cellsX) < static_cast<int32_t>(cellsZ) ? cellsZ : cellsX;
    if (!At<void*>(self, step::kCells))
        At<void*>(self, step::kCells) = operatorNew(cellsX * cellsZ);
    std::memset(At<void*>(self, step::kCells), 0, At<uint32_t>(self, step::kHeight) * At<uint32_t>(self, step::kStride));
    if (!At<void*>(self, step::kVisited)) {
        const int32_t width = At<int32_t>(self, step::kVisitedWidth);
        At<void*>(self, step::kVisited) = operatorNew(static_cast<std::size_t>(((width * width) >> 5) << 2));
    }
    reinterpret_cast<ClearBitsFn>(addr::BitGrid_Clear)(At<uint32_t*>(self, step::kVisited),
                                                      At<uint32_t>(self, step::kVisitedWidth));
    if (!At<void*>(self, step::kCellIterator)) {
        void* const memory = operatorNew(0x20);
        At<void*>(self, step::kCellIterator) =
            memory ? reinterpret_cast<IteratorFn>(addr::SC4CellIterator_Construct)(memory) : nullptr;
    }
    const int32_t lastZ = Virtual<int32_t>(CityHelper(self), 0x178) - 1;
    const int32_t lastX = Virtual<int32_t>(CityHelper(self), 0x174) - 1;
    char* const iterator = At<char*>(self, step::kCellIterator);
    At<int32_t>(iterator, 0x10) = 0;
    At<int32_t>(iterator, 0x14) = 0;
    At<int32_t>(iterator, 0x18) = lastX;
    At<int32_t>(iterator, 0x1C) = lastZ;
    if (At<int32_t>(iterator, 0x18) < At<int32_t>(iterator, 0x10))
        At<int32_t>(iterator, 0x18) = At<int32_t>(iterator, 0x10);
    if (At<int32_t>(iterator, 0x1C) < At<int32_t>(iterator, 0x14))
        At<int32_t>(iterator, 0x1C) = At<int32_t>(iterator, 0x14);
    At<int32_t>(self, step::kState) = 0;
    At<uint8_t>(self, step::kDirty) = 1;
    return 1;
}

void Drop(void* self, std::size_t offset)
{
    if (void*& object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8); // Release
        object = nullptr;
    }
}

uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    using FreeFn2 = void(__cdecl*)(void* p);
    const auto operatorDelete = reinterpret_cast<FreeFn2>(addr::SC4_operator_delete);
    if (!At<uint8_t>(self, 0xC))
        return 1;
    At<uint8_t>(self, 0xC) = 0;
    if (void*& random = At<void*>(self, 0x2C)) {
        operatorDelete(random);
        random = nullptr;
    }
    for (const std::size_t offset : {0x18, 0x20, 0x24, 0x28})
        Drop(self, offset);
    Drop(self, field::kDepartment);
    void* const target = static_cast<char*>(self) + 0x4;
    if (At<void*>(self, 0x1C)) {
        for (const uint32_t message : kWaterMessages)
            Virtual<uint8_t>(At<void*>(self, 0x1C), 0x18, target, message);
        Drop(self, 0x1C);
    }
    if (void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(service, 0x50, target);
    for (const std::size_t grid : {step::kCellGrid, field::kSupplyGrid}) {
        if (At<void*>(self, grid)) {
            Virtual<uint32_t>(At<void*>(self, grid), 0x10);
            Virtual<uint32_t>(At<void*>(self, grid), 0x8);
            At<void*>(self, grid) = nullptr;
        }
    }
    for (const std::size_t buffer : {step::kCells, step::kVisited}) {
        if (void*& p = At<void*>(self, buffer)) {
            operatorDelete(p);
            p = nullptr;
        }
    }
    ForEachNode(At<void*>(self, field::kProducers), [](void** node) { Virtual<uint32_t>(At<void*>(node, 0x34), 0x8); });
    for (void** node = static_cast<void**>(At<void**>(self, field::kProducers)[0]); node != At<void*>(self, field::kProducers);) {
        void** const next = static_cast<void**>(node[0]);
        Free(node);
        node = next;
    }
    At<void**>(self, field::kProducers)[0] = At<void*>(self, field::kProducers);
    At<void**>(self, field::kProducers)[1] = At<void*>(self, field::kProducers);
    if (void*& iterator = At<void*>(self, step::kCellIterator)) {
        operatorDelete(iterator);
        iterator = nullptr;
    }
    if (At<void*>(self, step::kProducer))
        At<void*>(self, step::kProducer) = nullptr;
    ForEachNode(At<void*>(self, step::kPipes), [](void** node) {
        void* const piece = node[2];
        Virtual<uint32_t>(piece ? static_cast<char*>(piece) + 4 : nullptr, 0x8); // released through its +0x4 part
    });
    for (void** node = static_cast<void**>(At<void**>(self, step::kPipes)[0]); node != At<void*>(self, step::kPipes);) {
        void** const next = static_cast<void**>(node[0]);
        Free(node);
        node = next;
    }
    At<void**>(self, step::kPipes)[0] = At<void*>(self, step::kPipes);
    At<void**>(self, step::kPipes)[1] = At<void*>(self, step::kPipes);
    for (const std::size_t offset : {0x174, 0x178, 0x17C, 0x198})
        At<uint32_t>(self, offset) = 0;
    return 1;
}

using LifecycleFn = uint8_t(__fastcall*)(void*, void*);
LifecycleFn g_originalInit = nullptr;
LifecycleFn g_originalShutdown = nullptr;

bool InstallInit(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PlumbingSimulator_Init);
    return game::IsBuild641() && entry && hook::Install(*entry, &Init, &g_originalInit);
}

bool InstallShutdown(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PlumbingSimulator_Shutdown);
    return game::IsBuild641() && entry && hook::Install(*entry, &Shutdown, &g_originalShutdown);
}

const bool g_registeredInit = registry::Register({
    "simulators.water.init",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_Init,
    "Our water Init: references, registrations, the department, grids, cell flags and the cell iterator (docs/water.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.water.shutdown",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_Shutdown,
    "Our water Shutdown (docs/water.md)",
    false,
    InstallShutdown,
});


// --- Saving: cIGZSerializable (vtable 0x00AB21FC) at +0x8 ---------------------------
//
// Write (0x006DB090) and Read (0x006DB620): version 4. Producers go through
// 0x006DA4F0 / 0x006DA570, the cell iterator through 0x005DA480 / 0x005DA410.

constexpr std::size_t kSerializable = 0x8;

using WriteObjectFn = uint8_t(__cdecl*)(void* stream, const void* object);
using ReadObjectFn = uint8_t(__cdecl*)(void* stream, uint32_t interfaceId, void* out, uint32_t flags);
using ProducerIoFn = uint8_t(__thiscall*)(void* self, void* stream, void* payload);

uint8_t __fastcall Write(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    const auto writeReference = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);
    const auto writeProducer = reinterpret_cast<ProducerIoFn>(addr::cSC4PlumbingSimulator_WriteProducer);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    const auto u8 = [stream](uint8_t v) { Virtual<uint8_t>(stream, 0x14, v); };
    const auto u16 = [stream](uint16_t v) { Virtual<uint8_t>(stream, 0x1C, v); };
    const auto s32 = [stream](int32_t v) { Virtual<uint8_t>(stream, 0x20, v); };
    const auto u32 = [stream](uint32_t v) { Virtual<uint8_t>(stream, 0x24, v); };
    const auto s64 = [stream](int64_t v) { Virtual<uint8_t>(stream, 0x28, v); };
    u16(4);
    writeReference(stream, At<void*>(self, step::kCellGrid));
    writeReference(stream, At<void*>(self, field::kSupplyGrid));
    u32(At<uint32_t>(self, step::kStride));
    u32(At<uint32_t>(self, step::kHeight));
    for (uint32_t i = 0; i < At<uint32_t>(self, step::kStride) * At<uint32_t>(self, step::kHeight); ++i)
        u8(At<uint8_t*>(self, step::kCells)[i]);
    s32(At<int32_t>(self, step::kMonths));
    u32(At<uint32_t>(self, 0xA0));
    s64(At<int64_t>(self, 0x148));
    uint32_t count = 0;
    ForEachNode(At<void*>(self, field::kProducers), [&count](void**) { ++count; });
    u32(count);
    u32(At<uint32_t>(self, field::kPollutedSources));
    ForEachNode(At<void*>(self, field::kProducers), [self, stream, writeProducer](void** node) {
        writeProducer(self, stream, reinterpret_cast<char*>(node) + 8);
    });
    if (void* const current = At<void*>(self, step::kProducer)) {
        writeProducer(self, stream, current);
    } else {
        uint32_t none[12] = {};
        writeProducer(self, stream, none);
    }
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteCellIterator)(stream, At<void*>(self, step::kCellIterator));
    s64(At<int64_t>(self, 0x130));
    u32(At<uint32_t>(self, 0x174));
    u32(At<uint32_t>(self, 0x178));
    u32(At<uint32_t>(self, 0x17C));
    ForEachNode(At<void*>(self, step::kPipes), [stream, writeReference](void** node) {
        writeReference(stream, node[2] ? static_cast<char*>(node[2]) + 8 : nullptr);
    });
    writeReference(stream, At<void*>(self, field::kDepartment));
    for (const std::size_t offset : {0x110, 0x118, 0x120, 0x128})
        s64(At<int64_t>(self, offset));
    return 1;
}

uint8_t __fastcall Read(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    const auto readReference = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReference);
    const auto readProducer = reinterpret_cast<ProducerIoFn>(addr::cSC4PlumbingSimulator_ReadProducer);
    const auto allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
    const auto operatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    const auto in = [stream](std::size_t slot, void* to) { Virtual<uint8_t>(stream, slot, to); };
    uint16_t version = 0;
    in(0x1C, &version);
    if (version != 4)
        return 0;
    readReference(stream, 0xC9BD939A, self + step::kCellGrid, 0);
    readReference(stream, 0xC9BD939A, self + field::kSupplyGrid, 0);
    in(0x24, self + step::kStride);
    in(0x24, self + step::kHeight);
    const int32_t stride = At<int32_t>(self, step::kStride);
    const int32_t height = At<int32_t>(self, step::kHeight);
    const int32_t width = stride < height ? height : stride;
    At<int32_t>(self, step::kVisitedWidth) = width;
    if (!At<void*>(self, step::kVisited))
        At<void*>(self, step::kVisited) = operatorNew(static_cast<std::size_t>(((width * width) >> 5) * 4));
    if (!At<void*>(self, step::kCells))
        At<void*>(self, step::kCells) = operatorNew(At<uint32_t>(self, step::kStride) * At<uint32_t>(self, step::kHeight));
    for (uint32_t i = 0; i < At<uint32_t>(self, step::kStride) * At<uint32_t>(self, step::kHeight); ++i)
        in(0x14, At<uint8_t*>(self, step::kCells) + i);
    in(0x20, self + step::kMonths);
    in(0x24, self + 0xA0);
    in(0x28, self + 0x148);
    uint32_t count = 0;
    in(0x24, &count);
    in(0x24, self + field::kPollutedSources);
    for (; count; --count) {
        uint32_t payload[12];
        payload[9] = 0;                               // +0x24
        reinterpret_cast<uint8_t*>(payload)[0x28] = 0; // +0x28
        readProducer(self, stream, payload);
        void** const producers = At<void**>(self, field::kProducers);
        auto** const node = static_cast<void**>(allocate(0x38));
        std::memcpy(node + 2, payload, sizeof payload);
        node[0] = producers;
        node[1] = producers[1];
        static_cast<void**>(producers[1])[0] = node;
        producers[1] = node;
    }
    uint32_t current[12];
    current[9] = 0;
    current[11] = 0;
    readProducer(self, stream, current);
    void* const currentBuilding = reinterpret_cast<void*>(static_cast<std::uintptr_t>(current[11]));
    At<void*>(self, step::kProducer) = nullptr;
    ForEachNode(At<void*>(self, field::kProducers), [self, currentBuilding](void** node) {
        if (!At<void*>(self, step::kProducer) && At<void*>(node, 0x34) == currentBuilding)
            At<void*>(self, step::kProducer) = reinterpret_cast<char*>(node) + 8;
    });
    if (currentBuilding)
        Virtual<uint32_t>(currentBuilding, 0x8);
    auto* const iterator = static_cast<char*>(operatorNew(0x20));
    if (iterator) {
        At<uint32_t>(iterator, 0xC) = 1;
        At<uint32_t>(iterator, 0x10) = 0x80000000;
        At<uint32_t>(iterator, 0x14) = 0x80000000;
        At<uint32_t>(iterator, 0x18) = 0x7FFFFFFF;
        At<uint32_t>(iterator, 0x1C) = 0x7FFFFFFF;
        At<uint32_t>(iterator, 0x4) = 0;
        At<uint32_t>(iterator, 0x8) = 0;
        iterator[0] = 0;
        if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
            At<int32_t>(iterator, 0x1C) < 0)
            iterator[0] = 2;
    }
    At<void*>(self, step::kCellIterator) = iterator;
    reinterpret_cast<WriteObjectFn>(addr::SC4Stream_ReadCellIterator)(stream, iterator);
    in(0x28, self + 0x130);
    in(0x24, self + 0x174);
    in(0x24, self + 0x178);
    in(0x24, self + 0x17C);
    for (uint32_t pipes = At<uint32_t>(self, 0x174); pipes; --pipes) {
        void* piece = nullptr;
        readReference(stream, 0x49C05B9F, &piece, 0);
        void** const list = At<void**>(self, step::kPipes);
        auto** const node = static_cast<void**>(allocate(0xC));
        node[2] = piece;
        node[0] = list;
        node[1] = list[1];
        static_cast<void**>(list[1])[0] = node;
        list[1] = node;
    }
    readReference(stream, 0x223E2B67, self + field::kDepartment, 0);
    At<uint8_t>(self, 0xD) = 1;
    At<int32_t>(self, step::kState) = 0;
    for (const std::size_t offset : {0x110, 0x118, 0x120, 0x128})
        in(0x28, self + offset);
    return 1;
}

using SaveFn = uint8_t(__fastcall*)(void*, void*, void*);
SaveFn g_originalWrite = nullptr;
SaveFn g_originalRead = nullptr;

bool InstallSave(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PlumbingSimulator_Write);
    return game::IsBuild641() && entry && hook::Install(*entry, &Write, &g_originalWrite);
}

bool InstallLoad(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PlumbingSimulator_Read);
    return game::IsBuild641() && entry && hook::Install(*entry, &Read, &g_originalRead);
}

const bool g_registeredSave = registry::Register({
    "simulators.water.save",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_Write,
    "Our water Write: the save record, version 4 (docs/water.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.water.load",
    "simulators/water",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_Read,
    "Our water Read: the save record, version 4 (docs/water.md)",
    false,
    InstallLoad,
});

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x006DA440, GetSupplyValue),
    OPENSC4_SLOT(6, 0x006DA450, GetSupplyValueMapPtr),
    OPENSC4_SLOT(7, 0x00A6CAB0, GetWateredGrid),
    OPENSC4_SLOT(8, 0x006DAD50, AreWateredBuildingsInCell),
    OPENSC4_SLOT(9, 0x006DA6A0, IsBuildingWatered),
    OPENSC4_SLOT(10, 0x006DAD80, GetWaterProduction),
    OPENSC4_SLOT(11, 0x006DA460, IsCellWatered),
    OPENSC4_SLOT(12, 0x006DC710, GetNumWaterSources),
    OPENSC4_SLOT(13, 0x00423850, FieldGetter<field::kPollutedSources>),
    OPENSC4_SLOT(14, 0x00423700, FieldGetter<field::kCapacity>),
    OPENSC4_SLOT(15, 0x007C1910, FieldGetter<field::kConsumed>),
    OPENSC4_SLOT(16, 0x007E56F0, FieldGetter<field::kImported>),
    OPENSC4_SLOT(17, 0x006DC680, FieldGetter<field::kExported>),
    OPENSC4_SLOT(18, 0x006DC690, FieldGetter<field::kPipes>),
    OPENSC4_SLOT(19, 0x006DC6A0, FieldGetter<field::kDistressedPipes>),
    OPENSC4_SLOT(20, 0x007FF120, FieldGetter<field::kBurstPipes>),
    OPENSC4_SLOT(21, 0x006DC6B0, FieldGetter<field::kWateredBuildings>),
    OPENSC4_SLOT(22, 0x006DC6C0, GetNumUnwateredBuildings),
    OPENSC4_SLOT(23, 0x00798370, FieldGetter<field::kDemand>),
    OPENSC4_SLOT(24, 0x006DADB0, GetWaterProducerInfo),
    OPENSC4_SLOT(25, 0x006DBFC0, GetWaterProducerConditionPercent),
    OPENSC4_SLOT(26, 0x006DA640, GetOccupantMonthlyUpkeepCost),
    OPENSC4_SLOT(27, 0x006DAB60, FindWaterBuildings),
    OPENSC4_SLOT(28, 0x006DC7D0, GetWaterProducers),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 30;

// Swaps every slot or none: each must still hold the game's function.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4PlumbingSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("water interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("water interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("water interface: %u of %d cISC4PlumbingSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.water.interface",
    "simulators/water",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4PlumbingSimulator_vftable,
    "Our cISC4PlumbingSimulator getters on the original object (docs/water.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::water_sim
