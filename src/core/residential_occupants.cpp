// Our cSC4ResidentialSimulator's occupant and grid functions (build 641;
// docs/residential.md): funding percentages of schools and hospitals, the
// average EQ and HQ grids, the grid getters, query data, capacity and
// coverage of one building, the school and hospital building lists, the
// ideal funding. Every function is a replacement on the original object
// (slots of the primary vtable 0x00AB2C10); the helpers they call inside the
// class are plain functions here.
//
// Fields used here beyond residential_sim.h's (offsets from the object's
// start), to merge into its namespace field:
//   kFundingChanged = 0xA0 (uint8, set by SetOccupantFundingPercentages)
//   kHealthTarget = 0xD8 (float, the most EstimateIdealFunding suggests for
//     a hospital), kHealthFactor = 0xDC (float), kHealthCurve = 0xE8 and
//     kHealthCurve2 = 0x108 (SC4Curve: funding percent -> effect)
//   kSchoolTarget = 0x138, kSchoolFactor = 0x13C, kSchoolCurve = 0x148,
//     kSchoolCurve2 = 0x168 (the same for schools)
//   kHospitalServices = 0x194, kSchoolServices = 0x198 (pointers to list
//     sentinels of 0x34-byte service records, nodes of 0x3C bytes)
//   kSchoolList = 0x19C, kHospitalList = 0x1A0 (SC4List heads)
//   kSchoolUnit = 0x2F4, kHospitalUnit = 0x2F8 (uint32 divisors)
// A service record (node + 8): +0 float, +4 SC4Curve, +0x14 float, +0x18
// float, +0x1C SC4Curve, +0x2C a list sentinel of uint32 (occupant groups),
// +0x30 uint32.
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/residential_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::residential_sim {
namespace {

namespace local {
constexpr std::size_t kQualityCellSize = 0x78;
constexpr std::size_t kFundingChanged = 0xA0;
constexpr std::size_t kHealthTarget = 0xD8;
constexpr std::size_t kHealthFactor = 0xDC;
constexpr std::size_t kHealthCurve = 0xE8;
constexpr std::size_t kHealthCurve2 = 0x108;
constexpr std::size_t kSchoolTarget = 0x138;
constexpr std::size_t kSchoolFactor = 0x13C;
constexpr std::size_t kSchoolCurve = 0x148;
constexpr std::size_t kSchoolCurve2 = 0x168;
constexpr std::size_t kHospitalServices = 0x194;
constexpr std::size_t kSchoolServices = 0x198;
constexpr std::size_t kSchoolList = 0x19C;
constexpr std::size_t kHospitalList = 0x1A0;
constexpr std::size_t kSchoolUnit = 0x2F4;
constexpr std::size_t kHospitalUnit = 0x2F8;
} // namespace local

// Occupant properties.
constexpr uint32_t kSchoolProperty = 0x8922041B;
constexpr uint32_t kHospitalProperty = 0x69220415;
constexpr uint32_t kFundingFlagProperty = 0xEAA69F83;

// Budget line keys (the budget simulator's slot 51).
constexpr uint32_t kSchoolKey = 0xEA5654B6;
constexpr uint32_t kSchoolKey2 = 0x4A5654BA;
constexpr uint32_t kHospitalKey = 0xCA565486;
constexpr uint32_t kHospitalKey2 = 0xEA56549E;

// The game's float 0.01 (0x00A823A4), widened as x87 does.
constexpr double kHundredth = static_cast<double>(0.01f);

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = uint32_t(__cdecl*)(void* block);
using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0, unrounded
using CurveFn = void*(__thiscall*)(void* curve);
using CurveArgFn = uint32_t(__thiscall*)(void* curve, void* argument);
using CurveCopyFn = void*(__thiscall*)(void* curve, const void* other);
using InsertNFn = void(__thiscall*)(void* vector, void* where, const void* value, void* allocator, uint32_t count,
                                    uint32_t atEnd);
using InsertRangeFn = void(__thiscall*)(void* list, void* where, void* first, void* last, void* dummy);

void* StlAllocate(std::size_t size) { return reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(size); }
uint32_t Free(void* block) { return reinterpret_cast<FreeFn>(addr::SC4_Free)(block); }

uint8_t GetUint32(void* holder, uint32_t property, void* value)
{
    return reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32)(holder, property, value);
}

uint8_t GetFloat(void* holder, uint32_t property, void* value)
{
    return reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(holder, property, value);
}

double Evaluate(void* curve, float x) { return reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(curve, x); }

uint32_t FullFunding() { return Global<uint32_t>(addr::cSC4ResidentialSimulator_FullFunding); }

// __ftol2: truncation toward zero (EDX:EAX), the low 32 bits.
uint32_t FtolLow(double value) { return static_cast<uint32_t>(static_cast<int64_t>(value)); }

void* Holder(void* occupant) { return Virtual<void*>(occupant, 0x18); }

// What the budget simulator's slot 51 fills in for an occupant and a key:
// +0 the item key (its slot 28), +0x10 and +0x1C copied into query data.
struct BudgetLine {
    uint32_t word[8];
};

uint32_t FindBudgetLine(void* self, void* holder, uint32_t key, BudgetLine* line)
{
    return Virtual<uint32_t>(At<void*>(self, field::kBudget), 0xCC, holder, key, static_cast<void*>(line));
}

struct Point {
    float x;
    float y;
};

struct PointVector {
    Point* begin;
    Point* end;
    Point* capacity;
};

// An SC4Curve's 16 bytes (points begin, end, capacity, count).
struct Curve {
    uint32_t word[4];
};

// --- Lists of service records ------------------------------------------------

char* Next(char* node) { return *reinterpret_cast<char**>(node); }

void LinkBefore(char* node, char* where)
{
    char* const before = At<char*>(where, 4);
    At<char*>(node, 0) = where;
    At<char*>(node, 4) = before;
    At<char*>(before, 0) = node;
    At<char*>(where, 4) = node;
}

char* NewSentinel(std::size_t size)
{
    char* const node = static_cast<char*>(StlAllocate(size));
    At<char*>(node, 0) = node;
    At<char*>(node, 4) = node;
    return node;
}

// 0x006FCB40: empties a list of service records: each record's group list,
// its two curves' points, the node.
void ClearServices(char** list)
{
    char* node = Next(*list);
    if (node != *list) {
        do {
            char* const record = node;
            node = Next(node);
            char* group = Next(At<char*>(record, 0x34));
            while (group != At<char*>(record, 0x34)) {
                char* const done = group;
                group = Next(group);
                Free(done);
            }
            At<char*>(At<char*>(record, 0x34), 0) = At<char*>(record, 0x34);
            At<char*>(At<char*>(record, 0x34), 4) = At<char*>(record, 0x34);
            if (At<char*>(record, 0x34))
                Free(At<char*>(record, 0x34));
            if (At<void*>(record, 0x24))
                Free(At<void*>(record, 0x24));
            if (At<void*>(record, 0xC))
                Free(At<void*>(record, 0xC));
            Free(record);
        } while (node != *list);
    }
    At<char*>(*list, 0) = *list;
    At<char*>(*list, 4) = *list;
}

// 0x006FB9F0: copies a service record (curves through the game's copy
// constructor, the group list through its range insert).
void CopyService(char* to, char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
    reinterpret_cast<CurveCopyFn>(addr::SC4Curve_Copy)(to + 0x4, from + 0x4);
    At<uint32_t>(to, 0x14) = At<uint32_t>(from, 0x14);
    At<uint32_t>(to, 0x18) = At<uint32_t>(from, 0x18);
    reinterpret_cast<CurveCopyFn>(addr::SC4Curve_Copy)(to + 0x1C, from + 0x1C);
    At<char*>(to, 0x2C) = nullptr;
    char* const groups = NewSentinel(0xC);
    At<char*>(to, 0x2C) = groups;
    char* const source = At<char*>(from, 0x2C);
    uint32_t dummy = 0;
    reinterpret_cast<InsertRangeFn>(addr::SC4StdList_InsertRangeUint32)(to + 0x2C, Next(groups), Next(source), source,
                                                                         &dummy);
    At<uint32_t>(to, 0x30) = At<uint32_t>(from, 0x30);
}

// 0x006F94F0: whether a service record applies to an occupant: yes when its
// group list is empty, else when one of the occupant's values of property
// `key` (a uint32 array) is in it.
uint8_t ServiceApplies(void* occupant, char* record, uint32_t key)
{
    void* const holder = Holder(occupant);
    if (Next(At<char*>(record, 0x2C)) == At<char*>(record, 0x2C))
        return 1;
    void* const property = Virtual<void*>(holder, 0x24, key);
    if (!property)
        return 0;
    void* const variant = Virtual<void*>(property, 0x14);
    const auto* const values = Virtual<const uint32_t*>(variant, 0x1A4);
    for (uint32_t i = 0; i < Virtual<uint32_t>(variant, 0x10); ++i) {
        char* const head = At<char*>(record, 0x2C);
        char* group = Next(head);
        while (group != head && At<uint32_t>(group, 8) != values[i])
            group = Next(group);
        if (group != At<char*>(record, 0x2C))
            return 1;
    }
    return 0;
}

// 0x006FD070: `out` emptied, then copies of the records of `source` that
// apply to the occupant.
void CollectServices(void* occupant, uint32_t key, char** source, char** out)
{
    ClearServices(out);
    char* const head = *source;
    for (char* node = Next(head); node != head; node = Next(node)) {
        if (!ServiceApplies(occupant, node + 8, key))
            continue;
        char* const where = *out;
        char* const fresh = static_cast<char*>(StlAllocate(0x3C));
        CopyService(fresh + 8, node + 8);
        LinkBefore(fresh, where);
    }
}

// 0x006F9340 (the occupant is not read): the products of the records' +0,
// +0x18 and +0x14 floats.
void ServiceProducts(char** list, float* first, float* second, float* third)
{
    char* const head = *list;
    *first = 1.0f;
    *third = 1.0f;
    *second = 1.0f;
    for (char* node = Next(head); node != head; node = Next(node)) {
        *first = static_cast<float>(static_cast<double>(*first) * At<float>(node, 0x8));
        *second = static_cast<float>(static_cast<double>(At<float>(node, 0x20)) * *second);
        *third = static_cast<float>(static_cast<double>(At<float>(node, 0x1C)) * *third);
    }
}

// --- Funding -----------------------------------------------------------------

// 0x006F6F30: the occupant's property 0xEAA69F83 is present and not 0.
uint8_t FundingFlag(void* occupant)
{
    uint32_t value = 0;
    if (GetUint32(Holder(occupant), kFundingFlagProperty, &value))
        return value != 0 ? 1 : 0;
    return 0;
}

// 0x006F8710: the effects of a building's funding (through slot 20): the
// school (or health) percentage times the factor at +0x13C (+0xDC) when the
// flag property is set and `useCurve` is not, else through the curve at
// +0x148 (+0xE8); the other percentage through the curve at +0x168 (+0x108).
// Both stay at the default when the building has no budget line.
void FundingEffects(void* self, void* occupant, float* first, float* second, uint32_t school, uint32_t useCurve)
{
    *reinterpret_cast<uint32_t*>(first) = FullFunding();
    *reinterpret_cast<uint32_t*>(second) = FullFunding();
    float schoolFunding = 0.0f;
    float healthFunding = 0.0f;
    BudgetLine line;
    line.word[7] = 0;
    Virtual<uint32_t>(self, 0x50, occupant, &schoolFunding, &healthFunding, school);
    const bool isSchool = static_cast<uint8_t>(school) != 0;
    const std::size_t factor = isSchool ? local::kSchoolFactor : local::kHealthFactor;
    const std::size_t curve = isSchool ? local::kSchoolCurve : local::kHealthCurve;
    const std::size_t curve2 = isSchool ? local::kSchoolCurve2 : local::kHealthCurve2;
    void* const holder = Holder(occupant);
    if (!static_cast<uint8_t>(FindBudgetLine(self, holder, isSchool ? kSchoolKey : kHospitalKey, &line)))
        return;
    char* const base = static_cast<char*>(self);
    if (!static_cast<uint8_t>(useCurve) && FundingFlag(occupant))
        *first = static_cast<float>(static_cast<double>(schoolFunding) * At<float>(self, factor));
    else
        *first = static_cast<float>(
            Evaluate(base + curve, static_cast<float>(static_cast<double>(schoolFunding) * 100.0)) * kHundredth);
    *second = static_cast<float>(
        Evaluate(base + curve2, static_cast<float>(static_cast<double>(healthFunding) * 100.0)) * kHundredth);
}

// Slot 19 (0x006F8290): sets a building's school and health funding
// percentages on its budget lines (the second keys when `second` is set) and
// marks the funding changed.
uint32_t __fastcall SetOccupantFundingPercentages(void* self, void*, void* occupant, const float* school,
                                                  const float* health, uint32_t second)
{
    static bool logged = false;
    FirstCall("SetOccupantFundingPercentages", logged);
    void* const holder = Holder(occupant);
    const bool on = static_cast<uint8_t>(second) != 0;
    const uint32_t schoolKey = on ? kSchoolKey : kHospitalKey;
    const uint32_t healthKey = on ? kSchoolKey2 : kHospitalKey2;
    BudgetLine line;
    line.word[7] = 0;
    uint32_t result = FindBudgetLine(self, holder, schoolKey, &line);
    if (static_cast<uint8_t>(result)) {
        void* item = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, line.word[0]);
        if (!item)
            return 0;
        Virtual<uint32_t>(item, 0x60, holder, school, schoolKey);
        result = FindBudgetLine(self, holder, healthKey, &line);
        if (static_cast<uint8_t>(result)) {
            item = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, line.word[0]);
            if (!item)
                return 0;
            result = Virtual<uint32_t>(item, 0x60, holder, health, healthKey);
        }
    }
    At<uint8_t>(self, local::kFundingChanged) = 1;
    return result;
}

// Slot 20 (0x006F8360 through the thunk 0x006F84B0): a building's school
// and health funding percentages from its budget lines (the item's own
// value, or the line's when the item has it for the key); the default when
// there is no line.
uint32_t __fastcall GetOccupantFundingPercentages(void* self, void*, void* occupant, uint32_t* school,
                                                  uint32_t* health, uint32_t second)
{
    static bool logged = false;
    FirstCall("GetOccupantFundingPercentages", logged);
    void* const holder = Holder(occupant);
    const bool on = static_cast<uint8_t>(second) != 0;
    const uint32_t schoolKey = on ? kSchoolKey : kHospitalKey;
    const uint32_t healthKey = on ? kSchoolKey2 : kHospitalKey2;
    BudgetLine line;
    line.word[7] = 0;
    if (!static_cast<uint8_t>(FindBudgetLine(self, holder, schoolKey, &line))) {
        *school = FullFunding();
        *health = FullFunding();
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(health));
    }
    void* item = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, line.word[0]);
    if (!item)
        return 0;
    if (static_cast<uint8_t>(Virtual<uint32_t>(item, 0x70, holder, schoolKey)))
        *school = line.word[7];
    else
        *school = *Virtual<uint32_t*>(item, 0x28);
    if (!static_cast<uint8_t>(FindBudgetLine(self, holder, healthKey, &line))) {
        const uint32_t value = FullFunding();
        *health = value;
        return value;
    }
    item = Virtual<void*>(At<void*>(self, field::kBudget), 0x70, line.word[0]);
    if (!item)
        return 0;
    if (static_cast<uint8_t>(Virtual<uint32_t>(item, 0x70, holder, healthKey))) {
        *health = line.word[7];
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(health));
    }
    const uint32_t value = *Virtual<uint32_t*>(item, 0x28);
    *health = value;
    return value;
}

// --- Grids -------------------------------------------------------------------

// 0x006F70E0: fills `grid` with the population-weighted average of the three
// wealth grids (truncated to an integer; 0 where nobody lives) and returns
// the smallest and largest value (starting from 255 and 0). Returns the
// `largest` pointer, as the game's EAX.
uint32_t AverageGrid(void** grids, void** population, void* grid, float* smallest, float* largest)
{
    float low = 255.0f;
    float high = 0.0f;
    for (uint32_t x = 0; x < Virtual<uint32_t>(grid, 0x30); ++x) {
        for (uint32_t z = 0; z < Virtual<uint32_t>(grid, 0x34); ++z) {
            uint16_t total = Virtual<uint16_t>(population[0], 0x58, x, z);
            total = static_cast<uint16_t>(total + Virtual<uint16_t>(population[1], 0x58, x, z));
            total = static_cast<uint16_t>(total + Virtual<uint16_t>(population[2], 0x58, x, z));
            int32_t average = 0;
            if (total != 0) {
                const float people2 = static_cast<float>(Virtual<uint16_t>(population[2], 0x58, x, z));
                const float part2 =
                    static_cast<float>(Virtual<double>(grids[2], 0x58, x, z) * static_cast<double>(people2));
                const float people1 = static_cast<float>(Virtual<uint16_t>(population[1], 0x58, x, z));
                const float part12 = static_cast<float>(
                    Virtual<double>(grids[1], 0x58, x, z) * static_cast<double>(people1) + static_cast<double>(part2));
                const float people0 = static_cast<float>(Virtual<uint16_t>(population[0], 0x58, x, z));
                const double sum =
                    Virtual<double>(grids[0], 0x58, x, z) * static_cast<double>(people0) + static_cast<double>(part12);
                average = static_cast<int32_t>(FtolLow(sum / static_cast<double>(total)));
            }
            const float value = static_cast<float>(average);
            if (low > value)
                low = value;
            if (high < value)
                high = value;
            Virtual<uint32_t>(grid, 0x68, x, z, value);
        }
    }
    if (smallest)
        *smallest = low;
    if (largest)
        *largest = high;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(largest));
}

// A cSC4SimGridFloat32 as the game constructs it inline (operator new, 0x44
// bytes).
void* NewFloatGrid()
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    auto* const grid = static_cast<uint32_t*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x44));
    if (!grid)
        return nullptr;
    grid[1] = 0x00A80784;
    grid[0] = static_cast<uint32_t>(addr::SC4SimGridFloat32_Vtable);
    grid[1] = 0x00A87C04;
    reinterpret_cast<uint8_t*>(grid)[8] = 0;
    grid[3] = 0;
    grid[4] = 0x49B9E60A;
    grid[6] = 0;
    grid[14] = 0;
    grid[15] = 0;
    grid[16] = 0;
    return grid;
}

// Slots 21 and 22: a new float grid (referenced, its slot 3, cell size from
// +0x78, ID) filled by AverageGrid from the EQ (or HQ) grids.
uint32_t AverageQualityGrid(void* self, std::size_t grids, uint32_t id, void** out, float* smallest, float* largest)
{
    *out = NewFloatGrid();
    Virtual<uint32_t>(*out, 0x4);
    Virtual<uint32_t>(*out, 0xC);
    Virtual<uint32_t>(*out, 0x24, At<uint32_t>(self, local::kQualityCellSize));
    Virtual<uint32_t>(*out, 0x18, id);
    char* const base = static_cast<char*>(self);
    return AverageGrid(reinterpret_cast<void**>(base + grids), reinterpret_cast<void**>(base + field::kPopulationGrids),
                       *out, smallest, largest);
}

// Slot 21 (0x006FCF30): the average EQ grid.
uint32_t __fastcall GetAverageEQGrid(void* self, void*, void** out, float* smallest, float* largest)
{
    static bool logged = false;
    FirstCall("GetAverageEQGrid", logged);
    return AverageQualityGrid(self, field::kEQGrids, 0x69D5C351, out, smallest, largest);
}

// Slot 22 (0x006FCFD0): the average HQ grid.
uint32_t __fastcall GetAverageHQGrid(void* self, void*, void** out, float* smallest, float* largest)
{
    static bool logged = false;
    FirstCall("GetAverageHQGrid", logged);
    return AverageQualityGrid(self, field::kHQGrids, 0x69D5C360, out, smallest, largest);
}

// Slots 23 to 25: the three grids at `offset`, each referenced.
uint32_t ThreeGrids(void* self, std::size_t offset, void** first, void** second, void** third)
{
    *first = At<void*>(self, offset);
    *second = At<void*>(self, offset + 4);
    *third = At<void*>(self, offset + 8);
    Virtual<uint32_t>(*first, 0x4);
    Virtual<uint32_t>(*second, 0x4);
    return Virtual<uint32_t>(*third, 0x4);
}

// Slot 23 (0x006F72C0): the EQ grids by wealth.
uint32_t __fastcall GetEQGrids(void* self, void*, void** first, void** second, void** third)
{
    static bool logged = false;
    FirstCall("GetEQGrids", logged);
    return ThreeGrids(self, field::kEQGrids, first, second, third);
}

// Slot 24 (0x006F7300): the HQ grids by wealth.
uint32_t __fastcall GetHQGrids(void* self, void*, void** first, void** second, void** third)
{
    static bool logged = false;
    FirstCall("GetHQGrids", logged);
    return ThreeGrids(self, field::kHQGrids, first, second, third);
}

// Slot 25 (0x006F7340): the population grids by wealth.
uint32_t __fastcall GetPopulationGrids(void* self, void*, void** first, void** second, void** third)
{
    static bool logged = false;
    FirstCall("GetPopulationGrids", logged);
    return ThreeGrids(self, field::kPopulationGrids, first, second, third);
}

// --- Query data --------------------------------------------------------------

// Slot 26 (0x006F91C0): a school's figures: capacity (+0x14), +0x18, both
// in units of +0x2F4 (+0x1C rounded down, +0x20 rounded up), +0x24 (-1 read
// as 0), the float +0x28, and the budget lines of both keys (+0x2C/+0x30,
// +0x34/+0x38; the default and 0 when missing).
uint8_t __fastcall GetSchoolQueryData(void* self, void*, void* occupant, char* data)
{
    static bool logged = false;
    FirstCall("GetSchoolQueryData", logged);
    void* const holder = Holder(occupant);
    uint32_t first = 0;
    if (!GetUint32(holder, kSchoolProperty, &first))
        return 0;
    BudgetLine line;
    line.word[7] = 0;
    if (!GetUint32(holder, kSchoolProperty, data + 0x14))
        return 0;
    if (!GetUint32(holder, 0xE9244B2E, data + 0x18))
        return 0;
    if (!GetUint32(holder, 0x09220161, data + 0x24))
        return 0;
    uint32_t value = 0;
    if (!GetFloat(holder, 0x292453C5, &value))
        return 0;
    At<uint32_t>(data, 0x28) = value;
    if (At<int32_t>(data, 0x24) == -1)
        At<uint32_t>(data, 0x24) = 0;
    At<uint32_t>(data, 0x1C) = At<uint32_t>(data, 0x14) / At<uint32_t>(self, local::kSchoolUnit);
    const uint32_t unit = At<uint32_t>(self, local::kSchoolUnit);
    At<uint32_t>(data, 0x20) = (unit + At<uint32_t>(data, 0x18) - 1) / unit;
    if (static_cast<uint8_t>(FindBudgetLine(self, holder, kSchoolKey, &line))) {
        At<uint32_t>(data, 0x30) = line.word[4];
        At<uint32_t>(data, 0x2C) = line.word[7];
    } else {
        At<uint32_t>(data, 0x30) = 0;
        At<uint32_t>(data, 0x2C) = FullFunding();
    }
    if (static_cast<uint8_t>(FindBudgetLine(self, holder, kSchoolKey2, &line))) {
        At<uint32_t>(data, 0x34) = line.word[7];
        At<uint32_t>(data, 0x38) = line.word[4];
    } else {
        At<uint32_t>(data, 0x38) = 0;
        At<uint32_t>(data, 0x34) = FullFunding();
    }
    return 1;
}

// Slot 27 (0x006F7E80): a hospital's figures, as GetSchoolQueryData's with
// the hospital properties and +0x2F8 (both rounded down); no figures without
// the first budget line.
uint8_t __fastcall GetHospitalQueryData(void* self, void*, void* occupant, char* data)
{
    static bool logged = false;
    FirstCall("GetHospitalQueryData", logged);
    void* const holder = Holder(occupant);
    uint32_t first = 0;
    if (!GetUint32(holder, kHospitalProperty, &first))
        return 0;
    BudgetLine line;
    line.word[7] = 0;
    if (!GetUint32(holder, kHospitalProperty, data + 0x14))
        return 0;
    if (!GetUint32(holder, 0x49244B21, data + 0x18))
        return 0;
    if (!GetUint32(holder, 0xC922015C, data + 0x24))
        return 0;
    uint32_t value = 0;
    if (!GetFloat(holder, 0x092453CB, &value))
        return 0;
    if (!static_cast<uint8_t>(FindBudgetLine(self, holder, kHospitalKey, &line)))
        return 0;
    At<uint32_t>(data, 0x28) = value;
    if (At<int32_t>(data, 0x24) == -1)
        At<uint32_t>(data, 0x24) = 0;
    At<uint32_t>(data, 0x1C) = At<uint32_t>(data, 0x14) / At<uint32_t>(self, local::kHospitalUnit);
    At<uint32_t>(data, 0x20) = At<uint32_t>(data, 0x18) / At<uint32_t>(self, local::kHospitalUnit);
    At<uint32_t>(data, 0x2C) = line.word[7];
    At<uint32_t>(data, 0x30) = line.word[4];
    if (static_cast<uint8_t>(FindBudgetLine(self, holder, kHospitalKey2, &line))) {
        At<uint32_t>(data, 0x38) = line.word[4];
        At<uint32_t>(data, 0x34) = line.word[7];
    } else {
        At<uint32_t>(data, 0x38) = 0;
        At<uint32_t>(data, 0x34) = FullFunding();
    }
    return 1;
}

// --- Capacity, coverage, ideal funding ---------------------------------------

// Slot 28 (0x006FD510): a school's (or hospital's) capacity property times
// its funding effect and the product of the applying service records' +0x18,
// truncated; and that in units of +0x2F4, rounded up. Returns what the
// game's last call (the free) left in EAX.
uint32_t __fastcall EstimateCurrentOccupantCapacity(void* self, void*, void* occupant, uint32_t* capacity,
                                                    uint32_t* units)
{
    static bool logged = false;
    FirstCall("EstimateCurrentOccupantCapacity", logged);
    uint32_t effectBits = FullFunding();
    char* const sentinel = static_cast<char*>(StlAllocate(0x3C));
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    char* list = sentinel;
    float product20 = 1.0f;
    float product14 = 1.0f;
    float product0 = 1.0f;
    uint32_t value = 0;
    const uint8_t school = GetUint32(Holder(occupant), kSchoolProperty, &value);
    char* const base = static_cast<char*>(self);
    uint32_t key;
    uint32_t property;
    char** owner;
    if (school) {
        owner = reinterpret_cast<char**>(base + local::kSchoolServices);
        key = 0xA92D8E40;
        property = kSchoolProperty;
    } else {
        owner = reinterpret_cast<char**>(base + local::kHospitalServices);
        key = 0x292D8F9B;
        property = kHospitalProperty;
    }
    float other = 0.0f;
    FundingEffects(self, occupant, reinterpret_cast<float*>(&effectBits), &other, school, 1);
    ClearServices(&list);
    CollectServices(occupant, key, owner, &list);
    if (Next(sentinel) != sentinel)
        ServiceProducts(&list, &product0, &product20, &product14);
    GetUint32(Holder(occupant), property, &value);
    float effect;
    std::memcpy(&effect, &effectBits, sizeof(effect));
    const uint32_t result = FtolLow(static_cast<double>(value) * static_cast<double>(effect) *
                                    static_cast<double>(product20));
    *capacity = result;
    const uint32_t unit = At<uint32_t>(self, local::kSchoolUnit);
    *units = (unit + result - 1) / unit;
    ClearServices(&list);
    return Free(sentinel);
}

// 0x006FC400: the three coverage curves of a school (or hospital) from the
// occupant's properties, each defaulting to a flat line at 100.
void LoadCoverageCurves(void* holder, void* first, void* second, void* third, uint32_t school)
{
    constexpr uint32_t kProperties[6] = {0xC9299662, 0xC92D7EC0, 0xE921A936, 0xA92AE3BD, 0xA92D9FCD, 0x2921A9CC};
    PointVector points{nullptr, nullptr, nullptr};
    Point point{0.0f, 100.0f};
    const auto insert = reinterpret_cast<InsertNFn>(addr::SC4PointVector_InsertN);
    insert(&points, nullptr, &point, &school, 1u, 1u);
    point = Point{100.0f, 100.0f};
    if (points.end != points.capacity) {
        if (points.end)
            *points.end = point;
        ++points.end;
    } else {
        insert(&points, points.end, &point, &school, 1u, 1u);
    }
    const uint32_t* const ids = static_cast<uint8_t>(school) ? kProperties : kProperties + 3;
    void* const curves[3] = {first, second, third};
    for (int i = 0; i < 3; ++i) {
        if (void* const property = Virtual<void*>(holder, 0x24, ids[i]))
            reinterpret_cast<CurveArgFn>(addr::SC4Curve_LoadFromProperty)(curves[i], property);
        else
            reinterpret_cast<CurveArgFn>(addr::SC4Curve_SetPoints)(curves[i], &points);
    }
    if (points.begin)
        Free(points.begin);
}

// Slot 45 (0x006FD260): a school's (or hospital's) coverage: rangeZ = its
// funding effect x the applying records' +0 product x the range property;
// rangeX from the first coverage curve's points at the effectiveness (in
// percent): interpolated between the last point above it and the first at or
// below it, times rangeZ. False for other buildings.
uint8_t __fastcall GetOccupantCoverage(void* self, void*, void* occupant, const float* effectiveness, float* rangeX,
                                       float* rangeZ)
{
    static bool logged = false;
    FirstCall("GetOccupantCoverage", logged);
    uint8_t found = 0;
    uint8_t school = 0;
    void* const holder = Holder(occupant);
    char* const base = static_cast<char*>(self);
    uint32_t scratch = 0;
    uint32_t key = 0;
    uint32_t property = 0;
    char** owner = nullptr;
    if (GetUint32(holder, kSchoolProperty, &scratch)) {
        found = 1;
        school = 1;
        key = 0xA92D8E40;
        property = 0x691B42B3;
        owner = reinterpret_cast<char**>(base + local::kSchoolServices);
    }
    if (GetUint32(holder, kHospitalProperty, &scratch)) {
        found = 1;
        key = 0x292D8F9B;
        property = 0x091B42A2;
        owner = reinterpret_cast<char**>(base + local::kHospitalServices);
    } else if (!found) {
        return found;
    }
    float rangeEffect = Global<float>(addr::cSC4ResidentialSimulator_DefaultPercentage);
    float rangeProperty = 0.0f;
    float product0 = 0.0f;
    float product20 = 0.0f;
    float product14 = 0.0f;
    char* const sentinel = NewSentinel(0x3C);
    char* list = sentinel;
    Curve curve3;
    Curve curve2;
    Curve curve1;
    const auto init = reinterpret_cast<CurveFn>(addr::SC4Curve_Init);
    init(&curve3);
    init(&curve2);
    init(&curve1);
    GetFloat(holder, property, &rangeProperty);
    float effect = 0.0f;
    FundingEffects(self, occupant, &effect, &rangeEffect, school, 0);
    LoadCoverageCurves(holder, &curve3, &curve2, &curve1, school);
    ClearServices(&list);
    CollectServices(occupant, key, owner, &list);
    ServiceProducts(&list, &product0, &product20, &product14);
    *rangeZ = static_cast<float>(static_cast<double>(rangeEffect) * static_cast<double>(product0) *
                                 static_cast<double>(rangeProperty));
    PointVector points{nullptr, nullptr, nullptr};
    reinterpret_cast<CurveArgFn>(addr::SC4Curve_GetPoints)(&curve1, &points);
    const auto count = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(points.end) - reinterpret_cast<uintptr_t>(points.begin)) >> 3);
    if (count != 0) {
        *reinterpret_cast<uint32_t*>(rangeX) = 0;
        Point* const previous = points.begin;
        for (uint32_t i = 1; i < count; ++i) {
            const Point& point = points.begin[i];
            if (static_cast<double>(*effectiveness) * 100.0 < static_cast<double>(point.y)) {
                *previous = point;
                continue;
            }
            const double percent = static_cast<double>(*effectiveness) * 100.0;
            const double share = (static_cast<double>(previous->y) - percent) /
                                 (static_cast<double>(previous->y) - static_cast<double>(point.y));
            const double step = (static_cast<double>(point.x) - static_cast<double>(previous->x)) * kHundredth;
            const double scaled = share * step * static_cast<double>(*rangeZ);
            *rangeX = static_cast<float>(scaled + static_cast<double>(previous->x) * static_cast<double>(*rangeZ));
            break;
        }
    }
    if (points.begin)
        Free(points.begin);
    if (curve1.word[0])
        Free(reinterpret_cast<void*>(curve1.word[0]));
    if (curve2.word[0])
        Free(reinterpret_cast<void*>(curve2.word[0]));
    if (curve3.word[0])
        Free(reinterpret_cast<void*>(curve3.word[0]));
    ClearServices(&list);
    Free(sentinel);
    return found;
}

// The query data the game builds on the stack (an empty SC4String first).
struct QueryData {
    uint32_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t word[12]; // +0x10 to +0x3C
};
static_assert(sizeof(QueryData) == 0x40);

// The string part's inline destructor.
void DestroyQueryData(QueryData& data)
{
    data.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (data.begin)
        Free(data.begin);
}

// Slot 58 (0x006FD680): the funding at which a school (or hospital) would
// serve its capacity: from the ratio of its capacity to its students (query
// data +0x14, times the applying records' +0x18 product), stepped by 0.01
// through the funding curve (+0x148, or +0xE8) down while the curve is above
// the ratio (not below 0), then up while below (not above +0x138, or +0xD8).
uint8_t __fastcall EstimateIdealFunding(void* self, void*, void* occupant, float* funding)
{
    static bool logged = false;
    FirstCall("EstimateIdealFunding", logged);
    QueryData data;
    data.begin = nullptr;
    data.end = nullptr;
    data.capacity = nullptr;
    char* const buffer = static_cast<char*>(StlAllocate(8));
    data.begin = buffer;
    data.end = buffer;
    data.capacity = buffer + 8;
    *buffer = 0;
    data.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    data.word[0] = 0;  // +0x10
    data.word[6] = 0;  // +0x28
    data.word[7] = 0;  // +0x2C
    data.word[9] = 0;  // +0x34
    const uint8_t school = static_cast<uint8_t>(Virtual<uint32_t>(self, 0x68, occupant, static_cast<void*>(&data)));
    if (!school && !static_cast<uint8_t>(Virtual<uint32_t>(self, 0x6C, occupant, static_cast<void*>(&data)))) {
        DestroyQueryData(data);
        return 0;
    }
    uint32_t check = 0xFFFFFFFF;
    GetUint32(Holder(occupant), school ? 0x09220161u : 0xC922015Cu, &check);
    if (check == 0xFFFFFFFF) {
        DestroyQueryData(data);
        return 0;
    }
    const uint32_t capacity = data.word[5]; // +0x24
    const uint32_t key = school ? 0xA92D8E40u : 0x292D8F9Bu;
    char* const base = static_cast<char*>(self);
    char** const owner =
        reinterpret_cast<char**>(base + (school ? local::kSchoolServices : local::kHospitalServices));
    char* const sentinel = static_cast<char*>(StlAllocate(0x3C));
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    char* list = sentinel;
    CollectServices(occupant, key, owner, &list);
    float product0 = 1.0f;
    float product20 = 1.0f;
    float product14 = 1.0f;
    ServiceProducts(&list, &product0, &product20, &product14);
    const uint32_t students =
        FtolLow(static_cast<double>(data.word[1]) * static_cast<double>(product20)); // +0x14
    const float ratio = static_cast<float>(static_cast<double>(capacity) / static_cast<double>(students));
    const float target = At<float>(self, school ? local::kSchoolTarget : local::kHealthTarget);
    void* const curve = base + (school ? local::kSchoolCurve : local::kHealthCurve);
    float value = ratio;
    double effect = Evaluate(curve, static_cast<float>(static_cast<double>(ratio) * 100.0)) * kHundredth;
    if (effect > static_cast<double>(ratio)) {
        for (;;) {
            const double lower = static_cast<double>(value) - kHundredth;
            value = static_cast<float>(lower);
            effect = Evaluate(curve, static_cast<float>(lower * 100.0)) * kHundredth;
            if (value < 0.0f || value == 0.0f) {
                value = 0.0f;
                break;
            }
            if (!(effect > static_cast<double>(ratio)))
                break;
        }
    }
    if (effect < static_cast<double>(ratio)) {
        for (;;) {
            const double higher = static_cast<double>(value) + kHundredth;
            value = static_cast<float>(higher);
            effect = Evaluate(curve, static_cast<float>(higher * 100.0)) * kHundredth;
            if (value > target) {
                value = target;
                break;
            }
            if (!(effect < static_cast<double>(ratio)))
                break;
        }
    }
    *funding = value;
    ClearServices(&list);
    Free(sentinel);
    DestroyQueryData(data);
    return 1;
}

// --- Building lists ----------------------------------------------------------

// 0x006FACE0: appends the occupants of a list (referenced) to `out`, only
// those in one of `groups` (their slot 22) when given. Returns the list's
// sentinel, as the game's EAX.
uint32_t AppendBuildings(char** list, char** out, void* const* groups)
{
    char* const head = *list;
    for (char* node = Next(head); node != head; node = Next(node)) {
        void* const occupant = At<void*>(node, 8);
        if (groups) {
            const auto count = [groups] {
                return static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<uintptr_t>(groups[1]) -
                                                                  reinterpret_cast<uintptr_t>(groups[0])) >>
                                             2);
            };
            bool found = false;
            for (uint32_t i = 0, n = count(); i < n; ++i) {
                if (found)
                    break;
                if (static_cast<uint8_t>(
                        Virtual<uint32_t>(occupant, 0x58, static_cast<const uint32_t*>(groups[0])[i])))
                    found = true;
                n = count();
            }
            if (!found)
                continue;
        }
        Virtual<uint32_t>(occupant, 0x4);
        char* const where = *out;
        char* const fresh = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
        At<void*>(fresh, 8) = occupant;
        LinkBefore(fresh, where);
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(head));
}

// Slots 47 and 49: the buildings of each list of the vector at `lists`, then
// of the list at `last`.
uint32_t Buildings(void* self, std::size_t lists, std::size_t last, char** out, void* const* groups)
{
    char* const base = static_cast<char*>(self);
    for (uint32_t i = 0;
         i < static_cast<uint32_t>(static_cast<int32_t>(At<uintptr_t>(self, lists + 4) - At<uintptr_t>(self, lists)) >> 2);
         ++i)
        AppendBuildings(At<char**>(self, lists) + i, out, groups);
    return AppendBuildings(reinterpret_cast<char**>(base + last), out, groups);
}

// Slot 47 (0x006FADE0): the school buildings.
uint32_t __fastcall GetSchoolBuildings(void* self, void*, char** out, void* const* groups)
{
    static bool logged = false;
    FirstCall("GetSchoolBuildings", logged);
    return Buildings(self, field::kSchoolBuildings, local::kSchoolList, out, groups);
}

// Slot 49 (0x006FAE50): the hospital buildings.
uint32_t __fastcall GetHospitalBuildings(void* self, void*, char** out, void* const* groups)
{
    static bool logged = false;
    FirstCall("GetHospitalBuildings", logged);
    return Buildings(self, field::kHospitalBuildings, local::kHospitalList, out, groups);
}

// --- Installation ------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn)                                                                    \
    {addr::cSC4ResidentialSimulator_vftable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(19, 0x006F8290, SetOccupantFundingPercentages),
    OPENSC4_SLOT(20, 0x006F84B0, GetOccupantFundingPercentages),
    OPENSC4_SLOT(21, 0x006FCF30, GetAverageEQGrid),
    OPENSC4_SLOT(22, 0x006FCFD0, GetAverageHQGrid),
    OPENSC4_SLOT(23, 0x006F72C0, GetEQGrids),
    OPENSC4_SLOT(24, 0x006F7300, GetHQGrids),
    OPENSC4_SLOT(25, 0x006F7340, GetPopulationGrids),
    OPENSC4_SLOT(26, 0x006F91C0, GetSchoolQueryData),
    OPENSC4_SLOT(27, 0x006F7E80, GetHospitalQueryData),
    OPENSC4_SLOT(28, 0x006FD510, EstimateCurrentOccupantCapacity),
    OPENSC4_SLOT(45, 0x006FD260, GetOccupantCoverage),
    OPENSC4_SLOT(47, 0x006FADE0, GetSchoolBuildings),
    OPENSC4_SLOT(49, 0x006FAE50, GetHospitalBuildings),
    OPENSC4_SLOT(58, 0x006FD680, EstimateIdealFunding),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("%s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool Install(registry::Mode) { return SwapSlots(kSlots, "residential sim occupants"); }

const bool g_registered = registry::Register(
    {"simulators.residential.occupants", "simulators/residential", registry::Kind::Vtable,
     registry::Phase::PostAppInit, addr::cSC4ResidentialSimulator_vftable,
     "Our cISC4ResidentialSimulator occupant and grid functions: funding percentages, average EQ/HQ grids, "
     "grid getters, query data, capacity, coverage, building lists, ideal funding (docs/residential.md)",
     false, Install});

} // namespace
} // namespace opensc4::residential_sim
