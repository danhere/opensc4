// Our cSC4PollutionSimulator, first part: the cISC4PollutionSimulator methods
// that only read and write the simulator's state, reimplemented on the
// original object (docs/pollution.md).
//
// The original object stays: the game creates, loads, saves and ticks it as
// before. Replacement `simulators.pollution.interface` swaps 59 of the 71
// slots of its primary vtable (0x00AB2630) for the functions below, which
// work on the original's fields at the original's offsets. Vtable slots and
// not entry detours, because these methods are only reached through the
// vtable and several are compiler-folded with other classes' functions
// (0x006E3000 is shared by nine vtables).
//
// Each function does what the game's does, including what gzcom-dll's
// declarations hide: return registers carry whatever the game's code leaves
// in them where a caller could read it (the bool getters return the low byte
// of a value, the 64-bit garbage figures return EDX:EAX), and quirks such as
// GetGarbageValue's sign extension or GarbagePutInLandfill's clamps are kept.
// Callers that reach these through the vtable take `this` in ECX and pop the
// arguments, so the functions are __fastcall with an unused EDX.
#include "core/pollution_sim.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::pollution_sim {
namespace {

// --- Interface-specific constants ------------------------------------------

// Occupant properties the capacity getters read.
constexpr uint32_t kPropertyGarbageCapacity = 0x48EE7A44;
constexpr uint32_t kPropertyRecyclingCenter = 0x88ED1059;
constexpr uint32_t kPropertyWaterTreatment = 0xC8EBD131;
constexpr uint32_t kPropertyWaterTreatmentUsed = 0x28ECF510;
constexpr uint32_t kPropertyIncineratorCurrent = 0xA8EBCF0C;
constexpr uint32_t kPropertyIncineratorUsed = 0xE8ECF641;
constexpr uint32_t kPropertyWasteToEnergyCurrent = 0x48EBD0B1;
constexpr uint32_t kPropertyWasteToEnergyUsed = 0x88ECF76C;

// --- Slots ------------------------------------------------------------------

// 5 (0x006E3360)
void* __fastcall GetPollutionGrid(void* self, void*, uint32_t type)
{
    if (type == 3)
        return Grid(self, field::kRadiationGrid);
    return Grid(self, field::kAirGrid + type * 4);
}

// 6 (0x006E37E0): negative cell values read as 0.
uint16_t __fastcall GetPollutionValue(void* self, void*, uint32_t type, uint32_t x, uint32_t z)
{
    const int16_t value = GetCellValue(Grid(self, field::kValueGrids + type * 4), x, z);
    return value < 0 ? 0 : static_cast<uint16_t>(value);
}

// 7, 8 (0x006E2EF0, 0x006E2F10): the bool result is the value's low byte.
uint32_t __fastcall GetAirValue(void* self, void*, uint32_t x, uint32_t z, int32_t& value)
{
    value = GetCellValue(Grid(self, field::kAirGrid), x, z);
    return static_cast<uint32_t>(value);
}

uint32_t __fastcall GetWaterValue(void* self, void*, uint32_t x, uint32_t z, int32_t& value)
{
    value = GetCellValue(Grid(self, field::kWaterGrid), x, z);
    return static_cast<uint32_t>(value);
}

// 9 (0x006E2F30): clears the landfill bit after sign extension, so a cell
// with it set reads as a negative number.
uint32_t __fastcall GetGarbageValue(void* self, void*, uint32_t x, uint32_t z, int32_t& value)
{
    value = static_cast<int32_t>(GetCellValue(Grid(self, field::kGarbageGrid), x, z)) &
            static_cast<int32_t>(0xFFFF7FFF);
    return static_cast<uint32_t>(value);
}

// 10, 11, 12 (0x007624C0, 0x006E3010, 0x006E3020)
int32_t __fastcall GetAverageAirValue(void* self, void*) { return At<int32_t>(self, field::kAverageAir); }
int32_t __fastcall GetAverageWaterValue(void* self, void*) { return At<int32_t>(self, field::kAverageWater); }
int32_t __fastcall GetAverageGarbageValue(void* self, void*) { return At<uint16_t>(self, field::kAverageGarbage); }

// 13, 14, 15 (0x006E2F60...): the value against a tuning threshold,
// compared unsigned. The game passes its `z` argument's stack slot as the
// output, so that is the value if the getter left it alone.
uint8_t IsPolluted(void* self, std::size_t getter, uint32_t x, uint32_t z, std::uintptr_t threshold)
{
    int32_t value = static_cast<int32_t>(z);
    Virtual<uint32_t>(self, getter, x, z, &value);
    return static_cast<uint32_t>(value) >= Global<uint32_t>(threshold) ? 1 : 0;
}

uint8_t __fastcall IsAirPolluted(void* self, void*, uint32_t x, uint32_t z)
{
    return IsPolluted(self, kSlotGetAirValue, x, z, addr::PollutionTuning_AirPollutedThreshold);
}

uint8_t __fastcall IsWaterPolluted(void* self, void*, uint32_t x, uint32_t z)
{
    return IsPolluted(self, kSlotGetWaterValue, x, z, addr::PollutionTuning_WaterPollutedThreshold);
}

uint8_t __fastcall IsGarbageAccumulated(void* self, void*, uint32_t x, uint32_t z)
{
    return IsPolluted(self, kSlotGetGarbageValue, x, z, addr::PollutionTuning_GarbageAccumulatedThreshold);
}

// 16 (0x006E2ED0)
uint8_t __fastcall IsRadioactive(void* self, void*, uint32_t x, uint32_t z)
{
    return GetCellValue(Grid(self, field::kRadiationGrid), x, z) != 0 ? 1 : 0;
}

// 17, 18, 19 (0x006E4FA0...): the bool result is the value's low byte.
uint32_t WorstTract(void* self, std::size_t offset, uint32_t& x, uint32_t& z, int32_t& value)
{
    x = At<uint16_t>(self, offset);
    z = At<uint16_t>(self, offset + 2);
    value = At<int32_t>(self, offset + 4);
    return static_cast<uint32_t>(value);
}

uint32_t __fastcall GetWorstAirTract(void* self, void*, uint32_t& x, uint32_t& z, int32_t& value)
{
    return WorstTract(self, field::kWorstAirTract, x, z, value);
}

uint32_t __fastcall GetWorstWaterTract(void* self, void*, uint32_t& x, uint32_t& z, int32_t& value)
{
    return WorstTract(self, field::kWorstWaterTract, x, z, value);
}

uint32_t __fastcall GetWorstGarbageTract(void* self, void*, uint32_t& x, uint32_t& z, int32_t& value)
{
    return WorstTract(self, field::kWorstGarbageTract, x, z, value);
}

// 20 (0x006E3340): a negative supply is reset to 0 on read.
int32_t __fastcall GetGarbageSupply(void* self, void*)
{
    int32_t& supply = At<int32_t>(self, field::kGarbageSupply);
    if (supply < 0)
        supply = 0;
    return supply;
}

// 21, 22, 23, 24 (0x006E4F10, 0x006E5040, 0x006E2FF0, 0x006E3000)
int32_t __fastcall GetLandfillTileCapacity(void*, void*)
{
    return Global<int32_t>(addr::PollutionTuning_LandfillTileCapacity);
}

int32_t __fastcall GetGarbageCapacity(void* self, void*) { return At<int32_t>(self, field::kGarbageCapacity); }

uint8_t __fastcall IsGarbageUncollected(void* self, void*)
{
    return At<uint8_t>(self, field::kGarbageUncollected) != 0 ? 1 : 0;
}

int32_t __fastcall GetTotalGarbageUncollected(void* self, void*)
{
    return At<int32_t>(self, field::kTotalGarbageUncollected);
}

// 25 (0x006E3030): the cell is given in city cells, stored per tract. The
// result is whatever SetTractValue left in EAX.
uint32_t __fastcall SetIsRadioactive(void* self, void*, uint32_t x, uint32_t z, uint8_t radioactive)
{
    void* const grid = Grid(self, field::kRadiationGrid);
    const uint32_t shift = GetTractShift(grid) & 31;
    const uint32_t result = SetTractValue(grid, x >> shift, z >> shift, radioactive);
    At<uint8_t>(self, field::kRadiationChanged) = 1;
    return result;
}

// 26, 27 (0x006E3070, 0x006E30B0)
uint32_t SetValue(void* self, std::size_t gridOffset, std::size_t changedOffset, uint32_t x, uint32_t z,
                  int16_t value)
{
    void* const grid = Grid(self, gridOffset);
    const uint32_t shift = GetTractShift(grid) & 31;
    const uint32_t result = SetTractValue(grid, x >> shift, z >> shift, value);
    At<uint8_t>(self, changedOffset) = 1;
    return result;
}

uint32_t __fastcall SetAirValue(void* self, void*, uint32_t x, uint32_t z, int16_t value)
{
    return SetValue(self, field::kAirGrid, field::kAirChanged, x, z, value);
}

uint32_t __fastcall SetWaterValue(void* self, void*, uint32_t x, uint32_t z, int16_t value)
{
    return SetValue(self, field::kWaterGrid, field::kWaterChanged, x, z, value);
}

// 28 (0x006E30F0): keeps the cell's landfill bit.
uint32_t __fastcall SetGarbageValue(void* self, void*, uint32_t x, uint32_t z, int16_t value)
{
    void* const grid = Grid(self, field::kGarbageGrid);
    const uint32_t shift = GetTractShift(grid) & 31;
    const int16_t old = GetCellValue(grid, x, z);
    const auto merged = static_cast<int16_t>((old & 0x8000) | static_cast<uint16_t>(value));
    const uint32_t result = SetTractValue(grid, x >> shift, z >> shift, merged);
    At<uint8_t>(self, field::kGarbageChanged) = 1;
    return result;
}

// 29 (0x006E32E0): saturating add; the result is the amount actually added.
int32_t __fastcall AddToGarbageSupply(void* self, void*, int32_t garbage)
{
    int32_t& supply = At<int32_t>(self, field::kGarbageSupply);
    const auto sum = static_cast<int32_t>(static_cast<uint32_t>(supply) + static_cast<uint32_t>(garbage));
    if (garbage > 0) {
        if (sum < supply)
            garbage = static_cast<int32_t>(0x7FFFFFFFu - static_cast<uint32_t>(supply));
    } else if (garbage < 0) {
        if (sum < 0)
            garbage = static_cast<int32_t>(0u - static_cast<uint32_t>(supply));
    }
    supply = static_cast<int32_t>(static_cast<uint32_t>(supply) + static_cast<uint32_t>(garbage));
    if (supply < 0)
        supply = 0;
    return garbage;
}

// 30 (0x006E5050, folded with another class's setter)
int32_t __fastcall SetGarbageCapacity(void* self, void*, int32_t capacity)
{
    At<int32_t>(self, field::kGarbageCapacity) = capacity;
    return capacity;
}

// 31, 32, 33 (0x00423710, 0x00798370, 0x006E3150). gzcom-dll declares 33 as
// returning int32_t; the game returns a float*, which the Lua globals code
// (0x004220A0) reads for g_recycling_center_effect_p.
int32_t __fastcall GetMonthlyIncineratorCapacity(void* self, void*)
{
    return At<int32_t>(self, field::kMonthlyIncineratorCapacity);
}

int32_t __fastcall GetMonthlyWasteToEnergyCapacity(void* self, void*)
{
    return At<int32_t>(self, field::kMonthlyWasteToEnergyCapacity);
}

float* __fastcall GetRecyclingCenterEffect(void* self, void*)
{
    return &At<float>(self, field::kRecyclingCenterEffect);
}

// 34-41 (0x006E4F20-0x006E4F90): 64-bit results in EDX:EAX.
int64_t __fastcall GetGarbageAbductedPrevMonth(void* self, void*) { return At<uint32_t>(self, field::kAbductedPrevMonth); }
int64_t __fastcall GetGarbageProducedLastMonth(void* self, void*) { return At<int64_t>(self, field::kProducedLastMonth); }
int64_t __fastcall GetGarbageImportedLastMonth(void* self, void*) { return At<int64_t>(self, field::kImportedLastMonth); }
int64_t __fastcall GetGarbageExportedLastMonth(void* self, void*) { return At<int64_t>(self, field::kExportedLastMonth); }
// Truncated toward zero, as __ftol2 does.
int64_t __fastcall GetGarbageRecycledLastMonth(void* self, void*)
{
    return static_cast<int64_t>(At<double>(self, field::kRecycledLastMonth));
}
int64_t __fastcall GetGarbageIncineratedLastMonth(void* self, void*) { return At<int64_t>(self, field::kIncineratedLastMonth); }
int64_t __fastcall GetGarbageConvertedToEnergyLastMonth(void* self, void*) { return At<int64_t>(self, field::kConvertedLastMonth); }
int64_t __fastcall GetGarbageLandfilledLastMonth(void* self, void*) { return At<int64_t>(self, field::kLandfilledLastMonth); }

// 42-45 (0x006E50D0-0x006E5130). The names are gzcom-dll's; which list each
// counts is the game's.
uint32_t __fastcall GetIncineratorCount(void* self, void*) { return ListCount(self, field::kIncineratorList); }
uint32_t __fastcall GetWasteToEnergyBuildingCount(void* self, void*) { return ListCount(self, field::kWasteToEnergyList); }
uint32_t __fastcall GetRecyclingCenterCount(void* self, void*) { return ListCount(self, field::kRecyclingList); }
uint32_t __fastcall GetWaterTreatmentPlantCount(void* self, void*) { return ListCount(self, field::kWaterTreatmentList); }

// 47 (0x006E3160): the game uses its argument's stack slot as the output.
int32_t __fastcall GetBuildingGarbageCapacity(void*, void*, void* occupant)
{
    auto capacity = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(occupant));
    const uint8_t found = static_cast<uint8_t>(GetPropertySint32(PropertyHolder(occupant), kPropertyGarbageCapacity, &capacity));
    return found ? capacity : 0;
}

// 48 (0x006E3190)
float __fastcall GetGarbageScalingFactor(void*, void*)
{
    return Global<float>(addr::PollutionTuning_GarbageScalingFactor);
}

// 49 (0x006E31A0)
uint8_t __fastcall IsActiveLandfill(void* self, void*, uint32_t x, uint32_t z)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(GetCellValue(Grid(self, field::kGarbageGrid), x, z)) >> 15) & 1);
}

// 50 (0x006E31C0): only landfill-zoned cells can become active. Unlike the
// other setters it passes city cells straight to SetTractValue.
uint32_t __fastcall SetIsActiveLandfill(void* self, void*, uint32_t x, uint32_t z, uint8_t active)
{
    const uint32_t zone = Virtual<uint32_t>(At<void*>(self, field::kZoneManager), 0x30, x, z);
    if (active && zone != kZoneLandfill)
        return zone;
    const uint32_t was = Virtual<uint32_t>(self, kSlotIsActiveLandfill, x, z);
    const bool wasActive = (was & 0xFF) != 0;
    if (wasActive == (active != 0))
        return was;
    void* const grid = Grid(self, field::kGarbageGrid);
    const auto cell = static_cast<uint16_t>(GetCellValue(grid, x, z));
    uint32_t& tiles = At<uint32_t>(self, field::kActiveLandfillTiles);
    int16_t value;
    if (active) {
        value = static_cast<int16_t>(cell | 0x8000);
        ++tiles;
    } else {
        value = static_cast<int16_t>(cell & 0x7FFF);
        if (tiles != 0)
            --tiles;
    }
    const uint32_t result = SetTractValue(grid, x, z, value);
    At<uint8_t>(self, field::kGarbageChanged) = 1;
    return result;
}

// 51 (0x006E3250): adds to this month's landfilled garbage. The clamps
// compare against the old total instead of the limits they seem meant for,
// and the "overflow" clamp computes from the low word only. The result is
// EDX = the sign of the amount, EAX = the new total's high word.
uint64_t __fastcall GarbagePutInLandfill(void* self, void*, int32_t garbage)
{
    int64_t& total = At<int64_t>(self, field::kLandfilledThisMonth);
    const auto low = static_cast<uint32_t>(total);
    const int64_t sum = static_cast<int64_t>(static_cast<uint64_t>(total) + static_cast<uint64_t>(int64_t{garbage}));
    if (garbage > 0) {
        if (sum < total)
            garbage = static_cast<int32_t>(0xFFFFFFFFu - low);
    } else if (garbage < 0) {
        if (sum > total)
            garbage = static_cast<int32_t>(0u - low);
    }
    total = static_cast<int64_t>(static_cast<uint64_t>(total) + static_cast<uint64_t>(int64_t{garbage}));
    const uint32_t sign = garbage < 0 ? 0xFFFFFFFFu : 0;
    return (uint64_t{sign} << 32) | static_cast<uint32_t>(static_cast<uint64_t>(total) >> 32);
}

// 52 (0x006E2DA0)
uint32_t __fastcall GetMaxCapacity(void*, void*, uint32_t property, void* occupant, uint32_t& result)
{
    result = 0;
    return GetPropertyUint32(PropertyHolder(occupant), property, &result);
}

// 54 (0x006E2D70)
uint32_t __fastcall GetRecyclingCenterMaxCapacity(void*, void*, void* occupant, float& capacity)
{
    capacity = Global<float>(addr::PollutionTuning_DefaultPercentage);
    return GetPropertyPercentage(PropertyHolder(occupant), kPropertyRecyclingCenter, &capacity);
}

// --- 53, 55-57: capacity, condition and efficiency (x87) --------------------

// The game's curve (16 bytes): points (begin, end, capacity) and a count.
struct Curve {
    void* points = nullptr;
    void* end = nullptr;
    void* capacity = nullptr;
    int32_t count = 0;
};

using LoadCurveFromPropertyFn = uint8_t(__thiscall*)(Curve* curve, void* property);
using EvaluateCurveFn = double(__thiscall*)(const Curve* curve, float x); // ST0, unrounded
using GetPropertyFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using FreeFn = void(__cdecl*)(void* p);
const auto LoadCurveFromProperty = reinterpret_cast<LoadCurveFromPropertyFn>(addr::SC4Curve_LoadFromProperty);
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);
const auto GetPropertyFloat = reinterpret_cast<GetPropertyFloatFn>(addr::SC4_GetPropertyFloat);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr uint32_t kPropertyConditionCurve = 0x8A1E07EE; // a curve of condition by age
constexpr uint32_t kPropertyAge = 0xC910B4CF;
constexpr std::size_t kCapacityScale = 0x228; // float, from the department's funding

// __ftol2: truncation toward zero; callers keep the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The game's rounding idiom: add 1.5 * 2^23, store as float, take the bits
// minus 0x4B400000.
int32_t MagicRound(double value)
{
    const float f = static_cast<float>(value + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return static_cast<int32_t>(bits + 0xB4C00000u);
}

// The building's condition curve (property 0x8A1E07EE) at its age, or
// false without the curve.
bool ConditionByAge(void* holder, double& factor)
{
    void* const property = Virtual<void*>(holder, 0x24, kPropertyConditionCurve);
    if (!property)
        return false;
    Curve curve;
    LoadCurveFromProperty(&curve, property);
    float age = 0.0f;
    GetPropertyFloat(holder, kPropertyAge, &age);
    factor = EvaluateCurve(&curve, age);
    if (curve.points)
        Free(curve.points);
    return true;
}

// 53 (0x006E4300): the property, 0 unless the building works (the service
// at +0x34, slot 31), times the capacity scale, then times its condition.
uint32_t __fastcall GetCurrentCapacity(void* self, void*, uint32_t property, void* occupant, uint32_t& capacity)
{
    if (!static_cast<uint8_t>(GetPropertyUint32(PropertyHolder(occupant), property, &capacity))) {
        capacity = 0;
        return 0;
    }
    if (!Virtual<uint8_t>(At<void*>(self, 0x34), 0x7C, occupant))
        capacity = 0;
    capacity = static_cast<uint32_t>(Ftol(static_cast<double>(capacity) * At<float>(self, kCapacityScale)));
    void* const holder = PropertyHolder(occupant);
    void* const curveProperty = Virtual<void*>(holder, 0x24, kPropertyConditionCurve);
    if (curveProperty) {
        Curve curve;
        LoadCurveFromProperty(&curve, curveProperty);
        float age = 0.0f;
        GetPropertyFloat(holder, kPropertyAge, &age);
        const double factor = EvaluateCurve(&curve, age);
        capacity = static_cast<uint32_t>(MagicRound(factor * static_cast<double>(capacity)));
        if (curve.points)
            Free(curve.points);
    }
    return 1;
}

// 55 (0x006E3720): GetCurrentCapacity of property 0x88ED1059 / 100. The
// game passes its own argument's stack slot as the output.
uint32_t __fastcall GetRecyclingCenterCurrentCapacity(void* self, void*, void* occupant, float& capacity)
{
    uint32_t current = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(occupant));
    const uint32_t result = Virtual<uint32_t>(self, kSlotGetCurrentCapacity, kPropertyRecyclingCenter, occupant, &current);
    if (static_cast<uint8_t>(result))
        capacity = static_cast<float>(static_cast<double>(current) * static_cast<double>(0.01f));
    else
        capacity = Global<float>(addr::PollutionTuning_DefaultPercentage);
    return result;
}

// 56 (0x006E4410): 1 (0x00B0D408) times the condition curve at the age.
uint32_t __fastcall GetCurrentOccupantCondition(void*, void*, void* occupant, float& condition)
{
    condition = Global<float>(addr::PollutionTuning_DefaultCondition);
    double factor = 0.0;
    if (!ConditionByAge(PropertyHolder(occupant), factor))
        return 0;
    condition = static_cast<float>(factor * condition);
    return 1;
}

// 57 (0x006E3770): the capacity scale times the condition, for a building
// the service at 0x00B43DA0 (slot 31) accepts.
uint32_t __fastcall GetCurrentOccupantEfficiency(void* self, void*, void* occupant, float& efficiency)
{
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA0), 0x7C, occupant)) {
        efficiency = At<float>(self, kCapacityScale);
        float condition = 0.0f;
        if (Virtual<uint8_t>(self, 0xE0, occupant, &condition)) {
            efficiency = static_cast<float>(static_cast<double>(condition) * efficiency);
            return 1;
        }
    }
    efficiency = Global<float>(addr::PollutionTuning_DefaultPercentage);
    return 0;
}

// --- 65, 66 ------------------------------------------------------------------

using GetPropertySint64Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int64_t* value);
using SpreadFn = void(__cdecl*)(void* data, int32_t tractsX, int32_t tractsZ, int32_t tractX, int32_t tractZ,
                                float radius, float value, float edge);
using PostMessageFn = void(__cdecl*)(uint32_t message, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
using ThisFn = void(__thiscall*)(void* self);
const auto GetPropertySint64 = reinterpret_cast<GetPropertySint64Fn>(addr::SC4_GetPropertySint64);
const auto SpreadAround = reinterpret_cast<SpreadFn>(addr::SC4SimGrid_SpreadAround);
const auto PostMessage = reinterpret_cast<PostMessageFn>(addr::SC4_PostMessage);
const auto UpdateWorstTractsAndEffects = reinterpret_cast<ThisFn>(addr::cSC4PollutionSimulator_UpdateWorstTractsAndEffects);

// 65 (0x006E4530): the maintenance cost property (0x2A58D350) times the
// maintenance curve (0x0911E2E7) at the building's age, truncated; false
// without any of the three.
uint32_t __fastcall CalcOccupantMaintenanceCost(void*, void*, void* occupant, int64_t& cost)
{
    void* const holder = PropertyHolder(occupant);
    float age = 0.0f;
    cost = 0;
    if (!GetPropertySint64(holder, 0x2A58D350, &cost) || !GetPropertyFloat(holder, kPropertyAge, &age))
        return 0;
    void* const curveProperty = Virtual<void*>(holder, 0x24, 0x0911E2E7u);
    if (!curveProperty)
        return 0;
    Curve curve;
    LoadCurveFromProperty(&curve, curveProperty);
    const double factor = EvaluateCurve(&curve, age);
    cost = static_cast<int64_t>(static_cast<double>(cost) * factor);
    if (curve.points)
        Free(curve.points);
    return 1;
}

struct Vector3 {
    float x, y, z;
};

// 66 (0x006E7040): radiation spread around a point, as a building's
// footprint is (OnOccupantInsertedOrRemoved); then the worst tracts and
// effects, and the radiation-changed message.
void __fastcall AddRadiationToCity(void* self, void*, const Vector3& center, float amount, float radius)
{
    if (!(radius > 0.0f) || amount == 0.0f)
        return;
    void* const grid = Grid(self, field::kRadiationGrid);
    int32_t tractX, tractZ;
    Virtual<uint32_t>(grid, 0x4C, center.x, center.z, &tractX, &tractZ); // PositionToTract
    const int32_t tractsX = Virtual<int32_t>(grid, 0x30);
    const int32_t tractsZ = Virtual<int32_t>(grid, 0x34);
    Virtual<uint32_t>(grid, 0x28);
    SpreadAround(Virtual<void*>(grid, 0x60), tractsX, tractsZ, tractX, tractZ, radius, amount, 0.0f);
    UpdateWorstTractsAndEffects(self);
    PostMessage(0xDA6DD35D, 0, 0, 0, 0, 0);
}

// With OPENSC4_POLLUTION_SELFCHECK set, each of 53, 55-57 also calls the
// game's function on the same arguments and compares (they change nothing
// but their output; the condition curve's allocation is freed again).
int g_checks = 0;
int g_checkDifferences = 0;

void Checked(const char* name, bool same, uint32_t ours, uint32_t theirs)
{
    ++g_checks;
    if (!same && g_checkDifferences++ < 20)
        log::Write("pollution interface: %s differs: ours %08X, the game's %08X", name, ours, theirs);
    if ((g_checks & (g_checks - 1)) == 0)
        log::Write("pollution interface: %d checked calls, %d differing", g_checks, g_checkDifferences);
}

template <typename Out>
uint32_t Bits(const Out& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint32_t __fastcall CheckGetCurrentCapacity(void* self, void* edx, uint32_t property, void* occupant, uint32_t& capacity)
{
    using Fn = uint32_t(__fastcall*)(void*, void*, uint32_t, void*, uint32_t&);
    uint32_t theirs = 0;
    const uint32_t theirResult = reinterpret_cast<Fn>(0x006E4300)(self, edx, property, occupant, theirs);
    const uint32_t result = GetCurrentCapacity(self, edx, property, occupant, capacity);
    Checked("GetCurrentCapacity", capacity == theirs && static_cast<uint8_t>(result) == static_cast<uint8_t>(theirResult),
            capacity, theirs);
    return result;
}

template <uint32_t (__fastcall* Ours)(void*, void*, void*, float&), std::uintptr_t Original>
uint32_t __fastcall CheckFloat(void* self, void* edx, void* occupant, float& value)
{
    using Fn = uint32_t(__fastcall*)(void*, void*, void*, float&);
    float theirs = 0.0f;
    const uint32_t theirResult = reinterpret_cast<Fn>(Original)(self, edx, occupant, theirs);
    const uint32_t result = Ours(self, edx, occupant, value);
    Checked("a float getter", Bits(value) == Bits(theirs) && static_cast<uint8_t>(result) == static_cast<uint8_t>(theirResult),
            Bits(value), Bits(theirs));
    return result;
}

uint32_t __fastcall CheckCalcOccupantMaintenanceCost(void* self, void* edx, void* occupant, int64_t& cost)
{
    using Fn = uint32_t(__fastcall*)(void*, void*, void*, int64_t&);
    int64_t theirs = 0;
    const uint32_t theirResult = reinterpret_cast<Fn>(0x006E4530)(self, edx, occupant, theirs);
    const uint32_t result = CalcOccupantMaintenanceCost(self, edx, occupant, cost);
    Checked("CalcOccupantMaintenanceCost", cost == theirs && static_cast<uint8_t>(result) == static_cast<uint8_t>(theirResult),
            static_cast<uint32_t>(cost), static_cast<uint32_t>(theirs));
    return result;
}

// 46 (0x006E4C70): appends the incinerators, waste-to-energy plants and
// recycling centers (not the water treatment plants) to the caller's list,
// taking a reference to each. The game returns whatever EAX last held.
void* __fastcall GetGarbageBuildings(void* self, void*, void* list)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("pollution interface: first GetGarbageBuildings");
    }
    void* last = nullptr;
    for (const std::size_t offset : {field::kIncineratorList, field::kWasteToEnergyList, field::kRecyclingList}) {
        void* const sentinel = At<void*>(self, offset);
        for (void* node = NextNode(sentinel); node != sentinel; node = NextNode(node)) {
            Virtual<uint32_t>(NodeObject(node), 0x4); // AddRef
            void** const end = *static_cast<void***>(list);
            auto** const added = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
            added[2] = NodeObject(node);
            void** const before = static_cast<void**>(end[1]);
            added[0] = end;
            added[1] = before;
            before[0] = added;
            end[1] = added;
            last = added;
        }
    }
    return last;
}

// The self-check for 46: the game's function into a scratch list, ours
// into the caller's, the same objects in the same order; the scratch
// list's references released and its nodes freed.
void* __fastcall CheckGetGarbageBuildings(void* self, void* edx, void* list)
{
    using Fn = void*(__fastcall*)(void*, void*, void*);
    using DestroyFn = void(__thiscall*)(void* list);
    void** const scratch = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
    scratch[0] = scratch;
    scratch[1] = scratch;
    void* scratchList = scratch;
    reinterpret_cast<Fn>(0x006E4C70)(self, edx, &scratchList);
    void* const before = static_cast<void**>(*static_cast<void**>(list))[1]; // the caller's last node
    void* const result = GetGarbageBuildings(self, edx, list);
    void* ours = NextNode(before);
    bool same = true;
    int count = 0;
    for (void* theirs = NextNode(scratch); theirs != scratch; theirs = NextNode(theirs), ours = NextNode(ours)) {
        same &= ours != *static_cast<void**>(list) && NodeObject(ours) == NodeObject(theirs);
        Virtual<uint32_t>(NodeObject(theirs), 0x8); // Release
        ++count;
    }
    same &= ours == *static_cast<void**>(list);
    reinterpret_cast<DestroyFn>(addr::SC4List_Destroy2)(&scratchList);
    log::Write("pollution interface: GetGarbageBuildings check: %s, %d buildings", same ? "same" : "DIFFERENT", count);
    return result;
}

// 58, 59 (0x006E2E70, 0x006E2EA0)
uint32_t __fastcall GetIncineratorUsedCapacity(void*, void*, void* occupant, uint32_t& capacity)
{
    capacity = 0;
    return GetPropertyUint32(PropertyHolder(occupant), kPropertyIncineratorUsed, &capacity);
}

uint32_t __fastcall GetWasteToEnergyUsedCapacity(void*, void*, void* occupant, uint32_t& capacity)
{
    capacity = 0;
    return GetPropertyUint32(PropertyHolder(occupant), kPropertyWasteToEnergyUsed, &capacity);
}

// 60-64 (0x006E2DD0-0x006E2E50): GetMaxCapacity or GetCurrentCapacity
// through the vtable with a fixed property.
uint32_t Capacity(void* self, std::size_t slot, uint32_t property, void* occupant, uint32_t& capacity)
{
    capacity = 0;
    return Virtual<uint32_t>(self, slot, property, occupant, &capacity);
}

uint32_t __fastcall GetIncineratorCurrentCapacity(void* self, void*, void* occupant, uint32_t& capacity)
{
    return Capacity(self, kSlotGetCurrentCapacity, kPropertyIncineratorCurrent, occupant, capacity);
}

uint32_t __fastcall GetWasteToEnergyCurrentCapacity(void* self, void*, void* occupant, uint32_t& capacity)
{
    return Capacity(self, kSlotGetCurrentCapacity, kPropertyWasteToEnergyCurrent, occupant, capacity);
}

uint32_t __fastcall GetWaterTreatmentMaxCapacity(void* self, void*, void* occupant, uint32_t& capacity)
{
    return Capacity(self, kSlotGetMaxCapacity, kPropertyWaterTreatment, occupant, capacity);
}

uint32_t __fastcall GetWaterTreatmentCurrentCapacity(void* self, void*, void* occupant, uint32_t& capacity)
{
    return Capacity(self, kSlotGetCurrentCapacity, kPropertyWaterTreatment, occupant, capacity);
}

uint32_t __fastcall GetWaterTreatmentUsedCapacity(void* self, void*, void* occupant, uint32_t& capacity)
{
    return Capacity(self, kSlotGetMaxCapacity, kPropertyWaterTreatmentUsed, occupant, capacity);
}

// 67-70 (0x006E5060-0x006E5090)
int32_t __fastcall GetMaxAirPollutionLevelForUI(void*, void*) { return Global<int32_t>(addr::PollutionTuning_MaxAirLevelForUI); }
int32_t __fastcall GetMaxWaterPollutionLevelForUI(void*, void*) { return Global<int32_t>(addr::PollutionTuning_MaxWaterLevelForUI); }
int32_t __fastcall GetMaxGarbagePollutionLevelForUI(void*, void*) { return Global<int32_t>(addr::PollutionTuning_MaxGarbageLevelForUI); }
int32_t __fastcall GetMaxRadiationPollutionLevelForUI(void*, void*) { return Global<int32_t>(addr::PollutionTuning_MaxRadiationLevelForUI); }

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

// Not replaced: 0-2 (QueryInterface, AddRef, Release; the object's
// identity); 3 Init and 4 Shutdown are entry detours (pollution_init.cpp).
const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x006E3360, GetPollutionGrid),
    OPENSC4_SLOT(6, 0x006E37E0, GetPollutionValue),
    OPENSC4_SLOT(7, 0x006E2EF0, GetAirValue),
    OPENSC4_SLOT(8, 0x006E2F10, GetWaterValue),
    OPENSC4_SLOT(9, 0x006E2F30, GetGarbageValue),
    OPENSC4_SLOT(10, 0x007624C0, GetAverageAirValue),
    OPENSC4_SLOT(11, 0x006E3010, GetAverageWaterValue),
    OPENSC4_SLOT(12, 0x006E3020, GetAverageGarbageValue),
    OPENSC4_SLOT(13, 0x006E2F60, IsAirPolluted),
    OPENSC4_SLOT(14, 0x006E2F90, IsWaterPolluted),
    OPENSC4_SLOT(15, 0x006E2FC0, IsGarbageAccumulated),
    OPENSC4_SLOT(16, 0x006E2ED0, IsRadioactive),
    OPENSC4_SLOT(17, 0x006E4FA0, GetWorstAirTract),
    OPENSC4_SLOT(18, 0x006E4FD0, GetWorstWaterTract),
    OPENSC4_SLOT(19, 0x006E5000, GetWorstGarbageTract),
    OPENSC4_SLOT(20, 0x006E3340, GetGarbageSupply),
    OPENSC4_SLOT(21, 0x006E4F10, GetLandfillTileCapacity),
    OPENSC4_SLOT(22, 0x006E5040, GetGarbageCapacity),
    OPENSC4_SLOT(23, 0x006E2FF0, IsGarbageUncollected),
    OPENSC4_SLOT(24, 0x006E3000, GetTotalGarbageUncollected),
    OPENSC4_SLOT(25, 0x006E3030, SetIsRadioactive),
    OPENSC4_SLOT(26, 0x006E3070, SetAirValue),
    OPENSC4_SLOT(27, 0x006E30B0, SetWaterValue),
    OPENSC4_SLOT(28, 0x006E30F0, SetGarbageValue),
    OPENSC4_SLOT(29, 0x006E32E0, AddToGarbageSupply),
    OPENSC4_SLOT(30, 0x006E5050, SetGarbageCapacity),
    OPENSC4_SLOT(31, 0x00423710, GetMonthlyIncineratorCapacity),
    OPENSC4_SLOT(32, 0x00798370, GetMonthlyWasteToEnergyCapacity),
    OPENSC4_SLOT(33, 0x006E3150, GetRecyclingCenterEffect),
    OPENSC4_SLOT(34, 0x006E4F20, GetGarbageAbductedPrevMonth),
    OPENSC4_SLOT(35, 0x006E4F30, GetGarbageProducedLastMonth),
    OPENSC4_SLOT(36, 0x006E4F40, GetGarbageImportedLastMonth),
    OPENSC4_SLOT(37, 0x006E4F50, GetGarbageExportedLastMonth),
    OPENSC4_SLOT(38, 0x006E4F60, GetGarbageRecycledLastMonth),
    OPENSC4_SLOT(39, 0x006E4F70, GetGarbageIncineratedLastMonth),
    OPENSC4_SLOT(40, 0x006E4F80, GetGarbageConvertedToEnergyLastMonth),
    OPENSC4_SLOT(41, 0x006E4F90, GetGarbageLandfilledLastMonth),
    OPENSC4_SLOT(42, 0x006E50D0, GetIncineratorCount),
    OPENSC4_SLOT(43, 0x006E50F0, GetWasteToEnergyBuildingCount),
    OPENSC4_SLOT(44, 0x006E5110, GetRecyclingCenterCount),
    OPENSC4_SLOT(45, 0x006E5130, GetWaterTreatmentPlantCount),
    OPENSC4_SLOT(46, 0x006E4C70, GetGarbageBuildings),
    OPENSC4_SLOT(47, 0x006E3160, GetBuildingGarbageCapacity),
    OPENSC4_SLOT(48, 0x006E3190, GetGarbageScalingFactor),
    OPENSC4_SLOT(49, 0x006E31A0, IsActiveLandfill),
    OPENSC4_SLOT(50, 0x006E31C0, SetIsActiveLandfill),
    OPENSC4_SLOT(51, 0x006E3250, GarbagePutInLandfill),
    OPENSC4_SLOT(52, 0x006E2DA0, GetMaxCapacity),
    OPENSC4_SLOT(53, 0x006E4300, GetCurrentCapacity),
    OPENSC4_SLOT(54, 0x006E2D70, GetRecyclingCenterMaxCapacity),
    OPENSC4_SLOT(55, 0x006E3720, GetRecyclingCenterCurrentCapacity),
    OPENSC4_SLOT(56, 0x006E4410, GetCurrentOccupantCondition),
    OPENSC4_SLOT(57, 0x006E3770, GetCurrentOccupantEfficiency),
    OPENSC4_SLOT(58, 0x006E2E70, GetIncineratorUsedCapacity),
    OPENSC4_SLOT(59, 0x006E2EA0, GetWasteToEnergyUsedCapacity),
    OPENSC4_SLOT(60, 0x006E2DD0, GetIncineratorCurrentCapacity),
    OPENSC4_SLOT(61, 0x006E2DF0, GetWasteToEnergyCurrentCapacity),
    OPENSC4_SLOT(62, 0x006E2E10, GetWaterTreatmentMaxCapacity),
    OPENSC4_SLOT(63, 0x006E2E30, GetWaterTreatmentCurrentCapacity),
    OPENSC4_SLOT(64, 0x006E2E50, GetWaterTreatmentUsedCapacity),
    OPENSC4_SLOT(65, 0x006E4530, CalcOccupantMaintenanceCost),
    OPENSC4_SLOT(66, 0x006E7040, AddRadiationToCity),
    OPENSC4_SLOT(67, 0x006E5060, GetMaxAirPollutionLevelForUI),
    OPENSC4_SLOT(68, 0x006E5070, GetMaxWaterPollutionLevelForUI),
    OPENSC4_SLOT(69, 0x006E5080, GetMaxGarbagePollutionLevelForUI),
    OPENSC4_SLOT(70, 0x006E5090, GetMaxRadiationPollutionLevelForUI),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 71;

// Swaps every slot or none: each must still hold the game's function.
bool Install(registry::Mode)
{
    if (!game::IsBuild641()) {
        log::Write("pollution interface: not build 641, not installing");
        return false;
    }
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4PollutionSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("pollution interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("pollution interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    const bool check = GetEnvironmentVariableA("OPENSC4_POLLUTION_SELFCHECK", nullptr, 0) != 0;
    for (const Slot& slot : kSlots) {
        void* ours = slot.ours;
        if (check && slot.index == 53)
            ours = reinterpret_cast<void*>(&CheckGetCurrentCapacity);
        else if (check && slot.index == 55)
            ours = reinterpret_cast<void*>(&CheckFloat<&GetRecyclingCenterCurrentCapacity, 0x006E3720>);
        else if (check && slot.index == 56)
            ours = reinterpret_cast<void*>(&CheckFloat<&GetCurrentOccupantCondition, 0x006E4410>);
        else if (check && slot.index == 57)
            ours = reinterpret_cast<void*>(&CheckFloat<&GetCurrentOccupantEfficiency, 0x006E3770>);
        else if (check && slot.index == 46)
            ours = reinterpret_cast<void*>(&CheckGetGarbageBuildings);
        else if (check && slot.index == 65)
            ours = reinterpret_cast<void*>(&CheckCalcOccupantMaintenanceCost);
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(ours);
    }
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("pollution interface: %u of %d cISC4PollutionSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.pollution.interface",
    "simulators/pollution",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_vftable,
    "Our cISC4PollutionSimulator getters and setters on the original object (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim
