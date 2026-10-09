// cSC4FlammabilitySimulator (class 0xE990C045; docs/flammability.md): how
// flammable each cell is, a walk that averages it and marks hot cells,
// clusters of hot cells, and the monthly chance of a fire. Every function
// of its own is here, each group off by default:
//
//   simulators.flammability.sim: identity, Init, Shutdown, tuning, messages,
//     the walk, cells, clusters, fires, Write, Read;
//   simulators.flammability.object: our own object under the class ID.
//
// Layout (0xB0 bytes): +0 cISC4FlammabilitySimulator, +4 message target,
// +8 cIGZSerializable, +0xC initialised, +0x10 references, +0x18 random
// numbers, +0x30 the walk (a tract iterator), +0x34 the city, +0x38/+0x3C
// its size in cells, +0x40 message server, +0x44/+0x48/+0x4C/+0x50/+0x54
// services, +0x58 the flammability grid, +0x5C the occupants' raw values,
// +0x60 the average, +0x64 the multiplier, +0x68/+0x6C the walk's sum and
// count, +0x70 the month's hot cells, +0x7C clusters, +0x80.. tuning.
#include "core/address_check.h"
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::flammability_sim {
namespace {

namespace field {
constexpr std::size_t kInitialised = 0xC;
constexpr std::size_t kReferences = 0x10;
constexpr std::size_t kRandom = 0x18;
constexpr std::size_t kWalk = 0x30;          // TractIterator*
constexpr std::size_t kCity = 0x34;
constexpr std::size_t kWidth = 0x38;
constexpr std::size_t kHeight = 0x3C;
constexpr std::size_t kMessageServer = 0x40;
constexpr std::size_t kSeason = 0x44;        // 0x00B43DAC
constexpr std::size_t kWater = 0x48;         // 0x00B43D94
constexpr std::size_t kSettings = 0x4C;      // 0x00B43D90
constexpr std::size_t kNetwork = 0x50;       // 0x00B43D0C
constexpr std::size_t kLots = 0x54;          // 0x00B43D08
constexpr std::size_t kGrid = 0x58;          // uint8 sim grid
constexpr std::size_t kRaw = 0x5C;           // uint8 sim grid
constexpr std::size_t kAverage = 0x60;       // uint8
constexpr std::size_t kMultiplier = 0x64;    // float
constexpr std::size_t kSum = 0x68;
constexpr std::size_t kCount = 0x6C;
constexpr std::size_t kHotCells = 0x70;      // vector of (x, z, 1)
constexpr std::size_t kClusters = 0x7C;      // list of (x, z, count)
constexpr std::size_t kSeasonFactor = 0x80;  // float
constexpr std::size_t kLotFactor = 0x84;     // float
constexpr std::size_t kDryFactor = 0x88;     // float
constexpr std::size_t kBlock = 0x8C;
constexpr std::size_t kSteps = 0x90;
constexpr std::size_t kCellsPerStep = 0x94;
constexpr std::size_t kClusterDistance = 0x98;
constexpr std::size_t kHotThreshold = 0x9C;
constexpr std::size_t kClusterMinimum = 0xA0;
constexpr std::size_t kLotFireChance = 0xA4; // float, percent
constexpr std::size_t kClusterCurve = 0xA8;  // SC4Curve*
} // namespace field

constexpr uint32_t kClassId = 0xE990C045;
constexpr uint32_t kBuilding = 0x278128A0;
constexpr uint32_t kLotType = 0xA823821E;
constexpr uint32_t kFlora = 0x74758926;
constexpr uint32_t kMessages[] = {0x99EF1142, 0x99EF1143, 0x99EF1145, 0x6716A4E5, 0xC2C09554, 0xE99B3D4D};
constexpr uint32_t kUnregister[] = {0x6716A4E5, 0xC2C09554, 0x99EF1142, 0x99EF1143, 0x99EF1145, 0xE99B3D4D};

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto GetPropertySint32 =
    reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, int32_t*)>(addr::SC4_GetPropertySint32);
const auto GetPropertyFloat = reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, void*)>(addr::SC4_GetPropertyFloat);

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Hold(char* self, std::size_t at, void* object)
{
    At<void*>(self, at) = object;
    Virtual<uint32_t>(object, 0x4);
}

int32_t MagicRound(double value)
{
    const auto rounded = static_cast<float>(value + 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof(bits));
    return static_cast<int32_t>(bits + 0xB4C00000);
}

// A cell of a uint8 sim grid through its rows (slot 24: the row table, 12
// bytes a row).
uint8_t& Cell(void* rows, int32_t x, int32_t z)
{
    char* const table = *static_cast<char**>(rows);
    return At<uint8_t*>(table, static_cast<std::size_t>(x) * 12)[z];
}

struct Hot {
    int32_t x;
    int32_t z;
    int32_t count;
};

struct HotVector {
    Hot* begin;
    Hot* end;
    Hot* capacity;
};

// --- Identity ----------------------------------------------------------------------

// 0x006AE380 (+8 slot 5).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    return kClassId;
}

// 0x006AE390.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    char* found;
    switch (iid) {
    case 1:
    case 0x48F41617:
        found = self;
        break;
    case 0x452294AA:
        found = self ? self + 4 : nullptr;
        break;
    case 0xE4FDA3D4:
        found = self ? self + 8 : nullptr;
        break;
    default:
        return 0;
    }
    *out = found;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// 0x006AE4A0.
uint32_t __fastcall ReleaseSelf(char* self, void*)
{
    int32_t& references = At<int32_t>(self, field::kReferences);
    if (references > 1)
        return static_cast<uint32_t>(--references);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x24, 1u);
    return 0;
}

// --- Getters ---------------------------------------------------------------------------

// 0x006AE410: the settings' value 0x28F42AA0, more in season 2.
double __fastcall GetMultiplier(char* self, void*)
{
    const auto value = static_cast<float>(Virtual<double>(At<void*>(self, field::kSettings), 0x64, 0x28F42AA0u, 0u));
    if (Virtual<uint32_t>(At<void*>(self, field::kSeason), 0x38) == 2)
        return static_cast<double>(value) * At<float>(self, field::kSeasonFactor);
    return value;
}

// 0x006AE450 (slot 8; only in our object's vtable, too short to hook).
uint8_t __fastcall GetAverageFlammability(char* self, void*)
{
    return At<uint8_t>(self, field::kAverage);
}

// 0x006AE460.
uint8_t __fastcall IsCellFlammable(char* self, void*, int32_t x, int32_t z)
{
    if (x < 0 || z < 0 || x >= At<int32_t>(self, field::kWidth) || z >= At<int32_t>(self, field::kHeight))
        return 0;
    return Virtual<uint8_t>(At<void*>(self, field::kGrid), 0x1C, x, z) != 0;
}

// 0x006AE4D0: an occupant's flammability (property 0x29244DB5), more for
// a building or lot whose lot is of kind 4; 0 for other types.
uint8_t __fastcall OccupantFlammability(char* self, void*, void* occupant)
{
    float factor = 1.0f;
    int32_t value = 0;
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kBuilding || type == kLotType) {
        if (void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x38, occupant))
            if (Virtual<uint32_t>(lot, 0x8C) == 4)
                factor = At<float>(self, field::kLotFactor);
    } else if (type != kFlora) {
        return 0;
    }
    GetPropertySint32(Virtual<void*>(occupant, 0x18), 0x29244DB5, &value);
    const int32_t rounded = MagicRound(static_cast<double>(value) * factor);
    if (rounded > 0xFF)
        return 0xFF;
    return static_cast<uint8_t>(rounded < 0 ? 0 : rounded);
}

// 0x006AE590: the largest cluster's value and position (cells times the
// grid's cell size).
uint8_t __fastcall GetFlammabilityHotspot(char* self, void*, uint8_t* value, int32_t* x, int32_t* z)
{
    int32_t best = 0;
    int32_t bestX = 0;
    int32_t bestZ = 0;
    uint8_t found = 0;
    char* const head = At<char*>(self, field::kClusters);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kClusters); node = At<char*>(node, 0)) {
        if (At<int32_t>(node, 0x10) <= best)
            continue;
        bestZ = At<int32_t>(node, 0xC);
        bestX = At<int32_t>(node, 8);
        best = At<int32_t>(node, 0x10);
        found = Virtual<uint8_t>(At<void*>(self, field::kGrid), 0x58, bestX, bestZ);
    }
    if (!found)
        return 0;
    if (value)
        *value = found;
    if (x)
        *x = Virtual<int32_t>(At<void*>(self, field::kGrid), 0x28) * bestX;
    if (z)
        *z = Virtual<int32_t>(At<void*>(self, field::kGrid), 0x28) * bestZ;
    return 1;
}

// --- Cells -----------------------------------------------------------------------

// 0x006AEE90: the block's occupants of the three types (network service
// slot 40 with a type filter): the highest building value, else the highest
// lot value, else the highest other.
uint8_t __fastcall CellFlammability(char* self, void*, int32_t x, int32_t z)
{
    using FilterBaseFn = void*(__thiscall*)(void* filter);
    using PushFn = void(__thiscall*)(void* vector, uint32_t* where, uint32_t* value, void* allocator, uint32_t count,
                                     uint32_t atEnd);
    const int32_t block = At<int32_t>(self, field::kBlock);
    const int32_t first[2] = {block * x, block * z};
    const int32_t last[2] = {block + first[0] - 1, block + first[1] - 1};
    char* const sentinel = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    char* list = sentinel;
    char* const filter = static_cast<char*>(OperatorNew(0x18));
    if (filter) {
        reinterpret_cast<FilterBaseFn>(addr::SC4_OccupantFilterBase)(filter);
        At<std::uintptr_t>(filter, 0) = addr::SC4OccupantTypeFilter_vftable;
        At<std::uintptr_t>(filter, 4) = addr::SC4OccupantTypeFilter_vftable_4;
        At<uint32_t>(filter, 0xC) = 0;
        At<uint32_t>(filter, 0x10) = 0;
        At<uint32_t>(filter, 0x14) = 0;
    }
    Virtual<uint32_t>(filter, 0x4);
    for (uint32_t type : {kBuilding, kFlora, kLotType}) {
        auto*& end = At<uint32_t*>(filter, 0x10);
        if (end != At<uint32_t*>(filter, 0x14)) {
            if (end)
                *end = type;
            ++end;
        } else {
            uint8_t allocator = 0;
            reinterpret_cast<PushFn>(addr::SC4PtrVector_InsertN)(filter + 0xC, end, &type, &allocator, 1, 1);
        }
    }
    Virtual<uint32_t>(At<void*>(self, field::kNetwork), 0xA0, &list, first, last, filter, 0xFFFFFFFFu);
    Release(filter);
    int32_t building = 0;
    int32_t lot = 0;
    int32_t other = 0;
    for (char* node = At<char*>(list, 0); node != list; node = At<char*>(node, 0)) {
        void* const occupant = At<void*>(node, 8);
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        if (type == kBuilding) {
            const int32_t value = OccupantFlammability(self, nullptr, occupant);
            if (value > building)
                building = value;
        } else if (Virtual<uint32_t>(occupant, 0x1C) == kLotType) {
            if (building == 0) {
                const int32_t value = OccupantFlammability(self, nullptr, occupant);
                if (value > lot)
                    lot = value;
            }
        } else if (building == 0 && lot == 0) {
            const int32_t value = OccupantFlammability(self, nullptr, occupant);
            if (value > other)
                other = value;
        }
        Release(occupant);
    }
    int32_t result = building > 0 ? building : lot > 0 ? lot : other;
    if (result > 0xFF)
        result = 0xFF;
    else if (result < 0)
        result = 0;
    reinterpret_cast<ThisFn>(addr::SC4List_Clear2)(&list);
    Virtual<uint32_t>(ListNodeAllocator(), 0x10, list);
    return static_cast<uint8_t>(result);
}

// 0x006AF380: the cells under an occupant: raised to its value (raiseOnly),
// or recomputed; the flammability grid is the raw value times the
// multiplier.
void __fastcall UpdateOccupant(char* self, void*, void* occupant, uint8_t raiseOnly)
{
    using IntersectFn = uint8_t(__thiscall*)(int32_t* out, const int32_t* a, const int32_t* b);
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type != kBuilding && type != kLotType && type != kFlora)
        return;
    uint8_t value = OccupantFlammability(self, nullptr, occupant);
    if (raiseOnly && value == 0)
        return;
    void* const raw = Virtual<void*>(At<void*>(self, field::kRaw), 0x60);
    void* const grid = Virtual<void*>(At<void*>(self, field::kGrid), 0x60);
    int32_t rect[4];
    Virtual<uint32_t>(occupant, 0x2C, rect);
    const uint32_t shift = Virtual<uint32_t>(At<void*>(self, field::kRaw), 0x2C);
    for (int32_t& edge : rect)
        edge <<= shift;
    if (rect[0] < 0 || rect[2] >= At<int32_t>(self, field::kWidth) || rect[1] < 0 ||
        rect[3] >= At<int32_t>(self, field::kHeight)) {
        const int32_t bounds[4] = {0, 0, At<int32_t>(self, field::kWidth) - 1, At<int32_t>(self, field::kHeight) - 1};
        reinterpret_cast<IntersectFn>(addr::SC4Rect_Intersect)(rect, rect, bounds);
    }
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            if (raiseOnly) {
                uint8_t& cell = Cell(raw, x, z);
                if (cell < value)
                    cell = value;
            } else {
                value = CellFlammability(self, nullptr, x, z);
                Cell(raw, x, z) = value;
            }
            int32_t scaled = MagicRound(static_cast<double>(value) * At<float>(self, field::kMultiplier));
            if (scaled < 0)
                scaled = 0;
            else if (scaled > 0xFF)
                scaled = 0xFF;
            Cell(grid, x, z) = static_cast<uint8_t>(scaled);
        }
    }
}

// --- The walk -------------------------------------------------------------------

// 0x006AE940: the month's hot cells merged into clusters: a cell within
// +0x98 (city-block distance) of a cluster moves its centre (integer mean)
// and adds to it, else starts one; clusters under +0xA0 cells dropped.
void __fastcall RebuildClusters(char* self, void*)
{
    const auto& hot = At<HotVector>(self, field::kHotCells);
    const int32_t count = static_cast<int32_t>(hot.end - hot.begin);
    char* head = At<char*>(self, field::kClusters);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, field::kClusters);) {
        char* const next = At<char*>(node, 0);
        Free(node);
        node = next;
    }
    head = At<char*>(self, field::kClusters);
    At<char*>(head, 0) = head;
    At<char*>(At<char*>(self, field::kClusters), 4) = At<char*>(self, field::kClusters);
    for (int32_t i = 0; i < count; ++i) {
        const Hot& cell = At<HotVector>(self, field::kHotCells).begin[i];
        char* const clusters = At<char*>(self, field::kClusters);
        bool merged = false;
        for (char* node = At<char*>(clusters, 0); node != clusters; node = At<char*>(node, 0)) {
            const int32_t cx = At<int32_t>(node, 8);
            const int32_t cz = At<int32_t>(node, 0xC);
            const int32_t dx = cell.x < cx ? cx - cell.x : cell.x - cx;
            const int32_t dz = cell.z < cz ? cz - cell.z : cell.z - cz;
            if (dx + dz < At<int32_t>(self, field::kClusterDistance)) {
                const int32_t n = At<int32_t>(node, 0x10);
                At<int32_t>(node, 8) = (cx * n + cell.x) / (n + 1);
                At<int32_t>(node, 0xC) = (cz * n + cell.z) / (n + 1);
                At<int32_t>(node, 0x10) = n + 1;
                merged = true;
                break;
            }
        }
        if (merged)
            continue;
        auto* const node = static_cast<char*>(Allocate(0x14));
        if (node + 8) {
            At<int32_t>(node, 8) = cell.x;
            At<int32_t>(node, 0xC) = cell.z;
            At<int32_t>(node, 0x10) = cell.count;
        }
        char* const last = At<char*>(clusters, 4);
        At<char*>(node, 0) = clusters;
        At<char*>(node, 4) = last;
        At<char*>(last, 0) = node;
        At<char*>(clusters, 4) = node;
    }
    for (char* node = At<char*>(At<char*>(self, field::kClusters), 0); node != At<char*>(self, field::kClusters);) {
        char* const next = At<char*>(node, 0);
        if (At<int32_t>(node, 0x10) < At<int32_t>(self, field::kClusterMinimum)) {
            char* const after = At<char*>(node, 0);
            char* const before = At<char*>(node, 4);
            At<char*>(before, 0) = after;
            At<char*>(after, 4) = before;
            Free(node);
        }
        node = next;
    }
}

// 0x006AF150: monthly, unless disasters are off (city slot 82, its slot 8):
// a draw of 0-100 under +0xA4 sets a random lot's building alight; else
// the clusters in a shuffled order, the first whose curve value (+0xA8 at
// its size) beats the draw; the fire goes through city slot 72's object
// 0x621CDA33 (interface 0x49B43F57).
void __fastcall MaybeStartFire(char* self, void*)
{
    using RangeFn = double(__thiscall*)(void* random, double low, double high);
    using IndexInitFn = void(__thiscall*)(void* index, uint32_t mode);
    using IndexNextFn = uint8_t(__thiscall*)(void* index, uint32_t* out);
    using QueryHeldFn = void**(__cdecl*)(void** out, void* object, uint32_t iid, uint32_t unused);
    void* const city = Global<void*>(addr::SC4_City);
    if (void* const disasters = Virtual<void*>(city, 0x148))
        if (Virtual<uint8_t>(disasters, 0x20))
            return;
    // The draw is compared unrounded with +0xA4, then kept as a float.
    const double exact = reinterpret_cast<RangeFn>(addr::SC4Random_Uniform)(self + field::kRandom, 0.0, 100.0);
    const auto draw = static_cast<float>(exact);
    uint8_t found = 0;
    uint32_t fromLot = 1;
    int32_t fx = 0;
    int32_t fz = 0;
    bool fromLotPath = false;
    if (exact < At<float>(self, field::kLotFireChance)) {
        if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x34)) {
            if (void* const building = Virtual<void*>(lot, 0xC0)) {
                void* const occupant = Virtual<void*>(building, 0xC);
                float position[3];
                Virtual<uint32_t>(occupant, 0x20, position);
                Virtual<uint32_t>(At<void*>(self, field::kCity), 0x17C, position[0], position[2], &fx, &fz);
                fromLot = 1;
                fromLotPath = true;
            }
        }
    }
    if (!fromLotPath) {
        char* const clusters = At<char*>(self, field::kClusters);
        if (At<char*>(clusters, 0) == clusters || !At<void*>(self, field::kClusterCurve))
            return;
        struct {
            uint8_t flags[4];
            uint32_t buffer[3];
            uint32_t count;
            uint32_t zero[2];
            uint32_t one;
        } index{};
        index.flags[0] = 1;
        int32_t size = 0;
        for (char* node = At<char*>(clusters, 0); node != clusters; node = At<char*>(node, 0))
            ++size;
        index.one = 1;
        index.count = static_cast<uint32_t>(size);
        index.flags[1] = 0;
        index.flags[2] = 1;
        reinterpret_cast<IndexInitFn>(addr::SC4ShuffledIndex_Init)(&index, 1);
        uint32_t at = 0;
        while (reinterpret_cast<IndexNextFn>(addr::SC4ShuffledIndex_Next)(&index, &at) && !found) {
            char* node = At<char*>(At<char*>(self, field::kClusters), 0);
            for (uint32_t i = at; i > 0; --i)
                node = At<char*>(node, 0);
            const double chance = EvaluateCurve(At<void*>(self, field::kClusterCurve),
                                                static_cast<float>(At<int32_t>(node, 0x10)));
            if (chance > draw) {
                const int32_t size = Virtual<int32_t>(At<void*>(self, field::kGrid), 0x28);
                const int32_t half = size / 2;
                fx = At<int32_t>(node, 8) * size + half;
                fz = At<int32_t>(node, 0xC) * size + half;
                found = 1;
                fromLot = 0;
            }
        }
        if (index.buffer[0])
            Free(reinterpret_cast<void*>(static_cast<std::uintptr_t>(index.buffer[0])));
        if (!found)
            return;
    }
    void* const services = Virtual<void*>(city, 0x120);
    if (!services)
        return;
    void* const source = Virtual<void*>(services, 0x1C, 0x621CDA33u);
    void* fire = nullptr;
    reinterpret_cast<QueryHeldFn>(addr::SC4_QueryHeld)(&fire, source, 0x49B43F57, 0);
    Virtual<uint32_t>(fire, 0x14, fx, fz, fromLot, 0u, 0u);
    Virtual<uint32_t>(fire, 0x24, 5u);
    Release(fire);
}

// 0x006AF810 (the walk's end).
void __fastcall OnWalkEnd(char* self, void*)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    const int32_t count = At<int32_t>(self, field::kCount);
    At<uint8_t>(self, field::kAverage) = count ? static_cast<uint8_t>(At<int32_t>(self, field::kSum) / count) : 0;
    At<int32_t>(self, field::kCount) = 0;
    At<int32_t>(self, field::kSum) = 0;
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, 0x08F415FEu);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x10, message, 0u);
    Release(message);
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, 0x2A11CB48u,
                      static_cast<uint32_t>(At<uint8_t>(self, field::kAverage)));
    RebuildClusters(self, nullptr);
    auto& hot = At<HotVector>(self, field::kHotCells);
    hot.end = hot.begin;
    MaybeStartFire(self, nullptr);
}

// 0x006AF8D0: +0x94 cells of the walk: the raw value times the multiplier
// (times +0x88 where there is no water), at most 255, into the grid; hot
// cells (over +0x9C) noted; developed cells summed for the average.
void __fastcall Step(char* self, void*, uint32_t)
{
    using NextFn = uint8_t(__thiscall*)(void* walk, int32_t* x, int32_t* z);
    using RestartFn = void(__thiscall*)(void* walk, int32_t mode);
    using InsertFn = void(__thiscall*)(void* vector, Hot* where, const Hot* value, void* allocator, uint32_t count,
                                       uint32_t atEnd);
    void* const grid = Virtual<void*>(At<void*>(self, field::kGrid), 0x60);
    void* const raw = Virtual<void*>(At<void*>(self, field::kRaw), 0x60);
    void* const cells = Global<void*>(addr::SC4_Service43D14);
    int32_t left = At<int32_t>(self, field::kCellsPerStep);
    while (left != 0) {
        int32_t x = 0;
        int32_t z = 0;
        if (!reinterpret_cast<NextFn>(addr::TractIterator_Next)(At<void*>(self, field::kWalk), &x, &z)) {
            OnWalkEnd(self, nullptr);
            reinterpret_cast<RestartFn>(addr::TractIterator_Restart)(At<void*>(self, field::kWalk), 1);
            continue;
        }
        int32_t value = Cell(raw, x, z);
        if (value > 0) {
            value = MagicRound(static_cast<double>(value) * At<float>(self, field::kMultiplier));
            if (!Virtual<uint8_t>(At<void*>(self, field::kWater), 0x2C, x, z))
                value = MagicRound(static_cast<double>(value) * At<float>(self, field::kDryFactor));
            if (value > 0xFF)
                value = 0xFF;
            Cell(grid, x, z) = static_cast<uint8_t>(value);
            if (value > At<int32_t>(self, field::kHotThreshold)) {
                auto& hot = At<HotVector>(self, field::kHotCells);
                const Hot cell{x, z, 1};
                if (hot.end != hot.capacity) {
                    if (hot.end)
                        *hot.end = cell;
                    ++hot.end;
                } else {
                    uint8_t allocator = 0;
                    reinterpret_cast<InsertFn>(addr::SC4Uint32x3Vector_Insert)(&hot, hot.end, &cell, &allocator, 1, 1);
                }
            }
        }
        if (Virtual<uint32_t>(cells, 0x30, x, z)) {
            ++At<int32_t>(self, field::kCount);
            At<int32_t>(self, field::kSum) += value;
        }
        --left;
    }
}

// --- Messages ---------------------------------------------------------------------

// 0x006AFA60 (this = +4).
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - 4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    auto marked = [](void* occupant) {
        return Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, 0x029244DBu) != 0;
    };
    switch (type) {
    case 0x66956814:
        Step(self, nullptr, Virtual<uint32_t>(message, 0x24));
        break;
    case 0x6716A4E5:
    case 0xC2C09554:
        At<float>(self, field::kMultiplier) = static_cast<float>(GetMultiplier(self, nullptr));
        break;
    case 0x99EF1142:
    case 0x99EF1145: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!marked(occupant))
            UpdateOccupant(self, nullptr, occupant, 1);
        break;
    }
    case 0x99EF1143: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!marked(occupant))
            UpdateOccupant(self, nullptr, occupant, 0);
        break;
    }
    case 0xE99B3D4D: {
        void* const data = Virtual<void*>(message, 0x80);
        void* query = nullptr;
        if (!data || !Virtual<uint8_t>(data, 0x0, 0xE818FCEDu, &query))
            break;
        // Slot 3 takes no argument: the 0 the game pushes before it is
        // UpdateOccupant's second.
        if (void* const building = Virtual<void*>(query, 0xC0))
            UpdateOccupant(self, nullptr, Virtual<void*>(building, 0xC), 0);
        Release(query);
        break;
    }
    default:
        break;
    }
    return 1;
}

// --- Tuning, Init, Shutdown ----------------------------------------------------------

void DropCurve(char* self)
{
    if (void* const curve = At<void*>(self, field::kClusterCurve)) {
        if (void* const points = At<void*>(curve, 0))
            Free(points);
        OperatorDelete(curve);
        At<void*>(self, field::kClusterCurve) = nullptr;
    }
}

// 0x006AEB50.
void __fastcall LoadTuning(char* self, void*)
{
    using GetVectorFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* vector);
    using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
    At<int32_t>(self, field::kBlock) = 1;
    At<int32_t>(self, field::kSteps) = 40;
    At<float>(self, field::kSeasonFactor) = 1.2f;
    At<float>(self, field::kLotFactor) = 1.1f;
    At<float>(self, field::kDryFactor) = 1.1f;
    At<float>(self, field::kLotFireChance) = 0.01f;
    At<int32_t>(self, field::kClusterDistance) = 8;
    At<int32_t>(self, field::kHotThreshold) = 196;
    At<int32_t>(self, field::kClusterMinimum) = 16;
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xA8F41EE3};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    GetPropertyFloat(holder, 0xC9B40128, self + field::kSeasonFactor);
    GetPropertyFloat(holder, 0x29B40134, self + field::kLotFactor);
    GetPropertyFloat(holder, 0x29B4013B, self + field::kDryFactor);
    uint32_t values[3] = {};
    reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyUint32Vector)(holder, 0x29B4013C, values);
    const auto* const begin = reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(values[0]));
    if (((values[1] - values[0]) & ~3u) == 12) {
        At<uint32_t>(self, field::kHotThreshold) = begin[0];
        At<uint32_t>(self, field::kClusterDistance) = begin[1];
        At<uint32_t>(self, field::kClusterMinimum) = begin[2];
    }
    GetPropertyFloat(holder, 0x29B4013E, self + field::kLotFireChance);
    DropCurve(self);
    if (Virtual<uint8_t>(holder, 0xC, 0x29B4013Du)) {
        auto* const curve = static_cast<uint32_t*>(OperatorNew(0x10));
        if (curve)
            curve[0] = curve[1] = curve[2] = curve[3] = 0;
        At<void*>(self, field::kClusterCurve) = curve;
        reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar)(curve, key, 0x29B4013D);
    }
    Release(exemplar);
    if (values[0])
        Free(reinterpret_cast<void*>(static_cast<std::uintptr_t>(values[0])));
}

void* NewGrid(uint32_t block, uint32_t id)
{
    auto* const grid = static_cast<uint32_t*>(OperatorNew(0x44));
    if (grid) {
        grid[0] = static_cast<uint32_t>(addr::SC4SimGridUint8_Vtable);
        grid[1] = static_cast<uint32_t>(addr::SC4SimGridUint8_Vtable2);
        reinterpret_cast<uint8_t*>(grid)[8] = 0;
        grid[3] = 0;
        grid[4] = 0x49B9E602;
        grid[6] = 0;
        grid[14] = 0;
        grid[15] = 0;
        grid[16] = 0;
    }
    Virtual<uint32_t>(grid, 0x4);
    Virtual<uint8_t>(grid, 0xC);
    Virtual<void>(grid, 0x24, block);
    Virtual<uint32_t>(grid, 0x18, id);
    Virtual<void>(grid, 0x6C, 0u);
    return grid;
}

// 0x006AF540.
uint8_t __fastcall Init(char* self, void*)
{
    if (self[field::kInitialised])
        return 1;
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("flammability sim: first Init (%p)", static_cast<void*>(self));
    }
    self[field::kInitialised] = 1;
    LoadTuning(self, nullptr);
    {
        struct GameString {
            std::uintptr_t vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t zero;
        } name{};
        void* const simulators = Global<void*>(addr::SC4_SimulatorService);
        using FromTextFn = void*(__thiscall*)(GameString* text, const char* value);
        void* const text =
            reinterpret_cast<FromTextFn>(addr::SC4String_FromText)(&name, reinterpret_cast<const char*>(addr::SC4_FlammabilityName));
        Virtual<uint32_t>(simulators, 0x4C, self + 4, 4u, text, 2u);
        name.vtable = addr::SC4String_Vtable;
        if (name.begin)
            Free(name.begin);
    }
    if (!At<void*>(self, field::kMessageServer)) {
        Hold(self, field::kMessageServer, Global<void*>(addr::SC4_MessageServer));
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x14, self + 4, message);
    }
    if (!At<void*>(self, field::kCity))
        Hold(self, field::kCity, Global<void*>(addr::SC4_City));
    At<int32_t>(self, field::kWidth) = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174);
    At<int32_t>(self, field::kHeight) = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178);
    const struct {
        std::size_t at;
        std::uintptr_t global;
    } services[] = {{field::kSettings, addr::SC4_Service43D90},
                    {field::kSeason, addr::SC4_Service43DAC},
                    {field::kWater, addr::SC4_Service43D94},
                    {field::kNetwork, addr::SC4_NetworkService},
                    {field::kLots, addr::SC4_LotManager}};
    for (const auto& service : services)
        if (!At<void*>(self, service.at))
            Hold(self, service.at, Global<void*>(service.global));
    if (!At<void*>(self, field::kGrid))
        At<void*>(self, field::kGrid) = NewGrid(At<uint32_t>(self, field::kBlock), 0x49D5B953);
    if (!At<void*>(self, field::kRaw))
        At<void*>(self, field::kRaw) = NewGrid(At<uint32_t>(self, field::kBlock), 0x49D5B964);
    void* const grid = At<void*>(self, field::kGrid);
    const int32_t depth = Virtual<int32_t>(grid, 0x34);
    At<int32_t>(self, field::kCellsPerStep) = Virtual<int32_t>(grid, 0x30) * depth / At<int32_t>(self, field::kSteps);
    const int32_t lastZ = Virtual<int32_t>(At<void*>(self, field::kGrid), 0x34) - 1;
    const int32_t lastX = Virtual<int32_t>(At<void*>(self, field::kGrid), 0x30) - 1;
    char* const walk = At<char*>(self, field::kWalk);
    At<int32_t>(walk, 4) = 0;
    At<int32_t>(walk, 8) = 0;
    At<int32_t>(walk, 0xC) = lastX;
    At<int32_t>(walk, 0x10) = lastZ;
    if (At<int32_t>(walk, 0xC) < At<int32_t>(walk, 4))
        At<int32_t>(walk, 0xC) = At<int32_t>(walk, 4);
    if (At<int32_t>(walk, 0x10) < At<int32_t>(walk, 8))
        At<int32_t>(walk, 0x10) = At<int32_t>(walk, 8);
    walk[2] = 1;
    At<int32_t>(self, field::kSum) = 0;
    At<int32_t>(self, field::kCount) = 0;
    return 1;
}

// 0x006AED40.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!self[field::kInitialised])
        return 1;
    self[field::kInitialised] = 0;
    DropCurve(self);
    for (const std::size_t at : {field::kGrid, field::kRaw}) {
        if (void* const grid = At<void*>(self, at)) {
            Virtual<uint8_t>(grid, 0x10);
            Release(At<void*>(self, at));
            At<void*>(self, at) = nullptr;
        }
    }
    for (const std::size_t at : {field::kSettings, field::kSeason, field::kWater, field::kCity}) {
        if (void* const held = At<void*>(self, at)) {
            Release(held);
            At<void*>(self, at) = nullptr;
        }
    }
    if (At<void*>(self, field::kMessageServer)) {
        for (const uint32_t message : kUnregister)
            Virtual<uint8_t>(At<void*>(self, field::kMessageServer), 0x18, self + 4, message);
        Release(At<void*>(self, field::kMessageServer));
        At<void*>(self, field::kMessageServer) = nullptr;
    }
    for (const std::size_t at : {field::kNetwork, field::kLots}) {
        if (void* const held = At<void*>(self, at)) {
            Release(held);
            At<void*>(self, at) = nullptr;
        }
    }
    if (void* const simulator = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulator, 0x50, self + 4);
    return 1;
}

// --- Write (0x006AE620) and Read (0x006AE830), this = +8 -------------------------

uint32_t StreamOk(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) == 0;
}

uint32_t __fastcall Write(char* serializable, void*, void* stream)
{
    using WriteObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
    char* const self = serializable - 8;
    const auto writeReference = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);
    Virtual<uint8_t>(stream, 0x1C, uint16_t{2});
    writeReference(stream, At<void*>(self, field::kGrid));
    writeReference(stream, At<void*>(self, field::kRaw));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kMultiplier));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kAverage)));
    char* const head = At<char*>(self, field::kClusters);
    uint32_t count = 0;
    for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
        ++count;
    Virtual<uint8_t>(stream, 0x24, count);
    for (char* node = At<char*>(At<char*>(self, field::kClusters), 0); node != At<char*>(self, field::kClusters);
         node = At<char*>(node, 0)) {
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(node, 8));
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(node, 0xC));
        Virtual<uint8_t>(stream, 0x20, At<int32_t>(node, 0x10));
    }
    return StreamOk(stream);
}

uint32_t __fastcall Read(char* serializable, void*, void* stream)
{
    using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);
    char* const self = serializable - 8;
    const auto readReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 2)
        return 0;
    readReference(stream, 0xC9BD939A, &At<void*>(self, field::kGrid), 0);
    readReference(stream, 0xC9BD939A, &At<void*>(self, field::kRaw), 0);
    Virtual<uint8_t>(stream, 0x30, self + field::kMultiplier);
    Virtual<uint8_t>(stream, 0x14, self + field::kAverage);
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    while (count != 0) {
        --count;
        int32_t values[3];
        Virtual<uint8_t>(stream, 0x20, &values[0]);
        Virtual<uint8_t>(stream, 0x20, &values[1]);
        Virtual<uint8_t>(stream, 0x20, &values[2]);
        char* const head = At<char*>(self, field::kClusters);
        auto* const node = static_cast<char*>(Allocate(0x14));
        if (node + 8) {
            At<int32_t>(node, 8) = values[0];
            At<int32_t>(node, 0xC) = values[1];
            At<int32_t>(node, 0x10) = values[2];
        }
        char* const last = At<char*>(head, 4);
        At<char*>(node, 0) = head;
        At<char*>(node, 4) = last;
        At<char*>(last, 0) = node;
        At<char*>(head, 4) = node;
    }
    return StreamOk(stream);
}

// --- Our object ------------------------------------------------------------------

void* g_vtable[10];
void* g_vtable4[4];
void* g_vtable8[6];

void SetVtables(char* self)
{
    At<void**>(self, 0) = g_vtable;
    At<void**>(self, 4) = g_vtable4;
    At<void**>(self, 8) = g_vtable8;
}

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, field::kReferences);
}

// As the destructor (0x006AEAF0).
void Destroy(char* self)
{
    SetVtables(self);
    if (char* const walk = At<char*>(self, field::kWalk)) {
        if (void* const buffer = At<void*>(walk, 0x18))
            Free(buffer);
        OperatorDelete(walk);
    }
    reinterpret_cast<ThisFn>(addr::SC4PairList_Destroy)(self + field::kClusters);
    if (void* const buffer = At<void*>(self, field::kHotCells))
        Free(buffer);
}

uint32_t __fastcall ObjectRelease(char* self, void*)
{
    int32_t& references = At<int32_t>(self, field::kReferences);
    if (references > 1)
        return static_cast<uint32_t>(--references);
    Virtual<uint8_t>(self, 0x10);
    log::Write("flammability object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    OperatorDelete(self);
    return 0;
}

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ObjectRelease(part - Offset, nullptr);
}

// As the constructor (0x006AE6D0).
void Construct(char* self)
{
    using RandomFn = void*(__thiscall*)(void* random, int32_t seed);
    SetVtables(self);
    self[field::kInitialised] = 0;
    At<uint32_t>(self, field::kReferences) = 0;
    reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(self + field::kRandom, 0);
    std::memset(self + 0x30, 0, 0x60 - 0x30);
    self[field::kAverage] = 0;
    std::memset(self + 0x64, 0, 0x7C - 0x64);
    At<void*>(self, field::kClusters) = nullptr;
    auto** const sentinel = static_cast<void**>(Allocate(0x14));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    At<void*>(self, field::kClusters) = sentinel;
    std::memset(self + 0x80, 0, 0xAC - 0x80);
    auto* const walk = static_cast<char*>(OperatorNew(0x30));
    if (walk) {
        walk[0] = 1;
        walk[1] = 1;
        walk[2] = 1;
        std::memset(walk + 4, 0, 0x2C - 4);
        At<uint32_t>(walk, 0x2C) = 1;
    }
    At<char*>(self, field::kWalk) = walk;
}

cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4FlammabilitySimulator_vftable), sizeof g_vtable);
        std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4FlammabilitySimulator_vftable_4),
                    sizeof g_vtable4);
        std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4FlammabilitySimulator_vftable_8),
                    sizeof g_vtable8);
        g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
        g_vtable[1] = reinterpret_cast<void*>(&AddRef);
        g_vtable[2] = reinterpret_cast<void*>(&ObjectRelease);
        g_vtable[8] = reinterpret_cast<void*>(&GetAverageFlammability); // 4 bytes: too short to hook
        g_vtable[9] = reinterpret_cast<void*>(&DeletingDestructor);
        g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<4>);
        g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<4>);
        g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<4>);
        g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<8>);
        g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<8>);
        g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<8>);
    }
    auto* const self = static_cast<char*>(OperatorNew(0xB0));
    if (!self)
        return nullptr;
    Construct(self);
    log::Write("flammability object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

// --- Registration ----------------------------------------------------------------------

// Hooks to leave out, for bisecting: OPENSC4_FLAMMABILITY_SKIP lists
// function addresses (hex).
bool Skipped(std::uintptr_t address)
{
    char list[512] = {};
    if (!GetEnvironmentVariableA("OPENSC4_FLAMMABILITY_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4FlammabilitySimulator_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
        {addr::cSC4FlammabilitySimulator_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4FlammabilitySimulator_GetMultiplier, reinterpret_cast<void*>(&GetMultiplier)},
        {addr::cSC4FlammabilitySimulator_IsCellFlammable, reinterpret_cast<void*>(&IsCellFlammable)},
        {addr::cSC4FlammabilitySimulator_Release, reinterpret_cast<void*>(&ReleaseSelf)},
        {addr::cSC4FlammabilitySimulator_OccupantFlammability, reinterpret_cast<void*>(&OccupantFlammability)},
        {addr::cSC4FlammabilitySimulator_GetFlammabilityHotspot, reinterpret_cast<void*>(&GetFlammabilityHotspot)},
        {addr::cSC4FlammabilitySimulator_Write, reinterpret_cast<void*>(&Write)},
        {addr::cSC4FlammabilitySimulator_Read, reinterpret_cast<void*>(&Read)},
        {addr::cSC4FlammabilitySimulator_RebuildClusters, reinterpret_cast<void*>(&RebuildClusters)},
        {addr::cSC4FlammabilitySimulator_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::cSC4FlammabilitySimulator_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4FlammabilitySimulator_CellFlammability, reinterpret_cast<void*>(&CellFlammability)},
        {addr::cSC4FlammabilitySimulator_MaybeStartFire, reinterpret_cast<void*>(&MaybeStartFire)},
        {addr::cSC4FlammabilitySimulator_UpdateOccupant, reinterpret_cast<void*>(&UpdateOccupant)},
        {addr::cSC4FlammabilitySimulator_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4FlammabilitySimulator_OnWalkEnd, reinterpret_cast<void*>(&OnWalkEnd)},
        {addr::cSC4FlammabilitySimulator_Step, reinterpret_cast<void*>(&Step)},
        {addr::cSC4FlammabilitySimulator_DoMessage, reinterpret_cast<void*>(&DoMessage)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
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
    "simulators.flammability.sim",
    "simulators/flammability",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FlammabilitySimulator_Init,
    "Our flammability simulator: cells, the walk, clusters, fires, tuning, Init, Shutdown, Write, Read "
    "(docs/flammability.md)",
    false,
    Install,
});

bool ApplyObject(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_objectRegistered = registry::Register({
    "simulators.flammability.object",
    "simulators/flammability",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4FlammabilitySimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/flammability.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::flammability_sim
