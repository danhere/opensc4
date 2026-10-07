// Our cSC4PollutionSimulator, second part: SimulateTracts (0x006E7FD0), the
// handler of message 0x66956814 and the simulation itself, reimplemented on
// the original object (docs/pollution.md, "The tract walk").
//
// One call: garbage capacity left in the water treatment, incinerator and
// waste-to-energy lists; garbage from the neighbor deals; then the next
// +0x1D4 tracts of the city: targets for air, water and garbage from the
// source grids, the zones and traffic, the garbage moved off the tract into
// disposal, landfills or the supply, and the air and water values moved
// toward their targets by at most a tuning step per visit.
//
// The helpers it calls go through the game's addresses, so each runs ours
// or the game's as its own replacement says. x87 arithmetic follows docs/pollution.md, "Floating point":
// each step as double, rounded to float where the game stores a float.
#include "core/pollution_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstring>

namespace opensc4::pollution_sim {
namespace {

// More fields (see pollution_sim.h for the rest).
constexpr std::size_t kOrdinances = 0x20;      // cISC4OrdinanceSimulator*: per-zone factors
constexpr std::size_t kTraffic = 0x28;         // cISC4TrafficSimulator*
constexpr std::size_t kNeighbors = 0x30;       // cISC4NeighborsSimulator*: garbage deals
constexpr std::size_t kCursorX = 0x60;         // the walk's position, in cells (even)
constexpr std::size_t kCursorZ = 0x64;
constexpr std::size_t kSumAirTarget = 0x108;   // int32 sums over the visits
constexpr std::size_t kSumWaterLeft = 0x10C;
constexpr std::size_t kSumWaterTarget = 0x110;
constexpr std::size_t kSumWaterTreated = 0x114;
constexpr std::size_t kTreatmentRatioDivisor = 0x118;
constexpr std::size_t kTreatmentRatioDividend = 0x11C;
constexpr std::size_t kZonedTracts = 0x120;
constexpr std::size_t kSumRadiationSource = 0x124;
constexpr std::size_t kProducedThisMonth = 0x140; // int64
constexpr std::size_t kImportedThisMonth = 0x148; // int64
constexpr std::size_t kExportedThisMonth = 0x150; // int64
constexpr std::size_t kIncineratedThisMonth = 0x160; // int64
constexpr std::size_t kConvertedThisMonth = 0x168;   // int64: converted to energy
constexpr std::size_t kAbductedThisMonth = 0x1B4;    // uint32
constexpr std::size_t kLandfills = 0x3C;             // the service at 0x00B43D28: landfill use and size
constexpr std::size_t kCapacityScale = 0x228;        // float
constexpr std::size_t kDepartment = 0xD8;            // cISC4DepartmentBudget*; slot 20 is its line item for disposal
constexpr std::size_t kDisposalCostPerUnit = 0x1D0;  // float
constexpr std::size_t kDisposalCostLastMonth = 0x1C8; // int64
constexpr std::size_t kRecycledThisMonth = 0x158; // double
constexpr std::size_t kExportDealCapacity = 0x1B0; // int32: what the export deals accept this month
constexpr std::size_t kTractsPerCall = 0x1D4;
constexpr std::size_t kTractIterator = 0x1D8;     // its first byte is the state; 2 = finished

// Slots of the simulator's own interface called through its vtable.
constexpr std::size_t kSlotSetGarbageValue = 0x70;
constexpr std::size_t kSlotAddToGarbageSupply = 0x74;
constexpr std::size_t kSlotSetAirValue = 0x68;
constexpr std::size_t kSlotSetWaterValue = 0x6C;

constexpr int16_t kMaxCell = 0x7FFF;
constexpr double kMaxCellF = 32767.0; // 0x00AB274C

// Neighbor deal types (cISC4NeighborsSimulator slot 30 fills a list with them).
constexpr uint32_t kDealGarbageImport = 0x4773FFE5;
constexpr uint32_t kDealGarbageExport = 0x4773FFE4;

using DisposeGarbageFn = double(__thiscall*)(void* self, float amount, uint32_t* incineratorLeft,
                                             uint32_t* wasteToEnergyLeft);
using SpreadUsedCapacityFn = void(__thiscall*)(void* self, void* list, uint32_t currentProperty,
                                               uint32_t usedProperty, uint32_t total, int32_t* used, int32_t count);
using ListFn = void(__thiscall*)(void* self, void* list);
using RestartFn = void(__thiscall*)(void* iterator, int32_t mode);
using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
using ThisFn = void(__thiscall*)(void* self);
using GetFrameworkFn = void*(__cdecl*)();
using ReleaseRefFn = void(__thiscall*)(void** ref);

const auto DisposeGarbage = reinterpret_cast<DisposeGarbageFn>(addr::cSC4PollutionSimulator_DisposeGarbage);
const auto SpreadUsedCapacity = reinterpret_cast<SpreadUsedCapacityFn>(addr::cSC4PollutionSimulator_SpreadUsedCapacity);
const auto UpdateMaintenanceCosts = reinterpret_cast<ListFn>(addr::cSC4PollutionSimulator_UpdateMaintenanceCosts);
const auto OnTractWalkRestart = reinterpret_cast<ThisFn>(addr::cSC4PollutionSimulator_OnTractWalkRestart);
const auto RestartTracts = reinterpret_cast<RestartFn>(addr::TractIterator_Restart);
const auto NextTract = reinterpret_cast<NextFn>(addr::TractIterator_Next);
const auto ClearList = reinterpret_cast<ThisFn>(addr::SC4List_Clear);
const auto GetFramework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework);
const auto ReleaseRef = reinterpret_cast<ReleaseRefFn>(addr::SC4_ReleaseRef);

// __ftol2: truncation toward zero; callers keep the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The game's rounding idiom: add 1.5 * 2^23, store as float, take the bits
// minus 0x4B400000. Nearest integer, ties to even, for |value| < 2^22.
int32_t MagicRound(double value)
{
    const float f = static_cast<float>(value + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return static_cast<int32_t>(bits + 0xB4C00000u);
}

// a - b with 32-bit wraparound, as the game's SUB.
int32_t Minus(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

double Unsigned(uint32_t value)
{
    return static_cast<double>(value);
}

// The list node allocator, fetched from the framework on first use as the
// game's list code does.
void* NodeAllocator()
{
    void*& allocator = Global<void*>(addr::SC4List_NodeAllocator);
    if (!allocator) {
        void* fetched = nullptr;
        if (void* framework = GetFramework())
            Virtual<uint8_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, &fetched);
        allocator = fetched;
        ReleaseRef(&fetched);
    }
    return allocator;
}

// Capacity left in one building list: the sum over its buildings of the
// current capacity (GetCurrentCapacity, through the vtable) minus the used
// capacity (a property), when both are known.
struct Capacity {
    uint32_t left = 0;
    int32_t count = 0;
};

Capacity CapacityLeft(void* self, std::size_t list, uint32_t currentProperty, uint32_t usedProperty)
{
    Capacity result;
    void* const sentinel = At<void*>(self, list);
    for (void* node = NextNode(sentinel); node != sentinel; node = NextNode(node)) {
        void* const occupant = NodeObject(node);
        uint32_t current;
        uint32_t used = 0;
        if (Virtual<uint8_t>(self, kSlotGetCurrentCapacity, currentProperty, occupant, &current) &&
            static_cast<uint8_t>(GetPropertyUint32(PropertyHolder(occupant), usedProperty, &used)))
            result.left += current >= used ? current - used : 0;
        ++result.count;
    }
    return result;
}

// The incinerator and waste-to-energy lists also total their current
// capacity into a field first.
Capacity CapacityLeftWithTotal(void* self, std::size_t list, uint32_t currentProperty, uint32_t usedProperty,
                               std::size_t totalField)
{
    Capacity result;
    At<uint32_t>(self, totalField) = 0;
    void* const sentinel = At<void*>(self, list);
    for (void* node = NextNode(sentinel); node != sentinel; node = NextNode(node)) {
        uint32_t current;
        if (Virtual<uint8_t>(self, kSlotGetCurrentCapacity, currentProperty, NodeObject(node), &current))
            At<uint32_t>(self, totalField) += current;
        void* const occupant = NodeObject(node);
        uint32_t current2;
        uint32_t used = 0;
        if (Virtual<uint8_t>(self, kSlotGetCurrentCapacity, currentProperty, occupant, &current2) &&
            static_cast<uint8_t>(GetPropertyUint32(PropertyHolder(occupant), usedProperty, &used)))
            result.left += current2 >= used ? current2 - used : 0;
        ++result.count;
    }
    return result;
}

void SetGarbage(void* self, uint32_t x, uint32_t z, int32_t value)
{
    Virtual<uint32_t>(self, kSlotSetGarbageValue, x, z, static_cast<int16_t>(value));
}

void AddToSupply(void* self, int32_t garbage)
{
    Virtual<uint32_t>(self, kSlotAddToGarbageSupply, garbage);
}

int32_t ValueAt(void* self, std::size_t getter, uint32_t x, uint32_t z)
{
    int32_t value;
    Virtual<uint32_t>(self, getter, x, z, &value);
    return value;
}

// One cell's garbage after the tract's target is applied (the inner loop of
// SimulateTracts for a cell that is not a landfill).
void SettleGarbage(void* self, uint32_t x, uint32_t z, int32_t garbageTarget, int32_t zoned, int32_t cellGarbage,
                   uint32_t* incineratorLeft, uint32_t* wasteToEnergyLeft)
{
    const float target = static_cast<float>(static_cast<int16_t>(garbageTarget));
    const float zones = static_cast<float>(zoned);
    const double share = static_cast<double>(target) / static_cast<double>(zones);
    const int32_t produced = MagicRound(share);
    At<int64_t>(self, kProducedThisMonth) += produced;

    double rest = share;
    const float effect = At<float>(self, field::kRecyclingCenterEffect);
    if (static_cast<double>(effect) > static_cast<double>(Global<float>(addr::PollutionTuning_DefaultPercentage)) &&
        share > 0.0) {
        double recycled = share * static_cast<double>(effect);
        if (recycled > share)
            recycled = share;
        At<double>(self, kRecycledThisMonth) = recycled + At<double>(self, kRecycledThisMonth);
        rest = share - recycled;
    }
    const float total = static_cast<float>(static_cast<double>(cellGarbage) + rest);
    SetGarbage(self, x, z, 0);
    uint32_t& uncollected = At<uint32_t>(self, field::kTotalGarbageUncollected);
    if (uncollected >= static_cast<uint32_t>(cellGarbage))
        uncollected -= static_cast<uint32_t>(cellGarbage);

    const double leftExact = static_cast<double>(total) - DisposeGarbage(self, total, incineratorLeft, wasteToEnergyLeft);
    float left = static_cast<float>(leftExact);
    if (!(leftExact > 0.0))
        return;

    if (static_cast<double>(left) > kMaxCellF) {
        left = static_cast<float>(static_cast<double>(left) - kMaxCellF);
        SetGarbage(self, x, z, kMaxCell);
        uncollected += kMaxCell;
        if (static_cast<double>(left) > 0.0)
            AddToSupply(self, Ftol(left));
        return;
    }

    const int32_t room = Minus(At<int32_t>(self, field::kGarbageCapacity), At<int32_t>(self, field::kGarbageSupply));
    if (room < 0 && static_cast<double>(left) < kMaxCellF) {
        // The supply is over capacity: take some of it back onto the cell.
        const int32_t excess = Minus(0, room);
        int32_t amount = excess;
        if (excess > produced)
            amount = static_cast<int16_t>(garbageTarget) != 0 ? produced : 1;
        if (static_cast<double>(amount) + static_cast<double>(left) > kMaxCellF)
            amount = Ftol(kMaxCellF - static_cast<double>(left));
        AddToSupply(self, -amount);
        const float amountF = static_cast<float>(amount);
        SetGarbage(self, x, z, Ftol(static_cast<double>(amountF) + static_cast<double>(left)));
        uncollected = static_cast<uint32_t>(
            Ftol((Unsigned(uncollected) + static_cast<double>(amountF)) + static_cast<double>(left)));
        return;
    }

    SetGarbage(self, x, z, Ftol(left));
    uncollected = static_cast<uint32_t>(Ftol(Unsigned(uncollected) + static_cast<double>(left)));
}

// Moves a tract's value toward its target by at most `maxStep`.
int32_t Approach(int32_t current, int32_t target, uint32_t maxStep)
{
    if (current < target) {
        uint32_t step = static_cast<uint32_t>(target - current);
        if (step > maxStep)
            step = maxStep;
        return current + static_cast<int32_t>(step);
    }
    uint32_t step = static_cast<uint32_t>(current - target);
    if (step > maxStep)
        step = maxStep;
    return current - static_cast<int32_t>(step);
}

void __fastcall SimulateTracts(void* self, void*)
{
    // Capacity left in the three building lists.
    const Capacity treatment = CapacityLeft(self, field::kWaterTreatmentList, 0xC8EBD131, 0x28ECF510);
    uint32_t treatmentLeft = treatment.left;
    const Capacity incinerators = CapacityLeftWithTotal(self, field::kIncineratorList, 0xA8EBCF0C, 0xE8ECF641,
                                                        field::kMonthlyIncineratorCapacity);
    uint32_t incineratorLeft = incinerators.left;
    const Capacity wasteToEnergy = CapacityLeftWithTotal(self, field::kWasteToEnergyList, 0x48EBD0B1, 0x88ECF76C,
                                                         field::kMonthlyWasteToEnergyCapacity);
    uint32_t wasteToEnergyLeft = wasteToEnergy.left;

    // Garbage from the neighbor deals: imports bring a tenth of their
    // capacity per call, exports take one.
    void* list = Virtual<void*>(NodeAllocator(), 0xC, 0xCu);
    static_cast<void**>(list)[0] = list;
    static_cast<void**>(list)[1] = list;
    void* const neighbors = At<void*>(self, kNeighbors);
    int32_t imported = 0;
    Virtual<uint32_t>(neighbors, 0x78, kDealGarbageImport, &list);
    for (void* node = NextNode(list); node != list; node = NextNode(node)) {
        void* const deal = NodeObject(node);
        const int32_t used = Virtual<int32_t>(deal, 0x50);
        const int32_t room = Minus(Virtual<int32_t>(deal, 0x28), used);
        if (static_cast<double>(room) > 0.0) {
            const int32_t amount =
                MagicRound(static_cast<double>(Virtual<int32_t>(deal, 0x28)) * static_cast<double>(0.1f));
            imported += amount;
            Virtual<uint32_t>(deal, 0x54, Virtual<int32_t>(deal, 0x50) + amount);
            At<int64_t>(self, kImportedThisMonth) += amount;
        }
    }
    Virtual<uint32_t>(neighbors, 0x78, kDealGarbageExport, &list);
    for (void* node = NextNode(list); node != list; node = NextNode(node)) {
        void* const deal = NodeObject(node);
        const int32_t used = Virtual<int32_t>(deal, 0x50);
        const int32_t room = Minus(Virtual<int32_t>(deal, 0x28), used);
        if (room > 0) {
            int32_t amount =
                MagicRound(static_cast<double>(Virtual<int32_t>(deal, 0x28)) * static_cast<double>(0.1f));
            if (room < amount)
                amount = room;
            Virtual<uint32_t>(deal, 0x54, Virtual<int32_t>(deal, 0x50) + amount);
            At<int32_t>(self, kExportDealCapacity) += amount;
        }
    }
    const float importedF = static_cast<float>(imported);
    AddToSupply(self, Ftol(static_cast<double>(importedF) -
                           DisposeGarbage(self, importedF, &incineratorLeft, &wasteToEnergyLeft)));

    // Factors from the ordinance simulator: per source grid, for traffic,
    // and per zone type.
    void* const ordinances = At<void*>(self, kOrdinances);
    auto factor = [ordinances](uint32_t id, int32_t index) {
        return static_cast<float>(Virtual<double>(ordinances, 0x64, id, index));
    };
    const float trafficFactor = factor(0x8A67E378, 0);
    const float airFactor = factor(0x08F79B8E, 0);
    const float waterFactor = factor(0xE8F79C8B, 0);
    const float garbageFactor = factor(0xE8F79C90, 0);
    struct ZoneFactors {
        float air, water, garbage;
    } zoneFactors[16];
    for (int32_t zone = 0; zone < 16; ++zone)
        zoneFactors[zone] = {factor(0x8A67E373, zone), factor(0x8A67E374, zone), factor(0x8A67E376, zone)};

    void* const zones = At<void*>(self, field::kZoneManager);
    int32_t nextX = static_cast<int32_t>(At<uint32_t>(self, kCursorX) / 2); // kept if the iterator has nothing
    int32_t nextZ = static_cast<int32_t>(At<uint32_t>(self, kCursorZ) / 2);
    // The count is read again after every tract, as the game does.
    for (uint32_t i = 0; i < At<uint32_t>(self, kTractsPerCall); ++i) {
        const uint32_t x = At<uint32_t>(self, kCursorX);
        const uint32_t z = At<uint32_t>(self, kCursorZ);
        auto source = [&](std::size_t grid) -> int16_t {
            const int16_t value = GetCellValue(Grid(self, grid), x, z);
            return value < 0 ? int16_t{0} : value;
        };
        const int16_t airSource = source(field::kValueGrids);
        const int16_t waterSource = source(field::kValueGrids + 4);
        const int16_t garbageSource = source(field::kValueGrids + 8);
        const int16_t radiationSource = source(field::kValueGrids + 12);
        int32_t air = Ftol(static_cast<double>(airSource) * static_cast<double>(airFactor));
        int32_t water = Ftol(static_cast<double>(waterSource) * static_cast<double>(waterFactor));
        int32_t garbage = Ftol(static_cast<double>(garbageSource) * static_cast<double>(garbageFactor));

        // The zones of the tract's four cells weight the targets.
        float airWeight = 0.0f;
        float waterWeight = 0.0f;
        float garbageWeight = 0.0f;
        int32_t zoned = 0;
        bool developed = false;
        for (uint32_t cell = 0; cell < 4; ++cell) {
            const uint32_t zone = Virtual<uint32_t>(zones, 0x30, x + (cell & 1), z + (cell >> 1));
            if (zone == kZoneLandfill)
                continue;
            if (zone >= 16) // the game would read past its table; zone types are 0 to 15
                continue;
            airWeight = static_cast<float>(static_cast<double>(airWeight) + zoneFactors[zone].air);
            ++zoned;
            waterWeight = static_cast<float>(static_cast<double>(waterWeight) + zoneFactors[zone].water);
            garbageWeight = static_cast<float>(static_cast<double>(garbageWeight) + zoneFactors[zone].garbage);
            if (zone != 0)
                developed = true;
        }
        if (developed)
            ++At<int32_t>(self, kZonedTracts);
        if (zoned > 0) {
            const double inverse = 1.0 / static_cast<double>(zoned);
            const double airScale = static_cast<double>(airWeight) * inverse;
            waterWeight = static_cast<float>(inverse * static_cast<double>(waterWeight));
            garbageWeight = static_cast<float>(inverse * static_cast<double>(garbageWeight));
            air = Ftol(static_cast<double>(static_cast<int16_t>(air)) * airScale);
            water = Ftol(static_cast<double>(static_cast<int16_t>(water)) * static_cast<double>(waterWeight));
            garbage = Ftol(static_cast<double>(static_cast<int16_t>(garbage)) * static_cast<double>(garbageWeight));
        }
        int32_t airTarget = static_cast<int16_t>(air);
        int32_t waterTarget = static_cast<int16_t>(water);

        // Per cell: traffic adds to the air target; garbage outside landfills
        // is settled.
        for (uint32_t cell = 0; cell < 4; ++cell) {
            const uint32_t cx = x + (cell & 1);
            const uint32_t cz = z + (cell >> 1);
            const bool landfill = Virtual<uint32_t>(zones, 0x30, cx, cz) == kZoneLandfill;
            const int32_t cellGarbage = ValueAt(self, kSlotGetGarbageValue, cx, cz);
            void* const trafficGrid = Virtual<void*>(At<void*>(self, kTraffic), 0x20);
            const uint8_t traffic = Virtual<uint8_t>(trafficGrid, 0x1C, cx, cz);
            airTarget = Ftol((static_cast<double>(traffic) * static_cast<double>(trafficFactor)) *
                                 static_cast<double>(Global<float>(addr::PollutionTuning_TrafficAirFactor)) +
                             static_cast<double>(airTarget));
            if (!landfill)
                SettleGarbage(self, cx, cz, garbage, zoned, cellGarbage, &incineratorLeft, &wasteToEnergyLeft);
        }

        // Water treatment takes its share of the water target.
        At<int32_t>(self, kSumWaterTarget) += waterTarget;
        if (treatmentLeft != 0 && waterTarget > 0) {
            int32_t treated = static_cast<int32_t>(treatmentLeft);
            const int32_t divisor = At<int32_t>(self, kTreatmentRatioDivisor);
            const int32_t dividend = At<int32_t>(self, kTreatmentRatioDividend);
            if (divisor > 0 && dividend > 0 && !(treatmentLeft > static_cast<uint32_t>(dividend))) {
                treated = MagicRound((static_cast<double>(dividend) / static_cast<double>(divisor)) *
                                     static_cast<double>(waterTarget));
                if (treated == 0)
                    treated = 1;
            }
            if (treated > waterTarget)
                treated = waterTarget;
            if (static_cast<uint32_t>(treated) > treatmentLeft)
                treated = static_cast<int32_t>(treatmentLeft);
            At<int32_t>(self, kSumWaterTreated) += treated;
            waterTarget -= treated;
            treatmentLeft -= static_cast<uint32_t>(treated);
        }
        At<int32_t>(self, kSumWaterLeft) += waterTarget;
        At<int32_t>(self, kSumAirTarget) += airTarget;
        At<int32_t>(self, kSumRadiationSource) += radiationSource;
        airTarget = airTarget < 0 ? 0 : (airTarget > kMaxCell ? kMaxCell : airTarget);
        waterTarget = waterTarget < 0 ? 0 : (waterTarget > kMaxCell ? kMaxCell : waterTarget);

        // Air and water move toward their targets.
        const int32_t airNow = ValueAt(self, kSlotGetAirValue, x, z);
        if (airNow != airTarget)
            Virtual<uint32_t>(self, kSlotSetAirValue, x, z,
                              static_cast<int16_t>(Approach(airNow, airTarget, Global<uint32_t>(addr::PollutionTuning_MaxAirStep))));
        const int32_t waterNow = ValueAt(self, kSlotGetWaterValue, x, z);
        if (waterNow != waterTarget) {
            int32_t water2 = Approach(waterNow, waterTarget, Global<uint32_t>(addr::PollutionTuning_MaxWaterStep));
            if (waterNow > waterTarget && water2 == 0) {
                // Water only drains to 0 when no neighbour holds more than 1.
                bool isolated = true;
                if (x > 0 && ValueAt(self, kSlotGetWaterValue, x - 1, z) > 1)
                    isolated = false;
                if (z > 0 && isolated && ValueAt(self, kSlotGetWaterValue, x, z - 1) > 1)
                    isolated = false;
                const uint32_t lastX = Virtual<uint32_t>(Grid(self, field::kValueGrids), 0x30) - 1;
                if (x < lastX && isolated && ValueAt(self, kSlotGetWaterValue, x + 1, z) > 1)
                    isolated = false;
                const uint32_t lastZ = Virtual<uint32_t>(Grid(self, field::kValueGrids), 0x34) - 1;
                if (z < lastZ && isolated && ValueAt(self, kSlotGetWaterValue, x, z + 1) > 1)
                    isolated = false;
                water2 = isolated ? 0 : 1;
            }
            Virtual<uint32_t>(self, kSlotSetWaterValue, x, z, static_cast<int16_t>(water2));
        }

        // Next tract; after the last, the walk starts over.
        void* const iterator = &At<char>(self, kTractIterator);
        if (At<uint8_t>(self, kTractIterator) == 2) {
            RestartTracts(iterator, 1);
            OnTractWalkRestart(self);
        }
        NextTract(iterator, &nextX, &nextZ);
        At<int32_t>(self, kCursorX) = nextX * 2;
        At<int32_t>(self, kCursorZ) = nextZ * 2;
    }

    // Supply over capacity is disposed of, or cut by at most a tuning limit.
    const int32_t room = Minus(At<int32_t>(self, field::kGarbageCapacity), At<int32_t>(self, field::kGarbageSupply));
    if (room < 0) {
        const uint32_t excess = 0u - static_cast<uint32_t>(room);
        int32_t cut = Ftol(DisposeGarbage(self, static_cast<float>(static_cast<int32_t>(excess)), &incineratorLeft,
                                          &wasteToEnergyLeft));
        const uint32_t limit = Global<uint32_t>(addr::PollutionTuning_GarbageExcessLimit);
        const uint32_t beyond = excess - static_cast<uint32_t>(cut);
        if (beyond > limit)
            cut += static_cast<int32_t>(beyond - limit);
        AddToSupply(self, -cut);
    }

    // The used capacity is spread back over the buildings.
    int32_t used = static_cast<int32_t>(treatment.left - treatmentLeft);
    if (used != 0)
        SpreadUsedCapacity(self, &At<char>(self, field::kWaterTreatmentList), 0xC8EBD131, 0x28ECF510, treatment.left,
                           &used, treatment.count);
    used = static_cast<int32_t>(incinerators.left - incineratorLeft);
    if (used != 0)
        SpreadUsedCapacity(self, &At<char>(self, field::kIncineratorList), 0xA8EBCF0C, 0xE8ECF641, incinerators.left,
                           &used, incinerators.count);
    used = static_cast<int32_t>(wasteToEnergy.left - wasteToEnergyLeft);
    if (used != 0)
        SpreadUsedCapacity(self, &At<char>(self, field::kWasteToEnergyList), 0x48EBD0B1, 0x88ECF76C,
                           wasteToEnergy.left, &used, wasteToEnergy.count);
    UpdateMaintenanceCosts(self, &At<char>(self, field::kWaterTreatmentList));
    UpdateMaintenanceCosts(self, &At<char>(self, field::kIncineratorList));
    UpdateMaintenanceCosts(self, &At<char>(self, field::kRecyclingList));

    ClearList(&list);
    Virtual<uint32_t>(NodeAllocator(), 0x10, list);
}

// --- DisposeGarbage (0x006E3820) --------------------------------------------

using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
const auto EvaluateCurve = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate);

int64_t Ftol64(double value)
{
    return static_cast<int64_t>(value);
}

// Takes up to `amount` of garbage, in order: export deals, waste-to-energy
// plants (counted as converted to energy), incinerators (counted as
// incinerated), landfills, abduction. Returns the amount taken, in ST0:
// until the landfill step the exact sum, from there on continuing from the
// sum as stored in a float.
double __fastcall DisposeGarbageOurs(void* self, void*, float amount, uint32_t* incineratorLeft,
                                     uint32_t* wasteToEnergyLeft)
{
    if (!(amount > 0.0f))
        return 0.0;
    double result = 0.0;
    float total = 0.0f;

    const int32_t exportRoom = Minus(At<int32_t>(self, kExportDealCapacity), At<int32_t>(self, kExportedThisMonth));
    if (exportRoom > 0) {
        double taken;
        if (static_cast<double>(amount) > static_cast<double>(exportRoom)) {
            taken = exportRoom;
            amount = static_cast<float>(static_cast<double>(amount) - static_cast<double>(exportRoom));
        } else {
            taken = amount;
            amount = 0.0f;
        }
        At<int64_t>(self, kExportedThisMonth) =
            Ftol64(static_cast<double>(At<int64_t>(self, kExportedThisMonth)) + taken);
        result = taken;
        total = static_cast<float>(taken);
    }
    if (!(amount > 0.0f))
        return result;

    // Burns all of the amount if the capacity is larger, else nothing, and
    // the capacity is used up either way.
    auto burn = [&](uint32_t* capacity, std::size_t counter) {
        const uint32_t left = *capacity;
        if (left == 0)
            return;
        double taken;
        if (Unsigned(left) > static_cast<double>(amount)) {
            int32_t whole = MagicRound(static_cast<double>(amount) - 0.5);
            if (static_cast<double>(amount) == static_cast<double>(whole + 1))
                whole += 1;
            taken = amount;
            *capacity = left - static_cast<uint32_t>(whole);
            amount = 0.0f;
        } else {
            taken = 0.0;
            *capacity = 0;
        }
        At<int64_t>(self, counter) = Ftol64(static_cast<double>(At<int64_t>(self, counter)) + taken);
        result = result + taken;
        total = static_cast<float>(result);
    };
    burn(wasteToEnergyLeft, kConvertedThisMonth);
    if (!(amount > 0.0f))
        return result;
    burn(incineratorLeft, kIncineratedThisMonth);
    if (!(amount > 0.0f))
        return result;

    // Landfills: room = curve(size) * scale - supply, at most what the
    // landfill service reports free; what is put there joins the supply.
    void* const landfills = At<void*>(self, kLandfills);
    int32_t used = 0;
    int32_t size = 0;
    Virtual<uint32_t>(landfills, 0x28, &used, &size);
    const int32_t free = Minus(size, used);
    const float scale = At<float>(self, kCapacityScale);
    const auto tiles = static_cast<float>(Virtual<int32_t>(landfills, 0x2C));
    const double curve = EvaluateCurve(Global<void*>(addr::PollutionTuning_LandfillCapacityCurve), tiles);
    const int32_t room = Minus(MagicRound(curve * static_cast<double>(scale)), At<int32_t>(self, field::kGarbageSupply));
    const int32_t fits = room < free ? room : free;
    if (fits <= 0) {
        result = total;
    } else {
        const auto fitsF = static_cast<float>(fits);
        if (static_cast<double>(amount) > static_cast<double>(fitsF)) {
            AddToSupply(self, fits);
            amount = static_cast<float>(static_cast<double>(amount) - static_cast<double>(fitsF));
            result = static_cast<double>(fitsF) + static_cast<double>(total);
        } else {
            AddToSupply(self, MagicRound(amount));
            result = static_cast<double>(amount) + static_cast<double>(total);
            amount = 0.0f;
        }
    }
    if (!(amount > 0.0f))
        return result;

    // Abduction, up to a monthly limit.
    const uint32_t abducted = At<uint32_t>(self, kAbductedThisMonth);
    const int32_t abductionRoom =
        Minus(Global<int32_t>(addr::PollutionTuning_MonthlyAbductionLimit), static_cast<int32_t>(abducted));
    if (abductionRoom <= 0)
        return result;
    const double taken = static_cast<double>(amount) > static_cast<double>(abductionRoom)
                             ? static_cast<double>(abductionRoom)
                             : static_cast<double>(amount);
    At<uint32_t>(self, kAbductedThisMonth) = static_cast<uint32_t>(Ftol(Unsigned(abducted) + taken));
    return result + taken;
}

using DisposeFn = double(__fastcall*)(void* self, void* edx, float amount, uint32_t* capacity, uint32_t* unused);
DisposeFn g_originalDispose = nullptr;

bool InstallDispose(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_DisposeGarbage);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &DisposeGarbageOurs, &g_originalDispose);
}

const bool g_registeredDispose = registry::Register({
    "simulators.pollution.disposal",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_DisposeGarbage,
    "Our DisposeGarbage: export deals, burning, landfills, abduction (docs/pollution.md)",
    false,
    InstallDispose,
});

// --- OnMonthPassed (0x006E7B60) ----------------------------------------------

using ListHelperFn = void(__thiscall*)(void* self, void* list, uint32_t property1, uint32_t property2);
const auto AgeBuildings = reinterpret_cast<ListHelperFn>(addr::cSC4PollutionSimulator_AgeBuildings);
const auto RollUsedCapacity = reinterpret_cast<ListHelperFn>(addr::cSC4PollutionSimulator_RollUsedCapacity);
const auto UpdateWasteToEnergyOutput = reinterpret_cast<ThisFn>(addr::cSC4PollutionSimulator_UpdateWasteToEnergyOutput);
const auto UpdateWorstTractsAndEffects = reinterpret_cast<ThisFn>(addr::cSC4PollutionSimulator_UpdateWorstTractsAndEffects);
const auto DestroyList = reinterpret_cast<ThisFn>(addr::SC4List_Destroy);

// Takes the first node off the list: unlinks it, releases its object and
// frees it.
void PopFront(void* list)
{
    void** const node = static_cast<void**>(NextNode(list));
    void** const next = static_cast<void**>(node[0]);
    void** const previous = static_cast<void**>(node[1]);
    previous[0] = next;
    next[1] = previous;
    if (void* const object = node[2])
        Virtual<uint32_t>(object, 0x8); // Release
    Virtual<uint32_t>(NodeAllocator(), 0x10, static_cast<void*>(node));
}

void __fastcall OnMonthPassed(void* self, void*)
{
    void* list = Virtual<void*>(NodeAllocator(), 0xC, 0xCu);
    static_cast<void**>(list)[0] = list;
    static_cast<void**>(list)[1] = list;

    // The garbage deals start the month unused; the export deals' capacity
    // counts toward the month's disposal.
    void* const neighbors = At<void*>(self, kNeighbors);
    Virtual<uint32_t>(neighbors, 0x78, kDealGarbageImport, &list);
    while (NextNode(list) != list) {
        Virtual<uint32_t>(NodeObject(NextNode(list)), 0x54, 0);
        PopFront(list);
    }
    ClearList(&list);
    Virtual<uint32_t>(neighbors, 0x78, kDealGarbageExport, &list);
    int64_t exports = 0;
    while (NextNode(list) != list) {
        void* const deal = NodeObject(NextNode(list));
        Virtual<uint32_t>(deal, 0x54, 0);
        exports += Virtual<int32_t>(deal, 0x28);
        PopFront(list);
    }

    // The month's disposal cost goes to the department budget.
    const int64_t disposed = static_cast<int64_t>(
        static_cast<uint64_t>(At<int64_t>(self, kConvertedThisMonth)) +
        static_cast<uint64_t>(At<int64_t>(self, field::kLandfilledThisMonth)) + static_cast<uint64_t>(exports) +
        static_cast<uint64_t>(At<int64_t>(self, kIncineratedThisMonth)));
    const int64_t cost = Ftol64(static_cast<double>(disposed) * static_cast<double>(At<float>(self, kDisposalCostPerUnit)));
    void* const lineItem = Virtual<void*>(At<void*>(self, kDepartment), 0x50, 0x14u);
    Virtual<uint32_t>(lineItem, 0x30, cost);

    // This month's figures become last month's.
    At<double>(self, field::kRecycledLastMonth) = At<double>(self, kRecycledThisMonth);
    At<uint32_t>(self, field::kAbductedPrevMonth) = At<uint32_t>(self, kAbductedThisMonth);
    At<int64_t>(self, field::kProducedLastMonth) = At<int64_t>(self, kProducedThisMonth);
    At<int64_t>(self, field::kImportedLastMonth) = At<int64_t>(self, kImportedThisMonth);
    At<int64_t>(self, field::kExportedLastMonth) = At<int64_t>(self, kExportedThisMonth);
    At<int64_t>(self, field::kIncineratedLastMonth) = At<int64_t>(self, kIncineratedThisMonth);
    At<int64_t>(self, field::kConvertedLastMonth) = At<int64_t>(self, kConvertedThisMonth);
    At<int64_t>(self, field::kLandfilledLastMonth) = At<int64_t>(self, field::kLandfilledThisMonth);
    At<int64_t>(self, kDisposalCostLastMonth) = cost;

    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    auto publish = [figures](uint32_t id, int32_t value) { Virtual<uint32_t>(figures, 0x30, id, value); };
    publish(0xEA110158, At<int32_t>(self, kProducedThisMonth));
    publish(0x8AB2F258, At<int32_t>(self, kImportedThisMonth));
    publish(0x6AB2F260, At<int32_t>(self, kExportedThisMonth));
    publish(0xEA11016F, static_cast<int32_t>(Ftol64(At<double>(self, kRecycledThisMonth))));
    publish(0xEA110197, At<int32_t>(self, kIncineratedThisMonth));
    publish(0xEA1101A1, At<int32_t>(self, kConvertedThisMonth));
    publish(0xEA1101AA, At<int32_t>(self, field::kLandfilledThisMonth));

    At<double>(self, kRecycledThisMonth) = 0.0;
    At<int64_t>(self, kProducedThisMonth) = 0;
    At<int64_t>(self, kImportedThisMonth) = 0;
    At<int64_t>(self, kExportedThisMonth) = 0;
    At<int64_t>(self, kIncineratedThisMonth) = 0;
    At<int64_t>(self, kConvertedThisMonth) = 0;
    At<int64_t>(self, field::kLandfilledThisMonth) = 0;
    At<uint32_t>(self, kAbductedThisMonth) = 0;
    At<int32_t>(self, kExportDealCapacity) = 0;

    // Monthly building upkeep, then the worst tracts and effects.
    void* const treatment = &At<char>(self, field::kWaterTreatmentList);
    void* const incinerators = &At<char>(self, field::kIncineratorList);
    AgeBuildings(self, treatment, 0x28ECF510, 0xC8EBD131);
    AgeBuildings(self, incinerators, 0xE8ECF641, 0xA8EBCF0C);
    AgeBuildings(self, &At<char>(self, field::kRecyclingList), 0, 0);
    RollUsedCapacity(self, treatment, 0x28ECF510, 0x08FCDB69);
    RollUsedCapacity(self, incinerators, 0xE8ECF641, 0xA8FCDBA8);
    UpdateWasteToEnergyOutput(self);
    RollUsedCapacity(self, &At<char>(self, field::kWasteToEnergyList), 0x88ECF76C, 0x28FCDBB0);
    UpdateWorstTractsAndEffects(self);
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), 0x7C);
    DestroyList(&list);
}

using MonthFn = void(__fastcall*)(void* self, void* edx);
MonthFn g_originalMonth = nullptr;

bool InstallMonth(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_OnMonthPassed);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &OnMonthPassed, &g_originalMonth);
}

const bool g_registeredMonth = registry::Register({
    "simulators.pollution.month",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_OnMonthPassed,
    "Our MonthPassed handler: monthly garbage figures and disposal cost (docs/pollution.md)",
    false,
    InstallMonth,
});

// --- Installation of SimulateTracts -----------------------------------------

using HandlerFn = void(__fastcall*)(void* self, void* edx);
HandlerFn g_original = nullptr;

bool Install(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_SimulateTracts);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &SimulateTracts, &g_original);
}

const bool g_registered = registry::Register({
    "simulators.pollution.tracts",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_SimulateTracts,
    "Our tract walk, the pollution simulation (message 0x66956814) (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim
