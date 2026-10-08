// Our cSC4PowerSimulator (build 641), first part: the getters of its
// interface, swapped into the original object's vtable (docs/power.md).
// Most are one field; the rest go through the city's helpers, a bit grid
// or a building's properties, as the game's do.
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

namespace opensc4::power_sim {
namespace {

// Fields of cSC4PowerSimulator used here (offsets from the object's start).
namespace field {
constexpr std::size_t kCityHelper = 0x18;       // cell size and count (slots 91-93), the clock (slot 50)
constexpr std::size_t kDepartment = 0x20;       // department budget; slot 10 is the funding
constexpr std::size_t kMonthlyProduction = 0x50; // int32 each, slots 11-15 and 17
constexpr std::size_t kMonthlyConsumption = 0x54;
constexpr std::size_t kMonthlyImport = 0x58;
constexpr std::size_t kMonthlyExport = 0x5C;
constexpr std::size_t kMonthlyDemand = 0x60;
constexpr std::size_t kPoweredConsumers = 0x6C;
constexpr std::size_t kUnpoweredConsumers = 0x70;
constexpr std::size_t kMonthlyRequirement = 0x74;
constexpr std::size_t kSupplyGrid = 0x80;       // cISC4SimGrid<uint8_t>*
constexpr std::size_t kConsumptionMap = 0x84;   // vector<vector<int16_t>>
constexpr std::size_t kAnnualConsumption = 0x270; // int32[10], by power type
constexpr std::size_t kPoleCount = 0x2B8;
constexpr std::size_t kWiredBits = 0x2C8;       // one bit per cell
constexpr std::size_t kPoweredBits = 0x2D4;
constexpr std::size_t kPlantCount = 0x2DC;
constexpr std::size_t kPlantCountByType = 0x2E0;
constexpr std::size_t kOverworkedPlants = 0x308;
constexpr std::size_t kAgedPlants = 0x30C;
constexpr std::size_t kDistressedPlants = 0x310;
constexpr std::size_t kStrikeStart = 0x334;
constexpr std::size_t kStrikeEfficiency = 0x36C;
} // namespace field

using GetPropertyUint32Fn = uint32_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
const auto GetPropertyUint32 = reinterpret_cast<GetPropertyUint32Fn>(addr::SC4_GetPropertyUint32);

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void* CityHelper(void* self) { return At<void*>(self, field::kCityHelper); }

// The cell's bit in one of the per-cell bit arrays (0x00603DD0).
uint32_t CellBit(void* self, std::size_t bits, int32_t x, int32_t z)
{
    const int32_t width = Virtual<int32_t>(CityHelper(self), 0x174);
    const int32_t index = width * z + x;
    return (At<const uint32_t*>(self, bits)[index >> 5] >> (x & 31)) & 1;
}

// 5 (0x006EAC40)
void __fastcall PositionToCell(void* self, void*, float x, float z, int32_t& cellX, int32_t& cellZ)
{
    cellX = Ftol(1.0 / Virtual<double>(CityHelper(self), 0x16C) * x);
    cellZ = Ftol(1.0 / Virtual<double>(CityHelper(self), 0x170) * z);
}

// 6 (0x006EAC90)
void __fastcall CellToPosition(void* self, void*, int32_t cellX, int32_t cellZ, float& x, float& z)
{
    x = static_cast<float>(Virtual<double>(CityHelper(self), 0x16C) * cellX);
    z = static_cast<float>(Virtual<double>(CityHelper(self), 0x170) * cellZ);
}

// 7, 8 (0x006EACD0): the cell as it is.
void __fastcall SameCell(void*, void*, int32_t x, int32_t z, int32_t& outX, int32_t& outZ)
{
    outX = x;
    outZ = z;
}

// 9, 10 (0x006EACF0, 0x006EAD30)
uint32_t __fastcall GetAnnualConsumption(void* self, void*)
{
    uint32_t sum = 0;
    for (int type = 0; type < 10; ++type)
        sum += At<uint32_t>(self, field::kAnnualConsumption + 4 * type);
    return sum;
}

uint32_t __fastcall GetAnnualConsumptionByPowerType(void* self, void*, int32_t type)
{
    return At<uint32_t>(self, field::kAnnualConsumption + 4 * type);
}

// 11-15, 17, 21, 22, 24, 26-29, 35, 36, 43: one field each (several of the
// game's are one-line getters shared with other classes).
template <std::size_t Offset>
uint32_t __fastcall FieldGetter(void* self, void*)
{
    return At<uint32_t>(self, Offset);
}

// 16 (0x006EBA10): the consumption map's value at a cell.
int32_t __fastcall GetMonthlyConsumptionAt(void* self, void*, int32_t x, int32_t z)
{
    auto* const rows = At<int16_t* const*>(self, field::kConsumptionMap);
    return rows[3 * x][z]; // rows are vectors: begin, end, capacity
}

// 18, 20 (0x006EAD40, 0x006EAD60)
void __fastcall GetMonthlyConsumptionMapPtr(void* self, void*, void*& map)
{
    map = static_cast<char*>(self) + field::kConsumptionMap;
}

void __fastcall GetSupplyValueMapPtr(void* self, void*, void*& grid)
{
    grid = At<void*>(self, field::kSupplyGrid);
}

// 19 (0x006EAD50): IsPowered, through the vtable.
uint32_t __fastcall GetSupplyValue(void* self, void*, int32_t x, int32_t z)
{
    return Virtual<uint32_t>(self, 0x94, x, z);
}

// 23 (0x006EFA00)
uint32_t __fastcall GetPowerPlantCountByType(void* self, void*, int32_t type)
{
    return At<uint32_t>(self, field::kPlantCountByType + 4 * type);
}

// 32 (0x006EB0D0): property 0x27812854's value (variant slot 72).
uint32_t __fastcall GetOccupantPowerConsumed(void*, void*, void* occupant, uint32_t& consumed)
{
    void* const holder = Virtual<void*>(occupant, 0x18);
    void* const property = Virtual<void*>(holder, 0x24, 0x27812854u);
    if (!property)
        return 0;
    consumed = Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x120);
    return 1;
}

// 34 (0x006EB110): property 0x27812852.
uint32_t __fastcall GetOccupantMaxPowerProduced(void*, void*, void* occupant, uint32_t& produced)
{
    return GetPropertyUint32(Virtual<void*>(occupant, 0x18), 0x27812852, &produced);
}

// 37, 38 (0x006EB070, 0x006EB0A0): the cell's bit in the powered and the
// wired bit arrays.
uint32_t __fastcall IsPowered(void* self, void*, int32_t x, int32_t z)
{
    return CellBit(self, field::kPoweredBits, x, z);
}

uint32_t __fastcall IsWired(void* self, void*, int32_t x, int32_t z)
{
    return CellBit(self, field::kWiredBits, x, z);
}

// 40 (0x006EB130): the chance of a strike is the default percentage, 0.
void __fastcall GetChanceOfStrike(void*, void*, uint32_t& chance)
{
    chance = Global<uint32_t>(addr::PowerTuning_DefaultPercentage);
}

// 44 (0x006EB150): days since the strike began (the city's date, slot 50,
// then slot 29), or -1 without one.
int32_t __fastcall GetStrikeLength(void* self, void*)
{
    const int32_t start = At<int32_t>(self, field::kStrikeStart);
    if (start == 0)
        return -1;
    void* const clock = Virtual<void*>(CityHelper(self), 0xC8);
    return Virtual<int32_t>(clock, 0x74) - start;
}

// 47, 48 (0x006EFA60, 0x006EB180)
void* __fastcall GetStrikeEfficiencyPercent(void* self, void*)
{
    return static_cast<char*>(self) + field::kStrikeEfficiency;
}

void* __fastcall GetFundingPercent(void* self, void*)
{
    return Virtual<void*>(At<void*>(self, field::kDepartment), 0x28);
}

// --- Buildings and their budget items ----------------------------------------

// cISC4BudgetSimulator::BudgetItem, as in the pollution simulator.
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

constexpr uint32_t kPurposePowerGeneration = 0xCA58E540;

// The building's power generation budget item (budget slot 51); the game
// zeroes only the local funding before the call.
bool PowerBudgetItem(void* occupant, BudgetItem& item)
{
    item.localFundingPercentage = 0.0f;
    void* const holder = Virtual<void*>(occupant, 0x18);
    return Virtual<uint8_t>(Global<void*>(addr::SC4_BudgetSimulator), 0xCC, holder, kPurposePowerGeneration, &item) != 0;
}

// 31 (0x006EB3D0): through the building's lot (lot manager slot 14): powered
// if the lot's slot 66 says so, or unless its slot 65 says otherwise.
uint32_t __fastcall IsOccupantPowered(void*, void*, void* occupant)
{
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (!lot)
        return 0;
    if (Virtual<uint8_t>(lot, 0x108))
        return 1;
    return Virtual<uint8_t>(lot, 0x104) ? 0 : 1;
}

// 33 (0x006EB420): for a plant (property 0x27812852), what it produces
// (0x006EB1C0) and what of it is used (property 0x08D9763D, variant slot 74).
uint32_t __fastcall GetOccupantPowerProduced(void* self, void*, void* occupant, uint32_t& produced, uint32_t& used)
{
    using ProducedFn = uint32_t(__thiscall*)(void* self, void* occupant);
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (!Virtual<void*>(holder, 0x24, 0x27812852u))
        return 0;
    produced = reinterpret_cast<ProducedFn>(addr::cSC4PowerSimulator_PlantOutput)(self, occupant);
    used = 0;
    if (void* const property = Virtual<void*>(holder, 0x24, 0x08D9763Du))
        used = Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x128);
    return 1;
}

// 49, 50 (0x006EB2F0, 0x006EB520): the plant's budget item's cost and
// local funding.
uint32_t __fastcall GetMonthlyPowerGenerationCost(void*, void*, void* occupant, int64_t& cost)
{
    BudgetItem item;
    if (!PowerBudgetItem(occupant, item))
        return 0;
    cost = item.cost;
    return 1;
}

uint32_t __fastcall GetPowerPlantFundingPercentage(void*, void*, void* occupant, float& funding)
{
    BudgetItem item;
    if (!PowerBudgetItem(occupant, item))
        return 0;
    funding = item.localFundingPercentage;
    return 1;
}

// 51 (0x006EB580): through the department (budget slot 28) of the plant's
// budget item, its slot 24.
uint32_t __fastcall SetPowerPlantFundingPercentage(void*, void*, void* occupant, const float& funding)
{
    BudgetItem item;
    item.localFundingPercentage = 0.0f;
    void* const holder = Virtual<void*>(occupant, 0x18);
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    if (!Virtual<uint8_t>(budget, 0xCC, holder, kPurposePowerGeneration, &item))
        return 0;
    void* const department = Virtual<void*>(budget, 0x70, item.departmentId);
    if (!department)
        return 0;
    Virtual<uint32_t>(department, 0x60, holder, &funding, kPurposePowerGeneration);
    return 1;
}

// --- Plants: efficiency, condition, output, the list --------------------------

struct Curve {
    void* points = nullptr;
    void* end = nullptr;
    void* capacity = nullptr;
    int32_t count = 0;
};

using LoadCurveFromPropertyFn = uint8_t(__thiscall*)(Curve* curve, void* property);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using GetPropertyFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using FreeFn = void(__cdecl*)(void* p);
const auto LoadCurveFromProperty = reinterpret_cast<LoadCurveFromPropertyFn>(addr::SC4Curve_LoadFromProperty);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto GetPropertyFloat = reinterpret_cast<GetPropertyFloatFn>(addr::SC4_GetPropertyFloat);
using SetPropertyFloatFn = void(__cdecl*)(void* holder, uint32_t property, float value);
const auto SetPropertyFloat = reinterpret_cast<SetPropertyFloatFn>(addr::SC4_SetPropertyFloat);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr std::size_t kFundingCurve = 0x378; // SC4Curve: efficiency by local funding

// 46 (0x006EC820): the plant's condition curve (property 0x8A1E07EE) at its
// age (0xC910B4CF), or the default (0x00B4A5B0). An SC4Percentage returned
// through a hidden pointer.
float* __fastcall GetCurrentPlantConditionPercent(void*, void*, float* condition, void* occupant)
{
    float value = Global<float>(addr::PowerTuning_DefaultPercentage);
    if (occupant) {
        void* const holder = Virtual<void*>(occupant, 0x18);
        if (void* const property = Virtual<void*>(holder, 0x24, 0x8A1E07EEu)) {
            Curve curve;
            LoadCurveFromProperty(&curve, property);
            float age = 0.0f;
            GetPropertyFloat(holder, 0xC910B4CF, &age);
            value = static_cast<float>(EvaluateCurve(&curve, age));
            if (curve.points)
                Free(curve.points);
        }
    }
    *condition = value;
    return condition;
}

// 45 (0x006EB5F0): the efficiency curve (+0x378) at the plant's local
// funding in percent (its budget item), / 100, times its condition.
float* __fastcall GetCurrentPlantEfficiencyPercent(void* self, void*, float* efficiency, void* occupant)
{
    float value = Global<float>(addr::PowerTuning_DefaultPercentage);
    if (!occupant) {
        *efficiency = value;
        return efficiency;
    }
    BudgetItem item;
    if (PowerBudgetItem(occupant, item)) {
        const float x = item.localFundingPercentage * 100.0f;
        value = static_cast<float>(EvaluateCurve(static_cast<char*>(self) + kFundingCurve, x) * static_cast<double>(0.01f));
    }
    float condition;
    const float* const conditionPercent = Virtual<float*>(self, 0xB8, &condition, occupant);
    *efficiency = static_cast<float>(static_cast<double>(value) * *conditionPercent);
    return efficiency;
}

// 0x006EB1C0: the plant's capacity (property 0x27812852) times its
// efficiency (slot 45), truncated; 0 without a plant, the property's value
// (0) without the property.
uint32_t __fastcall PlantOutput(void* self, void*, void* occupant)
{
    uint32_t capacity = 0;
    if (!occupant)
        return 0;
    if (!static_cast<uint8_t>(GetPropertyUint32(Virtual<void*>(occupant, 0x18), 0x27812852, &capacity)))
        return capacity;
    float efficiency;
    const float* const percent = Virtual<float*>(self, 0xB4, &efficiency, occupant);
    return static_cast<uint32_t>(Ftol(static_cast<double>(capacity) * *percent));
}

// 25 (0x006ED120): the plants (the list at +0x298, objects at node +0xC)
// appended to the caller's list, a reference each.
void __fastcall GetPowerPlants(void* self, void*, void* list)
{
    void* const sentinel = At<void*>(self, 0x298);
    for (void* node = *static_cast<void**>(sentinel); node != sentinel; node = *static_cast<void**>(node)) {
        void* const plant = static_cast<void**>(node)[3];
        Virtual<uint32_t>(plant, 0x4); // AddRef
        void** const end = *static_cast<void***>(list);
        auto** const added = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
        added[2] = plant;
        void** const before = static_cast<void**>(end[1]);
        added[0] = end;
        added[1] = before;
        before[0] = added;
        end[1] = added;
    }
}

// 30 (0x006EBA30): of the plants, the one with the lowest local funding
// (below 2.0, with a line item in the department, +0x20 slot 28) and its
// funding; and the one furthest through its life (age over the larger of
// properties 0xEA1CF221 and 0xEA1CF222, default 99) and that ratio in
// percent. Each comes with a reference; nothing is written without plants.
void __fastcall FindPowerPlants(void* self, void*, void** lowestFunded, uint32_t& funding, void** oldest,
                                int32_t& agePercent)
{
    void* const sentinel = At<void*>(self, 0x298);
    void* node = *static_cast<void**>(sentinel);
    if (node == sentinel)
        return;
    void* funded = nullptr;
    void* aged = nullptr;
    float lowestFunding = 2.0f;
    float highestAge = 0.0f;
    for (; node != sentinel; node = *static_cast<void**>(node)) {
        void* const plant = static_cast<void**>(node)[3];
        void* const holder = Virtual<void*>(plant, 0x18);
        BudgetItem item;
        item.localFundingPercentage = 0.0f;
        if (Virtual<uint8_t>(At<void*>(self, 0x1C), 0xCC, holder, kPurposePowerGeneration, &item) &&
            Virtual<uint8_t>(At<void*>(self, field::kDepartment), 0x70, holder, kPurposePowerGeneration) &&
            item.localFundingPercentage < lowestFunding) {
            if (plant != funded) {
                Virtual<uint32_t>(plant, 0x4);
                if (funded)
                    Virtual<uint32_t>(funded, 0x8);
                funded = plant;
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
            if (plant != aged) {
                Virtual<uint32_t>(plant, 0x4);
                if (aged)
                    Virtual<uint32_t>(aged, 0x8);
                aged = plant;
            }
            highestAge = ratio;
        }
    }
    if (aged && oldest) {
        *oldest = aged;
        Virtual<uint32_t>(aged, 0x4);
        agePercent = Ftol(static_cast<double>(highestAge) * 100.0f);
    }
    if (funded && lowestFunded) {
        *lowestFunded = funded;
        Virtual<uint32_t>(funded, 0x4);
        std::memcpy(&funding, &lowestFunding, sizeof funding);
    }
    if (aged)
        Virtual<uint32_t>(aged, 0x8);
    if (funded)
        Virtual<uint32_t>(funded, 0x8);
}

// With OPENSC4_POWER_SELFCHECK set: the game's FindPowerPlants, then ours,
// the outputs compared and the game's extra references released.
void __fastcall CheckFindPowerPlants(void* self, void* edx, void** lowestFunded, uint32_t& funding, void** oldest,
                                     int32_t& agePercent)
{
    using Fn = void(__fastcall*)(void*, void*, void**, uint32_t&, void**, int32_t&);
    void* theirFunded = nullptr;
    void* theirOldest = nullptr;
    uint32_t theirFunding = 0;
    int32_t theirAge = 0;
    reinterpret_cast<Fn>(0x006EBA30)(self, edx, &theirFunded, theirFunding, &theirOldest, theirAge);
    FindPowerPlants(self, edx, lowestFunded, funding, oldest, agePercent);
    const bool same = (!lowestFunded || *lowestFunded == theirFunded) && (!oldest || *oldest == theirOldest) &&
                      (!lowestFunded || !theirFunded || funding == theirFunding) && (!oldest || !theirOldest || agePercent == theirAge);
    log::Write("power interface: FindPowerPlants check: %s (funded %p %08X, oldest %p %d%%; the game's %p %08X, %p %d%%)",
               same ? "same" : "DIFFERENT", lowestFunded ? *lowestFunded : nullptr, funding, oldest ? *oldest : nullptr,
               agePercent, theirFunded, theirFunding, theirOldest, theirAge);
    if (theirFunded)
        Virtual<uint32_t>(theirFunded, 0x8);
    if (theirOldest)
        Virtual<uint32_t>(theirOldest, 0x8);
}

// --- Helpers the handlers call ------------------------------------------------

// Logs the first call of each helper, to show which ones a run reaches.
void FirstCall(const char* name, bool& logged)
{
    if (!logged) {
        logged = true;
        log::Write("power: first %s", name);
    }
}

constexpr std::size_t kRectMargin = 0x2B0;   // cells around a change that are recomputed
constexpr std::size_t kDirty = 0x338;        // bool
constexpr std::size_t kHasDirtyRect = 0x33A; // bool
constexpr std::size_t kDirtyRect = 0x33C;    // int32 x0, z0, x1, z1
constexpr std::size_t kDemandTotal = 0x78;   // int64 (the high half only cleared)
constexpr std::size_t kMonth = 0x38;
constexpr std::size_t kUsageByMonth = 0x90;  // int32[12][10]: [month][power type]

using SetPropertyFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using GetPropertySint64Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int64_t* value);
using GetPropertySint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
const auto SetPropertySint32 = reinterpret_cast<SetPropertyFn>(addr::SC4_SetPropertySint32);
const auto GetPropertySint64 = reinterpret_cast<GetPropertySint64Fn>(addr::SC4_GetPropertySint64);
const auto GetPropertySint32 = reinterpret_cast<GetPropertySint32Fn>(addr::SC4_GetPropertySint32);

uint32_t CityCellsX(void* self) { return Virtual<uint32_t>(CityHelper(self), 0x174); }
uint32_t CityCellsZ(void* self) { return Virtual<uint32_t>(CityHelper(self), 0x178); }

// 0x006EAD70: the rectangle around (x, z), clamped (unsigned, as the game).
void __fastcall CellRectAround(void* self, void*, uint32_t x, uint32_t z, uint32_t* rect)
{
    static bool logged = false;
    FirstCall("CellRectAround", logged);
    const uint32_t margin = At<uint32_t>(self, kRectMargin);
    rect[0] = x > margin ? x - margin : 0;
    rect[1] = z > margin ? z - margin : 0;
    const uint32_t x1 = x + At<uint32_t>(self, kRectMargin);
    rect[2] = x1 < CityCellsX(self) ? x1 : CityCellsX(self) - 1;
    const uint32_t z1 = At<uint32_t>(self, kRectMargin) + z;
    rect[3] = z1 < CityCellsZ(self) ? z1 : CityCellsZ(self) - 1;
}

// 0x006EAE20: the same, in place.
void __fastcall GrowRect(void* self, void*, uint32_t* rect)
{
    static bool logged = false;
    FirstCall("GrowRect", logged);
    const uint32_t margin = At<uint32_t>(self, kRectMargin);
    rect[0] = rect[0] > margin ? rect[0] - margin : 0;
    rect[1] = rect[1] > At<uint32_t>(self, kRectMargin) ? rect[1] - At<uint32_t>(self, kRectMargin) : 0;
    const uint32_t x1 = At<uint32_t>(self, kRectMargin) + rect[2];
    rect[2] = x1 < CityCellsX(self) ? x1 : CityCellsX(self) - 1;
    const uint32_t z1 = At<uint32_t>(self, kRectMargin) + rect[3];
    rect[3] = z1 < CityCellsZ(self) ? z1 : CityCellsZ(self) - 1;
}

// 0x006EB230: the dirty area grows to include the rectangle (the union of
// 0x004665C0: the smaller low corner, the larger high corner).
void __fastcall AddDirtyRect(void* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("AddDirtyRect", logged);
    auto* const dirty = &At<int32_t>(self, kDirtyRect);
    if (At<uint8_t>(self, kHasDirtyRect)) {
        dirty[0] = dirty[0] < rect[0] ? dirty[0] : rect[0];
        dirty[1] = dirty[1] < rect[1] ? dirty[1] : rect[1];
        dirty[2] = dirty[2] > rect[2] ? dirty[2] : rect[2];
        dirty[3] = dirty[3] > rect[3] ? dirty[3] : rect[3];
        return;
    }
    At<uint8_t>(self, kHasDirtyRect) = 1;
    At<uint8_t>(self, kDirty) = 1;
    for (int i = 0; i < 4; ++i)
        dirty[i] = rect[i];
}

// 0x006EB490: the total of property 0x09132DE8 over the objects of the
// vector at +0x29C (each object's slot 3, then its property holder).
uint8_t __fastcall UpdateDemandTotal(void* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateDemandTotal", logged);
    auto* const* const begin = At<void** const*>(self, 0x29C);
    auto* const* const end = At<void** const*>(self, 0x2A0);
    uint32_t total = 0;
    for (auto* const* item = begin; item != end; ++item) {
        void* const object = Virtual<void*>(*item, 0xC);
        int64_t value = 0;
        GetPropertySint64(Virtual<void*>(object, 0x18), 0x09132DE8, &value);
        total += static_cast<uint32_t>(value);
    }
    const bool same = At<uint32_t>(self, kDemandTotal) == total && At<uint32_t>(self, kDemandTotal + 4) == 0;
    At<uint32_t>(self, kDemandTotal) = total;
    At<uint32_t>(self, kDemandTotal + 4) = 0;
    return same ? 0 : 1;
}

// 0x006EBC10: for each plant node in state 1 (+0x8), its output times
// used / produced becomes its used power (property 0x08D9763D) and is added
// to this month's figure for its power type (property 0x27812853).
void __fastcall UpdatePlantUsage(void* self, void*, uint32_t used, uint32_t produced)
{
    static bool logged = false;
    FirstCall("UpdatePlantUsage", logged);
    float share = 0.0f;
    if (At<int32_t>(self, 0x3C) > 0)
        share = static_cast<float>(static_cast<double>(used) / static_cast<double>(produced));
    void* const sentinel = At<void*>(self, 0x298);
    for (void* node = *static_cast<void**>(sentinel); node != sentinel; node = *static_cast<void**>(node)) {
        auto* const fields = static_cast<uint32_t*>(node);
        if (fields[2] != 1)
            continue;
        void* const plant = reinterpret_cast<void*>(fields[3]);
        int32_t type = 0;
        void* const holder = Virtual<void*>(plant, 0x18);
        const auto output = static_cast<int32_t>(
            reinterpret_cast<uint32_t(__thiscall*)(void*, void*)>(addr::cSC4PowerSimulator_PlantOutput)(self, plant));
        GetPropertySint32(holder, 0x27812853, &type);
        const int32_t usedHere = Ftol(static_cast<double>(output) * share);
        SetPropertySint32(holder, 0x08D9763D, static_cast<uint32_t>(usedHere));
        At<int32_t>(self, kUsageByMonth + 4 * (type + At<int32_t>(self, kMonth) * 10)) += usedHere;
        fields[2] = 2;
    }
}

// The bit arrays and the network's cells.
using BitRectFn = void(__cdecl*)(uint32_t* bits, uint32_t width, const int32_t* rect);
using BitClearFn = void(__cdecl*)(uint32_t* bits, uint32_t width);
using AllocateFn = void*(__cdecl*)(std::size_t size);
const auto SetBitRect = reinterpret_cast<BitRectFn>(addr::BitGrid_SetRect);
const auto ClearBitRect = reinterpret_cast<BitRectFn>(addr::BitGrid_ClearRect);
const auto ClearBits = reinterpret_cast<BitClearFn>(addr::BitGrid_Clear);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);

// 0x006EAFD0, 0x006EB020: a rectangle of the wired bits set or cleared.
void __fastcall FillWiredRect(void* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("FillWiredRect", logged);
    const int32_t copy[4] = {rect[0], rect[1], rect[2], rect[3]};
    SetBitRect(At<uint32_t*>(self, field::kWiredBits), CityCellsX(self), copy);
}

void __fastcall ClearWiredRect(void* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("ClearWiredRect", logged);
    const int32_t copy[4] = {rect[0], rect[1], rect[2], rect[3]};
    ClearBitRect(At<uint32_t*>(self, field::kWiredBits), CityCellsX(self), copy);
}

// 0x006EB280 (stdcall): the object's cell (its slot 6), clamped to the city.
void __stdcall OccupantCell(void* object, int32_t* x, int32_t* z)
{
    static bool logged = false;
    FirstCall("OccupantCell", logged);
    if (!object)
        return;
    void* const city = Global<void*>(addr::SC4_City);
    const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
    const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
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

// 0x006EB350: a network change at a cell rectangle.
uint8_t __fastcall OnNetworkCellChanged(void* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    static bool logged = false;
    FirstCall("OnNetworkCellChanged", logged);
    using RectFn = void(__thiscall*)(void* self, int32_t* rect);
    int32_t rect[4] = {x0, z0, x1, z1};
    reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_GrowRect)(self, rect);
    if (Virtual<uint8_t>(At<void*>(self, 0x2C), 0x30, x0, z0))
        reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_FillWiredRect)(self, rect);
    reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_AddDirtyRect)(self, rect);
    return 1;
}

// 0x006EAED0: the four bit arrays, cells / 32 words each, cleared.
void __fastcall CreateBitArrays(void* self, void*)
{
    static bool logged = false;
    FirstCall("CreateBitArrays", logged);
    const int32_t cellsZ = Virtual<int32_t>(CityHelper(self), 0x178);
    const int32_t cellsX = Virtual<int32_t>(CityHelper(self), 0x174);
    const int32_t words = cellsX * cellsZ / 32;
    At<int32_t>(self, 0x2CC) = words;
    for (const std::size_t array : {0x2C4, 0x2C8, 0x2D4, 0x2D0}) {
        At<void*>(self, array) = OperatorNew(static_cast<std::size_t>(At<int32_t>(self, 0x2CC)) * 4);
        ClearBits(At<uint32_t*>(self, array), CityCellsX(self));
    }
}

// 0x006EBD00: the cells whose state changed, published by 8x8 block.
void __fastcall PublishChanges(void* self, void*)
{
    static bool logged = false;
    FirstCall("PublishChanges", logged);
    using ChangesFn = void(__cdecl*)(uint32_t* changes, const uint32_t* now, uint32_t width);
    using AnyFn = uint8_t(__cdecl*)(const uint32_t* bits, uint32_t width, const int32_t* rect);
    using BitFn = uint32_t(__cdecl*)(const uint32_t* bits, uint32_t width, int32_t x, int32_t z);
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const messages = Global<void*>(addr::SC4_MessageServer);
    const uint32_t width = CityCellsX(self);
    reinterpret_cast<ChangesFn>(addr::BitGrid_Changes)(At<uint32_t*>(self, 0x2D0), At<uint32_t*>(self, 0x2D4),
                                                     CityCellsX(self));
    const auto changes = [self] { return At<const uint32_t*>(self, 0x2D0); };
    const auto bit = reinterpret_cast<BitFn>(0x00603DD0);
    int32_t lastBlock = static_cast<int32_t>((width >> 3) - 1);
    if (lastBlock < 0)
        lastBlock = 0;
    for (int32_t blockZ = 0; blockZ <= lastBlock; ++blockZ) {
        for (int32_t blockX = 0; blockX <= lastBlock; ++blockX) {
            const int32_t rect[4] = {blockX * 8, blockZ * 8, blockX * 8 + 7, blockZ * 8 + 7};
            if (!reinterpret_cast<AnyFn>(addr::BitGrid_AnyInRect)(changes(), width, rect))
                continue;
            for (int32_t x = rect[0]; x <= rect[2]; ++x) {
                for (int32_t z = rect[1]; z <= rect[3]; ++z) {
                    if (!static_cast<uint8_t>(bit(changes(), width, x, z)))
                        continue;
                    const uint8_t wired = static_cast<uint8_t>(bit(At<uint32_t*>(self, field::kWiredBits), width, x, z));
                    const uint8_t powered = static_cast<uint8_t>(bit(At<uint32_t*>(self, field::kPoweredBits), width, x, z));
                    const uint32_t value = wired ? (powered ? 2u : 1u) : 0u;
                    Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0x68, x, z, value);
                }
            }
            void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
            void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
            Virtual<uint32_t>(message, 0x4);
            Virtual<uint32_t>(message, 0x14, 0x8A07AE00u);
            Virtual<uint32_t>(message, 0x2C, rect[0]);
            Virtual<uint32_t>(message, 0x40, rect[1]);
            Virtual<uint32_t>(message, 0x54, rect[2]);
            Virtual<uint32_t>(message, 0x68, rect[3]);
            Virtual<uint32_t>(messages, 0x10, message, 0u);
            Virtual<uint32_t>(message, 0x8);
            At<uint8_t>(self, 0x2D9) = 1;
        }
    }
}

// 0x006EDB40: the first of the type-4 occupants for the key (occupant
// service +0x30, slot 18) whose cell is set in +0x2C4.
uint8_t __fastcall FindConnectedOccupant(void* self, void*, void* key, uint8_t switchOn, uint8_t mark)
{
    static bool logged = false;
    FirstCall("FindConnectedOccupant", logged);
    using ClearFn = void(__thiscall*)(void** list);
    using SetBitFn = void(__cdecl*)(uint32_t* bits, uint32_t width, int32_t x, int32_t z);
    void** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 12u));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    Virtual<uint32_t>(At<void*>(self, 0x30), 0x48, 4u, key, &list);
    int32_t x = 0;
    auto z = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(key)); // the game keeps z in the key's slot
    bool found = false;
    void** node = static_cast<void**>(*static_cast<void**>(list));
    for (; node != list; node = static_cast<void**>(node[0])) {
        if (void* const occupant = node[2]) {
            void* const city = Global<void*>(addr::SC4_City);
            const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
            const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
            Virtual<uint32_t>(occupant, 0x18, &x, &z);
            if (x < 0)
                x = 0;
            if (z < 0)
                z = 0;
            if (x > lastX)
                x = lastX;
            if (z > lastZ)
                z = lastZ;
        }
        const int32_t width = Virtual<int32_t>(CityHelper(self), 0x174);
        const uint32_t word = static_cast<uint32_t>(width / 32 * z) + (static_cast<uint32_t>(x) >> 5);
        if (At<uint32_t*>(self, 0x2C4)[word] & (1u << (x & 31))) {
            found = true;
            break;
        }
    }
    if (found) {
        if (switchOn)
            Virtual<uint32_t>(node[2], 0x30, 1u);
        if (mark)
            reinterpret_cast<SetBitFn>(addr::BitGrid_Set)(At<uint32_t*>(self, 0x2D0), CityCellsX(self), x, z);
    }
    reinterpret_cast<ClearFn>(addr::SC4List_ClearReleasing)(&list);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, list);
    return found ? 1 : 0;
}

// 0x006EC8F0: a plant's month of wear (as the pollution simulator's
// AgeBuildings), and what happens at the end of its life.
void __fastcall AgePlant(void* self, void*, void* plant)
{
    static bool logged = false;
    FirstCall("AgePlant", logged);
    using PostMessageFn = void(__cdecl*)(uint32_t message, uint32_t a, uint32_t b, uint32_t c, uint32_t d, void* e);
    using RefListFn = void(__thiscall*)(void** list, void* const* object);
    using RefClearFn = void(__thiscall*)(void** list);
    using AllocateFn2 = void*(__cdecl*)(std::size_t size);
    float age = 0.0f;
    int32_t capacity = 0;
    int32_t used = 0;
    float use = 0.0f;
    float end4 = 99.0f;
    uint32_t blowUpType = 0;
    float end2 = 99.0f;
    float rate = 0.0f;
    const float* const funding = Virtual<float*>(At<void*>(self, field::kDepartment), 0x28);
    void* const holder = Virtual<void*>(plant, 0x18);
    GetPropertyFloat(holder, 0xC910B4CF, &age);
    GetPropertyFloat(holder, 0xEA1CF220, &rate);
    GetPropertySint32(holder, 0x27812852, &capacity);
    GetPropertySint32(holder, 0x08D9763D, &used);
    GetPropertyFloat(holder, 0xEA1CF221, &end2);
    GetPropertyFloat(holder, 0xEA1CF222, &end4);
    GetPropertyUint32(holder, 0x6A43150F, &blowUpType);
    float efficiency;
    const float* const percent = Virtual<float*>(self, 0xB4, &efficiency, plant);
    capacity = Ftol(static_cast<double>(capacity) * *percent);
    if (capacity > 0 && used > 0)
        use = static_cast<float>(static_cast<double>(used) / capacity);
    const float fundingNow = *funding;
    const float byUse = static_cast<float>(EvaluateCurve(static_cast<char*>(self) + 0x3A8, use));
    age = static_cast<float>(EvaluateCurve(static_cast<char*>(self) + 0x398, fundingNow) * byUse * rate + age);
    SetPropertyFloat(holder, 0xC910B4CF, age);
    if (use > 1.0f)
        ++At<int32_t>(self, field::kOverworkedPlants);
    if (age >= 0.8)
        ++At<int32_t>(self, field::kAgedPlants);
    if (!(age >= end4) && !(age >= end2))
        return;

    float position[3];
    Virtual<uint32_t>(plant, 0x20, position);
    int32_t cellX, cellZ;
    Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, 0u);
    if (!lot)
        return;
    if (age >= end4 && blowUpType != 0) {
        // The plant blows up: its lot is demolished, radiation spreads.
        void* list = nullptr;
        auto** const sentinel = static_cast<void**>(reinterpret_cast<AllocateFn2>(addr::SC4_stl_allocate)(0xC));
        sentinel[0] = sentinel;
        sentinel[1] = sentinel;
        list = sentinel;
        void* lotRef = lot;
        Virtual<uint32_t>(lot, 0x4);
        reinterpret_cast<RefListFn>(addr::SC4RefList_PushBack)(&list, &lotRef);
        Virtual<uint32_t>(lot, 0x8);
        Virtual<uint32_t>(At<void*>(self, 0x34), 0x1C, 1u, &list, 0u, blowUpType | 0x300, 0u, 0u, 0u, 0u);
        float radius = 0.0f;
        float amount = 0.0f;
        GetPropertyFloat(holder, 0xAA5C04C8, &radius);
        GetPropertyFloat(holder, 0xAA5C04C5, &amount);
        if (radius > 0.0f)
            Virtual<uint32_t>(Global<void*>(addr::SC4_PollutionSimulator), 0x108, position, amount, radius);
        uint32_t powerType = 0;
        if (static_cast<uint8_t>(GetPropertyUint32(holder, 0x27812853, &powerType)) && powerType == 5)
            reinterpret_cast<PostMessageFn>(addr::SC4_PostMessage)(0xAA552F77, 0, 0, 0, 0, plant);
        reinterpret_cast<RefClearFn>(addr::SC4RefList_Clear)(&list);
        if (list)
            Free(list);
        return;
    }
    if (age >= end2) {
        ++At<int32_t>(self, field::kDistressedPlants);
        Virtual<uint8_t>(lot, 0x90, 2u); // cISC4Lot::SetCondition
    }
}

// 0x006ED6E0: a changed cell rectangle recomputed: the wired bits around
// the network occupants near it (but `removed`) and around every cell the
// network service (+0x2C, slot 12) says is wired; then the supply grid.
void __fastcall RecomputeArea(void* self, void*, const uint32_t* rect, void* removed)
{
    static bool logged = false;
    FirstCall("RecomputeArea", logged);
    using RectFn = void(__thiscall*)(void* self, int32_t* rect);
    using AroundFn = void(__thiscall*)(void* self, uint32_t x, uint32_t z, int32_t* rect);
    using ClearFn = void(__thiscall*)(void** list);
    using BitFn = uint32_t(__cdecl*)(const uint32_t* bits, uint32_t width, uint32_t x, uint32_t z);
    const auto grow = reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_GrowRect);
    const auto fill = reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_FillWiredRect);
    auto** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    const uint32_t margin2 = At<uint32_t>(self, kRectMargin) * 2;
    const uint32_t low[2] = {rect[0] - margin2, rect[1] - margin2};
    const uint32_t high[2] = {rect[2] + margin2, rect[3] + margin2};
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0xA0, &list, low, high, 0u, 0xFFFFFFFFu)) {
        void* const end = list;
        for (void** node = static_cast<void**>(*static_cast<void**>(end)); node != end;
             node = static_cast<void**>(node[0])) {
            void* const occupant = node[2];
            if (occupant != removed) {
                // The game asks the type until it matches, then once more.
                bool network = Virtual<uint32_t>(occupant, 0x1C) == 0x278128A0;
                network = network || Virtual<uint32_t>(occupant, 0x1C) == 0x2890D4DE;
                network = network || Virtual<uint32_t>(occupant, 0x1C) == 0x0990B2EA;
                network = network || Virtual<uint32_t>(occupant, 0x1C) == 0xC772BF98;
                if (network) {
                    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
                    int32_t cell[4];
                    void* part = nullptr;
                    if (type == 0x0990B2EA || type == 0x2890D4DE) {
                        const bool isA = type == 0x0990B2EA;
                        if (Virtual<uint8_t>(occupant, 0x0, isA ? 0xC98F3B24u : 0xA8C86142u, &part)) {
                            int32_t x, z;
                            Virtual<uint32_t>(part, isA ? 0x14 : 0x20, &x, &z);
                            cell[0] = x;
                            cell[1] = z;
                            cell[2] = x;
                            cell[3] = z;
                            grow(self, cell);
                            fill(self, cell);
                            Virtual<uint32_t>(part, 0x8);
                        }
                    } else if (type == 0xC772BF98) {
                        if (Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &part)) {
                            if (Virtual<uint32_t>(part, 0x30) & 0x40000) {
                                int32_t x, z;
                                Virtual<uint32_t>(part, 0xEC, &x, &z);
                                cell[0] = x;
                                cell[1] = z;
                                cell[2] = x;
                                cell[3] = z;
                                fill(self, cell);
                            }
                            Virtual<uint32_t>(part, 0x8);
                        }
                    } else {
                        Virtual<uint32_t>(occupant, 0x2C, cell);
                        grow(self, cell);
                        fill(self, cell);
                    }
                }
            }
            Virtual<uint32_t>(occupant, 0x8); // the list's reference
        }
    }
    const uint32_t width = CityCellsX(self);
    for (uint32_t x = rect[0]; x <= rect[2]; ++x) {
        for (uint32_t z = rect[1]; z <= rect[3]; ++z) {
            if (!Virtual<uint8_t>(At<void*>(self, 0x2C), 0x30, x, z))
                continue;
            int32_t around[4];
            reinterpret_cast<AroundFn>(addr::cSC4PowerSimulator_CellRectAround)(self, x, z, around);
            const int32_t copy[4] = {around[0], around[1], around[2], around[3]};
            SetBitRect(At<uint32_t*>(self, field::kWiredBits), CityCellsX(self), copy);
        }
    }
    const auto bit = reinterpret_cast<BitFn>(0x00603DD0);
    for (uint32_t x = rect[0]; x <= rect[2]; ++x) {
        for (uint32_t z = rect[1]; z <= rect[3]; ++z) {
            const uint8_t wired = static_cast<uint8_t>(bit(At<uint32_t*>(self, field::kWiredBits), width, x, z));
            const uint8_t powered = static_cast<uint8_t>(bit(At<uint32_t*>(self, field::kPoweredBits), width, x, z));
            Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0x68, x, z, wired ? (powered ? 2u : 1u) : 0u);
        }
    }
    reinterpret_cast<ClearFn>(addr::SC4List_Clear2)(&list);
    Virtual<uint32_t>(ListNodeAllocator(), 0x10, list);
}

// 0x006EE110: power from the plant at cell (x, z) through the network.
void __fastcall DistributePower(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("DistributePower", logged);
    using FloodFn = void(__cdecl*)(uint32_t* bits, const uint32_t* through, uint32_t width, uint32_t limit,
                                   int32_t* minZ, int32_t* maxZ);
    using AnyFn = uint8_t(__cdecl*)(const uint32_t* bits, uint32_t width, const int32_t* rect);
    using OrRectFn = void(__cdecl*)(uint32_t* bits, const uint32_t* from, uint32_t width, const int32_t* rect);
    using OutputFn = uint32_t(__thiscall*)(void* self, void* plant);
    using FindFn = uint8_t(__thiscall*)(void* self, void* key, uint8_t switchOn, uint8_t mark);
    using UsageFn = void(__thiscall*)(void* self, uint32_t used, uint32_t produced);
    using RestartFn = void(__thiscall*)(void* iterator, uint32_t flag);
    using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
    using ClearFn = void(__thiscall*)(void** list);
    const auto any = reinterpret_cast<AnyFn>(addr::BitGrid_AnyInRect);
    const auto find = reinterpret_cast<FindFn>(addr::cSC4PowerSimulator_FindConnectedOccupant);
    uint32_t*& reached = At<uint32_t*>(self, 0x2C4);

    // The cells reached from the plant's cell.
    ClearBits(reached, CityCellsX(self));
    {
        const int32_t width = Virtual<int32_t>(CityHelper(self), 0x174);
        const uint32_t word = static_cast<uint32_t>(width / 32 * z) + (static_cast<uint32_t>(x) >> 5);
        reached[word] |= 1u << (x & 31);
    }
    int32_t minZ = z;
    int32_t maxZ = z + 1;
    reinterpret_cast<FloodFn>(addr::BitGrid_Flood)(reached, At<uint32_t*>(self, field::kWiredBits), CityCellsX(self),
                                                   600, &minZ, &maxZ);
    if (minZ > maxZ) {
        minZ = z;
        maxZ = z + 1;
    }
    if (maxZ >= static_cast<int32_t>(CityCellsZ(self)))
        maxZ = static_cast<int32_t>(CityCellsZ(self)) - 1;

    // The plants reached for the first time (node state 0 to 1).
    uint32_t produced = 0;
    void* const plants = At<void*>(self, 0x298);
    for (void* node = *static_cast<void**>(plants); node != plants; node = *static_cast<void**>(node)) {
        auto* const fields = static_cast<uint32_t*>(node);
        if (fields[2] != 0)
            continue;
        void* const plant = reinterpret_cast<void*>(fields[3]);
        float low[3], high[3];
        Virtual<uint32_t>(plant, 0x28, low, high);
        int32_t rect[4];
        Virtual<uint32_t>(self, 0x14, low[0], low[2], &rect[0], &rect[1]);  // PositionToCell
        Virtual<uint32_t>(self, 0x14, high[0], high[2], &rect[2], &rect[3]);
        const int32_t copy[4] = {rect[0], rect[1], rect[2], rect[3]};
        if (!any(reached, CityCellsX(self), copy))
            continue;
        produced += reinterpret_cast<OutputFn>(addr::cSC4PowerSimulator_PlantOutput)(self, plant);
        fields[2] = 1;
    }
    At<uint32_t>(self, 0x3C) += produced;

    // The consumers of type 0x4773FFE0 on the network: what they still need.
    auto newList = [] {
        auto** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0xC, 12u));
        sentinel[0] = sentinel;
        sentinel[1] = sentinel;
        return static_cast<void*>(sentinel);
    };
    uint32_t needed = 0;
    void* first = newList();
    Virtual<uint32_t>(At<void*>(self, 0x30), 0x78, 0x4773FFE0u, &first);
    for (void* node = *static_cast<void**>(first); node != first; node = *static_cast<void**>(node)) {
        void* const consumer = static_cast<void**>(node)[2];
        void* const key = Virtual<void*>(consumer, 0x1C);
        const int32_t has = Virtual<int32_t>(consumer, 0x50);
        if (has >= Virtual<int32_t>(consumer, 0x28))
            continue;
        if (!find(self, key, 1, 0))
            continue;
        const int32_t hasNow = Virtual<int32_t>(consumer, 0x50);
        needed += static_cast<uint32_t>(Virtual<int32_t>(consumer, 0x28) - hasNow);
    }
    const auto reserve = static_cast<int32_t>(needed) - Ftol(static_cast<double>(produced) * -1.1f);
    uint32_t supply = static_cast<uint32_t>(
        Ftol(static_cast<double>(static_cast<uint32_t>(reserve)) / At<float>(self, 0x2AC)));

    // The consumers of type 0x4773FFE1, served from the supply.
    void* second = newList();
    Virtual<uint32_t>(At<void*>(self, 0x30), 0x78, 0x4773FFE1u, &second);
    for (void* node = *static_cast<void**>(second); node != second; node = *static_cast<void**>(node)) {
        void* const consumer = static_cast<void**>(node)[2];
        void* const key = Virtual<void*>(consumer, 0x1C);
        const int32_t has = Virtual<int32_t>(consumer, 0x50);
        if (has >= Virtual<int32_t>(consumer, 0x28))
            continue;
        if (!find(self, key, 0, supply > 0 ? 1 : 0))
            continue;
        const int32_t hasNow = Virtual<int32_t>(consumer, 0x50);
        const int32_t missing = Virtual<int32_t>(consumer, 0x28) - hasNow;
        const auto available = static_cast<int32_t>(supply);
        const int32_t give = missing < available ? missing : available;
        if (give > 0) {
            At<int32_t>(self, 0x48) += give;
            supply -= static_cast<uint32_t>(give);
            Virtual<uint32_t>(consumer, 0x54, Virtual<int32_t>(consumer, 0x50) + give);
        } else {
            Virtual<uint32_t>(consumer, 0x94, 0x20u, 1u);
        }
    }

    // The 8x8 blocks of the network, powered while the supply lasts.
    alignas(4) uint8_t iterator[0x30] = {};
    iterator[0] = 1;
    iterator[1] = 0;
    auto* const bounds = reinterpret_cast<int32_t*>(iterator + 4);
    const uint32_t lastBlock = (CityCellsX(self) >> 3) - 1;
    bounds[0] = 0;
    bounds[1] = static_cast<int32_t>(static_cast<uint32_t>(minZ) >> 3);
    bounds[2] = static_cast<int32_t>(lastBlock);
    bounds[3] = static_cast<int32_t>(static_cast<uint32_t>(maxZ) >> 3);
    reinterpret_cast<uint32_t*>(iterator)[11] = 1; // +0x2C
    if (bounds[2] < 0)
        bounds[2] = 0;
    if (bounds[3] < bounds[1])
        bounds[3] = bounds[1];
    iterator[2] = 1;
    reinterpret_cast<RestartFn>(addr::TractIterator_Restart)(iterator, 1);
    bool shortage = false;
    int32_t blockX, blockZ;
    const auto next = reinterpret_cast<NextFn>(addr::TractIterator_Next);
    if (next(iterator, &blockX, &blockZ)) {
        do {
            const int32_t rect[4] = {blockX * 8, blockZ * 8, blockX * 8 + 7, blockZ * 8 + 7};
            if (!any(reached, CityCellsX(self), rect))
                continue;
            uint32_t demand = 0;
            int32_t cells = 0;
            for (int32_t cx = rect[0]; cx <= rect[2]; ++cx) {
                const int16_t* const row = At<int16_t* const*>(self, field::kConsumptionMap)[3 * cx];
                for (int32_t cz = rect[1]; cz <= rect[3]; ++cz) {
                    const int32_t width = Virtual<int32_t>(CityHelper(self), 0x174);
                    const uint32_t word = static_cast<uint32_t>(width / 32 * cz) + (static_cast<uint32_t>(cx) >> 5);
                    if (!(reached[word] & (1u << (cx & 31))))
                        continue;
                    const int16_t value = row[cz];
                    if (value > 0) {
                        demand += static_cast<uint32_t>(static_cast<int32_t>(value));
                        ++cells;
                    }
                }
            }
            const bool fits = supply > 0 && demand <= supply;
            At<uint32_t>(self, 0x4C) += demand;
            if (fits) {
                reinterpret_cast<OrRectFn>(addr::BitGrid_OrRect)(At<uint32_t*>(self, 0x2D0), reached, CityCellsX(self), rect);
                At<int32_t>(self, field::kPoweredConsumers) += cells;
                supply -= demand;
            } else {
                shortage = true;
            }
        } while (next(iterator, &blockX, &blockZ));
        if (shortage)
            supply = 0;
    }

    // What is left over, and the first consumers topped up from it.
    const int32_t surplus = Ftol(static_cast<double>(supply) * At<float>(self, 0x2AC));
    At<int32_t>(self, 0x40) += reserve - surplus;
    int32_t left = reserve - surplus;
    Virtual<uint32_t>(At<void*>(self, 0x30), 0x78, 0x4773FFE0u, &first); // asked again, into the same list
    for (void* node = *static_cast<void**>(first); node != first; node = *static_cast<void**>(node)) {
        void* const consumer = static_cast<void**>(node)[2];
        void* const key = Virtual<void*>(consumer, 0x1C);
        const int32_t has = Virtual<int32_t>(consumer, 0x50);
        if (has >= Virtual<int32_t>(consumer, 0x28))
            continue;
        if (!find(self, key, 0, 0))
            continue;
        const int32_t hasNow = Virtual<int32_t>(consumer, 0x50);
        const int32_t missing = Virtual<int32_t>(consumer, 0x28) - hasNow;
        const int32_t give = missing < left ? missing : left;
        At<int32_t>(self, 0x44) += give;
        left -= give;
        Virtual<uint32_t>(consumer, 0x54, Virtual<int32_t>(consumer, 0x50) + give);
    }
    const auto producedNow = static_cast<int32_t>(produced);
    reinterpret_cast<UsageFn>(addr::cSC4PowerSimulator_UpdatePlantUsage)(
        self, static_cast<uint32_t>(left), static_cast<uint32_t>(producedNow < left ? left : producedNow));
    if (void* const buffer = reinterpret_cast<void**>(iterator)[6]) // the iterator's buffer, +0x18
        Free(buffer);
    for (void** list : {&second, &first}) {
        reinterpret_cast<ClearFn>(addr::SC4List_Clear)(list);
        Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0x10, *list);
    }
}

// --- OnMonthPassed (0x006EC570) ------------------------------------------------

constexpr std::size_t kAnnualByType = 0x270; // int32[10]: the last 12 months' usage by type

void __fastcall OnMonthPassed(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("power: first OnMonthPassed");
    }
    At<float>(self, 0x2AC) =
        static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Service43D90), 0x64, 0x0911E117u, 0u));
    int32_t& month = At<int32_t>(self, kMonth);
    if (++month >= 12)
        month = 0;
    // The month 12 months ago leaves the annual totals.
    for (int type = 0; type < 10; ++type) {
        int32_t& usage = At<int32_t>(self, kUsageByMonth + 4 * (month * 10 + type));
        At<int32_t>(self, kAnnualByType + 4 * type) -= usage;
        usage = 0;
    }
    const int32_t produced = At<int32_t>(self, 0x3C);
    const int32_t needed = At<int32_t>(self, 0x40);
    if (needed > produced && produced > 0)
        At<float>(self, 0x64) = static_cast<float>((produced - needed) / produced);
    else
        At<uint32_t>(self, 0x64) = Global<uint32_t>(addr::PowerTuning_DefaultPercentage);
    // The game adds the month's (now cleared) figures back in.
    for (int type = 0; type < 10; ++type)
        At<int32_t>(self, kAnnualByType + 4 * type) += At<int32_t>(self, kUsageByMonth + 4 * (month * 10 + type));
    At<uint8_t>(self, kDirty) = 1;
    At<uint8_t>(self, 0x339) = 1;
    using ApplyFn = void(__thiscall*)(void* self, void* funding);
    reinterpret_cast<ApplyFn>(addr::cSC4PowerSimulator_ApplyFunding)(self, Virtual<void*>(self, 0xC0));
}

// ApplyFunding (0x006EC3F0): the share of the objects at +0x29C that are on
// follows the funding (through the curve at +0x3B8); the count (+0x2BC) is
// brought to it by switching objects of a shuffled copy (the CRT's rand()).
void __fastcall ApplyFunding(void* self, void*, const float* funding)
{
    static bool logged = false;
    FirstCall("ApplyFunding", logged);
    using CopyFn = void*(__thiscall*)(void* copy, const void* vector);
    using ShuffleFn = void(__cdecl*)(void** begin, void** end);
    const float x = *funding * 100.0f;
    const double step = EvaluateCurve(static_cast<char*>(self) + 0x3B8, x);
    float& share = At<float>(self, 0x2C0);
    const double moved = static_cast<double>(share) - step;
    share = static_cast<float>(moved);
    if (moved > 1.0)
        share = 1.0f;
    else if (moved < 0.0)
        At<uint32_t>(self, 0x2C0) = 0;
    const auto wanted = static_cast<uint32_t>(Ftol(static_cast<double>(At<uint32_t>(self, 0x2B8)) * share));
    uint32_t& on = At<uint32_t>(self, 0x2BC);
    if (wanted == on)
        return;
    struct {
        void** begin;
        void** end;
        void** capacity;
    } copy;
    reinterpret_cast<CopyFn>(addr::SC4PtrVector_Copy)(&copy, static_cast<char*>(self) + 0x29C);
    reinterpret_cast<ShuffleFn>(addr::SC4_RandomShuffle)(copy.begin, copy.end);
    for (void** item = copy.begin; item != copy.end && wanted != on; ++item) {
        void* const object = *item;
        int32_t cellX, cellZ;
        Virtual<uint32_t>(object, 0x20, &cellX, &cellZ);
        const bool isOn = Virtual<uint8_t>(object, 0x2C) != 0;
        if (isOn) {
            if (on > wanted) {
                Virtual<uint32_t>(object, 0x30, 0u);
                on = on - 1;
            }
        } else if (on < wanted) {
            Virtual<uint32_t>(object, 0x30, 1u);
            on = on + 1;
        }
    }
    if (copy.begin)
        Free(copy.begin);
}

using ThisFn = void(__fastcall*)(void*, void*);
ThisFn g_originalMonth = nullptr;
using FundingFn = void(__fastcall*)(void*, void*, const float*);
FundingFn g_originalFunding = nullptr;

bool InstallMonth(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_OnMonthPassed);
    const addr::Entry* funding = addr_check::Find(addr::cSC4PowerSimulator_ApplyFunding);
    return game::IsBuild641() && entry && funding && hook::Install(*entry, &OnMonthPassed, &g_originalMonth) &&
           hook::Install(*funding, &ApplyFunding, &g_originalFunding);
}

const bool g_registeredMonth = registry::Register({
    "simulators.power.month",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_OnMonthPassed,
    "Our power MonthPassed handler: the month's usage, the shortage ratio (docs/power.md)",
    false,
    InstallMonth,
});

// --- The simulation step (0x006EE960) -----------------------------------------
//
// One state per call (+0x34C): 0 starts a month over, 1 distributes power
// from every plant and books their costs, 2 serves the consumers still short
// and publishes the changed cells, 3 publishes the month's figures and tells
// each lot whether it is powered, 4 waits for the next change (+0x338).

constexpr std::size_t kState = 0x34C;
constexpr std::size_t kPlantNodes = 0x298; // list: node +8 state, +0xC plant

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
using CellFn = void(__thiscall*)(void* self, int32_t x, int32_t z);
const auto Distribute = reinterpret_cast<CellFn>(addr::cSC4PowerSimulator_DistributePower);

void Figure(uint32_t id, int32_t value)
{
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, id, value);
}

int32_t& MonthUsage(void* self, int type)
{
    return At<int32_t>(self, kUsageByMonth + 4 * (At<int32_t>(self, kMonth) * 10 + type));
}

void StartMonth(void* self)
{
    int32_t& month = At<int32_t>(self, kMonth);
    if (++month >= 12)
        month = 0;
    for (int type = 0; type < 10; ++type) {
        At<int32_t>(self, kAnnualByType + 4 * type) -= MonthUsage(self, type);
        MonthUsage(self, type) = 0;
    }
    void* const occupants = At<void*>(self, 0x30);
    void* consumers = NewList(addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(occupants, 0x78, 0x4773FFE0u, &consumers);
    ForEachNode(consumers, [](void** node) { Virtual<uint32_t>(node[2], 0x54, 0u); });
    void* others = NewList(addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(occupants, 0x78, 0x4773FFE1u, &others);
    ForEachNode(others, [](void** node) { Virtual<uint32_t>(node[2], 0x54, 0u); });
    void* lots = NewList(addr::SC4List_NodeAllocator3);
    Virtual<uint32_t>(occupants, 0x44, 4u, &lots);
    ForEachNode(lots, [](void** node) { Virtual<uint32_t>(node[2], 0x30, 0u); });
    ListClearReleasing(&lots);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, lots);
    ListClear(&others);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0x10, others);
    ListClear(&consumers);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0x10, consumers);
    for (std::size_t offset : {0x3C, 0x40, 0x48, 0x44, 0x4C, 0x6C, 0x70})
        At<int32_t>(self, offset) = 0;
    ForEachNode(At<void*>(self, kPlantNodes), [](void** node) { node[2] = nullptr; });
    ClearBits(At<uint32_t*>(self, 0x2D0), CityCellsX(self));
    At<uint8_t>(self, 0x2D9) = 0;
}

// Power from each plant not reached yet; each plant's usage (used times the
// rate 0xC910B4CE) is totalled, its cost follows the maintenance curve at
// its age; then the plants age if a month has passed (+0x339).
void DistributeFromPlants(void* self)
{
    void* const plants = At<void*>(self, kPlantNodes);
    int64_t total = At<int64_t>(self, kDemandTotal);
    ForEachNode(plants, [self, &total](void** node) {
        void* const plant = node[3];
        if (node[2] == nullptr) {
            float position[3];
            Virtual<uint32_t>(plant, 0x20, position);
            int32_t cellX, cellZ;
            Virtual<uint32_t>(self, 0x14, position[0], position[2], &cellX, &cellZ); // PositionToCell
            Distribute(self, cellX, cellZ);
        }
        int64_t cost = 0;
        float rate = 0.0f;
        int32_t used = 0;
        float age = 0.0f;
        void* const holder = Virtual<void*>(plant, 0x18);
        GetPropertyFloat(holder, 0xC910B4CE, &rate);
        GetPropertySint64(holder, 0x2A58D350, &cost);
        GetPropertySint32(holder, 0x08D9763D, &used);
        GetPropertyFloat(holder, 0xC910B4CF, &age);
        const auto usage = static_cast<int64_t>(static_cast<double>(used) * rate);
        if (void* const property = Virtual<void*>(holder, 0x24, 0x0911E2E7u)) {
            Curve curve;
            LoadCurveFromProperty(&curve, property);
            cost = static_cast<int64_t>(static_cast<double>(cost) * EvaluateCurve(&curve, age));
            void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
            BudgetItem item;
            item.localFundingPercentage = 0.0f;
            if (Virtual<uint8_t>(budget, 0xCC, holder, kPurposePowerGeneration, &item) && item.cost != cost) {
                if (void* const department = Virtual<void*>(budget, 0x70, item.departmentId)) {
                    Virtual<uint32_t>(department, 0x64, holder, cost, kPurposePowerGeneration);
                    Virtual<uint32_t>(budget, 0xD4, holder, item.index, cost);
                }
            }
            if (curve.points)
                Free(curve.points);
        }
        total += usage;
    });
    Virtual<uint32_t>(Virtual<void*>(At<void*>(self, field::kDepartment), 0x50, 1u), 0x30, total);
    if (!At<uint8_t>(self, 0x339))
        return;
    At<int32_t>(self, field::kOverworkedPlants) = 0;
    At<int32_t>(self, field::kAgedPlants) = 0;
    At<int32_t>(self, field::kDistressedPlants) = 0;
    At<uint8_t>(self, 0x339) = 0;
    using PlantFn = void(__thiscall*)(void* self, void* plant);
    ForEachNode(plants, [self](void** node) {
        reinterpret_cast<PlantFn>(addr::cSC4PowerSimulator_AgePlant)(self, node[3]);
    });
}

// Consumers (0x4773FFE0) still short of power: their unserved lots (occupant
// service slot 15) are switched on and powered from their cell.
void ServeShortConsumers(void* self)
{
    using OccupantCellFn = void(__stdcall*)(void* object, int32_t* x, int32_t* z);
    void* consumers = NewList(addr::SC4List_NodeAllocator);
    ListClear(&consumers);
    Virtual<uint32_t>(At<void*>(self, 0x30), 0x78, 0x4773FFE0u, &consumers);
    ForEachNode(consumers, [self](void** node) {
        void* const consumer = node[2];
        void* const key = Virtual<void*>(consumer, 0x1C);
        const int32_t served = Virtual<int32_t>(consumer, 0x50);
        if (served >= Virtual<int32_t>(consumer, 0x28))
            return;
        for (void* lot = Virtual<void*>(At<void*>(self, 0x30), 0x3C, 4u, key); lot;
             lot = Virtual<void*>(At<void*>(self, 0x30), 0x3C, 4u, key)) {
            Virtual<uint32_t>(lot, 0x30, 1u);
            void* const site = Virtual<void*>(At<void*>(self, 0x30), 0x34, lot);
            if (site && Virtual<uint8_t>(site, 0x34)) {
                int32_t x, z;
                reinterpret_cast<OccupantCellFn>(addr::cSC4PowerSimulator_OccupantCell)(lot, &x, &z);
                Distribute(self, x, z);
            }
        }
    });
    ListDestroy(&consumers);
    std::swap(At<uint32_t*>(self, 0x2D0), At<uint32_t*>(self, field::kPoweredBits));
    reinterpret_cast<ThisFn>(addr::cSC4PowerSimulator_PublishChanges)(self, nullptr);
    if (!At<uint8_t>(self, 0x2D9))
        return;
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const messages = Global<void*>(addr::SC4_MessageServer);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, 0x6A07C2EEu);
    Virtual<uint32_t>(messages, 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// The month's figures (monthly figures slot 12), the last 12 months by
// power type, and every lot's powered state.
void PublishMonth(void* self)
{
    for (auto [to, from] : {std::pair{0x54, 0x40}, {0x58, 0x44}, {0x5C, 0x48}, {0x50, 0x3C}, {0x60, 0x4C}})
        At<int32_t>(self, to) = At<int32_t>(self, from);
    const double price = At<float>(self, 0x2AC);
    Figure(0xAA11F7CE, At<int32_t>(self, 0x3C));
    Figure(0x0AB2F224, At<int32_t>(self, 0x44));
    Figure(0x4AB2F236, At<int32_t>(self, 0x48));
    Figure(0xAA11F7D2, Ftol(static_cast<double>(At<int32_t>(self, 0x40) - At<int32_t>(self, 0x48)) * price));
    Figure(0xAA11F7D5, Ftol(static_cast<double>(At<int32_t>(self, 0x4C)) * price));
    Figure(0xAA11F7D8, At<int32_t>(self, 0x3C) - At<int32_t>(self, 0x40));
    void* others = NewList(addr::SC4List_NodeAllocator);
    Virtual<uint32_t>(At<void*>(self, 0x30), 0x78, 0x4773FFE1u, &others);
    ForEachNode(others, [](void** node) {
        void* const consumer = node[2];
        const int32_t served = Virtual<int32_t>(consumer, 0x50);
        if (served < Virtual<int32_t>(consumer, 0x28))
            Virtual<uint32_t>(consumer, 0x94, 0x20u, 1u);
    });
    ListDestroy(&others);
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x7C);
    const int64_t spent = Virtual<int64_t>(At<void*>(self, 0x1C), 0x8C, At<void*>(self, field::kDepartment));
    if (const int32_t needed = At<int32_t>(self, 0x40); needed > 0)
        Figure(0xAA11F7DC, static_cast<int32_t>(spent / needed));
    if (const int32_t consumers = At<int32_t>(self, 0x68); consumers > 0)
        Figure(0xAA11FB59,
               Ftol((1.0 - static_cast<double>(At<int32_t>(self, 0x6C)) / consumers) * 100.0));
    else
        Figure(0xAA11FB59, 0);
    static constexpr uint32_t kUsageFigures[9] = {0xAA120D5E, 0xAA120D61, 0xAA120D63, 0xAA120D65, 0xAA120D66,
                                                  0xAA120D68, 0xAA120D6A, 0xAA120D6B, 0xAA120D6D};
    for (int type = 1; type < 10; ++type)
        Figure(kUsageFigures[type - 1], MonthUsage(self, type));
    for (int type = 0; type < 10; ++type)
        At<int32_t>(self, kAnnualByType + 4 * type) += MonthUsage(self, type);
    const int32_t produced = At<int32_t>(self, 0x3C);
    const int32_t needed = At<int32_t>(self, 0x40);
    if (produced > needed)
        At<float>(self, 0x64) = static_cast<float>((produced - needed) / produced);
    else
        At<uint32_t>(self, 0x64) = Global<uint32_t>(addr::PowerTuning_DefaultPercentage);
    const int32_t unpowered = At<int32_t>(self, 0x68) - At<int32_t>(self, 0x6C);
    At<int32_t>(self, field::kUnpoweredConsumers) = unpowered < 0 ? 0 : unpowered;
    void* lots = NewList(addr::SC4List_NodeAllocator3);
    Virtual<uint32_t>(At<void*>(self, 0x30), 0x44, 4u, &lots);
    int32_t x = 0, z = 0;
    ForEachNode(lots, [self, &x, &z](void** node) {
        void* const lot = node[2];
        void* const city = Global<void*>(addr::SC4_City);
        const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
        const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
        Virtual<uint32_t>(lot, 0x18, &x, &z);
        x = x < 0 ? 0 : x;
        z = z < 0 ? 0 : z;
        x = x > lastX ? lastX : x;
        z = z > lastZ ? lastZ : z;
        Virtual<uint32_t>(lot, 0x38, Virtual<uint32_t>(self, 0x94, x, z)); // IsPowered
    });
    ListClearReleasing(&lots);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, lots);
    At<int32_t>(self, kState) = 4;
    using PostMessageFn = void(__cdecl*)(uint32_t message, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
    reinterpret_cast<PostMessageFn>(addr::SC4_PostMessage)(0xEA418D8F, 8, 0, 0, 0, 0);
}

void __fastcall Simulate(void* self, void*, void*)
{
    static bool logged = false;
    FirstCall("Simulate", logged);
    if (!At<uint8_t>(self, 0x14))
        return;
    if (At<uint8_t>(self, 0x33A)) {
        using RectFn = void(__thiscall*)(void* self, const void* rect);
        using AreaFn = void(__thiscall*)(void* self, const void* rect, void* removed);
        void* const rect = static_cast<char*>(self) + kDirtyRect;
        reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_ClearWiredRect)(self, rect);
        reinterpret_cast<AreaFn>(addr::cSC4PowerSimulator_RecomputeArea)(self, rect, nullptr);
        At<uint8_t>(self, 0x33A) = 0;
    }
    int32_t& state = At<int32_t>(self, kState);
    switch (state) {
    case 0:
        StartMonth(self);
        state = 1;
        break;
    case 1:
        DistributeFromPlants(self);
        state = 2;
        break;
    case 2:
        ServeShortConsumers(self);
        state = 3;
        break;
    case 3:
        PublishMonth(self);
        break;
    case 4:
        if (At<uint8_t>(self, kDirty)) {
            state = 0;
            At<uint8_t>(self, kDirty) = 0;
        }
        break;
    default:
        break;
    }
}

using StepFn = void(__fastcall*)(void*, void*, void*);
StepFn g_originalSimulate = nullptr;

bool InstallStep(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_Simulate);
    return game::IsBuild641() && entry && hook::Install(*entry, &Simulate, &g_originalSimulate);
}

const bool g_registeredStep = registry::Register({
    "simulators.power.step",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_Simulate,
    "Our power simulation step: the month's distribution in four states (docs/power.md)",
    false,
    InstallStep,
});

// --- Buildings added and removed (0x006ECC50, 0x006EBF60) ------------------------
//
// By occupant type: network pieces (0x2890D4DE, kept in the vector at
// +0x29C, their cost in +0x78), two other network kinds (0x0990B2EA,
// 0xC772BF98) whose cell is wired or marked dirty, and buildings
// (0x278128A0): consumers add to the consumption map at their centre cell,
// plants (capacity above consumption) join the plant list and their
// budget line.

constexpr uint32_t kNetworkPiece = 0x2890D4DE;
constexpr uint32_t kNetworkKindB = 0x0990B2EA;
constexpr uint32_t kNetworkKindC = 0xC772BF98;
constexpr uint32_t kBuilding = 0x278128A0;

using RectFn = void(__thiscall*)(void* self, int32_t* rect);
const auto GrowRectAt = reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_GrowRect);
const auto FillWiredRectAt = reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_FillWiredRect);
const auto AddDirtyRectAt = reinterpret_cast<RectFn>(addr::cSC4PowerSimulator_AddDirtyRect);
using SetPropertySint64Fn = void(__cdecl*)(void* holder, uint32_t property, int64_t value);
const auto SetPropertySint64 = reinterpret_cast<SetPropertySint64Fn>(addr::SC4_SetPropertySint64);

void CellRect(int32_t* rect)
{
    rect[2] = rect[0];
    rect[3] = rect[1];
}

// The cell under the centre of a building's bounds (occupant slot 10, the
// city helper's cell sizes, slots 91 and 92).
void CentreCell(void* self, void* occupant, int32_t& x, int32_t& z)
{
    float low[3] = {1.0f, 1.0f, 1.0f};
    float high[3] = {0.0f, 0.0f, 0.0f};
    Virtual<uint32_t>(occupant, 0x28, low, high);
    const auto centreZ = static_cast<float>((static_cast<double>(high[2]) + low[2]) * 0.5f);
    const auto centreX = static_cast<float>((static_cast<double>(high[0]) + low[0]) * 0.5f);
    x = Ftol(centreX / Virtual<double>(CityHelper(self), 0x16C));
    z = Ftol(centreZ / Virtual<double>(CityHelper(self), 0x170));
}

int16_t& ConsumptionAt(void* self, int32_t x, int32_t z)
{
    return At<int16_t**>(self, field::kConsumptionMap)[3 * x][z]; // rows are vectors: begin, end, capacity
}

void ReadLoad(void* holder, uint32_t& consumption, uint32_t& capacity)
{
    consumption = 0;
    capacity = 0;
    GetPropertyUint32(holder, 0x27812854, &consumption);
    GetPropertyUint32(holder, 0x27812852, &capacity);
}

void __fastcall OnOccupantInserted(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("OnOccupantInserted", logged);
    if (!At<uint8_t>(self, 0xC))
        return;
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    void* object = nullptr;
    int32_t rect[4];
    if (type == kNetworkPiece) {
        using PushFn = void(__thiscall*)(void* vector, void* const* item);
        if (!Virtual<uint8_t>(occupant, 0x0, 0xA8C86142u, &object))
            return;
        if (Virtual<uint8_t>(object, 0x34)) {
            Virtual<uint32_t>(object, 0x8);
            return;
        }
        Virtual<uint32_t>(object, 0x20, &rect[0], &rect[1]);
        CellRect(rect);
        GrowRectAt(self, rect);
        FillWiredRectAt(self, rect);
        reinterpret_cast<PushFn>(addr::SC4PtrVector_PushBack)(static_cast<char*>(self) + 0x29C, &object); // keeps the reference
        ++At<int32_t>(self, 0x2B8);
        int64_t cost = 0;
        GetPropertySint64(Virtual<void*>(occupant, 0x18), 0x09132DE8, &cost);
        At<int64_t>(self, kDemandTotal) += cost;
        At<uint8_t>(self, kDirty) = 1;
        return;
    }
    if (type == kNetworkKindB) {
        if (!Virtual<uint8_t>(occupant, 0x0, 0xC98F3B24u, &object))
            return;
        if (!Virtual<uint8_t>(object, 0x18)) {
            Virtual<uint32_t>(object, 0x14, &rect[0], &rect[1]);
            CellRect(rect);
            GrowRectAt(self, rect);
            FillWiredRectAt(self, rect);
            At<uint8_t>(self, kDirty) = 1;
        }
        Virtual<uint32_t>(object, 0x8);
        return;
    }
    if (type == kNetworkKindC) {
        if (!Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &object))
            return;
        if (!Virtual<uint8_t>(object, 0x20, 0x80000000u) && Virtual<uint8_t>(object, 0x20, 0x40000u)) {
            Virtual<uint32_t>(object, 0xEC, &rect[0], &rect[1]);
            CellRect(rect);
            FillWiredRectAt(self, rect);
            At<uint8_t>(self, kDirty) = 1;
        }
        Virtual<uint32_t>(object, 0x8);
        return;
    }
    if (type != kBuilding)
        return;
    void* const holder = Virtual<void*>(occupant, 0x18);
    int32_t x, z;
    CentreCell(self, occupant, x, z);
    Virtual<uint32_t>(occupant, 0x2C, rect);
    GrowRectAt(self, rect);
    FillWiredRectAt(self, rect);
    uint32_t consumption, capacity;
    ReadLoad(holder, consumption, capacity);
    if (consumption > 0 && consumption > capacity) {
        ConsumptionAt(self, x, z) += static_cast<int16_t>(consumption);
        ++At<int32_t>(self, 0x68);
        At<uint32_t>(self, 0x74) += consumption;
        if (static_cast<uint8_t>(Virtual<uint32_t>(self, 0x94, x, z))) { // IsPowered
            if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant))
                Virtual<uint32_t>(lot, 0x10C, 1u);
        }
    }
    if (!(capacity > 0 && capacity > consumption))
        return;
    using PushFn = void(__thiscall*)(void* list, const void* entry);
    int32_t powerType = 0;
    GetPropertySint32(holder, 0x27812853, &powerType);
    const struct {
        uint32_t state;
        void* plant;
    } entry = {0, occupant};
    Virtual<uint32_t>(occupant, 0x4);
    reinterpret_cast<PushFn>(addr::cSC4PowerSimulator_PlantList_PushBack)(static_cast<char*>(self) + kPlantNodes, &entry);
    ++At<int32_t>(self, field::kPlantCount);
    ++At<int32_t>(self, field::kPlantCountByType + 4 * powerType);
    At<uint8_t>(self, kDirty) = 1;
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    BudgetItem item;
    item.localFundingPercentage = 0.0f;
    if (!Virtual<uint8_t>(budget, 0xCC, holder, kPurposePowerGeneration, &item))
        return;
    void* source = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, 0x87DDA3A9u, &source))
        return;
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
            line = Virtual<void*>(department, 0x48, item.lineItem, 1u);
            Virtual<uint32_t>(line, 0x48, 2u, 1u);
        }
        Virtual<uint32_t>(department, 0x58, holder, kPurposePowerGeneration);
        Virtual<uint32_t>(line, 0x24, Virtual<int64_t>(line, 0x20) + 1);
        SetPropertySint64(holder, 0x2A58D350, item.cost);
    }
    Virtual<uint32_t>(source, 0x8);
}

void __fastcall OnOccupantRemoved(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("OnOccupantRemoved", logged);
    if (!At<uint8_t>(self, 0xC))
        return;
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    void* object = nullptr;
    int32_t rect[4];
    if (type == kNetworkPiece) {
        if (!Virtual<uint8_t>(occupant, 0x0, 0xA8C86142u, &object))
            return;
        if (!Virtual<uint8_t>(object, 0x34)) {
            Virtual<uint32_t>(object, 0x20, &rect[0], &rect[1]);
            CellRect(rect);
            GrowRectAt(self, rect);
            AddDirtyRectAt(self, rect);
            void**& end = At<void**>(self, 0x2A0);
            for (void** item = At<void**>(self, 0x29C); item != end; ++item) {
                if (*item != object)
                    continue;
                Virtual<uint32_t>(*item, 0x8);
                std::memmove(item, item + 1, (end - (item + 1)) * sizeof(void*));
                --end;
                --At<int32_t>(self, 0x2B8);
                break;
            }
            int64_t cost = 0;
            GetPropertySint64(Virtual<void*>(occupant, 0x18), 0x09132DE8, &cost);
            At<int64_t>(self, kDemandTotal) -= cost;
            At<uint8_t>(self, kDirty) = 1;
        }
        Virtual<uint32_t>(object, 0x8);
        return;
    }
    if (type == kNetworkKindB) {
        if (!Virtual<uint8_t>(occupant, 0x0, 0xC98F3B24u, &object))
            return;
        if (!Virtual<uint8_t>(object, 0x18)) {
            Virtual<uint32_t>(object, 0x14, &rect[0], &rect[1]);
            CellRect(rect);
            GrowRectAt(self, rect);
            AddDirtyRectAt(self, rect);
            At<uint8_t>(self, kDirty) = 1;
        }
        Virtual<uint32_t>(object, 0x8);
        return;
    }
    if (type == kNetworkKindC) {
        if (!Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &object))
            return;
        if (!Virtual<uint8_t>(object, 0x20, 0x80000000u) && Virtual<uint8_t>(object, 0x20, 0x40000u)) {
            Virtual<uint32_t>(object, 0xEC, &rect[0], &rect[1]);
            CellRect(rect);
            AddDirtyRectAt(self, rect);
            At<uint8_t>(self, kDirty) = 1;
        }
        Virtual<uint32_t>(object, 0x8);
        return;
    }
    if (type != kBuilding)
        return;
    int32_t x, z;
    CentreCell(self, occupant, x, z);
    void* const holder = Virtual<void*>(occupant, 0x18);
    uint32_t consumption, capacity;
    ReadLoad(holder, consumption, capacity);
    if (consumption > 0 && consumption > capacity) {
        ConsumptionAt(self, x, z) -= static_cast<int16_t>(consumption);
        if (--At<int32_t>(self, 0x68) < 0)
            At<int32_t>(self, 0x68) = 0;
        At<uint32_t>(self, 0x74) -= consumption;
        if (At<int32_t>(self, 0x74) < 0)
            At<int32_t>(self, 0x74) = 0;
    }
    Virtual<uint32_t>(occupant, 0x2C, rect);
    GrowRectAt(self, rect);
    AddDirtyRectAt(self, rect);
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    BudgetItem item;
    item.localFundingPercentage = 0.0f;
    if (Virtual<uint8_t>(budget, 0xCC, holder, kPurposePowerGeneration, &item)) {
        if (void* const department = Virtual<void*>(budget, 0x70, item.departmentId)) {
            if (void* const line = Virtual<void*>(department, 0x50, item.lineItem)) {
                Virtual<uint32_t>(department, 0x5C, holder, kPurposePowerGeneration);
                Virtual<uint32_t>(line, 0x24, Virtual<int64_t>(line, 0x20) - 1);
            }
        }
    }
    void** const plants = At<void**>(self, kPlantNodes);
    for (void** node = static_cast<void**>(plants[0]); node != plants; node = static_cast<void**>(node[0])) {
        if (node[3] != occupant)
            continue;
        int32_t powerType = 0;
        GetPropertySint32(holder, 0x27812853, &powerType);
        Virtual<uint32_t>(node[3], 0x8);
        auto** const next = static_cast<void**>(node[0]);
        auto** const previous = static_cast<void**>(node[1]);
        previous[0] = next;
        next[1] = previous;
        Free(node);
        --At<int32_t>(self, field::kPlantCountByType + 4 * powerType);
        --At<int32_t>(self, field::kPlantCount);
        At<uint8_t>(self, kDirty) = 1;
        return;
    }
}

using OccupantFn = void(__fastcall*)(void*, void*, void*);
OccupantFn g_originalInserted = nullptr;
OccupantFn g_originalRemoved = nullptr;

bool InstallBuildings(registry::Mode)
{
    const addr::Entry* inserted = addr_check::Find(addr::cSC4PowerSimulator_OnOccupantInserted);
    const addr::Entry* removed = addr_check::Find(addr::cSC4PowerSimulator_OnOccupantRemoved);
    return game::IsBuild641() && inserted && removed &&
           hook::Install(*inserted, &OnOccupantInserted, &g_originalInserted) &&
           hook::Install(*removed, &OnOccupantRemoved, &g_originalRemoved);
}

const bool g_registeredBuildings = registry::Register({
    "simulators.power.buildings",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_OnOccupantInserted,
    "Our power handlers for occupants added and removed: network pieces, consumers, plants (docs/power.md)",
    false,
    InstallBuildings,
});

// --- A network change (0x006EDDA0, message 0x278E6003) --------------------------
//
// Uses no simulator field. The network service's occupants in the rectangle
// (slot 40): pieces (interface 0xA8C86142) whose cell is inside are switched
// on (slot 6) and reported (service slot 21); then each distinct pair of
// ends among the links (interface 0xC98F3B24, slot 3) goes to 0x0064DA00.

void __fastcall OnNetworkChanged(void*, void*, const int32_t* low, const int32_t* high)
{
    static bool logged = false;
    FirstCall("OnNetworkChanged", logged);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using ConnectFn = void(__thiscall*)(void* first, void* second, uint32_t flag);
    const auto allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
    void* const service = Global<void*>(addr::SC4_NetworkService);
    if (service)
        Virtual<uint32_t>(service, 0x4);
    void* occupants = NewList(addr::SC4List_NodeAllocator2);
    if (Virtual<uint8_t>(service, 0xA0, &occupants, low, high, 0u, 0xFFFFFFFFu)) {
        ForEachNode(occupants, [service, low, high](void** node) {
            void* const occupant = node[2];
            void* piece = nullptr;
            if (!Virtual<uint8_t>(occupant, 0x0, 0xA8C86142u, &piece))
                return;
            int32_t x, z;
            Virtual<uint32_t>(piece, 0x20, &x, &z);
            bool inside = false;
            if (x >= low[0] && x <= high[0] && z >= low[1] && z <= high[1]) {
                Virtual<uint32_t>(piece, 0x18, 1u);
                inside = true;
            }
            Virtual<uint32_t>(piece, 0x8);
            if (inside)
                Virtual<uint32_t>(service, 0x54, occupant, 1u, 0u);
        });
    }
    // The links, one per distinct pair of ends (a std::list of references).
    auto** const links = static_cast<void**>(allocate(0xC));
    links[0] = links;
    links[1] = links;
    ForEachNode(occupants, [links, allocate](void** node) {
        void* const occupant = node[2];
        void* link = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, 0xC98F3B24u, &link)) {
            void* first = nullptr;
            void* second = nullptr;
            uint32_t flag = 0;
            Virtual<uint32_t>(link, 0xC, &first, &second, &flag);
            bool duplicate = false;
            if (first) {
                if (second) {
                    for (void** other = static_cast<void**>(links[0]); other != links;
                         other = static_cast<void**>(other[0])) {
                        void* otherFirst = nullptr;
                        void* otherSecond = nullptr;
                        uint32_t otherFlag = 0;
                        Virtual<uint32_t>(other[2], 0xC, &otherFirst, &otherSecond, &otherFlag);
                        if (otherFirst)
                            Virtual<uint32_t>(otherFirst, 0x8);
                        if (otherSecond)
                            Virtual<uint32_t>(otherSecond, 0x8);
                        if (first == otherFirst && second == otherSecond && otherFirst && otherSecond) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        auto** const added = static_cast<void**>(allocate(0xC));
                        added[2] = link; // keeps the reference
                        added[0] = links;
                        added[1] = links[1];
                        static_cast<void**>(links[1])[0] = added;
                        links[1] = added;
                    }
                }
                Virtual<uint32_t>(first, 0x8);
            }
            if (second)
                Virtual<uint32_t>(second, 0x8);
            if (duplicate)
                Virtual<uint32_t>(link, 0x8);
        }
        Virtual<uint32_t>(occupant, 0x8); // the service's reference
    });
    ForEachNode(links, [](void** node) {
        void* first = nullptr;
        void* second = nullptr;
        uint32_t flag = 0;
        Virtual<uint32_t>(node[2], 0xC, &first, &second, &flag);
        Virtual<uint32_t>(node[2], 0x8);
        reinterpret_cast<ConnectFn>(addr::SC4Network_ConnectEnds)(first, second, 0u);
        if (first)
            Virtual<uint32_t>(first, 0x8);
        if (second)
            Virtual<uint32_t>(second, 0x8);
    });
    for (void** node = static_cast<void**>(links[0]); node != links;) {
        void** const next = static_cast<void**>(node[0]);
        Free(node);
        node = next;
    }
    links[0] = links;
    links[1] = links;
    Free(links);
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&occupants);
    Virtual<uint32_t>(NodeAllocatorAt(addr::SC4List_NodeAllocator2), 0x10, occupants);
    Virtual<uint32_t>(service, 0x8);
}

using NetworkFn = void(__fastcall*)(void*, void*, const int32_t*, const int32_t*);
NetworkFn g_originalNetwork = nullptr;

bool InstallNetwork(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_OnNetworkChanged);
    return game::IsBuild641() && entry && hook::Install(*entry, &OnNetworkChanged, &g_originalNetwork);
}

const bool g_registeredNetwork = registry::Register({
    "simulators.power.network",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_OnNetworkChanged,
    "Our power handler for a network change: pieces switched on, link ends joined (docs/power.md)",
    false,
    InstallNetwork,
});

// --- The tuning (0x006ED1C0, message 0xCA1B151C) ---------------------------------
//
// Defaults, then the simulator tuning exemplar (as pollution's LoadTuning);
// the curve defaults go through the game's vector and curve code.

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

void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    using SetPointsFn = uint8_t(__thiscall*)(void* curve, const PointVector* points);
    using LoadCurveFn = uint8_t(__thiscall*)(void* curve, const uint32_t* key, uint32_t property);
    const auto setPoints = reinterpret_cast<SetPointsFn>(addr::SC4Curve_SetPoints);
    const auto loadCurve = reinterpret_cast<LoadCurveFn>(addr::SC4Curve_LoadFromExemplar);
    char* const base = static_cast<char*>(self);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xC911E35B};
    At<uint32_t>(self, 0x2B0) = 2;
    At<float>(self, 0x2B4) = 1.1f;
    At<float>(self, 0x368) = 1.2f;
    PointVector points{};
    PushPoint(points, 0.0f, 0.0f);
    PushPoint(points, 100.0f, 100.0f);
    PushPoint(points, 120.0f, 110.0f);
    setPoints(base + 0x378, &points); // efficiency by funding
    points.end = points.begin;
    PushPoint(points, 0.0f, 1.0f);
    PushPoint(points, 1.0f, 1.0f);
    setPoints(base + 0x398, &points); // aging by funding
    setPoints(base + 0x3A8, &points); // aging by use
    points.end = points.begin;
    PushPoint(points, 0.0f, 20.0f);
    PushPoint(points, 100.0f, 100.0f);
    setPoints(base + 0x388, &points);
    points.end = points.begin;
    PushPoint(points, 0.0f, -0.02f);
    PushPoint(points, 120.0f, 0.01f);
    setPoints(base + 0x3B8, &points); // the funding share step (ApplyFunding)
    At<float>(self, 0x36C) = 0.5f;
    At<int32_t>(self, 0x370) = 10;
    At<float>(self, 0x374) = 0.9f;
    At<float>(self, 0x364) = 0.6f;
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u)) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        // Percentages in the exemplar, fractions here.
        const auto percent = [holder](uint32_t property, float& to) {
            float value;
            if (GetPropertyFloat(holder, property, &value))
                to = static_cast<float>(static_cast<double>(value) * 0.01f);
        };
        GetPropertyUint32(holder, 0xA911E2F1, &At<uint32_t>(self, 0x2B0));
        if (GetPropertyFloat(holder, 0xE911E2FC, &At<float>(self, 0x2B4)))
            At<float>(self, 0x2B4) = static_cast<float>(static_cast<double>(At<float>(self, 0x2B4)) * 0.01f);
        percent(0x29121E8B, At<float>(self, 0x364));
        loadCurve(base + 0x378, key, 0x6911E2DF);
        percent(0xE911E2D1, At<float>(self, 0x368));
        percent(0x09122027, At<float>(self, 0x36C));
        GetPropertySint32(holder, 0x2912202E, &At<int32_t>(self, 0x370));
        percent(0x49122036, At<float>(self, 0x374));
        loadCurve(base + 0x388, key, 0x0911E2E6);
        loadCurve(base + 0x398, key, 0x0911E2E8);
        loadCurve(base + 0x3A8, key, 0x0911E2E9);
        loadCurve(base + 0x3B8, key, 0xAA713782);
        Virtual<uint32_t>(exemplar, 0x8);
    }
    At<float>(self, 0x2AC) =
        static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Service43D90), 0x64, 0x0911E117u, 0u));
    if (points.begin)
        Free(points.begin);
}

ThisFn g_originalTuning = nullptr;

bool InstallTuning(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_LoadTuning);
    return game::IsBuild641() && entry && hook::Install(*entry, &LoadTuning, &g_originalTuning);
}

const bool g_registeredTuning = registry::Register({
    "simulators.power.tuning",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_LoadTuning,
    "Our power tuning: defaults and the simulator exemplar's values and curves (docs/power.md)",
    false,
    InstallTuning,
});

// --- The interface at +0x4: cIGZMessageTarget2 (vtable 0x00AB28A4) ----------------

// 0x26D31EC1 CityInited: the network pieces that are gone (interface
// 0x26BB767E, slot 32) leave the vector at +0x29C; then the demand total.
void OnCityInited(void* self)
{
    static bool logged = false;
    FirstCall("OnCityInited", logged);
    using EraseFn = void**(__thiscall*)(void* vector, void** item);
    using DemandFn = uint8_t(__thiscall*)(void* self);
    void** item = At<void**>(self, 0x29C);
    while (item != At<void**>(self, 0x2A0)) {
        void* const piece = *item;
        void* occupant = nullptr;
        if (Virtual<uint8_t>(piece, 0x0, 0x26BB767Eu, &occupant) && !Virtual<uint8_t>(occupant, 0x80, 1u)) {
            Virtual<uint32_t>(piece, 0x8);
            item = reinterpret_cast<EraseFn>(addr::SC4PtrVector_Erase)(static_cast<char*>(self) + 0x29C, item);
            --At<int32_t>(self, 0x2B8);
        } else {
            ++item;
        }
        if (occupant)
            Virtual<uint32_t>(occupant, 0x8);
    }
    if (reinterpret_cast<DemandFn>(addr::cSC4PowerSimulator_UpdateDemandTotal)(self))
        At<uint8_t>(self, 0x338) = 1;
}

// 0x07699455: a cell rectangle (message slot 15) changed: the wired bits
// around it, and the city's view object 5 (city slot 38, then its slot 5)
// told.
void OnCellsChanged(void* self, void* message)
{
    static bool logged = false;
    FirstCall("OnCellsChanged", logged);
    struct Rect {
        int32_t x0, z0, x1, z1;
    };
    using CellsFn = uint8_t(__thiscall*)(void* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1);
    const Rect* const rect = Virtual<const Rect*>(message, 0x3C);
    void* const view = Virtual<void*>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x98), 0x14, 5u, 0u);
    Virtual<uint32_t>(view, 0x4);
    Virtual<uint32_t>(view, 0xC);
    reinterpret_cast<CellsFn>(addr::cSC4PowerSimulator_OnNetworkCellChanged)(self, rect->x0, rect->z0, rect->x1,
                                                                             rect->z1);
    const uint32_t value = Virtual<uint32_t>(message, 0x4C);
    Virtual<uint32_t>(view, 0x28, *rect, value);
    Virtual<uint32_t>(view, 0x10);
    Virtual<uint32_t>(view, 0x8);
}

// 0xC2C09554 with 0xE0D0722E: the lots of the plants of type 5 go to the
// object at 0x00B43D00 (slot 7); then the object at 0x00B43DC4 (slots 24, 26).
void OnPlantDisaster(void* self, void* message)
{
    static bool logged = false;
    FirstCall("OnPlantDisaster", logged);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using RefListFn = void(__thiscall*)(void** list, void* const* object);
    using RefClearFn = void(__thiscall*)(void** list);
    if (Virtual<uint32_t>(message, 0x38) != 0xE0D0722E || !Virtual<uint32_t>(message, 0x4C))
        return;
    auto** const sentinel = static_cast<void**>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0xC));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* lots = sentinel;
    ForEachNode(At<void*>(self, kPlantNodes), [&lots](void** node) {
        uint32_t powerType;
        if (!static_cast<uint8_t>(GetPropertyUint32(Virtual<void*>(node[3], 0x18), 0x27812853, &powerType)) ||
            powerType != 5)
            return;
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, node[3]);
        if (lot)
            Virtual<uint32_t>(lot, 0x4);
        reinterpret_cast<RefListFn>(addr::SC4RefList_PushBack)(&lots, &lot);
        if (lot)
            Virtual<uint32_t>(lot, 0x8);
    });
    Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D00), 0x1C, 1u, &lots, 0u, 1u, 0u, 0u, 0u, 0u);
    void* const service = Global<void*>(addr::SC4_Service43DC4);
    if (void* const current = Virtual<void*>(service, 0x60)) {
        if (Virtual<uint32_t>(current, 0x14) == 0x88F154FB)
            Virtual<uint32_t>(service, 0x68, 0u);
    }
    reinterpret_cast<RefClearFn>(addr::SC4RefList_Clear)(&lots);
    if (lots)
        Free(lots);
}

// 0xE8BDA2E7: an object (message slot 32, interface 0xE818FCED) of kind 1
// (slot 33) whose centre cell is powered switches its lot's power on.
void OnObjectPlaced(void* self, void* message)
{
    static bool logged = false;
    FirstCall("OnObjectPlaced", logged);
    if (!Virtual<void*>(message, 0x80))
        return;
    void* object = nullptr;
    if (!Virtual<uint8_t>(Virtual<void*>(message, 0x80), 0x0, 0xE818FCEDu, &object))
        return;
    if (Virtual<uint32_t>(object, 0x84) == 1) {
        int32_t rect[4];
        Virtual<uint32_t>(object, 0x38, rect);
        const int32_t x = (rect[0] + rect[2]) / 2;
        const int32_t z = (rect[1] + rect[3]) / 2;
        if (static_cast<uint8_t>(Virtual<uint32_t>(self, 0x94, x, z))) { // IsPowered
            if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u))
                Virtual<uint32_t>(lot, 0x10C, 1u);
        }
    }
    Virtual<uint32_t>(object, 0x8);
}

// 0xEA418D8F: a consumer (interface 0x0773FEF1, type 0x4773FFE0 or
// 0x4773FFE1) changed: the cycle starts over at the next step.
void OnConsumerChanged(void* self, void* message)
{
    static bool logged = false;
    FirstCall("OnConsumerChanged", logged);
    if (Virtual<uint32_t>(message, 0x24) != 0)
        return;
    void* consumer = nullptr;
    if (void* const data = Virtual<void*>(message, 0x80)) {
        if (Virtual<uint8_t>(data, 0x0, 0x0773FEF1u, &consumer)) {
            if (Virtual<uint32_t>(consumer, 0x20) == 0x4773FFE1 || Virtual<uint32_t>(consumer, 0x20) == 0x4773FFE0) {
                At<int32_t>(self, kState) = 4;
                At<uint8_t>(self, kDirty) = 1;
            }
        }
    }
    if (consumer)
        Virtual<uint32_t>(consumer, 0x8);
}

// 3 (0x006EFB70) DoMessage: hands each message to its handler (by address,
// so that each handler is the game's or ours as configured).
uint8_t __fastcall DoMessage(void* interface4, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(interface4) - 0x4;
    using StepFn = void(__thiscall*)(void* self, uint32_t amount);
    using OccupantFn = void(__thiscall*)(void* self, void* occupant);
    using CornersFn = void(__thiscall*)(void* self, const int32_t* low, const int32_t* high);
    const auto step = reinterpret_cast<StepFn>(addr::cSC4PowerSimulator_Simulate);
    switch (Virtual<uint32_t>(message, 0x10)) { // GetType
    case 0x278E6003: {
        const uint32_t kind = Virtual<uint32_t>(message, 0x60);
        if (kind == 4 || kind == 5)
            break;
        const uint32_t first = Virtual<uint32_t>(message, 0x24);
        const uint32_t second = Virtual<uint32_t>(message, 0x38);
        int32_t low[2] = {static_cast<int32_t>(first >> 16) - 1, static_cast<int32_t>(second >> 16) - 1};
        int32_t high[2] = {static_cast<int32_t>(first & 0xFFFF), static_cast<int32_t>(second & 0xFFFF)};
        const int32_t lastX = Virtual<int32_t>(CityHelper(self), 0x174) - 1;
        const int32_t lastZ = Virtual<int32_t>(CityHelper(self), 0x178) - 1;
        low[0] = low[0] < 0 ? 0 : low[0];
        low[1] = low[1] < 0 ? 0 : low[1];
        high[0] = high[0] > lastX ? lastX : high[0];
        high[1] = high[1] > lastZ ? lastZ : high[1];
        reinterpret_cast<CornersFn>(addr::cSC4PowerSimulator_OnNetworkChanged)(self, low, high);
        break;
    }
    case 0x26D31EC1: OnCityInited(self); break;
    case 0x07699455: OnCellsChanged(self, message); break;
    case 0x66956814: step(self, 5); break;
    case 0x6695681A: step(self, Virtual<uint32_t>(message, 0x4C)); break;
    case 0x66956816: reinterpret_cast<ThisFn>(addr::cSC4PowerSimulator_OnMonthPassed)(self, nullptr); break;
    case 0x6AC284F3: At<uint8_t>(self, 0x14) = 1; break;
    case 0xCA1B151C: reinterpret_cast<ThisFn>(addr::cSC4PowerSimulator_LoadTuning)(self, nullptr); break;
    case 0x99EF1142:
        reinterpret_cast<OccupantFn>(addr::cSC4PowerSimulator_OnOccupantInserted)(self, Virtual<void*>(message, 0x28));
        break;
    case 0x99EF1143:
        reinterpret_cast<OccupantFn>(addr::cSC4PowerSimulator_OnOccupantRemoved)(self, Virtual<void*>(message, 0x28));
        break;
    case 0xC2C09554: OnPlantDisaster(self, message); break;
    case 0xE8BDA2E7: OnObjectPlaced(self, message); break;
    case 0xEA418D8F: OnConsumerChanged(self, message); break;
    default: break;
    }
    return 1;
}

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4PowerSimulator_vftable_4);
    if (vtable[3] != 0x006EFB70) {
        log::Write("power messages: +0x4 slot 3 holds %08X, expected 006EFB70; not installing",
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
    "simulators.power.messages",
    "simulators/power",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_vftable_4,
    "Our power DoMessage: each message to its handler, the rare ones handled here (docs/power.md)",
    false,
    InstallMessages,
});

// --- Init (slot 3, 0x006F03B0) -------------------------------------------------
//
// References to the city and its services, then once the tuning, the
// simulator and figure registrations, the messages, the consumption map
// sized to the city; once more the bit arrays, the supply grid (a
// cSC4SimGridUint8), the cleared usage history and the department.
// Allocations go through the game's own code, in the game's order.

constexpr uint32_t kPowerMessages[] = {0x99EF1142, 0x99EF1143, 0x07699455, 0xCA1B151C, 0x278E6003, 0xC2C09554,
                                       0xE8BDA2E7, 0xEA418D8F, 0x6AC284F3, 0x26D31EC1};
constexpr uint32_t kUsageFigureIds[] = {0xAA120D5E, 0xAA120D61, 0xAA120D63, 0xAA120D65, 0xAA120D66,
                                        0xAA120D68, 0xAA120D6A, 0xAA120D6B, 0xAA120D6D};

void Reference(void* self, std::size_t offset, void* object)
{
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4); // AddRef
}

// The consumption map (+0x84, vector<vector<int16_t>>) sized to the city's
// cells, all 0, when its row count differs.
void SizeConsumptionMap(void* self)
{
    struct ShortVector {
        int16_t* begin;
        int16_t* end;
        int16_t* capacity;
    };
    using ResizeFn = void(__thiscall*)(void* map, uint32_t rows, const ShortVector* value);
    using FillInsertFn = void(__thiscall*)(ShortVector* row, int16_t* where, uint32_t count, const int16_t* value);
    const uint32_t cellsX = Virtual<uint32_t>(CityHelper(self), 0x174);
    const uint32_t cellsZ = Virtual<uint32_t>(CityHelper(self), 0x178);
    auto& rows = At<ShortVector*>(self, field::kConsumptionMap);
    const auto count = static_cast<uint32_t>(At<ShortVector*>(self, field::kConsumptionMap + 4) - rows);
    if (count == cellsX)
        return;
    const ShortVector empty{};
    reinterpret_cast<ResizeFn>(addr::SC4ShortVectorVector_Resize)(&rows, cellsX, &empty);
    for (uint32_t x = 0; x < cellsX; ++x) {
        ShortVector& row = rows[x];
        const auto size = static_cast<uint32_t>(row.end - row.begin);
        if (cellsZ < size) {
            row.end = row.begin + cellsZ;
        } else {
            const int16_t zero[2] = {0, 0};
            reinterpret_cast<FillInsertFn>(addr::SC4ShortVector_FillInsert)(&row, row.end, cellsZ - size, zero);
        }
        for (uint32_t z = 0; z < cellsZ; ++z)
            rows[x].begin[z] = 0;
    }
}

uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    using SelfFn = void(__thiscall*)(void* self);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    char* const base = static_cast<char*>(self);
    Global<uint8_t>(addr::PowerSimulator_Started) = 1;
    if (!At<void*>(self, 0x18))
        Reference(self, 0x18, Global<void*>(addr::SC4_City));
    if (!At<void*>(self, 0x28))
        Reference(self, 0x28, Virtual<void*>(CityHelper(self), 0x88));
    if (!At<void*>(self, 0x2C))
        Reference(self, 0x2C, Virtual<void*>(CityHelper(self), 0x90));
    if (!At<void*>(self, 0x34))
        Reference(self, 0x34, Global<void*>(addr::SC4_Service43D00));
    if (!At<void*>(self, 0x30))
        Reference(self, 0x30, Virtual<void*>(CityHelper(self), 0xF8));
    if (!At<void*>(self, 0x1C))
        Reference(self, 0x1C, Virtual<void*>(CityHelper(self), 0xD0));
    if (At<void*>(self, 0x1C) && !At<void*>(self, field::kDepartment)) {
        void* const department = Virtual<void*>(At<void*>(self, 0x1C), 0x70, 0x8910BC8Au);
        At<void*>(self, field::kDepartment) = department;
        if (department)
            Virtual<uint32_t>(department, 0x4);
    }
    void* const target = base + 0x4;
    if (!At<uint8_t>(self, 0xC)) {
        At<uint8_t>(self, 0xC) = 1;
        reinterpret_cast<ThisFn>(addr::cSC4PowerSimulator_LoadTuning)(self, nullptr);
        if (void* const simulators = Global<void*>(addr::SC4_SimulatorService)) {
            struct {
                const void* vtable;
                char* begin;
                char* end;
                char* capacity;
                uint32_t zero;
            } name;
            reinterpret_cast<StringFn>(addr::SC4String_FromText)(&name, reinterpret_cast<const char*>(addr::PowerSimulator_Name));
            Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 2u);
            Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
            Virtual<uint32_t>(simulators, 0x4C, target, 10u, &name, 2u);
            name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
            if (name.begin)
                Free(name.begin);
        }
        if (void* const figures = Global<void*>(addr::SC4_MonthlyFigures)) {
            for (const uint32_t id : kUsageFigureIds)
                Virtual<uint32_t>(figures, 0x34, id, 2u);
        }
        if (void* const messages = Global<void*>(addr::SC4_MessageServer)) {
            for (const uint32_t message : kPowerMessages)
                Virtual<uint8_t>(messages, 0x14, target, message);
        }
        SizeConsumptionMap(self);
    }
    if (At<uint8_t>(self, 0xD))
        return 1;
    At<uint8_t>(self, 0xD) = 1;
    At<uint8_t>(self, 0x2D8) = 0;
    At<uint8_t>(self, kDirty) = 0;
    At<uint8_t>(self, 0x339) = 0;
    At<uint8_t>(self, 0x33A) = 0;
    reinterpret_cast<SelfFn>(addr::cSC4PowerSimulator_CreateBitArrays)(self);
    if (!At<void*>(self, field::kSupplyGrid)) {
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
        At<void*>(self, field::kSupplyGrid) = grid;
        Virtual<uint32_t>(grid, 0x4);
        Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0xC);
        Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0x24, 1u);
        Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0x18, 0x49D5BC86u);
        Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0x6C, 0u);
    }
    for (int type = 0; type < 10; ++type) {
        for (int month = 0; month < 12; ++month)
            At<int32_t>(self, kUsageByMonth + 4 * (month * 10 + type)) = 0;
        At<int32_t>(self, kAnnualByType + 4 * type) = 0;
    }
    At<int32_t>(self, kState) = 0;
    At<int32_t>(self, kState) = 4;
    if (!At<void*>(self, field::kDepartment)) {
        Reference(self, field::kDepartment,
                  Virtual<void*>(At<void*>(self, 0x1C), 0x64, 0x8910BC8Au, 0x4A357EAFu));
        void* const department = At<void*>(self, field::kDepartment);
        Virtual<uint32_t>(department, 0x14, 0x6A3FF01Cu, 0x4A493980u);
        Virtual<uint32_t>(department, 0x34, base + 0x368);
        Virtual<uint32_t>(Virtual<void*>(department, 0x44, 1u, 0u), 0x1C, 0x6A3FF01Cu, 0x2A75F757u);
    }
    return 1;
}

// --- Shutdown (slot 4, 0x006F0110) ---------------------------------------------

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
    using FreeFn = void(__cdecl*)(void* p);
    using EraseRowsFn = void(__thiscall*)(void* map, void* first, void* last);
    const auto operatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
    void* const target = static_cast<char*>(self) + 0x4;
    if (At<uint8_t>(self, 0xD)) {
        At<uint8_t>(self, 0xD) = 0;
        for (const std::size_t offset : {0x18, 0x28, 0x34, 0x2C, 0x20, 0x1C})
            Drop(self, offset);
        if (void* const messages = Global<void*>(addr::SC4_MessageServer)) {
            for (const uint32_t message : kPowerMessages)
                Virtual<uint8_t>(messages, 0x18, target, message);
        }
        if (void* const service = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
            Virtual<uint32_t>(service, 0x50, target);
    }
    if (!At<uint8_t>(self, 0xC))
        return 1;
    At<uint8_t>(self, 0xC) = 0;
    ForEachNode(At<void*>(self, kPlantNodes), [](void** node) { Virtual<uint32_t>(node[3], 0x8); });
    void** const plants = At<void**>(self, kPlantNodes);
    for (void** node = static_cast<void**>(plants[0]); node != At<void**>(self, kPlantNodes);) {
        void** const next = static_cast<void**>(node[0]);
        Free(node);
        node = next;
    }
    At<void**>(self, kPlantNodes)[0] = At<void*>(self, kPlantNodes);
    At<void**>(self, kPlantNodes)[1] = At<void*>(self, kPlantNodes);
    for (void** piece = At<void**>(self, 0x29C); piece != At<void**>(self, 0x2A0); ++piece) {
        Virtual<uint32_t>(*piece, 0x18, 0u);
        Virtual<uint32_t>(*piece, 0x8);
    }
    At<void**>(self, 0x2A0) = At<void**>(self, 0x29C);
    At<uint32_t>(self, 0x2B8) = 0;
    At<uint32_t>(self, 0x2BC) = 0;
    for (const std::size_t offset : {0x2C8, 0x2C4, 0x2D4, 0x2D0}) {
        if (void*& bits = At<void*>(self, offset)) {
            operatorDelete(bits);
            bits = nullptr;
        }
    }
    if (At<void*>(self, field::kSupplyGrid)) {
        Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0x10);
        Virtual<uint32_t>(At<void*>(self, field::kSupplyGrid), 0x8);
        At<void*>(self, field::kSupplyGrid) = nullptr;
    }
    Drop(self, 0x30);
    reinterpret_cast<EraseRowsFn>(addr::SC4ShortVectorVector_Erase)(
        static_cast<char*>(self) + field::kConsumptionMap, At<void*>(self, field::kConsumptionMap),
        At<void*>(self, field::kConsumptionMap + 4));
    return 1;
}

using LifecycleFn = uint8_t(__fastcall*)(void*, void*);
LifecycleFn g_originalInit = nullptr;
LifecycleFn g_originalShutdown = nullptr;

bool InstallInit(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_Init);
    return game::IsBuild641() && entry && hook::Install(*entry, &Init, &g_originalInit);
}

bool InstallShutdown(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_Shutdown);
    return game::IsBuild641() && entry && hook::Install(*entry, &Shutdown, &g_originalShutdown);
}

const bool g_registeredInit = registry::Register({
    "simulators.power.init",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_Init,
    "Our power Init: references, registrations, the consumption map, bit arrays and supply grid (docs/power.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.power.shutdown",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_Shutdown,
    "Our power Shutdown: references and registrations dropped, the plant list, bit arrays and grid freed (docs/power.md)",
    false,
    InstallShutdown,
});

// --- Saving: cIGZSerializable (vtable 0x00AB288C) at +0x8 ---------------------------
//
// Write is its slot 3 (0x006EB6A0); the record, version 6, field by field
// in the game's order and types.

constexpr std::size_t kSerializable = 0x8;

using WriteObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
const auto WriteReference = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);

// cIGZOStream, as the game calls it.
struct Out {
    void* stream;
    void Uint8(uint8_t v) const { Virtual<uint8_t>(stream, 0x14, v); }
    void Uint16At18(uint16_t v) const { Virtual<uint8_t>(stream, 0x18, static_cast<uint32_t>(v)); }
    void Uint16(uint16_t v) const { Virtual<uint8_t>(stream, 0x1C, v); }
    void Sint32(int32_t v) const { Virtual<uint8_t>(stream, 0x20, v); }
    void Uint32(uint32_t v) const { Virtual<uint8_t>(stream, 0x24, v); }
    void Sint64(int64_t v) const { Virtual<uint8_t>(stream, 0x28, v); }
    void Float32(uint32_t bits) const { Virtual<uint8_t>(stream, 0x30, bits); } // the float's bits, as the game pushes them
};

uint8_t __fastcall Write(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    const Out out{stream};
    auto i32 = [self](std::size_t offset) { return At<int32_t>(self, offset); };
    auto u32 = [self](std::size_t offset) { return At<uint32_t>(self, offset); };
    out.Uint16(6); // version
    WriteReference(stream, At<void*>(self, field::kDepartment));
    for (const std::size_t offset : {0x38, 0x3C, 0x40})
        out.Sint32(i32(offset));
    out.Float32(u32(0x64));
    for (const std::size_t offset : {0x68, 0x6C, 0x70, 0x74})
        out.Sint32(i32(offset));
    out.Sint64(At<int64_t>(self, kDemandTotal));
    for (const std::size_t offset : {0x50, 0x54, 0x58, 0x5C, 0x60})
        out.Sint32(i32(offset));
    WriteReference(stream, At<void*>(self, field::kSupplyGrid));
    // The consumption map: rows, the first row's length, then the values.
    auto* const rows = At<int16_t**>(self, field::kConsumptionMap);
    const auto rowCount = static_cast<uint32_t>((At<char*>(self, field::kConsumptionMap + 4) -
                                                 At<char*>(self, field::kConsumptionMap)) / 12);
    const auto columns = static_cast<uint32_t>(rows[1] - rows[0]);
    out.Uint32(rowCount);
    out.Uint32(columns);
    for (uint32_t x = 0; x < rowCount; ++x)
        for (uint32_t z = 0; z < columns; ++z)
            out.Uint16At18(static_cast<uint16_t>(At<int16_t**>(self, field::kConsumptionMap)[3 * x][z]));
    for (int month = 0; month < 12; ++month)
        for (int type = 0; type < 10; ++type)
            out.Sint32(i32(kUsageByMonth + 4 * (month * 10 + type)));
    for (int type = 0; type < 10; ++type)
        out.Sint32(i32(kAnnualByType + 4 * type));
    out.Sint32(i32(field::kPlantCount));
    for (int type = 0; type < 10; ++type)
        out.Sint32(i32(field::kPlantCountByType + 4 * type));
    ForEachNode(At<void*>(self, kPlantNodes), [&out, stream](void** node) {
        out.Uint32(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(node[2])));
        WriteReference(stream, node[3]);
    });
    out.Uint32(u32(0x2B8));
    out.Uint32(u32(0x2BC));
    for (uint32_t i = 0; i < u32(0x2B8); ++i)
        WriteReference(stream, At<void**>(self, 0x29C)[i]);
    out.Uint32(u32(0x2CC)); // words per bit array
    for (const std::size_t bits : {0x2C4, 0x2C8, 0x2D4, 0x2D0})
        for (uint32_t i = 0; i < u32(0x2CC); ++i)
            out.Uint32(At<uint32_t*>(self, bits)[i]);
    out.Uint8(At<uint8_t>(self, 0x2D8));
    out.Uint8(At<uint8_t>(self, 0x330));
    out.Sint32(i32(0x334));
    out.Uint8(At<uint8_t>(self, 0x338));
    out.Uint8(At<uint8_t>(self, 0x339));
    out.Uint32(u32(kState));
    return 1;
}

using WriteFn = uint8_t(__fastcall*)(void*, void*, void*);
WriteFn g_originalWrite = nullptr;

bool InstallSave(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_Write);
    return game::IsBuild641() && entry && hook::Install(*entry, &Write, &g_originalWrite);
}

const bool g_registeredSave = registry::Register({
    "simulators.power.save",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_Write,
    "Our power Write: the save record, version 6 (docs/power.md)",
    false,
    InstallSave,
});

// Read is its slot 4 (0x006F0880): versions up to 6 (before 6, +0x64 was a
// whole percentage). Marks Init's second stage as done (+0xD): the bit
// arrays come from the record.

using ReadObjectFn = uint8_t(__cdecl*)(void* stream, uint32_t interfaceId, void* out, uint32_t flags);
const auto ReadReference = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReference);

// cIGZIStream, as the game calls it.
struct In {
    void* stream;
    void Uint8(void* to) const { Virtual<uint8_t>(stream, 0x14, to); }
    void Sint16(void* to) const { Virtual<uint8_t>(stream, 0x18, to); }
    void Uint16(void* to) const { Virtual<uint8_t>(stream, 0x1C, to); }
    void Sint32(void* to) const { Virtual<uint8_t>(stream, 0x20, to); }
    void Uint32(void* to) const { Virtual<uint8_t>(stream, 0x24, to); }
    void Sint64(void* to) const { Virtual<uint8_t>(stream, 0x28, to); }
    void Float32(void* to) const { Virtual<uint8_t>(stream, 0x30, to); }
};

uint8_t __fastcall Read(void* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    struct ShortVector {
        int16_t* begin;
        int16_t* end;
        int16_t* capacity;
    };
    using ResizeFn = void(__thiscall*)(void* map, uint32_t rows, const ShortVector* value);
    using FillInsertFn = void(__thiscall*)(ShortVector* row, int16_t* where, uint32_t count, const int16_t* value);
    using PtrFillInsertFn = void(__thiscall*)(void* vector, void** where, uint32_t count, void* const* value);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    char* const self = static_cast<char*>(serializable) - kSerializable;
    const In in{stream};
    At<uint8_t>(self, 0xD) = 1;
    uint16_t version = 0;
    in.Uint16(&version);
    if (version > 6)
        return 0;
    ReadReference(stream, 0x223E2B67, self + field::kDepartment, 0);
    for (const std::size_t offset : {0x38, 0x3C, 0x40})
        in.Sint32(self + offset);
    if (version >= 6) {
        uint32_t bits;
        in.Float32(&bits);
        At<uint32_t>(self, 0x64) = bits;
    } else {
        int32_t percent;
        in.Sint32(&percent);
        At<float>(self, 0x64) = static_cast<float>(static_cast<double>(percent) * 0.01f);
    }
    for (const std::size_t offset : {0x68, 0x6C, 0x70, 0x74})
        in.Sint32(self + offset);
    in.Sint64(self + kDemandTotal);
    for (const std::size_t offset : {0x50, 0x54, 0x58, 0x5C, 0x60})
        in.Sint32(self + offset);
    ReadReference(stream, 0xC9BD939A, self + field::kSupplyGrid, 0);

    uint32_t rowCount = 0, columns = 0;
    in.Uint32(&rowCount);
    in.Uint32(&columns);
    const ShortVector empty{};
    reinterpret_cast<ResizeFn>(addr::SC4ShortVectorVector_Resize)(self + field::kConsumptionMap, rowCount, &empty);
    const int16_t zero[2] = {0, 0};
    for (uint32_t x = 0; x < rowCount; ++x) {
        ShortVector& row = At<ShortVector*>(self, field::kConsumptionMap)[x];
        const auto size = static_cast<uint32_t>(row.end - row.begin);
        if (columns < size)
            row.end = row.begin + columns;
        else
            reinterpret_cast<FillInsertFn>(addr::SC4ShortVector_FillInsert)(&row, row.end, columns - size, zero);
        for (uint32_t z = 0; z < columns; ++z)
            in.Sint16(&At<ShortVector*>(self, field::kConsumptionMap)[x].begin[z]);
    }
    for (int month = 0; month < 12; ++month)
        for (int type = 0; type < 10; ++type)
            in.Sint32(self + kUsageByMonth + 4 * (month * 10 + type));
    for (int type = 0; type < 10; ++type)
        in.Sint32(self + kAnnualByType + 4 * type);
    in.Sint32(self + field::kPlantCount);
    for (int type = 0; type < 10; ++type)
        in.Sint32(self + field::kPlantCountByType + 4 * type);
    for (int32_t i = 0; i < At<int32_t>(self, field::kPlantCount); ++i) {
        uint32_t state = 0;
        in.Uint32(&state);
        void* plant = nullptr;
        ReadReference(stream, 0x26BB767E, &plant, 0);
        void** const plants = At<void**>(self, kPlantNodes);
        auto** const node = static_cast<void**>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x10));
        node[2] = reinterpret_cast<void*>(static_cast<std::uintptr_t>(state));
        node[3] = plant;
        node[0] = plants;
        node[1] = plants[1];
        static_cast<void**>(plants[1])[0] = node;
        plants[1] = node;
    }
    in.Uint32(self + 0x2B8);
    in.Uint32(self + 0x2BC);
    {
        const uint32_t count = At<uint32_t>(self, 0x2B8);
        void**& end = At<void**>(self, 0x2A0);
        const auto size = static_cast<uint32_t>(end - At<void**>(self, 0x29C));
        void* const none = nullptr;
        if (count < size)
            end = At<void**>(self, 0x29C) + count;
        else
            reinterpret_cast<PtrFillInsertFn>(addr::SC4PtrVector_FillInsert)(self + 0x29C, end, count - size, &none);
        for (uint32_t i = 0; i < At<uint32_t>(self, 0x2B8); ++i)
            ReadReference(stream, 0xA8C86142, At<void**>(self, 0x29C) + i, 0);
    }
    in.Uint32(self + 0x2CC); // words per bit array
    for (const std::size_t bits : {0x2C4, 0x2C8, 0x2D4, 0x2D0}) {
        At<void*>(self, bits) = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(At<uint32_t>(self, 0x2CC) * 4);
        for (uint32_t i = 0; i < At<uint32_t>(self, 0x2CC); ++i)
            in.Uint32(At<uint32_t*>(self, bits) + i);
    }
    in.Uint8(self + 0x2D8);
    in.Uint8(self + 0x330);
    in.Sint32(self + 0x334);
    in.Uint8(self + 0x338);
    in.Uint8(self + 0x339);
    At<uint8_t>(self, 0x330) = 0;
    uint32_t state = 0;
    in.Uint32(&state);
    At<uint32_t>(self, kState) = state;
    return 1;
}

using ReadFn = uint8_t(__fastcall*)(void*, void*, void*);
ReadFn g_originalRead = nullptr;

bool InstallLoad(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_Read);
    return game::IsBuild641() && entry && hook::Install(*entry, &Read, &g_originalRead);
}

const bool g_registeredLoad = registry::Register({
    "simulators.power.load",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_Read,
    "Our power Read: the save record, versions up to 6 (docs/power.md)",
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
    OPENSC4_SLOT(5, 0x006EAC40, PositionToCell),
    OPENSC4_SLOT(6, 0x006EAC90, CellToPosition),
    OPENSC4_SLOT(7, 0x006EACD0, SameCell),
    OPENSC4_SLOT(8, 0x006EACD0, SameCell),
    OPENSC4_SLOT(9, 0x006EACF0, GetAnnualConsumption),
    OPENSC4_SLOT(10, 0x006EAD30, GetAnnualConsumptionByPowerType),
    OPENSC4_SLOT(11, 0x00407AC0, FieldGetter<field::kMonthlyRequirement>),
    OPENSC4_SLOT(12, 0x0081F910, FieldGetter<field::kMonthlyProduction>),
    OPENSC4_SLOT(13, 0x0040D390, FieldGetter<field::kMonthlyImport>),
    OPENSC4_SLOT(14, 0x006A0050, FieldGetter<field::kMonthlyExport>),
    OPENSC4_SLOT(15, 0x00477110, FieldGetter<field::kMonthlyDemand>),
    OPENSC4_SLOT(16, 0x006EBA10, GetMonthlyConsumptionAt),
    OPENSC4_SLOT(17, 0x006AAEB0, FieldGetter<field::kMonthlyConsumption>),
    OPENSC4_SLOT(18, 0x006EAD40, GetMonthlyConsumptionMapPtr),
    OPENSC4_SLOT(19, 0x006EAD50, GetSupplyValue),
    OPENSC4_SLOT(20, 0x006EAD60, GetSupplyValueMapPtr),
    OPENSC4_SLOT(21, 0x00809240, FieldGetter<field::kUnpoweredConsumers>),
    OPENSC4_SLOT(22, 0x00698CE0, FieldGetter<field::kPoweredConsumers>),
    OPENSC4_SLOT(23, 0x006EFA00, GetPowerPlantCountByType),
    OPENSC4_SLOT(24, 0x006EF9F0, FieldGetter<field::kPlantCount>),
    OPENSC4_SLOT(25, 0x006ED120, GetPowerPlants),
    OPENSC4_SLOT(26, 0x006EFA10, FieldGetter<field::kPoleCount>),
    OPENSC4_SLOT(27, 0x006EFA20, FieldGetter<field::kOverworkedPlants>),
    OPENSC4_SLOT(28, 0x006EFA30, FieldGetter<field::kAgedPlants>),
    OPENSC4_SLOT(29, 0x006EFA40, FieldGetter<field::kDistressedPlants>),
    OPENSC4_SLOT(30, 0x006EBA30, FindPowerPlants),
    OPENSC4_SLOT(31, 0x006EB3D0, IsOccupantPowered),
    OPENSC4_SLOT(32, 0x006EB0D0, GetOccupantPowerConsumed),
    OPENSC4_SLOT(33, 0x006EB420, GetOccupantPowerProduced),
    OPENSC4_SLOT(34, 0x006EB110, GetOccupantMaxPowerProduced),
    OPENSC4_SLOT(35, 0x006EFA50, FieldGetter<field::kSupplyGrid>),
    OPENSC4_SLOT(36, 0x006EFA50, FieldGetter<field::kSupplyGrid>),
    OPENSC4_SLOT(37, 0x006EB070, IsPowered),
    OPENSC4_SLOT(38, 0x006EB0A0, IsWired),
    OPENSC4_SLOT(40, 0x006EB130, GetChanceOfStrike),
    OPENSC4_SLOT(43, 0x006EB140, FieldGetter<field::kStrikeStart>),
    OPENSC4_SLOT(44, 0x006EB150, GetStrikeLength),
    OPENSC4_SLOT(45, 0x006EB5F0, GetCurrentPlantEfficiencyPercent),
    OPENSC4_SLOT(46, 0x006EC820, GetCurrentPlantConditionPercent),
    OPENSC4_SLOT(47, 0x006EFA60, GetStrikeEfficiencyPercent),
    OPENSC4_SLOT(48, 0x006EB180, GetFundingPercent),
    OPENSC4_SLOT(49, 0x006EB2F0, GetMonthlyPowerGenerationCost),
    OPENSC4_SLOT(50, 0x006EB520, GetPowerPlantFundingPercentage),
    OPENSC4_SLOT(51, 0x006EB580, SetPowerPlantFundingPercentage),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 52;

// Swaps every slot or none: each must still hold the game's function.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4PowerSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("power interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("power interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    const bool check = GetEnvironmentVariableA("OPENSC4_POWER_SELFCHECK", nullptr, 0) != 0;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(
            check && slot.index == 30 ? reinterpret_cast<void*>(&CheckFindPowerPlants) : slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("power interface: %u of %d cISC4PowerSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

using PlantOutputFn = uint32_t(__fastcall*)(void*, void*, void*);
PlantOutputFn g_originalPlantOutput = nullptr;

void* g_originalHelper[15] = {};

bool InstallHelpers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const struct {
        std::uintptr_t address;
        void* ours;
    } helpers[] = {
        {addr::cSC4PowerSimulator_CellRectAround, reinterpret_cast<void*>(&CellRectAround)},
        {addr::cSC4PowerSimulator_GrowRect, reinterpret_cast<void*>(&GrowRect)},
        {addr::cSC4PowerSimulator_AddDirtyRect, reinterpret_cast<void*>(&AddDirtyRect)},
        {addr::cSC4PowerSimulator_UpdateDemandTotal, reinterpret_cast<void*>(&UpdateDemandTotal)},
        {addr::cSC4PowerSimulator_UpdatePlantUsage, reinterpret_cast<void*>(&UpdatePlantUsage)},
        {addr::cSC4PowerSimulator_FillWiredRect, reinterpret_cast<void*>(&FillWiredRect)},
        {addr::cSC4PowerSimulator_ClearWiredRect, reinterpret_cast<void*>(&ClearWiredRect)},
        {addr::cSC4PowerSimulator_OccupantCell, reinterpret_cast<void*>(&OccupantCell)},
        {addr::cSC4PowerSimulator_OnNetworkCellChanged, reinterpret_cast<void*>(&OnNetworkCellChanged)},
        {addr::cSC4PowerSimulator_CreateBitArrays, reinterpret_cast<void*>(&CreateBitArrays)},
        {addr::cSC4PowerSimulator_PublishChanges, reinterpret_cast<void*>(&PublishChanges)},
        {addr::cSC4PowerSimulator_FindConnectedOccupant, reinterpret_cast<void*>(&FindConnectedOccupant)},
        {addr::cSC4PowerSimulator_AgePlant, reinterpret_cast<void*>(&AgePlant)},
        {addr::cSC4PowerSimulator_RecomputeArea, reinterpret_cast<void*>(&RecomputeArea)},
        {addr::cSC4PowerSimulator_DistributePower, reinterpret_cast<void*>(&DistributePower)},
    };
    const addr::Entry* entry = addr_check::Find(addr::cSC4PowerSimulator_PlantOutput);
    if (!entry || !hook::Install(*entry, &PlantOutput, &g_originalPlantOutput))
        return false;
    for (std::size_t i = 0; i < sizeof helpers / sizeof helpers[0]; ++i) {
        const addr::Entry* helper = addr_check::Find(helpers[i].address);
        if (!helper || !hook::Install(*helper, helpers[i].ours, &g_originalHelper[i]))
            return false;
    }
    return true;
}

const bool g_registeredHelpers = registry::Register({
    "simulators.power.helpers",
    "simulators/power",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_PlantOutput,
    "Our power plant output (capacity times efficiency) (docs/power.md)",
    false,
    InstallHelpers,
});

const bool g_registered = registry::Register({
    "simulators.power.interface",
    "simulators/power",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4PowerSimulator_vftable,
    "Our cISC4PowerSimulator getters on the original object (docs/power.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::power_sim
